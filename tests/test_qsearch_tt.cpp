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

bool test_lazy_qsearch_check() {
    const char * fen = "4r2k/8/8/8/5q2/8/4N3/4K3 w - - 0 1";
    trans_table::clear();
    auto search = std::make_unique<search_t>(fen);
    prepare(*search);
    const int expected = evaluate(search.get());
    move_t hint;
    hint.set(&search->brd, "e2f4");
    trans_table::store(position_key(search->brd), 0, 0, 0, 0, hint.to_compact(), 0);
    const int value = search->qsearch(-score::INF, score::INF, 0);
    const auto & facts = search->brd._check_info[0];
    return check("illegal pinned captures must not query gives_check",
        value == expected && search->nodes == 0 && facts.check_questions == 0
        && !facts.checks_ready && search->brd.to_string() == fen
        && search->stack == search->root_stack);
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

// Qsearch and the main search share one table: qsearch stores at depth 0,
// which serves qsearch cutoffs but never a main-search depth.
bool test_shared_entries() {
    const char * fen = "7k/8/8/3q4/8/8/8/K2R4 w - - 0 1";
    trans_table::clear();
    auto search = std::make_unique<search_t>(fen);
    prepare(*search);
    trans_table::store(position_key(search->brd), 0, 0, 0, 12345, 0, score::EXACT);
    bool success = check("a depth-0 entry must serve a qsearch cutoff",
        search->qsearch(12344, 12345, 0) == 12345 && search->nodes == 0);
    success &= check("a depth-0 entry must not serve a main-search cutoff",
        search->pvs(12344, 12345, 1, true) != 12345 && search->nodes > 0);
    trans_table::clear();
    auto main = std::make_unique<search_t>(fen);
    prepare(*main);
    trans_table::store(position_key(main->brd), 0, 0, 3, -12345, 0, score::EXACT);
    success &= check("a main-search entry must serve a qsearch cutoff",
        main->qsearch(-12346, -12345, 0) == -12345 && main->nodes == 0);
    return success;
}

bool test_mates_and_terminals() {
    bool success = true;
    for (int value : {score::MATE - 10, -score::MATE + 10,
            int(score::TABLEBASE), -int(score::TABLEBASE)}) {
        trans_table::clear();
        auto search = std::make_unique<search_t>("7k/8/8/8/8/8/8/KR6 w - - 4 1");
        prepare(*search);
        move_t quiet;
        quiet.set(&search->brd, "a1a2");
        search->forward(&quiet, false);
        trans_table::store(position_key(search->brd), 0, 3, 0, value, 0, score::EXACT);
        success &= check("cached q mate/tablebase scores must decode at the probing ply",
            search->qsearch(0, 1, -1) == value + (value > 0 ? 2 : -2)
            && search->nodes == 1);
        search->backward(&quiet);
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
    trans_table::store(position_key(drawn->brd), 0, 0, 0, 12345, 0, score::EXACT);
    success &= check("draw adjudication must precede q cache cutoffs",
        drawn->qsearch(0, 1, -1) == 0);
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
        tt_hit_t hit;
        success &= check("cancelled qsearch must restore its board without caching an unfinished result",
            search->stop_all && search->brd.to_string() == fen && search->brd.ply == 0
            && search->stack == search->root_stack
            && !(trans_table::probe(position_key(search->brd), 0, 0, hit) && hit.bound));
        success &= check("cancellation after entering a child must retain its parent's raw evaluation",
            trans_table::probe(position_key(search->brd), 0, 0, hit)
            && hit.eval != score::INVALID && !hit.bound);
    }
    return success;
}

bool test_raw_eval_reuse() {
    bool success = true;
    for (int entry : {0, 1, 2, 3}) {
        for (int clock : {0, 12, 50}) {
            trans_table::clear();
            const std::string fen = "7k/8/8/8/8/8/8/KQ6 w - - "
                + std::to_string(clock) + " 1";
            auto search = std::make_unique<search_t>(fen.c_str());
            prepare(*search);
            nnue::accumulator_t fresh;
            const int raw = search->evaluation_network->evaluate(search->brd, fresh);
            constexpr int cached_raw = 1000;
            const bool cached = entry >= 2;
            if (entry) {
                // Include eval-only entries and bounds that cannot cut
                // this full-window search. A hit need not have a bound.
                trans_table::store(position_key(search->brd), 0, 0, 0, 0, 0,
                    entry == 3 ? score::UPPERBOUND : 0,
                    entry >= 2 ? cached_raw : score::INVALID);
            }
            const int expected = scale_network_score(cached ? cached_raw : raw,
                search->brd, clock);
            success &= check("qsearch must reuse raw TT eval independently of bounds",
                search->qsearch(-score::INF, score::INF, 0) == expected
                && search->nodes == 0);
            tt_hit_t stored;
            success &= check("a no-child result must retain the raw evaluation for later searches",
                trans_table::probe(position_key(search->brd), 0, clock, stored)
                && stored.eval == (cached ? cached_raw : raw));
            const auto & accumulator = search->evaluation_accumulators[0];
            success &= check("only a TTEval hit may skip inference",
                accumulator.ready[0] == !cached && accumulator.ready[1] == !cached);
            success &= check("raw eval reuse must preserve the board and search stack",
                search->brd.to_string() == fen && search->stack == search->root_stack
                && search->brd.ply == 0);
        }
    }
    return success;
}


bool test_unusable_hints() {
    struct fixture_t { const char * fen; const char * hint; bool valid; };
    bool success = true;
    for (const auto & fixture : {
            fixture_t{"7k/8/8/8/8/8/8/KQ6 w - - 0 1", "b1b1", false},
            fixture_t{"7k/8/8/8/8/8/8/KR6 w - - 0 1", "b1c2", false},
            fixture_t{"4r2k/8/8/8/8/8/4R3/4K3 w - - 0 1", "e2f2", true},
            fixture_t{"7k/8/8/r4pPK/8/8/8/8 w - f6 0 1", "g5f6", true}}) {
        trans_table::clear();
        auto reference = std::make_unique<search_t>(fixture.fen);
        prepare(*reference);
        const int expected = reference->qsearch(-score::INF, score::INF, 0);
        trans_table::clear();
        auto search = std::make_unique<search_t>(fixture.fen);
        prepare(*search);
        move_t hint;
        hint.set(&search->brd, fixture.hint);
        success &= check("TT hint fixtures must distinguish invalid geometry from king exposure",
            search->brd.valid(&hint) == fixture.valid
            && (!fixture.valid || !search->brd.legal(&hint)));
        const U64 key = position_key(search->brd);
        const U64 pawn_key = search->brd.stack->pawn_key;
        trans_table::store(key, 0, 0, 0, 0, hint.to_compact(), 0);
        success &= check("unusable TT hints must not change searched moves or the result",
            search->qsearch(-score::INF, score::INF, 0) == expected
            && search->nodes == reference->nodes);
        success &= check("rejecting an unusable TT hint must restore board, hashes and stacks",
            search->brd.to_string() == fixture.fen && position_key(search->brd) == key
            && search->brd.stack->pawn_key == pawn_key && search->brd.ply == 0
            && search->stack == search->root_stack);
        // A cached score is trusted independently of its move hint. Neither
        // invalid geometry nor exposed king safety authenticates the score.
        trans_table::clear();
        trans_table::store(key, 0, 0, 0, 12345, hint.to_compact(), score::EXACT);
        success &= check("a cached-score cutoff must not require its TT move to be playable",
            search->qsearch(12344, 12345, 0) == 12345
            && search->brd.to_string() == fixture.fen);
    }
    return success;
}

}


int main() {
    magic::init();
    trans_table::set_size(4);
    bool success = test_lazy_qsearch_check();
    success &= test_warm_qsearch();
    success &= test_shared_entries();
    success &= test_cancellation();
    success &= test_mates_and_terminals();
    success &= test_raw_eval_reuse();
    success &= test_unusable_hints();
    return success ? EXIT_SUCCESS : EXIT_FAILURE;
}
