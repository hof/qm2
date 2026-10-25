#ifndef MAXIMA_NNUE_H
#define MAXIMA_NNUE_H

#include "board.h"
#include <array>
#include <memory>
#include <span>
#include <string>
#include <vector>

// Legacy HalfKP integer inference, GPL-3.0-or-later, like the rest of Maxima.
// Format/arithmetic reference: third_party/nnue_reference/README.md.
namespace nnue {

struct change_t {
    int piece;
    int from;
    int to;
};

struct alignas(64) accumulator_t {
    std::array<std::array<int16_t, 256>, 2> values{};
    std::array<int, 2> kings{}; // Maxima color order: Black, White.
    std::array<bool, 2> ready{};
    std::array<change_t, 3> changes{};
    int change_count = 0;
};

class network_t {
public:
    static std::shared_ptr<const network_t> load(const std::string & path, std::string & error);
    // Raw Stockfish 13 internal units. Convert before use by Maxima's search.
    int evaluate(const board_t & board, accumulator_t & accumulator,
        const accumulator_t * previous = nullptr
        ) const;
    // Prepared ancestors are ordered oldest first and exclude the current state.
    // Unready intermediate states supply changes only; their values are never read.
    int evaluate(const board_t & board, accumulator_t & accumulator,
        std::span<const accumulator_t> ancestors
        ) const;
    void prepare(const board_t & board, const move_t * move, accumulator_t & accumulator) const;
    U64 fingerprint() const { return _fingerprint; }

private:
    std::array<int16_t, 256> _biases{};
    std::vector<int16_t> _features;
    std::array<int32_t, 32> _hidden1_biases{}, _hidden2_biases{};
    std::array<int8_t, 512 * 32> _hidden1_weights{};
    std::array<int8_t, 32 * 32> _hidden2_weights{};
    std::array<int8_t, 32> _output_weights{};
    int32_t _output_bias = 0;
    U64 _fingerprint = 0;

    void refresh(const board_t & board, accumulator_t & accumulator, bool color) const;
    void add_feature(std::array<int16_t, 256> & values, bool color,
        int king, int piece, int square, bool subtract) const;
};

int to_centipawns(int raw_score);

// Configuration changes occur after stopping/joining the engine worker.
// Snapshots also retain their weights across later reloads or unloads.
bool set_path(const std::string & path, std::string & error);
// Standard chess requires a network. Discover eval.nnue beside the book/engine.
bool load_default(std::string & error);
std::shared_ptr<const network_t> snapshot();
std::string path();

}

/*
 * Dense layers (internal); activations must be in [0,127]
 */

#include <algorithm>
#include <array>
#include <cstdint>
#include <cstddef>
#ifdef __AVX2__
#include <immintrin.h>
#endif

namespace nnue::detail {
template<bool use_simd = true>
std::array<uint8_t, 512> clip_activations(
        const std::array<std::array<int16_t, 256>, 2> & values, bool us) {
    std::array<uint8_t, 512> output;
    for (size_t perspective = 0; perspective < 2; ++perspective) {
        const auto & input = values[perspective == 0 ? us : !us];
        auto * destination = output.data() + perspective * 256;
#ifdef __AVX2__
        if constexpr (use_simd) {
            const auto zero = _mm256_setzero_si256();
            const auto limit = _mm256_set1_epi16(127);
            for (size_t i = 0; i < 256; i += 32) {
                auto low = _mm256_loadu_si256(reinterpret_cast<const __m256i *>(input.data() + i));
                auto high = _mm256_loadu_si256(reinterpret_cast<const __m256i *>(input.data() + i + 16));
                low = _mm256_min_epi16(_mm256_max_epi16(low, zero), limit);
                high = _mm256_min_epi16(_mm256_max_epi16(high, zero), limit);
                // Packing is local to 128-bit halves; restore the original lane order.
                const auto packed = _mm256_permute4x64_epi64(_mm256_packus_epi16(low, high), 0xd8);
                _mm256_storeu_si256(reinterpret_cast<__m256i *>(destination + i), packed);
            }
        } else
#endif
        {
            for (size_t i = 0; i < 256; ++i)
                destination[i] = static_cast<uint8_t>(std::clamp<int>(input[i], 0, 127));
        }
    }
    return output;
}

template<size_t input_size, bool use_simd = true>
std::array<uint8_t, 32> affine(const std::array<uint8_t, input_size> & input,
        const std::array<int8_t, input_size * 32> & weights,
        const std::array<int32_t, 32> & biases) {
    static_assert(input_size % 32 == 0 && input_size <= 512);
    // Inputs are [0,127], weights [-128,127], at most 512 products:
    // the dot product fits int32 even for extreme external weights.
    std::array<int32_t, 32> dots{};
#ifdef __AVX2__
    if constexpr (use_simd) {
        // Eight partial int32 sums of one row's dot product.
        auto row_sums = [&](size_t row) {
            auto sums = _mm256_setzero_si256();
            const auto ones = _mm256_set1_epi16(1);
            for (size_t column = 0; column < input_size; column += 32) {
                const auto values = _mm256_loadu_si256(reinterpret_cast<const __m256i *>(input.data() + column));
                const auto packed_weights = _mm256_loadu_si256(
                    reinterpret_cast<const __m256i *>(weights.data() + row * input_size + column));
                // Each pair is in [-32512,32258]: maddubs cannot saturate.
                // Widen immediately; summing two pairs in int16 could overflow.
                const auto pairs = _mm256_maddubs_epi16(values, packed_weights);
                sums = _mm256_add_epi32(sums, _mm256_madd_epi16(pairs, ones));
            }
            return sums;
        };
        // Four rows share one horizontal reduction instead of one each.
        for (size_t row = 0; row < 32; row += 4) {
            const auto sums01 = _mm256_hadd_epi32(row_sums(row), row_sums(row + 1));
            const auto sums23 = _mm256_hadd_epi32(row_sums(row + 2), row_sums(row + 3));
            const auto sums0123 = _mm256_hadd_epi32(sums01, sums23);
            const auto four = _mm_add_epi32(_mm256_castsi256_si128(sums0123), _mm256_extracti128_si256(sums0123, 1));
            _mm_storeu_si128(reinterpret_cast<__m128i *>(dots.data() + row), four);
        }
    } else
#endif
    {
        for (size_t row = 0; row < 32; ++row) {
            for (size_t column = 0; column < input_size; ++column) {
                dots[row] += int(input[column]) * int(weights[row * input_size + column]);
            }
        }
    }
    std::array<uint8_t, 32> output;
    for (size_t row = 0; row < 32; ++row) {
        // Biases are unrestricted int32 in the file. Add them in wide arithmetic.
        const int64_t sum = int64_t(biases[row]) + dots[row];
        output[row] = static_cast<uint8_t>(std::clamp(sum >> 6, int64_t(0), int64_t(127)));
    }
    return output;
}
}

#endif
