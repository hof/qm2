#include "engine.h"
#include <vector>

namespace {
class hash_probe_t : public search_t {
public:
    int children = 0, helpers = 0;
    std::string first_move;
    explicit hash_probe_t(const char * fen) : search_t(fen) {
        beta_pruning = null_enabled = ffp_enabled = lmr_enabled = false;
        tablebase_limit = draw_contempt = 0;
    }
    int pvs(int alpha, int beta, int depth) override {
        if (brd.ply == 0 && iid_active) {
            ++helpers;
            ++nodes;
            stack->best_move.clear();
            return 0;
        }
        if (brd.ply > 0) {
            ++nodes;
            ++children;
            if (first_move.empty()) {
                first_move = (stack - 1)->current_move.to_string();
            }
            stack->pv_count = 0;
            return 0;
        }
        return search_t::pvs(alpha, beta, depth);
    }
};
bool check(bool ok, const char * message) {
    if (!ok) {
        std::cerr << message << '\n';
    }
    return ok;
}
}

int main(int argc, char ** argv) {
    if (argc != 2) {
        return 1;
    }
    magic::init();
    uci::silent(true);
    trans_table::set_size(4);
    options::get_option("OwnBook")->value = 0;
    options::get_option("NNUEAuditHorizon")->value = 2;
    options::get_option("NNUEAuditIID")->value = 1;
    std::string error;
    if (!nnue::set_path(argv[1], error)) {
        return 1;
    }
    bool ok = true;
    {
        for (const char * fen : {
                "rnbqkbnr/pppppppp/8/8/8/8/PPPPPPPP/RNBQKBNR w KQkq - 0 1",
                "rnbqkbnr/pppppppp/8/8/8/8/PPPPPPPP/RNBQKBNR b KQkq - 0 1"}) {
            auto board_probe = std::make_unique<hash_probe_t>(fen);
            const char * hint_name = board_probe->brd.us() ? "b1c3" : "b8c6";
            for (int scenario : {0, 1, 2, 3, 4, 5}) {
                options::get_option("NNUEAuditHashMoves")->value = scenario != 4;
                auto probe = std::make_unique<hash_probe_t>(fen);
                move_t hint{};hint.set(&probe->brd, hint_name);
                const U64 key = probe->brd.stack->tt_key;
                const U64 context = probe->tt_context();
                trans_table::clear();
                const int encoded = scenario == 5 ? int(a3 | (b3 << 6)) : hint.to_compact();
                // Poisoned scores must never cut off a controlled search.
                trans_table::store(key, 0, 0, scenario == 1 ? 1 : 8, 7777, encoded,
                    score::EXACT, scenario == 2 ? context ^ 1 : context,
                    scenario == 3 ? clock_range_t{50, 90} : clock_range_t{0, 101});
                const int value = probe->pvs(-1000, 1000, 4);
                const auto & counts = probe->nnue_audit.counters;
                ok &= check(value == 0 && probe->children > 0 && !counts.hash_score_cutoffs,
                    "controlled search consumed cached score");
                if (scenario < 4) {
                    ok &= check(probe->first_move == hint_name && probe->helpers == 0
                        && counts.hash_probes == 1 && counts.hash_moves == 1 && counts.hash_first == 1,
                        "usable ordering hint was lost with deep, shallow, context or clock mismatch");
                } else if (scenario == 4) {
                    ok &= check(!counts.hash_probes && !counts.hash_moves && probe->helpers == 1,
                        "disabled hash ordering used a TT hint");
                } else {
                    ok &= check(counts.hash_rejected == 1 && !counts.hash_moves && probe->helpers == 1,
                        "invalid hash hint was not rejected");
                }
                ok &= check(!probe->brd.ply && probe->brd.to_string() == fen
                    && probe->brd.stack->tt_key == key && probe->stack == probe->root_stack,
                    "hash ordering changed root board state");
            }
            options::get_option("NNUEAuditHashMoves")->value = 1;
            trans_table::clear();
            auto live = std::make_unique<search_t>(fen);
            const int expected = evaluate(live.get());
            live->game.max_depth = 4;
            live->game.max_nodes = 200000;
            live->go();
            const auto & live_counts = live->nnue_audit.counters;
            ok &= check(live->completed_depth == 4 && !live->stop_all
                && live_counts.hash_probes && live_counts.hash_moves && live_counts.hash_first
                && !live_counts.hash_score_cutoffs && !live->brd.ply
                && live->brd.to_string() == fen && evaluate(live.get()) == expected,
                "live ordering-only hash search failed or changed evaluation/root state");
            trans_table::clear();
            auto stopped = std::make_unique<search_t>(fen);
            stopped->game.max_depth = 32;
            stopped->game.max_nodes = 128;
            stopped->go();
            ok &= check(stopped->stop_all && stopped->nodes == 128 && !stopped->brd.ply
                && stopped->brd.to_string() == fen && !stopped->iid_active
                && !stopped->nnue_audit.counters.hash_score_cutoffs,
                "cancelled live hash search failed to restore root state");
            options::get_option("NNUEAuditHorizon")->value = 1;
            options::get_option("NNUEAuditHashMoves")->value = 1;
            auto fixed = std::make_unique<hash_probe_t>(fen);
            ok &= check(!fixed->nnue_audit.hash_moves && !fixed->iid_enabled,
                "fixed neural horizon enabled hash ordering or IID");
            options::get_option("NNUEAuditHorizon")->value = 2;
        }
    }
    return ok ? 0 : 1;
}
