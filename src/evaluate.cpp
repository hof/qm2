#include "evaluate.h"
#include "searchdata.h"
#include "defs.h"
#include "score.h"

int evaluate(TSearchData * searchData, int alpha, int beta) {

    if (searchData->stack->evaluationScore != SCORE_INVALID) {
        return searchData->stack->evaluationScore;
    }

    int result = 0;
    TBoard * pos = searchData->pos;

    result += evaluateMaterial(searchData)->get(searchData->stack->gamePhase);
    result += pos->boardFlags->pct.get(searchData->stack->gamePhase);
    result += evaluatePawns(searchData)->get(searchData->stack->gamePhase);

    evaluateKingShelter(searchData);
    result += searchData->stack->scores[SCORE_SHELTER_W].get(searchData->stack->gamePhase);
    result -= searchData->stack->scores[SCORE_SHELTER_B].get(searchData->stack->gamePhase);

    if (searchData->learnParam == 1) { //learning
        result += evaluateExp(searchData)->get(searchData->stack->gamePhase);
    }
    result &= GRAIN;
    result = pos->boardFlags->WTM ? result : -result;
    searchData->stack->evaluationScore = result;


    assert(searchData->stack->scores[SCORE_PAWNS].mg > -VQUEEN && searchData->stack->scores[SCORE_PAWNS].mg < VQUEEN);
    assert(searchData->stack->scores[SCORE_MATERIAL].mg > -VKING && searchData->stack->scores[SCORE_PAWNS].mg < VKING);
    assert(result > -VKING && result < VKING);

    return result;
}

/**
 * Experimental evaluation for learning
 * @param searchData search meta-data object
 * @return score the evaluation score
 */
TScore * evaluateExp(TSearchData * searchData) {
    TScore * result = &searchData->stack->scores[SCORE_EXP];
    result->clear();

    return result;
}

/**
 * Evaluate material score and set the current game phase
 * @param searchData search meta-data object
 */
TScore * evaluateMaterial(TSearchData * searchData) {

    /*
     * 1. Get the score from the last stack record if the previous move was quiet, 
     *    so the material balance did not change. This is easy to verify with 
     *    the material hash
     */
    if (searchData->pos->currentPly > 0 &&
            (searchData->pos->boardFlags - 1)->materialHash == searchData->pos->boardFlags->materialHash
            && (searchData->stack - 1)->evaluationScore != SCORE_INVALID) {
        searchData->stack->scores[SCORE_MATERIAL] = (searchData->stack - 1)->scores[SCORE_MATERIAL];
        searchData->stack->gamePhase = (searchData->stack - 1)->gamePhase;
        return &searchData->stack->scores[SCORE_MATERIAL];
    }

    /*
     * 2. Probe the hash table for the material score
     */
    searchData->hashTable->mtLookup(searchData);
    if (searchData->stack->scores[SCORE_MATERIAL].valid()) {
        return &searchData->stack->scores[SCORE_MATERIAL];
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


    // Calculate game phase
    int phase = MAX_GAMEPHASES
            - (wminors + bminors) /* max: 8 */
            - (wrooks + brooks) /* max:4 */
            - 2 * (wqueens + bqueens) /* max: 4 */;
    phase = MAX(0, phase);
    phase = (MAX_GAMEPHASES * phase) / MAX_GAMEPHASES;

    int piecepower = 0;

    // Knights
    if (wknights != bknights) {
        piecepower += (wknights - bknights) * VKNIGHT;
        value += factor(wknights, bknights, FKNIGHT_OPPOSING_PAWNS, bpawns, wpawns);
        value += cond(wknights >= 2, bknights >= 2, VKNIGHT_PAIR);
        value += factor(wknights, bknights, KNIGHT_X_PIECECOUNT, wpieces + bpieces);
    }

    //Bishops
    if (wbishops != bbishops) {
        piecepower += (wbishops - bbishops) * VBISHOP;
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

    //Rooks
    if (wrooks != brooks) {
        piecepower += (wrooks - brooks) * VROOK;
        value += cond(wrooks >= 2, brooks >= 2, VROOKPAIR);
        value += factor(wrooks, brooks, ROOK_OPPOSING_PAWNS, bpawns, wpawns);
    }

    //Queens
    if (wqueens != bqueens) {
        piecepower += (wqueens - bqueens) * VQUEEN;
        value += cond(wqueens && wrooks, bqueens && brooks, VQUEEN_AND_ROOKS);
        value += factor(wqueens, bqueens, QUEEN_MINORCOUNT, wminors + bminors);
    }

    //Bonus for having more "piece power" (excluding pawns)
    piecepower += value;
    value += cond(wpawns && piecepower > MATERIAL_AHEAD_TRESHOLD,
            bpawns && piecepower < -MATERIAL_AHEAD_TRESHOLD,
            PIECEPOWER_AHEAD,
            piecepower / VPAWN,
            -piecepower / VPAWN);

    //Pawns
    if (wpawns != bpawns) {
        value += cond(!wpawns, !bpawns, VNOPAWNS); //penalty for not having pawns (difficult to win)
    }

    /* 
     * Endgame adjustment: 
     * 1)If ahead, but no pawns and no mating material the score is draw
     */
    if (!wpawns || !bpawns) {
        if (!wpawns && value > 0 && (piecepower < 2 * VPAWN
                || (wpieces == 1 && wminors == 1)
                || (wpieces == 2 && wknights == 2)
                || (wpieces == wminors && bpieces == bminors && wminors < 3 && bminors < 3 && wminors && bminors))) {
            value = SCORE_DRAW;
        }
        if (!bpawns && value < 0 && (piecepower > -2 * VPAWN
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
            && piecepower == 0
            && wpieces <= 3
            && wpieces == bpieces
            && wminors < 2
            && bminors < 2
            && ABS(value) < 2 * VPAWN) {
        value += DRAWISH_QR_ENDGAME;
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
     * Store and return
     */
    searchData->stack->scores[SCORE_MATERIAL] = value;
    searchData->stack->gamePhase = phase;
    searchData->hashTable->mtStore(searchData, value, phase);
    return &searchData->stack->scores[SCORE_MATERIAL];
}

void init_pct(TSCORE_PCT & pct) {
    TScore scores[64];
    TScore vpawn(VPAWN - 10, VPAWN);
    TScore vknight(VKNIGHT, VKNIGHT - 5);
    TScore vbishop(VBISHOP, VBISHOP);
    TScore vrook(VROOK, VROOK);
    TScore vqueen(VQUEEN, VQUEEN);
    TScore vking(VKING, VKING);
    TScore PAWN_OFFSET(-6, -8);
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
            4, 5, 7, 7, 7, 7, 5, 4,
            4, 5, 7, 7, 7, 7, 5, 4,
            3, 4, 5, 7, 7, 5, 4, 3,
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
        scores[sq].set(0);
        if ((bbsq & RANK_1) || (bbsq & RANK_8)) {
            continue;
        }
        if (bbsq & RANK_2) { //extra mobility
            scores[sq].add_ix64(&mobility_scale, FLIP_SQUARE(FILE(sq) + 16));
        }
        if (bbsq & (FILE_A | FILE_H)) {
            scores[sq].add(-10);
        }
        int rank = RANK(sq) - 1;
        scores[sq].eg += 2 * rank*rank; //moving pawns forward to promote

        if (sq == e2 || sq == d2) {
            scores[sq].mg -= 10;
        }
        if (sq == c2 || sq == f2) {
            scores[sq].mg -= 5;
        }
        if (bbsq & FRONTFILL(CENTER)) {
            scores[sq].add(5);
        }
        U64 caps = WPawnCaptures[sq] | WPawnMoves[sq];
        while (caps) {
            int ix = POP(caps);
            scores[sq].add_ix64(&mobility_scale, FLIP_SQUARE(ix));
        }
        scores[sq].add(vpawn);
        scores[sq].add(PAWN_OFFSET);
        scores[sq].round();
        pct[WPAWN][sq].set(scores[sq]);
        pct[BPAWN][FLIP_SQUARE(sq)].nset(scores[sq]);
    }

    //Knight
    for (int sq = a1; sq < 64; sq++) {
        scores[sq].set(0);
        U64 caps = KnightMoves[sq];
        while (caps) {
            int ix = POP(caps);
            scores[sq].add_ix64(&mobility_scale, FLIP_SQUARE(ix));
        }
        scores[sq].mul(1.5); //mobility is extra important because knights move slow
        scores[sq].add(vknight);
        scores[sq].add(KNIGHT_OFFSET);
        scores[sq].round();
        pct[WKNIGHT][sq].set(scores[sq]);
        pct[BKNIGHT][FLIP_SQUARE(sq)].nset(scores[sq]);
    }

    //Bishop
    for (int sq = a1; sq < 64; sq++) {
        scores[sq].set(0);
        U64 caps = BishopMoves[sq];
        while (caps) {
            int ix = POP(caps);
            scores[sq].add_ix64(&mobility_scale, FLIP_SQUARE(ix));
        }
        scores[sq].mul(0.5); //mobility is less important because bishops move fast
        scores[sq].add(vbishop);
        scores[sq].add(BISHOP_OFFSET);
        scores[sq].round();
        pct[WBISHOP][sq].set(scores[sq]);
        pct[BBISHOP][FLIP_SQUARE(sq)].nset(scores[sq]);
    }

    //Rook
    for (int sq = a1; sq < 64; sq++) {
        scores[sq].set(0);
        U64 bbsq = BIT(sq);
        U64 caps = RookMoves[sq];
        while (caps) {
            int ix = POP(caps);
            scores[sq].add_ix64(&mobility_scale, FLIP_SQUARE(ix));
        }
        scores[sq].mg *= 0.25; //mobility is less important because rooks move fast
        scores[sq].eg *= 0.5; //mobility is less important because rooks move fast

        if (bbsq & (RANK_7 | RANK_8)) {
            scores[sq].mg += 20; //extra bonus for 7th / 8th rank
            scores[sq].eg += 5;
        }
        if (bbsq & (RANK_6)) {
            scores[sq].mg += 5;
            scores[sq].eg += 2;
        }
        scores[sq].add(vrook);
        scores[sq].add(ROOK_OFFSET);
        scores[sq].round();
        pct[WROOK][sq].set(scores[sq]);
        pct[BROOK][FLIP_SQUARE(sq)].nset(scores[sq]);
    }

    //Queen
    for (int sq = a1; sq < 64; sq++) {
        scores[sq].set(0);
        U64 caps = QueenMoves[sq];
        while (caps) {
            int ix = POP(caps);
            scores[sq].add_ix64(&mobility_scale, FLIP_SQUARE(ix));
        }
        scores[sq].mg *= 0.15; //mobility is less important because queens move fast
        scores[sq].eg *= 0.25; //mobility is less important because queens move fast
        scores[sq].add(vqueen);
        scores[sq].add(QUEEN_OFFSET);
        scores[sq].round();
        pct[WQUEEN][sq].set(scores[sq]);
        pct[BQUEEN][FLIP_SQUARE(sq)].nset(scores[sq]);
    }


    //King
    for (int sq = a1; sq < 64; sq++) {
        scores[sq].set(0);
        U64 caps = KingMoves[sq];
        while (caps) {
            int ix = POP(caps);
            scores[sq].add_ix64(&mobility_scale, FLIP_SQUARE(ix));
        }
        scores[sq].mg = 0;
        scores[sq].add(vking);
        scores[sq].add(KING_OFFSET);
        scores[sq].round();
        pct[WKING][sq].set(scores[sq]);
        pct[BKING][FLIP_SQUARE(sq)].nset(scores[sq]);
    }
}

/**
 * Evaluate pawn structure score 
 * @param searchData search metadata object
 */

TScore * evaluatePawns(TSearchData * searchData) {

    /*
     * 1. Get the score from the last stack record if the latest move did not
     *    involve any pawns. This is easy to check with the pawn hash 
     */
    if (searchData->pos->currentPly > 0
            && searchData->pos->boardFlags->pawnHash == (searchData->pos->boardFlags - 1)->pawnHash
            && (searchData->stack - 1)->evaluationScore != SCORE_INVALID) {
        assert(searchData->stack != searchData->rootStack);
        searchData->stack->scores[SCORE_PAWNS] = (searchData->stack - 1)->scores[SCORE_PAWNS];
        return &searchData->stack->scores[SCORE_PAWNS];
    }

    /*
     * 2. Probe the hash table for the pawn score
     */
    searchData->hashTable->ptLookup(searchData);
    if (searchData->stack->scores[SCORE_PAWNS].valid()) {
        return &searchData->stack->scores[SCORE_PAWNS];
    }

    /*
     * 3. Calculate pawn evaluation score
     */
    TBoard * pos = searchData->pos;
    TScore pawnScore;
    pawnScore = 0;

    U64 passers = 0;
    U64 openW = FULL_BOARD;
    U64 openB = FULL_BOARD;
    U64 allPawns = pos->whitePawns | pos->blackPawns;
    TPiecePlacement * wPawns = &pos->pieces[WPAWN];
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
                pawnScore.add(ISOLATED_OPEN_PAWN[tSq]);
            } else {
                pawnScore.add(ISOLATED_PAWN[tSq]);
            }
        }
        if (doubled) {
            pawnScore.add(DOUBLED_PAWN[tSq]);
        }
        if (passed) {
            pawnScore.add(PASSED_PAWN[tSq]);
            passers |= sqBit;
        }
    }

    TPiecePlacement * bPawns = &pos->pieces[BPAWN];
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
                pawnScore.add(-ISOLATED_OPEN_PAWN[tSq]);
            } else {
                pawnScore.add(-ISOLATED_PAWN[tSq]);
            }
        }
        if (doubled) {
            pawnScore.add(-DOUBLED_PAWN[tSq]);
        }
        if (passed) {
            pawnScore.add(-PASSED_PAWN[tSq]);
            passers |= sqBit;
        }

    }

    if (passers) {
        U64 passersW = passers & pos->whitePawns;
        while (passersW) {
            int sq = POP(passersW);
            U64 mask = passers & pos->whitePawns & KingMoves[sq] & NEIGHBOUR_FILES[FILE(sq)];
            if (mask) {
                pawnScore.add(CONNECED_PASSED_PAWN[FLIP_SQUARE(sq)]);

            }
        }
        U64 passersB = passers & pos->blackPawns;
        while (passersB) {
            int sq = POP(passersB);
            U64 mask = passers & pos->blackPawns & KingMoves[sq] & NEIGHBOUR_FILES[FILE(sq)];
            if (mask) {
                pawnScore.add(-CONNECED_PASSED_PAWN[sq]);
            }
        }
    }

    searchData->stack->scores[SCORE_PAWNS] = pawnScore;
    searchData->hashTable->ptStore(searchData, pawnScore);
    return &searchData->stack->scores[SCORE_PAWNS];
}

void evaluateKingShelter(TSearchData * searchData) {

    TScore score_w;
    TScore score_b;
    assert(score_w.mg == 0 && score_w.eg == 0);
    assert(score_b.mg == 0 && score_b.eg == 0);

    TBoard * pos = searchData->pos;
    if (pos->blackQueens) {
        /* 
         * Get the score from the stack if
         * - the previous position was evaluated
         * - the king did not move
         * - castling rights for white are the same
         * - previous position also had a black queen
         * - pawn structure did not change
         */
        if (searchData->pos->currentPly > 0
                && (searchData->stack - 1)->evaluationScore != SCORE_INVALID
                && searchData->pos->boardFlags->pawnHash == (searchData->pos->boardFlags - 1)->pawnHash) {
            TMove * prevMove = &(searchData->stack - 1)->move;
            if (prevMove->piece != WKING && prevMove->promotion != BQUEEN &&
                    (pos->boardFlags->castlingFlags & CASTLE_WHITE) == ((searchData->pos->boardFlags - 1)->castlingFlags & CASTLE_WHITE)) {
                score_w = (searchData->stack - 1)->scores[SCORE_SHELTER_W];
                goto BLACK;
            }
        }

        /*
         * Calculate king shelter score
         */
        int kpos = *pos->whiteKingPos;
        score_w.add_ix64(&SHELTER_KPOS, FLIP_SQUARE(kpos));

        //1. reward having the right to castle
        if (pos->boardFlags->castlingFlags & CASTLE_K
                && ((pos->Matrix[h2] == WPAWN && pos->Matrix[g2] == WPAWN)
                || (pos->Matrix[f2] == WPAWN && pos->Matrix[h2] == WPAWN && pos->Matrix[g3] == WPAWN)
                || (pos->Matrix[h3] == WPAWN && pos->Matrix[g2] == WPAWN && pos->Matrix[f2] == WPAWN))) {
            score_w.add(&SHELTER_CASTLING_KINGSIDE);
        } else if (pos->boardFlags->castlingFlags & CASTLE_Q
                && ((pos->Matrix[a2] == WPAWN && pos->Matrix[b2] == WPAWN && pos->Matrix[c2] == WPAWN)
                || (pos->Matrix[a2] == WPAWN && pos->Matrix[b3] == WPAWN && pos->Matrix[c2] == WPAWN))) {
            score_w.add(&SHELTER_CASTLING_QUEENSIDE);
        }

        //2. reward having pawns in front of the king
        U64 kingFront = FORWARD_RANKS[RANK(kpos)] & PAWN_SCOPE[FILE(kpos)];
        U64 shelterPawns = kingFront & pos->whitePawns;

        while (shelterPawns) {
            int sq = POP(shelterPawns);
            score_w.add_ix64(&SHELTER_PAWN, FLIP_SQUARE(sq));
        }

        //3. penalize (half)open files on the king
        U64 open = ~FILEFILL(pos->blackPawns) & kingFront & RANK_8;
        if (open) {
            int pc = popCount(open);
            score_w.add_ix4(&SHELTER_OPEN_FILES, pc);
        }
        //store score in eval table

    }
BLACK:
    if (pos->whiteQueens) {
        /* 
         * Get the score from the stack if
         * - the previous position was evaluated
         * - the king did not move
         * - castling rights for black are the same
         * - previous position also had a white queen
         * - pawn structure did not change
         */
        if (searchData->pos->currentPly > 0
                && (searchData->stack - 1)->evaluationScore != SCORE_INVALID
                && searchData->pos->boardFlags->pawnHash == (searchData->pos->boardFlags - 1)->pawnHash) {
            TMove * prevMove = &(searchData->stack - 1)->move;
            if (prevMove->piece != BKING && prevMove->promotion != WQUEEN &&
                    (pos->boardFlags->castlingFlags & CASTLE_BLACK) == ((searchData->pos->boardFlags - 1)->castlingFlags & CASTLE_BLACK)) {
                score_b = (searchData->stack - 1)->scores[SCORE_SHELTER_B];
                goto RETURN;
            }
        }
        /*
         * Calculate king shelter score
         */
        int kpos = *pos->blackKingPos;
        score_b.add_ix64(&SHELTER_KPOS, kpos);

        //1. reward having the right to castle safely
        if (pos->boardFlags->castlingFlags & CASTLE_k
                && ((pos->Matrix[h7] == BPAWN && pos->Matrix[g7] == BPAWN)
                || (pos->Matrix[f7] == BPAWN && pos->Matrix[h7] == BPAWN && pos->Matrix[g6] == BPAWN)
                || (pos->Matrix[h6] == BPAWN && pos->Matrix[g7] == BPAWN && pos->Matrix[f7] == BPAWN))) {
            score_b.add(&SHELTER_CASTLING_KINGSIDE);
        } else if (pos->boardFlags->castlingFlags & CASTLE_q
                && ((pos->Matrix[a7] == BPAWN && pos->Matrix[b7] == BPAWN && pos->Matrix[c7] == BPAWN)
                || (pos->Matrix[a7] == BPAWN && pos->Matrix[b6] == BPAWN && pos->Matrix[c7] == BPAWN))) {
            score_b.add(&SHELTER_CASTLING_QUEENSIDE);
        }

        //2. reward having pawns in front of the king
        U64 kingFront = BACKWARD_RANKS[RANK(kpos)] & PAWN_SCOPE[FILE(kpos)];
        U64 shelterPawns = kingFront & pos->blackPawns;
        while (shelterPawns) {
            int sq = POP(shelterPawns);
            score_b.add_ix64(&SHELTER_PAWN, sq);
        }

        //3. penalize (half)open files on the king
        U64 open = (~FILEFILL(pos->whitePawns)) & kingFront & RANK_1;
        if (open) { //half open
            score_b.add_ix4(&SHELTER_OPEN_FILES, popCount(open));
        }


    }
RETURN:
    searchData->stack->scores[SCORE_SHELTER_W] = score_w;
    searchData->stack->scores[SCORE_SHELTER_B] = score_b;
}

