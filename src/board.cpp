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

/**
 * Flips board stack (e.g. the castling rights and side to move)
 */
void board_stack_t::do_flip() {
    wtm = !wtm;
    HASH_STM(tt_key);
    if (enpassant_sq) {
        enpassant_sq = FLIP_SQUARE(enpassant_sq);
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
        HASH_ADD_PIECE(stack->material_hash, piece, count(piece));
        HASH_ADD_PIECE(stack->tt_key, piece, sq);
        if (piece == WPAWN || piece == BPAWN || piece == WKING || piece == BKING) {
            HASH_ADD_PIECE(stack->pawn_hash, piece, sq);
        }
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
        HASH_REMOVE_PIECE(stack->material_hash, piece, count(piece));
        HASH_REMOVE_PIECE(stack->tt_key, piece, sq);
        if (piece == WPAWN || piece == BPAWN || piece == WKING || piece == BKING) {
            HASH_REMOVE_PIECE(stack->pawn_hash, piece, sq);
        }
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
        if (piece == WPAWN || piece == BPAWN || piece == WKING || piece == BKING) {
            HASH_MOVE_PIECE(stack->pawn_hash, piece, ssq, tsq);
        }
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
            HASH_CASTLE_K(stack->pawn_hash);
        }
        if (has_castle_right(CASTLE_Q) && (ssq == a1 || ssq == e1 || tsq == a1)) {
            stack->castling_flags ^= CASTLE_Q;
            HASH_CASTLE_Q(stack->tt_key);
            HASH_CASTLE_Q(stack->pawn_hash);
        }
        if (has_castle_right(CASTLE_k) && (ssq == h8 || ssq == e8 || tsq == h8)) {
            stack->castling_flags ^= CASTLE_k;
            HASH_CASTLE_k(stack->tt_key);
            HASH_CASTLE_k(stack->pawn_hash);
        }
        if (has_castle_right(CASTLE_q) && (ssq == a8 || ssq == e8 || tsq == a8)) {
            stack->castling_flags ^= CASTLE_q;
            HASH_CASTLE_q(stack->tt_key);
            HASH_CASTLE_q(stack->pawn_hash);
        }

    }

    // update flags and hashcode for the side to move
    stack->wtm = !stack->wtm;
    HASH_STM(stack->tt_key);

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
                return matrix[h1] == WROOK && matrix[f1] == EMPTY && matrix[g1] == EMPTY
                        && !is_attacked(f1, BLACK) && !is_attacked(g1, BLACK) && !is_attacked(e1, BLACK);
            case CASTLE_Q:
                return matrix[a1] == WROOK && matrix[b1] == EMPTY && matrix[c1] == EMPTY && matrix[d1] == EMPTY
                        && !is_attacked(e1, BLACK) && !is_attacked(d1, BLACK) && !is_attacked(c1, BLACK);
            case CASTLE_k:
                return matrix[h8] == BROOK && matrix[f8] == EMPTY && matrix[g8] == EMPTY
                        && !is_attacked(f8, WHITE) && !is_attacked(g8, WHITE) && !is_attacked(e8, WHITE);
            case CASTLE_q:
                return matrix[a8] == BROOK && matrix[b8] == EMPTY && matrix[c8] == EMPTY && matrix[d8] == EMPTY
                        && !is_attacked(e8, WHITE) && !is_attacked(d8, WHITE) && !is_attacked(c8, WHITE);
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
 * Get the smallest attacking piece. This function is used in the SEE routine
 * @param attacks bitboard with attack information
 * @param wtm side to move, white(1) or black(0)
 * @param piece this will be set to the piece type 
 * @return bitboard with the location of the smallest attacker
 */
U64 board_t::smallest_attacker(const U64 attacks, const bool wtm, int & piece) {
    if (attacks & all(wtm)) {
        for (piece = PAWN[wtm]; piece <= KING[wtm]; piece++) {
            U64 subset = attacks & bb[piece];
            if (subset) {
                return subset & -subset;
            }
        }
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
        return see(move) > 0;
    }
    if (move->promotion == WQUEEN || move->promotion == BQUEEN) {
        return see(move) >= 0;
    }
    return false;
}

/**
 * SEE, Static Exchange Evaluator. Verify if a move or capture wins or looses material
 * @param move the move to verify
 * @return expected gain or loss by playing this move as a number in centipawns (e.g +300 when winning a knight)
 */
int board_t::see(const move_t * move) {
    int captured_piece = move->capture;
    int moving_piece = move->piece;
    int captured_val = board::PVAL[captured_piece];

    /*
     * 1. A capture by a king is always a gain
     */

    if (moving_piece == WKING || moving_piece == BKING) {
        return captured_val;
    }

    /*
     * 2. If a lower value piece captures a higher value piece, it's a gain
     * (e.g. pawn x knight)
     */

    int piece_val = board::PVAL[moving_piece];
    if (piece_val < captured_val) {
        return captured_val - piece_val;
    }

    /*
     * 3. If a piece captures a lower value piece that is defended by
     * a pawn, it's a loss
     * (e.g. rook captures knight that is defended by a pawn)
     */

    int tsq = move->tsq;
    bool wtm = moving_piece > WKING;
    if (captured_val && piece_val > captured_val && is_attacked_by_pawn(tsq, wtm)) {
        return captured_val - piece_val;
    }

    /*
     * 4. full static exchange evaluation using the swap algorithm
     */

    U64 attacks = attacks_to(tsq);
    int gain[32];
    int depth = 0;
    U64 from_bit = BIT(move->ssq);
    U64 occ = bb[ALLPIECES];
    const U64 queens = bb[WQUEEN] | bb[BQUEEN];
    const U64 diag_sliders = bb[WBISHOP] | bb[BBISHOP] | queens;
    const U64 hor_ver_sliders = bb[WROOK] | bb[BROOK] | queens;
    const U64 xrays = diag_sliders | hor_ver_sliders | bb[WPAWN] | bb[BPAWN];
    gain[0] = captured_val;

    //set the non-capturing pawn move ssq bit in the attacks bitboard, so it
    //will be removed again in the swap loop
    if (!captured_piece && (moving_piece == WPAWN || moving_piece == BPAWN)) {
        attacks ^= from_bit;
    }

    //swap loop
    do {
        depth++;
        gain[depth] = board::PVAL[moving_piece] - gain[depth - 1];
        attacks ^= from_bit;
        occ ^= from_bit;
        if (from_bit & xrays) {
            attacks |= magic::bishop_moves(tsq, occ) & occ & diag_sliders;
            attacks |= magic::rook_moves(tsq, occ) & occ & hor_ver_sliders;
        }
        from_bit = smallest_attacker(attacks, wtm, moving_piece);
        wtm = !wtm;
    } while (from_bit);

    //calculate and return final material gain
    while (--depth) {
        gain[depth - 1] = -MAX(-gain[depth - 1], gain[depth]);
    }
    return gain[0];
}

/**
 * Verify if the position on the board is a trivial, theoretical draw
 * @return true: it's a draw; false: not a trivial draw
 */
bool board_t::is_draw() {
    if (bb[WPAWN] || bb[BPAWN]
            || bb[WROOK] || bb[BROOK]
            || bb[WQUEEN] || bb[BQUEEN]
            || (bb[WKNIGHT] && bb[WBISHOP]) || (bb[BKNIGHT] && bb[BBISHOP])
            || gt_1(bb[WBISHOP]) || gt_1(bb[BBISHOP])) {
        return false; //not a draw.. there is mating material on the board
    }
    int wn = count(WKNIGHT);
    if (wn > 2) { //3 knights, exotic!
        return false;
    }
    int bn = count(BKNIGHT);
    if (bn > 2) {
        return false;
    }

    //at this point a side can have: a) no pieces, b) 1 knight, c) 2 knights or d) 1 bishop.
    int wbc = count(WBISHOP);
    int bbc = count(BBISHOP);
    assert(wn <= 2 && wbc <= 1);
    assert(bn <= 2 && bbc <= 1);
    assert(!(wbc >= 1 && wn >= 1));
    assert(!(bbc >= 1 && bn >= 1));

    int wminors = wbc + wn;
    int bminors = bbc + bn;
    if ((wminors == 0 && bminors == 1)
            || (bminors == 0 && wminors == 1)) {
        return true; //simple case of 1 minor vs no minors (KBK or KNK)
    }

    //for other cases, mates are only possible with a king on the edge
    if ((bb[WKING] & EDGE) || (bb[BKING] & EDGE)) {
        return false;
    }
    return true; //save to assume theoretical draw, when no king is on the edge
}

bool board_t::is_eg(endgame_t eg, bool us) {
    const bool them = !us;
    switch (eg) {
        case OPP_BISHOPS:
            return bb[WKNIGHT] == 0 && bb[BKNIGHT] == 0
                    && is_1(bb[WBISHOP]) && is_1(bb[BBISHOP])
                    && bb[WROOK] == 0 && bb[BROOK] == 0
                    && bb[WQUEEN] == 0 && bb[BQUEEN] == 0
                    && bool(bb[WBISHOP] & WHITE_SQUARES) == bool(bb[BBISHOP] & BLACK_SQUARES);
        case KBBKN:
            return bb[WPAWN] == 0 && bb[BPAWN] == 0
                    && bb[KNIGHT[us]] == 0 && is_1(bb[KNIGHT[them]])
                    && has_bishop_pair(us) && bb[BISHOP[them]] == 0
                    && bb[WROOK] == 0 && bb[BROOK] == 0
                    && bb[WQUEEN] == 0 && bb[BQUEEN] == 0;
        case KBPSK:
            return bb[PAWN[them]] == 0 && bb[PAWN[us]] != 0
                    && bb[WKNIGHT] == 0 && bb[BKNIGHT] == 0
                    && is_1(bb[BISHOP[us]]) && bb[BISHOP[them]] == 0
                    && bb[WROOK] == 0 && bb[BROOK] == 0
                    && bb[WQUEEN] == 0 && bb[BQUEEN] == 0;
        case KNPK:
            return bb[PAWN[them]] == 0 && is_1(bb[PAWN[us]])
                    && is_1(bb[KNIGHT[us]]) && bb[KNIGHT[them]] == 0
                    && bb[WBISHOP] == 0 && bb[BBISHOP] == 0
                    && bb[WROOK] == 0 && bb[BROOK] == 0
                    && bb[WQUEEN] == 0 && bb[BQUEEN] == 0;
        case KRKP:
            return bb[PAWN[us]] == 0 && is_1(bb[PAWN[them]])
                    && bb[WKNIGHT] == 0 && bb[BKNIGHT] == 0
                    && bb[WBISHOP] == 0 && bb[BBISHOP] == 0
                    && is_1(bb[ROOK[us]]) && bb[ROOK[them]] == 0
                    && bb[WQUEEN] == 0 && bb[BQUEEN] == 0;
        case KQKP:
            return bb[PAWN[us]] == 0 && is_1(bb[PAWN[them]])
                    && bb[WKNIGHT] == 0 && bb[BKNIGHT] == 0
                    && bb[WBISHOP] == 0 && bb[BBISHOP] == 0
                    && bb[WROOK] == 0 && bb[BROOK] == 0
                    && is_1(bb[QUEEN[us]]) && bb[QUEEN[them]] == 0;
        case KRPKR:
            return is_1(bb[PAWN[us]]) && bb[PAWN[them]] == 0
                    && bb[WKNIGHT] == 0 && bb[BKNIGHT] == 0
                    && bb[WBISHOP] == 0 && bb[BBISHOP] == 0
                    && is_1(bb[ROOK[us]]) && is_1(bb[ROOK[them]])
                    && bb[WQUEEN] == 0 && bb[BQUEEN] == 0;
        case KQPSKQ:
            return bb[PAWN[us]] != 0 && bb[PAWN[them]] == 0
                    && bb[WKNIGHT] == 0 && bb[BKNIGHT] == 0
                    && bb[WBISHOP] == 0 && bb[BBISHOP] == 0
                    && bb[ROOK[us]] == 0 && bb[ROOK[them]] == 0
                    && is_1(bb[WQUEEN]) && is_1(bb[BQUEEN]);
        case KQPSKQPS:
            return bb[PAWN[us]] != 0 && bb[PAWN[them]] != 0
                    && bb[WKNIGHT] == 0 && bb[BKNIGHT] == 0
                    && bb[WBISHOP] == 0 && bb[BBISHOP] == 0
                    && bb[ROOK[us]] == 0 && bb[ROOK[them]] == 0
                    && is_1(bb[WQUEEN]) && is_1(bb[BQUEEN]);
        default:
            return false;
    }
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
                HASH_CASTLE_k(stack->pawn_hash);
                break;
            case 'q':
                castle_dash = false;
                stack->castling_flags |= CASTLE_q;
                HASH_CASTLE_q(stack->tt_key);
                HASH_CASTLE_q(stack->pawn_hash);
                break;
            case 'K':
                castle_dash = false;
                stack->castling_flags |= CASTLE_K;
                HASH_CASTLE_K(stack->tt_key);
                HASH_CASTLE_K(stack->pawn_hash);
                break;
            case 'Q':
                castle_dash = false;
                stack->castling_flags |= CASTLE_Q;
                HASH_CASTLE_Q(stack->tt_key);
                HASH_CASTLE_Q(stack->pawn_hash);
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