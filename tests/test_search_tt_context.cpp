#include "engine.h"

namespace {

bool check(const char * name, bool success) {
    if (!success) {
        std::cerr << name << "\n";
    }
    return success;
}

void prepare(search_t & search) {
    search.draw_contempt = 0;
    search.stack->in_check = search.brd.in_check();
    search.null_enabled = false;
    search.beta_pruning = false;
    search.lmr_enabled = false;
    search.ffp_enabled = false;
}

int run(const std::string & fen, const std::vector<U64> & history = {}) {
    search_t search(fen.c_str(), nullptr, nullptr, history);
    prepare(search);
    const int result = search.pvs(-score::INF, score::INF, 1);
    if (search.stop_all || search.brd.ply != 0 || search.brd.to_string() != fen) {
        std::cerr << "context test search aborted or failed to restore the board\n";
        std::exit(EXIT_FAILURE);
    }
    return result;
}

void set_option(const char * name, int value) {
    uci::in(std::string("setoption name ") + name + " value " + std::to_string(value));
}

// Cached score cutoffs apply only to non-PV windows, so reuse probes use one.
int probe(search_t & search, int expected) {
    return search.pvs(expected - 1, expected, 1, true);
}

std::pair<int, U64> run_configured(const std::string & fen, int expected = score::INVALID) {
    auto search = std::make_unique<search_t>(fen.c_str());
    prepare(*search);
    search->draw_contempt = options::get_value("DrawContempt");
    const int result = expected == score::INVALID ? search->pvs(-score::INF, score::INF, 1)
        : probe(*search, expected);
    if (search->stop_all || search->brd.ply != 0 || search->brd.to_string() != fen
            || search->stack != search->root_stack) {
        std::cerr << "option test search aborted or failed to restore state\n";
        std::exit(EXIT_FAILURE);
    }
    return {result, search->nodes};
}

bool test_rule50_buckets() {
    const std::string prefix = "5B2/3k1K2/8/2R4P/5P2/8/8/7r w - - ";
    auto first = std::make_unique<search_t>((prefix + "22 203").c_str());
    auto same = std::make_unique<search_t>((prefix + "29 203").c_str());
    auto next = std::make_unique<search_t>((prefix + "30 203").c_str());
    for (auto * search : {first.get(), same.get(), next.get()}) prepare(*search);
    bool success = check("clock buckets must not change board or repetition identity",
        first->brd.stack->tt_key == next->brd.stack->tt_key
        && first->brd.stack->repetition_key() == next->brd.stack->repetition_key());
    success &= check("score context must share within a bucket and differ across buckets",
        first->tt_context() == same->tt_context() && first->tt_context() != next->tt_context());
    move_t hint;
    hint.set(&first->brd, "h5h6");
    const int compact = hint.to_compact();
    trans_table::clear();
    trans_table::store(first->brd.stack->tt_key, 0, 0, 1, 123, compact,
        score::EXACT, first->tt_context(), {0, 99});
    int value = 0, move = 0, flag = 0;
    success &= check("different counts within a bucket must retain TT score reuse",
        probe(*same, 123) == 123 && same->nodes == 0);
    success &= check("another bucket must reject the score while retaining the move",
        !trans_table::retrieve(next->brd.stack->tt_key, 0, 1, value, move, flag,
            next->tt_context(), 30) && move == compact);
    const int warm = next->pvs(-score::INF, score::INF, 1);
    trans_table::clear();
    success &= check("cross-bucket warm search must match a fresh search",
        warm == run(prefix + "30 203") && next->nodes > 0);
    return success;
}

bool test_draw_contempt() {
    const int saved_contempt = options::get_value("DrawContempt");
    uci::silent(true);
    set_option("DrawContempt", 0);
    bool success = true;
    for (int before : {-12, 12}) {
        const std::string draw_fen = "7k/8/8/8/8/8/8/KR6 w - - 99 1";
        set_option("DrawContempt", before);
        trans_table::clear();
        const auto original = run_configured(draw_fen);
        set_option("DrawContempt", before);
        const auto unchanged = run_configured(draw_fen, original.first);
        success &= check("unchanged draw contempt must retain score reuse",
            unchanged.first == original.first && unchanged.second == 0);
        set_option("DrawContempt", -before);
        const auto warm = run_configured(draw_fen);
        trans_table::clear();
        const auto cold = run_configured(draw_fen);
        success &= check("changed draw contempt must retain context protection",
            original.first != cold.first && warm.first == cold.first && warm.second > 1);
    }
    set_option("DrawContempt", saved_contempt);
    return success;
}

bool test_fifty_move_context() {
    const std::string fresh = "7k/8/8/8/8/8/8/KR6 w - - 0 1";
    const std::string near_draw = "7k/8/8/8/8/8/8/KR6 w - - 99 1";
    trans_table::clear();
    const int winning_score = run(fresh);
    const int warm_draw = run(near_draw);
    trans_table::clear();
    const int draw_score = run(near_draw);
    const int warm_win = run(fresh);
    bool success = check("fixture must distinguish a rook advantage from the fifty-move draw",
        winning_score > 200 && draw_score == 0);
    success &= check("cached winning scores must not bypass a draw on the next move",
        warm_draw == draw_score);
    success &= check("cached draw scores must not erase a fresh rook advantage",
        warm_win == winning_score);
    return success;
}

bool test_history_context() {
    board_t board;
    board.init("7k/8/8/8/8/8/8/KR6 w - - 0 1");
    const char * cycle[] = {"a1a2", "h8h7", "a2a1", "h7h8"};
    std::vector<U64> history;
    for (int ply = 0; ply < 7; ++ply) {
        history.push_back(board.stack->repetition_key());
        move_t move;
        move.set(&board, cycle[ply % 4]);
        if (!board.valid(&move) || !board.legal(&move)) {
            std::cerr << "invalid history fixture\n";
            return false;
        }
        board.forward(&move);
    }
    const std::string fen = board.to_string();
    trans_table::clear();
    const int losing_score = run(fen);
    const int warm_draw = run(fen, history);
    trans_table::clear();
    const int draw_score = run(fen, history);
    const int warm_loss = run(fen);
    bool success = check("history fixture must offer Black a genuine threefold draw",
        losing_score < -200 && draw_score == 0);
    success &= check("cached losing scores must not hide a repetition escape",
        warm_draw == draw_score);
    success &= check("cached repetition scores must not invent an escape without history",
        warm_loss == losing_score);
    return success;
}

bool test_matching_context_and_move_hint() {
    const char * fen = "7k/8/8/8/8/8/8/KR6 w - - 0 1";
    trans_table::clear();
    search_t original(fen);
    prepare(original);
    const int expected = original.pvs(-score::INF, score::INF, 1);
    search_t matching(fen);
    prepare(matching);
    bool success = check("matching draw context must retain score cutoffs",
        probe(matching, expected) == expected && matching.nodes == 0);
    search_t different("7k/8/8/8/8/8/8/KR6 w - - 99 1");
    prepare(different);
    int score = 0, move = 0, flag = 0;
    success &= check("an out-of-range clock must retain the move but reject the score",
        !trans_table::retrieve(different.brd.stack->tt_key, 0, 1,
            score, move, flag, different.tt_context(), different.brd.stack->fifty_count)
        && move == original.stack->best_move.to_compact());
    for (int bound : {score::LOWERBOUND, score::UPPERBOUND, score::EXACT}) {
        trans_table::store(original.brd.stack->tt_key, 0, 0, 1, expected,
            move, bound, original.tt_context(), original.stack->tt_clocks);
        success &= check("all bound types must validate the clock range",
            !trans_table::retrieve(different.brd.stack->tt_key, 0, 1,
                score, move, flag, different.tt_context(), different.brd.stack->fifty_count)
            && trans_table::retrieve(original.brd.stack->tt_key, 0, 1,
                score, move, flag, original.tt_context(), original.brd.stack->fifty_count)
            && score == expected && flag == bound);
    }
    return success;
}

bool test_clock_range_reuse() {
    trans_table::clear();
    search_t original("7k/8/8/8/8/8/8/KR6 w - - 4 1");
    prepare(original);
    const int expected = original.pvs(-score::INF, score::INF, 1);
    search_t different("7k/8/8/8/8/8/8/KR6 w - - 8 1");
    prepare(different);
    bool success = check("ordinary clock differences must preserve score reuse",
        probe(different, expected) == expected && different.nodes == 0);
    // Reusing a child entry must carry its restrictions into the parent's entry.
    trans_table::clear();
    search_t child("7k/8/8/8/8/8/8/KR6 w - - 4 1");
    prepare(child);
    child.stack->tt_clocks = {0, 99};
    move_t quiet;
    quiet.set(&child.brd, "a1a2");
    child.forward(&quiet, false);
    child.nodes = 0; // the fixture move is setup, not search
    trans_table::store(child.brd.stack->tt_key, 0, 0, 1, -500, 0,
        score::EXACT, child.tt_context(), {3, 20});
    clock_range_t clocks = {0, 101};
    int score = 0, move = 0, flag = 0;
    success &= check("cached scores must return their clock validity range",
        trans_table::retrieve(child.brd.stack->tt_key, 0, 1, score, move, flag,
            child.tt_context(), 5, &clocks) && clocks.min == 3 && clocks.max == 20);
    for (int clock : {2, 21, 99}) {
        success &= check("out-of-range scores must not be reusable",
            !trans_table::retrieve(child.brd.stack->tt_key, 0, 1, score, move, flag,
                child.tt_context(), clock));
    }
    success &= check("a cached child score must carry its range into the search stack",
        probe(child, -500) == -500 && child.nodes == 0
        && child.stack->tt_clocks.min == 3 && child.stack->tt_clocks.max == 20);
    child.backward(&quiet);
    success &= check("cached child restrictions must reach the parent",
        child.stack->tt_clocks.min == 2 && child.stack->tt_clocks.max == 19);
    return success;
}

bool test_clock_range_propagation() {
    search_t search("7k/8/8/8/8/8/8/KR6 w - - 95 1");
    prepare(search);
    search.stack->tt_clocks = {0, 99};
    move_t move;
    move.set(&search.brd, "a1a2");
    search.forward(&move, false);
    search.stack->tt_clocks = {96, 98};
    search.backward(&move);
    bool success = check("quiet moves must translate child clock constraints by one",
        search.stack->tt_clocks.min == 95 && search.stack->tt_clocks.max == 97);
    search.forward();
    search.stack->tt_clocks = {95, 98};
    search.backward();
    success &= check("null moves must preserve the clock rather than advance it",
        search.stack->tt_clocks.min == 95 && search.stack->tt_clocks.max == 97);
    search_t pawn("7k/8/8/8/8/8/P7/KR6 w - - 95 1");
    prepare(pawn);
    pawn.stack->tt_clocks = {0, 99};
    move.set(&pawn.brd, "a2a3");
    pawn.forward(&move, false);
    pawn.stack->tt_clocks = {0, 2};
    pawn.backward(&move);
    success &= check("a pawn reset must disconnect child clock restrictions",
        pawn.stack->tt_clocks.min == 0 && pawn.stack->tt_clocks.max == 99);
    search_t near_draw("7k/8/8/8/8/8/8/KR6 w - - 99 1");
    prepare(near_draw);
    const int result = near_draw.pvs(-score::INF, score::INF, 1);
    success &= check("a real draw-dependent search must narrow its reusable clock range",
        result == 0 && near_draw.stack->tt_clocks.min == 99
        && near_draw.stack->tt_clocks.max == 99);
    return success;
}

bool test_non_pv_tt_bounds() {
    const char * fen = "7k/8/8/8/8/8/8/KR6 w - - 4 1";
    bool success = true;
    for (int bound : {score::EXACT, score::LOWERBOUND, score::UPPERBOUND}) {
        for (int value : {-123, 123}) {
            // Include equality at alpha and beta as well as values beyond them.
            for (int alpha : {value - 2, value - 1, value, value + 1}) {
                trans_table::clear();
                search_t search(fen);
                prepare(search);
                const U64 key = search.brd.stack->tt_key;
                const U64 context = search.tt_context();
                const auto * board_stack = search.brd.stack;
                search.stack->pv_count = 1;
                trans_table::store(key, 0, 0, 1, value, 0, bound, context, {3, 8});
                const int result = search.pvs(alpha, alpha + 1, 1);
                const bool cutoff = bound == score::EXACT
                    || (bound == score::LOWERBOUND && value >= alpha + 1)
                    || (bound == score::UPPERBOUND && value <= alpha);
                success &= check("non-PV TT cutoffs must respect bound direction and equality",
                    cutoff ? search.nodes == 0 && result == value : search.nodes > 0);
                success &= check("non-PV TT probes must restore board, hash and search state",
                    !search.stop_all && search.brd.to_string() == fen
                    && search.brd.ply == 0 && search.brd.stack == board_stack
                    && search.brd.stack->tt_key == key && search.stack == search.root_stack
                    && search.tt_context() == context);
                if (cutoff) {
                    success &= check("non-PV TT reuse must clear stale PV and retain clock constraints",
                        search.stack->pv_count == 0 && search.stack->tt_clocks.min == 3
                        && search.stack->tt_clocks.max == 8);
                }
            }
        }
    }
    return success;
}

bool test_non_pv_tt_compatibility() {
    const char * fen = "7k/8/8/8/8/8/8/KR6 w - - 4 1";
    bool success = true;
    for (int mismatch = 0; mismatch < 4; ++mismatch) {
        trans_table::clear();
        search_t search(fen);
        prepare(search);
        move_t hint;
        hint.set(&search.brd, "a1a2");
        const U64 key = search.brd.stack->tt_key;
        const U64 context = search.tt_context();
        trans_table::store(key ^ (mismatch == 3 ? U64(1) : 0), 0, 0,
            mismatch == 0 ? 1 : 2, -123, hint.to_compact(), score::EXACT,
            context ^ (mismatch == 1 ? U64(1) : 0), mismatch == 2
                ? clock_range_t{5, 8} : clock_range_t{3, 8});
        int value = 0, move = 0, bound = 0;
        success &= check("incompatible TT scores must retain only matching-key move hints",
            !trans_table::retrieve(key, 0, 2, value, move, bound, context, 4)
            && move == (mismatch == 3 ? 0 : hint.to_compact()));
        const int warm = search.pvs(0, 1, 2);
        const U64 warm_nodes = search.nodes;
        trans_table::clear();
        search_t cold(fen);
        prepare(cold);
        const int fresh = cold.pvs(0, 1, 2);
        success &= check("non-PV search must reject incompatible exact scores",
            warm_nodes > 0 && warm != -123 && (warm >= 1) == (fresh >= 1));
    }
    return success;
}

bool test_non_pv_tt_mates_and_clocks() {
    bool success = true;
    for (int value : {score::MATE - 10, -score::MATE + 10, int(score::TABLEBASE), -int(score::TABLEBASE)}) {
        trans_table::clear();
        search_t search("7k/8/8/8/8/8/8/KR6 w - - 4 1");
        prepare(search);
        move_t quiet;
        quiet.set(&search.brd, "a1a2");
        const U64 parent_context = search.tt_context();
        search.forward(&quiet, false);
        search.nodes = 0; // the fixture move is setup, not search
        // Store at another ply to exercise mate-distance decoding at this node.
        trans_table::store(search.brd.stack->tt_key, 0, 3, 1, value, 0,
            score::EXACT, search.tt_context(), {3, 8});
        const int expected = value >= score::TABLEBASE - MAX_PLY ? value + 2
            : value <= -score::TABLEBASE + MAX_PLY ? value - 2 : value;
        success &= check("non-PV exact reuse must decode mate distance at the probing ply",
            search.pvs(0, 1, 1) == expected && search.nodes == 0);
        search.backward(&quiet);
        success &= check("non-PV exact clock restrictions must reach the parent",
            search.stack->tt_clocks.min == 2 && search.stack->tt_clocks.max == 7
            && search.brd.ply == 0 && search.stack == search.root_stack
            && search.tt_context() == parent_context);
    }
    // Draw adjudication precedes cached bounds, including mate at clock 100.
    for (const char * fen : {"7k/8/8/8/8/8/8/KR6 w - - 100 1",
            "7k/6Q1/5K2/8/8/8/8/8 b - - 100 1"}) {
        trans_table::clear();
        search_t search(fen);
        prepare(search);
        trans_table::store(search.brd.stack->tt_key, 0, 0, 1,
            search.stack->in_check ? -score::MATE : 123, 0,
            score::EXACT, search.tt_context(), {0, 101});
        success &= check("non-PV exact reuse must not bypass draw or checkmate adjudication",
            search.pvs(0, 1, 1) == (search.stack->in_check ? -score::MATE : 0));
    }
    return success;
}

bool test_compact_moves() {
    const char * positions[] = {
        "rnbqkbnr/pppppppp/8/8/8/8/PPPPPPPP/RNBQKBNR w KQkq - 0 1",
        "r3k2r/8/8/8/8/8/8/R3K2R w KQkq - 0 1",
        "r3k2r/8/8/8/8/8/8/R3K2R b KQkq - 0 1",
        "7k/8/8/3pP3/8/8/8/K7 w - d6 0 1",
        "k7/8/8/8/3Pp3/8/8/7K b - d3 0 1",
        "1r5k/P7/8/8/8/8/8/K7 w - - 0 1",
        "k7/8/8/8/8/8/p7/1R5K b - - 0 1",
    };
    bool success = true;
    int castles = 0, ep_captures = 0, promotions = 0;
    trans_table_t table(1);
    for (const char * fen : positions) {
        search_t search(fen);
        const int count = search.init_root_moves();
        for (int i = 0; i < count; ++i) {
            move_t & original = search.root.moves[i].move;
            move_t decoded{};
            decoded.castle = CASTLE_Q;
            decoded.en_passant = true;
            table.store(search.brd.stack->tt_key, 63, 0, 255, -1234,
                original.to_compact(), score::EXACT, 0x123456789abcdef0ULL, {7, 101});
            int score = 0, move = 0, flag = 0;
            clock_range_t clocks{};
            success &= check("packed entries must preserve move, score, depth, context and clocks",
                table.retrieve(search.brd.stack->tt_key, 0, 255, score, move, flag,
                    0x123456789abcdef0ULL, 99, &clocks)
                && score == -1234 && flag == score::EXACT
                && clocks.min == 7 && clocks.max == 101);
            decoded.set_compact(&search.brd, move);
            success &= check(original.to_string().c_str(),
                decoded.to_int() == original.to_int() && decoded.score == score::INVALID);
            castles += original.castle != 0;
            ep_captures += original.en_passant;
            promotions += original.promotion != 0;
        }
    }
    success &= check("compact move fixtures must cover all special move kinds",
        castles == 4 && ep_captures == 2 && promotions == 16);
    move_t empty{};
    empty.set_compact(nullptr, 0);
    success &= check("an empty cached move must remain empty", empty.piece == EMPTY && empty.to_compact() == 0);
    return success;
}

bool test_table_boundaries() {
    bool success = true;
    for (int size_in_mb : {0, 1}) {
        trans_table_t table(size_in_mb);
        int score = 0, move = 0, flag = 0;
        success &= check("an empty table must not match the zero key",
            !table.retrieve(0, 0, 0, score, move, flag));
        const U64 context = 0xfedcba9876543210ULL;
        const U64 keys[] = {~U64(0), ~U64(0) - 1, ~U64(0) - 2, ~U64(0) - 3};
        for (int i = 0; i < 4; ++i) {
            table.store(keys[i], 63, 0, 255, i % 2 ? 31000 : -31000,
                65535 - i, score::EXACT, context, {7, 101});
        }
        for (int i = 0; i < 4; ++i) {
            success &= check("the final cluster must preserve all four full keys",
                table.retrieve(keys[i], 0, 255, score, move, flag, context, 100)
                && score == (i % 2 ? 31000 : -31000) && move == 65535 - i);
            success &= check("context comparison must retain its high bits",
                !table.retrieve(keys[i], 0, 255, score, move, flag,
                    context ^ (U64(1) << 63), 100));
        }
        table.clear();
        table.store(0, 0, 0, 1, 12, 0, score::EXACT);
        success &= check("a stored zero key must remain retrievable",
            table.retrieve(0, 0, 1, score, move, flag) && score == 12);
    }
    return success;
}

bool test_tt_score_and_hint_retention() {
    trans_table_t table(0);
    const U64 key = 123, context = 456;
    const int original = score::MATE - 12;
    table.store(key, 7, 2, 12, original, 100, score::EXACT, context, {3, 9});
    table.store(key, 7, 0, 1, -500, 101, score::UPPERBOUND, context, {3, 9});
    int value = 0, move = 0, bound = 0;
    clock_range_t clocks{};
    bool success = check("a shallow bound must update the hint without erasing a deeper score",
        table.retrieve(key, 2, 12, value, move, bound, context, 5, &clocks)
        && value == original && move == 101 && bound == score::EXACT
        && clocks.min == 3 && clocks.max == 9);
    // PV hint stores have no score context and must not replace score metadata.
    for (int hint : {0, 102}) {
        table.store(key, 8, 0, 1, 0, hint, 0);
        success &= check("move-only stores must preserve scored depth, context and clocks",
            table.retrieve(key, 2, 12, value, move, bound, context, 5, &clocks)
            && value == original && move == (hint ? hint : 101)
            && bound == score::EXACT && clocks.min == 3 && clocks.max == 9);
    }
    success &= check("retained mate scores must still decode at the probing ply",
        table.retrieve(key, 5, 12, value, move, bound, context, 5)
        && value == original - 3 && move == 102);
    success &= check("hint updates must not expose a score to an incompatible context",
        !table.retrieve(key, 2, 1, value, move, bound, 0, 5) && move == 102);
    table.store(key, 7, 2, 13, 77, 0, score::LOWERBOUND, context, {3, 9});
    success &= check("a deeper score store without a move must retain the matching-board hint",
        table.retrieve(key, 2, 13, value, move, bound, context, 5)
        && value == 77 && move == 102 && bound == score::LOWERBOUND);
    return success;
}

bool test_tt_replacement_domains() {
    bool success = true;
    for (int change = 0; change < 5; ++change) {
        trans_table_t table(0);
        table.store(123, 63, 0, 12, 321, 100, score::EXACT, 456, {3, 9});
        const int age = change == 1 ? 64 : 63;
        const U64 context = change == 2 ? 789 : 456;
        const clock_range_t clocks = change == 3 ? clock_range_t{4, 8} : clock_range_t{3, 9};
        const int depth = change == 4 ? 12 : 1;
        const int flag = change == 0 ? score::EXACT : score::LOWERBOUND;
        table.store(123, age, 0, depth, 77, 0, flag, context, clocks);
        int value = 0, move = 0, bound = 0;
        clock_range_t stored_clocks{};
        success &= check("exact, stale, changed-domain and equal-depth stores must accept fresh scores",
            table.retrieve(123, 0, depth, value, move, bound, context, 5, &stored_clocks)
            && value == 77 && move == 100 && bound == flag
            && stored_clocks.min == clocks.min && stored_clocks.max == clocks.max);
        if (change == 2) {
            success &= check("replacing a score context must not retain the old score domain",
                !table.retrieve(123, 0, 1, value, move, bound, 456, 5) && move == 100);
        }
        if (change == 3) {
            success &= check("replacement must not widen the incoming score's clock range",
                !table.retrieve(123, 0, 1, value, move, bound, context, 3) && move == 100);
        }
    }
    return success;
}

bool test_tt_empty_slots_and_age_wrap() {
    trans_table_t table(0); // All keys share the same four-entry cluster.
    table.store(1, 62, 0, 255, 101, 11, score::EXACT);
    table.store(2, 0, 0, 1, 102, 12, score::EXACT);
    int value = 0, move = 0, bound = 0;
    bool success = check("an empty slot must be used before evicting an aged entry",
        table.retrieve(1, 0, 255, value, move, bound) && value == 101 && move == 11);
    table.clear();
    for (int key = 1; key <= 4; ++key) {
        table.store(key, key == 1 ? 63 : 0, 0, key == 1 ? 255 : key - 1,
            100 + key, 10 + key, score::EXACT);
    }
    table.store(5, 64, 0, 2, 105, 15, score::EXACT);
    success &= check("age 63 must be one generation old at age 0",
        !table.retrieve(1, 0, 1, value, move, bound) && move == 0);
    for (int key = 2; key <= 5; ++key) {
        success &= check("age wrap must preserve current-generation collision entries",
            table.retrieve(key, 0, 1, value, move, bound)
            && value == 100 + key && move == 10 + key);
    }
    return success;
}

bool test_tt_move_only_collisions() {
    trans_table_t table(0);
    for (int key = 1; key <= 4; ++key) {
        table.store(key, 0, 0, 8, 100 + key, 10 + key, score::EXACT);
    }
    table.store(5, 0, 0, 1, 0, 15, 0);
    int value = 0, move = 0, bound = 0;
    bool success = check("a move-only insertion must not evict a scored collision",
        !table.retrieve(5, 0, 1, value, move, bound) && move == 0);
    for (int key = 1; key <= 4; ++key) {
        success &= check("all scored collision entries must survive a move-only insertion",
            table.retrieve(key, 0, 8, value, move, bound)
            && value == 100 + key && move == 10 + key);
    }
    table.clear();
    table.store(1, 0, 0, 255, 0, 11, 0);
    for (int key = 2; key <= 4; ++key) {
        table.store(key, 63, 0, 1, 100 + key, 10 + key, score::EXACT);
    }
    table.store(5, 0, 0, 1, 105, 0, score::EXACT);
    success &= check("a scored collision must replace a move-only entry before another score",
        !table.retrieve(1, 0, 1, value, move, bound)
        && table.retrieve(5, 0, 1, value, move, bound) && value == 105 && move == 0);
    for (int key = 2; key <= 4; ++key) {
        success &= check("replacing a move-only collision must preserve unrelated scored entries",
            table.retrieve(key, 0, 1, value, move, bound) && value == 100 + key);
    }
    return success;
}

bool test_context_restoration() {
    const char * fen = "rnbqkbnr/pppppppp/8/8/8/8/PPPPPPPP/RNBQKBNR w KQkq - 0 1";
    search_t search(fen);
    prepare(search);
    const U64 root_context = search.tt_context();
    move_t knight;
    knight.set(&search.brd, "g1f3");
    search.forward(&knight, false);
    const U64 knight_context = search.tt_context();
    search.forward();
    bool success = check("null moves must discard legal repetition history",
        search.stack->tt_history == 0 && search.tt_context() != knight_context);
    move_t after_null;
    after_null.set(&search.brd, "b1c3");
    search.forward(&after_null, false);
    success &= check("the artificial null position must remain outside legal history",
        search.stack->tt_history == 0);
    search.backward(&after_null);
    search.backward();
    success &= check("undoing null must restore draw context", search.tt_context() == knight_context);
    search.backward(&knight);
    success &= check("undoing a real move must restore draw context", search.tt_context() == root_context);
    move_t pawn;
    pawn.set(&search.brd, "e2e4");
    search.forward(&pawn, false);
    success &= check("irreversible moves must discard repetition context", search.stack->tt_history == 0);
    search_t pawn_root(search.brd.to_string().c_str());
    prepare(pawn_root);
    success &= check("root and interior nodes must distinguish future cycle policy",
        search.tt_context() != pawn_root.tt_context());
    search.backward(&pawn);
    success &= check("undoing an irreversible move must restore context", search.tt_context() == root_context);
    for (const char * text : {"g1f3", "g8f6", "f3g1", "f6g8"}) {
        move_t move;
        move.set(&search.brd, text);
        search.forward(&move, false);
    }
    std::vector<U64> history;
    for (int ply = 0; ply < search.brd.ply; ++ply) {
        history.push_back(search.brd._stack[ply].repetition_key());
    }
    search.reset_stack();
    search_t supplied(search.brd.to_string().c_str(), nullptr, nullptr, history);
    prepare(supplied);
    success &= check("rebasing must agree with equivalent supplied game history",
        search.tt_context() == supplied.tt_context());
    search_t opposite_contempt(search.brd.to_string().c_str(), nullptr, nullptr, history);
    prepare(opposite_contempt);
    opposite_contempt.draw_contempt = 12;
    success &= check("different draw preferences must not share scores",
        search.tt_context() != opposite_contempt.tt_context());
    search_t capture("7k/8/8/8/8/8/p7/KR6 w - - 17 1");
    prepare(capture);
    for (const char * text : {"b1c1", "h8g8"}) {
        move_t move;
        move.set(&capture.brd, text);
        capture.forward(&move, false);
    }
    const U64 before_capture = capture.tt_context();
    move_t king;
    king.set(&capture.brd, "a1a2");
    capture.forward(&king, false);
    success &= check("captures must discard old repetition context", capture.stack->tt_history == 0);
    capture.backward(&king);
    success &= check("undoing a capture must restore context", capture.tt_context() == before_capture);
    return success;
}

}

int main() {
    magic::init();
    trans_table::set_size(4);
    bool success = test_rule50_buckets();
    success &= test_fifty_move_context();
    success &= test_history_context();
    success &= test_matching_context_and_move_hint();
    success &= test_context_restoration();
    success &= test_clock_range_reuse();
    success &= test_clock_range_propagation();
    success &= test_non_pv_tt_bounds();
    success &= test_non_pv_tt_compatibility();
    success &= test_non_pv_tt_mates_and_clocks();
    success &= test_compact_moves();
    success &= test_table_boundaries();
    success &= test_tt_score_and_hint_retention();
    success &= test_tt_replacement_domains();
    success &= test_tt_empty_slots_and_age_wrap();
    success &= test_tt_move_only_collisions();
    success &= test_draw_contempt();
    return success ? EXIT_SUCCESS : EXIT_FAILURE;
}
