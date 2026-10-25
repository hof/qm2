#include "engine.h"
#include "nnue.h"
#include "w17/w17_eval.h"
#include "w17/w17_search.h"
#include <bit>
#include <random>
#include <filesystem>
#include <fstream>
#include <limits>

namespace {
bool check(bool condition, const char * message) {
    if (!condition) {
        std::cerr << message << '\n';
    }
    return condition;
}

void put_u32(std::vector<uint8_t> & bytes, size_t offset, uint32_t value) {
    for (int i = 0; i < 4; ++i) bytes[offset + i] = (value >> (8 * i)) & 255;
}

void write_file(const std::filesystem::path & path, const std::vector<uint8_t> & bytes) {
    std::ofstream output(path, std::ios::binary);
    output.write(reinterpret_cast<const char *>(bytes.data()), bytes.size());
    if (!output) {
        throw std::runtime_error("Cannot write test network");
    }
}

bool check_activation_clipping() {
    std::array<std::array<int16_t, 256>, 2> values;
    // Cover every signed int16 value, both perspectives, and asymmetric lane patterns.
    for (unsigned base = 0; base < 65536; base += 256) {
        for (unsigned i = 0; i < 256; ++i) {
            values[0][i] = std::bit_cast<int16_t>(static_cast<uint16_t>(base + i));
            values[1][i] = std::bit_cast<int16_t>(static_cast<uint16_t>((base + i) ^ 0xaaaa));
        }
        for (bool us : {false, true}) {
            const auto scalar = nnue::detail::clip_activations<false>(values, us);
            const auto simd = nnue::detail::clip_activations(values, us);
            for (size_t i = 0; i < 512; ++i) {
                const int expected = std::clamp<int>(values[i < 256 ? us : !us][i % 256], 0, 127);
                if (scalar[i] != expected || simd[i] != expected) {
                    return check(false, "Activation clipping changed a value, lane or perspective");
                }
            }
        }
    }
    return true;
}

// Independent wide arithmetic also checks the scalar production fallback.
template<size_t input_size>
bool check_affine() {
    std::array<uint8_t, input_size> input;
    std::array<int8_t, input_size * 32> weights;
    std::array<int32_t, 32> biases;
    std::mt19937 random(20261016);
    for (int trial = 0; trial < 300; ++trial) {
        for (auto & value : input) value = trial < 4 ? (trial == 0 ? 0 : 127) : random() % 128;
        for (auto & weight : weights) {
            if (trial < 4) {
                weight = trial == 2 ? -128 : 127;
            } else if (trial < 12) {
                weight = 0;
            } else if (trial % 3 == 0) {
                weight = static_cast<int8_t>(int(random() % 65) - 32);
            } else {
                weight = std::bit_cast<int8_t>(static_cast<uint8_t>(random()));
            }
        }
        if (trial >= 4 && trial < 12) {
            // 258 * 127 fits int16; 259 * 127 does not. Exercise both
            // signs and both SIMD pair lanes, independently of input values.
            const int sign = trial < 8 ? 1 : -1;
            const size_t pair = trial % 4 < 2 ? 0 : 2;
            for (size_t row = 0; row < 32; ++row) {
                weights[row * input_size + pair] = 127 * sign;
                weights[row * input_size + pair + 1] = 127 * sign;
                weights[row * input_size + pair + 4] = (4 + trial % 2) * sign;
            }
        }
        for (size_t row = 0; row < 32; ++row) {
            int64_t dot = 0;
            for (size_t column = 0; column < input_size; ++column)
                dot += int64_t(input[column]) * weights[row * input_size + column];
            // Include both sides of rounding/clipping boundaries and wide bias addition.
            constexpr int targets[] = {-65, -64, -1, 0, 1, 63, 64, 65, 8127, 8128, 8191, 8192};
            biases[row] = row == 0 ? INT32_MIN : row == 1 ? INT32_MAX
                : static_cast<int32_t>(targets[(trial + row) % 12] - dot);
        }
        const auto packed = nnue::detail::pack_affine_weights<input_size>(weights);
        const auto scalar = nnue::detail::affine<input_size, false>(input, packed, biases);
        const auto simd = nnue::detail::affine(input, packed, biases);
        const bool safe = nnue::detail::affine_pair_sums_safe<input_size>(packed);
        if (trial >= 4 && trial < 12 && safe != (trial % 2 == 0)) {
            return check(false, "Dense pair-sum bounds must reject either sign beyond int16");
        }
        const auto blocked = nnue::detail::affine(input, packed, biases, safe);
        for (size_t row = 0; row < 32; ++row) {
            int64_t sum = biases[row];
            for (size_t column = 0; column < input_size; ++column)
                sum += int64_t(input[column]) * weights[row * input_size + column];
            const int expected = sum <= 0 ? 0 : sum >= 127 * 64 ? 127 : sum / 64;
            if (scalar[row] != expected || simd[row] != expected || blocked[row] != expected) {
                return check(false, "Dense scalar/SIMD mismatch at an arithmetic boundary");
            }
        }
    }
    return true;
}
}

int main() {
    const auto path = std::filesystem::temp_directory_path() / ("maxima-nnue-"
        + std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()) + ".nnue");
    struct cleanup_t {
        std::filesystem::path path;
        ~cleanup_t() { std::error_code error; std::filesystem::remove(path, error); }
    } cleanup{path};
    bool unit_checks = check_activation_clipping() && check_affine<512>() && check_affine<32>();
    unit_checks &= check(nnue::to_centipawns(208) == 100
        && nnue::to_centipawns(-208) == -100 && nnue::to_centipawns(0) == 0
        && nnue::to_centipawns(std::numeric_limits<int>::max()) == score::TABLEBASE - 1
        && nnue::to_centipawns(std::numeric_limits<int>::min()) == -score::TABLEBASE + 1,
        "Stockfish units must convert to Maxima centipawns without reserved scores or overflow");
    magic::init();
    uci::silent(true);
    std::vector<uint8_t> bytes(21022697);
    const size_t network_start = 189 + 4 + 512 + 2 * 256 * 41024;
    const size_t output_bias_offset = bytes.size() - 36;
    put_u32(bytes, 0, 0x7af32f16);
    put_u32(bytes, 4, 0x3e5aa6ee);
    put_u32(bytes, 8, 177);
    put_u32(bytes, 189, 0x5d69d7b8);
    put_u32(bytes, network_start, 0x63337156);
    put_u32(bytes, output_bias_offset, 16 * 208);
    write_file(path, bytes);
    std::string error;
    bool success = unit_checks;
    success &= check(nnue::set_path(path.string(), error), "Valid HalfKP model must load");
    auto first = nnue::snapshot();
    if (!first) {
        return 1;
    }
    const char * fen = "rnbqkbnr/pppppppp/8/8/8/8/PPPPPPPP/RNBQKBNR w KQkq - 0 1";
    auto neural_storage = std::make_unique<search_t>(fen);
    auto & neural = *neural_storage;
    // The synthetic network's output bias gives every position this raw score.
    constexpr int raw = 208;
    const int start_score = scale_network_score(raw, neural.brd, 0);
    success &= check(evaluate(&neural) == start_score && neural.neural_static_score() == raw,
        "NNUE score must use the network");
    // Scaling uses only material and the clock, never another evaluator.
    auto queen_storage = std::make_unique<search_t>("7k/8/8/8/8/8/3Q4/K7 w - - 0 1");
    const int queen_score = evaluate(queen_storage.get());
    success &= check(queen_score > 0 && queen_score < start_score
        && queen_storage->neural_static_score() == raw,
        "Less material must scale the same raw score down");
    int previous = queen_score;
    for (int clock : {1, 50, 99}) {
        const std::string position = "7k/8/8/8/8/8/3Q4/K7 w - - " + std::to_string(clock) + " 1";
        auto probe = std::make_unique<search_t>(position.c_str());
        const int value = evaluate(probe.get());
        success &= check(value < previous && value >= 0 && probe->neural_static_score() == raw,
            "A higher clock must damp the score without changing the raw score");
        previous = value;
    }
    auto drawn = std::make_unique<search_t>("7k/8/8/8/8/8/3Q4/K7 w - - 100 1");
    success &= check(evaluate(drawn.get()) == 0, "Clock 100 must damp the score to zero");
    // A repeated board at another clock reuses the undamped static score.
    // It must still apply the current clock once.
    {
        const char * cached_fen = "rnbqkbnr/pppppppp/8/8/8/8/PPPPPPPP/RNBQKBNR w KQkq - 8 1";
        auto cached = std::make_unique<search_t>(cached_fen);
        success &= check(evaluate(cached.get()) == scale_network_score(raw, cached->brd, 8),
            "Initial neural cache score must be scaled at its own clock");
        const U64 key = cached->brd.stack->tt_key;
        std::array<move_t, 4> cycle;
        const char * moves[] = {"g1f3", "g8f6", "f3g1", "f6g8"};
        for (size_t i = 0; i < cycle.size(); ++i) {
            cycle[i].set(&cached->brd, moves[i]);
            success &= check(cached->brd.valid(&cycle[i]) && cached->brd.legal(&cycle[i]),
                "Neural cache fixture must use legal reversible moves");
            cached->forward(&cycle[i], cached->brd.gives_check(&cycle[i]));
        }
        success &= check(cached->brd.stack->tt_key == key && cached->brd.stack->fifty_count == 12,
            "A reversible cycle must revisit the board across a rule50 bucket boundary");
        success &= check(evaluate(cached.get()) == scale_network_score(raw, cached->brd, 12)
            && cached->neural_static_score() == raw
            && !cached->evaluation_accumulators[cached->brd.ply].ready[0]
            && !cached->evaluation_accumulators[cached->brd.ply].ready[1],
            "Neural cache hits must skip inference and preserve the clock contract");
        for (size_t i = cycle.size(); i > 0; --i) cached->backward(&cycle[i - 1]);
        cached->init(cached_fen, nullptr, nullptr);
        // Isolate worker-local invalidation: shared TT evaluation can otherwise
        // legitimately leave this freshly initialized accumulator lazy.
        cached->tt_eval_enabled = false;
        success &= check(evaluate(cached.get()) == scale_network_score(raw, cached->brd, 8)
            && cached->evaluation_accumulators[0].ready[0]
            && cached->evaluation_accumulators[0].ready[1],
            "Search reinitialization must discard its previous neural cache");
    }
    success &= check(!nnue::set_path(path.string() + ".missing", error)
        && !error.empty() && nnue::snapshot() == first, "Failed reload must retain model");
    success &= check(nnue::path() == path.string(), "Failed reload must retain path");
    for (size_t offset : {size_t(0), size_t(4), size_t(8), size_t(189), network_start}) {
        bytes[offset] ^= 1;
        write_file(path, bytes);
        success &= check(!nnue::set_path(path.string(), error) && nnue::snapshot() == first,
            "Every unsupported version/architecture hash must reject atomically");
        bytes[offset] ^= 1;
    }
    bytes.pop_back();
    write_file(path, bytes);
    success &= check(!nnue::set_path(path.string(), error), "Truncated network must reject");
    bytes.push_back(0);
    put_u32(bytes, output_bias_offset, std::numeric_limits<int32_t>::max());
    write_file(path, bytes);
    success &= check(nnue::set_path(path.string(), error), "Extreme valid-format bias must load safely");
    trans_table::clear(); // the engine clears cached evaluations on every model change
    auto extreme_storage = std::make_unique<search_t>(fen);
    auto & extreme = *extreme_storage;
    const int extreme_score = scale_network_score(score::TABLEBASE - 1, extreme.brd, 0);
    success &= check(evaluate(&extreme) == std::min(extreme_score, score::TABLEBASE - MAX_PLY - 1)
        && extreme.neural_static_score() == score::TABLEBASE - 1,
        "Corrected evaluation must stay below the decisive band without changing raw NNUE");
    extreme.correction_history_enabled = false;
    success &= check(evaluate(&extreme) == extreme_score,
        "Disabled correction must preserve the original NNUE limit");
    extreme.correction_history_enabled = true;
    trans_table::clear(); // as on every model change; the TT holds the other model's evaluations
    neural.stack->eval_result = score::INVALID;
    success &= check(evaluate(&neural) == start_score, "Search must own immutable model across reloads");
    auto checked_storage = std::make_unique<search_t>("7k/8/8/8/8/8/7r/K7 w - - 0 1");
    auto & checked = *checked_storage;
    checked.stack->in_check = true;
    success &= check(evaluate(&checked) == score::INVALID, "In-check evaluation contract must survive NNUE");
    auto losers_storage = std::make_unique<w17_search_t>(fen);
    auto & losers = *losers_storage;
    const int losers_score = w17_evaluate(&losers);
    success &= check(!losers.evaluation_network && losers.evaluation_accumulators.empty()
        && losers_score != score::TABLEBASE - 1, "Royal-losers must retain material-only evaluator");
    nnue::set_path("", error);
    trans_table::clear();
    neural.stack->eval_result = score::INVALID;
    success &= check(!nnue::snapshot() && evaluate(&neural) == start_score,
        "Unload must not invalidate retained snapshots");
    success &= check(nnue::set_path(path.string(), error), "A valid network must reload after internal unload");
    trans_table::clear();
    auto restored = std::make_unique<search_t>(fen);
    success &= check(evaluate(restored.get()) == std::min(extreme_score, score::TABLEBASE - MAX_PLY - 1)
        && restored->neural_static_score() == score::TABLEBASE - 1, "Reloaded model must retain its scores");
    return success ? 0 : 1;
}
