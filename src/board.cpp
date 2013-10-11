/**
 * board.cpp
 * 
 * Board structure implementation
 */

#include <cstdlib>
#include <stdio.h>
#include <iostream>
#include <string.h>
#include "board.h"
#include "hashcodes.h"
#include "evaluate.h"

void TBoard::clear() {
    _boardFlags[0].clear();
    boardFlags = &_boardFlags[0];
    currentPly = 0;
    rootPly = 0;
    whiteKingPos = &pieces[WKING].squares[0];
    blackKingPos = &pieces[BKING].squares[0];
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

void TBoard::clearPieceSquareTable() {
    memset(pieceSquareTable, 0, sizeof(pieceSquareTable));
}

void TBoard::setPieceSquareTable(const TSCORE_PCT & pct) {
    for (int pc = 0; pc <= BKING; pc++) {
        for (int sq = 0; sq < 64; sq++) {
            pieceSquareTable[pc][sq].set(pct[pc][sq]);
        }
    }
}

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

    (boardFlags + 1)->copy(boardFlags);
    boardFlags++;
    currentPly++;

    HASH_EP(boardFlags->hashCode, boardFlags->epSquare); //remove (a possible) ep square from hashcode

    if (capture || promotion) {
        if (capture) {
            assert(capture != WKING && capture != BKING);
            if (move->en_passant) {
                assert(move->piece == WPAWN || move->piece == BPAWN);
                assert(Matrix[tsq] == EMPTY);
                assert(boardFlags->epSquare == tsq);
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
        boardFlags->epSquare = EMPTY;
        boardFlags->fiftyCount = 0;
    } else { //not a capture or promotion
        assert(Matrix[tsq] == EMPTY);
        movePieceFull(piece, ssq, tsq);
        boardFlags->fiftyCount++;
        boardFlags->epSquare = EMPTY;
        if (piece == WPAWN) {
            if (ssq <= h2 && tsq == ssq + 16) {
                boardFlags->epSquare = tsq - 8;
                HASH_EP(boardFlags->hashCode, boardFlags->epSquare);
            }
            boardFlags->fiftyCount = 0;
        } else if (piece == BPAWN) {
            if (ssq <= h7 && tsq == ssq - 16) {
                boardFlags->epSquare = tsq + 8;
                HASH_EP(boardFlags->hashCode, boardFlags->epSquare);
            }
            boardFlags->fiftyCount = 0;
        }
    }

    if (boardFlags->castlingFlags) {
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
            boardFlags->castlingFlags ^= CASTLE_K;
            HASH_CASTLE_K(boardFlags->hashCode);
            HASH_CASTLE_K(boardFlags->pawnHash);
        }
        if (castleRight(CASTLE_Q) && (ssq == a1 || ssq == e1 || tsq == a1)) {
            boardFlags->castlingFlags ^= CASTLE_Q;
            HASH_CASTLE_Q(boardFlags->hashCode);
            HASH_CASTLE_Q(boardFlags->pawnHash);
        }
        if (castleRight(CASTLE_k) && (ssq == h8 || ssq == e8 || tsq == h8)) {
            boardFlags->castlingFlags ^= CASTLE_k;
            HASH_CASTLE_k(boardFlags->hashCode);
            HASH_CASTLE_k(boardFlags->pawnHash);
        }
        if (castleRight(CASTLE_q) && (ssq == a8 || ssq == e8 || tsq == a8)) {
            boardFlags->castlingFlags ^= CASTLE_q;
            HASH_CASTLE_q(boardFlags->hashCode);
            HASH_CASTLE_q(boardFlags->pawnHash);
        }

    }

    /* update flags and hashcode for STM */
    boardFlags->WTM = !boardFlags->WTM;
    HASH_STM(boardFlags->hashCode);

    assert(Matrix[*whiteKingPos] == WKING && Matrix[*blackKingPos] == BKING);
    assert(piece == Matrix[tsq] || (promotion && promotion == Matrix[tsq]));
}

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
    boardFlags--;
}

void TBoard::forward() { //do a null move
    (boardFlags + 1)->copy(boardFlags);
    boardFlags++;
    currentPly++;
    HASH_EP(boardFlags->hashCode, boardFlags->epSquare); //remove epsquare if it is set
    boardFlags->epSquare = EMPTY;
    boardFlags->WTM = !boardFlags->WTM;
    HASH_STM(boardFlags->hashCode);
}

void TBoard::backward() { //undo a null move
    currentPly--;
    boardFlags--;
}

/**
 * see if the move is valid in the current board (legality is not checked here)
 * valid means: the move would be generated by the move generator
 * @param move the move to test for validity, not yet performed in the position
 * @return true if the move is valid in the current position, false otherwise 
 */
bool TBoard::valid(TMove * move) {
    int piece = move->piece;
    if (boardFlags->WTM != piece <= WKING) {
        return false;
    }
    int ssq = move->ssq;
    if (Matrix[ssq] != piece) {
        return false;
    }
    int tsq = move->tsq;
    if (move->en_passant) {
        return tsq == boardFlags->epSquare && Matrix[tsq] == EMPTY && ((boardFlags->WTM && piece == WPAWN && Matrix[tsq - 8] == BPAWN)
                || (!boardFlags->WTM && piece == BPAWN && Matrix[tsq + 8] == WPAWN));
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
 * see if the move is legal in the current board (not leaving our king in check)
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
    if (boardFlags->WTM) {
        move->en_passant = boardFlags->epSquare && tsq == boardFlags->epSquare && piece == WPAWN;
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
        move->en_passant = boardFlags->epSquare && tsq == boardFlags->epSquare && piece == BPAWN;
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

    /*
     * Direct Check?
     */
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
        (boardFlags + 1)->checkers = checkers;
        (boardFlags + 1)->checkerSq = tsq;
        return 1;
    }
    /*
     * Exposed check?
     */
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
                (boardFlags + 1)->checkers = checkers;
                (boardFlags + 1)->checkerSq = BSF(checkers);
                return 2;
            }
            U64 horVer = MagicRookMoves(kpos, occ);
            sliders &= ~(whiteBishops | blackBishops);
            checkers = horVer & sliders;
            if (checkers) {
                (boardFlags + 1)->checkers = checkers;
                (boardFlags + 1)->checkerSq = BSF(checkers);
                return 2;
            }
        }
    }
    /*
     * Check by promoting?
     */
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
        (boardFlags + 1)->checkers = checkers;
        (boardFlags + 1)->checkerSq = tsq;
        return 1;
    }
    return 0;
}

bool TBoard::active(TMove * move) {
    if (move->capture || move->promotion || move->castle) {
        return true;
    }
    switch (move->piece) {
        case EMPTY:
            return false;
        case WPAWN:
            return move->tsq >= a4
                    || (WPawnCaptures[move->tsq] & blackPieces);
        case WKNIGHT:
            return KnightMoves[move->tsq] & (blackBishops | blackRooks | blackQueens);
        case WBISHOP:
            return BishopMoves[move->tsq] & (blackRooks | blackQueens | blackKings);
        case WROOK:
            return RookMoves[move->tsq] & (blackQueens | blackKings);
        case WQUEEN:
            return false;
        case WKING:
            return false;
        case BPAWN:
            return move->tsq <= h5
                    || (BPawnCaptures[move->tsq] & whitePieces);
        case BKNIGHT:
            return KnightMoves[move->tsq] & (whiteBishops | whiteRooks | whiteQueens);
        case BBISHOP:
            return BishopMoves[move->tsq] & (whiteRooks | whiteQueens | whiteKings);
        case BROOK:
            return RookMoves[move->tsq] & (whiteQueens | whiteKings);
        case BQUEEN:
            return false;
        case BKING:
            return false;
        default:
            return false;
    }
    return false;
}

bool TBoard::checksPiece(TMove * move) {
    switch (move->piece) {
        case EMPTY:
            return false;
        case WPAWN:
            return WPawnCaptures[move->tsq] & (blackKnights | blackBishops | blackRooks | blackQueens);
        case WKNIGHT:
            return KnightMoves[move->tsq] & (blackBishops | blackRooks | blackQueens);
        case WBISHOP:
            return BishopMoves[move->tsq] & (blackRooks | blackQueens | blackKings);
        case WROOK:
            return RookMoves[move->tsq] & (blackQueens | blackKings);
        case WQUEEN:
            return false;
        case WKING:
            return false;
        case BPAWN:
            return BPawnCaptures[move->tsq] & (whiteKnights | whiteBishops | whiteRooks | whiteQueens);
        case BKNIGHT:
            return KnightMoves[move->tsq] & (whiteBishops | whiteRooks | whiteQueens);
        case BBISHOP:
            return BishopMoves[move->tsq] & (whiteRooks | whiteQueens | whiteKings);
        case BROOK:
            return RookMoves[move->tsq] & (whiteQueens | whiteKings);
        case BQUEEN:
            return false;
        case BKING:
            return false;
        default:
            return false;
    }
    return false;
}

U64 TBoard::getSmallestAttacker(U64 attacks, bool wtm, int& piece) {
    static const int FIRSTPIECE[2] = {BPAWN, WPAWN};
    int firstPiece = FIRSTPIECE[wtm];
    int lastPiece = firstPiece + (WKING - WPAWN);
    for (piece = firstPiece; piece <= lastPiece; piece++) {
        U64 subset = attacks & *boards[piece];
        if (subset) {
            return subset & -subset;
        }
    }
    return 0;
}

int TBoard::SEE(TMove * move) {
    int ssq = move->ssq;
    int tsq = move->tsq;
    int capturedPiece = move->capture;
    int movingPiece = move->piece;
    int pieceVal = PIECE_VALUE[movingPiece];
    int capturedVal = PIECE_VALUE[capturedPiece];

    /*
     * 1. if a lower valued piece captures a higher valued piece 
     * return quickly as we are (almost) sure this capture gains material.
     * (e.g. pawn x knight)
     */
    if (pieceVal < capturedVal) {
        return capturedVal - pieceVal;
    }

    /*
     * 2. if a piece (not pawn) captures a pawn and that pawn is defended by
     * another pawn, return a negative score quickly
     */
    if ((movingPiece > BPAWN && capturedPiece == WPAWN && BPawnCaptures[tsq] & whitePawns)
            || (movingPiece <= WKING && movingPiece > WPAWN && capturedPiece == BPAWN && WPawnCaptures[tsq] & blackPawns)) {
        return capturedVal - pieceVal;
    }

    /*
     * 3. full static exchange evaluation using the swap algoritm
     */
    int gain[32];
    int depth = 0;
    U64 fromBit = BIT(ssq);
    U64 occupied = *boards[ALLPIECES];
    const U64 diagSliders = whiteBishops | whiteQueens | blackBishops | blackQueens;
    const U64 horVerSliders = whiteRooks | whiteQueens | blackRooks | blackQueens;
    const U64 xrays = diagSliders | horVerSliders;
    U64 attacks = attacksTo(tsq);
    if (!capturedPiece && (movingPiece == WPAWN || movingPiece == BPAWN)) {
        //set the non-captureing pawn move ssq bit in the attacks bitboard, 
        //so it will be removed again in the swap loop
        attacks ^= fromBit;
    }
    bool wtm = movingPiece > WKING;
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

bool TBoard::isDraw() {
    bool result = false;
    if (!whitePawns && !blackPawns && !whiteRooks && !blackRooks
            && !whiteQueens && !blackQueens) {
        /* there are only minor pieces left on the board */
        int wbishops = pieces[WBISHOP].count;
        int bbishops = pieces[BBISHOP].count;
        int wknights = pieces[WKNIGHT].count;
        int bknights = pieces[BKNIGHT].count;
        int wminors = wbishops + wknights;
        int bminors = bbishops + bknights;
        result = wminors < 2 && bminors < 2;
    }
    return result;
}

void TBoard::fromFen(const char* fen) {
    /*
     * Initialize
     */
    clear();
    char offset = a8;
    unsigned char pos = a8;
    bool wtm = true;
    int i, end = strlen(fen);
    /*
     * Piece placement
     */
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
    /*
     * Site to move
     */
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
    /*
     * Castling status and en passant square
     */
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
                    boardFlags->epSquare = (char((fen[i] - '1')*8 + (fen[i - 1] - 'a')));
                    HASH_EP(boardFlags->hashCode, boardFlags->epSquare);
                }
                i += 2;
                goto half_move;
                break;
            case 'k':
                castleDash = false;
                boardFlags->castlingFlags |= CASTLE_k;
                HASH_CASTLE_k(boardFlags->hashCode);
                HASH_CASTLE_k(boardFlags->pawnHash);
                break;
            case 'q':
                castleDash = false;
                boardFlags->castlingFlags |= CASTLE_q;
                HASH_CASTLE_q(boardFlags->hashCode);
                HASH_CASTLE_q(boardFlags->pawnHash);
                break;
            case 'K':
                castleDash = false;
                boardFlags->castlingFlags |= CASTLE_K;
                HASH_CASTLE_K(boardFlags->hashCode);
                HASH_CASTLE_K(boardFlags->pawnHash);
                break;
            case 'Q':
                castleDash = false;
                boardFlags->castlingFlags |= CASTLE_Q;
                HASH_CASTLE_Q(boardFlags->hashCode);
                HASH_CASTLE_Q(boardFlags->pawnHash);
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
    /*
     * Half move clock and move number
     */
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

    boardFlags->fiftyCount = half_move;
    rootPly = movenumber * 2;
    if (wtm) {
        boardFlags->WTM = true;
    } else {
        boardFlags->WTM = false;
        HASH_STM(boardFlags->hashCode);
        rootPly++;
    }
}

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
    result += boardFlags->WTM ? " w " : " b ";
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

    if (boardFlags->epSquare) {
        result += FILE_SYMBOL(boardFlags->epSquare);
        result += RANK_SYMBOL(boardFlags->epSquare);
    } else {
        result += "-";
    }
    result += " ";
    sprintf(buf, "%d", boardFlags->fiftyCount);
    result += buf;
    result += " ";
    sprintf(buf, "%d", getGamePly() / 2);
    result += buf;
    return result;
}


