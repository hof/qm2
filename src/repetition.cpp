#include "search.h"
#include <array>
#include <stdexcept>
#include <utility>

namespace {

struct repetition_entry_t {
    U64 key = 0;
    uint8_t piece = 0, from = 0, to = 0;
};

// Piece keys have a zero low bit, while side-to-move has a one. Discard that
// invariant bit rather than limiting the first hash to half the table.
int first_slot(U64 key) {
    return (key >> 1) & 8191;
}

int second_slot(U64 key) {
    return (key >> 17) & 8191;
}

struct repetition_moves_t {
    std::array<repetition_entry_t, 8192> entries{};

    repetition_moves_t() {
        int count = 0;
        for (int piece : {WKNIGHT, WBISHOP, WROOK, WQUEEN, WKING,
                BKNIGHT, BBISHOP, BROOK, BQUEEN, BKING}) {
            for (int from = 0; from < 64; ++from) {
                const U64 attacks = piece == WKNIGHT || piece == BKNIGHT ? KNIGHT_MOVES[from]
                    : piece == WBISHOP || piece == BBISHOP ? BISHOP_MOVES[from]
                    : piece == WROOK || piece == BROOK ? ROOK_MOVES[from]
                    : piece == WQUEEN || piece == BQUEEN ? QUEEN_MOVES[from] : KING_MOVES[from];
                for (int to = from + 1; to < 64; ++to) {
                    if (!(attacks & BIT(to))) {
                        continue;
                    }
                    U64 key = hash::codes[piece][from] ^ hash::codes[piece][to];
                    HASH_STM(key);
                    repetition_entry_t entry{key, uint8_t(piece), uint8_t(from), uint8_t(to)};
                    int slot = first_slot(key);
                    int displaced = 0;
                    do {
                        std::swap(entry, entries[slot]);
                        if (!entry.piece) {
                            break;
                        }
                        slot = slot == first_slot(entry.key) ? second_slot(entry.key) : first_slot(entry.key);
                        if (++displaced >= int(entries.size())) {
                            throw std::logic_error("repetition move table insertion failed");
                        }
                    } while (true);
                    ++count;
                }
            }
        }
        if (count != 3668) {
            throw std::logic_error("incomplete repetition move table");
        }
    }
};

}

bool repetition::find_move(board_t & board, U64 delta, move_t & move) {
    // Initialization is synchronized by C++; published tables are immutable.
    static const repetition_moves_t moves;
    const auto & first = moves.entries[first_slot(delta)];
    const auto & entry = first.key == delta ? first : moves.entries[second_slot(delta)];
    if (!entry.piece || entry.key != delta) {
        return false;
    }
    const bool forward = board.matrix[entry.from] == entry.piece;
    move.set(entry.piece, forward ? entry.from : entry.to, forward ? entry.to : entry.from);
    return board.valid(&move) && board.legal(&move);
}
