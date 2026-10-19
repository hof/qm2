/**
 * Maxima, a chess playing program. 
 * Copyright (C) 1996-2015 Erik van het Hof and Hermen Reitsma 
 * 
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License
 * as published by the Free Software Foundation; either version 3
 * of the License, or (at your option) any later version.
 *  
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *  
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, If not, see <http://www.gnu.org/licenses/>.
 * 
 * 
 * File: board.cpp
 * Board representation:
 * - Bitboard for each piece and all (white/black) occupied squares
 * - Matrix[64]
 */

#include "board.h"
#include "hashcodes.h"
#include <type_traits>

/**
 * Flips board stack (e.g. the castling rights and side to move)
 */
void board_stack_t::do_flip() {
    wtm = !wtm;
    HASH_STM(tt_key);
    if (enpassant_sq) {
        HASH_EP(tt_key, enpassant_sq);
        enpassant_sq = FLIP_SQUARE(enpassant_sq);
        HASH_EP(tt_key, enpassant_sq);
    }
    uint8_t flags = castling_flags;
    castling_flags = 0;
    if (flags & CASTLE_K) {
        castling_flags |= CASTLE_k;
    }
    if (flags & CASTLE_Q) {
        castling_flags |= CASTLE_q;
    }
    if (flags & CASTLE_k) {
        castling_flags |= CASTLE_K;
    }
    if (flags & CASTLE_q) {
        castling_flags |= CASTLE_Q;
    }
}

/**
 * Empty and initialize the board structure
 */
void board_t::clear() {
    _stack[0].clear();
    stack = &_stack[0];
    ply = 0;
    root_ply = 0;
    memset(bb, 0, sizeof (bb));
    memset(matrix, 0, sizeof (matrix));
}

/**
 * Add a piece to the board (e.g. a promotion)
 * @param piece the piece type to be added
 * @param sq the location square of the piece (a1..h8)
 * @param hash update the hash codes or not
 */
void board_t::add_piece(const int piece, const int sq, const bool hash = false) {
    if (hash) {
        HASH_ADD_PIECE(stack->tt_key, piece, sq);
    }
    U64 bit = BIT(sq);
    bb[piece] ^= bit;
    bb[WPIECES + (piece > WKING)] ^= bit;
    bb[ALLPIECES] ^= bit;
    matrix[sq] = piece;
}

/**
 * Remove a piece from the board (e.g. a capture)
 * @param piece the piece type to be removed
 * @param sq the square location of the piece (a1..h8)
 * @param hash update the hash codes or not
 */
void board_t::remove_piece(const int piece, const int sq, const bool hash = false) {
    U64 bit = BIT(sq);
    bb[piece] ^= bit;
    bb[WPIECES + (piece > WKING)] ^= bit;
    bb[ALLPIECES] ^= bit;
    matrix[sq] = EMPTY;
    if (hash) {
        HASH_REMOVE_PIECE(stack->tt_key, piece, sq);
    }
}

/**
 * Move a piece on the board
 * @param piece the moving piece type
 * @param ssq source square, where the piece if moving from
 * @param tsq target square, where the piece is moving to
 * @param hash update the hash codes or not
 */
void board_t::move_piece(const int piece, const int ssq, int tsq, const bool hash = false) {
    U64 update_mask = BIT(ssq) | BIT(tsq);
    bb[piece] ^= update_mask;
    bb[WPIECES + (piece > WKING)] ^= update_mask;
    bb[ALLPIECES] ^= update_mask;
    matrix[ssq] = EMPTY;
    matrix[tsq] = piece;
    if (hash) {
        HASH_MOVE_PIECE(stack->tt_key, piece, ssq, tsq);
    }
}

/**
 * Do the move in the current position and update the board structure 
 * @param move move object, the move to make
 */
void board_t::forward(const move_t * move) {
    int ssq = move->ssq;
    int tsq = move->tsq;
    int piece = move->piece;
    int promotion = move->promotion;
    int capture = move->capture;

    assert(ssq != tsq);
    assert(ssq >= a1 && tsq <= h8);
    assert(tsq >= a1 && tsq <= h8);
    assert(piece >= WPAWN && piece <= BKING);
    assert(piece == matrix[ssq]);
    assert(ply < MAX_PLY);

    (stack + 1)->copy(stack);
    stack++;
    ply++;

    HASH_EP(stack->tt_key, stack->enpassant_sq); //remove (a possible) ep square from hashcode

    if (capture || promotion) {
        if (capture) {
            assert(capture != WKING && capture != BKING);
            if (move->en_passant) {
                assert(move->piece == WPAWN || move->piece == BPAWN);
                assert(matrix[tsq] == EMPTY);
                assert(stack->enpassant_sq == tsq);
                assert(tsq >= a6 ? matrix[tsq - 8] == BPAWN : matrix[tsq + 8] == WPAWN);
                remove_piece(capture, tsq >= a6 ? tsq - 8 : tsq + 8, true);
            } else {
                assert(capture == matrix[tsq]);
                remove_piece(capture, tsq, true);
            }
        }
        if (promotion) {
            remove_piece(piece, ssq, true);
            add_piece(promotion, tsq, true);
        } else {
            move_piece(piece, ssq, tsq, true);
        }
        stack->enpassant_sq = EMPTY;
        stack->fifty_count = 0;
    } else { //not a capture or promotion
        assert(matrix[tsq] == EMPTY);
        move_piece(piece, ssq, tsq, true);
        stack->fifty_count++;
        stack->enpassant_sq = EMPTY;
        if (piece == WPAWN) {
            if (ssq <= h2 && tsq == ssq + 16) {
                stack->enpassant_sq = tsq - 8;
                HASH_EP(stack->tt_key, stack->enpassant_sq);
            }
            stack->fifty_count = 0;
        } else if (piece == BPAWN) {
            if (ssq <= h7 && tsq == ssq - 16) {
                stack->enpassant_sq = tsq + 8;
                HASH_EP(stack->tt_key, stack->enpassant_sq);
            }
            stack->fifty_count = 0;
        }
    }

    if (stack->castling_flags) {
        /*
         * For castling moves, move the rook as well 
         */
        if (move->castle) {
            switch (move->castle) {
                case CASTLE_K:
                    move_piece(WROOK, h1, f1, true);
                    break;
                case CASTLE_Q:
                    move_piece(WROOK, a1, d1, true);
                    break;
                case CASTLE_k:
                    move_piece(BROOK, h8, f8, true);
                    break;
                case CASTLE_q:
                    move_piece(BROOK, a8, d8, true);
                    break;
                default:
                    assert(false);
            }
        }
        /*
         * Removal of castling rights
         */
        if (has_castle_right(CASTLE_K) && (ssq == h1 || ssq == e1 || tsq == h1)) {
            stack->castling_flags ^= CASTLE_K;
            HASH_CASTLE_K(stack->tt_key);
        }
        if (has_castle_right(CASTLE_Q) && (ssq == a1 || ssq == e1 || tsq == a1)) {
            stack->castling_flags ^= CASTLE_Q;
            HASH_CASTLE_Q(stack->tt_key);
        }
        if (has_castle_right(CASTLE_k) && (ssq == h8 || ssq == e8 || tsq == h8)) {
            stack->castling_flags ^= CASTLE_k;
            HASH_CASTLE_k(stack->tt_key);
        }
        if (has_castle_right(CASTLE_q) && (ssq == a8 || ssq == e8 || tsq == a8)) {
            stack->castling_flags ^= CASTLE_q;
            HASH_CASTLE_q(stack->tt_key);
        }

    }

    // update flags and hashcode for the side to move
    stack->wtm = !stack->wtm;
    HASH_STM(stack->tt_key);

    stack->legal_enpassant = stack->enpassant_sq && has_legal_enpassant();
    assert(matrix[get_sq(WKING)] == WKING && matrix[get_sq(BKING)] == BKING);
    assert(piece == matrix[tsq] || (promotion && promotion == matrix[tsq]));
}

/**
 * Undo the move, updating the board structure
 * @param move the move to unmake
 */
void board_t::backward(const move_t * move) {
    int ssq = move->ssq;
    int tsq = move->tsq;
    int promotion = move->promotion;
    int capture = move->capture;
    int piece = matrix[tsq];

    if (capture || promotion) {
        if (promotion) {
            piece = move->piece;
            add_piece(piece, ssq);
            remove_piece(promotion, tsq);
        } else {
            move_piece(piece, tsq, ssq);
        }
        if (capture) {
            if (move->en_passant) {
                tsq = tsq >= a6 ? tsq - 8 : tsq + 8;
            }
            add_piece(capture, tsq);
        }
    } else { //not a capture and not a promotion
        move_piece(piece, tsq, ssq);
        /* castling: move the rook */
        if (move->castle) {
            switch (move->castle) {
                case CASTLE_K:
                    move_piece(WROOK, f1, h1);
                    break;
                case CASTLE_Q:
                    move_piece(WROOK, d1, a1);
                    break;
                case CASTLE_k:
                    move_piece(BROOK, f8, h8);
                    break;
                case CASTLE_q:
                    move_piece(BROOK, d8, a8);
                    break;
                default:
                    assert(false);

            }
        }
    }
    ply--;
    stack--;
}

/**
 * Do a nullmove: update board structure, e.g. switch side to move
 */
void board_t::forward() {
    (stack + 1)->copy(stack);
    stack++;
    ply++;
    HASH_EP(stack->tt_key, stack->enpassant_sq); //remove enpassant_square if it is set
    stack->enpassant_sq = EMPTY;
    stack->legal_enpassant = false;
    stack->wtm = !stack->wtm;
    HASH_STM(stack->tt_key);
}

/**
 * Undo a nullmove: just restore board structure
 */
void board_t::backward() {
    ply--;
    stack--;
}

/**
 * See if the move is valid in the current board (legality is not checked here)
 * "valid" means: the move would be generated by the move generator
 * @param move the move to test for validity, not yet performed in the position
 * @return true if the move is valid in the current position, false otherwise 
 */
bool board_t::valid(const move_t * move) {
    int piece = move->piece;
    if (stack->wtm != (piece <= WKING)) {
        return false;
    }
    int ssq = move->ssq;
    if (matrix[ssq] != piece) {
        return false;
    }
    int tsq = move->tsq;
    if (move->en_passant) {
        return tsq == stack->enpassant_sq && matrix[tsq] == EMPTY && ((stack->wtm && piece == WPAWN && matrix[tsq - 8] == BPAWN)
                || (!stack->wtm && piece == BPAWN && matrix[tsq + 8] == WPAWN));
    } else if (move->castle) {
        int castle = move->castle;
        if (!has_castle_right(castle)) {
            return false;
        }
        switch (castle) {
            case EMPTY:
                break;
            case CASTLE_K:
                return matrix[h1] == WROOK && matrix[f1] == EMPTY && matrix[g1] == EMPTY;
            case CASTLE_Q:
                return matrix[a1] == WROOK && matrix[b1] == EMPTY && matrix[c1] == EMPTY && matrix[d1] == EMPTY;
            case CASTLE_k:
                return matrix[h8] == BROOK && matrix[f8] == EMPTY && matrix[g8] == EMPTY;
            case CASTLE_q:
                return matrix[a8] == BROOK && matrix[b8] == EMPTY && matrix[c8] == EMPTY && matrix[d8] == EMPTY;
            default:
                assert(false);
        }
    }
    int captured = move->capture; //empty for non-captures
    if (matrix[tsq] != captured) {
        return false;
    }

    /*
     * At this point, the only possible reason to the move being invalid
     * is an obstruction
     */
    switch (piece) {
        case EMPTY:
            assert(false);
            return false;
        case WPAWN:
            return captured || tsq - ssq == 8 || matrix[ssq + 8] == EMPTY;
        case WKNIGHT:
            return true;
        case WBISHOP:
            return magic::bishop_moves(ssq, bb[ALLPIECES]) & BIT(tsq);
        case WROOK:
            return magic::rook_moves(ssq, bb[ALLPIECES]) & BIT(tsq);
        case WQUEEN:
            return magic::queen_moves(ssq, bb[ALLPIECES]) & BIT(tsq);
        case WKING:
            return true;
        case BPAWN:
            return captured || ssq - tsq == 8 || matrix[tsq + 8] == EMPTY;
        case BKNIGHT:
            return true;
        case BBISHOP:
            return magic::bishop_moves(ssq, bb[ALLPIECES]) & BIT(tsq);
        case BROOK:
            return magic::rook_moves(ssq, bb[ALLPIECES]) & BIT(tsq);
        case BQUEEN:
            return magic::queen_moves(ssq, bb[ALLPIECES]) & BIT(tsq);
        case BKING:
            return true;
        default:
            assert(false);
            return false;
    }
    return true;
}

/**
 * Verify if the move is legal in the current board, e.g. not leaving our king in check
 * @param move the move to test for legality, not yet performed in the position
 * @return true if the move is legal in the current position, false otherwise 
 * NOTE: this version does not consider if we are in check or not which leaves some
 * room for further optimization.
 */
bool board_t::legal(move_t * move) {
    int piece = move->piece;
    int tsq = move->tsq;
    assert(piece >= WPAWN && piece <= BKING && tsq >= a1 && tsq <= h8);
    U64 tsq_bit = BIT(tsq);
    U64 occ = bb[ALLPIECES] & ~tsq_bit;
    if (stack->wtm) {
        move->en_passant = stack->enpassant_sq && tsq == stack->enpassant_sq && piece == WPAWN;
        if (move->en_passant) {
            occ ^= tsq_bit >> 8;
        }

        //1. check for direct checks on our king (non-sliders or contact checks)
        int kpos_list[2] = {get_sq(WKING), tsq};
        int kpos = kpos_list[piece == WKING];

        if ((bb[BKNIGHT] & occ & KNIGHT_MOVES[kpos])
                || (bb[BPAWN] & occ & PAWN_CAPTURES[WHITE][kpos])
                || (piece == WKING && KING_MOVES[kpos] & bb[BKING])) {
            return false;
        }

        move->castle = 0;
        if (move->ssq == e1 && piece == WKING) {
            if (tsq == g1) {
                move->castle = CASTLE_K;
                if (is_attacked(e1, BLACK) || is_attacked(f1, BLACK) || is_attacked(g1, BLACK)) {
                    return false;
                }
            } else if (tsq == c1) {
                move->castle = CASTLE_Q;
                if (is_attacked(e1, BLACK) || is_attacked(d1, BLACK) || is_attacked(c1, BLACK)) {
                    return false;
                }
            }
        }


        //2. update the occupied bitboard and check for sliding attacks on the king
        //   which can happen when we move a piece which was pinned, or move the king
        //   on the same path as a sliding attack.
        //   note we deliberately dont update occupied for rooks for castling moves
        //   but en-passant update is required 
        U64 opp_sliders = (bb[BBISHOP] | bb[BROOK] | bb[BQUEEN]) & ~tsq_bit;
        if (QUEEN_MOVES[kpos] & opp_sliders) {
            U64 opp_diag_sliders = opp_sliders & ~bb[BROOK];
            U64 opp_hv_sliders = opp_sliders & ~bb[BBISHOP];
            occ ^= BIT(move->ssq);
            occ |= tsq_bit;
            return !(magic::bishop_moves(kpos, occ) & opp_diag_sliders)
                    && !(magic::rook_moves(kpos, occ) & opp_hv_sliders);
        }
    } else {
        move->en_passant = stack->enpassant_sq && tsq == stack->enpassant_sq && piece == BPAWN;
        if (move->en_passant) {
            occ ^= tsq_bit << 8;
        }

        //1. check for direct checks on our king (non-sliders or contact checks)
        int kpos_list[2] = {get_sq(BKING), tsq};
        int kpos = kpos_list[piece == BKING];

        if ((bb[WKNIGHT] & occ & KNIGHT_MOVES[kpos])
                || (bb[WPAWN] & occ & PAWN_CAPTURES[BLACK][kpos])
                || (piece == BKING && KING_MOVES[kpos] & bb[WKING])) {
            return false;
        }

        move->castle = 0;
        if (move->ssq == e8 && piece == BKING) {
            if (tsq == g8) {
                move->castle = CASTLE_k;
                if (is_attacked(e8, WHITE) || is_attacked(f8, WHITE) || is_attacked(g8, WHITE)) {
                    return false;
                }
            } else if (tsq == c8) {
                move->castle = CASTLE_q;
                if (is_attacked(e8, WHITE) || is_attacked(d8, WHITE) || is_attacked(c8, WHITE)) {
                    return false;
                }
            }
        }

        //2. update the occupied bitboard and check for sliding attacks on the king
        //   which can happen when we move a piece which was pinned, or move the king
        //   on the same path as a sliding attack.
        //   note we deliberately dont update occupied for rooks for castling moves
        //   but en-passant update is required 


        U64 opp_sliders = (bb[WBISHOP] | bb[WROOK] | bb[WQUEEN]) & ~tsq_bit;
        if (QUEEN_MOVES[kpos] & opp_sliders) {
            U64 opp_diag_sliders = opp_sliders & ~bb[WROOK];
            U64 opp_hv_sliders = opp_sliders & ~bb[WBISHOP];
            occ ^= BIT(move->ssq);
            occ |= tsq_bit;
            return !(magic::bishop_moves(kpos, occ) & opp_diag_sliders)
                    && !(magic::rook_moves(kpos, occ) & opp_hv_sliders);
        }
    }
    return true;
}

/**
 * Verify if a move checks the opponent's king
 * @param move the move to verify
 * @return 0: no check, 1: simple, direct check, 2: exposed check 
 */
int board_t::gives_check(const move_t * move) {
    int piece = move->piece;
    const int ssq = move->ssq;
    const int tsq = move->tsq;
    const U64 ssq_bit = BIT(ssq);
    const U64 tsq_bit = BIT(tsq);
    const int kpos = get_sq(KING[piece > WKING]);
    const U64 check_mask = CHECK_MASK[kpos]; 
    if ((check_mask & (ssq_bit | tsq_bit)) == 0 && !move->castle && !move->en_passant) {
        return 0;
    }
    
    //is it a direct check?
    U64 checkers = 0;
    if ((check_mask & tsq_bit) || move->castle) {
        switch (piece) {
            case EMPTY:
                assert(false);
            case WPAWN:
                checkers = PAWN_CAPTURES[BLACK][kpos] & tsq_bit;
                break;
            case WKNIGHT:
                checkers = KNIGHT_MOVES[kpos] & tsq_bit;
                break;
            case WBISHOP:
                if (BISHOP_MOVES[kpos] & tsq_bit) {
                    checkers = magic::bishop_moves(kpos, bb[ALLPIECES]) & tsq_bit;
                }
                break;
            case WROOK:
                if (ROOK_MOVES[kpos] & tsq_bit) {
                    checkers = magic::rook_moves(kpos, bb[ALLPIECES]) & tsq_bit;
                }
                break;
            case WQUEEN:
                if (QUEEN_MOVES[kpos] & tsq_bit) {
                    checkers = magic::queen_moves(kpos, bb[ALLPIECES]) & tsq_bit;
                }
                break;
            case WKING:
                if (move->castle) {
                    if (move->castle == CASTLE_K) {
                        checkers = magic::rook_moves(kpos, bb[ALLPIECES]^BIT(ssq)) & BIT(f1);
                    } else {
                        checkers = magic::rook_moves(kpos, bb[ALLPIECES]^BIT(ssq)) & BIT(d1);
                    }
                }
                break;
            case BPAWN:
                checkers = PAWN_CAPTURES[WHITE][kpos] & tsq_bit;
                break;
            case BKNIGHT:
                checkers = KNIGHT_MOVES[kpos] & tsq_bit;
                break;
            case BBISHOP:
                if (BISHOP_MOVES[kpos] & tsq_bit) {
                    checkers = magic::bishop_moves(kpos, bb[ALLPIECES]) & tsq_bit;
                }
                break;
            case BROOK:
                if (ROOK_MOVES[kpos] & tsq_bit) {
                    checkers = magic::rook_moves(kpos, bb[ALLPIECES]) & tsq_bit;
                }
                break;
            case BQUEEN:
                if (QUEEN_MOVES[kpos] & tsq_bit) {
                    checkers = magic::queen_moves(kpos, bb[ALLPIECES]) & tsq_bit;
                }
                break;
            case BKING:
                if (move->castle) {
                    if (move->castle == CASTLE_k) {
                        checkers = magic::rook_moves(kpos, bb[ALLPIECES]^BIT(ssq)) & BIT(f8);
                    } else {
                        checkers = magic::rook_moves(kpos, bb[ALLPIECES]^BIT(ssq)) & BIT(d8);
                    }
                }
                break;
            default: assert(false);
        }
    }
    if (checkers) {
        return 1;
    }

    //is it an exposed check?
    if ((check_mask & ssq_bit) || move->en_passant) {
        U64 sliders = piece <= WKING ?
                bb[WBISHOP] | bb[WQUEEN] | bb[WROOK]
                :
                bb[BBISHOP] | bb[BQUEEN] | bb[BROOK];

        U64 exclude = ssq_bit | tsq_bit;
        sliders &= ~exclude;
        sliders &= QUEEN_MOVES[kpos];
        if (sliders) {
            U64 occ = (bb[ALLPIECES] ^ ssq_bit) | tsq_bit;
            if (move->en_passant) {
                occ ^= BIT(tsq + (kpos == get_sq(WKING) ? 8 : -8));
            }
            U64 diag = magic::bishop_moves(kpos, occ);
            U64 sliders_diag = sliders & ~(bb[WROOK] | bb[BROOK]);
            checkers = diag & sliders_diag;
            if (checkers) {
                return 2;
            }
            U64 hor_ver = magic::rook_moves(kpos, occ);
            sliders &= ~(bb[WBISHOP] | bb[BBISHOP]);
            checkers = hor_ver & sliders;
            if (checkers) {
                return 2;
            }
        }
    }

    //is it a check by promotion?
    if (move->promotion && (check_mask & tsq_bit)) {
        piece = move->promotion;
        if (piece == WKNIGHT) {
            checkers = KNIGHT_MOVES[kpos] & tsq_bit;
        } else if (piece == BKNIGHT) {
            checkers = KNIGHT_MOVES[kpos] & tsq_bit;
        } else {
            U64 checkMask = QUEEN_MOVES[kpos];
            if (checkMask & tsq_bit) {
                U64 occ = bb[ALLPIECES] ^ ssq_bit;
                switch (piece) {
                    case EMPTY:
                    case WPAWN:
                    case WKNIGHT:
                        assert(false);
                    case WBISHOP:
                        checkers = magic::bishop_moves(kpos, occ) & tsq_bit;
                        break;
                    case WROOK:
                        checkers = magic::rook_moves(kpos, occ) & tsq_bit;
                        break;
                    case WQUEEN:
                        checkers = magic::queen_moves(kpos, occ) & tsq_bit;
                        break;
                    case WKING:
                    case BPAWN:
                    case BKNIGHT:
                        assert(false);
                    case BBISHOP:
                        checkers = magic::bishop_moves(kpos, occ) & tsq_bit;
                        break;
                    case BROOK:
                        checkers = magic::rook_moves(kpos, occ) & tsq_bit;
                        break;
                    case BQUEEN:
                        checkers = magic::queen_moves(kpos, occ) & tsq_bit;
                        break;
                    case BKING:
                        assert(false);
                }
            }
        }
    }
    if (checkers) {
        return 1;
    }
    return 0;
}

/**
 * Returns MVVLVA score 
 * @return 
 */
int board_t::mvvlva(const move_t * move) {
    return board::PVAL[move->capture] + board::PVAL[move->promotion] - move->piece;
}

int board_t::max_gain(const move_t * move) {
    int result = board::PVAL[move->capture];
    if (move->promotion) {
        result += board::PVAL[move->promotion] - board::PVAL[WPAWN];
    }
    return result;
}

bool board_t::is_gain(const move_t * move) {
    if (move->capture) {
        return see(move, 1);
    }
    if (move->promotion == WQUEEN || move->promotion == BQUEEN) {
        return see(move, 0);
    }
    return false;
}

namespace {

// Both public overloads share the recapture selection and legality checks.
// Compile-time specialization removes threshold logic from full-score calls
// and the gain list/backward fold from threshold calls.
template<bool threshold_test>
std::conditional_t<threshold_test, bool, int>
see_exchange(board_t & board, const move_t * move, int threshold) {
    const int initial_gain = move->castle ? 0 : board.max_gain(move);
    if (move->castle || move->piece == WKING || move->piece == BKING) {
        if constexpr (threshold_test) return initial_gain >= threshold;
        else return initial_gain;
    }
    // The opponent can decline, so the initial gain is an upper bound.
    if constexpr (threshold_test)
        if (initial_gain < threshold) return false;

    const int target = move->tsq;
    const U64 target_bit = BIT(target);
    U64 occupied = (board.bb[ALLPIECES] & ~BIT(move->ssq)) | target_bit;
    if (move->en_passant)
        occupied &= ~BIT(target + (move->piece == WPAWN ? -8 : 8));

    int gain[32];
    int depth = 0;
    if constexpr (!threshold_test) gain[0] = initial_gain;
    int occupant = move->promotion ? move->promotion : move->piece;
    bool side = move->piece > WKING; // Opponent of the initial mover.
    const int king_square[2] = {board.get_sq(BKING), board.get_sq(WKING)};
    // Root score >= threshold iff the opponent's optional gain <= limit.
    // For W = max(0, capture_gain - next_W), each exchange flips <= to >=.
    int64_t limit = int64_t(initial_gain) - threshold;
    bool at_least = false;

    while (depth < 31) {
        const bool promotion_rank = side ? target >= a8 : target <= h1;
        if constexpr (threshold_test) {
            if (at_least && limit <= 0) return true;
            if (!at_least && limit < 0) return false;
            const int upper = board::PVAL[occupant]
                + (promotion_rank ? board::PVAL[WQUEEN] - board::PVAL[WPAWN] : 0);
            if (at_least && upper < limit) return false;
            if (!at_least && upper <= limit) return true;
        }
        // Original bitboards remain valid at every source still occupied.
        // The target occupant is tracked separately and must be masked out.
        const U64 attackers = board.attacks_to(target, occupied) & board.all(side) & ~target_bit;
        U64 from_bit = 0;
        int piece = PAWN[side];
        for (; piece <= KING[side]; ++piece) {
            U64 candidates = attackers & board.bb[piece];
            while (candidates) {
                const U64 candidate = candidates & -candidates;
                candidates &= candidates - 1;
                const U64 next_occupied = occupied & ~candidate;
                const int king = piece == KING[side] ? target : king_square[side];
                if (!(board.attacks_to(king, next_occupied) & board.all(!side) & ~target_bit)) {
                    from_bit = candidate;
                    break;
                }
            }
            if (from_bit) break;
        }
        if (!from_bit) break;
        occupied &= ~from_bit;
        const bool promotes = piece == PAWN[side] && promotion_rank;
        const int next_occupant = promotes ? QUEEN[side] : piece;
        const int promotion_gain = promotes ? board::PVAL[next_occupant] - board::PVAL[piece] : 0;
        const int capture_gain = board::PVAL[occupant] + promotion_gain;
        if constexpr (threshold_test) {
            // A legal king capture ends the exchange; otherwise declining
            // bounds W between zero and this actual capture's gain.
            if (piece == KING[side])
                return at_least ? capture_gain >= limit : capture_gain <= limit;
            if (at_least && capture_gain < limit) return false;
            if (!at_least && capture_gain <= limit) return true;
            limit = capture_gain - limit;
            at_least = !at_least;
        } else {
            gain[depth + 1] = capture_gain - gain[depth];
        }
        ++depth;
        occupant = next_occupant;
        if (piece == KING[side]) break;
        side = !side;
    }
    if constexpr (threshold_test) {
        return at_least ? 0 >= limit : 0 <= limit;
    } else {
        while (depth > 0) {
            gain[depth - 1] = -MAX(-gain[depth - 1], gain[depth]);
            --depth;
        }
        return gain[0];
    }
}

}

/**
 * Material estimate for a legal move followed by least-valuable legal
 * recaptures on its target square. Either side may decline an exchange.
 * Temporary occupancy keeps x-rays and king safety current without mutating
 * the board. This does not search positional compensation or other squares.
 */
int board_t::see(const move_t * move) {
    return see_exchange<false>(*this, move, 0);
}

bool board_t::see(const move_t * move, int threshold) {
    return see_exchange<true>(*this, move, threshold);
}

/**
 * Recognize dead positions from material alone.
 * @return true: no legal sequence can lead to mate; false: not proven dead
 */
bool board_t::is_draw() {
    if (bb[WPAWN] || bb[BPAWN]
            || bb[WROOK] || bb[BROOK]
            || bb[WQUEEN] || bb[BQUEEN]) {
        return false;
    }
    const U64 knights = bb[WKNIGHT] | bb[BKNIGHT];
    const U64 bishops = bb[WBISHOP] | bb[BBISHOP];
    if (!gt_1(knights | bishops)) {
        return true; //bare kings, or a single minor against a bare king
    }
    if (knights) {
        return false;
    }
    //Any number of bishops on a single square color cannot produce mate.
    return !(bishops & WHITE_SQUARES) || !(bishops & BLACK_SQUARES);
}

bool board_t::has_legal_enpassant() {
    const int target = stack->enpassant_sq;
    const int capture_square = target + (us() ? -8 : 8);
    if (target < (us() ? a6 : a3) || target > (us() ? h6 : h3)
            || matrix[target] != EMPTY || matrix[capture_square] != PAWN[them()]) {
        return false;
    }
    U64 candidates = PAWN_CAPTURES[them()][target] & bb[PAWN[us()]];
    while (candidates) {
        move_t move{};
        move.set(PAWN[us()], pop(candidates), target, PAWN[them()]);
        if (legal(&move)) {
            return true;
        }
    }
    return false;
}

/**
 * Initializes / resets the board structure from a FEN string
 * @param fen the FEN string
 */
void board_t::init(const char* fen) {
    //initialize:
    clear();
    int offset = a8;
    int pos = a8;
    bool wtm = true;
    int i, end = strlen(fen);
    //piece placement:
    for (i = 0; i < end; i++) {
        switch (fen[i]) {
            case ' ': offset = -1;
                break;
            case '/': offset -= 8;
                pos = offset;
                break;
            case '1': pos += 1;
                break;
            case '2': pos += 2;
                break;
            case '3': pos += 3;
                break;
            case '4': pos += 4;
                break;
            case '5': pos += 5;
                break;
            case '6': pos += 6;
                break;
            case '7': pos += 7;
                break;
            case 'p':
                add_piece(BPAWN, pos++, true);
                break;
            case 'n':
                add_piece(BKNIGHT, pos++, true);
                break;
            case 'b':
                add_piece(BBISHOP, pos++, true);
                break;
            case 'r':
                add_piece(BROOK, pos++, true);
                break;
            case 'q':
                add_piece(BQUEEN, pos++, true);
                break;
            case 'k':
                add_piece(BKING, pos++, true);
                break;
            case 'P':
                add_piece(WPAWN, pos++, true);
                break;
            case 'N':
                add_piece(WKNIGHT, pos++, true);
                break;
            case 'B':
                add_piece(WBISHOP, pos++, true);
                break;
            case 'R':
                add_piece(WROOK, pos++, true);
                break;
            case 'Q':
                add_piece(WQUEEN, pos++, true);
                break;
            case 'K':
                add_piece(WKING, pos++, true);
                break;
        }
        if (offset < 0) {
            i++;
            break;
        };
    }
    //site to move:
    for (bool done = false; i <= end; i++) {
        switch (fen[i]) {
            case '-':
            case 'W':
            case 'w':
                done = true;
                break;
            case 'B':
            case 'b':
                done = true;
                wtm = false;
        }
        if (done) {
            i++;
            break;
        };
    }
    //castling status and en-passant square:
    bool castle_dash = true;
    for (; i <= end; i++) {
        char c = fen[i];
        switch (c) {
            case 'a':
            case 'b':
            case 'c':
            case 'd':
            case 'e':
            case 'f':
            case 'g':
            case 'h':
                i++;
                if (fen[i] >= '1' && fen[i] <= '8') {
                    stack->enpassant_sq = (char((fen[i] - '1')*8 + (fen[i - 1] - 'a')));
                    HASH_EP(stack->tt_key, stack->enpassant_sq);
                }
                i += 2;
                goto half_move;
                break;
            case 'k':
                castle_dash = false;
                stack->castling_flags |= CASTLE_k;
                HASH_CASTLE_k(stack->tt_key);
                break;
            case 'q':
                castle_dash = false;
                stack->castling_flags |= CASTLE_q;
                HASH_CASTLE_q(stack->tt_key);
                break;
            case 'K':
                castle_dash = false;
                stack->castling_flags |= CASTLE_K;
                HASH_CASTLE_K(stack->tt_key);
                break;
            case 'Q':
                castle_dash = false;
                stack->castling_flags |= CASTLE_Q;
                HASH_CASTLE_Q(stack->tt_key);
                break;
            case ' ':
                break;
            case '-':
                if (castle_dash) {
                    castle_dash = false;
                } else {
                    i += 2;
                    goto half_move;
                }
            default:
                break;
        }
    }
    //half move count and move number:
half_move:
    int half_move = 0;
    int move_number = 0;
    bool do_half_move = true;
    for (; i <= end; i++) {
        char c = fen[i];
        switch (c) {
            case '0':
            case '1':
            case '2':
            case '3':
            case '4':
            case '5':
            case '6':
            case '7':
            case '8':
            case '9':
                do_half_move ? half_move = half_move * 10 + (c - '0') : move_number = move_number * 10 + (c - '0');
                break;
            case ' ':
                do_half_move = false;
            default:
                break;
        }
    }

    stack->fifty_count = half_move;
    root_ply = move_number * 2;
    if (wtm) {
        stack->wtm = true;
    } else {
        stack->wtm = false;
        HASH_STM(stack->tt_key);
        root_ply++;
    }
    stack->legal_enpassant = stack->enpassant_sq && has_legal_enpassant();
}

/**
 * Display the board structure as a FEN string
 * @return FEN string
 */
std::string board_t::to_string() {
    std::string result = "";
    int offset = 56;
    int es = 0;
    char buf[16];
    const char PIECENAME[13] = {'.', 'P', 'N', 'B', 'R', 'Q', 'K', 'p', 'n', 'b', 'r', 'q', 'k'};
    for (int r = 7; r >= 0; r--) {
        for (int f = 0; f <= 7; f++) {
            int sq = offset + f;
            int pc = matrix[sq];
            if (pc >= WPAWN && pc <= BKING) {
                if (es) {
                    sprintf(buf, "%d", es);
                    result += buf;
                    es = 0;
                }
                result += PIECENAME[pc];
            } else {
                es++;
            }
        }
        if (es) {
            sprintf(buf, "%d", es);
            result += buf;
            es = 0;
        }
        if (r > 0) {
            result += '/';
        }
        offset -= 8;
    }
    result += stack->wtm ? " w " : " b ";
    if (has_castle_right(CASTLE_ANY)) {
        if (has_castle_right(CASTLE_K)) {
            result += 'K';
        }
        if (has_castle_right(CASTLE_Q)) {
            result += 'Q';
        }
        if (has_castle_right(CASTLE_k)) {
            result += 'k';
        }
        if (has_castle_right(CASTLE_q)) {
            result += 'q';
        }
    } else {
        result += '-';
    }
    result += ' ';

    if (stack->enpassant_sq) {
        result += FILE_SYMBOL(stack->enpassant_sq);
        result += RANK_SYMBOL(stack->enpassant_sq);
    } else {
        result += "-";
    }
    result += " ";
    sprintf(buf, "%d", stack->fifty_count);
    result += buf;
    result += " ";
    sprintf(buf, "%d", (root_ply + ply) / 2);
    result += buf;
    return result;
}

/**
 * Flip the board, useful for testing on white/black bugs. A flipped
 * board should give exactly the same search and evaluation results.
 */ 
void board_t::do_flip() {
    bb[ALLPIECES] = bb_flip(bb[ALLPIECES]);
    U64 tmp = bb[WPIECES];
    bb[WPIECES] = bb_flip(bb[BPIECES]);
    bb[BPIECES] = bb_flip(tmp);
    for (int sq = 0; sq <= 31; sq++) {
        if (sq >= WPAWN && sq <= WKING) {
            U64 occ = bb[sq];
            bb[sq] = bb_flip(bb[sq + WKING]);
            bb[sq + WKING] = bb_flip(occ);
        }
        int fsq = FLIP_SQUARE(sq);
        int pc1 = matrix[sq];
        int pc2 = matrix[fsq];
        matrix[sq] = EMPTY;
        matrix[fsq] = EMPTY;
        if (pc2) {
            matrix[sq] = pc2 > WKING ? pc2 - WKING : pc2 + WKING;
        }
        if (pc1) {
            matrix[fsq] = pc1 > WKING ? pc1 - WKING : pc1 + WKING;
        }
    }
    stack->do_flip();
}
