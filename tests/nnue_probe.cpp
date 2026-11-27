// Test-only bridge for MaxDrone's serialized-network validation; no UCI extension.
#include "engine.h"
#include "movegen.h"
#include "nnue.h"
#include <memory>
#include <stdexcept>

namespace {
void report(search_t & search) {
    auto & accumulator = search.evaluation_accumulators[search.brd.ply];
    const int incremental = search.evaluation_network->evaluate(search.brd, accumulator,
        std::span<nnue::accumulator_t>(search.evaluation_accumulators.data(), search.brd.ply));
    nnue::accumulator_t fresh;
    const int recomputed = search.evaluation_network->evaluate(search.brd, fresh);
    if (incremental != recomputed || accumulator.values != fresh.values) {
        throw std::runtime_error("incremental/fresh NNUE mismatch");
    }
    std::cout << search.brd.to_string() << '|' << recomputed << '|'
        << nnue::to_centipawns(recomputed) << '\n';
}
}

int main(int argc, char ** argv) {
    if (argc != 2) {
        return 1;
    }
    try {
        magic::init();
        uci::silent(true);
        std::string error;
        if (!nnue::set_path(argv[1], error)) {
            throw std::runtime_error(error);
        }
        std::string fen;
        while (std::getline(std::cin, fen)) {
            auto search = std::make_unique<search_t>(fen.c_str());
            report(*search);
            move::list_t moves;
            move::gen_captures(&search->brd, &moves);
            move::gen_promotions(&search->brd, &moves);
            move::gen_quiet_moves(&search->brd, &moves);
            move::gen_castles(&search->brd, &moves);
            for (auto move = moves.first; move != moves.last; ++move) {
                if (!search->brd.legal(move)) {
                    continue;
                }
                search->forward(move, search->brd.gives_check(move));
                report(*search);
                search->backward(move);
            }
        }
    } catch (const std::exception & error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
    return 0;
}
