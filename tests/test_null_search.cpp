#include "engine.h"

namespace {

bool check(bool ok, const char * message) {
    if (!ok) {
        std::cerr << message << '\n';
    }
    return ok;
}

// The virtual PVS boundary lets the test observe artificial branches and
// supply reserved scores without requiring a particular tablebase installation.
class null_probe_t : public search_t {
public:
    using search_t::search_t;
    int entry_ply = 0;
    int null_calls = 0;
    int verification_calls = 0;
    int consecutive_nulls = 0;
    int verification_nulls = 0;
    int null_depth = 0, null_alpha = 0, null_beta = 0, verification_depth = 0;
    int null_value = 100;
    int verification_value = 100;
    bool inject_null = true;
    bool inject_verification = true;
    bool in_verification = false;
    bool verification_excluded = true;
    bool interrupt_null = false;
    bool interrupt_verification = false;
    bool null_cut_node = false, verification_cut_node = false;
    bool dirty_verification_frame = false;
    search_stack_t verification_parent{};

    int pvs(int alpha, int beta, int depth, bool cut_node = false) override {
        const bool artificial = brd.ply > 0 && !(stack - 1)->current_move.piece;
        if (artificial) {
            ++null_calls;
            null_cut_node = cut_node;
            null_depth = depth;
            null_alpha = alpha;
            null_beta = beta;
            consecutive_nulls += brd.ply > 1 && !(stack - 2)->current_move.piece;
            verification_nulls += in_verification;
            if (inject_null) {
                stop_all = interrupt_null;
                return -null_value;
            }
        }
        const bool verification = brd.ply == entry_ply;
        if (verification) {
            ++verification_calls;
            verification_depth = depth;
            verification_cut_node = cut_node;
            verification_excluded &= !null_enabled;
            if (inject_verification) {
                if (dirty_verification_frame) {
                    verification_parent = *stack;
                    stack->tt_history ^= U64(123);
                    stack->tt_key ^= U64(456);
                    stack->repetition_start = 77;
                    stack->current_move.set(KING[brd.us()], a1, a2);
                    stack->best_move = stack->current_move;
                    stack->killer[0] = stack->current_move;
                    stack->pv_count = 1;
                    stack->eval_result = 777;
                    stack->tt_clocks = {0, 80};
                }
                stack->improving = true; // Same-position helpers must not leak their trend.
                stop_all = interrupt_verification;
                return verification_value;
            }
        }
        const bool previous = in_verification;
        in_verification |= verification;
        const int result = search_t::pvs(alpha, beta, depth, cut_node);
        in_verification = previous;
        return result;
    }

    int run(int alpha, int beta, int depth, bool cut_node = false) {
        entry_ply = brd.ply;
        return search_t::pvs(alpha, beta, depth, cut_node);
    }
};

void prepare(search_t & search) {
    trans_table::clear();
    search.draw_contempt = 0;
    search.stack->in_check = search.brd.in_check();
    search.null_enabled = true;
    search.adaptive_null_enabled = false;
    search.null_verify = true;
    search.beta_pruning = false;
    search.tablebase_limit = 0;
}

bool restored(search_t & search, search_t & original) {
    return check(search.brd.to_string() == original.brd.to_string()
        && search.brd.stack->tt_key == original.brd.stack->tt_key
        && search.stack->tt_history == original.stack->tt_history
        && search.stack->repetition_start == original.stack->repetition_start
        && search.brd.ply == original.brd.ply && search.stack == search.root_stack
        && search.null_enabled, "null search failed to restore state");
}

bool test_cutoffs() {
    bool ok = true;
    for (const char * fen : {"7k/7p/8/8/8/8/P7/KR6 w - - 0 1",
                            "kr6/p7/8/8/8/8/7P/7K b - - 0 1"}) {
        auto original = std::make_unique<search_t>(fen);
        for (bool verify : {false, true}) {
            for (int value : {100, score::TABLEBASE - MAX_PLY - 1,
                              score::TABLEBASE - MAX_PLY, int(score::TABLEBASE),
                              score::MATE - 1, -int(score::TABLEBASE)}) {
                auto search = std::make_unique<null_probe_t>(fen);
                prepare(*search);
                search->null_verify = verify;
                search->null_value = value;
                const int result = search->run(-1, 0, 7);
                ok &= check(search->null_calls > 0, "expected null attempt");
                const bool ordinary = value >= 0 && value < score::TABLEBASE - MAX_PLY;
                ok &= check(search->verification_calls == (ordinary && verify ? 1 : 0),
                    "unexpected verification of reserved/failed null score");
                ok &= check(!score::is_mate(result) && std::abs(result) < score::TABLEBASE - MAX_PLY,
                    "artificial decisive score escaped null pruning");
                if (ordinary) {
                    ok &= check(result == 100 || (!verify && result == value), "ordinary null cutoff lost");
                }
                ok &= check(search->verification_excluded, "verification leaves null pruning enabled");
                ok &= check(!search->stop_all, "unexpected interruption");
                ok &= restored(*search, *original);
            }
        }
        for (bool during_verification : {false, true}) {
            auto search = std::make_unique<null_probe_t>(fen);
            prepare(*search);
            search->interrupt_null = !during_verification;
            search->interrupt_verification = during_verification;
            ok &= check(search->run(-1, 0, 7) == -1 && search->stop_all,
                "interrupted null/verification returned a cutoff");
            ok &= check(search->verification_calls == int(during_verification), "search continued after cancellation");
            ok &= restored(*search, *original);
        }
        auto failed = std::make_unique<null_probe_t>(fen);
        prepare(*failed);
        failed->verification_value = -1;
        ok &= check(failed->run(-1, 0, 7) >= 0 && failed->nodes > 1,
            "failed verification did not resume legal search");
        ok &= check(failed->verification_excluded, "failed verification did not exclude null");
        ok &= restored(*failed, *original);
    }
    return ok;
}

bool test_node_context() {
    bool ok = true;
    for (bool cut_node : {false, true}) {
        for (bool cancel : {false, true}) {
            auto search = std::make_unique<null_probe_t>("7k/7p/8/8/8/8/P7/KR6 w - - 0 1");
            prepare(*search);
            search->interrupt_verification = cancel;
            search->run(-1, 0, 7, cut_node);
            ok &= check(search->null_calls == 1 && search->null_cut_node == !cut_node
                && search->verification_calls == 1 && search->verification_cut_node == cut_node,
                "null and same-position verification cut-node contexts differ");
            ok &= check(search->stop_all == cancel && !search->stack->improving
                && search->null_enabled && search->stack == search->root_stack && search->brd.ply == 0,
                "null verification leaked its trend or failed cancellation restoration");
        }
    }
    return ok;
}

bool test_verification_frame_restore() {
    bool ok = true;
    for (bool cancel : {false, true}) {
        auto search = std::make_unique<null_probe_t>("7k/7p/8/8/8/8/P7/KR6 w - - 0 1");
        prepare(*search);
        search->dirty_verification_frame = true;
        search->interrupt_verification = cancel;
        search->run(-1, 0, 7);
        const auto & before = search->verification_parent;
        const auto & after = *search->stack;
        ok &= check(search->verification_calls == 1 && search->stop_all == cancel
            && after.tt_history == before.tt_history && after.tt_key == before.tt_key
            && after.repetition_start == before.repetition_start
            && after.current_move.piece == before.current_move.piece
            && after.best_move.piece == before.best_move.piece
            && after.killer[0].piece == before.killer[0].piece
            && after.pv_count == before.pv_count && after.eval_result == before.eval_result
            && after.tt_clocks.min == 0 && after.tt_clocks.max == 80,
            "null verification must restore the entire caller frame and merge clock restrictions");
    }
    return ok;
}

bool test_exclusions() {
    bool ok = true;
    const char * fen = "7k/7p/8/8/8/8/P7/KR6 w - - 0 1";
    auto original = std::make_unique<search_t>(fen);
    const char * both_pieces = "kr6/p7/8/8/8/8/P7/KR6 w - - 0 1";
    auto null_original = std::make_unique<search_t>(both_pieces);
    auto after_null = std::make_unique<null_probe_t>(both_pieces);
    prepare(*after_null);
    after_null->forward();
    after_null->run(-1001, -1000, 5);
    ok &= check(after_null->consecutive_nulls == 0, "consecutive null move allowed");
    after_null->backward();
    ok &= restored(*after_null, *null_original);

    auto verification = std::make_unique<null_probe_t>(fen);
    prepare(*verification);
    verification->inject_verification = false;
    verification->run(-1001, -1000, 11);
    ok &= check(verification->verification_calls == 1 && verification->verification_excluded
        && verification->verification_nulls == 0, "null move inside verification subtree");
    ok &= restored(*verification, *original);

    for (int beta : {-int(score::TABLEBASE), -score::TABLEBASE + MAX_PLY,
                     score::TABLEBASE - MAX_PLY, int(score::TABLEBASE)}) {
        auto search = std::make_unique<null_probe_t>(fen);
        prepare(*search);
        search->run(beta - 1, beta, 1);
        ok &= check(search->null_calls == 0, "null pruning in decisive-score window");
        ok &= restored(*search, *original);
    }
    return ok;
}

bool test_pawn_endgames() {
    bool ok = true;
    // Opposition with the pawn blocked by the enemy king: a pass changes
    // who must yield ground. Pawn-only zugzwang candidates must never use NMP.
    for (const char * fen : {"8/8/8/8/1k6/1P6/1K6/8 w - - 0 1",
                            "8/1k6/1p6/1K6/8/8/8/8 b - - 0 1"}) {
        auto original = std::make_unique<search_t>(fen);
        for (int depth : {1, 3, 5}) {
            auto reference = std::make_unique<search_t>(fen);
            prepare(*reference);
            reference->null_enabled = false;
            reference->ffp_enabled = false;
            reference->lmr_enabled = false;
            const int expected = reference->pvs(-score::INF, score::INF, depth);
            for (int alpha : {expected - 1, expected}) {
                auto search = std::make_unique<null_probe_t>(fen);
                prepare(*search);
                search->ffp_enabled = false;
                search->lmr_enabled = false;
                const int result = search->run(alpha, alpha + 1, depth);
                ok &= check(search->null_calls == 0, "pawn endgame allowed null pruning");
                ok &= check(alpha < expected ? result >= alpha + 1 : result <= alpha,
                    "pawn endgame bound disagrees with legal search");
                ok &= restored(*search, *original);
            }
        }
    }
    return ok;
}

bool test_static_cushion() {
    bool ok = true;
    for (const char * fen : {"7k/7p/8/8/8/8/P7/KR6 w - - 0 1",
                            "kr6/p7/8/8/8/8/7P/7K b - - 0 1"}) {
        auto original = std::make_unique<search_t>(fen);
        for (int surplus : {0, 49, 50, 51}) {
            auto search = std::make_unique<null_probe_t>(fen);
            prepare(*search);
            const int beta = evaluate(search.get()) - surplus;
            search->null_value = beta;
            search->run(beta - 1, beta, 1);
            const bool eligible = surplus >= 50;
            ok &= check(search->null_calls == int(eligible), "null static cushion differs");
            if (eligible) {
                ok &= check(search->null_depth == -3 && search->null_alpha == -beta
                    && search->null_beta == -beta + 1, "null depth or zero window differs");
            }
            ok &= restored(*search, *original);
        }
    }
    return ok;
}

// Legal children return a fixed fail-low; only the parent policy and the
// artificial/verification calls are under test, independent of move ordering.
class adaptive_probe_t : public null_probe_t {
public:
    using null_probe_t::null_probe_t;
    int verified_moves = 0;
    int pvs(int alpha, int beta, int depth, bool cut_node = false) override {
        if (brd.ply > entry_ply && (stack - 1)->current_move.piece) {
            verified_moves += verification_mode == verification_mode_t::null_move;
            ++nodes;
            return 0;
        }
        return null_probe_t::pvs(alpha, beta, depth, cut_node);
    }
};

bool test_adaptive_policy() {
    bool ok = true;
    constexpr auto start = "rnbqkbnr/pppppppp/8/8/8/8/PPPPPPPP/RNBQKBNR w KQkq - 0 1";
    // Both colors, all supported depths, monotonic surplus/depth behavior.
    for (const char * fen : {start, "rnbqkbnr/pppppppp/8/8/8/8/PPPPPPPP/RNBQKBNR b KQkq - 0 1"}) {
        auto search = std::make_unique<search_t>(fen);
        search->adaptive_null_enabled = true;
        for (int depth = 1; depth <= MAX_PLY; ++depth) {
            int previous = depth;
            for (int surplus : {-100, 0, 173, 174, 347, 348, 695, 696, 2000}) {
                const int child = search->null_search_depth(depth, surplus, 0);
                ok &= check(child >= 0 && child < depth && child <= previous, "null child depth must be bounded and monotonic in surplus");
                previous = child;
            }
            search->adaptive_null_enabled = false;
            ok &= check(search->null_search_depth(depth, 2000, 0) == depth - 4, "disabled policy must retain the legacy depth");
            search->adaptive_null_enabled = true;
        }
    }
    // Tactical hints must be valid AND legal. A pinned capture is no evidence.
    auto hint = std::make_unique<search_t>("4r2k/8/8/8/8/8/3nR3/4K3 w - - 0 1");
    hint->adaptive_null_enabled = true;
    const int base = hint->null_search_depth(18, 0, 0);
    hint->stack->tt_move.set(&hint->brd, "e2d2");
    ok &= check(hint->brd.valid(&hint->stack->tt_move) && !hint->brd.legal(&hint->stack->tt_move), "pinned capture fixture must be pseudo-legal but illegal");
    ok &= check(hint->null_search_depth(18, 0, 0) == base, "quiet or illegal hint must not increase reduction");
    hint->stack->tt_move.set(&hint->brd, "e2e8");
    ok &= check(hint->brd.valid(&hint->stack->tt_move) && hint->brd.legal(&hint->stack->tt_move), "legal capture fixture must resolve the pin");
    ok &= check(hint->null_search_depth(18, 0, 0) == base - 1, "legal tactical hint must increase reduction");
    hint->stack->tt_move.set(&hint->brd, "e2e3");
    ok &= check(hint->null_search_depth(18, 0, 0) == base, "quiet or illegal hint must not increase reduction");
    hint->stack->tt_move.set(WROOK, a1, a8, BROOK, 0);
    ok &= check(hint->null_search_depth(18, 0, 0) == base, "quiet or illegal hint must not increase reduction");
    auto promotion = std::make_unique<search_t>("7k/P7/8/8/8/8/8/7K w - - 0 1");
    promotion->adaptive_null_enabled = true;
    const int promotion_base = promotion->null_search_depth(18, 0, 0);
    promotion->stack->tt_move.set(&promotion->brd, "a7a8n");
    ok &= check(promotion->null_search_depth(18, 0, 0) == promotion_base - 1, "quiet promotion must count as a tactical hint");

    for (const char * text : {"g1f3", "b1c3"}) {
        for (int depth : {1, 2, 3, 5, 6, 11, 12, 18}) {
            for (bool cut : {false, true}) {
                for (int outcome = 0; outcome < 4; ++outcome) {
                    auto probe = std::make_unique<adaptive_probe_t>(start);
                    prepare(*probe);
                    probe->adaptive_null_enabled = true;
                    move_t move;
                    move.set(&probe->brd, text);
                    probe->forward(&move, false);
                    const auto board = probe->brd.to_string();
                    const auto key = probe->brd.stack->tt_key;
                    auto * frame = probe->stack;
                    probe->null_value = outcome == 3 ? score::TABLEBASE : 100;
                    probe->verification_value = outcome == 1 ? -1001 : 100;
                    probe->interrupt_null = outcome == 2;
                    probe->run(-1001, -1000, depth, cut);
                    const bool eligible = depth > 2 && cut;
                    ok &= check(probe->null_calls == int(eligible), "adaptive eligibility differs at depth/cut boundary");
                    ok &= check(probe->verification_calls == int(eligible && depth >= 12 && outcome < 2), "general verification must obey depth and fail-high guards");
                    if (eligible) {
                        ok &= check(probe->null_depth == probe->null_search_depth(depth, evaluate(probe.get()), -1000), "artificial search did not use the selected child depth");
                        ok &= check(!probe->null_cut_node && probe->null_alpha == 1000 && probe->null_beta == 1001, "artificial search must retain its zero window and opposite context");
                    }
                    if (probe->verification_calls) {
                        ok &= check(probe->verification_depth >= 1 && probe->verification_excluded, "verification must search real moves with null disabled");
                        ok &= check(probe->verification_depth == std::max(1, probe->null_depth), "general verification depth differs from the null probe");
                    }
                    ok &= check(probe->stop_all == (eligible && outcome == 2), "cancellation must stop the artificial branch");
                    ok &= check(probe->brd.to_string() == board && probe->brd.stack->tt_key == key
                        && probe->stack == frame && probe->null_enabled, "adaptive null search must restore board, hash and frame");
                    probe->backward();
                }
            }
        }
    }
    // The depth/improving margin uses the nearest same-side static value.
    for (bool improving : {false, true}) {
        for (int surplus : {0, 47, 48, 49}) {
            auto probe = std::make_unique<adaptive_probe_t>(start);
            prepare(*probe);
            probe->adaptive_null_enabled = true;
            for (const char * text : {"g1f3", "g8f6"}) {
                move_t move;
                move.set(&probe->brd, text);
                probe->forward(&move, false);
            }
            const int eval = evaluate(probe.get());
            (probe->stack - 2)->eval_result = probe->stack->eval_result + (improving ? -1 : 1);
            const int beta = eval - surplus;
            probe->null_value = beta;
            probe->run(beta - 1, beta, 6, true);
            ok &= check(probe->null_calls == int(improving || surplus >= 48), "depth and improving margin boundary differs");
        }
    }
    for (bool verify : {false, true}) {
        auto probe = std::make_unique<adaptive_probe_t>(start);
        prepare(*probe);
        probe->adaptive_null_enabled = true;
        probe->null_verify = verify;
        move_t move;
        move.set(&probe->brd, "g1f3");
        probe->forward(&move, false);
        probe->run(-1001, -1000, 12, true);
        ok &= check(probe->null_calls == 1 && probe->verification_calls == int(verify), "NullVerify must control the adaptive confirmation");
    }
    // Endgame verification remains active below the new general depth threshold.
    for (bool cancel : {false, true}) {
        auto probe = std::make_unique<adaptive_probe_t>("7k/7p/8/8/8/8/P7/KR6 b - - 0 1");
        prepare(*probe);
        probe->adaptive_null_enabled = true;
        move_t move;
        move.set(&probe->brd, "h8g8");
        probe->forward(&move, false);
        const auto board = probe->brd.to_string();
        probe->interrupt_verification = cancel;
        probe->run(-1001, -1000, 6, true);
        ok &= check(probe->null_calls == 1 && probe->verification_calls == 1
            && probe->verification_depth == 1 && probe->verification_excluded
            && probe->stop_all == cancel && probe->brd.to_string() == board, "endgame verification must search real moves and restore cancellation");
    }
    auto confirmation = std::make_unique<adaptive_probe_t>("7k/7p/8/8/8/8/P7/KR6 b - - 0 1");
    prepare(*confirmation);
    confirmation->adaptive_null_enabled = true;
    confirmation->beta_pruning = true;
    confirmation->inject_verification = false;
    move_t king_move;
    king_move.set(&confirmation->brd, "h8g8");
    confirmation->forward(&king_move, false);
    confirmation->run(-1001, -1000, 6, true);
    ok &= check(confirmation->verification_calls == 1 && confirmation->verified_moves > 0,
        "adaptive verification must search legal moves instead of reverse futility");
    // Disabled/null/helper/root/PV/clock/decisive/check/depth exclusions.
    for (int guard = 0; guard < 10; ++guard) {
        auto probe = std::make_unique<adaptive_probe_t>(start);
        prepare(*probe);
        probe->adaptive_null_enabled = true;
        if (guard != 0) {
            move_t move;
            move.set(&probe->brd, "g1f3");
            probe->forward(&move, false);
        }
        if (guard == 1) { probe->null_enabled = false; }
        if (guard == 2) { probe->iid_active = true; }
        if (guard == 3) { probe->verification_mode = verification_mode_t::null_move; }
        if (guard == 4) { probe->brd.stack->fifty_count = 99; }
        if (guard == 5) { probe->stack->in_check = true; }
        if (guard == 6) { (probe->stack - 1)->current_move.clear(); }
        const int beta = guard == 7 ? -score::TABLEBASE : -1000;
        const int alpha = beta - (guard == 8 ? 2 : 1);
        probe->run(alpha, beta, guard == 9 ? 2 : 3, guard != 8);
        ok &= check(probe->null_calls == 0, "adaptive null eligibility guard failed");
    }
    // A nonpawn stalemate must not become a fail-high after an artificial pass.
    auto terminal = std::make_unique<adaptive_probe_t>("7k/5K1n/8/7Q/8/8/8/8 w - - 0 1");
    prepare(*terminal);
    terminal->adaptive_null_enabled = true;
    move_t quiet;
    quiet.set(&terminal->brd, "h5h6");
    terminal->forward(&quiet, false);
    ok &= check(terminal->run(-2001, -2000, 6, true) == 0 && terminal->null_calls == 0,
        "nonpawn stalemate must be resolved before null search");
    for (int enabled : {0, 1}) {
        options::get_option("AdaptiveNullMove")->value = enabled;
        auto search = std::make_unique<search_t>(start);
        options::get_option("AdaptiveNullMove")->value = !enabled;
        ok &= check(search->adaptive_null_enabled == bool(enabled), "worker must snapshot AdaptiveNullMove");
    }
    options::get_option("AdaptiveNullMove")->value = 1;
    auto losers = std::make_unique<search_t>(start, nullptr, nullptr, std::vector<U64>{}, 17);
    ok &= check(!losers->adaptive_null_enabled && options::get_option("AdaptiveNullMove")->clears_tt, "adaptive policy must be standard-only and invalidate TT");
    return check(ok, "adaptive null reduction/verification/ownership contract failed");
}

}

int main() {
    magic::init();
    const bool adaptive = test_adaptive_policy();
    const bool restoration = test_verification_frame_restore();
    const bool context = test_node_context();
    const bool cutoffs = test_cutoffs();
    const bool exclusions = test_exclusions();
    const bool pawn_endgames = test_pawn_endgames();
    const bool static_cushion = test_static_cushion();
    return adaptive && restoration && context && cutoffs && exclusions && pawn_endgames && static_cushion ? EXIT_SUCCESS : EXIT_FAILURE;
}
