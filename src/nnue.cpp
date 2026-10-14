// GPL-3.0-or-later. Legacy HalfKP format and integer arithmetic described by
// Stockfish/CFish and the pinned nnue-probe reference; see its README for credit.
#include "nnue.h"
#include "score.h"
#include <algorithm>
#include <bit>
#include <fstream>
#include <mutex>

#ifdef __AVX2__
#include <immintrin.h>
#endif

namespace nnue {
namespace {
constexpr size_t network_size = 21022697;
constexpr size_t transformer_start = 189;
constexpr size_t network_start = transformer_start + 4 + 512 + 2 * 256 * 41024;
std::mutex configuration_mutex;
std::shared_ptr<const network_t> configured_network;
std::string configured_path;

uint32_t read_u32(const std::vector<uint8_t> & bytes, size_t offset) {
    return uint32_t(bytes[offset]) | (uint32_t(bytes[offset + 1]) << 8)
        | (uint32_t(bytes[offset + 2]) << 16) | (uint32_t(bytes[offset + 3]) << 24);
}

template<size_t input_size>
std::array<int16_t, 32> affine(const std::array<int16_t, input_size> & input,
        const std::array<int8_t, input_size * 32> & weights,
        const std::array<int32_t, 32> & biases) {
    std::array<int16_t, 32> output;
    for (size_t row = 0; row < 32; ++row) {
        // Inputs are [0,127], weights [-128,127], at most 512 products:
        // the dot product fits int32 even for extreme external weights.
        int32_t dot = 0;
#ifdef __AVX2__
        auto sums = _mm256_setzero_si256();
        for (size_t column = 0; column < input_size; column += 16) {
            const auto values = _mm256_loadu_si256(reinterpret_cast<const __m256i *>(input.data() + column));
            const auto packed_weights = _mm_loadu_si128(
                reinterpret_cast<const __m128i *>(weights.data() + row * input_size + column));
            const auto wide_weights = _mm256_cvtepi8_epi16(packed_weights);
            sums = _mm256_add_epi32(sums, _mm256_madd_epi16(values, wide_weights));
        }
        auto total = _mm_add_epi32(_mm256_castsi256_si128(sums), _mm256_extracti128_si256(sums, 1));
        total = _mm_hadd_epi32(total, total);
        total = _mm_hadd_epi32(total, total);
        dot = _mm_cvtsi128_si32(total);
#else
        for (size_t column = 0; column < input_size; ++column)
            dot += int(input[column]) * int(weights[row * input_size + column]);
#endif
        // Biases are unrestricted int32 in the file. Add them in wide arithmetic.
        const int64_t sum = int64_t(biases[row]) + dot;
        output[row] = static_cast<int16_t>(std::clamp(sum >> 6, int64_t(0), int64_t(127)));
    }
    return output;
}
}

std::shared_ptr<const network_t> network_t::load(const std::string & path, std::string & error) {
    error.clear();
    std::ifstream stream(path, std::ios::binary | std::ios::ate);
    if (!stream || stream.tellg() != static_cast<std::streamoff>(network_size)) {
        error = "expected a 21022697-byte legacy HalfKP network";
        return nullptr;
    }
    std::vector<uint8_t> bytes(network_size);
    stream.seekg(0);
    if (!stream.read(reinterpret_cast<char *>(bytes.data()), bytes.size())
            || stream.peek() != std::char_traits<char>::eof()
            || read_u32(bytes, 0) != 0x7af32f16 || read_u32(bytes, 4) != 0x3e5aa6ee
            || read_u32(bytes, 8) != 177 || read_u32(bytes, transformer_start) != 0x5d69d7b8
            || read_u32(bytes, network_start) != 0x63337156) {
        error = "invalid or unsupported HalfKP network format";
        return nullptr;
    }
    auto network = std::make_shared<network_t>();
    U64 fingerprint = 14695981039346656037ULL;
    for (uint8_t byte : bytes) fingerprint = (fingerprint ^ byte) * 1099511628211ULL;
    network->_fingerprint = fingerprint;
    size_t offset = transformer_start + 4;
    auto read_i16 = [&]() {
        const uint16_t value = uint16_t(bytes[offset]) | (uint16_t(bytes[offset + 1]) << 8);
        offset += 2;
        return std::bit_cast<int16_t>(value);
    };
    auto read_i32 = [&]() {
        const uint32_t value = read_u32(bytes, offset);
        offset += 4;
        return std::bit_cast<int32_t>(value);
    };
    auto read_weights = [&](auto & weights) {
        for (auto & weight : weights) weight = std::bit_cast<int8_t>(bytes[offset++]);
    };
    for (auto & bias : network->_biases) bias = read_i16();
    network->_features.resize(41024 * 256);
    for (auto & weight : network->_features) weight = read_i16();
    offset += 4; // Already checked the affine architecture hash.
    for (auto & bias : network->_hidden1_biases) bias = read_i32();
    read_weights(network->_hidden1_weights);
    for (auto & bias : network->_hidden2_biases) bias = read_i32();
    read_weights(network->_hidden2_weights);
    network->_output_bias = read_i32();
    read_weights(network->_output_weights);
    assert(offset == bytes.size());
    return network;
}

void network_t::add_feature(std::array<int16_t, 256> & values, bool color,
        int king, int piece, int square, bool subtract) const {
    const int orientation = color == WHITE ? 0 : 63;
    const int type = (piece - 1) % 6;
    assert(type < 5 && square >= 0 && square < 64 && king >= 0 && king < 64);
    const int relative_color = (piece <= WKING) == color ? 0 : 1;
    const size_t index = (square ^ orientation) + 1 + 64 * (2 * type + relative_color)
        + 641 * (king ^ orientation);
    const int16_t * weights = _features.data() + index * 256;
#ifdef __AVX2__
    for (size_t i = 0; i < 256; i += 16) {
        const auto value = _mm256_loadu_si256(reinterpret_cast<const __m256i *>(values.data() + i));
        const auto weight = _mm256_loadu_si256(reinterpret_cast<const __m256i *>(weights + i));
        const auto result = subtract ? _mm256_sub_epi16(value, weight) : _mm256_add_epi16(value, weight);
        _mm256_storeu_si256(reinterpret_cast<__m256i *>(values.data() + i), result);
    }
#else
    for (size_t i = 0; i < 256; ++i) {
        const uint16_t result = subtract ? uint16_t(values[i]) - uint16_t(weights[i])
            : uint16_t(values[i]) + uint16_t(weights[i]);
        values[i] = std::bit_cast<int16_t>(result);
    }
#endif
}

void network_t::refresh(const board_t & board, accumulator_t & accumulator, bool color) const {
    accumulator.values[color] = _biases;
    accumulator.kings[color] = board.get_sq(KING[color]);
    for (int square = 0; square < 64; ++square) {
        const int piece = board.matrix[square];
        if (piece && piece != WKING && piece != BKING)
            add_feature(accumulator.values[color], color, accumulator.kings[color], piece, square, false);
    }
    accumulator.ready[color] = true;
}

void network_t::prepare(const board_t & board, const move_t * move, accumulator_t & accumulator) const {
    accumulator.ready = {false, false};
    accumulator.kings = {board.get_sq(BKING), board.get_sq(WKING)};
    accumulator.change_count = 0;
    if (!move) return; // Null move: unchanged features, exchanged perspectives.
    auto add = [&](int piece, int from, int to) {
        if (piece && piece != WKING && piece != BKING)
            accumulator.changes[accumulator.change_count++] = {piece, from, to};
    };
    if (move->promotion) {
        add(move->piece, move->ssq, -1);
        add(move->promotion, -1, move->tsq);
    } else {
        add(move->piece, move->ssq, move->tsq);
    }
    if (move->capture) {
        const int square = move->en_passant ? move->tsq + (move->piece == WPAWN ? -8 : 8) : move->tsq;
        add(move->capture, square, -1);
    }
    if (move->castle) {
        const int rank = move->piece == WKING ? 0 : 56;
        const bool kingside = FILE(move->tsq) == 6;
        add(move->piece == WKING ? WROOK : BROOK, rank + (kingside ? 7 : 0), rank + (kingside ? 5 : 3));
    }
}

int network_t::evaluate(const board_t & board, accumulator_t & accumulator,
        const accumulator_t * previous
#ifdef MAXIMA_NNUE_SEARCH_AUDIT
        , audit_counters_t * counters
#endif
        ) const {
    for (bool color : {false, true}) {
        if (accumulator.ready[color]) {
#ifdef MAXIMA_NNUE_SEARCH_AUDIT
            if (counters) ++counters->reuses;
#endif
            continue;
        }
        if (!previous || !previous->ready[color] || previous->kings[color] != accumulator.kings[color]) {
#ifdef MAXIMA_NNUE_SEARCH_AUDIT
            if (counters) {
                if (!board.ply) ++counters->root_refreshes;
                else if (previous && previous->kings[color] != accumulator.kings[color]) ++counters->king_refreshes;
                else ++counters->parent_refreshes;
            }
#endif
            refresh(board, accumulator, color);
            continue;
        }
#ifdef MAXIMA_NNUE_SEARCH_AUDIT
        if (counters) ++counters->updates;
#endif
        accumulator.values[color] = previous->values[color];
        for (int i = 0; i < accumulator.change_count; ++i) {
            const auto & change = accumulator.changes[i];
            if (change.from >= 0)
                add_feature(accumulator.values[color], color, accumulator.kings[color], change.piece, change.from, true);
            if (change.to >= 0)
                add_feature(accumulator.values[color], color, accumulator.kings[color], change.piece, change.to, false);
        }
        accumulator.ready[color] = true;
    }
    std::array<int16_t, 512> input;
    for (size_t i = 0; i < 512; ++i)
        input[i] = std::clamp<int>(accumulator.values[i < 256 ? board.us() : !board.us()][i % 256], 0, 127);
    const auto hidden1 = affine(input, _hidden1_weights, _hidden1_biases);
    const auto hidden2 = affine(hidden1, _hidden2_weights, _hidden2_biases);
    int64_t sum = _output_bias;
    for (size_t i = 0; i < 32; ++i) sum += int(hidden2[i]) * int(_output_weights[i]);
    return static_cast<int>(sum / 16);
}

int to_centipawns(int raw_score) {
    // Pinned sf_13 UCI::value(): 100 centipawns = PawnValueEg (208 units).
    // This is a unit conversion, not Stockfish's material/tempo/rule50 scaling.
    const int64_t centipawns = int64_t(raw_score) * 100 / 208;
    return static_cast<int>(std::clamp(centipawns, int64_t(-score::TABLEBASE + 1), int64_t(score::TABLEBASE - 1)));
}

bool set_path(const std::string & path, std::string & error) {
    auto candidate = path.empty() ? nullptr : network_t::load(path, error);
    if (!path.empty() && !candidate) return false;
    std::lock_guard lock(configuration_mutex);
    configured_network = std::move(candidate);
    configured_path = path;
    error.clear();
    return true;
}

std::shared_ptr<const network_t> snapshot() {
    std::lock_guard lock(configuration_mutex);
    return configured_network;
}

std::string path() {
    std::lock_guard lock(configuration_mutex);
    return configured_path;
}
}
