#include "engine.h"

namespace {

bool check(bool ok, const char * message) {
    if (!ok) std::cerr << message << '\n';
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
    int null_depth = 0, null_alpha = 0, null_beta = 0;
    int null_value = 100;
    int verification_value = 100;
    bool inject_null = true;
    bool inject_verification = true;
    bool in_verification = false;
    bool verification_excluded = true;
    bool interrupt_null = false;
    bool interrupt_verification = false;

    int pvs(int alpha, int beta, int depth) override {
        const bool artificial = brd.ply > 0 && !(stack - 1)->current_move.piece;
        if (artificial) {
            ++null_calls;
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
            verification_excluded &= !null_enabled;
            if (inject_verification) {
                stop_all = interrupt_verification;
                return verification_value;
            }
        }
        const bool previous = in_verification;
        in_verification |= verification;
        const int result = search_t::pvs(alpha, beta, depth);
        in_verification = previous;
        return result;
    }

    int run(int alpha, int beta, int depth) {
        entry_ply = brd.ply;
        return search_t::pvs(alpha, beta, depth);
    }
};

void prepare(search_t & search) {
    trans_table::clear();
    search.draw_contempt = 0;
    search.stack->in_check = search.brd.in_check();
    search.null_enabled = true;
    search.null_verify = true;
    search.beta_pruning = false;
    search.tablebase_limit = 0;
}

bool restored(search_t & search, search_t & original) {
    return check(search.brd.to_string() == original.brd.to_string()
        && search.brd.stack->tt_key == original.brd.stack->tt_key
        && search.brd.stack->pawn_hash == original.brd.stack->pawn_hash
        && search.brd.stack->material_hash == original.brd.stack->material_hash
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
                if (ordinary) ok &= check(result == 100 || (!verify && result == value), "ordinary null cutoff lost");
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

}

int main() {
    magic::init();
    const bool cutoffs = test_cutoffs();
    const bool exclusions = test_exclusions();
    const bool pawn_endgames = test_pawn_endgames();
    const bool static_cushion = test_static_cushion();
    return cutoffs && exclusions && pawn_endgames && static_cushion ? EXIT_SUCCESS : EXIT_FAILURE;
}
