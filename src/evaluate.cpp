#include "evaluate.h"
#include "searchdata.h"
#include "defs.h"

int evaluate(TSearchData * searchData, int alpha, int beta) {

    if (searchData->stack->evaluationScore != SCORE_UNKNOWN) {
        return searchData->stack->evaluationScore;
    }

    int result = 0;
    TBoard * pos = searchData->pos;
    result += evaluateMaterial(searchData);
    result += phasedScore(searchData->stack->gamePhase, pos->boardFlags->pctMG, pos->boardFlags->pctEG);
    result += evaluatePawns(searchData);
    result += evaluateKings(searchData);


    if (searchData->learnParam == 1) {
        //learning
        result += evaluateExp(searchData);
    }

    result &= GRAIN;
    return pos->boardFlags->WTM ? result : -result;
}

/**
 * Experimental evaluation for learning
 * @param searchData search metadata object
 * @return score the evaluation score
 */
int evaluateExp(TSearchData * searchData) {
    int result = 0;
    result *= searchData->learnFactor;
    return result;
}

/**
 * Evaluate material score
 * @param searchData search metadata object
 */
int evaluateMaterial(TSearchData * searchData) {
    
    /*
     * 1. Get the score from the last stack record if the previous move was quiet, 
     *    so the material balance did not change. This is easy to verify with 
     *    the material hash
     */
    if ((searchData->pos->boardFlags-1)->materialHash == searchData->pos->boardFlags->materialHash
            && (searchData->stack-1)->evaluationScore != SCORE_UNKNOWN) {
        searchData->stack->scores[SCORE_MATERIAL] = (searchData->stack-1)->scores[SCORE_MATERIAL];
        return searchData->stack->scores[SCORE_MATERIAL];
    }

    /*
     * 2. Probe the hash table for the material score
     */
    searchData->hashTable->mtLookup(searchData);
    if (searchData->stack->scores[SCORE_MATERIAL] != SCORE_INVALID) {
        return searchData->stack->scores[SCORE_MATERIAL];
    }

    /*
     * 3. Calculate material value and store in material hash table
     */
    int value = 0;
    TBoard * pos = searchData->pos;
    int wpawns = pos->pieces[WPAWN].count;
    int bpawns = pos->pieces[BPAWN].count;
    int wknights = pos->pieces[WKNIGHT].count;
    int bknights = pos->pieces[BKNIGHT].count;
    int wbishops = pos->pieces[WBISHOP].count;
    int bbishops = pos->pieces[BBISHOP].count;
    int wrooks = pos->pieces[WROOK].count;
    int brooks = pos->pieces[BROOK].count;
    int wqueens = pos->pieces[WQUEEN].count;
    int bqueens = pos->pieces[BQUEEN].count;
    int wminors = wknights + wbishops;
    int bminors = bknights + bbishops;
    int wpieces = wminors + wrooks + wqueens;
    int bpieces = bminors + brooks + bqueens;

    if (wknights != bknights) {
        value += VKNIGHT * (wknights - bknights);
        value += factor(wknights, bknights, FKNIGHT_OPPOSING_PAWNS, bpawns, wpawns);
        value += cond(wknights >= 2, bknights >= 2, VKNIGHT_PAIR);
        value += factor(wknights, bknights, KNIGHT_X_PIECECOUNT, wpieces + bpieces);
    }
    if (wbishops != bbishops) {
        value += VBISHOP * (wbishops - bbishops);
        bool wPair = wbishops > 1 && pos->whiteBishopPair(); //note: material hash includes bishop colors
        bool bPair = bbishops > 1 && pos->blackBishopPair();
        value += cond(wPair, bPair, VBISHOPPAIR);
        value += cond(wPair, bPair,
                BISHOPPAIR_MINOR_OPPOSITION, bminors, wminors);
        value += cond(wPair, bPair,
                BISHOPPAIR_OPPOSING_PAWNS, bpawns, wpawns);
        value += factor(wPair, bPair, BISHOPPAIR_X_PIECECOUNT, wpieces + bpieces, wpieces + bpieces);
        value += factor(wbishops && !wPair, bbishops && !bPair, BISHOP_X_PIECECOUNT, wpieces + bpieces, wpieces + bpieces);
        value += cond(wbishops == wpieces && brooks && brooks == bpieces,
                bbishops == bpieces && wrooks && wrooks == wpieces, VBISHOP_VS_ROOK_ENDGAME);
        value += cond(wbishops && !bpieces && bpawns, bbishops && !wpieces && wpawns, VBISHOP_VS_PAWNS_ENDGAME);

    }
    if (wrooks != brooks) {
        value += VROOK * (wrooks - brooks);
        value += cond(wrooks >= 2, brooks >= 2, VROOKPAIR);
        value += factor(wrooks, brooks, ROOK_OPPOSING_PAWNS, bpawns, wpawns);
    }
    if (wqueens != bqueens) {

        value += VQUEEN * (wqueens - bqueens);
        value += cond(wqueens && wrooks, bqueens && brooks, VQUEEN_AND_ROOKS);
        value += factor(wqueens, bqueens, QUEEN_MINORCOUNT, wminors + bminors);
    }

    /*
     * Bonus for having more "piece power"
     */
    //value += cond(wpieces>bpieces, bpieces>wpieces, PIECECOUNT_AHEAD, wpieces - bpieces, bpieces - wpieces);
    int piecePower = value;
    value += cond(wpawns && piecePower > MATERIAL_AHEAD_TRESHOLD,
            bpawns && piecePower < -MATERIAL_AHEAD_TRESHOLD,
            PIECEPOWER_AHEAD,
            piecePower / VPAWN,
            -piecePower / VPAWN);

    if (wpawns != bpawns) {
        int pawnValue = VPAWN * (wpawns - bpawns);
        pawnValue += cond(!wpawns, !bpawns, VNOPAWNS); //penalty for not having pawns (difficult to win)
        value += pawnValue;
    }

    /* 
     * Endgame adjustment: 
     * 1)If ahead, but no pawns and no mating material the score is draw
     */

    if (!wpawns || !bpawns) {
        if (!wpawns && value > 0 && (piecePower < 2 * VPAWN
                || (wpieces == 1 && wminors == 1)
                || (wpieces == 2 && wknights == 2)
                || (wpieces == wminors && bpieces == bminors && wminors < 3 && bminors < 3 && wminors && bminors))) {
            value = SCORE_DRAW;
        }
        if (!bpawns && value < 0 && (piecePower > -2 * VPAWN
                || (bpieces == 1 && bminors == 1)
                || (bpieces == 2 && bknights == 2)
                || (bpieces == bminors && wpieces == wminors && bminors < 3 && wminors < 3 && bminors && wminors))) {
            value = SCORE_DRAW;
        }
    }

    /* 
     * Endgame adjustment: 
     * 2) Rooks and queen endgames are drawish. Reduce any small material advantage.
     */
    if (value
            && piecePower == 0
            && wpieces <= 3
            && wpieces == bpieces
            && wminors < 2
            && bminors < 2
            && ABS(value) < 2 * VPAWN) {
        value -= VPAWN / 5;
    }

    /*
     * Endgame adjustment:
     * Opposite  bishop ending is drawish
     */
    if (value && wpieces == 1 && bpieces == 1 && wbishops && bbishops) {
        if ((pos->whiteBishops & BLACK_SQUARES) != (pos->blackBishops & BLACK_SQUARES)) {
            value >>= 1;
        }
    }

    //trade down bonus: trade pieces but not pawns when ahead
    value += cond(value > MATERIAL_AHEAD_TRESHOLD, value < -MATERIAL_AHEAD_TRESHOLD, TRADEDOWN_PIECES, bpieces, wpieces);
    value += cond(value > MATERIAL_AHEAD_TRESHOLD, value < -MATERIAL_AHEAD_TRESHOLD, TRADEDOWN_PAWNS, wpawns, bpawns);
    /*
     * Calculate game phase. Phases are:
     * - Opening / early middle game: all pieces are on the board
     * - Middlegame: half of the pieces are on the board
     * - Endgame: pawns and one piece on the board
     * - Late endgame: just pawns left.
     * 
     * 
     */
    int phase = MAX_PHASE_SCORE
            - (wminors + bminors) /* max: 8 */
            - (wrooks + brooks) /* max:4 */
            - 2 * (wqueens + bqueens) /* max: 4 */;
    phase = MAX(0, phase);
    phase = (MAX_GAMEPHASES * phase) / MAX_PHASE_SCORE;
    
    /*
     * Store and return
     */
    searchData->stack->scores[SCORE_MATERIAL] = value;
    searchData->stack->gamePhase = phase;
    searchData->hashTable->mtStore(searchData, value, phase);
    return value;
}

void printBB(std::string msg, U64 bb) {
    std::cout << msg;
    for (int sq = 0; sq < 64; sq++) {
        if (FILE(sq) == 0) {
            std::cout << std::endl;
        }
        if (BIT(sq) & bb) {
            std::cout << "x";
        } else {
            std::cout << ".";
        }
    }
    std::cout << std::endl;
}

/**
 * Evaluate pawn  score and king shelter
 * @param searchData search metadata object
 */

int evaluatePawns(TSearchData * searchData) {
    
    
     /*
     * 1. Get the score from the last stack record if the latest move did not
     *    involve any pawns. This is easy to check with the pawn hash 
     */
    if (searchData->pos->boardFlags->pawnHash == (searchData->pos->boardFlags-1)->pawnHash 
            && (searchData->stack-1)->evaluationScore != SCORE_UNKNOWN) {
        searchData->stack->scores[SCORE_PAWNS] = (searchData->stack-1)->scores[SCORE_PAWNS];
        searchData->stack->scores[SCORE_SHELTERW] = (searchData->stack-1)->scores[SCORE_SHELTERW];
        searchData->stack->scores[SCORE_SHELTERB] = (searchData->stack-1)->scores[SCORE_SHELTERB];
        return searchData->stack->scores[SCORE_PAWNS];
    }

    /*
     * 2. Probe the hash table for the pawn score
     */
    searchData->hashTable->ptLookup(searchData);
    if (searchData->stack->scores[SCORE_PAWNS] != SCORE_INVALID) {
        return searchData->stack->scores[SCORE_PAWNS];
    }
    
    /*
     * 3. Calculate pawn evaluation score
     */
    TBoard * pos = searchData->pos;
    int pawnScore = 0;
    int shelterScoreW = 0;
    int shelterScoreB = 0;
    if (true) {
        U64 passers = 0;
        U64 openW = FULL_BOARD;
        U64 openB = FULL_BOARD;
        U64 allPawns = pos->whitePawns | pos->blackPawns;
        TPiecePlacement * wPawns = &pos->pieces[WPAWN];
        int kpos = *pos->whiteKingPos;
        shelterScoreW = SHELTER_KPOS[FLIP_SQUARE(kpos)];
        if (pos->boardFlags->castlingFlags & CASTLE_K
                && ((pos->Matrix[h2] == WPAWN && pos->Matrix[g2] == WPAWN)
                || (pos->Matrix[f2] == WPAWN && pos->Matrix[h2] == WPAWN && pos->Matrix[g3] == WPAWN)
                || (pos->Matrix[h3] == WPAWN && pos->Matrix[g2] == WPAWN && pos->Matrix[f2] == WPAWN))) {
            shelterScoreW += SHELTER_CASTLING_KINGSIDE;
        } else if (pos->boardFlags->castlingFlags & CASTLE_Q
                && ((pos->Matrix[a2] == WPAWN && pos->Matrix[b2] == WPAWN && pos->Matrix[c2] == WPAWN)
                || (pos->Matrix[a2] == WPAWN && pos->Matrix[b3] == WPAWN && pos->Matrix[c2] == WPAWN))) {
            shelterScoreW += SHELTER_CASTLING_QUEENSIDE;
        }
        U64 kingFront = FORWARD_RANKS[RANK(kpos)] & PAWN_SCOPE[FILE(kpos)];
        for (int i = 0; i < wPawns->count; i++) {
            int sq = wPawns->squares[i];
            int file = FILE(sq);
            int rank = RANK(sq);
            U64 sqBit = BIT(sq);
            U64 myFile = FILES[file];
            U64 myScope = PAWN_SCOPE[file];
            U64 besideMe = NEIGHBOUR_FILES[file];
            U64 beforeMe = FORWARD_RANKS[rank] & myScope;
            bool isolated = !(besideMe & pos->whitePawns);
            bool doubled = myFile & beforeMe & pos->whitePawns;
            bool open = !(myFile & beforeMe & allPawns);
            bool passed = !doubled && !(beforeMe & pos->blackPawns);
            int tSq = FLIP_SQUARE(sq);
            if (!open) {
                openB &= ~myFile;
            }
            if (isolated) {
                if (open) {
                    pawnScore += ISOLATED_OPEN_PAWN[tSq];
                } else {
                    pawnScore += ISOLATED_PAWN[tSq];
                }
            }
            if (doubled) {
                pawnScore += DOUBLED_PAWN[tSq];
            }
            if (passed) {
                pawnScore += PASSED_PAWN[tSq];
                passers |= sqBit;
            }
            if (kingFront & sqBit) {
                shelterScoreW += SHELTER_PAWN[tSq];
            }
        }
        U64 kingOpenFiles = kingFront & openB;
        if (kingOpenFiles) {
            kingOpenFiles &= RANK_8;
            shelterScoreW += SHELTER_OPEN_FILES[popCount(kingOpenFiles)];
        }
        TPiecePlacement * bPawns = &pos->pieces[BPAWN];
        kpos = *pos->blackKingPos;
        shelterScoreB = SHELTER_KPOS[kpos];
        if (pos->boardFlags->castlingFlags & CASTLE_k
                && ((pos->Matrix[h7] == BPAWN && pos->Matrix[g7] == BPAWN)
                || (pos->Matrix[f7] == BPAWN && pos->Matrix[h7] == BPAWN && pos->Matrix[g6] == BPAWN)
                || (pos->Matrix[h6] == BPAWN && pos->Matrix[g7] == BPAWN && pos->Matrix[f7] == BPAWN))) {
            shelterScoreB += SHELTER_CASTLING_KINGSIDE;
        } else if (pos->boardFlags->castlingFlags & CASTLE_q
                && ((pos->Matrix[a7] == BPAWN && pos->Matrix[b7] == BPAWN && pos->Matrix[c7] == BPAWN)
                || (pos->Matrix[a7] == BPAWN && pos->Matrix[b6] == BPAWN && pos->Matrix[c7] == BPAWN))) {
            shelterScoreB += SHELTER_CASTLING_QUEENSIDE;
        }
        kingFront = BACKWARD_RANKS[RANK(kpos)] & PAWN_SCOPE[FILE(kpos)];
        for (int i = 0; i < bPawns->count; i++) {
            int sq = bPawns->squares[i];
            int file = FILE(sq);
            int rank = RANK(sq);
            U64 myFile = FILES[file];
            U64 myScope = PAWN_SCOPE[file];
            U64 besideMe = NEIGHBOUR_FILES[file];
            U64 beforeMe = BACKWARD_RANKS[rank] & myScope;
            U64 sqBit = BIT(sq);
            bool isolated = !(besideMe & pos->blackPawns);
            bool doubled = myFile & beforeMe & pos->blackPawns;
            bool open = !(myFile & beforeMe & allPawns);
            bool passed = !doubled && !(beforeMe & pos->whitePawns);
            int tSq = sq;
            if (!open) {
                openW &= ~myFile;
            }
            if (isolated) {
                if (open) {
                    pawnScore -= ISOLATED_OPEN_PAWN[tSq];
                } else {
                    pawnScore -= ISOLATED_PAWN[tSq];
                }
            }
            if (doubled) {
                pawnScore -= DOUBLED_PAWN[tSq];
            }
            if (passed) {
                pawnScore -= PASSED_PAWN[tSq];
                passers |= sqBit;
            }
            if (kingFront & sqBit) {
                shelterScoreB += SHELTER_PAWN[tSq];
            }
        }
        kingOpenFiles = kingFront & openW;
        if (kingOpenFiles) {
            kingOpenFiles &= RANK_1;
            shelterScoreB += SHELTER_OPEN_FILES[popCount(kingOpenFiles)];
        }
        if (passers) {
            U64 passersW = passers & pos->whitePawns;
            while (passersW) {
                int sq = POP(passersW);
                U64 mask = passers & pos->whitePawns & KingMoves[sq] & NEIGHBOUR_FILES[FILE(sq)];
                if (mask) {
                    pawnScore += CONNECED_PASSED_PAWN[FLIP_SQUARE(sq)];

                }
            }
            U64 passersB = passers & pos->blackPawns;
            while (passersB) {
                int sq = POP(passersB);
                U64 mask = passers & pos->blackPawns & KingMoves[sq] & NEIGHBOUR_FILES[FILE(sq)];
                if (mask) {
                    pawnScore -= CONNECED_PASSED_PAWN[sq];
                }
            }
        }
    }
    searchData->stack->scores[SCORE_PAWNS] = pawnScore;
    searchData->stack->scores[SCORE_SHELTERW] = shelterScoreW;
    searchData->stack->scores[SCORE_SHELTERB] = shelterScoreB;
    searchData->hashTable->ptStore(searchData, pawnScore, shelterScoreW, shelterScoreB);
    return pawnScore;
}

int evaluateKings(TSearchData * searchData) {
    int score = 0;
    TBoard * pos = searchData->pos;
    if (searchData->stack->scores[SCORE_SHELTERW] && pos->blackQueens) {
        score += phasedScore(searchData->stack->gamePhase, 2*searchData->stack->scores[SCORE_SHELTERW], 0);
    }
    if (searchData->stack->scores[SCORE_SHELTERB] && pos->whiteQueens) {
        score -= phasedScore(searchData->stack->gamePhase, 2*searchData->stack->scores[SCORE_SHELTERB], 0);
    }
    searchData->stack->scores[SCORE_KINGS] = score;

    return score;
}

