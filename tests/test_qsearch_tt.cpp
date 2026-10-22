#include "engine.h"

namespace {

bool check(const char * message, bool success) {
    if (!success) {
        std::cerr << message << '\n';
    }
    return success;
}

void prepare(search_t & search) {
    search.draw_contempt = 0;
    search.stack->in_check = search.brd.in_check();
    search.tablebase_limit = 0;
}

bool test_warm_qsearch() {
    const char * fen = "7k/8/8/3q4/8/8/8/K2R4 w - - 0 1";
    trans_table::clear();
    auto reference = std::make_unique<search_t>(fen);
    prepare(*reference);
    const int expected = reference->qsearch(-score::INF, score::INF, -1);
    bool success = check("the cache fixture must search a queen capture beyond stand pat",
        reference->nodes > 0 && expected > evaluate(reference.get())
        && reference->stack->best_move.to_string() == "d1d5");
    trans_table::clear();
    auto cold = std::make_unique<search_t>(fen);
    prepare(*cold);
    const int value = cold->qsearch(expected - 1, expected, -1);
    auto warm = std::make_unique<search_t>(fen);
    prepare(*warm);
    const int cached = warm->qsearch(expected - 1, expected, -1);
    success &= check("a compatible warm qsearch bound must avoid searching the capture again",
        cold->nodes > 0 && value >= expected && warm->nodes == 0 && cached == value);
    success &= check("qsearch cache reuse must restore board, stack and cancellation state",
        warm->brd.to_string() == fen && warm->brd.ply == 0
        && warm->stack == warm->root_stack && !warm->stop_all);
    return success;
}

bool test_packed_modes() {
    trans_table_t table(0);
    bool success = true;
    for (auto mode : {tt_mode_t::main_search, tt_mode_t::quiescence_entry,
            tt_mode_t::quiescence_tactical}) {
        for (U64 key : {U64(0), ~U64(0)}) {
            table.clear();
            const U64 context = ~U64(0);
            table.store(key, 63, 3, 255, 31000, 65535, score::EXACT, context, {7, 101}, mode);
            // An independent hint update must preserve even the top mode bits.
            table.store(key, 0, 0, 1, 0, 65534, 0);
            for (auto requested : {tt_mode_t::main_search, tt_mode_t::quiescence_entry,
                    tt_mode_t::quiescence_tactical}) {
                int value = 0, move = 0, flag = 0;
                clock_range_t clocks{};
                const bool found = table.retrieve(key, 3, 255, value, move, flag,
                    context, 100, &clocks, requested);
                const bool compatible = mode == requested;
                success &= check("packed modes must preserve full keys, hints, depth, score and clocks",
                    found == compatible && move == 65534 && (!found
                        || (value == 31000 && flag == score::EXACT
                            && clocks.min == 7 && clocks.max == 101)));
            }
        }
    }
    return success;
}

bool test_mode_replacement() {
    trans_table_t table(0);
    table.store(123, 0, 0, 8, 111, 11, score::EXACT);
    table.store(123, 0, 0, 1, 222, 12, score::EXACT, 0, {0, 101}, tt_mode_t::quiescence_entry);
    int value = 0, move = 0, flag = 0;
    bool success = check("a current compatible main score must survive a q write while accepting its hint",
        table.retrieve(123, 0, 8, value, move, flag) && value == 111 && move == 12);
    table.store(123, 1, 0, 1, 333, 0, score::LOWERBOUND, 0, {0, 101}, tt_mode_t::quiescence_entry);
    success &= check("an aged main entry may refresh to the current q domain",
        !table.retrieve(123, 0, 1, value, move, flag) && move == 12
        && table.retrieve(123, 0, 1, value, move, flag, 0, 0, nullptr,
            tt_mode_t::quiescence_entry) && value == 333);
    success &= check("q entry and tactical modes must not share scores",
        !table.retrieve(123, 0, 1, value, move, flag, 0, 0, nullptr,
            tt_mode_t::quiescence_tactical) && move == 12);
    table.store(123, 1, 0, 1, 444, 0, score::UPPERBOUND);
    success &= check("a main write must supersede a q entry and retain its hint",
        table.retrieve(123, 0, 1, value, move, flag) && value == 444 && move == 12);
    table.store(123, 1, 0, 1, 555, 0, score::EXACT, 999, {0, 101}, tt_mode_t::quiescence_tactical);
    success &= check("an incompatible old main domain must not strand a new q result",
        table.retrieve(123, 0, 1, value, move, flag, 999, 0, nullptr,
            tt_mode_t::quiescence_tactical) && value == 555 && move == 12
        && !table.retrieve(123, 0, 1, value, move, flag, 999));
    return success;
}

bool test_qsearch_bounds_and_modes() {
    const char * fen = "7k/8/8/3q4/8/8/8/K2R4 w - - 4 1";
    bool success = true;
    for (int depth : {0, -1}) {
        const auto mode = depth == 0 ? tt_mode_t::quiescence_entry : tt_mode_t::quiescence_tactical;
        for (int flag : {score::EXACT, score::LOWERBOUND, score::UPPERBOUND}) {
            for (int value : {-12345, 12345}) {
                for (int alpha : {value - 2, value - 1, value, value + 1}) {
                    trans_table::clear();
                    auto search = std::make_unique<search_t>(fen);
                    prepare(*search);
                    search->stack->pv_count = 1;
                    trans_table::store(search->brd.stack->tt_key, 0, 0, 1, value, 0,
                        flag, search->tt_context(), {3, 8}, mode);
                    const int result = search->qsearch(alpha, alpha + 1, depth);
                    const bool cutoff = flag == score::EXACT
                        || (flag == score::LOWERBOUND && value >= alpha + 1)
                        || (flag == score::UPPERBOUND && value <= alpha);
                    success &= check("q TT cutoffs must obey bound direction and equality",
                        cutoff ? result == value && search->nodes == 0
                            : result != value || search->nodes > 0);
                    success &= check("q TT probes must clear stale PV and restore the board",
                        search->stack->pv_count == 0 && search->brd.to_string() == fen
                        && search->stack == search->root_stack && !search->stop_all);
                    if (cutoff) {
                        success &= check("q TT cutoffs must retain clock restrictions",
                            search->stack->tt_clocks.min == 3 && search->stack->tt_clocks.max == 8);
                    }
                }
            }
        }
    }
    return success;
}

bool test_qsearch_compatibility() {
    const char * fen = "7k/8/8/3q4/8/8/8/K2R4 w - - 4 1";
    bool success = true;
    for (int mismatch = 0; mismatch < 4; ++mismatch) {
        trans_table::clear();
        auto search = std::make_unique<search_t>(fen);
        prepare(*search);
        const auto mode = mismatch == 2 ? tt_mode_t::quiescence_entry : tt_mode_t::quiescence_tactical;
        trans_table::store(search->brd.stack->tt_key ^ (mismatch == 3 ? U64(1) : 0),
            0, 0, 1, 12345, 0, score::EXACT,
            search->tt_context() ^ (mismatch == 0 ? U64(1) : 0),
            mismatch == 1 ? clock_range_t{5, 8} : clock_range_t{3, 8}, mode);
        success &= check("q TT must reject incompatible contexts, clocks, modes and keys",
            search->qsearch(12344, 12345, -1) != 12345 && search->nodes > 0);
    }
    trans_table::clear();
    auto search = std::make_unique<search_t>(fen);
    prepare(*search);
    trans_table::store(search->brd.stack->tt_key, 0, 0, 4, 12345, 0,
        score::EXACT, search->tt_context(), {3, 8});
    success &= check("q TT must not substitute a main-search score for its own horizon",
        search->qsearch(12344, 12345, -1) != 12345 && search->nodes > 0);
    trans_table::clear();
    auto main = std::make_unique<search_t>(fen);
    prepare(*main);
    trans_table::store(main->brd.stack->tt_key, 0, 0, 1, 12345, 0,
        score::EXACT, main->tt_context(), {3, 8}, tt_mode_t::quiescence_tactical);
    success &= check("main search must not accept a q score as a full-depth result",
        main->pvs(-score::INF, score::INF, 1) != 12345 && main->nodes > 0);
    return success;
}

bool test_hint_domains() {
    struct hint_case_t {
        const char * fen;
        const char * move;
        int depth;
    };
    // Any valid hint is searched first: quiet, tactical, castling or evasion.
    const hint_case_t cases[] = {
        {"rnbqkbnr/pppppppp/8/8/8/8/PPPPPPPP/RNBQKBNR w KQkq - 0 1", "g1f3", -1},
        {"6k1/8/8/8/8/8/8/K2R4 w - - 0 1", "d1d8", 0},
        {"7k/8/8/3q4/8/8/8/K2R4 w - - 0 1", "d1d5", -1},
        {"7k/P7/8/8/8/8/8/K7 w - - 0 1", "a7a8n", -1},
        {"7k/8/8/3pP3/8/8/8/K7 w - d6 0 1", "e5d6", -1},
        {"4k3/8/8/8/8/8/8/4K2R w K - 0 1", "e1g1", 0},
        {"R6k/8/5K2/8/8/8/8/8 b - - 0 1", "h8h7", -1},
    };
    bool success = true;
    for (const auto & test : cases) {
        trans_table::clear();
        auto search = std::make_unique<search_t>(test.fen);
        prepare(*search);
        move_t hint;
        hint.set(&search->brd, test.move);
        success &= check("q TT hint fixture must be a legal move",
            search->brd.valid(&hint) && search->brd.legal(&hint));
        trans_table::store(search->brd.stack->tt_key, 0, 0, 1, 0, hint.to_compact(), 0);
        search->qsearch(-score::INF, score::INF, test.depth);
        success &= check("a valid q TT hint must be searched first",
            search->stack->move_list.returned_hints[0] == hint.to_compact() && search->nodes > 0);
        success &= check("q TT hinted searches must restore board and stack state",
            search->brd.to_string() == test.fen && search->stack == search->root_stack);
    }
    return success;
}

bool test_cancellation() {
    const char * fen = "7k/8/8/3q4/8/8/8/K2R4 w - - 0 1";
    bool success = true;
    {
        trans_table::clear();
        auto search = std::make_unique<search_t>(fen);
        prepare(*search);
        search->game.max_nodes = 1;
        search->qsearch(-score::INF, score::INF, -1);
        int value = 0, move = 0, flag = 0;
        success &= check("cancelled qsearch must restore its board without caching an unfinished result",
            search->stop_all && search->brd.to_string() == fen && search->brd.ply == 0
            && search->stack == search->root_stack
            && !trans_table::retrieve(search->brd.stack->tt_key, 0, 1, value, move, flag,
                search->tt_context(), 0, nullptr, tt_mode_t::quiescence_tactical));
    }
    return success;
}

bool test_mates_clocks_and_terminals() {
    bool success = true;
    for (int value : {score::MATE - 10, -score::MATE + 10,
            int(score::TABLEBASE), -int(score::TABLEBASE)}) {
        trans_table::clear();
        auto search = std::make_unique<search_t>("7k/8/8/8/8/8/8/KR6 w - - 4 1");
        prepare(*search);
        move_t quiet;
        quiet.set(&search->brd, "a1a2");
        const U64 root_context = search->tt_context();
        search->forward(&quiet, false);
        trans_table::store(search->brd.stack->tt_key, 0, 3, 1, value, 0,
            score::EXACT, search->tt_context(), {3, 8}, tt_mode_t::quiescence_tactical);
        success &= check("cached q mate/tablebase scores must decode at the probing ply",
            search->qsearch(0, 1, -1) == value + (value > 0 ? 2 : -2)
            && search->nodes == 1);
        search->backward(&quiet);
        success &= check("cached q clock constraints must propagate to the parent on undo",
            search->stack->tt_clocks.min == 2 && search->stack->tt_clocks.max == 7
            && search->brd.ply == 0 && search->tt_context() == root_context);
    }
    struct terminal_t { const char * fen; int score; };
    for (const auto & test : {terminal_t{"7k/8/8/8/8/8/8/KR6 w - - 100 1", 0},
            terminal_t{"7k/6Q1/5K2/8/8/8/8/8 b - - 100 1", -score::MATE},
            terminal_t{"7k/5Q2/6K1/8/8/8/8/8 b - - 0 1", 0}}) {
        for (int depth : {0, -1}) {
            trans_table::clear();
            auto search = std::make_unique<search_t>(test.fen);
            prepare(*search);
            success &= check("q TT integration must preserve fifty-move, mate and stalemate scores",
                search->qsearch(-1, 0, depth) == test.score);
            auto warm = std::make_unique<search_t>(test.fen);
            prepare(*warm);
            success &= check("warm q TT must retain certified terminal scores",
                warm->qsearch(-1, 0, depth) == test.score);
        }
    }
    trans_table::clear();
    auto drawn = std::make_unique<search_t>("7k/8/8/8/8/8/8/KR6 w - - 100 1");
    prepare(*drawn);
    trans_table::store(drawn->brd.stack->tt_key, 0, 0, 1, 12345, 0,
        score::EXACT, drawn->tt_context(), {0, 101}, tt_mode_t::quiescence_tactical);
    success &= check("draw adjudication must precede q cache cutoffs",
        drawn->qsearch(0, 1, -1) == 0);
    return success;
}

bool test_audit_isolation() {
#ifdef MAXIMA_NNUE_SEARCH_AUDIT
    auto * horizon = options::get_option("NNUEAuditHorizon");
    const int saved = horizon->value;
    bool success = true;
    for (int value : {1, 2, 0}) {
        horizon->value = value;
        auto search = std::make_unique<search_t>("7k/8/8/3q4/8/8/8/K2R4 w - - 0 1");
        success &= check("fixed and aligned audits must isolate q cache reuse",
            search->q_tt_enabled == (value == 0));
    }
    horizon->value = saved;
    trans_table::clear();
    return success;
#else
    return true;
#endif
}

bool test_option_snapshot_and_off() {
    const int saved = options::get_value("QsearchTT");
    uci::silent(true);
    uci::in("setoption name QsearchTT value true");
    auto before = std::make_unique<search_t>("7k/8/8/3q4/8/8/8/K2R4 w - - 0 1");
    trans_table::store(123, 0, 0, 1, 77, 0, score::EXACT);
    uci::in("setoption name QsearchTT value false");
    auto after = std::make_unique<search_t>(before->brd.to_string().c_str());
    prepare(*after);
    int value = 0, move = 0, flag = 0;
    bool success = check("changing QsearchTT must snapshot worker flags and clear incompatible cached results",
        before->q_tt_enabled && !after->q_tt_enabled
        && !trans_table::retrieve(123, 0, 1, value, move, flag));
    trans_table::store(after->brd.stack->tt_key, 0, 0, 1, 12345, 0,
        score::EXACT, after->tt_context(), {0, 99}, tt_mode_t::quiescence_tactical);
    success &= check("QsearchTT off must skip score probes",
        after->qsearch(12344, 12345, -1) != 12345 && after->nodes > 0);
    trans_table::clear();
    after->nodes = 0;
    after->qsearch(-score::INF, score::INF, -1);
    success &= check("QsearchTT off must skip bound stores",
        !trans_table::retrieve(after->brd.stack->tt_key, 0, 1, value, move, flag,
            after->tt_context(), 0, nullptr, tt_mode_t::quiescence_tactical));
    move_t hint;
    hint.set(&after->brd, "d1d5");
    trans_table::store(after->brd.stack->tt_key, 0, 0, 1, 0, hint.to_compact(), 0);
    after->qsearch(-score::INF, score::INF, -1);
    success &= check("QsearchTT off must also skip move-hint probes",
        after->stack->move_list.returned_hints[0] == 0);
    uci::in(std::string("setoption name QsearchTT value ") + (saved ? "true" : "false"));
    uci::silent(false);
    return success;
}

}

int main() {
    magic::init();
    trans_table::set_size(4);
    bool success = test_warm_qsearch();
    success &= test_packed_modes();
    success &= test_mode_replacement();
    success &= test_qsearch_bounds_and_modes();
    success &= test_qsearch_compatibility();
    success &= test_hint_domains();
    success &= test_cancellation();
    success &= test_mates_clocks_and_terminals();
    success &= test_audit_isolation();
    success &= test_option_snapshot_and_off();
    return success ? EXIT_SUCCESS : EXIT_FAILURE;
}
