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
    const int lower = expected - 101, upper = expected - 100;
    const int cutoff = search.pvs(lower, upper, 1);
    if (std::abs(upper) < score::TABLEBASE - MAX_PLY) {
        if (cutoff != expected - 50 || search.nnue_audit.beta_cutoffs.size() != 1) return 1;
        const auto & event = search.nnue_audit.beta_cutoffs.front();
        if (event.fen != fen || event.alpha != lower || event.beta != upper || event.eval != expected) return 1;
    }
    search.init(fen, nullptr);
    search.game.max_nodes = 4000;
    search.game.max_depth = 32;
    search.go();
    if (search.nodes != 4000 || search.brd.ply || !search.nnue_audit.counters.calls) return 1;
    if (evaluate(&search) != expected) return 1;
    std::cout << "Live lane/cache corruption detected; audited cancellation/restoration passed\n";
}
