#include "engine.h"
#include "nnue_audit.h"

namespace {
int failure(int line) {
    std::cerr << "NNUE search audit check failed at line " << line << '\n';
    return 1;
}
}

int main(int argc, char ** argv) {
    if (argc != 2) return failure(__LINE__);
    magic::init();
    uci::silent(true);
    options::get_option("UseNNUE")->value = 1;
    options::get_option("OwnBook")->value = 0;
    std::string error;
    if (!nnue::set_path(argv[1], error)) return failure(__LINE__);
    const char * fen = "rnbqkbnr/pppppppp/8/8/8/8/PPPPPPPP/RNBQKBNR w KQkq - 0 1";
    auto storage = std::make_unique<search_t>(fen);
    auto & search = *storage;
    const int expected = evaluate(&search);
    // Detect corrupted lanes even when clipping leaves the final score unchanged.
    ++search.evaluation_accumulators[0].values[0][0];
    bool caught_lane = false, caught_cache = false;
    try { evaluate(&search); } catch (const std::runtime_error &) { caught_lane = true; }
    search.nnue_audit.mode = nnue::audit_mode_t::full;
    if (evaluate(&search) != expected) return failure(__LINE__);
    ++search.stack->eval_result;
    try { evaluate(&search); } catch (const std::runtime_error &) { caught_cache = true; }
    if (!caught_lane || !caught_cache) return failure(__LINE__);
    search.init(fen, nullptr);
    // Verify that the recorder captures a genuine eligible beta cutoff.
    trans_table::clear();
    search.nnue_audit.record_cutoffs = true;
    const int lower = expected - 201, upper = expected - 200;
    const int cutoff = search.pvs(lower, upper, 1);
    if (std::abs(upper) < score::TABLEBASE - MAX_PLY) {
        if (cutoff != expected || search.nnue_audit.beta_cutoffs.size() != 1) return failure(__LINE__);
        const auto & event = search.nnue_audit.beta_cutoffs.front();
        if (event.fen != fen || event.alpha != lower || event.beta != upper || event.eval != expected) return failure(__LINE__);
    }
    search.init(fen, nullptr);
    search.game.max_nodes = 4000;
    search.game.max_depth = 32;
    search.go();
    if (search.nodes != 4000 || search.brd.ply || !search.nnue_audit.counters.calls) return failure(__LINE__);
    if (evaluate(&search) != expected) return failure(__LINE__);
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
            || search.brd.ply || search.brd.to_string() != fen) return failure(__LINE__);
    options::get_option("NNUEAuditHorizon")->value = 2;
    search.init(fen, nullptr);
    options::get_option("NNUEAuditHorizon")->value = 0;
    if (!search.nnue_audit.aligned_quiescence || search.beta_pruning || search.ffp_enabled
            || search.null_enabled || search.lmr_enabled || search.extension(&search.root.moves[0].move, 2)) return failure(__LINE__);
    options::get_option("NNUEAuditHorizon")->value = 2;
    options::get_option("UseNNUE")->value = 0;
    search.init(fen, nullptr);
    if (search.evaluation_network || !search.nnue_audit.aligned_quiescence
            || search.beta_pruning || search.ffp_enabled || search.null_enabled || search.lmr_enabled
            || search.nnue_audit.q_delta || search.nnue_audit.q_see
            || search.extension(&search.root.moves[0].move, 2)) return failure(__LINE__);
    search.game.max_depth = 3;
    search.go();
    if (search.completed_depth != 3 || search.brd.ply || search.brd.to_string() != fen) return failure(__LINE__);
    options::get_option("NNUEAuditHorizon")->value = 1;
    search.init(fen, nullptr);
    if (search.nnue_audit.fixed_horizon || search.nnue_audit.aligned_quiescence) return failure(__LINE__);
    options::get_option("NNUEAuditHorizon")->value = 0;
    options::get_option("UseNNUE")->value = 1;
    options::get_option("NNUEAuditHorizon")->value = 2;
    options::get_option("NNUEAuditNullMove")->value = 1;
    search.init(fen, nullptr);
    if (!search.null_enabled || search.beta_pruning || search.ffp_enabled || search.lmr_enabled
            || search.nnue_audit.q_delta || search.nnue_audit.q_see) return failure(__LINE__);
    search.game.max_depth = 3;
    search.go();
    if (search.completed_depth != 3 || search.brd.ply || search.brd.to_string() != fen
            || !search.nnue_audit.counters.null_attempts || !search.nnue_audit.counters.null_cutoffs) return failure(__LINE__);
    search.init(fen, nullptr);
    search.game.max_depth = 3;
    search.game.max_nodes = 128;
    search.go();
    if (search.nodes != 128 || !search.stop_all || search.brd.ply
            || search.brd.to_string() != fen || evaluate(&search) != expected) return failure(__LINE__);
    options::get_option("NNUEAuditHorizon")->value = 0;
    options::get_option("NNUEAuditNullMove")->value = 0;
    options::get_option("NNUEAuditHorizon")->value = 2;
    options::get_option("NNUEAuditBetaPruning")->value = 1;
    search.init(fen, nullptr);
    if (!search.beta_pruning || search.null_enabled || search.ffp_enabled || search.lmr_enabled
            || search.nnue_audit.q_delta || search.nnue_audit.q_see) return failure(__LINE__);
    search.game.max_depth = 3;
    search.go();
    if (search.completed_depth != 3 || search.brd.ply || search.brd.to_string() != fen
            || !search.nnue_audit.counters.beta_candidates
            || search.nnue_audit.counters.null_attempts) return failure(__LINE__);
    // The former narrow margin admitted this 80-cp surplus. Search legal moves
    // instead; no child main-search nodes exist at depth one to mask the result.
    search.init(fen, nullptr);
    search.pvs(expected - 81, expected - 80, 1);
    if (search.nnue_audit.counters.beta_cutoffs || search.nodes <= 1) return failure(__LINE__);
    search.init(fen, nullptr);
    if (search.pvs(expected - 101, expected - 100, 1) != expected
            || search.nnue_audit.counters.beta_cutoffs != 1 || search.nodes != 1) return failure(__LINE__);
    for (int surplus : {49, 50}) {
        search.init(fen, nullptr);
        search.beta_pruning = false;
        search.null_enabled = true;
        search.pvs(expected - surplus - 1, expected - surplus, 1);
        if (search.nnue_audit.counters.null_attempts != uint64_t(surplus == 50)
                || search.brd.to_string() != fen || search.brd.ply) return failure(__LINE__);
    }
    search.init(fen, nullptr);
    search.game.max_depth = 3;
    search.game.max_nodes = 128;
    search.go();
    if (search.nodes != 128 || !search.stop_all || search.brd.ply
            || search.brd.to_string() != fen || evaluate(&search) != expected) return failure(__LINE__);
    options::get_option("NNUEAuditBetaPruning")->value = 0;
    options::get_option("NNUEAuditFutility")->value = 1;
    search.init(fen, nullptr);
    if (!search.ffp_enabled || search.null_enabled || search.beta_pruning || search.lmr_enabled) return failure(__LINE__);
    search.pvs(expected + 299, expected + 300, 1);
    if (!search.nnue_audit.counters.ffp_candidates || search.nnue_audit.counters.ffp_prunes) return failure(__LINE__);
    search.init(fen, nullptr);
    search.pvs(expected + 300, expected + 301, 1);
    if (!search.nnue_audit.counters.ffp_prunes || search.brd.to_string() != fen || search.brd.ply) return failure(__LINE__);
    search.init(fen, nullptr);
    search.game.max_depth = 3;
    search.game.max_nodes = 128;
    search.go();
    if (search.nodes != 128 || !search.stop_all || search.brd.ply
            || search.brd.to_string() != fen || evaluate(&search) != expected) return failure(__LINE__);
    options::get_option("NNUEAuditFutility")->value = 0;
    options::get_option("NNUEAuditDelta")->value = 1;
    for (bool neural : {false, true}) {
        options::get_option("UseNNUE")->value = neural;
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
                        || probe->brd.to_string() != capture_fen) return failure(__LINE__);
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
        if (probe->nodes != 2 || probe->brd.ply || probe->brd.to_string() != protected_fen) return failure(__LINE__);
    }
    options::get_option("UseNNUE")->value = 1;
    search.init(fen, nullptr);
    if (!search.nnue_audit.q_delta || search.nnue_audit.q_see) return failure(__LINE__);
    search.game.max_nodes = 128;
    search.game.max_depth = 3;
    search.go();
    if (search.nodes != 128 || !search.stop_all || search.brd.ply
            || search.brd.to_string() != fen || evaluate(&search) != expected) return failure(__LINE__);
    options::get_option("NNUEAuditDelta")->value = 0;
    options::get_option("NNUEAuditSEE")->value = 1;
    for (bool neural : {false, true}) {
        options::get_option("UseNNUE")->value = neural;
        // Both knight captures lose 225 cp. SEE alone must reject them even
        // when alpha is too close to stand pat for capture-gain delta pruning.
        const char * losing_fen = "r3k2r/ppp2ppp/2n1bn2/3p4/3Pp3/2N1BN2/PPP2PPP/R2QK2R w KQkq - 0 1";
        auto probe = std::make_unique<search_t>(losing_fen);
        const int alpha = evaluate(probe.get()) + 1;
        probe->qsearch(alpha, alpha + 1, -1);
        if (probe->nodes != 1 || probe->nnue_audit.counters.q_see_candidates != 2
                || probe->nnue_audit.counters.q_see_prunes != 2
                || probe->nnue_audit.counters.q_delta_candidates || probe->brd.ply
                || probe->brd.to_string() != losing_fen) return failure(__LINE__);
        // The e6 pawn is pinned to e8 by Re1. Nc3xd5 wins a pawn and
        // must survive SEE pruning in the real qsearch with either evaluator.
        const char * pinned_fen = "rnb1kbnr/ppp3pp/4p3/3p4/8/2N5/PPP2PPP/R3R1K1 w - - 0 1";
        probe->init(pinned_fen, nullptr);
        move_t pinned_move;
        pinned_move.set(WKNIGHT, c3, d5, BPAWN);
        if (!probe->brd.legal(&pinned_move) || probe->brd.see(&pinned_move) != 100) return failure(__LINE__);
        const int pinned_alpha = evaluate(probe.get()) + 1;
        probe->qsearch(pinned_alpha, pinned_alpha + 1, -1);
        if (probe->nodes <= 1 || !probe->nnue_audit.counters.q_see_candidates
                || probe->brd.ply || probe->brd.to_string() != pinned_fen) return failure(__LINE__);
        // c4xd5 is an equal exchange. A high alpha must not make SEE discard it.
        const char * equal_fen = "r3k2r/ppp2ppp/2n1bn2/3p4/2PPp3/4BN2/PPP2PPP/R2QK2R w KQkq - 0 1";
        probe->init(equal_fen, nullptr);
        move_t equal_move;
        equal_move.set(WPAWN, c4, d5, BPAWN);
        if (!probe->brd.legal(&equal_move) || probe->brd.see(&equal_move) != 0) return failure(__LINE__);
        const int high_alpha = evaluate(probe.get()) + 5000;
        probe->qsearch(high_alpha, high_alpha + 1, -1);
        if (probe->nodes <= 1 || !probe->nnue_audit.counters.q_see_candidates
                || probe->brd.ply || probe->brd.to_string() != equal_fen) return failure(__LINE__);
        // Losing captures remain searchable when they check or when the
        // opponent is already below the post-capture material threshold.
        for (const char * protected_fen : {
                "r3k2r/ppp2ppp/2n5/8/2B5/2N5/PPP2PPP/R3K2R w KQkq - 0 1",
                "r3k2r/ppp2ppp/4p3/3p4/3P4/2N5/PPP2PPP/R3K2R w KQkq - 0 1"}) {
            probe->init(protected_fen, nullptr);
            const int protected_alpha = evaluate(probe.get()) + 5000;
            probe->qsearch(protected_alpha, protected_alpha + 1, -1);
            if (probe->nodes <= 1 || probe->nnue_audit.counters.q_see_prunes
                    || probe->brd.ply || probe->brd.to_string() != protected_fen) return failure(__LINE__);
        }
    }
    options::get_option("UseNNUE")->value = 1;
    search.init(fen, nullptr);
    if (search.nnue_audit.q_delta || !search.nnue_audit.q_see) return failure(__LINE__);
    search.game.max_nodes = 128;
    search.game.max_depth = 3;
    search.go();
    if (search.nodes != 128 || !search.stop_all || search.brd.ply
            || search.brd.to_string() != fen || evaluate(&search) != expected) return failure(__LINE__);
    options::get_option("NNUEAuditSEE")->value = 0;
    options::get_option("NNUEAuditQuietChecks")->value = 1;
    for (bool neural : {false, true}) {
        options::get_option("UseNNUE")->value = neural;
        const char * queen_fen = "6k1/8/8/8/8/8/6r1/K2Q4 w - - 0 1";
        auto probe = std::make_unique<search_t>(queen_fen);
        // Independent legal-move census: Qb3+, Qd5+, Qd8+ are safe;
        // Qg4+ hangs the queen to Rg2xg4. There are no root captures.
        const int alpha = evaluate(probe.get()) + 5000;
        probe->qsearch(alpha, alpha + 1, 0);
        const auto & counts = probe->nnue_audit.counters;
        if (counts.q_check_candidates != 4 || counts.q_check_rejected != 1
                || counts.q_check_searched != 3 || counts.q_castles
                || probe->brd.ply || probe->brd.to_string() != queen_fen) return failure(__LINE__);
        probe->init(queen_fen, nullptr);
        probe->qsearch(alpha, alpha + 1, -1);
        if (probe->nodes != 1 || probe->nnue_audit.counters.q_check_candidates) return failure(__LINE__);
        const char * counter_fen = "r5k1/8/8/8/8/8/8/KQ6 w - - 0 1";
        probe->init(counter_fen, nullptr);
        probe->stack->in_check = probe->brd.in_check();
        move_t counter_move{};
        counter_move.set(&probe->brd, "b1a2");
        if (!probe->stack->in_check || !probe->brd.legal(&counter_move)
                || !probe->brd.gives_check(&counter_move) || probe->brd.see(&counter_move, 0)) return failure(__LINE__);
        probe->qsearch(-score::INF, score::INF, -1);
        if (probe->nodes <= 1 || probe->nnue_audit.counters.q_check_candidates
                || probe->brd.ply || probe->brd.to_string() != counter_fen) return failure(__LINE__);
        options::get_option("NNUEAuditCastling")->value = 1;
        for (const char * castle_fen : {
                "5k2/8/8/8/8/8/8/4K2R w K - 0 1",
                "4k3/8/8/8/8/8/8/4K2R w K - 0 1"}) {
            probe->init(castle_fen, nullptr);
            const int castle_alpha = evaluate(probe.get()) + 5000;
            probe->qsearch(castle_alpha, castle_alpha + 1, 0);
            if (probe->nnue_audit.counters.q_castles != 1 || probe->brd.ply
                    || probe->brd.to_string() != castle_fen) return failure(__LINE__);
            probe->init(castle_fen, nullptr);
            probe->qsearch(castle_alpha, castle_alpha + 1, -1);
            if (probe->nodes != 1 || probe->nnue_audit.counters.q_castles) return failure(__LINE__);
        }
        options::get_option("NNUEAuditCastling")->value = 0;
    }
    options::get_option("UseNNUE")->value = 1;
    search.init(fen, nullptr);
    search.game.max_nodes = 128;
    search.game.max_depth = 3;
    search.go();
    if (search.nodes != 128 || !search.stop_all || search.brd.ply
            || search.brd.to_string() != fen || evaluate(&search) != expected) return failure(__LINE__);
    options::get_option("NNUEAuditQuietChecks")->value = 0;
    options::get_option("NNUEAuditHorizon")->value = 0;
    std::cout << "Live lane/cache corruption detected; audited cancellation/restoration passed\n";
}
