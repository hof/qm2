#include "evaluate.h"
#include "search.h"
#include "defs.h"
#include "score.h"

inline short evaluateMaterial(TSearch * sd);

inline TScore * evaluatePawns(TSearch * sd);
inline TScore * evaluateBishops(TSearch * sd);
inline TScore * evaluateRooks(TSearch * sd);
inline TScore * evaluateQueens(TSearch * sd);
inline TScore * evaluateExp(TSearch * sd);

int evaluate(TSearch * sd, int alpha, int beta) {
    if (sd->stack->eval_result != SCORE_INVALID) {
        return sd->stack->eval_result;
    }

    sd->stack->equal_pawns = sd->pos->currentPly > 0
            && sd->pos->boardFlags->pawnHash == (sd->pos->boardFlags - 1)->pawnHash
            && (sd->stack - 1)->eval_result != SCORE_INVALID;

    int result = evaluateMaterial(sd); //sets stack->phase
    TScore * score = &sd->stack->eval_score;
    TBoard * pos = sd->pos;
    score->set(sd->pos->boardFlags->pct);

    sd->stack->occ = pos->pawnsAndKings();
    sd->stack->fill[WHITE] = FILEFILL(pos->whitePawns);
    sd->stack->fill[BLACK] = FILEFILL(pos->blackPawns);
    sd->stack->mobMask[WHITE] = ~(pos->whitePawns | pos->whiteKings | pos->blackPawnAttacks());
    sd->stack->mobMask[BLACK] = ~(pos->blackPawns | pos->blackKings | pos->whitePawnAttacks());

    score->add(evaluatePawns(sd));
    score->add(evaluateBishops(sd));
    score->add(evaluateRooks(sd));
    score->add(evaluateQueens(sd));
    result += score->get(sd->stack->phase);
    result &= GRAIN;
    result = sd->pos->boardFlags->WTM ? result : -result;
    sd->stack->eval_result = result;

    assert(result > -VKING && result < VKING);

    return result;
}

/**
 * Experimental evaluation for learning
 * @param sd search meta-data object
 * @return score the evaluation score
 */


TScore * evaluateExp(TSearch * sd) {
    TScore * result = &sd->stack->exp_score;
    result->clear();
    return result;
}

bool skipExp(TSearch * sd) {
    int pc = WQUEEN;
    return sd->pos->pieces[pc].count == 0 && sd->pos->pieces[pc + WKING].count == 0;
}


const short TRADEDOWN_PIECES[MAX_PIECES + 1] = {
    80, 60, 50, 40, 30, 20, 10, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0
};

const short TRADEDOWN_PAWNS[9] = {
    -40, -20, -10, -5, 0, 4, 8, 16, 24
};

const short PIECEPOWER_AHEAD[] = {//in amount of pawns
    0, 10, 30, 50, 100, 125, 150, 175, 200, 225, 250, 250, 250, 250, 250, 250, 250,
    250, 250, 250, 250, 250, 250, 250, 250, 250, 250, 250, 250, 250, 250, 250, 250, 250
};

/**
 * Evaluate material score and set the current game phase
 * @param sd search meta-data object
 */
inline short evaluateMaterial(TSearch * sd) {

    /*
     * 1. Get the score from the last stack record if the previous move was quiet, 
     *    so the material balance did not change. This is easy to verify with 
     *    the material hash
     */
    if (sd->pos->currentPly > 0 &&
            (sd->pos->boardFlags - 1)->materialHash == sd->pos->boardFlags->materialHash
            && (sd->stack - 1)->eval_result != SCORE_INVALID) {
        sd->stack->material_score = (sd->stack - 1)->material_score;
        sd->stack->phase = (sd->stack - 1)->phase;
        return sd->stack->material_score;
    }

    /*
     * 2. Probe the hash table for the material score
     */
    sd->hashTable->mtLookup(sd);
    if (sd->stack->material_score != SCORE_INVALID) {
        return sd->stack->material_score;
    }

    /*
     * 3. Calculate material value and store in material hash table
     */
    int value = 0;
    TBoard * pos = sd->pos;
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

    // Calculate game phase
    int phase = MAX_GAMEPHASES
            - (wminors + bminors) /* max: 8 */
            - (wrooks + brooks) /* max:4 */
            - 2 * (wqueens + bqueens) /* max: 4 */;
    phase = MAX(0, phase);
    phase = (MAX_GAMEPHASES * phase) / MAX_GAMEPHASES;

    // Knights
    if (wknights != bknights) {
        value += (wknights - bknights) * PHASED_SCORE(SVKNIGHT, phase);
    }

    //Bishops
    if (wbishops != bbishops) {
        value += (wbishops - bbishops) * PHASED_SCORE(SVBISHOP, phase);
        bool wPair = wbishops > 1 && pos->whiteBishopPair(); //note: material hash includes bishop colors
        bool bPair = bbishops > 1 && pos->blackBishopPair();
        value += cond(wPair, bPair, VBISHOPPAIR);
    }

    //Rooks
    if (wrooks != brooks) {
        value += (wrooks - brooks) * PHASED_SCORE(SVROOK, phase);
    }

    //Queens
    if (wqueens != bqueens) {
        value += (wqueens - bqueens) * PHASED_SCORE(SVQUEEN, phase);
    }

    //Bonus for having more "piece power" (excluding pawns)
    int piecepower = value;
    value += cond(wpawns && piecepower > MATERIAL_AHEAD_TRESHOLD,
            bpawns && piecepower < -MATERIAL_AHEAD_TRESHOLD,
            PIECEPOWER_AHEAD,
            piecepower / VPAWN,
            -piecepower / VPAWN);

    if (wpawns != bpawns) {
        value += (wpawns - bpawns) * PHASED_SCORE(SVPAWN, phase);
    }

    /* 
     * Endgame adjustment: 
     * 1) Not having pawns makes it hard to win
     * 2) If ahead, but no pawns and no mating material the score is draw
     */
    if (!wpawns && bpawns) {
        if (value > 0 && piecepower > 0 && piecepower < 4 * VPAWN) {
            if (wminors < 2 && wrooks == brooks && wqueens == bqueens) {
                value >>= 2;
            } else {
                value >>= 1;
            }
        }
        value += VNOPAWNS;
    } else if (!bpawns && wpawns) {
        if (piecepower > -4 * VPAWN && piecepower < 0) {
            if (value < 0 && piecepower < 0 && piecepower > -4 * VPAWN) {
                if (bminors < 2 && wrooks == brooks && wqueens == bqueens) {
                    value >>= 2;
                } else {
                    value >>= 1;
                }
            }
        }
        value -= VNOPAWNS;
    } else if (!wpawns && !bpawns) {
        if (ABS(piecepower) < 4 * VPAWN) {
            value >>= 3;
        }
    }

    /* 
     * Endgame adjustment: 
     * 2) Rooks and queen endgames are drawish. Reduce any small material advantage.
     */
    if (value
            && wminors < 2
            && bminors < 2
            && wpieces <= 3
            && wpieces == bpieces
            && ABS(value) > ABS(DRAWISH_QR_ENDGAME)
            && ABS(value) < 2 * VPAWN
            && ABS(piecepower) < VPAWN / 2) {
        value += cond(value > 0, value < 0, DRAWISH_QR_ENDGAME);
    }

    /*
     * Endgame adjustment:
     * Opposite  bishop ending is drawish
     */
    if (value && wpieces == 1 && bpieces == 1 && wpawns != bpawns && wbishops && bbishops) {
        if ((pos->whiteBishops & BLACK_SQUARES) != (pos->blackBishops & BLACK_SQUARES)) {
            value += cond(wpawns>bpawns, bpawns>wpawns, MAX(-(ABS(value) >> 1), DRAWISH_OPP_BISHOPS));
        }
    }

    //trade down bonus: trade pieces but not pawns when ahead
    value += cond(piecepower > MATERIAL_AHEAD_TRESHOLD, value < -MATERIAL_AHEAD_TRESHOLD, TRADEDOWN_PIECES, bpieces, wpieces);
    value += cond(piecepower > MATERIAL_AHEAD_TRESHOLD, value < -MATERIAL_AHEAD_TRESHOLD, TRADEDOWN_PAWNS, wpawns, bpawns);

    /*
     * Store and return
     */

    sd->stack->material_score = value;
    sd->stack->phase = phase;
    sd->hashTable->mtStore(sd, value, phase);
    return value;
}

void init_pct(TSCORE_PCT & pct) {
    TScore scores[64];
    TScore PAWN_OFFSET(-20, -20);
    TScore KNIGHT_OFFSET(-35, -30);
    TScore BISHOP_OFFSET(-20, -12);
    TScore ROOK_OFFSET(-15, -15);
    TScore QUEEN_OFFSET(-10, -15);
    TScore KING_OFFSET(0, -10);

    const short mobility_scale[2][64] = {
        {
            6, 7, 8, 9, 9, 8, 7, 6,
            5, 6, 7, 8, 8, 7, 6, 5,
            4, 5, 7, 7, 7, 7, 5, 4,
            3, 4, 6, 8, 8, 6, 4, 3,
            2, 3, 5, 7, 7, 5, 3, 2,
            2, 3, 4, 5, 5, 4, 3, 2,
            2, 3, 4, 5, 5, 4, 3, 2,
            1, 2, 3, 4, 4, 3, 2, 1
        },
        {
            1, 2, 3, 4, 4, 3, 2, 1,
            2, 3, 3, 5, 5, 3, 3, 2,
            3, 3, 4, 6, 6, 4, 3, 3,
            2, 3, 5, 7, 7, 5, 3, 2,
            2, 3, 5, 7, 7, 5, 3, 2,
            2, 3, 4, 5, 5, 4, 3, 2,
            2, 3, 4, 5, 5, 4, 3, 2,
            1, 2, 3, 4, 4, 3, 2, 1
        }
    };

    //Pawn
    for (int sq = a1; sq < 64; sq++) {
        U64 bbsq = BIT(sq);
        scores[sq].clear();
        if ((bbsq & RANK_1) || (bbsq & RANK_8)) {
            continue;
        }
        if (bbsq & RANK_2) { //extra mobility
            scores[sq].add_ix64(&mobility_scale, FLIP_SQUARE(FILE(sq) + 16));
        }
        if (bbsq & (FILE_A | FILE_H)) {
            scores[sq].add(-10, 0);
        }
        int rank = RANK(sq) - 1;
        scores[sq].eg += rank*rank; //moving pawns forward to promote

        if (sq == e2 || sq == d2) {
            scores[sq].mg -= 10;
        }
        if (sq == c2 || sq == f2) {
            scores[sq].mg -= 5;
        }
        if (bbsq & FRONTFILL(CENTER)) {
            scores[sq].add(10, 0);
        }
        if (bbsq & CENTER) {
            scores[sq].add(10, 0);
        }
        if (bbsq & LARGE_CENTER) {
            scores[sq].add(10, 0);
        }
        U64 caps = WPawnCaptures[sq] | WPawnMoves[sq];
        while (caps) {
            int ix = POP(caps);
            scores[sq].add_ix64(&mobility_scale, FLIP_SQUARE(ix));
        }
        scores[sq].add(PAWN_OFFSET);
        scores[sq].round();
        pct[WPAWN][sq].set(scores[sq]);
        pct[BPAWN][FLIP_SQUARE(sq)].nset(scores[sq]);
    }

    //Knight
    for (int sq = a1; sq < 64; sq++) {
        scores[sq].clear();
        U64 caps = KnightMoves[sq];
        while (caps) {
            int ix = POP(caps);
            scores[sq].add_ix64(&mobility_scale, FLIP_SQUARE(ix));
        }
        scores[sq].mul(1.5); //mobility is extra important because knights move slow
        scores[sq].add(KNIGHT_OFFSET);
        scores[sq].round();
        pct[WKNIGHT][sq].set(scores[sq]);
        pct[BKNIGHT][FLIP_SQUARE(sq)].nset(scores[sq]);
    }

    //Bishop
    for (int sq = a1; sq < 64; sq++) {
        scores[sq].clear();
        U64 caps = BishopMoves[sq];
        while (caps) {
            int ix = POP(caps);
            scores[sq].add_ix64(&mobility_scale, FLIP_SQUARE(ix));
        }
        scores[sq].mul(0.5); //mobility is less important because bishops move fast
        scores[sq].add(BISHOP_OFFSET);
        scores[sq].round();
        pct[WBISHOP][sq].set(scores[sq]);
        pct[BBISHOP][FLIP_SQUARE(sq)].nset(scores[sq]);
    }

    //Rook
    for (int sq = a1; sq < 64; sq++) {
        scores[sq].clear();
        U64 bbsq = BIT(sq);
        U64 caps = RookMoves[sq];
        while (caps) {
            int ix = POP(caps);
            scores[sq].add_ix64(&mobility_scale, FLIP_SQUARE(ix));
        }
        scores[sq].mg *= 0.25; //mobility is less important because rooks move fast
        scores[sq].eg *= 0.5; //mobility is less important because rooks move fast

        if (bbsq & (RANK_7 | RANK_8)) {
            scores[sq].mg += 40; //extra bonus for 7th / 8th rank
            scores[sq].eg += 5;
        }
        if (bbsq & (RANK_6)) {
            scores[sq].mg += 10;
            scores[sq].eg += 2;
        }
        if (bbsq & RANK_1) { //protection
            scores[sq].mg += 8;
        }
        scores[sq].add(ROOK_OFFSET);
        scores[sq].round();
        pct[WROOK][sq].set(scores[sq]);
        pct[BROOK][FLIP_SQUARE(sq)].nset(scores[sq]);
    }

    //Queen
    for (int sq = a1; sq < 64; sq++) {
        scores[sq].clear();
        U64 caps = QueenMoves[sq];
        while (caps) {
            int ix = POP(caps);
            scores[sq].add_ix64(&mobility_scale, FLIP_SQUARE(ix));
        }
        scores[sq].mg *= 0.15;
        scores[sq].eg *= 0.50;
        scores[sq].add(QUEEN_OFFSET);
        scores[sq].round();
        pct[WQUEEN][sq].set(scores[sq]);
        pct[BQUEEN][FLIP_SQUARE(sq)].nset(scores[sq]);
    }


    //King
    for (int sq = a1; sq < 64; sq++) {
        scores[sq].clear();
        U64 caps = KingMoves[sq];
        while (caps) {
            int ix = POP(caps);
            scores[sq].add_ix64(&mobility_scale, FLIP_SQUARE(ix));
        }
        scores[sq].mg = 0;
        scores[sq].add(KING_OFFSET);
        scores[sq].round();
        pct[WKING][sq].set(scores[sq]);
        pct[BKING][FLIP_SQUARE(sq)].nset(scores[sq]);
    }
}

/**
 * Evaluate pawn structure score 
 * @param sd search metadata object
 */

inline TScore * evaluatePawns(TSearch * sd) {




    /*
     * 1. Get the score from the last stack record if the latest move did not
     *    involve any pawns. This is easy to check with the pawn hash 
     */
    if (sd->stack->equal_pawns) {
        sd->stack->pawn_score.set((sd->stack - 1)->pawn_score);
        sd->stack->shelter_score_w.set((sd->stack - 1)->shelter_score_w);
        sd->stack->shelter_score_b.set((sd->stack - 1)->shelter_score_b);
        return &sd->stack->pawn_score;
    }

    /*
     * 2. Probe the hash table for the pawn score
     */
    sd->hashTable->ptLookup(sd);
    if (sd->stack->pawn_score.valid()) {
        return &sd->stack->pawn_score;
    }

    /*
     * 3. Calculate pawn evaluation score
     */
    TScore * pawn_score = &sd->stack->pawn_score;
    TScore * shelter_score_w = &sd->stack->shelter_score_w;
    TScore * shelter_score_b = &sd->stack->shelter_score_b;
    pawn_score->clear();
    shelter_score_w->set(-150, -50);
    shelter_score_b->set(-150, -50);
    TBoard * pos = sd->pos;

    int wkpos = *pos->whiteKingPos;
    int bkpos = *pos->blackKingPos;
    U64 passers = 0;
    U64 openW = ~sd->stack->fill[WHITE];
    U64 openB = ~sd->stack->fill[BLACK];
    U64 wUp = UP1(pos->whitePawns);
    U64 bDown = DOWN1(pos->blackPawns);
    U64 upLeftW = UPLEFT1(pos->whitePawns);
    U64 upRightW = UPRIGHT1(pos->whitePawns);
    U64 downLeftB = DOWNLEFT1(pos->blackPawns);
    U64 downRightB = DOWNRIGHT1(pos->blackPawns);
    U64 wAttacks = upRightW | upLeftW;
    U64 bAttacks = downLeftB | downRightB;
    U64 wBlocked = DOWN1(wUp & pos->blackPawns) & ~bAttacks;
    U64 bBlocked = UP1(bDown & pos->whitePawns) & ~wAttacks;
    U64 wSafe = (upLeftW & upRightW) | ~bAttacks | ((upLeftW ^ upRightW) & ~(downLeftB & downRightB));
    U64 bSafe = (downLeftB & downRightB) | ~wAttacks | ((downLeftB ^ downRightB) & ~(upLeftW & upRightW));
    U64 wMoves = UP1(pos->whitePawns & ~wBlocked) & wSafe;
    U64 bMoves = DOWN1(pos->blackPawns & ~bBlocked) & bSafe;
    wMoves |= UP1(wMoves & RANK_3 & ~bAttacks & ~DOWN1((pos->blackPawns | pos->whitePawns))) & wSafe;
    bMoves |= DOWN1(bMoves & RANK_6 & ~wAttacks & ~UP1((pos->whitePawns | pos->blackPawns))) & bSafe;
    U64 wAttackRange = wAttacks | UPLEFT1(wMoves) | UPRIGHT1(wMoves);
    U64 bAttackRange = bAttacks | DOWNLEFT1(bMoves) | DOWNRIGHT1(bMoves);
    U64 wIsolated = pos->whitePawns & ~(FILEFILL(wAttacks));
    U64 bIsolated = pos->blackPawns & ~(FILEFILL(bAttacks));
    U64 wDoubled = DOWN1(southFill(pos->whitePawns)) & pos->whitePawns;
    U64 bDoubled = UP1(northFill(pos->blackPawns)) & pos->blackPawns;

    TPiecePlacement * wPawns = &pos->pieces[WPAWN];
    for (int i = 0; i < wPawns->count; i++) {
        int sq = wPawns->squares[i];
        int isq = FLIP_SQUARE(sq);

        U64 sqBit = BIT(sq);
        U64 up = northFill(sqBit);
        bool open = sqBit & openB;
        bool doubled = sqBit & wDoubled;
        bool isolated = sqBit & wIsolated;
        bool defended = sqBit & wAttacks;
        bool blocked = sqBit & wBlocked;
        bool push_defend = !blocked && (UP1(sqBit) & wAttackRange);
        bool push_double_defend = !blocked && (UP2(sqBit & RANK_2) & wMoves & wAttackRange);
        bool lost = sqBit & ~wAttackRange;
        bool weak = !defended && lost && !push_defend && !push_double_defend;
        bool passed = open && !doubled && !(up & bAttacks);
        bool candidate = open && !doubled && !passed && !(up & ~wSafe);


        //std::cout << "WP " << PRINT_SQUARE(sq) << ": ";
        if (isolated) {
            pawn_score->add(ISOLATED_PAWN[open]);
            //std::cout << "isolated: " << PRINT_SCORE(ISOLATED_PAWN[open]);
        } else if (weak) {
            pawn_score->add(WEAK_PAWN[open]);
            //std::cout << "weak: " << PRINT_SCORE(WEAK_PAWN[open]);
        }
        if (doubled) {
            pawn_score->add(DOUBLED_PAWN);
            //std::cout << "doubled: " << PRINT_SCORE(DOUBLED_PAWN);
        }
        if (defended) {
            pawn_score->add(DEFENDED_PAWN[open]);
            //std::cout << "defended: " << PRINT_SCORE(DEFENDED_PAWN[open]);
        }
        if (candidate) {
            pawn_score->add(CANDIDATE[isq]);
            //std::cout << "candidate: " << PRINT_SCORE(CANDIDATE[isq]);
        } else if (passed) {
            pawn_score->add(PASSED_PAWN[isq]);
            //std::cout << "passed: " << PRINT_SCORE(PASSED_PAWN[isq]);
            passers |= sqBit;
            if (up & pos->blackKings) { //blocked by king
                pawn_score->sub(PASSED_PAWN[isq].mg >> 1, PASSED_PAWN[isq].eg >> 1);
                //std::cout << "passer blocked: (" << short(-PASSED_PAWN[isq].mg >> 1) << ", " << short(-PASSED_PAWN[isq].eg >> 1) << ") ";
            }
            bool connected = pos->whitePawns & passers & KingMoves[sq];
            if (connected) {
                pawn_score->add(CONNECED_PASSED_PAWN[isq]);
                //std::cout << "connected: " << PRINT_SCORE(CONNECED_PASSED_PAWN[isq]);
            }
        }
        //std::cout << std::endl;
    }

    TPiecePlacement * bPawns = &pos->pieces[BPAWN];
    for (int i = 0; i < bPawns->count; i++) {
        int sq = bPawns->squares[i];

        U64 sqBit = BIT(sq);
        U64 down = southFill(sqBit);
        bool open = sqBit & openW;
        bool doubled = sqBit & bDoubled;
        bool isolated = sqBit & bIsolated;
        bool blocked = sqBit & bBlocked;
        bool defended = sqBit & bAttacks;
        bool push_defend = !blocked && (DOWN1(sqBit) & bAttackRange);
        bool push_double_defend = !blocked && (DOWN2(sqBit & RANK_7) & bMoves & bAttackRange);
        bool lost = sqBit & ~bAttackRange;
        bool weak = !defended && lost && !push_defend && !push_double_defend;
        bool passed = open && !doubled && !(down & wAttacks);
        bool candidate = open && !doubled && !passed && !(down & ~bSafe);

        //std::cout << "BP " << PRINT_SQUARE(sq) << ": ";
        if (isolated) {
            pawn_score->sub(ISOLATED_PAWN[open]);
            //  std::cout << "isolated: " << PRINT_SCORE(ISOLATED_PAWN[open]);
        } else if (weak) {
            pawn_score->sub(WEAK_PAWN[open]);
            //std::cout << "weak: " << PRINT_SCORE(WEAK_PAWN[open]);
        }
        if (doubled) {
            pawn_score->sub(DOUBLED_PAWN);
            //std::cout << "doubled: " << PRINT_SCORE(DOUBLED_PAWN);
        }
        if (defended) {
            pawn_score->sub(DEFENDED_PAWN[open]);
            //std::cout << "defended: " << PRINT_SCORE(DEFENDED_PAWN[open]);
        }
        if (candidate) {
            pawn_score->sub(CANDIDATE[sq]);
            // std::cout << "candidate: " << PRINT_SCORE(CANDIDATE[sq]);
        } else if (passed) {
            pawn_score->sub(PASSED_PAWN[sq]);
            // std::cout << "passed: " << PRINT_SCORE(PASSED_PAWN[sq]);
            passers |= sqBit;
            if (down & pos->whiteKings) { //blocked by king
                pawn_score->add(PASSED_PAWN[sq].mg >> 1, PASSED_PAWN[sq].eg >> 1);
                //std::cout << "passer blocked: (" << short(-PASSED_PAWN[sq].mg >> 1) << ", " << short(-PASSED_PAWN[sq].eg >> 1) << ") ";
            }
            bool connected = pos->blackPawns & passers & KingMoves[sq];
            if (connected) {
                pawn_score->sub(CONNECED_PASSED_PAWN[sq]);
                //    std::cout << "connected: " << PRINT_SCORE(CONNECED_PASSED_PAWN[sq]);
            }
        }
        //std::cout << std::endl;
    }

    //std::cout << "total: ";
    //pawn_score->print();
    //std::cout << std::endl;

    //support and attack pawns with king in the EG
    pawn_score->add(0, popCount(KingZone[wkpos] & pos->pawns(), true) * 12);
    pawn_score->sub(0, popCount(KingZone[bkpos] & pos->pawns(), true) * 12);



    /*
     * 4. Evaluate King shelter score. 
     */

    shelter_score_w->add(SHELTER_KPOS[FLIP_SQUARE(wkpos)]);
    //1. reward having the right to castle
    if (pos->boardFlags->castlingFlags & CASTLE_K
            && ((pos->Matrix[h2] == WPAWN && pos->Matrix[g2] == WPAWN)
            || (pos->Matrix[f2] == WPAWN && pos->Matrix[h2] == WPAWN && pos->Matrix[g3] == WPAWN)
            || (pos->Matrix[h3] == WPAWN && pos->Matrix[g2] == WPAWN && pos->Matrix[f2] == WPAWN))) {
        shelter_score_w->add(SHELTER_CASTLING_KINGSIDE);
    } else if (pos->boardFlags->castlingFlags & CASTLE_Q
            && ((pos->Matrix[a2] == WPAWN && pos->Matrix[b2] == WPAWN && pos->Matrix[c2] == WPAWN)
            || (pos->Matrix[a2] == WPAWN && pos->Matrix[b3] == WPAWN && pos->Matrix[c2] == WPAWN))) {
        shelter_score_w->add(SHELTER_CASTLING_QUEENSIDE);
    }

    //2. reward having pawns in front of the king
    U64 kingFront = FORWARD_RANKS[RANK(wkpos)] & PAWN_SCOPE[FILE(wkpos)];
    U64 shelterPawns = kingFront & pos->whitePawns;

    while (shelterPawns) {
        int sq = POP(shelterPawns);
        shelter_score_w->add(SHELTER_PAWN[FLIP_SQUARE(sq)]);
    }

    U64 stormPawns = kingFront & KingZone[wkpos] & pos->blackPawns;
    while (stormPawns) {
        int sq = POP(stormPawns);
        shelter_score_w->sub(STORM_PAWN[FLIP_SQUARE(sq)]);
    }

    //3. penalize (half)open files on the king
    U64 open = (openW | openB) & kingFront & RANK_8;
    if (open) {
        shelter_score_w->add(SHELTER_OPEN_FILES[popCount(open & openW, true)]);
        shelter_score_b->sub(SHELTER_OPEN_ATTACK_FILES[popCount(open & openB, true)]);
        if (open & openW & openB & (FILE_A | FILE_B | FILE_H | FILE_G)) {
            shelter_score_w->add(SHELTER_OPEN_EDGE_FILE);
        }
    }


    //black king shelter
    shelter_score_b->add(SHELTER_KPOS[bkpos]);
    //1. reward having the right to castle safely
    if (pos->boardFlags->castlingFlags & CASTLE_k
            && ((pos->Matrix[h7] == BPAWN && pos->Matrix[g7] == BPAWN)
            || (pos->Matrix[f7] == BPAWN && pos->Matrix[h7] == BPAWN && pos->Matrix[g6] == BPAWN)
            || (pos->Matrix[h6] == BPAWN && pos->Matrix[g7] == BPAWN && pos->Matrix[f7] == BPAWN))) {
        shelter_score_b->add(SHELTER_CASTLING_KINGSIDE);
    } else if (pos->boardFlags->castlingFlags & CASTLE_q
            && ((pos->Matrix[a7] == BPAWN && pos->Matrix[b7] == BPAWN && pos->Matrix[c7] == BPAWN)
            || (pos->Matrix[a7] == BPAWN && pos->Matrix[b6] == BPAWN && pos->Matrix[c7] == BPAWN))) {
        shelter_score_b->add(SHELTER_CASTLING_QUEENSIDE);
    }

    //2. reward having pawns in front of the king
    kingFront = BACKWARD_RANKS[RANK(bkpos)] & PAWN_SCOPE[FILE(bkpos)];
    shelterPawns = kingFront & pos->blackPawns;
    while (shelterPawns) {
        int sq = POP(shelterPawns);
        shelter_score_b->add(SHELTER_PAWN[sq]);
    }

    stormPawns = kingFront & KingZone[bkpos] & pos->whitePawns;
    while (stormPawns) {
        int sq = POP(stormPawns);
        shelter_score_b->sub(STORM_PAWN[sq]);
    }

    //3. penalize (half)open files on the king
    open = (openW | openB) & kingFront & RANK_1;
    if (open) {
        shelter_score_b->add(SHELTER_OPEN_FILES[popCount(open & openB, true)]);
        shelter_score_w->sub(SHELTER_OPEN_ATTACK_FILES[popCount(open & openW, true)]);
        if (open & openB & openW & (FILE_A | FILE_B | FILE_H | FILE_G)) {
            shelter_score_b->add(SHELTER_OPEN_EDGE_FILE);
        }
    }
    sd->hashTable->ptStore(sd, pawn_score, shelter_score_w, shelter_score_b);
    return &sd->stack->pawn_score;
}

inline TScore * evaluateBishops(TSearch * sd) {

    TScore * result = &sd->stack->bishop_score;
    result->clear();

    TBoard * pos = sd->pos;
    if (pos->whiteBishops == 0 && pos->blackBishops == 0) {
        return result;
    }

    /*
     * 2. Get the score from the last stack record if the previous move 
     *   a) did not change the pawn+kings structure (pawn hash) 
     *   b) did not move or capture any bishop
     */
    if (sd->stack->equal_pawns) {
        TMove * prevMove = &(sd->stack - 1)->move;
        if (prevMove->piece != WBISHOP && prevMove->piece != BBISHOP
                && prevMove->capture != WBISHOP && prevMove->capture != BBISHOP) {
            result->set((sd->stack - 1)->bishop_score);
            return result;
        }
    }

    /*
     * 3. Calculate the score and store on the stack
     */


    TPiecePlacement * pc = &pos->pieces[WBISHOP];
    for (int i = 0; i < pc->count; i++) {
        int sq = pc->squares[i];
        U64 moves = MagicBishopMoves(sq, sd->stack->occ) & sd->stack->mobMask[WHITE];
        int count = popCount(moves);
        result->add(BISHOP_MOBILITY[count]);
        if (count < 2 && (sq == h7 && pos->Matrix[g6] == BPAWN && pos->Matrix[f7] == BPAWN)
                || (sq == a7 && pos->Matrix[b6] == BPAWN && pos->Matrix[c7] == BPAWN)) {
            result->add(TRAPPED_BISHOP);
        }
    }
    pc = &pos->pieces[BBISHOP];
    for (int i = 0; i < pc->count; i++) {
        int sq = pc->squares[i];
        U64 moves = MagicBishopMoves(sq, sd->stack->occ) & sd->stack->mobMask[BLACK];
        int count = popCount(moves);
        result->sub(BISHOP_MOBILITY[count]);
        if (count < 2 && (sq == h2 && pos->Matrix[g3] == WPAWN && pos->Matrix[f3] == WPAWN)
                || (sq == a2 && pos->Matrix[b3] == WPAWN && pos->Matrix[c2] == WPAWN)) {
            result->add(TRAPPED_BISHOP);
        }
    }


    return result;
}

inline TScore * evaluateRooks(TSearch * sd) {


    TScore * result = &sd->stack->rook_score;
    result->clear();

    TBoard * pos = sd->pos;
    if (pos->whiteRooks == 0 && pos->blackRooks == 0) {
        return result;
    }
    /*
     * 2. Get the score from the last stack record if the previous move 
     *   a) did not change the pawn+kings structure (pawn hash) 
     *   b) did not move or capture any rook
     */
    if (sd->stack->equal_pawns) {
        TMove * prevMove = &(sd->stack - 1)->move;
        if (prevMove->piece != WROOK && prevMove->capture != WROOK
                && prevMove->piece != BROOK && prevMove->capture != BROOK) {
            result->set((sd->stack - 1)->rook_score);
            return result;
        }
    }

    /*
     * 3. Calculate the score and store on the stack
     */



    TPiecePlacement * pc = &pos->pieces[WROOK];
    for (int i = 0; i < pc->count; i++) {
        int sq = pc->squares[i];
        if (BIT(sq) & (~sd->stack->fill[WHITE])) {
            if (BIT(sq) & sd->stack->fill[BLACK]) {
                result->add(ROOK_SEMIOPEN_FILE);
            } else {
                result->add(ROOK_OPEN_FILE);
            }
        }
        U64 moves = MagicRookMoves(sq, sd->stack->occ) & sd->stack->mobMask[WHITE];
        int count = popCount(moves);
        result->add(ROOK_MOBILITY[count]);
        U64 attack = popCount(moves & (pos->blackPawns | RANK_7 | KingZone[*pos->blackKingPos]), true);
        result->add(attack << 3, attack << 1);
        if (sq <= h1 && *pos->whiteKingPos <= h2) {
            sd->stack->shelter_score_w.add(ROOK_SHELTER_PROTECT);
        }
    }
    pc = &pos->pieces[BROOK];
    for (int i = 0; i < pc->count; i++) {
        int sq = pc->squares[i];
        if (BIT(sq) & (~sd->stack->fill[BLACK])) {
            if (BIT(sq) & sd->stack->fill[WHITE]) {
                result->sub(ROOK_SEMIOPEN_FILE);
            } else {
                result->sub(ROOK_OPEN_FILE);
            }
        }
        U64 moves = MagicRookMoves(sq, sd->stack->occ) & sd->stack->mobMask[BLACK];
        int count = popCount(moves);
        result->sub(ROOK_MOBILITY[count]);
        U64 attack = popCount(moves & (pos->whitePawns | RANK_2 | KingZone[*pos->whiteKingPos]), true);
        result->sub(attack << 3, attack << 1);
        if (sq >= a8 && *pos->blackKingPos >= a7) {
            sd->stack->shelter_score_b.add(ROOK_SHELTER_PROTECT);
        }
    }

    return result;
}

inline TScore * evaluateQueens(TSearch * sd) {
    TScore * result = &sd->stack->queen_score;
    result->clear();

    if (sd->pos->blackQueens) {
        result->add(sd->stack->shelter_score_w);
    }

    if (sd->pos->whiteQueens) {
        result->sub(sd->stack->shelter_score_b);
    }

    return result;
}


