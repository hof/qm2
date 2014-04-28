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
 * board.cpp
 * Board structure implementation
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
    currentPly = 0;
    rootPly = 0;
    whiteKingPos = &pieces[WKING].squares[0];
    blackKingPos = &pieces[BKING].squares[0];
    kingPos[WHITE] = whiteKingPos;
    kingPos[BLACK] = blackKingPos;
    whitePawns = 0;
    blackPawns = 0;
    whiteKnights = 0;
    blackKnights = 0;
    whiteBishops = 0;
    blackBishops = 0;
    whiteRooks = 0;
    blackRooks = 0;
    whiteQueens = 0;
    blackQueens = 0;
    whiteKings = 0;
    blackKings = 0;
    whitePieces = 0;
    blackPieces = 0;
    allPieces = 0;
    knights[WHITE] = &whiteKnights;
    knights[BLACK] = &blackKnights;
    kings[WHITE] = &whiteKings;
    kings[BLACK] = &blackKings;
    queens[WHITE] = &whiteQueens;
    queens[BLACK] = &blackQueens;
    rooks[WHITE] = &whiteRooks;
    rooks[BLACK] = &blackRooks;
    bishops[WHITE] = &whiteBishops;
    bishops[BLACK] = &blackBishops;
    pawns[WHITE] = &whitePawns;
    pawns[BLACK] = &blackPawns;
    boards[WPAWN] = &whitePawns;
    boards[WKNIGHT] = &whiteKnights;
    boards[WBISHOP] = &whiteBishops;
    boards[WROOK] = &whiteRooks;
    boards[WQUEEN] = &whiteQueens;
    boards[WKING] = &whiteKings;
    boards[BPAWN] = &blackPawns;
    boards[BKNIGHT] = &blackKnights;
    boards[BBISHOP] = &blackBishops;
    boards[BROOK] = &blackRooks;
    boards[BQUEEN] = &blackQueens;
    boards[BKING] = &blackKings;
    boards[WPIECES] = &whitePieces;
    boards[BPIECES] = &blackPieces;
    boards[ALLPIECES] = &allPieces;
    memset(Matrix, 0, sizeof (Matrix));
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
    assert(piece == Matrix[ssq]);
    assert(currentPly < MAX_PLY);

    (stack + 1)->copy(stack);
    stack++;
    currentPly++;

    HASH_EP(stack->hashCode, stack->epsq); //remove (a possible) ep square from hashcode

    if (capture || promotion) {
        if (capture) {
            assert(capture != WKING && capture != BKING);
            if (move->en_passant) {
                assert(move->piece == WPAWN || move->piece == BPAWN);
                assert(Matrix[tsq] == EMPTY);
                assert(stack->ep_square == tsq);
                assert(tsq >= a6 ? Matrix[tsq - 8] == BPAWN : Matrix[tsq + 8] == WPAWN);
                removePieceFull(capture, tsq >= a6 ? tsq - 8 : tsq + 8);
            } else {
                assert(capture == Matrix[tsq]);
                removePieceFull(capture, tsq);
            }
        }
        if (promotion) {
            removePieceFull(piece, ssq);
            addPieceFull(promotion, tsq);
        } else {
            movePieceFull(piece, ssq, tsq);
        }
        stack->epsq = EMPTY;
        stack->fiftyCount = 0;
    } else { //not a capture or promotion
        assert(Matrix[tsq] == EMPTY);
        movePieceFull(piece, ssq, tsq);
        stack->fiftyCount++;
        stack->epsq = EMPTY;
        if (piece == WPAWN) {
            if (ssq <= h2 && tsq == ssq + 16) {
                stack->epsq = tsq - 8;
                HASH_EP(stack->hashCode, stack->epsq);
            }
            stack->fiftyCount = 0;
        } else if (piece == BPAWN) {
            if (ssq <= h7 && tsq == ssq - 16) {
                stack->epsq = tsq + 8;
                HASH_EP(stack->hashCode, stack->epsq);
            }
            stack->fiftyCount = 0;
        }
    }

    if (stack->castlingFlags) {
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
            stack->castlingFlags ^= CASTLE_K;
            HASH_CASTLE_K(stack->hashCode);
            HASH_CASTLE_K(stack->pawnHash);
        }
        if (castleRight(CASTLE_Q) && (ssq == a1 || ssq == e1 || tsq == a1)) {
            stack->castlingFlags ^= CASTLE_Q;
            HASH_CASTLE_Q(stack->hashCode);
            HASH_CASTLE_Q(stack->pawnHash);
        }
        if (castleRight(CASTLE_k) && (ssq == h8 || ssq == e8 || tsq == h8)) {
            stack->castlingFlags ^= CASTLE_k;
            HASH_CASTLE_k(stack->hashCode);
            HASH_CASTLE_k(stack->pawnHash);
        }
        if (castleRight(CASTLE_q) && (ssq == a8 || ssq == e8 || tsq == a8)) {
            stack->castlingFlags ^= CASTLE_q;
            HASH_CASTLE_q(stack->hashCode);
            HASH_CASTLE_q(stack->pawnHash);
        }

    }

    // update flags and hashcode for the side to move
    stack->WTM = !stack->WTM;
    HASH_STM(stack->hashCode);

    assert(Matrix[*whiteKingPos] == WKING && Matrix[*blackKingPos] == BKING);
    assert(piece == Matrix[tsq] || (promotion && promotion == Matrix[tsq]));
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
    int piece = Matrix[tsq];

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
    currentPly--;
    stack--;
}

/**
 * Do a nullmove: update board structure, e.g. switch side to move
 */
void TBoard::forward() { //do a null move
    (stack + 1)->copy(stack);
    stack++;
    currentPly++;
    HASH_EP(stack->hashCode, stack->epsq); //remove epsquare if it is set
    stack->epsq = EMPTY;
    stack->WTM = !stack->WTM;
    HASH_STM(stack->hashCode);
}

/**
 * Undo a nullmove: restore board structure
 */
void TBoard::backward() { //undo a null move
    currentPly--;
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
    if (stack->WTM != piece <= WKING) {
        return false;
    }
    int ssq = move->ssq;
    if (Matrix[ssq] != piece) {
        return false;
    }
    int tsq = move->tsq;
    if (move->en_passant) {
        return tsq == stack->epsq && Matrix[tsq] == EMPTY && ((stack->WTM && piece == WPAWN && Matrix[tsq - 8] == BPAWN)
                || (!stack->WTM && piece == BPAWN && Matrix[tsq + 8] == WPAWN));
    } else if (move->castle) {
        int castle = move->castle;
        if (!castleRight(castle)) {
            return false;
        }
        switch (castle) {
            case EMPTY:
                break;
            case CASTLE_K:
                return Matrix[h1] == WROOK && Matrix[f1] == EMPTY && Matrix[g1] == EMPTY
                        && !attackedByBlack(f1) && !attackedByBlack(g1) && !attackedByBlack(e1);
            case CASTLE_Q:
                return Matrix[a1] == WROOK && Matrix[b1] == EMPTY && Matrix[c1] == EMPTY && Matrix[d1] == EMPTY
                        && !attackedByBlack(e1) && !attackedByBlack(d1) && !attackedByBlack(c1);
            case CASTLE_k:
                return Matrix[h8] == BROOK && Matrix[f8] == EMPTY && Matrix[g8] == EMPTY
                        && !attackedByWhite(f8) && !attackedByWhite(g8) && !attackedByWhite(e8);
            case CASTLE_q:
                return Matrix[a8] == BROOK && Matrix[b8] == EMPTY && Matrix[c8] == EMPTY && Matrix[d8] == EMPTY
                        && !attackedByWhite(e8) && !attackedByWhite(d8) && !attackedByWhite(c8);
            default:
                assert(false);
        }
    }
    int captured = move->capture; //empty for non-captures
    if (Matrix[tsq] != captured) {
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
            return captured || tsq - ssq == 8 || Matrix[ssq + 8] == EMPTY;
        case WKNIGHT:
            return true;
        case WBISHOP:
            return MagicBishopMoves(ssq, allPieces) & BIT(tsq);
        case WROOK:
            return MagicRookMoves(ssq, allPieces) & BIT(tsq);
        case WQUEEN:
            return MagicQueenMoves(ssq, allPieces) & BIT(tsq);
        case WKING:
            return true;
        case BPAWN:
            return captured || ssq - tsq == 8 || Matrix[tsq + 8] == EMPTY;
        case BKNIGHT:
            return true;
        case BBISHOP:
            return MagicBishopMoves(ssq, allPieces) & BIT(tsq);
        case BROOK:
            return MagicRookMoves(ssq, allPieces) & BIT(tsq);
        case BQUEEN:
            return MagicQueenMoves(ssq, allPieces) & BIT(tsq);
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
    U64 tsqBoard = BIT(tsq);
    U64 occupied = allPieces & ~tsqBoard;
    if (stack->WTM) {
        move->en_passant = stack->epsq && tsq == stack->epsq && piece == WPAWN;
        if (move->en_passant) {
            occupied ^= tsqBoard >> 8;
        }

        //1. check for direct checks on our king (non-sliders or contact checks)
        int kposList[2] = {*whiteKingPos, tsq};
        int kpos = kposList[piece == WKING];

        if ((blackKnights & occupied & KnightMoves[kpos])
                || (blackPawns & occupied & WPawnCaptures[kpos])
                || (piece == WKING && KingMoves[kpos] & blackKings)) {
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

        //2. update the occupied bitboard and check for sliding attacks on the king
        //   which can happen when we move a piece which was pinned, or move the king
        //   on the same path as a sliding attack.
        //   note we deliberately dont update occupied for rooks for castling moves
        //   but en-passant update is required 
        U64 oppSliders = (blackBishops | blackRooks | blackQueens) & ~tsqBoard;
        if (QueenMoves[kpos] & oppSliders) {
            U64 oppDiagSliders = oppSliders & ~blackRooks;
            U64 oppHVSliders = oppSliders & ~blackBishops;
            occupied ^= BIT(move->ssq);
            occupied |= tsqBoard;
            return !(MagicBishopMoves(kpos, occupied) & oppDiagSliders)
                    && !(MagicRookMoves(kpos, occupied) & oppHVSliders);
        }
    } else {
        move->en_passant = stack->epsq && tsq == stack->epsq && piece == BPAWN;
        if (move->en_passant) {
            occupied ^= tsqBoard << 8;
        }

        //1. check for direct checks on our king (non-sliders or contact checks)
        int kposList[2] = {*blackKingPos, tsq};
        int kpos = kposList[piece == BKING];

        if ((whiteKnights & occupied & KnightMoves[kpos])
                || (whitePawns & occupied & BPawnCaptures[kpos])
                || (piece == BKING && KingMoves[kpos] & whiteKings)) {
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

        //2. update the occupied bitboard and check for sliding attacks on the king
        //   which can happen when we move a piece which was pinned, or move the king
        //   on the same path as a sliding attack.
        //   note we deliberately dont update occupied for rooks for castling moves
        //   but en-passant update is required 


        U64 oppSliders = (whiteBishops | whiteRooks | whiteQueens) & ~tsqBoard;
        if (QueenMoves[kpos] & oppSliders) {
            U64 oppDiagSliders = oppSliders & ~whiteRooks;
            U64 oppHVSliders = oppSliders & ~whiteBishops;
            occupied ^= BIT(move->ssq);
            occupied |= tsqBoard;
            return !(MagicBishopMoves(kpos, occupied) & oppDiagSliders)
                    && !(MagicRookMoves(kpos, occupied) & oppHVSliders);
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
    U64 ssqBB = BIT(ssq);
    U64 tsqBB = BIT(tsq);
    U64 checkers = 0;
    int kposList[2] = {*whiteKingPos, *blackKingPos};
    int kpos = kposList[piece <= WKING];
    U64 checkMask = QueenMoves[kpos] | KnightMoves[kpos];
    if ((checkMask & (ssqBB | tsqBB)) == 0 && !move->castle && !move->en_passant) {
        return 0;
    }

    //is it a direct check?
    if ((checkMask & tsqBB) || move->castle) {
        switch (piece) {
            case EMPTY:
                assert(false);
            case WPAWN:
                checkers = BPawnCaptures[kpos] & tsqBB;
                break;
            case WKNIGHT:
                checkers = KnightMoves[kpos] & tsqBB;
                break;
            case WBISHOP:
                if (BishopMoves[kpos] & tsqBB) {
                    checkers = MagicBishopMoves(kpos, allPieces) & tsqBB;
                }
                break;
            case WROOK:
                if (RookMoves[kpos] & tsqBB) {
                    checkers = MagicRookMoves(kpos, allPieces) & tsqBB;
                }
                break;
            case WQUEEN:
                if (QueenMoves[kpos] & tsqBB) {
                    checkers = MagicQueenMoves(kpos, allPieces) & tsqBB;
                }
            case WKING:
                if (move->castle) {
                    if (move->castle == CASTLE_K) {
                        checkers = MagicRookMoves(kpos, allPieces^BIT(ssq)) & BIT(f1);
                    } else {
                        checkers = MagicRookMoves(kpos, allPieces^BIT(ssq)) & BIT(d1);
                    }
                }
                break;
            case BPAWN:
                checkers = WPawnCaptures[kpos] & tsqBB;
                break;
            case BKNIGHT:
                checkers = KnightMoves[kpos] & tsqBB;
                break;
            case BBISHOP:
                if (BishopMoves[kpos] & tsqBB) {
                    checkers = MagicBishopMoves(kpos, allPieces) & tsqBB;
                }
                break;
            case BROOK:
                if (RookMoves[kpos] & tsqBB) {
                    checkers = MagicRookMoves(kpos, allPieces) & tsqBB;
                }
                break;
            case BQUEEN:
                if (QueenMoves[kpos] & tsqBB) {
                    checkers = MagicQueenMoves(kpos, allPieces) & tsqBB;
                }
            case BKING:
                if (move->castle) {
                    if (move->castle == CASTLE_k) {
                        checkers = MagicRookMoves(kpos, allPieces^BIT(ssq)) & BIT(f8);
                    } else {
                        checkers = MagicRookMoves(kpos, allPieces^BIT(ssq)) & BIT(d8);
                    }
                }
                break;
            default: assert(false);
        }
    }
    if (checkers) {
        (stack + 1)->checkers = checkers;
        (stack + 1)->checkerSq = tsq;
        return 1;
    }

    //is it an exposed check?
    if ((checkMask & ssqBB) || move->en_passant) {
        U64 sliders = piece <= WKING ?
                whiteBishops | whiteQueens | whiteRooks
                :
                blackBishops | blackQueens | blackRooks;

        U64 exclude = ssqBB | tsqBB;
        sliders &= ~exclude;
        sliders &= QueenMoves[kpos];
        if (sliders) {
            U64 occ = (allPieces ^ ssqBB) | tsqBB;
            if (move->en_passant) {
                occ ^= BIT(tsq + (kpos == *whiteKingPos ? 8 : -8));
            }
            U64 diag = MagicBishopMoves(kpos, occ);
            U64 slidersDiag = sliders & ~(whiteRooks | blackRooks);
            checkers = diag & slidersDiag;
            if (checkers) {
                (stack + 1)->checkers = checkers;
                (stack + 1)->checkerSq = BSF(checkers);
                return 2;
            }
            U64 horVer = MagicRookMoves(kpos, occ);
            sliders &= ~(whiteBishops | blackBishops);
            checkers = horVer & sliders;
            if (checkers) {
                (stack + 1)->checkers = checkers;
                (stack + 1)->checkerSq = BSF(checkers);
                return 2;
            }
        }
    }

    //is it a check by promotion?
    if (move->promotion && (checkMask & tsqBB)) {
        piece = move->promotion;
        if (piece == WKNIGHT) {
            checkers = KnightMoves[kpos] & tsqBB;
        } else if (piece == BKNIGHT) {
            checkers = KnightMoves[kpos] & tsqBB;
        } else {
            U64 checkMask = QueenMoves[kpos];
            if (checkMask & tsqBB) {
                U64 occ = allPieces ^ ssqBB;
                switch (piece) {
                    case EMPTY:
                    case WPAWN:
                    case WKNIGHT:
                        assert(false);
                    case WBISHOP:
                        checkers = MagicBishopMoves(kpos, occ) & tsqBB;
                        break;
                    case WROOK:
                        checkers = MagicRookMoves(kpos, occ) & tsqBB;
                        break;
                    case WQUEEN:
                        checkers = MagicQueenMoves(kpos, occ) & tsqBB;
                        break;
                    case WKING:
                    case BPAWN:
                    case BKNIGHT:
                        assert(false);
                    case BBISHOP:
                        checkers = MagicBishopMoves(kpos, occ) & tsqBB;
                        break;
                    case BROOK:
                        checkers = MagicRookMoves(kpos, occ) & tsqBB;
                        break;
                    case BQUEEN:
                        checkers = MagicQueenMoves(kpos, occ) & tsqBB;
                        break;
                    case BKING:
                        assert(false);
                }
            }
        }
    }
    if (checkers) {
        (stack + 1)->checkers = checkers;
        (stack + 1)->checkerSq = tsq;
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
U64 TBoard::getSmallestAttacker(U64 attacks, bool wtm, int& piece) {
    int firstPiece = PAWN[wtm];
    int lastPiece = firstPiece + (WKING - WPAWN);
    for (piece = firstPiece; piece <= lastPiece; piece++) {
        U64 subset = attacks & *boards[piece];
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
    int capturedPiece = move->capture;
    int movingPiece = move->piece;
    int capturedVal = PIECE_VALUE[capturedPiece];

    /*
     * 0. If the king captures, it's always a gain
     */
    if (movingPiece == WKING || movingPiece == BKING) {
        return capturedVal;
    }

    /*
     * 1. if a lower valued piece captures a higher valued piece 
     * return quickly as we are (almost) sure this capture gains material.
     * (e.g. pawn x knight)
     */
    int pieceVal = PIECE_VALUE[movingPiece];
    if (pieceVal < capturedVal) {
        return capturedVal - pieceVal;
    }

    /*
     * 2. if a piece (not pawn) captures a pawn and that pawn is defended by
     * another pawn, return a negative score quickly
     */
    int tsq = move->tsq;
    if ((movingPiece > BPAWN && capturedPiece == WPAWN && BPawnCaptures[tsq] & whitePawns)
            || (movingPiece < WKING && movingPiece > WPAWN && capturedPiece == BPAWN && WPawnCaptures[tsq] & blackPawns)) {
        return capturedVal - pieceVal;
    }

    /*
     * 3. full static exchange evaluation using the swap algoritm
     */
    bool wtm = movingPiece > WKING;
    U64 attacks = attacksTo(tsq);
    int gain[32];
    int depth = 0;
    U64 fromBit = BIT(move->ssq);
    U64 occupied = *boards[ALLPIECES];
    const U64 diagSliders = whiteBishops | whiteQueens | blackBishops | blackQueens;
    const U64 horVerSliders = whiteRooks | whiteQueens | blackRooks | blackQueens;
    const U64 xrays = diagSliders | horVerSliders;

    if (!capturedPiece && (movingPiece == WPAWN || movingPiece == BPAWN)) {
        //set the non-capturing pawn move ssq bit in the attacks bitboard, 
        //so it will be removed again in the swap loop
        attacks ^= fromBit;
    }

    gain[0] = capturedVal;
    do {
        depth++;
        gain[depth] = PIECE_VALUE[movingPiece] - gain[depth - 1];
        attacks ^= fromBit;
        occupied ^= fromBit;
        if (fromBit & xrays) {
            attacks |= MagicBishopMoves(tsq, occupied) & occupied & diagSliders;
            attacks |= MagicRookMoves(tsq, occupied) & occupied & horVerSliders;
        }
        fromBit = getSmallestAttacker(attacks, wtm, movingPiece);
        wtm = !wtm;
    } while (fromBit);
    while (--depth) {
        gain[depth - 1] = -MAX(-gain[depth - 1], gain[depth]);
    }
    return gain[0];
}

/**
 * Verify it the position on the board is a trivial, theoretical draw
 * @return true: it's a draw; false: not a trivial draw
 */
bool TBoard::isDraw() {
    if (whitePawns || blackPawns
            || whiteRooks || blackRooks
            || whiteQueens || blackQueens
            || (whiteKnights && whiteBishops) || (blackKnights && blackBishops)
            || gt_1(whiteBishops) || gt_1(blackBishops)) {
        return false; //not a draw.. there is mating material on the board
    }
    int wN = pieces[WKNIGHT].count;
    if (wN > 2) { //3 knights, exotic!
        return false;
    }
    int bN = pieces[BKNIGHT].count;
    if (bN > 2) {
        return false;
    }

    //at this point a side can have: a) no pieces, b) 1 knight, c) 2 knights or d) 1 bishop.
    int wB = pieces[WBISHOP].count;
    int bB = pieces[BBISHOP].count;
    assert(wN <= 2 && wB <= 1);
    assert(bN <= 2 && bB <= 1);
    assert(!(wB >= 1 && wN >= 1));
    assert(!(bB >= 1 && bN >= 1));

    int wminors = wB + wN;
    int bminors = bB + bN;
    if ((wminors == 0 && bminors == 1)
            || (bminors == 0 && wminors == 1)) {
        return true; //simple case of 1 minor vs no minors (KBK or KNK)
    }

    //for other cases, mates are only possible with a king on the edge
    if ((whiteKings & EDGE) || (blackKings & EDGE)) {
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
    bool castleDash = true;
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
                    stack->epsq = (char((fen[i] - '1')*8 + (fen[i - 1] - 'a')));
                    HASH_EP(stack->hashCode, stack->epsq);
                }
                i += 2;
                goto half_move;
                break;
            case 'k':
                castleDash = false;
                stack->castlingFlags |= CASTLE_k;
                HASH_CASTLE_k(stack->hashCode);
                HASH_CASTLE_k(stack->pawnHash);
                break;
            case 'q':
                castleDash = false;
                stack->castlingFlags |= CASTLE_q;
                HASH_CASTLE_q(stack->hashCode);
                HASH_CASTLE_q(stack->pawnHash);
                break;
            case 'K':
                castleDash = false;
                stack->castlingFlags |= CASTLE_K;
                HASH_CASTLE_K(stack->hashCode);
                HASH_CASTLE_K(stack->pawnHash);
                break;
            case 'Q':
                castleDash = false;
                stack->castlingFlags |= CASTLE_Q;
                HASH_CASTLE_Q(stack->hashCode);
                HASH_CASTLE_Q(stack->pawnHash);
                break;
            case ' ':
                break;
            case '-':
                if (castleDash) {
                    castleDash = false;
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
    int movenumber = 0;
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
                do_half_move ? half_move = half_move * 10 + (c - '0') : movenumber = movenumber * 10 + (c - '0');
                break;
            case ' ':
                do_half_move = false;
            default:
                break;
        }
    }

    stack->fiftyCount = half_move;
    rootPly = movenumber * 2;
    if (wtm) {
        stack->WTM = true;
    } else {
        stack->WTM = false;
        HASH_STM(stack->hashCode);
        rootPly++;
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
            int pc = Matrix[sq];
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
    result += stack->WTM ? " w " : " b ";
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

    if (stack->epsq) {
        result += FILE_SYMBOL(stack->epsq);
        result += RANK_SYMBOL(stack->epsq);
    } else {
        result += "-";
    }
    result += " ";
    sprintf(buf, "%d", stack->fiftyCount);
    result += buf;
    result += " ";
    sprintf(buf, "%d", getGamePly() / 2);
    result += buf;
    return result;
}