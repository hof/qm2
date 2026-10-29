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
    struct alignas(64) feature_column_t {
        std::array<int16_t, 256> values;
    };
    static_assert(sizeof(feature_column_t) == 256 * sizeof(int16_t));

    std::array<int16_t, 256> _biases{};
    std::vector<feature_column_t> _features;
    std::array<int32_t, 32> _hidden1_biases{}, _hidden2_biases{};
    std::array<int8_t, 512 * 32> _hidden1_weights{};
    std::array<int8_t, 32 * 32> _hidden2_weights{};
    std::array<int8_t, 32> _output_weights{};
    int32_t _output_bias = 0;
    U64 _fingerprint = 0;
    bool _hidden1_pair_sums_safe = false, _hidden2_pair_sums_safe = false;

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
#include <cstring>
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

// Store four adjacent inputs for each output, then advance to the next input
// block. One broadcast supplies eight output lanes without horizontal reductions.
template<size_t input_size>
std::array<int8_t, input_size * 32> pack_affine_weights(
        const std::array<int8_t, input_size * 32> & rows) {
    std::array<int8_t, input_size * 32> packed;
    for (size_t column = 0; column < input_size; column += 4) {
        for (size_t row = 0; row < 32; ++row) {
            for (size_t i = 0; i < 4; ++i) {
                packed[column * 32 + row * 4 + i] = rows[row * input_size + column + i];
            }
        }
    }
    return packed;
}

// Four SIMD product pairs may share a widening instruction only if every
// partial sum fits int16 for every possible activation in [0,127].
template<size_t input_size>
bool affine_pair_sums_safe(const std::array<int8_t, input_size * 32> & weights) {
    for (size_t column = 0; column < input_size; column += 16) {
        for (size_t row = 0; row < 32; ++row) {
            for (size_t pair = 0; pair < 4; pair += 2) {
                int positive = 0, negative = 0;
                for (size_t block = 0; block < 4; ++block) {
                    for (size_t i = 0; i < 2; ++i) {
                        const int w = weights[(column + block * 4) * 32 + row * 4 + pair + i];
                        positive += std::max(w, 0);
                        negative += std::min(w, 0);
                    }
                }
                if (positive * 127 > INT16_MAX || negative * 127 < INT16_MIN) {
                    return false;
                }
            }
        }
    }
    return true;
}

template<size_t input_size, bool use_simd = true>
std::array<uint8_t, 32> affine(const std::array<uint8_t, input_size> & input,
        const std::array<int8_t, input_size * 32> & weights,
        const std::array<int32_t, 32> & biases, [[maybe_unused]] bool pair_sums_safe = false) {
    static_assert(input_size % 32 == 0 && input_size <= 512);

    // Inputs are [0,127], weights [-128,127], at most 512 products:
    // the dot product fits int32 even for extreme external weights.
    std::array<int32_t, 32> dots{};
#ifdef __AVX2__
    if constexpr (use_simd) {
        auto sum0 = _mm256_setzero_si256();
        auto sum1 = _mm256_setzero_si256();
        auto sum2 = _mm256_setzero_si256();
        auto sum3 = _mm256_setzero_si256();
        const auto ones = _mm256_set1_epi16(1);
        if (pair_sums_safe) {
            for (size_t column = 0; column < input_size; column += 16) {
                auto broadcast = [&](size_t offset) {
                    int32_t block;
                    std::memcpy(&block, input.data() + column + offset, sizeof(block));
                    return _mm256_set1_epi32(block);
                };
                const auto in0 = broadcast(0), in1 = broadcast(4);
                const auto in2 = broadcast(8), in3 = broadcast(12);
                const auto * packed = weights.data() + column * 32;
                auto products = [&](size_t offset) {
                    auto pair = [&](const auto values, size_t block) {
                        const auto w = _mm256_loadu_si256(
                            reinterpret_cast<const __m256i *>(packed + offset + block * 128));
                        return _mm256_maddubs_epi16(values, w);
                    };

                    // Loading proved the sum of all positive or all negative
                    // terms fits int16, so every intermediate also fits.
                    const auto low = _mm256_add_epi16(pair(in0, 0), pair(in1, 1));
                    const auto high = _mm256_add_epi16(pair(in2, 2), pair(in3, 3));
                    return _mm256_madd_epi16(_mm256_add_epi16(low, high), ones);
                };
                sum0 = _mm256_add_epi32(sum0, products(0));
                sum1 = _mm256_add_epi32(sum1, products(32));
                sum2 = _mm256_add_epi32(sum2, products(64));
                sum3 = _mm256_add_epi32(sum3, products(96));
            }
        } else {
            for (size_t column = 0; column < input_size; column += 4) {
                int32_t block;
                std::memcpy(&block, input.data() + column, sizeof(block));
                if (block == 0) {
                    continue;
                }
                const auto values = _mm256_set1_epi32(block);
                const auto * packed = weights.data() + column * 32;
                auto products = [&](size_t offset) {
                    const auto w = _mm256_loadu_si256(reinterpret_cast<const __m256i *>(packed + offset));

                    // A pair is in [-32512,32258], so maddubs cannot saturate.
                    // Widen each pair before adding more products: unrestricted
                    // networks can overflow int16 when several pairs are summed.
                    return _mm256_madd_epi16(_mm256_maddubs_epi16(values, w), ones);
                };
                sum0 = _mm256_add_epi32(sum0, products(0));
                sum1 = _mm256_add_epi32(sum1, products(32));
                sum2 = _mm256_add_epi32(sum2, products(64));
                sum3 = _mm256_add_epi32(sum3, products(96));
            }
        }
        _mm256_storeu_si256(reinterpret_cast<__m256i *>(dots.data()), sum0);
        _mm256_storeu_si256(reinterpret_cast<__m256i *>(dots.data() + 8), sum1);
        _mm256_storeu_si256(reinterpret_cast<__m256i *>(dots.data() + 16), sum2);
        _mm256_storeu_si256(reinterpret_cast<__m256i *>(dots.data() + 24), sum3);
    } else
#endif
    {
        for (size_t row = 0; row < 32; ++row) {
            for (size_t column = 0; column < input_size; ++column) {
                dots[row] += int(input[column]) * int(weights[(column / 4 * 32 + row) * 4 + column % 4]);
            }
        }
    }
    std::array<uint8_t, 32> output;
    constexpr int dot_bound = int(input_size) * 128 * 127;
    for (size_t row = 0; row < 32; ++row) {

        // Beyond these bias bounds every possible dot already clips to 0 or 127.
        // Clamping first preserves that result and bounds the int32 addition.
        const int bias = std::clamp(biases[row], -dot_bound, dot_bound + 127 * 64);
        const int sum = bias + dots[row];
        output[row] = static_cast<uint8_t>(std::clamp(sum >> 6, 0, 127));
    }
    return output;
}
}

#endif
