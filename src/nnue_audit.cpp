#include "engine.h"
#include "nnue_audit.h"
#include <sstream>
#include <stdexcept>

namespace nnue {
void audit_beta_cutoff(search_t & search, int alpha, int beta, int depth, int eval) {
    auto & state = search.nnue_audit;
    // Irreversible nodes have no relevant prior repetition history, allowing
    // independent FEN replay. Bound diagnostic storage per root position.
    if (state.record_cutoffs && state.beta_cutoffs.size() < 128 && search.brd.stack->fifty_count == 0)
        state.beta_cutoffs.push_back({search.brd.to_string(), alpha, beta, depth, search.brd.ply, eval});
}

int audit_evaluate(search_t & search) {
    auto & state = search.nnue_audit;
    ++state.counters.calls;
    const bool cached = search.stack->eval_result != score::INVALID;
    if (cached) ++state.counters.cached_returns;
    const U64 key = search.brd.stack->tt_key;
    const U64 repetition = search.brd.stack->repetition_key();
    const int clock = search.brd.stack->fifty_count;
    accumulator_t fresh;
    const int fresh_raw = search.evaluation_network->evaluate(search.brd, fresh);
    auto & live = search.evaluation_accumulators[search.brd.ply];
    const auto * parent = search.brd.ply ? &search.evaluation_accumulators[search.brd.ply - 1] : nullptr;
    const int live_raw = state.mode == audit_mode_t::full ? fresh_raw
        : search.evaluation_network->evaluate(search.brd, live, parent, &state.counters);
    if (state.mode == audit_mode_t::full) live = fresh;
    const int expected = to_centipawns(fresh_raw);
    const bool stale_metadata = cached && (!search.stack->mt || !search.stack->pt
        || search.stack->mt->key != search.brd.stack->material_hash
        || search.stack->pt->key != search.brd.stack->pawn_hash);
    if (live_raw != fresh_raw || live.values != fresh.values || live.kings != fresh.kings
            || (cached && search.stack->eval_result != expected)
            || stale_metadata
            || key != search.brd.stack->tt_key || repetition != search.brd.stack->repetition_key()
            || clock != search.brd.stack->fifty_count) {
        std::ostringstream message;
        message << "NNUE search audit mismatch: root=" << state.root_fen
            << " fen=" << search.brd.to_string() << " ply=" << search.brd.ply
            << " side=" << search.brd.us() << " key=" << key
            << " model=" << search.evaluation_network->fingerprint()
            << " raw=" << live_raw << " fresh_raw=" << fresh_raw
            << " cached=" << search.stack->eval_result << " expected=" << expected
            << " stale_metadata=" << stale_metadata << " path=";
        for (int ply = 0; ply < search.brd.ply; ++ply)
            message << (search._stack[ply].current_move.piece
                ? search._stack[ply].current_move.to_string() : "null") << ' ';
        for (int color = 0; color < 2; ++color)
            for (int lane = 0; lane < 256; ++lane)
                if (live.values[color][lane] != fresh.values[color][lane])
                    message << "lane[" << color << ',' << lane << "]=" << live.values[color][lane]
                        << '/' << fresh.values[color][lane] << ' ';
        throw std::runtime_error(message.str());
    }
    return expected;
}
}
