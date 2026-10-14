#include "engine.h"
#include "nnue_audit.h"

int main(int argc, char ** argv) {
    if (argc != 2) return 1;
    magic::init();
    uci::silent(true);
    options::get_option("UseNNUE")->value = 1;
    options::get_option("OwnBook")->value = 0;
    std::string error;
    if (!nnue::set_path(argv[1], error)) return 1;
    const char * fen = "rnbqkbnr/pppppppp/8/8/8/8/PPPPPPPP/RNBQKBNR w KQkq - 0 1";
    auto storage = std::make_unique<search_t>(fen);
    auto & search = *storage;
    const int expected = evaluate(&search);
    // Detect corrupted lanes even when clipping leaves the final score unchanged.
    ++search.evaluation_accumulators[0].values[0][0];
    bool caught_lane = false, caught_cache = false;
    try { evaluate(&search); } catch (const std::runtime_error &) { caught_lane = true; }
    search.nnue_audit.mode = nnue::audit_mode_t::full;
    if (evaluate(&search) != expected) return 1;
    ++search.stack->eval_result;
    try { evaluate(&search); } catch (const std::runtime_error &) { caught_cache = true; }
    if (!caught_lane || !caught_cache) return 1;
    search.init(fen, nullptr);
    // Verify that the recorder captures a genuine eligible beta cutoff.
    trans_table::clear();
    search.nnue_audit.record_cutoffs = true;
    const int lower = expected - 201, upper = expected - 200;
    const int cutoff = search.pvs(lower, upper, 1);
    if (std::abs(upper) < score::TABLEBASE - MAX_PLY) {
        if (cutoff != expected || search.nnue_audit.beta_cutoffs.size() != 1) return 1;
        const auto & event = search.nnue_audit.beta_cutoffs.front();
        if (event.fen != fen || event.alpha != lower || event.beta != upper || event.eval != expected) return 1;
    }
    search.init(fen, nullptr);
    search.game.max_nodes = 4000;
    search.game.max_depth = 32;
    search.go();
    if (search.nodes != 4000 || search.brd.ply || !search.nnue_audit.counters.calls) return 1;
    if (evaluate(&search) != expected) return 1;
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
            || search.brd.ply || search.brd.to_string() != fen) return 1;
    options::get_option("NNUEAuditHorizon")->value = 2;
    search.init(fen, nullptr);
    options::get_option("NNUEAuditHorizon")->value = 0;
    if (!search.nnue_audit.aligned_quiescence || search.beta_pruning || search.ffp_enabled
            || search.null_enabled || search.lmr_enabled || search.extension(&search.root.moves[0].move, 2)) return 1;
    options::get_option("NNUEAuditHorizon")->value = 2;
    options::get_option("UseNNUE")->value = 0;
    search.init(fen, nullptr);
    if (search.evaluation_network || !search.nnue_audit.aligned_quiescence
            || search.beta_pruning || search.ffp_enabled || search.null_enabled || search.lmr_enabled
            || search.nnue_audit.q_delta || search.nnue_audit.q_see
            || search.extension(&search.root.moves[0].move, 2)) return 1;
    search.game.max_depth = 3;
    search.go();
    if (search.completed_depth != 3 || search.brd.ply || search.brd.to_string() != fen) return 1;
    options::get_option("NNUEAuditHorizon")->value = 1;
    search.init(fen, nullptr);
    if (search.nnue_audit.fixed_horizon || search.nnue_audit.aligned_quiescence) return 1;
    options::get_option("NNUEAuditHorizon")->value = 0;
    options::get_option("UseNNUE")->value = 1;
    options::get_option("NNUEAuditHorizon")->value = 2;
    options::get_option("NNUEAuditNullMove")->value = 1;
    search.init(fen, nullptr);
    if (!search.null_enabled || search.beta_pruning || search.ffp_enabled || search.lmr_enabled
            || search.nnue_audit.q_delta || search.nnue_audit.q_see) return 1;
    search.game.max_depth = 3;
    search.go();
    if (search.completed_depth != 3 || search.brd.ply || search.brd.to_string() != fen
            || !search.nnue_audit.counters.null_attempts || !search.nnue_audit.counters.null_cutoffs) return 1;
    search.init(fen, nullptr);
    search.game.max_depth = 3;
    search.game.max_nodes = 128;
    search.go();
    if (search.nodes != 128 || !search.stop_all || search.brd.ply
            || search.brd.to_string() != fen || evaluate(&search) != expected) return 1;
    options::get_option("NNUEAuditHorizon")->value = 0;
    options::get_option("NNUEAuditNullMove")->value = 0;
    options::get_option("NNUEAuditHorizon")->value = 2;
    options::get_option("NNUEAuditBetaPruning")->value = 1;
    search.init(fen, nullptr);
    if (!search.beta_pruning || search.null_enabled || search.ffp_enabled || search.lmr_enabled
            || search.nnue_audit.q_delta || search.nnue_audit.q_see) return 1;
    search.game.max_depth = 3;
    search.go();
    if (search.completed_depth != 3 || search.brd.ply || search.brd.to_string() != fen
            || !search.nnue_audit.counters.beta_candidates
            || search.nnue_audit.counters.null_attempts) return 1;
    // The former narrow margin admitted this 80-cp surplus. Search legal moves
    // instead; no child main-search nodes exist at depth one to mask the result.
    search.init(fen, nullptr);
    search.pvs(expected - 81, expected - 80, 1);
    if (search.nnue_audit.counters.beta_cutoffs || search.nodes <= 1) return 1;
    search.init(fen, nullptr);
    if (search.pvs(expected - 101, expected - 100, 1) != expected
            || search.nnue_audit.counters.beta_cutoffs != 1 || search.nodes != 1) return 1;
    for (int surplus : {49, 50}) {
        search.init(fen, nullptr);
        search.beta_pruning = false;
        search.null_enabled = true;
        search.pvs(expected - surplus - 1, expected - surplus, 1);
        if (search.nnue_audit.counters.null_attempts != uint64_t(surplus == 50)
                || search.brd.to_string() != fen || search.brd.ply) return 1;
    }
    search.init(fen, nullptr);
    search.game.max_depth = 3;
    search.game.max_nodes = 128;
    search.go();
    if (search.nodes != 128 || !search.stop_all || search.brd.ply
            || search.brd.to_string() != fen || evaluate(&search) != expected) return 1;
    options::get_option("NNUEAuditBetaPruning")->value = 0;
    options::get_option("NNUEAuditHorizon")->value = 0;
    std::cout << "Live lane/cache corruption detected; audited cancellation/restoration passed\n";
}
