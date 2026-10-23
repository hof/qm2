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
                        || probe->brd.to_string() != start_fen
                        || counts.pvs_nodes + counts.q_nodes
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
                    || cancelled->brd.to_string() != start_fen
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
                        || probe->brd.to_string() != start_fen) {
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
    options::get_option("NNUEAuditHorizon")->value = 0;
    if (!search.nnue_audit.aligned_quiescence || search.beta_pruning || search.ffp_enabled
            || search.null_enabled || search.adaptive_null_enabled || search.lmr_enabled) {
        return failure(__LINE__);
    }
    options::get_option("NNUEAuditHorizon")->value = 2;
    search.init(fen, nullptr);
    if (!search.evaluation_network || !search.nnue_audit.aligned_quiescence
            || search.broader_futility_enabled || search.beta_pruning || search.ffp_enabled || search.null_enabled || search.adaptive_null_enabled || search.lmr_enabled
            || search.nnue_audit.q_delta) {
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
            || search.nnue_audit.q_delta) {
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
            || search.nnue_audit.q_delta) {
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
    if (search.nnue_audit.counters.beta_cutoffs || search.nodes == 0) {
        return failure(__LINE__);
    }
    search.init(fen, nullptr);
    if (search.pvs(expected - 101, expected - 100, 1) != expected
            || search.nnue_audit.counters.beta_cutoffs != 1 || search.nodes != 0) {
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
        // c4xd5 is an equal exchange: futility skips it only once stand pat
        // plus the margin cannot reach alpha.
        const char * equal_fen = "r3k2r/ppp2ppp/2n1bn2/3p4/2PPp3/4BN2/PPP2PPP/R2QK2R w KQkq - 0 1";
        for (int surplus : {349, 350}) {
            auto probe = std::make_unique<search_t>(equal_fen);
            const int alpha = evaluate(probe.get()) + surplus;
            probe->qsearch(alpha, alpha + 1, -1);
            if (bool(probe->nnue_audit.counters.q_delta_prunes) != (surplus == 350)
                    || probe->brd.ply || probe->brd.to_string() != equal_fen) {
                return failure(__LINE__);
            }
        }
    }
    search.init(fen, nullptr);
    if (!search.nnue_audit.q_delta) {
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
    {
        const char * queen_fen = "6k1/8/8/8/8/8/6r1/K2Q4 w - - 0 1";
        auto probe = std::make_unique<search_t>(queen_fen);
        // Quiet checking moves must not expand qsearch. There are no root captures.
        const int alpha = evaluate(probe.get()) + 5000;
        probe->qsearch(alpha, alpha + 1, 0);
        const auto & counts = probe->nnue_audit.counters;
        if (counts.pvs_nodes || counts.move_visits != probe->nodes || counts.null_visits
                || counts.root_calls || counts.researches) {
            return failure(__LINE__);
        }
        if (probe->nodes != 0 || probe->brd.ply || probe->brd.to_string() != queen_fen) {
            return failure(__LINE__);
        }
        probe->init(queen_fen, nullptr);
        probe->qsearch(alpha, alpha + 1, -1);
        if (probe->nodes != 0) {
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
        if (probe->nodes == 0
                || probe->brd.ply || probe->brd.to_string() != counter_fen) {
            return failure(__LINE__);
        }
        for (const char * castle_fen : {
                "5k2/8/8/8/8/8/8/4K2R w K - 0 1",
                "4k3/8/8/8/8/8/8/4K2R w K - 0 1"}) {
            probe->init(castle_fen, nullptr);
            const int castle_alpha = evaluate(probe.get()) + 5000;
            probe->qsearch(castle_alpha, castle_alpha + 1, 0);
            if (probe->nodes != 0 || probe->brd.ply
                    || probe->brd.to_string() != castle_fen) {
                return failure(__LINE__);
            }
            probe->init(castle_fen, nullptr);
            probe->qsearch(castle_alpha, castle_alpha + 1, -1);
            if (probe->nodes != 0) {
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
    if (work.move_visits != search.nodes || !work.root_calls
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
    options::get_option("NNUEAuditHorizon")->value = 0;
    std::cout << "Live lane/cache corruption detected; audited cancellation/restoration passed\n";
}
