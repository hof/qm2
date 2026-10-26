#include "bbmoves.h"

namespace {
U64 ray_attacks(int square, U64 occupied, bool bishop) {
    U64 result = 0;
    for (int rank_step = -1; rank_step <= 1; ++rank_step) {
        for (int file_step = -1; file_step <= 1; ++file_step) {
            if (bishop ? !rank_step || !file_step : (!rank_step == !file_step)) {
                continue;
            }
            int rank = square / 8 + rank_step;
            int file = square % 8 + file_step;
            while (rank >= 0 && rank < 8 && file >= 0 && file < 8) {
                const U64 target = U64(1) << (rank * 8 + file);
                result |= target;
                if (occupied & target) {
                    break;
                }
                rank += rank_step;
                file += file_step;
            }
        }
    }
    return result;
}
}

int main() {
    magic::init();
    for (bool bishop : {false, true}) {
        for (int square = 0; square < 64; ++square) {
            const U64 mask = bishop ? magic::_MAGIC_MOVES_BISHOP_MASK[square]
                : magic::_MAGIC_MOVES_ROOK_MASK[square];
            U64 subset = 0;
            do {
                for (U64 occupied : {subset, subset | ~mask}) {
                    const U64 actual = bishop ? magic::bishop_moves(square, occupied)
                        : magic::rook_moves(square, occupied);
                    if (actual != ray_attacks(square, occupied, bishop)) {
                        std::cerr << "sliding attack mismatch at square " << square << '\n';
                        return EXIT_FAILURE;
                    }
                }
                subset = (subset - mask) & mask;
            } while (subset);
        }
    }
    return EXIT_SUCCESS;
}
