#include "evaluate.h"
#include "search.h"
#include "defs.h"
#include "score.h"

inline short evaluateMaterial(TSearch * sd);

inline TScore * evaluatePawnsAndKings(TSearch * sd);
inline TScore * evaluateBishops(TSearch * sd, bool white);
inline TScore * evaluateRooks(TSearch * sd, bool white);
inline TScore * evaluateShelter(TSearch * sd, bool white);
inline TScore * evaluatePassers(TSearch * sd, bool white);
inline int evaluatePasserVsK(TSearch * sd, bool white, int sq);

int evaluate(TSearch * sd, int alpha, int beta) {
    if (sd->stack->eval_result != SCORE_INVALID) {
        return sd->stack->eval_result;
    }

    sd->stack->equal_pawns = sd->pos->currentPly > 0
            && sd->pos->boardFlags->pawnHash == (sd->pos->boardFlags - 1)->pawnHash
            && (sd->stack - 1)->eval_result != SCORE_INVALID;

    int result = evaluateMaterial(sd); //sets stack->phase

    TScore * score = &sd->stack->eval_score;
    score->set(sd->pos->boardFlags->pct);
    score->add(evaluatePawnsAndKings(sd));
    score->add(evaluateBishops(sd, WHITE));
    score->sub(evaluateBishops(sd, BLACK));
    score->add(evaluateRooks(sd, WHITE));
    score->sub(evaluateRooks(sd, BLACK));
    score->add(evaluateShelter(sd, WHITE));
    score->sub(evaluateShelter(sd, BLACK));
    score->add(evaluatePassers(sd, WHITE));
    score->sub(evaluatePassers(sd, BLACK));

    result += score->get(sd->stack->phase);
    result &= GRAIN;
    result = sd->pos->boardFlags->WTM ? result : -result;
    sd->stack->eval_result = result;

    assert(result > -VKING && result < VKING);

    return result;
}

bool skipExp(TSearch * sd) {
    int pc = WROOK;
    return sd->pos->pieces[pc].count == 0 && sd->pos->pieces[pc + WKING].count == 0;
}

const short TRADEDOWN_PIECES[MAX_PIECES + 1] = {
    80, 60, 40, 20, 10, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0
};

const short TRADEDOWN_PAWNS[9] = {
    -60, -40, -20, -10, -5, 0, 5, 10, 15
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
    TScore result;
    result.clear();
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
    int phase = MAX_GAMEPHASES /* 16 */
            - (wminors + bminors) /* max: 8 */
            - (wrooks + brooks) /* max:4 */
            - 2 * (wqueens + bqueens) /* max: 4 */;
    phase = MAX(0, phase);

    // Piece values 
    result.mg += (wknights - bknights) * SVKNIGHT.mg;
    result.eg += (wknights - bknights) * SVKNIGHT.eg;
    result.mg += (wbishops - bbishops) * SVBISHOP.mg;
    result.eg += (wbishops - bbishops) * SVBISHOP.eg;
    result.mg += (wrooks - brooks) * SVROOK.mg;
    result.eg += (wrooks - brooks) * SVROOK.eg;
    result.mg += (wqueens - bqueens) * SVQUEEN.mg;
    result.eg += (wqueens - bqueens) * SVQUEEN.eg;
    if (wbishops > 1 && pos->whiteBishopPair()) {
        result.add(VBISHOPPAIR);
    }
    if (bbishops > 1 && pos->blackBishopPair()) {
        result.sub(VBISHOPPAIR);
    }

    int piecePower = result.get(phase);
    result.mg += (wpawns - bpawns) * SVPAWN.mg;
    result.eg += (wpawns - bpawns) * SVPAWN.eg;

    // when ahead in material, reduce opponent's pieces (simplify) and keep pawns
    if (piecePower > MATERIAL_AHEAD_TRESHOLD) {
        result.add(TRADEDOWN_PIECES[bpieces]);
        result.add(TRADEDOWN_PAWNS[wpawns]);
    } else if (piecePower < -MATERIAL_AHEAD_TRESHOLD) {
        result.sub(TRADEDOWN_PIECES[wpieces]);
        result.sub(TRADEDOWN_PAWNS[bpawns]);
    }

    // penalty for not having pawns - makes it hard to win
    if (wpawns == 0) {
        result.add(VNOPAWNS);
    }
    if (bpawns == 0) {
        result.sub(VNOPAWNS);
    }

    int value = result.get(phase);

    //Rooks and queen endgames are drawish. Reduce any small material advantage.
    if (value
            && wminors < 2
            && bminors < 2
            && wpieces <= 3
            && (wrooks || wqueens)
            && (brooks || bqueens)
            && wpieces == bpieces
            && ABS(value) > ABS(DRAWISH_QR_ENDGAME)
            && ABS(value) < 2 * VPAWN
            && ABS(piecePower) < VPAWN / 2) {
        value += cond(value > 0, value < 0, DRAWISH_QR_ENDGAME);
    }

    // Opposite  bishop ending is drawish
    if (value && wpieces == 1 && bpieces == 1 && wpawns != bpawns && wbishops && bbishops) {
        if (bool(pos->whiteBishops & BLACK_SQUARES) != bool(pos->blackBishops & BLACK_SQUARES)) {
            //set drawflag 
            value += cond(wpawns>bpawns, bpawns>wpawns, MAX(-(ABS(value) >> 1), DRAWISH_OPP_BISHOPS));
        }
    }

    //winning edge?
    if (piecePower >= VROOK && value > 2 * VPAWN) {
        value += (value >> 2);
    } else if (piecePower <= -VROOK && value < -2 * VPAWN) {
        value -= (value >> 2);
    }

    //ahead but no mating material?
    if (value > 0 && piecePower < VROOK && !wpawns) {
        value >>= 2;
    } else if (value < 0 && piecePower > -VROOK && !bpawns) {
        value >>= 2;
    }

    //ahead with only pawns? 
    if (value > 0 && piecePower < 0) {
        value -= 50;
        if (value < VPAWN && bminors > wminors) {
            //set drawflag
        }
    } else if (value < 0 && piecePower > 0) {
        if (value > -VPAWN && wminors > bminors) {
            //set drawflag
        }
        value += 50;

    }

    /*
     * Store and return
     */
    sd->stack->material_score = value;
    sd->stack->phase = phase;
    sd->hashTable->mtStore(sd, value, phase);
    return value;
}

void init_pct_store(TSCORE_PCT & pct, TScore scores[], int wpiece) {
    int tot_mg = 0;
    int tot_eg = 0;
    for (int sq = a1; sq <= h8; sq++) {
        tot_mg += scores[sq].mg;
        tot_eg += scores[sq].eg;
    }
    int avg_mg = tot_mg / 64;
    int avg_eg = tot_eg / 64;
    for (int sq = a1; sq <= h8; sq++) {
        scores[sq].mg -= avg_mg;
        scores[sq].eg -= avg_eg;
        scores[sq].round();
        pct[wpiece][sq].set(scores[sq]);
        pct[wpiece + WKING][FLIP_SQUARE(sq)].nset(scores[sq]);
    }
}

void init_pct(TSCORE_PCT & pct) {
    TScore scores[64];
    const short mobility_scale[2][64] = {{
        1, 2, 3, 4, 4, 3, 2, 1,
        2, 3, 4, 5, 5, 4, 3, 2,
        3, 4, 5, 6, 6, 5, 4, 3,
        4, 5, 6, 7, 7, 6, 5, 4,
        4, 5, 6, 7, 7, 6, 5, 4,
        3, 4, 5, 6, 6, 5, 4, 3,
        2, 3, 4, 5, 5, 4, 3, 2,
        1, 2, 3, 4, 4, 3, 2, 1
    },
            {
        1, 2, 3, 4, 4, 3, 2, 1,
        2, 3, 4, 5, 5, 4, 3, 2,
        3, 4, 5, 6, 6, 5, 4, 3,
        4, 5, 6, 7, 7, 6, 5, 4,
        4, 5, 6, 7, 7, 6, 5, 4,
        3, 4, 5, 6, 6, 5, 4, 3,
        2, 3, 4, 5, 5, 4, 3, 2,
        1, 2, 3, 4, 4, 3, 2, 1
    }};

    //Pawn
    for (int sq = a1; sq < 64; sq++) {
        U64 bbsq = BIT(sq);
        scores[sq].clear();
        if ((bbsq & RANK_1) || (bbsq & RANK_8)) {
            continue;
        }
        U64 caps = WPawnCaptures[sq] | WPawnMoves[sq];
        while (caps) {
            int ix = POP(caps);
            scores[sq].add_ix64(&mobility_scale, FLIP_SQUARE(ix));
        }
        scores[sq].mul(3); //pawns are the most powerful to control squares
    }
    init_pct_store(pct, scores, WPAWN);

    //Knight
    for (int sq = a1; sq < 64; sq++) {
        scores[sq].clear();
        U64 caps = KnightMoves[sq];
        while (caps) {
            int ix = POP(caps);
            scores[sq].add_ix64(&mobility_scale, FLIP_SQUARE(ix));
        }
        scores[sq].mul(1.5); //mobility is extra important because knights move slow
    }
    init_pct_store(pct, scores, WKNIGHT);

    //Bishop
    for (int sq = a1; sq < 64; sq++) {
        scores[sq].clear();
        U64 caps = BishopMoves[sq];
        while (caps) {
            int ix = POP(caps);
            scores[sq].add_ix64(&mobility_scale, FLIP_SQUARE(ix));
        }
    }
    init_pct_store(pct, scores, WBISHOP);

    //Rook
    for (int sq = a1; sq < 64; sq++) {
        scores[sq].clear();
        U64 bbsq = BIT(sq);
        U64 caps = RookMoves[sq];
        while (caps) {
            int ix = POP(caps);
            scores[sq].add_ix64(&mobility_scale, FLIP_SQUARE(ix));
        }
        scores[sq].half(); //square control by rook is less powerful than by bishop/knight/pawn
    }
    init_pct_store(pct, scores, WROOK);

    //Queen
    for (int sq = a1; sq < 64; sq++) {
        scores[sq].clear();
        U64 caps = QueenMoves[sq];
        U64 bbsq = BIT(sq);
        while (caps) {
            int ix = POP(caps);
            scores[sq].add_ix64(&mobility_scale, FLIP_SQUARE(ix));
        }
        scores[sq].mul(0.25);
    }
    init_pct_store(pct, scores, WQUEEN);

    //King
    for (int sq = a1; sq < 64; sq++) {
        scores[sq].clear();
        U64 caps = KingMoves[sq];
        while (caps) {
            int ix = POP(caps);
            scores[sq].add_ix64(&mobility_scale, FLIP_SQUARE(ix));
        }
        scores[sq].mg = 0;
        scores[sq].eg *= 1.5; // kings move slow
    }
    init_pct_store(pct, scores, WKING);
}

/**
 * Evaluate pawn structure score 
 * @param sd search metadata object
 */

inline TScore * evaluatePawnsAndKings(TSearch * sd) {

    /*
     * 1. Get the score from the last stack record if the latest move did not
     *    involve any pawns. This is easy to check with the pawn hash 
     */
    if (sd->stack->equal_pawns) {
        sd->stack->pawn_score.set((sd->stack - 1)->pawn_score);
        sd->stack->shelter_score[WHITE].set((sd->stack - 1)->shelter_score[WHITE]);
        sd->stack->shelter_score[BLACK].set((sd->stack - 1)->shelter_score[BLACK]);
        sd->stack->passers = (sd->stack - 1)->passers;
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
    TScore * shelter_score_w = &sd->stack->shelter_score[WHITE];
    TScore * shelter_score_b = &sd->stack->shelter_score[BLACK];
    pawn_score->clear();
    shelter_score_w->set(-120, -50);
    shelter_score_b->set(-120, -50);
    TBoard * pos = sd->pos;

    int wkpos = *pos->whiteKingPos;
    int bkpos = *pos->blackKingPos;
    U64 passers = 0;
    U64 openW = ~FILEFILL(pos->whitePawns);
    U64 openB = ~FILEFILL(pos->blackPawns);
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
        bool passed = !doubled && !(up & (bAttacks | pos->blackPawns));
        bool candidate = open && !doubled && !passed && !(up & ~wSafe);

        //std::cout << "WP " << PRINT_SQUARE(sq) << ": ";
        if (isolated) {
            pawn_score->add(ISOLATED_PAWN[open]);
            //std::cout << "isolated: " << PRINT_SCORE(ISOLATED_PAWN[open]);
        } else if (weak) {
            pawn_score->add(WEAK_PAWN[open]); //elo vs-1:+22, elo vs0:1
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
            pawn_score->add(PASSED_PAWN[isq]); //elo vs-1: +21, elo vs0: +6
            //std::cout << "passed: " << PRINT_SCORE(PASSED_PAWN[isq]);
            passers |= sqBit;
            if (up & pos->blackKings) { //blocked by king
                pawn_score->sub(PASSED_PAWN[isq].mg >> 1, PASSED_PAWN[isq].eg >> 1);
                //std::cout << "passer blocked: (" << short(-PASSED_PAWN[isq].mg >> 1) << ", " << short(-PASSED_PAWN[isq].eg >> 1) << ") ";
            }
            if (KingMoves[sq] & passers & pos->whitePawns) {
                //std::cout << "connected: " << PRINT_SCORE(PASSED_PAWN[isq]);
                pawn_score->add(CONNECED_PASSED_PAWN[isq]);
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
        bool passed = !doubled && !(down & (wAttacks | pos->whitePawns));
        bool candidate = open && !doubled && !passed && !(down & ~bSafe);

        //std::cout << "BP " << PRINT_SQUARE(sq) << ": ";
        if (isolated) {
            pawn_score->sub(ISOLATED_PAWN[open]);
            //std::cout << "isolated: " << PRINT_SCORE(ISOLATED_PAWN[open]);
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
            //std::cout << "candidate: " << PRINT_SCORE(CANDIDATE[sq]);
        } else if (passed) {
            pawn_score->sub(PASSED_PAWN[sq]); //elo vs-1: +21, elo vs0: +6
            //std::cout << "passed: " << PRINT_SCORE(PASSED_PAWN[sq]);
            passers |= sqBit;
            if (down & pos->whiteKings) { //blocked by king
                pawn_score->add(PASSED_PAWN[sq].mg >> 1, PASSED_PAWN[sq].eg >> 1);
                //std::cout << "passer blocked: (" << short(-PASSED_PAWN[sq].mg >> 1) << ", " << short(-PASSED_PAWN[sq].eg >> 1) << ") ";
            }
            if (KingMoves[sq] & passers & pos->blackPawns) {
                pawn_score->sub(CONNECED_PASSED_PAWN[sq]);
                //std::cout << "passer connected: " << PRINT_SCORE(CONNECED_PASSED_PAWN[sq]);
            }
        }
        //std::cout << std::endl;
    }

    //std::cout << "total: ";
    //pawn_score->print();
    //std::cout << std::endl;

    //support and attack pawns with king in the EG
    pawn_score->add(0, popCount(KingZone[wkpos] & pos->allPawns(), true) * 12);
    pawn_score->sub(0, popCount(KingZone[bkpos] & pos->allPawns(), true) * 12);

    //support and attack passed pawns even more
    pawn_score->add(0, popCount(KingZone[wkpos] & passers, true) * 12);
    pawn_score->sub(0, popCount(KingZone[bkpos] & passers, true) * 12);

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
        shelter_score_w->add(SHELTER_OPEN_ATTACK_FILES[popCount(open & openB, true)]);
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
        shelter_score_b->add(SHELTER_OPEN_ATTACK_FILES[popCount(open & openW, true)]);
        if (open & openB & openW & (FILE_A | FILE_B | FILE_H | FILE_G)) {
            shelter_score_b->add(SHELTER_OPEN_EDGE_FILE);
        }
    }
    sd->stack->passers = passers;
    sd->hashTable->ptStore(sd, pawn_score, shelter_score_w, shelter_score_b, passers);
    return &sd->stack->pawn_score;
}

inline TScore * evaluateBishops(TSearch * sd, bool us) {

    TScore * result = &sd->stack->bishop_score[us];
    result->clear();

    TBoard * pos = sd->pos;
    if (*pos->bishops[us] == 0) {
        return result;
    }

    /*
     * 2. Get the score from the last stack record if the previous move 
     *   a) did not change the pawn+kings structure (pawn hash) 
     *   b) did not move or capture any bishop
     */
    if (sd->stack->equal_pawns) {
        TMove * prevMove = &(sd->stack - 1)->move;
        if (prevMove->piece != BISHOP[us] && prevMove->capture != BISHOP[us]) {
            result->set((sd->stack - 1)->bishop_score[us]);
            return result;
        }
    }

    /*
     * 3. Calculate the score and store on the stack
     */
    TPiecePlacement * pc = &pos->pieces[BISHOP[us]];
    U64 occ = pos->pawnsAndKings();
    bool them = !us;
    U64 mobMask = ~(*pos->pawns[us] | pos->pawnAttacks(them));
    for (int i = 0; i < pc->count; i++) {
        int sq = pc->squares[i];
        U64 moves = MagicBishopMoves(sq, occ);
        int count = popCount(moves & mobMask);
        result->add(BISHOP_MOBILITY[count]);
        if (us == WHITE && count < 2 && (sq == h7 && pos->Matrix[g6] == BPAWN && pos->Matrix[f7] == BPAWN)
                || (sq == a7 && pos->Matrix[b6] == BPAWN && pos->Matrix[c7] == BPAWN)) {
            result->add(TRAPPED_BISHOP);
        }
        if (us == BLACK && count < 2 && (sq == h2 && pos->Matrix[g3] == WPAWN && pos->Matrix[f3] == WPAWN)
                || (sq == a2 && pos->Matrix[b3] == WPAWN && pos->Matrix[c2] == WPAWN)) {
            result->add(TRAPPED_BISHOP);
        }
    }
    return result;
}

inline TScore * evaluateRooks(TSearch * sd, bool us) {

    TScore * result = &sd->stack->rook_score[us];
    result->clear();

    TBoard * pos = sd->pos;
    if (*pos->rooks[us] == 0) {
        return result;
    }

    /*
     * 2. Get the score from the last stack record if the previous move 
     *   a) did not change the pawn+kings structure (pawn hash, includes promotions to rook) 
     *   b) did not move or capture any rook
     */
    if (sd->stack->equal_pawns) {
        TMove * prevMove = &(sd->stack - 1)->move;
        if (prevMove->piece != ROOK[us] && prevMove->capture != ROOK[us]) {
            result->set((sd->stack - 1)->rook_score[us]);
            return result;
        }
    }

    /*
     * 3. Calculate the score and store on the stack
     */
    TPiecePlacement * pc = &pos->pieces[ROOK[us]];
    bool them = !us;
    U64 fill[2] = {FILEFILL(pos->blackPawns), FILEFILL(pos->whitePawns)};
    U64 occ = pos->pawnsAndKings();
    U64 mobMask = ~(*pos->pawns[us] | pos->pawnAttacks(them));
    for (int i = 0; i < pc->count; i++) {
        int sq = pc->squares[i];
        U64 bitSq = BIT(sq);
        if (bitSq & (~fill[us])) {
            if (bitSq & fill[them]) {
                result->add(ROOK_SEMIOPEN_FILE);
            } else {
                result->add(ROOK_OPEN_FILE);
            }
        }
        U64 moves = MagicRookMoves(sq, occ);
        int count = popCount(moves & mobMask);
        result->add(ROOK_MOBILITY[count]);

        if ((bitSq & RANK[us][1]) && (BIT(*pos->kingPos[us]) & (RANK[us][1] | RANK[us][2]))) {
            result->add(ROOK_SHELTER_PROTECT);
        }
        if (bitSq & RANK[us][7] && (BIT(*pos->kingPos[them]) & RANK[us][8])) {
            result->add(20, 40);
        }

        //Tarrasch rule: place rook behind passers
        if (moves & sd->stack->passers) {
            U64 front[2];
            front[BLACK] = southFill(bitSq) & moves & sd->stack->passers & (WHITE_SIDE | RANK_5);
            front[WHITE] = northFill(bitSq) & moves & sd->stack->passers & (BLACK_SIDE | RANK_4);
            if (front[us] & *pos->pawns[us]) { //supporting a passer from behind
                result->add(ROOK_TARRASCH_SUPPORT);
            }
            if (front[them] & *pos->pawns[them]) { //attacking a passer from behind
                result->add(ROOK_TARRASCH_ATTACK);
            }
            if (front[them] & *pos->pawns[us]) { //supporting from the wrong side
                result->add(ROOK_WRONG_TARRASCH_SUPPORT);
            }
            if (front[us] & *pos->pawns[them]) { //attacking from the wrong side
                result->add(ROOK_WRONG_TARRASCH_ATTACK);
            }
        }

    }
    return result;
}

inline TScore * evaluateShelter(TSearch * sd, bool us) {
    TScore * result = &sd->stack->queen_score[us];
    result->clear();
    TBoard * pos = sd->pos;
    if (*pos->queens[us] == 0) {
        return result;
    }
    bool them = !us;
    result->sub(sd->stack->shelter_score[them]);
    return result;
}

inline TScore * evaluatePassers(TSearch * sd, bool us) {
    TScore * result = &sd->stack->passer_score[us];
    result->clear();
    if (sd->stack->phase <= 10 || sd->stack->passers == 0
            || (sd->stack->passers & *sd->pos->pawns[us]) == 0) {
        return result;
    }
    bool them = !us;
    U64 passers = sd->stack->passers & *sd->pos->pawns[us];
    int unstoppable = 0;
    bool pVsK = sd->pos->getPieces(them) == 0;
    while (passers) {
        int sq = POP(passers);
        if (pVsK) {
            unstoppable = MAX(unstoppable, evaluatePasserVsK(sd, us, sq));
        }
        if (BIT(sq) & SIDE[us]) {
            continue;
        }
        int ix = us == WHITE ? FLIP_SQUARE(sq) : sq;
        int bonus = PASSED_PAWN[ix].eg >> 1;
        int to = forwardSq(sq, us);
        do {
            if (BIT(to) & sd->pos->allPieces) {
                break; //blocked
            }
            result->add(0, bonus);
            sd->pos->allPieces ^= BIT(sq); //to include rook/queen xray attacks from behind
            U64 attacks = sd->pos->attacksTo(to);
            sd->pos->allPieces ^= BIT(sq);
            U64 defend = attacks & sd->pos->all(them);
            U64 support = attacks & sd->pos->all(us);
            if (defend) {
                if (support == 0) {
                    break;
                }
                if (is_1(defend)) {
                    break;
                }
            }
            result->add(0, bonus);
            to = forwardSq(to, us);
        } while (to >= a1 && to <= h8);
    }
    result->add(0, unstoppable); //add the best unstoppable passer score
    return result;
}

int evaluatePasserVsK(TSearch * sd, bool us, int sq) {

    //is the pawn unstoppable by the nme king?
    U64 path = forwardFill(sq, us) ^ BIT(sq);
    if (path & sd->pos->allPieces) {
        return 0; //no, the path is blocked
    }
    bool them = !us;
    int kingThem = *sd->pos->kingPos[them];
    int queening_square = FILE(sq) + us * 56;
    if (us == WHITE) {
        if (sq <= h2) {
            sq += 8;
        }
        if (sd->pos->boardFlags->WTM && sq <= h6) {
            sq += 8;
        }
    } else if (us == BLACK) {
        if (sq >= a7) {
            sq -= 8;
        }
        if (sd->pos->boardFlags->WTM == false && sq >= a3) {
            sq -= 8;
        }
    }
    int prank = RANK(sq);
    int qrank = RANK(queening_square);
    int qfile = FILE(queening_square);
    int pdistance = 1 + ABS(qrank - prank);
    int kingUs = *sd->pos->kingPos[us];
    if ((path & KingMoves[kingUs]) == path) {
        //yes, the promotion path is fully defended and not blocked by our own King
        return 700 - pdistance;
    }

    int krank = RANK(kingThem);
    int kfile = FILE(kingThem);
    int kdistance1 = ABS(qrank - krank);
    int kdistance2 = ABS(qfile - kfile);
    int kdistance = MAX(kdistance1, kdistance2);
    if (pdistance < kdistance) {
        //yes, the nme king is too far away
        return 700 - pdistance;
    }
    return 0;
}

