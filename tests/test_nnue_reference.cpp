#include "engine.h"
#include "movegen.h"
#include "nnue.h"
#include <bit>
#include <fstream>
#include <random>

namespace {
// Independent, fresh-only scalar oracle. File rows and wide dot products are
// deliberately kept separate from the engine's feature tables and packed SIMD.
struct reference_t {
    std::array<int16_t, 256> biases;
    std::vector<int16_t> features;
    std::array<int32_t, 32> bias1, bias2;
    std::array<int8_t, 512 * 32> weights1;
    std::array<int8_t, 32 * 32> weights2;
    std::array<int8_t, 32> output_weights;
    int32_t output_bias;

    explicit reference_t(const char * path) : features(22528 * 256) {
        std::ifstream file(path, std::ios::binary);
        auto read = [&]<typename value_t>(value_t & value) {
            using unsigned_t = std::make_unsigned_t<value_t>;
            unsigned_t bits = 0;
            for (size_t i = 0; i < sizeof(value_t); ++i) {
                const int byte = file.get();
                if (byte == std::char_traits<char>::eof()) {
                    throw std::runtime_error("Truncated reference model");
                }
                bits |= unsigned_t(byte) << (8 * i);
            }
            value = std::bit_cast<value_t>(bits);
        };
        auto row = [&](auto & values) {
            for (auto & value : values) {
                read(value);
            }
        };
        file.seekg(193);
        row(biases);
        row(features);
        file.seekg(4, std::ios::cur);
        row(bias1);
        row(weights1);
        row(bias2);
        row(weights2);
        read(output_bias);
        row(output_weights);
        if (file.peek() != std::char_traits<char>::eof()) {
            throw std::runtime_error("Trailing reference model bytes");
        }
    }

    auto refresh(const board_t & board) const {
        std::array<std::array<int16_t, 256>, 2> result;
        for (int perspective = 0; perspective < 2; ++perspective) {
            result[perspective] = biases;
            const int king = board.get_sq(perspective == WHITE ? WKING : BKING);
            const int vertical = perspective == WHITE ? 0 : 56;
            const int horizontal = king % 8 < 4 ? 7 : 0;
            const int oriented_king = king ^ vertical ^ horizontal;
            const int bucket = oriented_king / 8 * 4 + oriented_king % 8 - 4;
            for (int square = 0; square < 64; ++square) {
                const int piece = board.matrix[square];
                if (!piece) {
                    continue;
                }
                const int type = (piece - 1) % 6;
                const bool friendly = (piece <= WKING) == (perspective == WHITE);
                const int piece_bucket = type == 5 ? 10 : 2 * type + !friendly;
                const int index = bucket * 704 + piece_bucket * 64 + (square ^ vertical ^ horizontal);
                for (size_t lane = 0; lane < 256; ++lane) {
                    const uint16_t sum = uint16_t(result[perspective][lane])
                        + uint16_t(features[index * 256 + lane]);
                    result[perspective][lane] = std::bit_cast<int16_t>(sum);
                }
            }
        }
        return result;
    }

    template<size_t count>
    static auto dense(const std::array<int, count> & inputs,
            const std::array<int8_t, count * 32> & weights, const std::array<int32_t, 32> & biases) {
        std::array<int, 32> output;
        for (size_t row = 0; row < 32; ++row) {
            int64_t sum = biases[row];
            for (size_t column = 0; column < count; ++column) {
                sum += int64_t(inputs[column]) * weights[row * count + column];
            }
            output[row] = static_cast<int>(std::clamp<int64_t>(sum / 64, 0, 127));
        }
        return output;
    }

    int evaluate(const board_t & board, const std::array<std::array<int16_t, 256>, 2> & values) const {
        std::array<int, 512> inputs;
        for (size_t i = 0; i < inputs.size(); ++i) {
            const int color = i < 256 ? board.us() : !board.us();
            inputs[i] = std::clamp<int>(values[color][i % 256], 0, 127);
        }
        const auto first = dense(inputs, weights1, bias1);
        const auto second = dense(first, weights2, bias2);
        int64_t sum = output_bias;
        for (size_t i = 0; i < 32; ++i) {
            sum += int64_t(second[i]) * output_weights[i];
        }
        return static_cast<int>(sum / 16);
    }
};

std::unique_ptr<reference_t> reference;
size_t comparisons = 0;

bool compare(search_t & search) {
    const auto fen = search.brd.to_string();
    const auto reference_values = reference->refresh(search.brd);
    const int reference_score = reference->evaluate(search.brd, reference_values);
    nnue::accumulator_t fresh;
    const int recomputed = search.evaluation_network->evaluate(search.brd, fresh);
    auto & accumulator = search.evaluation_accumulators[search.brd.ply];
    const int incremental = search.evaluation_network->evaluate(search.brd, accumulator,
        std::span<nnue::accumulator_t>(search.evaluation_accumulators.data(), search.brd.ply));
    ++comparisons;
    bool success = reference_score == recomputed && reference_score == incremental
        && reference_values == fresh.values && fresh.values == accumulator.values;
    if (!search.brd.in_check()) {
        success &= evaluate(&search)
            == search.corrected_static_eval(scale_network_score(reference_score,
                search.brd, search.brd.stack->fifty_count));
    }
    if (!success) {
        std::cerr << fen << ": reference=" << reference_score << " full=" << recomputed
            << " incremental=" << incremental << '\n';
    }
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
        if (!search.brd.legal(move)) {
            continue;
        }
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
    if (argc != 2) {
        return 1;
    }
    magic::init();
    uci::silent(true);
    std::string error;
    if (!nnue::set_path(argv[1], error)) {
        std::cerr << error << '\n';
        return 1;
    }
    reference = std::make_unique<reference_t>(argv[1]);
    options::get_option("OwnBook")->value = 0;
    bool success = true;
    const char * start = "rnbqkbnr/pppppppp/8/8/8/8/PPPPPPPP/RNBQKBNR w KQkq - 0 1";
    for (const char * fen : {start,
            "r1bq1rk1/pppnppbp/6p1/8/3PB3/5N2/PPP2PPP/R1BQR1K1 b - - 0 1",
            "r3k2r/ppp2ppp/2n5/3pp3/3PP3/2N5/PPP2PPP/R3K2R w KQkq - 0 1",
            "r3k2r/ppp2ppp/2n5/3pp3/3PP3/2N5/PPP2PPP/R3K2R b KQkq - 0 1",
            "7k/8/8/3pP3/8/8/8/K7 w - d6 0 1",
            "7k/8/8/8/3Pp3/8/8/K7 b - d3 0 1",
            "1r5k/P7/8/8/8/8/8/K7 w - - 0 1",
            "7k/8/8/8/8/8/p7/1R5K b - - 0 1",
            "7k/8/8/8/8/8/3R4/K7 w - - 0 1"}) {
        success &= test_moves(fen);
    }
    std::mt19937 random(20261013);
    for (int game = 0; game < 16; ++game) {
        auto storage = std::make_unique<search_t>(start);
        auto & search = *storage;
        std::vector<move_t> played;
        for (int ply = 0; ply < 100; ++ply) {
            // Skip several evaluations so inference must find a ready ancestor or refresh.
            if (ply % 7 < 2) {
                success &= compare(search);
            }
            move::list_t moves;
            move::gen_captures(&search.brd, &moves);
            move::gen_promotions(&search.brd, &moves);
            move::gen_quiet_moves(&search.brd, &moves);
            move::gen_castles(&search.brd, &moves);
            std::vector<move_t> legal;
            for (auto move = moves.first; move != moves.last; ++move) {
                if (search.brd.legal(move)) {
                    legal.push_back(*move);
                }
            }
            if (legal.empty()) {
                break;
            }
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
        if (search.brd.to_string() != start) {
            success = false;
        }
    }
    // Replay through deliberately unevaluated parents. These paths include
    // promotion plus capture, en passant, castling, and king-perspective changes.
    for (const auto & fixture : {
            std::pair{"1r5k/Pppppppp/8/8/8/8/1PPPPPPP/K7 w - - 0 1",
                std::vector<const char *>{"a7b8n", "b7b6", "b8c6"}},
            std::pair{"7k/ppp2ppp/8/3pP3/8/8/PPP2PPP/K7 w - d6 0 1",
                std::vector<const char *>{"e5d6", "a7a6", "a2a3"}},
            std::pair{"r3k2r/ppp2ppp/8/8/8/8/PPP2PPP/R3K2R w KQkq - 0 1",
                std::vector<const char *>{"a2a3", "a7a6", "e1g1", "h7h6"}}}) {
        auto storage = std::make_unique<search_t>(fixture.first);
        auto & search = *storage;
        success &= compare(search);
        const auto original_values = search.evaluation_accumulators[0].values;
        std::vector<move_t> moves;
        for (const char * text : fixture.second) {
            move_t move;
            move.set(&search.brd, text);
            if (!search.brd.valid(&move) || !search.brd.legal(&move)) {
                std::cerr << "Illegal skipped-accumulator fixture move: " << text << '\n';
                return 1;
            }
            moves.push_back(move);
            search.forward(&moves.back(), search.brd.gives_check(&moves.back()));
        }
        success &= compare(search);
        success &= search.evaluation_accumulators[0].values == original_values;
        while (!moves.empty()) {
            search.backward(&moves.back());
            moves.pop_back();
            success &= compare(search);
        }
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
    return success ? 0 : 1;
}
