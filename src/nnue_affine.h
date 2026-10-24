// GPL-3.0-or-later. Internal HalfKP dense layer; activations must be in [0,127].
#ifndef MAXIMA_NNUE_AFFINE_H
#define MAXIMA_NNUE_AFFINE_H
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
