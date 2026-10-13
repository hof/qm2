#include "engine.h"
#include "movegen.h"
#include "nnue.h"
#include <random>

// Keep the independent reference's piece/color names out of the engine scope.
namespace reference {
#include "../third_party/nnue_reference/nnue.h"
}

namespace {
size_t comparisons = 0;

bool compare(search_t & search) {
    const auto fen = search.brd.to_string();
    const int reference_score = reference::nnue_evaluate_fen(fen.c_str());
    nnue::accumulator_t fresh;
    const int recomputed = search.evaluation_network->evaluate(search.brd, fresh);
    auto & accumulator = search.evaluation_accumulators[search.brd.ply];
    const auto * previous = search.brd.ply ? &search.evaluation_accumulators[search.brd.ply - 1] : nullptr;
    const int incremental = search.evaluation_network->evaluate(search.brd, accumulator, previous);
    ++comparisons;
    // Independently refreshed reference accumulators also check the feature map
    // and every loaded feature weight, rather than only the final score.
    constexpr int piece_map[] = {0, 6, 5, 4, 3, 2, 1, 12, 11, 10, 9, 8, 7};
    int pieces[33] = {reference::wking, reference::bking};
    int squares[33] = {search.brd.get_sq(WKING), search.brd.get_sq(BKING)};
    int count = 2;
    for (int square = 0; square < 64; ++square) {
        const int piece = search.brd.matrix[square];
        if (!piece || piece == WKING || piece == BKING) continue;
        pieces[count] = piece_map[piece];
        squares[count++] = square;
    }
    reference::NNUEdata reference_data{};
    reference::NNUEdata * reference_states[] = {&reference_data, nullptr, nullptr};
    const int reference_refresh = reference::nnue_evaluate_incremental(
        search.brd.us() ? reference::white : reference::black, pieces, squares, reference_states);
    bool success = reference_score == recomputed && reference_score == incremental
        && reference_score == reference_refresh && fresh.values == accumulator.values;
    for (int color = 0; color < 2; ++color)
        for (int lane = 0; lane < 256; ++lane)
            success &= fresh.values[color][lane] == reference_data.accumulator.accumulation[!color][lane];
    if (!search.brd.in_check()) success &= evaluate(&search) == reference_score * 100 / 208;
    if (!success) std::cerr << fen << ": reference=" << reference_score << " full=" << recomputed
        << " incremental=" << incremental << '\n';
    return success;
}

bool test_moves(const char * fen) {
    auto storage = std::make_unique<search_t>(fen);
    auto & search = *storage;
    bool success = compare(search);
    const U64 original_hash = search.brd.stack->tt_key;
    const std::string original_fen = search.brd.to_string();
    const auto original_accumulator = search.evaluation_accumulators[0];
    move::list_t moves;
    move::gen_captures(&search.brd, &moves);
    move::gen_promotions(&search.brd, &moves);
    move::gen_quiet_moves(&search.brd, &moves);
    move::gen_castles(&search.brd, &moves);
    for (auto move = moves.first; move != moves.last; ++move) {
        if (!search.brd.legal(move)) continue;
        search.forward(move, search.brd.gives_check(move));
        success &= compare(search);
        search.backward(move);
        success &= compare(search);
        if (search.brd.stack->tt_key != original_hash || search.brd.to_string() != original_fen
                || search.evaluation_accumulators[0].values != original_accumulator.values) {
            std::cerr << "Make/unmake changed parent board/hash/accumulator\n";
            success = false;
        }
    }
    if (!search.brd.in_check()) {
        search.forward();
        success &= compare(search);
        search.backward();
        success &= compare(search);
    }
    return success;
}
}

int main(int argc, char ** argv) {
    if (argc != 2) return 1;
    magic::init();
    uci::silent(true);
    std::string error;
    if (!nnue::set_path(argv[1], error)) {
        std::cerr << error << '\n';
        return 1;
    }
    reference::nnue_init(argv[1]);
    options::get_option("UseNNUE")->value = 1;
    options::get_option("OwnBook")->value = 0;
    bool success = true;
    const char * start = "rnbqkbnr/pppppppp/8/8/8/8/PPPPPPPP/RNBQKBNR w KQkq - 0 1";
    for (const char * fen : {start,
            "r3k2r/ppp2ppp/2n5/3pp3/3PP3/2N5/PPP2PPP/R3K2R w KQkq - 0 1",
            "r3k2r/ppp2ppp/2n5/3pp3/3PP3/2N5/PPP2PPP/R3K2R b KQkq - 0 1",
            "7k/8/8/3pP3/8/8/8/K7 w - d6 0 1",
            "7k/8/8/8/3Pp3/8/8/K7 b - d3 0 1",
            "1r5k/P7/8/8/8/8/8/K7 w - - 0 1",
            "7k/8/8/8/8/8/p7/1R5K b - - 0 1",
            "7k/8/8/8/8/8/3R4/K7 w - - 0 1"})
        success &= test_moves(fen);
    std::mt19937 random(20261013);
    for (int game = 0; game < 16; ++game) {
        auto storage = std::make_unique<search_t>(start);
        auto & search = *storage;
        std::vector<move_t> played;
        for (int ply = 0; ply < 100; ++ply) {
            // Sometimes skip evaluation: child must refresh when parent isn't ready.
            if (ply % 5 != 2) success &= compare(search);
            move::list_t moves;
            move::gen_captures(&search.brd, &moves);
            move::gen_promotions(&search.brd, &moves);
            move::gen_quiet_moves(&search.brd, &moves);
            move::gen_castles(&search.brd, &moves);
            std::vector<move_t> legal;
            for (auto move = moves.first; move != moves.last; ++move)
                if (search.brd.legal(move)) legal.push_back(*move);
            if (legal.empty()) break;
            played.push_back(legal[random() % legal.size()]);
            auto & move = played.back();
            search.forward(&move, search.brd.gives_check(&move));
        }
        while (!played.empty()) {
            success &= compare(search);
            search.backward(&played.back());
            played.pop_back();
        }
        success &= compare(search);
        if (search.brd.to_string() != start) success = false;
    }
    // Exercise the final allocated accumulator and restoration from that depth.
    // This is a storage-boundary fixture, not a legal sequence of chess moves.
    {
        auto boundary_storage = std::make_unique<search_t>(start);
        auto & boundary = *boundary_storage;
        success &= compare(boundary);
        for (int ply = 0; ply < MAX_PLY; ++ply) {
            boundary.forward();
            success &= compare(boundary);
        }
        for (int ply = MAX_PLY; ply > 0; --ply) {
            boundary.backward();
            success &= compare(boundary);
        }
        success &= boundary.brd.ply == 0 && boundary.brd.to_string() == start;
    }
    std::cout << comparisons << " full/incremental/reference comparisons" << std::endl;
    // Run the actual search, including node-budget cancellation and restart.
    trans_table::clear();
    auto storage = std::make_unique<search_t>(start);
    auto & search = *storage;
    search.game.max_nodes = 4000;
    search.game.max_depth = 32;
    search.go();
    success &= search.stop_all && search.nodes == 4000 && search.brd.ply == 0;
    success &= compare(search);
    search.init(start, nullptr, nullptr);
    search.game.max_depth = 3;
    search.go();
    success &= !search.stop_all && search.completed_depth == 3 && search.brd.ply == 0;
    success &= compare(search);
    // Isolate dense inference throughput with a warmed accumulator.
    const auto began = std::chrono::steady_clock::now();
    int64_t checksum = 0;
    for (int i = 0; i < 20000; ++i)
        checksum += search.evaluation_network->evaluate(search.brd, search.evaluation_accumulators[0]);
    const auto elapsed = std::chrono::duration<double>(std::chrono::steady_clock::now() - began).count();
    std::cout << comparisons << " comparisons; 20000 warm evaluations in " << elapsed
        << " s; checksum " << checksum << '\n';
    return success ? 0 : 1;
}
