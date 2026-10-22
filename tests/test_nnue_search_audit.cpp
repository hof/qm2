#include "engine.h"
#include "nnue_audit.h"

namespace {
int failure(int line) {
    std::cerr << "NNUE search audit check failed at line " << line << '\n';
    return 1;
}
}

int main(int argc, char ** argv) {
    if (argc != 2) {
        return failure(__LINE__);
    }
    magic::init();
    uci::silent(true);
    options::get_option("OwnBook")->value = 0;
    std::string error;
    if (!nnue::set_path(argv[1], error)) {
        return failure(__LINE__);
    }
    const char * fen = "rnbqkbnr/pppppppp/8/8/8/8/PPPPPPPP/RNBQKBNR w KQkq - 0 1";
    auto storage = std::make_unique<search_t>(fen);
    auto & search = *storage;
    const int expected = evaluate(&search);
    // Detect corrupted lanes even when clipping leaves the final score unchanged.
    ++search.evaluation_accumulators[0].values[0][0];
    bool caught_lane = false, caught_cache = false;
    try { evaluate(&search); } catch (const std::runtime_error &) { caught_lane = true; }
    search.nnue_audit.mode = nnue::audit_mode_t::full;
    if (evaluate(&search) != expected) {
        return failure(__LINE__);
    }
    ++search.stack->eval_result;
    try { evaluate(&search); } catch (const std::runtime_error &) { caught_cache = true; }
    if (!caught_lane || !caught_cache) {
        return failure(__LINE__);
    }
    search.init(fen, nullptr);
    // Pin the recorder's legacy direct-root return-eval contract.
    search.broader_futility_enabled = false;
    trans_table::clear();
    search.nnue_audit.record_cutoffs = true;
    const int lower = expected - 201, upper = expected - 200;
    const int cutoff = search.pvs(lower, upper, 1);
    if (std::abs(upper) < score::TABLEBASE - MAX_PLY) {
        if (cutoff != expected || search.nnue_audit.beta_cutoffs.size() != 1) {
            return failure(__LINE__);
        }
        const auto & event = search.nnue_audit.beta_cutoffs.front();
        if (event.fen != fen || event.alpha != lower || event.beta != upper || event.eval != expected) {
            return failure(__LINE__);
        }
    }
    // A real irreversible predecessor gives the broader interior cutoff a
    // replayable clock-zero event, without relying on an injected evaluation.
    {
        trans_table::clear();
        auto entry = std::make_unique<search_t>(fen);
        move_t pawn{};
        pawn.set(&entry->brd, "e2e4");
        entry->forward(&pawn, false);
        const auto position = entry->brd.to_string();
        const int eval = evaluate(entry.get());
        const int beta = eval - 1000;
        entry->nnue_audit.record_cutoffs = true;
        const int value = entry->pvs(beta - 1, beta, 4);
        if (value != beta + (eval - beta) / 3 || entry->nnue_audit.beta_cutoffs.size() != 1
                || entry->nnue_audit.counters.beta_deep != 1
                || entry->brd.to_string() != position || entry->brd.ply != 1) {
            return failure(__LINE__);
        }
        const auto & event = entry->nnue_audit.beta_cutoffs.front();
        if (event.fen != position || event.alpha != beta - 1 || event.beta != beta || event.eval != eval) {
            return failure(__LINE__);
        }
    }
    search.init(fen, nullptr);
    search.game.max_nodes = 4000;
    search.game.max_depth = 32;
    search.go();
    if (search.nodes != 4000 || search.brd.ply || !search.nnue_audit.counters.calls) {
        return failure(__LINE__);
    }
    // A completed descendant can train correction before the root is restored.
    // Audit the unchanged raw cache and the freshly corrected return separately.
    if (evaluate(&search) != search.corrected_static_eval(expected)
            || search.stack->eval_result != expected) {
        return failure(__LINE__);
    }
    search.init(fen, nullptr);
    // Independent one-ply enumeration checks the matched-horizon PVS path.
    int exhaustive = -score::INF;
    for (int index = 0; index < search.root.move_count; ++index) {
        auto & root_move = search.root.moves[index];
        search.forward(&root_move.move, root_move.gives_check);
        nnue::accumulator_t fresh;
        const int value = -nnue::to_centipawns(search.evaluation_network->evaluate(search.brd, fresh));
        exhaustive = MAX(exhaustive, value);
        search.backward(&root_move.move);
    }
    search.nnue_audit.fixed_horizon = true;
    search.beta_pruning = search.ffp_enabled = search.null_enabled = search.lmr_enabled = false;
    search.game.max_depth = 1;
    search.go();
    if (search.result_score != exhaustive || search.completed_depth != 1
            || search.brd.ply || search.brd.to_string() != fen) {
        return failure(__LINE__);
    }
    options::get_option("NNUEAuditHorizon")->value = 2;
    search.init(fen, nullptr);
    options::get_option("NNUEAuditCheckExtensions")->value = 1;
    {
        for (bool white : {false, true}) {
            const char * check_fen = white ? "6k1/8/8/8/8/8/6r1/K2Q4 w - - 0 1"
                : "k2q4/6R1/8/8/8/8/8/6K1 b - - 0 1";
            auto probe = std::make_unique<search_t>(check_fen);
            move_t safe{}, losing{};
            safe.set(&probe->brd, white ? "d1b3" : "d8b6");
            losing.set(&probe->brd, white ? "d1g4" : "d8g5");
            if (!probe->brd.legal(&safe) || !probe->brd.legal(&losing)
                    || !probe->brd.gives_check(&safe) || !probe->brd.gives_check(&losing)) {
                return failure(__LINE__);
            }
            if (probe->extension(&safe, probe->brd.gives_check(&safe)) != 1
                    || probe->extension(&losing, probe->brd.gives_check(&losing))) {
                return failure(__LINE__);
            }
            probe->stack->check_extensions = 3;
            if (probe->extension(&safe, probe->brd.gives_check(&safe)) != 1) {
                return failure(__LINE__);
            }
            const auto & extension_counts = probe->nnue_audit.counters;
            if (extension_counts.extension_candidates != 3 || extension_counts.extensions_applied != 2
                    || extension_counts.extension_see_rejected != 1) {
                return failure(__LINE__);
            }
            probe->stack->check_extensions = 1;
            probe->forward(&safe, probe->brd.gives_check(&safe));
            if (probe->stack->check_extensions != 1) {
                return failure(__LINE__);
            }
            probe->backward(&safe);
            probe->forward();
            if (probe->stack->check_extensions != 1) {
                return failure(__LINE__);
            }
            probe->backward();
            if (probe->stack->check_extensions != 1 || probe->brd.to_string() != check_fen) {
                return failure(__LINE__);
            }
            // Depth four with no TT move is IID eligible. The controlled
            // flag is off here; exercise that entry directly.
            probe->init(check_fen, nullptr);
            probe->stack->check_extensions = 3;
            probe->game.max_nodes = 2048;
            probe->pvs(-score::INF, score::INF, 4);
            if (probe->nnue_audit.counters.iid_calls || probe->brd.ply
                    || probe->brd.to_string() != check_fen || probe->stack->check_extensions != 3
                    || !probe->nnue_audit.counters.extensions_applied
                    || probe->nnue_audit.counters.max_check_extensions <= 3) {
                return failure(__LINE__);
            }
            probe->init(check_fen, nullptr);
            probe->game.max_nodes = 512;
            probe->game.max_depth = 3;
            probe->go();
            if (probe->nodes != 512 || !probe->stop_all || probe->brd.ply
                    || probe->brd.to_string() != check_fen || probe->stack->check_extensions
                    || !probe->nnue_audit.counters.extensions_applied
                    || probe->nnue_audit.counters.iid_calls
                    || probe->nnue_audit.counters.max_main_ply >= MAX_PLY) {
                return failure(__LINE__);
            }
        }
    }
    options::get_option("NNUEAuditLMR")->value = 1;
    {
        for (bool white : {false, true}) {
            const char * start_fen = white
                ? "rnbqkbnr/pppppppp/8/8/8/8/PPPPPPPP/RNBQKBNR w KQkq - 0 1"
                : "rnbqkbnr/pppppppp/8/8/8/8/PPPPPPPP/RNBQKBNR b KQkq - 0 1";
            for (int depth : {3, 4}) {
                auto probe = std::make_unique<search_t>(start_fen);
                probe->game.max_depth = depth;
                probe->game.max_nodes = 200000;
                probe->go();
                const auto & counts = probe->nnue_audit.counters;
                if (!probe->lmr_enabled || probe->completed_depth != depth || probe->brd.ply
                        || probe->brd.to_string() != start_fen || probe->stack->check_extensions
                        || counts.iid_calls || counts.pvs_nodes + counts.q_nodes
                            != counts.move_visits + counts.null_visits + counts.researches) {
                    return failure(__LINE__);
                }
                if (depth == 3) {
                    if (counts.lmr_candidates || counts.lmr_reductions || counts.lmr_researches) {
                        return failure(__LINE__);
                    }
                } else if (!counts.lmr_reductions || counts.lmr_reductions != counts.lmr_candidates
                        || counts.min_lmr_depth != 3 || counts.max_lmr_depth != 3
                        || counts.min_lmr_child_depth != 1
                        || counts.lmr_researches > counts.lmr_reductions) {
                    return failure(__LINE__);
                }
            }
            auto cancelled = std::make_unique<search_t>(start_fen);
            cancelled->game.max_depth = 8;
            cancelled->game.max_nodes = 4096;
            cancelled->go();
            if (cancelled->nodes != 4096 || cancelled->brd.ply
                    || cancelled->brd.to_string() != start_fen || cancelled->stack->check_extensions
                    || !cancelled->nnue_audit.counters.lmr_reductions) {
                return failure(__LINE__);
            }
        }
    }
    options::get_option("NNUEAuditAspiration")->value = 1;
    {
        for (bool white : {false, true}) {
            const char * start_fen = white
                ? "rnbqkbnr/pppppppp/8/8/8/8/PPPPPPPP/RNBQKBNR w KQkq - 0 1"
                : "rnbqkbnr/pppppppp/8/8/8/8/PPPPPPPP/RNBQKBNR b KQkq - 0 1";
            for (int depth : {3, 4}) {
                auto probe = std::make_unique<search_t>(start_fen);
                probe->game.max_depth = depth;
                probe->game.max_nodes = 200000;
                probe->go();
                const auto & counts = probe->nnue_audit.counters;
                if (!probe->aspiration_enabled || probe->completed_depth != depth || probe->brd.ply
                        || probe->brd.to_string() != start_fen || counts.iid_calls) {
                    return failure(__LINE__);
                }
                if (depth == 3) {
                    if (counts.aspiration_searches || counts.aspiration_attempts) {
                        return failure(__LINE__);
                    }
                } else if (counts.aspiration_searches != 1 || counts.aspiration_successes != 1
                        || counts.aspiration_cancelled || counts.min_aspiration_depth != 4
                        || counts.max_aspiration_depth != 4 || counts.root_calls != depth
                            + counts.aspiration_attempts - counts.aspiration_searches) {
                    return failure(__LINE__);
                }
            }
        }
    }
    options::get_option("NNUEAuditAspiration")->value = 0;
    options::get_option("NNUEAuditLMR")->value = 0;
    options::get_option("NNUEAuditCheckExtensions")->value = 0;
    options::get_option("NNUEAuditHorizon")->value = 0;
    if (!search.nnue_audit.aligned_quiescence || search.beta_pruning || search.ffp_enabled
            || search.null_enabled || search.adaptive_null_enabled || search.lmr_enabled || search.extension(&search.root.moves[0].move, 2)) {
        return failure(__LINE__);
    }
    options::get_option("NNUEAuditHorizon")->value = 2;
    search.init(fen, nullptr);
    if (!search.evaluation_network || !search.nnue_audit.aligned_quiescence
            || search.broader_futility_enabled || search.beta_pruning || search.ffp_enabled || search.null_enabled || search.adaptive_null_enabled || search.lmr_enabled
            || search.iid_enabled || search.nnue_audit.q_delta || search.nnue_audit.q_see
            || search.extension(&search.root.moves[0].move, 2)) {
        return failure(__LINE__);
    }
    search.game.max_depth = 3;
    search.go();
    if (search.completed_depth != 3 || search.brd.ply || search.brd.to_string() != fen) {
        return failure(__LINE__);
    }
    options::get_option("NNUEAuditHorizon")->value = 1;
    search.init(fen, nullptr);
    if (!search.nnue_audit.fixed_horizon || search.nnue_audit.aligned_quiescence) {
        return failure(__LINE__);
    }
    options::get_option("NNUEAuditHorizon")->value = 0;
    options::get_option("NNUEAuditHorizon")->value = 2;
    options::get_option("NNUEAuditNullMove")->value = 1;
    search.init(fen, nullptr);
    if (!search.null_enabled || search.beta_pruning || search.ffp_enabled || search.lmr_enabled
            || search.nnue_audit.q_delta || search.nnue_audit.q_see) {
        return failure(__LINE__);
    }
    search.game.max_depth = 3;
    search.go();
    if (search.completed_depth != 3 || search.brd.ply || search.brd.to_string() != fen
            || !search.nnue_audit.counters.null_attempts || !search.nnue_audit.counters.null_cutoffs) {
        return failure(__LINE__);
    }
    search.init(fen, nullptr);
    search.game.max_depth = 3;
    search.game.max_nodes = 128;
    search.go();
    if (search.nodes != 128 || !search.stop_all || search.brd.ply
            || search.brd.to_string() != fen || evaluate(&search) != expected) {
        return failure(__LINE__);
    }
    options::get_option("NNUEAuditHorizon")->value = 0;
    options::get_option("NNUEAuditNullMove")->value = 0;
    options::get_option("NNUEAuditHorizon")->value = 2;
    options::get_option("NNUEAuditBetaPruning")->value = 1;
    search.init(fen, nullptr);
    if (!search.beta_pruning || search.null_enabled || search.ffp_enabled || search.lmr_enabled
            || search.nnue_audit.q_delta || search.nnue_audit.q_see) {
        return failure(__LINE__);
    }
    search.game.max_depth = 3;
    search.go();
    if (search.completed_depth != 3 || search.brd.ply || search.brd.to_string() != fen
            || !search.nnue_audit.counters.beta_candidates
            || search.nnue_audit.counters.null_attempts) {
        return failure(__LINE__);
    }
    // The former narrow margin admitted this 80-cp surplus. Search legal moves
    // instead; no child main-search nodes exist at depth one to mask the result.
    search.init(fen, nullptr);
    search.pvs(expected - 81, expected - 80, 1);
    if (search.nnue_audit.counters.beta_cutoffs || search.nodes <= 1) {
        return failure(__LINE__);
    }
    search.init(fen, nullptr);
    if (search.pvs(expected - 101, expected - 100, 1) != expected
            || search.nnue_audit.counters.beta_cutoffs != 1 || search.nodes != 1) {
        return failure(__LINE__);
    }
    for (int surplus : {49, 50}) {
        search.init(fen, nullptr);
        search.beta_pruning = false;
        search.null_enabled = true;
        search.pvs(expected - surplus - 1, expected - surplus, 1);
        if (search.nnue_audit.counters.null_attempts != uint64_t(surplus == 50)
                || search.brd.to_string() != fen || search.brd.ply) {
            return failure(__LINE__);
        }
    }
    search.init(fen, nullptr);
    search.game.max_depth = 3;
    search.game.max_nodes = 128;
    search.go();
    if (search.nodes != 128 || !search.stop_all || search.brd.ply
            || search.brd.to_string() != fen || evaluate(&search) != expected) {
        return failure(__LINE__);
    }
    options::get_option("NNUEAuditBetaPruning")->value = 0;
    options::get_option("NNUEAuditFutility")->value = 1;
    search.init(fen, nullptr);
    if (!search.ffp_enabled || search.null_enabled || search.beta_pruning || search.lmr_enabled) {
        return failure(__LINE__);
    }
    search.pvs(expected + 299, expected + 300, 1);
    if (!search.nnue_audit.counters.ffp_candidates || search.nnue_audit.counters.ffp_prunes) {
        return failure(__LINE__);
    }
    search.init(fen, nullptr);
    search.pvs(expected + 300, expected + 301, 1);
    if (!search.nnue_audit.counters.ffp_prunes || search.brd.to_string() != fen || search.brd.ply) {
        return failure(__LINE__);
    }
    search.init(fen, nullptr);
    search.game.max_depth = 3;
    search.game.max_nodes = 128;
    search.go();
    if (search.nodes != 128 || !search.stop_all || search.brd.ply
            || search.brd.to_string() != fen || evaluate(&search) != expected) {
        return failure(__LINE__);
    }
    options::get_option("NNUEAuditFutility")->value = 0;
    options::get_option("NNUEAuditDelta")->value = 1;
    {
        for (const char * capture_fen : {
                "r3k2r/ppp2ppp/2n1bn2/3p4/3Pp3/2N1BN2/PPP2PPP/R2QK2R w KQkq - 0 1",
                "r2qk2r/ppp2ppp/2n1bn2/3pP3/3P4/2N1BN2/PPP2PPP/R3K2R b KQkq - 0 1"}) {
            for (int surplus : {299, 300}) {
                auto probe = std::make_unique<search_t>(capture_fen);
                const int alpha = evaluate(probe.get()) + surplus;
                probe->qsearch(alpha, alpha + 1, -1);
                if (probe->nodes != uint64_t(surplus == 299 ? 3 : 1)
                        || probe->nnue_audit.counters.q_delta_candidates != 2
                        || probe->nnue_audit.counters.q_delta_prunes != uint64_t(surplus == 300 ? 2 : 0)
                        || probe->nnue_audit.counters.q_see_prunes || probe->brd.ply
                        || probe->brd.to_string() != capture_fen) {
                    return failure(__LINE__);
                }
            }
        }
    }
    // A low-material transition and en passant must remain searchable even
    // far below alpha. Their legal capture sets are independently fixed here.
    for (const char * protected_fen : {
            "r3k2r/ppp2ppp/8/3n4/3P4/2N5/PPP2PPP/R3K2R w KQkq - 0 1",
            "r3k2r/ppp2ppp/2n5/3pP3/8/2N5/PPP2PPP/R3K2R w KQkq d6 0 1"}) {
        auto probe = std::make_unique<search_t>(protected_fen);
        const int alpha = evaluate(probe.get()) + 5000;
        probe->qsearch(alpha, alpha + 1, -1);
        if (probe->nodes != 2 || probe->brd.ply || probe->brd.to_string() != protected_fen) {
            return failure(__LINE__);
        }
    }
    search.init(fen, nullptr);
    if (!search.nnue_audit.q_delta || search.nnue_audit.q_see) {
        return failure(__LINE__);
    }
    search.game.max_nodes = 128;
    search.game.max_depth = 3;
    search.go();
    if (search.nodes != 128 || !search.stop_all || search.brd.ply
            || search.brd.to_string() != fen || evaluate(&search) != expected) {
        return failure(__LINE__);
    }
    options::get_option("NNUEAuditDelta")->value = 0;
    options::get_option("NNUEAuditSEE")->value = 1;
    {
        // Both knight captures lose 225 cp. SEE alone must reject them even
        // when alpha is too close to stand pat for capture-gain delta pruning.
        const char * losing_fen = "r3k2r/ppp2ppp/2n1bn2/3p4/3Pp3/2N1BN2/PPP2PPP/R2QK2R w KQkq - 0 1";
        auto probe = std::make_unique<search_t>(losing_fen);
        const int alpha = evaluate(probe.get()) + 1;
        probe->qsearch(alpha, alpha + 1, -1);
        if (probe->nodes != 1 || probe->nnue_audit.counters.q_see_candidates != 2
                || probe->nnue_audit.counters.q_see_prunes != 2
                || probe->nnue_audit.counters.q_delta_candidates || probe->brd.ply
                || probe->brd.to_string() != losing_fen) {
            return failure(__LINE__);
        }
        // The e6 pawn is pinned to e8 by Re1. Nc3xd5 wins a pawn and
        // must survive SEE pruning in the real qsearch with either evaluator.
        const char * pinned_fen = "rnb1kbnr/ppp3pp/4p3/3p4/8/2N5/PPP2PPP/R3R1K1 w - - 0 1";
        probe->init(pinned_fen, nullptr);
        move_t pinned_move;
        pinned_move.set(WKNIGHT, c3, d5, BPAWN);
        if (!probe->brd.legal(&pinned_move) || probe->brd.see(&pinned_move) != 100) {
            return failure(__LINE__);
        }
        const int pinned_alpha = evaluate(probe.get()) + 1;
        probe->qsearch(pinned_alpha, pinned_alpha + 1, -1);
        if (probe->nodes <= 1 || !probe->nnue_audit.counters.q_see_candidates
                || probe->brd.ply || probe->brd.to_string() != pinned_fen) {
            return failure(__LINE__);
        }
        // c4xd5 is an equal exchange. A high alpha must not make SEE discard it.
        const char * equal_fen = "r3k2r/ppp2ppp/2n1bn2/3p4/2PPp3/4BN2/PPP2PPP/R2QK2R w KQkq - 0 1";
        probe->init(equal_fen, nullptr);
        move_t equal_move;
        equal_move.set(WPAWN, c4, d5, BPAWN);
        if (!probe->brd.legal(&equal_move) || probe->brd.see(&equal_move) != 0) {
            return failure(__LINE__);
        }
        const int high_alpha = evaluate(probe.get()) + 5000;
        probe->qsearch(high_alpha, high_alpha + 1, -1);
        if (probe->nodes <= 1 || !probe->nnue_audit.counters.q_see_candidates
                || probe->brd.ply || probe->brd.to_string() != equal_fen) {
            return failure(__LINE__);
        }
        // Losing captures remain searchable when they check or when the
        // opponent is already below the post-capture material threshold.
        for (const char * protected_fen : {
                "r3k2r/ppp2ppp/2n5/8/2B5/2N5/PPP2PPP/R3K2R w KQkq - 0 1",
                "r3k2r/ppp2ppp/4p3/3p4/3P4/2N5/PPP2PPP/R3K2R w KQkq - 0 1"}) {
            probe->init(protected_fen, nullptr);
            const int protected_alpha = evaluate(probe.get()) + 5000;
            probe->qsearch(protected_alpha, protected_alpha + 1, -1);
            if (probe->nodes <= 1 || probe->nnue_audit.counters.q_see_prunes
                    || probe->brd.ply || probe->brd.to_string() != protected_fen) {
                return failure(__LINE__);
            }
        }
    }
    search.init(fen, nullptr);
    if (search.nnue_audit.q_delta || !search.nnue_audit.q_see) {
        return failure(__LINE__);
    }
    search.game.max_nodes = 128;
    search.game.max_depth = 3;
    search.go();
    if (search.nodes != 128 || !search.stop_all || search.brd.ply
            || search.brd.to_string() != fen || evaluate(&search) != expected) {
        return failure(__LINE__);
    }
    options::get_option("NNUEAuditSEE")->value = 0;
    {
        const char * queen_fen = "6k1/8/8/8/8/8/6r1/K2Q4 w - - 0 1";
        auto probe = std::make_unique<search_t>(queen_fen);
        // Quiet checking moves must not expand qsearch. There are no root captures.
        const int alpha = evaluate(probe.get()) + 5000;
        probe->qsearch(alpha, alpha + 1, 0);
        const auto & counts = probe->nnue_audit.counters;
        if (counts.pvs_nodes || counts.q_nodes != probe->nodes
                || counts.move_visits + 1 != counts.q_nodes || counts.null_visits
                || counts.root_calls || counts.researches) {
            return failure(__LINE__);
        }
        if (probe->nodes != 1 || probe->brd.ply || probe->brd.to_string() != queen_fen) {
            return failure(__LINE__);
        }
        probe->init(queen_fen, nullptr);
        probe->qsearch(alpha, alpha + 1, -1);
        if (probe->nodes != 1) {
            return failure(__LINE__);
        }
        const char * counter_fen = "r5k1/8/8/8/8/8/8/KQ6 w - - 0 1";
        probe->init(counter_fen, nullptr);
        probe->stack->in_check = probe->brd.in_check();
        move_t counter_move{};
        counter_move.set(&probe->brd, "b1a2");
        if (!probe->stack->in_check || !probe->brd.legal(&counter_move)
                || !probe->brd.gives_check(&counter_move) || probe->brd.see(&counter_move, 0)) {
            return failure(__LINE__);
        }
        probe->qsearch(-score::INF, score::INF, -1);
        if (probe->nodes <= 1
                || probe->brd.ply || probe->brd.to_string() != counter_fen) {
            return failure(__LINE__);
        }
        for (const char * castle_fen : {
                "5k2/8/8/8/8/8/8/4K2R w K - 0 1",
                "4k3/8/8/8/8/8/8/4K2R w K - 0 1"}) {
            probe->init(castle_fen, nullptr);
            const int castle_alpha = evaluate(probe.get()) + 5000;
            probe->qsearch(castle_alpha, castle_alpha + 1, 0);
            if (probe->nodes != 1 || probe->brd.ply
                    || probe->brd.to_string() != castle_fen) {
                return failure(__LINE__);
            }
            probe->init(castle_fen, nullptr);
            probe->qsearch(castle_alpha, castle_alpha + 1, -1);
            if (probe->nodes != 1) {
                return failure(__LINE__);
            }
        }
    }
    search.init(fen, nullptr);
    search.game.max_nodes = 128;
    search.game.max_depth = 3;
    search.go();
    if (search.nodes != 128 || !search.stop_all || search.brd.ply
            || search.brd.to_string() != fen || evaluate(&search) != expected) {
        return failure(__LINE__);
    }
    const auto & work = search.nnue_audit.counters;
    if (work.pvs_nodes + work.q_nodes != search.nodes || !work.root_calls
            || work.move_visits + work.null_visits + work.researches != search.nodes
            || work.root_researches > work.researches
            || work.research_fail_highs > work.researches) {
        return failure(__LINE__);
    }
    move_t played_move{};
    played_move.set(&search.brd, "e2e4");
    if (!search.brd.valid(&played_move) || !search.brd.legal(&played_move)) {
        return failure(__LINE__);
    }
    search.forward(&played_move, search.brd.gives_check(&played_move));
    const auto played_fen = search.brd.to_string();
    search.reset_stack();
    if (search.nodes || work.pvs_nodes || work.q_nodes || work.root_calls
            || work.move_visits || work.null_visits || work.researches
            || work.root_researches || work.research_fail_highs
            || search.brd.to_string() != played_fen) {
        return failure(__LINE__);
    }
    options::get_option("NNUEAuditIID")->value = 1;
    {
        for (const char * start_fen : {fen,
                "rnbqkbnr/pppppppp/8/8/8/8/PPPPPPPP/RNBQKBNR b KQkq - 0 1"}) {
            for (int depth : {3, 4}) {
                auto probe = std::make_unique<search_t>(start_fen);
                probe->game.max_nodes = 200000;
                probe->pvs(-score::INF, score::INF, depth);
                const auto & counts = probe->nnue_audit.counters;
                if (probe->stop_all || probe->iid_active || !probe->iid_enabled
                        || probe->brd.ply || probe->brd.to_string() != start_fen) {
                    return failure(__LINE__);
                }
                if (depth == 3) {
                    if (counts.iid_calls) {
                        return failure(__LINE__);
                    }
                } else if (counts.iid_calls != 1 || counts.iid_moves != 1
                        || counts.iid_cancelled || counts.min_iid_depth != 4
                        || counts.max_iid_depth != 4 || counts.min_iid_probe_depth != 2
                        || !counts.iid_visits) {
                    return failure(__LINE__);
                }
                if (counts.pvs_nodes + counts.q_nodes != counts.move_visits
                        + counts.null_visits + counts.researches + counts.iid_calls + 1) {
                    return failure(__LINE__);
                }
            }
            auto cancelled = std::make_unique<search_t>(start_fen);
            cancelled->game.max_nodes = 2;
            cancelled->pvs(-score::INF, score::INF, 4);
            if (!cancelled->stop_all || cancelled->nodes != 2 || cancelled->brd.ply
                    || cancelled->iid_active || !cancelled->iid_enabled
                    || cancelled->nnue_audit.counters.iid_cancelled != 1
                    || cancelled->brd.to_string() != start_fen) {
                return failure(__LINE__);
            }
        }
    }
    options::get_option("NNUEAuditIID")->value = 0;
    options::get_option("NNUEAuditHorizon")->value = 0;
    std::cout << "Live lane/cache corruption detected; audited cancellation/restoration passed\n";
}
