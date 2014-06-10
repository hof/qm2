/**
 * Maxima, a chess playing program. 
 * Copyright (C) 1996-2014 Erik van het Hof and Hermen Reitsma 
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
 * - Bitboards for each piece and all (white/black) occupied squares
 * - Matrix[64]
 * - Piece placement arrays for each piece
 */

#include <cstdlib>
#include <stdio.h>
#include <iostream>
#include <string.h>
#include "board.h"
#include "hashcodes.h"
#include "evaluate.h"

/**
 * Empty and initialize the board structure
 */
void TBoard::clear() {
    _stack[0].clear();
    stack = &_stack[0];
    current_ply = 0;
    root_ply = 0;
    white_king_sq = &pieces[WKING].squares[0];
    black_king_sq = &pieces[BKING].squares[0];
    king_sq[WHITE] = white_king_sq;
    king_sq[BLACK] = black_king_sq;
    white_pawns = 0;
    black_pawns = 0;
    white_knights = 0;
    black_knights = 0;
    white_bishops = 0;
    black_bishops = 0;
    white_rooks = 0;
    black_rooks = 0;
    white_queens = 0;
    black_queens = 0;
    white_kings = 0;
    black_kings = 0;
    white_pieces = 0;
    black_pieces = 0;
    all_pieces = 0;
    knights[WHITE] = &white_knights;
    knights[BLACK] = &black_knights;
    kings[WHITE] = &white_kings;
    kings[BLACK] = &black_kings;
    queens[WHITE] = &white_queens;
    queens[BLACK] = &black_queens;
    rooks[WHITE] = &white_rooks;
    rooks[BLACK] = &black_rooks;
    bishops[WHITE] = &white_bishops;
    bishops[BLACK] = &black_bishops;
    pawns[WHITE] = &white_pawns;
    pawns[BLACK] = &black_pawns;
    boards[WPAWN] = &white_pawns;
    boards[WKNIGHT] = &white_knights;
    boards[WBISHOP] = &white_bishops;
    boards[WROOK] = &white_rooks;
    boards[WQUEEN] = &white_queens;
    boards[WKING] = &white_kings;
    boards[BPAWN] = &black_pawns;
    boards[BKNIGHT] = &black_knights;
    boards[BBISHOP] = &black_bishops;
    boards[BROOK] = &black_rooks;
    boards[BQUEEN] = &black_queens;
    boards[BKING] = &black_kings;
    boards[WPIECES] = &white_pieces;
    boards[BPIECES] = &black_pieces;
    boards[ALLPIECES] = &all_pieces;
    memset(matrix, 0, sizeof (matrix));
    memset(pieces, 0, sizeof (pieces));
}

/**
 * Do the move in the current position and update the board structure 
 * @param move move object, the move to make
 */
void TBoard::forward(TMove * move) {
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
    assert(current_ply < MAX_PLY);

    (stack + 1)->copy(stack);
    stack++;
    current_ply++;

    HASH_EP(stack->hash_code, stack->enpassant_sq); //remove (a possible) ep square from hashcode

    if (capture || promotion) {
        if (capture) {
            assert(capture != WKING && capture != BKING);
            if (move->en_passant) {
                assert(move->piece == WPAWN || move->piece == BPAWN);
                assert(matrix[tsq] == EMPTY);
                assert(stack->enpassant_sq == tsq);
                assert(tsq >= a6 ? matrix[tsq - 8] == BPAWN : matrix[tsq + 8] == WPAWN);
                removePieceFull(capture, tsq >= a6 ? tsq - 8 : tsq + 8);
            } else {
                assert(capture == matrix[tsq]);
                removePieceFull(capture, tsq);
            }
        }
        if (promotion) {
            removePieceFull(piece, ssq);
            addPieceFull(promotion, tsq);
        } else {
            movePieceFull(piece, ssq, tsq);
        }
        stack->enpassant_sq = EMPTY;
        stack->fifty_count = 0;
    } else { //not a capture or promotion
        assert(matrix[tsq] == EMPTY);
        movePieceFull(piece, ssq, tsq);
        stack->fifty_count++;
        stack->enpassant_sq = EMPTY;
        if (piece == WPAWN) {
            if (ssq <= h2 && tsq == ssq + 16) {
                stack->enpassant_sq = tsq - 8;
                HASH_EP(stack->hash_code, stack->enpassant_sq);
            }
            stack->fifty_count = 0;
        } else if (piece == BPAWN) {
            if (ssq <= h7 && tsq == ssq - 16) {
                stack->enpassant_sq = tsq + 8;
                HASH_EP(stack->hash_code, stack->enpassant_sq);
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
                    movePieceFull(WROOK, h1, f1);
                    break;
                case CASTLE_Q:
                    movePieceFull(WROOK, a1, d1);
                    break;
                case CASTLE_k:
                    movePieceFull(BROOK, h8, f8);
                    break;
                case CASTLE_q:
                    movePieceFull(BROOK, a8, d8);
                    break;
                default:
                    assert(false);
            }
        }
        /*
         * Removal of castling rights
         */
        if (castleRight(CASTLE_K) && (ssq == h1 || ssq == e1 || tsq == h1)) {
            stack->castling_flags ^= CASTLE_K;
            HASH_CASTLE_K(stack->hash_code);
            HASH_CASTLE_K(stack->pawn_hash);
        }
        if (castleRight(CASTLE_Q) && (ssq == a1 || ssq == e1 || tsq == a1)) {
            stack->castling_flags ^= CASTLE_Q;
            HASH_CASTLE_Q(stack->hash_code);
            HASH_CASTLE_Q(stack->pawn_hash);
        }
        if (castleRight(CASTLE_k) && (ssq == h8 || ssq == e8 || tsq == h8)) {
            stack->castling_flags ^= CASTLE_k;
            HASH_CASTLE_k(stack->hash_code);
            HASH_CASTLE_k(stack->pawn_hash);
        }
        if (castleRight(CASTLE_q) && (ssq == a8 || ssq == e8 || tsq == a8)) {
            stack->castling_flags ^= CASTLE_q;
            HASH_CASTLE_q(stack->hash_code);
            HASH_CASTLE_q(stack->pawn_hash);
        }

    }

    // update flags and hashcode for the side to move
    stack->wtm = !stack->wtm;
    HASH_STM(stack->hash_code);

    assert(matrix[*white_king_sq] == WKING && matrix[*black_king_sq] == BKING);
    assert(piece == matrix[tsq] || (promotion && promotion == matrix[tsq]));
}

/**
 * Undo the move, updating the board structure
 * @param move the move to unmake
 */
void TBoard::backward(TMove * move) {
    int ssq = move->ssq;
    int tsq = move->tsq;
    int promotion = move->promotion;
    int capture = move->capture;
    int piece = matrix[tsq];

    if (capture || promotion) {
        if (promotion) {
            piece = move->piece;
            addPiece(piece, ssq);
            removePiece(promotion, tsq);
        } else {
            movePiece(piece, tsq, ssq);
        }
        if (capture) {
            if (move->en_passant) {
                tsq = tsq >= a6 ? tsq - 8 : tsq + 8;
            }
            addPiece(capture, tsq);
        }
    } else { //not a capture and not a promotion
        movePiece(piece, tsq, ssq);
        /* castling: move the rook */
        if (move->castle) {
            switch (move->castle) {
                case CASTLE_K:
                    movePiece(WROOK, f1, h1);
                    break;
                case CASTLE_Q:
                    movePiece(WROOK, d1, a1);
                    break;
                case CASTLE_k:
                    movePiece(BROOK, f8, h8);
                    break;
                case CASTLE_q:
                    movePiece(BROOK, d8, a8);
                    break;
                default:
                    assert(false);

            }
        }
    }
    current_ply--;
    stack--;
}

/**
 * Do a nullmove: update board structure, e.g. switch side to move
 */
void TBoard::forward() { //do a null move
    (stack + 1)->copy(stack);
    stack++;
    current_ply++;
    HASH_EP(stack->hash_code, stack->enpassant_sq); //remove enpassant_square if it is set
    stack->enpassant_sq = EMPTY;
    stack->wtm = !stack->wtm;
    HASH_STM(stack->hash_code);
}

/**
 * Undo a nullmove: restore board structure
 */
void TBoard::backward() { //undo a null move
    current_ply--;
    stack--;
}

/**
 * See if the move is valid in the current board (legality is not checked here)
 * "valid" means: the move would be generated by the move generator
 * @param move the move to test for validity, not yet performed in the position
 * @return true if the move is valid in the current position, false otherwise 
 */
bool TBoard::valid(TMove * move) {
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
        if (!castleRight(castle)) {
            return false;
        }
        switch (castle) {
            case EMPTY:
                break;
            case CASTLE_K:
                return matrix[h1] == WROOK && matrix[f1] == EMPTY && matrix[g1] == EMPTY
                        && !attackedByBlack(f1) && !attackedByBlack(g1) && !attackedByBlack(e1);
            case CASTLE_Q:
                return matrix[a1] == WROOK && matrix[b1] == EMPTY && matrix[c1] == EMPTY && matrix[d1] == EMPTY
                        && !attackedByBlack(e1) && !attackedByBlack(d1) && !attackedByBlack(c1);
            case CASTLE_k:
                return matrix[h8] == BROOK && matrix[f8] == EMPTY && matrix[g8] == EMPTY
                        && !attackedByWhite(f8) && !attackedByWhite(g8) && !attackedByWhite(e8);
            case CASTLE_q:
                return matrix[a8] == BROOK && matrix[b8] == EMPTY && matrix[c8] == EMPTY && matrix[d8] == EMPTY
                        && !attackedByWhite(e8) && !attackedByWhite(d8) && !attackedByWhite(c8);
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
            return MagicBishopMoves(ssq, all_pieces) & BIT(tsq);
        case WROOK:
            return MagicRookMoves(ssq, all_pieces) & BIT(tsq);
        case WQUEEN:
            return MagicQueenMoves(ssq, all_pieces) & BIT(tsq);
        case WKING:
            return true;
        case BPAWN:
            return captured || ssq - tsq == 8 || matrix[tsq + 8] == EMPTY;
        case BKNIGHT:
            return true;
        case BBISHOP:
            return MagicBishopMoves(ssq, all_pieces) & BIT(tsq);
        case BROOK:
            return MagicRookMoves(ssq, all_pieces) & BIT(tsq);
        case BQUEEN:
            return MagicQueenMoves(ssq, all_pieces) & BIT(tsq);
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
bool TBoard::legal(TMove * move) {
    int piece = move->piece;
    int tsq = move->tsq;
    assert(piece >= WPAWN && piece <= BKING && tsq >= a1 && tsq <= h8);
    U64 tsq_bit = BIT(tsq);
    U64 occ = all_pieces & ~tsq_bit;
    if (stack->wtm) {
        move->en_passant = stack->enpassant_sq && tsq == stack->enpassant_sq && piece == WPAWN;
        if (move->en_passant) {
            occ ^= tsq_bit >> 8;
        }

        //1. check for direct checks on our king (non-sliders or contact checks)
        int kpos_list[2] = {*white_king_sq, tsq};
        int kpos = kpos_list[piece == WKING];

        if ((black_knights & occ & KNIGHT_MOVES[kpos])
                || (black_pawns & occ & WPAWN_CAPTURES[kpos])
                || (piece == WKING && KING_MOVES[kpos] & black_kings)) {
            return false;
        }

        move->castle = 0;
        if (move->ssq == e1 && piece == WKING) {
            if (tsq == g1) {
                move->castle = CASTLE_K;
                if (attackedByBlack(e1) || attackedByBlack(f1) || attackedByBlack(g1)) {
                    return false;
                }
            } else if (tsq == c1) {
                move->castle = CASTLE_Q;
                if (attackedByBlack(e1) || attackedByBlack(d1) || attackedByBlack(c1)) {
                    return false;
                }
            }
        }
        move->special = 0;

        //2. update the occupied bitboard and check for sliding attacks on the king
        //   which can happen when we move a piece which was pinned, or move the king
        //   on the same path as a sliding attack.
        //   note we deliberately dont update occupied for rooks for castling moves
        //   but en-passant update is required 
        U64 opp_sliders = (black_bishops | black_rooks | black_queens) & ~tsq_bit;
        if (QUEEN_MOVES[kpos] & opp_sliders) {
            U64 opp_diag_sliders = opp_sliders & ~black_rooks;
            U64 opp_hv_sliders = opp_sliders & ~black_bishops;
            occ ^= BIT(move->ssq);
            occ |= tsq_bit;
            return !(MagicBishopMoves(kpos, occ) & opp_diag_sliders)
                    && !(MagicRookMoves(kpos, occ) & opp_hv_sliders);
        }
    } else {
        move->en_passant = stack->enpassant_sq && tsq == stack->enpassant_sq && piece == BPAWN;
        if (move->en_passant) {
            occ ^= tsq_bit << 8;
        }

        //1. check for direct checks on our king (non-sliders or contact checks)
        int kpos_list[2] = {*black_king_sq, tsq};
        int kpos = kpos_list[piece == BKING];

        if ((white_knights & occ & KNIGHT_MOVES[kpos])
                || (white_pawns & occ & BPAWN_CAPTURES[kpos])
                || (piece == BKING && KING_MOVES[kpos] & white_kings)) {
            return false;
        }

        move->castle = 0;
        if (move->ssq == e8 && piece == BKING) {
            if (tsq == g8) {
                move->castle = CASTLE_k;
                if (attackedByWhite(e8) || attackedByWhite(f8) || attackedByWhite(g8)) {
                    return false;
                }
            } else if (tsq == c8) {
                move->castle = CASTLE_q;
                if (attackedByWhite(e8) || attackedByWhite(d8) || attackedByWhite(c8)) {
                    return false;
                }
            }
        }
        move->special = 0;

        //2. update the occupied bitboard and check for sliding attacks on the king
        //   which can happen when we move a piece which was pinned, or move the king
        //   on the same path as a sliding attack.
        //   note we deliberately dont update occupied for rooks for castling moves
        //   but en-passant update is required 


        U64 opp_sliders = (white_bishops | white_rooks | white_queens) & ~tsq_bit;
        if (QUEEN_MOVES[kpos] & opp_sliders) {
            U64 opp_diag_sliders = opp_sliders & ~white_rooks;
            U64 opp_hv_sliders = opp_sliders & ~white_bishops;
            occ ^= BIT(move->ssq);
            occ |= tsq_bit;
            return !(MagicBishopMoves(kpos, occ) & opp_diag_sliders)
                    && !(MagicRookMoves(kpos, occ) & opp_hv_sliders);
        }
    }
    return true;
}

/**
 * Verify if a move checks the opponent's king
 * @param move the move to verify
 * @return 0: no check, 1: simple, direct check, 2: exposed check 
 */
int TBoard::givesCheck(TMove * move) {
    int ssq = move->ssq;
    int tsq = move->tsq;
    int piece = move->piece;
    U64 ssq_bit = BIT(ssq);
    U64 tsq_bit = BIT(tsq);
    U64 checkers = 0;
    int kpos_list[2] = {*white_king_sq, *black_king_sq};
    int kpos = kpos_list[piece <= WKING];
    U64 check_mask = QUEEN_MOVES[kpos] | KNIGHT_MOVES[kpos];
    if ((check_mask & (ssq_bit | tsq_bit)) == 0 && !move->castle && !move->en_passant) {
        return 0;
    }

    //is it a direct check?
    if ((check_mask & tsq_bit) || move->castle) {
        switch (piece) {
            case EMPTY:
                assert(false);
            case WPAWN:
                checkers = BPAWN_CAPTURES[kpos] & tsq_bit;
                break;
            case WKNIGHT:
                checkers = KNIGHT_MOVES[kpos] & tsq_bit;
                break;
            case WBISHOP:
                if (BISHOP_MOVES[kpos] & tsq_bit) {
                    checkers = MagicBishopMoves(kpos, all_pieces) & tsq_bit;
                }
                break;
            case WROOK:
                if (ROOK_MOVES[kpos] & tsq_bit) {
                    checkers = MagicRookMoves(kpos, all_pieces) & tsq_bit;
                }
                break;
            case WQUEEN:
                if (QUEEN_MOVES[kpos] & tsq_bit) {
                    checkers = MagicQueenMoves(kpos, all_pieces) & tsq_bit;
                }
            case WKING:
                if (move->castle) {
                    if (move->castle == CASTLE_K) {
                        checkers = MagicRookMoves(kpos, all_pieces^BIT(ssq)) & BIT(f1);
                    } else {
                        checkers = MagicRookMoves(kpos, all_pieces^BIT(ssq)) & BIT(d1);
                    }
                }
                break;
            case BPAWN:
                checkers = WPAWN_CAPTURES[kpos] & tsq_bit;
                break;
            case BKNIGHT:
                checkers = KNIGHT_MOVES[kpos] & tsq_bit;
                break;
            case BBISHOP:
                if (BISHOP_MOVES[kpos] & tsq_bit) {
                    checkers = MagicBishopMoves(kpos, all_pieces) & tsq_bit;
                }
                break;
            case BROOK:
                if (ROOK_MOVES[kpos] & tsq_bit) {
                    checkers = MagicRookMoves(kpos, all_pieces) & tsq_bit;
                }
                break;
            case BQUEEN:
                if (QUEEN_MOVES[kpos] & tsq_bit) {
                    checkers = MagicQueenMoves(kpos, all_pieces) & tsq_bit;
                }
            case BKING:
                if (move->castle) {
                    if (move->castle == CASTLE_k) {
                        checkers = MagicRookMoves(kpos, all_pieces^BIT(ssq)) & BIT(f8);
                    } else {
                        checkers = MagicRookMoves(kpos, all_pieces^BIT(ssq)) & BIT(d8);
                    }
                }
                break;
            default: assert(false);
        }
    }
    if (checkers) {
        (stack + 1)->checkers = checkers;
        (stack + 1)->checker_sq = tsq;
        return 1;
    }

    //is it an exposed check?
    if ((check_mask & ssq_bit) || move->en_passant) {
        U64 sliders = piece <= WKING ?
                white_bishops | white_queens | white_rooks
                :
                black_bishops | black_queens | black_rooks;

        U64 exclude = ssq_bit | tsq_bit;
        sliders &= ~exclude;
        sliders &= QUEEN_MOVES[kpos];
        if (sliders) {
            U64 occ = (all_pieces ^ ssq_bit) | tsq_bit;
            if (move->en_passant) {
                occ ^= BIT(tsq + (kpos == *white_king_sq ? 8 : -8));
            }
            U64 diag = MagicBishopMoves(kpos, occ);
            U64 sliders_diag = sliders & ~(white_rooks | black_rooks);
            checkers = diag & sliders_diag;
            if (checkers) {
                (stack + 1)->checkers = checkers;
                (stack + 1)->checker_sq = BSF(checkers);
                return 2;
            }
            U64 hor_ver = MagicRookMoves(kpos, occ);
            sliders &= ~(white_bishops | black_bishops);
            checkers = hor_ver & sliders;
            if (checkers) {
                (stack + 1)->checkers = checkers;
                (stack + 1)->checker_sq = BSF(checkers);
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
                U64 occ = all_pieces ^ ssq_bit;
                switch (piece) {
                    case EMPTY:
                    case WPAWN:
                    case WKNIGHT:
                        assert(false);
                    case WBISHOP:
                        checkers = MagicBishopMoves(kpos, occ) & tsq_bit;
                        break;
                    case WROOK:
                        checkers = MagicRookMoves(kpos, occ) & tsq_bit;
                        break;
                    case WQUEEN:
                        checkers = MagicQueenMoves(kpos, occ) & tsq_bit;
                        break;
                    case WKING:
                    case BPAWN:
                    case BKNIGHT:
                        assert(false);
                    case BBISHOP:
                        checkers = MagicBishopMoves(kpos, occ) & tsq_bit;
                        break;
                    case BROOK:
                        checkers = MagicRookMoves(kpos, occ) & tsq_bit;
                        break;
                    case BQUEEN:
                        checkers = MagicQueenMoves(kpos, occ) & tsq_bit;
                        break;
                    case BKING:
                        assert(false);
                }
            }
        }
    }
    if (checkers) {
        (stack + 1)->checkers = checkers;
        (stack + 1)->checker_sq = tsq;
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
U64 TBoard::getSmallestAttacker(U64 attacks, bool wtm, uint8_t & piece) {
    uint8_t first_piece = PAWN[wtm];
    uint8_t last_piece = first_piece + (WKING - WPAWN);
    for (piece = first_piece; piece <= last_piece; piece++) {
        U64 subset = attacks & *(boards[piece]);
        if (subset) {
            return subset & -subset;
        }
    }
    return 0;
}

/**
 * SEE, Static Exchange Evaluator. Verify if a move or capture wins or looses material
 * @param move the move to verify
 * @return expected gain or loss by playing this move as a number in centipawns (e.g +300 when fully winning a knight)
 */
int TBoard::SEE(TMove * move) {
    uint8_t captured_piece = move->capture;
    uint8_t moving_piece = move->piece;
    int captured_val = PIECE_VALUE[captured_piece];

    /*
     * 0. If the king captures, it's always a gain
     */
    if (moving_piece == WKING || moving_piece == BKING) {
        return captured_val;
    }

    /*
     * 1. if a lower valued piece captures a higher valued piece 
     * return quickly as we are (almost) sure this capture gains material.
     * (e.g. pawn x knight)
     */
    int piece_val = PIECE_VALUE[moving_piece];
    if (piece_val < captured_val) {
        return captured_val - piece_val;
    }

    /*
     * 2. if a piece (not pawn) captures a pawn and that pawn is defended by
     * another pawn, return a negative score quickly
     */
    int tsq = move->tsq;
    if ((moving_piece > BPAWN && captured_piece == WPAWN && BPAWN_CAPTURES[tsq] & white_pawns)
            || (moving_piece < WKING && moving_piece > WPAWN && captured_piece == BPAWN && WPAWN_CAPTURES[tsq] & black_pawns)) {
        return captured_val - piece_val;
    }

    /*
     * 3. full static exchange evaluation using the swap algoritm
     */
    bool wtm = moving_piece > WKING;
    U64 attacks = attacksTo(tsq);
    int gain[32];
    int depth = 0;
    U64 from_bit = BIT(move->ssq);
    U64 occ = *boards[ALLPIECES];
    const U64 diag_sliders = white_bishops | white_queens | black_bishops | black_queens;
    const U64 hor_ver_sliders = white_rooks | white_queens | black_rooks | black_queens;
    const U64 xrays = diag_sliders | hor_ver_sliders;

    if (!captured_piece && (moving_piece == WPAWN || moving_piece == BPAWN)) {
        //set the non-capturing pawn move ssq bit in the attacks bitboard, 
        //so it will be removed again in the swap loop
        attacks ^= from_bit;
    }

    gain[0] = captured_val;
    do {
        depth++;
        gain[depth] = PIECE_VALUE[moving_piece] - gain[depth - 1];
        attacks ^= from_bit;
        occ ^= from_bit;
        if (from_bit & xrays) {
            attacks |= MagicBishopMoves(tsq, occ) & occ & diag_sliders;
            attacks |= MagicRookMoves(tsq, occ) & occ & hor_ver_sliders;
        }
        from_bit = getSmallestAttacker(attacks, wtm, moving_piece);
        wtm = !wtm;
    } while (from_bit);
    while (--depth) {
        gain[depth - 1] = -MAX(-gain[depth - 1], gain[depth]);
    }
    return gain[0];
}

/**
 * Verify if the position on the board is a trivial, theoretical draw
 * @return true: it's a draw; false: not a trivial draw
 */
bool TBoard::isDraw() {
    if (white_pawns || black_pawns
            || white_rooks || black_rooks
            || white_queens || black_queens
            || (white_knights && white_bishops) || (black_knights && black_bishops)
            || gt_1(white_bishops) || gt_1(black_bishops)) {
        return false; //not a draw.. there is mating material on the board
    }
    int wn = pieces[WKNIGHT].count;
    if (wn > 2) { //3 knights, exotic!
        return false;
    }
    int bn = pieces[BKNIGHT].count;
    if (bn > 2) {
        return false;
    }

    //at this point a side can have: a) no pieces, b) 1 knight, c) 2 knights or d) 1 bishop.
    int wb = pieces[WBISHOP].count;
    int bb = pieces[BBISHOP].count;
    assert(wn <= 2 && wb <= 1);
    assert(bn <= 2 && bb <= 1);
    assert(!(wb >= 1 && wn >= 1));
    assert(!(bb >= 1 && bn >= 1));

    int wminors = wb + wn;
    int bminors = bb + bn;
    if ((wminors == 0 && bminors == 1)
            || (bminors == 0 && wminors == 1)) {
        return true; //simple case of 1 minor vs no minors (KBK or KNK)
    }

    //for other cases, mates are only possible with a king on the edge
    if ((white_kings & EDGE) || (black_kings & EDGE)) {
        return false;
    }
    return true; //save to assume theoretical draw, when no king is on the edge
}

/**
 * Construct the board structure from a FEN string
 * @param fen the FEN string
 */
void TBoard::fromFen(const char* fen) {
    //initialize:
    clear();
    char offset = a8;
    unsigned char pos = a8;
    bool wtm = true;
    int i, end = strlen(fen);
    //piece placement:
    for (i = 0; i < end; i++) {
        switch (fen[i]) {
            case ' ': offset = -1;
                break;
            case '/': offset -= char(8);
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
                addPieceFull(BPAWN, pos++);
                break;
            case 'n':
                addPieceFull(BKNIGHT, pos++);
                break;
            case 'b':
                addPieceFull(BBISHOP, pos++);
                break;
            case 'r':
                addPieceFull(BROOK, pos++);
                break;
            case 'q':
                addPieceFull(BQUEEN, pos++);
                break;
            case 'k':
                addPieceFull(BKING, pos++);
                break;
            case 'P':
                addPieceFull(WPAWN, pos++);
                break;
            case 'N':
                addPieceFull(WKNIGHT, pos++);
                break;
            case 'B':
                addPieceFull(WBISHOP, pos++);
                break;
            case 'R':
                addPieceFull(WROOK, pos++);
                break;
            case 'Q':
                addPieceFull(WQUEEN, pos++);
                break;
            case 'K':
                addPieceFull(WKING, pos++);
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
                    HASH_EP(stack->hash_code, stack->enpassant_sq);
                }
                i += 2;
                goto half_move;
                break;
            case 'k':
                castle_dash = false;
                stack->castling_flags |= CASTLE_k;
                HASH_CASTLE_k(stack->hash_code);
                HASH_CASTLE_k(stack->pawn_hash);
                break;
            case 'q':
                castle_dash = false;
                stack->castling_flags |= CASTLE_q;
                HASH_CASTLE_q(stack->hash_code);
                HASH_CASTLE_q(stack->pawn_hash);
                break;
            case 'K':
                castle_dash = false;
                stack->castling_flags |= CASTLE_K;
                HASH_CASTLE_K(stack->hash_code);
                HASH_CASTLE_K(stack->pawn_hash);
                break;
            case 'Q':
                castle_dash = false;
                stack->castling_flags |= CASTLE_Q;
                HASH_CASTLE_Q(stack->hash_code);
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
        c = c;
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
        HASH_STM(stack->hash_code);
        root_ply++;
    }
}

/**
 * Display the board structure as a FEN string
 * @return FEN string
 */
string TBoard::asFen() {
    string result = "";
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
    if (castleRight(CASTLE_ANY)) {
        if (castleRight(CASTLE_K)) {
            result += 'K';
        }
        if (castleRight(CASTLE_Q)) {
            result += 'Q';
        }
        if (castleRight(CASTLE_k)) {
            result += 'k';
        }
        if (castleRight(CASTLE_q)) {
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
    sprintf(buf, "%d", getGamePly() / 2);
    result += buf;
    return result;
}

/**
 * Flip the board, useful for testing on white/black bugs. A flipped
 * board should give exactly the same search and evaluation results.
 */
void TBoard::flip() {
    all_pieces = flipBB(all_pieces);
    U64 tmp = white_pieces;
    white_pieces = flipBB(black_pieces);
    black_pieces = flipBB(tmp);
    for (int sq = 0; sq <= 31; sq++) {
        if (sq <= WKING && sq >= WPAWN) {
            U64 occ = *boards[sq];
            *boards[sq] = flipBB(*boards[sq + WKING]);
            *boards[sq + WKING] = flipBB(occ);

            TPiecePlacement * pp_w = &pieces[sq];
            TPiecePlacement * pp_b = &pieces[sq + WKING];
            TPiecePlacement * tmp = &pieces[EMPTY];
            tmp->count = pp_w->count;
            for (int i = 0; i < tmp->count; i++) {
                tmp->squares[i] = pp_w->squares[i];
            }
            pp_w->count = pp_b->count;
            for (int i = 0; i < pp_b->count; i++) {
                pp_w->squares[i] = FLIP_SQUARE(pp_b->squares[i]);
            }
            pp_b->count = tmp->count;
            for (int i = 0; i < tmp->count; i++) {
                pp_b->squares[i] = FLIP_SQUARE(tmp->squares[i]);
            }
            tmp->count = 0;
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
    stack->flip();
}

/**
 * Test the board, verify the bitboards, pieceplacement and matrix match.
 */
int TBoard::test() {
    U64 bb_occ[BKING+1];
    unsigned char matrix_test[64];
    memset(matrix_test, 0, sizeof(matrix_test));
    for (int pc = WPAWN; pc <= BKING; pc++) {
        bb_occ[pc] = 0;
        TPiecePlacement * pp = &pieces[pc];
        for (int i = 0; i < pp->count; i++) {
            int sq = pp->squares[i];
            if (matrix[sq] != pc) {
                return 100;
            }
            matrix_test[sq] = pc;
            bb_occ[pc] |= BIT(sq);
        }
        if (bb_occ[pc] != *boards[pc]) {
            return 101;
        }
    }
    if (memcmp(matrix, matrix_test, sizeof(matrix)) != 0) {
        return 102;
    }
    U64 bb_w = bb_occ[WPAWN] | bb_occ[WKNIGHT] | bb_occ[WBISHOP] | bb_occ[WROOK] | bb_occ[WQUEEN] | bb_occ[WKING];
    if (bb_w != white_pieces) {
        return 103;
    }
    U64 bb_b = bb_occ[BPAWN] | bb_occ[BKNIGHT] | bb_occ[BBISHOP] | bb_occ[BROOK] | bb_occ[BQUEEN] | bb_occ[BKING];
    if (bb_b != black_pieces) {
        return 104;
    }
    if (all_pieces != (bb_w | bb_b)) {
        return 105;
    }    
    return 0;
}