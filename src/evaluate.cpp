#include "evaluate.h"
#include "search.h"
#include "defs.h"
#include "score.h"

TSCORE_PST PST; //piece square table

inline short evaluateMaterial(TSearch * sd);
inline TScore * evaluatePawnsAndKings(TSearch * sd);
inline TScore * evaluateKnights(TSearch * sd, bool white);
inline TScore * evaluateBishops(TSearch * sd, bool white);
inline TScore * evaluateRooks(TSearch * sd, bool white);
inline TScore * evaluateQueens(TSearch * sd, bool white);
inline TScore * evaluatePassers(TSearch * sd, bool white);
inline int evaluatePasserVsK(TSearch * sd, bool white, int sq);

/*******************************************************************************
 * Material Evaluation Values 
 *******************************************************************************/

enum MaterialValues {
    MATERIAL_AHEAD_TRESHOLD = 80,
    VNOPAWNS = -40,
    VBISHOPPAIR = 50,
    DRAWISH_QR_ENDGAME = -20,
    DRAWISH_OPP_BISHOPS = -50
};

const TScore SVPAWN = S(VPAWN, VPAWN); //middle and endgame values
const TScore SVKNIGHT = S(VKNIGHT, VKNIGHT);
const TScore SVBISHOP = S(VBISHOP, VBISHOP);
const TScore SVROOK = S(VROOK, VROOK);
const TScore SVQUEEN = S(VQUEEN, VQUEEN);
const TScore SVKING = S(VKING, VKING);

const TScore PIECE_SCORE[13] = {
    S(0, 0), SVPAWN, SVKNIGHT, SVBISHOP, SVROOK, SVQUEEN, SVKING,
    SVPAWN, SVKNIGHT, SVBISHOP, SVROOK, SVQUEEN, SVKING
};

const short TRADEDOWN_PIECES[MAX_PIECES + 1] = {
    80, 60, 40, 20, 10, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0
};

const short TRADEDOWN_PAWNS[9] = {
    -60, -40, -20, -10, -5, 0, 5, 10, 15
};

/*******************************************************************************
 * Pawn Values 
 *******************************************************************************/

const TScore DEFENDED_PAWN[2] = {S(0, 4), S(4, 8)};
const TScore ISOLATED_PAWN[2] = {S(-10, -20), S(-20, -20)};
const TScore WEAK_PAWN[2] = {S(-8, -16), S(-16, -16)};
const TScore DOUBLED_PAWN = S(-4, -8);

const TScore PASSED_PAWN[64] = {
    S(0, 0), S(0, 0), S(0, 0), S(0, 0), S(0, 0), S(0, 0), S(0, 0), S(0, 0),
    S(60, 180), S(60, 180), S(60, 180), S(60, 180), S(60, 180), S(60, 180), S(60, 180), S(60, 180),
    S(40, 80), S(40, 80), S(40, 80), S(40, 80), S(40, 80), S(40, 80), S(40, 80), S(40, 80),
    S(30, 50), S(30, 50), S(30, 50), S(30, 50), S(30, 50), S(30, 50), S(30, 50), S(30, 50),
    S(20, 30), S(20, 30), S(20, 30), S(20, 30), S(20, 30), S(20, 30), S(20, 30), S(20, 30),
    S(20, 20), S(20, 20), S(20, 20), S(20, 20), S(20, 20), S(20, 20), S(20, 20), S(20, 20),
    S(20, 20), S(20, 20), S(20, 20), S(20, 20), S(20, 20), S(20, 20), S(20, 20), S(20, 20),
    S(0, 0), S(0, 0), S(0, 0), S(0, 0), S(0, 0), S(0, 0), S(0, 0), S(0, 0),
};

const TScore CONNECED_PASSED_PAWN[64] = {
    S(0, 0), S(0, 0), S(0, 0), S(0, 0), S(0, 0), S(0, 0), S(0, 0), S(0, 0),
    S(95, 95), S(95, 95), S(95, 95), S(95, 95), S(95, 95), S(95, 95), S(95, 95), S(95, 95),
    S(50, 50), S(50, 50), S(50, 50), S(50, 50), S(50, 50), S(50, 50), S(50, 50), S(50, 50),
    S(30, 30), S(30, 30), S(30, 30), S(30, 30), S(30, 30), S(30, 30), S(30, 30), S(30, 30),
    S(10, 10), S(10, 10), S(10, 10), S(10, 10), S(10, 10), S(10, 10), S(10, 10), S(10, 10),
    S(5, 5), S(5, 5), S(5, 5), S(5, 5), S(5, 5), S(5, 5), S(5, 5), S(5, 5),
    S(5, 5), S(5, 5), S(5, 5), S(5, 5), S(5, 5), S(5, 5), S(5, 5), S(5, 5),
    S(0, 0), S(0, 0), S(0, 0), S(0, 0), S(0, 0), S(0, 0), S(0, 0), S(0, 0),
};

const TScore CANDIDATE[64] = {
    S(0, 0), S(0, 0), S(0, 0), S(0, 0), S(0, 0), S(0, 0), S(0, 0), S(0, 0),
    S(0, 0), S(0, 0), S(0, 0), S(0, 0), S(0, 0), S(0, 0), S(0, 0), S(0, 0),
    S(32, 50), S(32, 50), S(32, 50), S(32, 50), S(32, 50), S(32, 50), S(32, 50), S(32, 50),
    S(24, 40), S(24, 40), S(24, 40), S(24, 40), S(24, 40), S(24, 40), S(24, 40), S(24, 40),
    S(18, 30), S(18, 30), S(18, 30), S(18, 30), S(18, 30), S(18, 30), S(18, 30), S(18, 30),
    S(10, 10), S(10, 10), S(10, 10), S(10, 10), S(10, 10), S(10, 10), S(10, 10), S(10, 10),
    S(10, 10), S(10, 10), S(10, 10), S(10, 10), S(10, 10), S(10, 10), S(10, 10), S(10, 10),
    S(0, 0), S(0, 0), S(0, 0), S(0, 0), S(0, 0), S(0, 0), S(0, 0), S(0, 0),
};

const TScore SHELTER_KPOS[64] = {
    S(-65, 0), S(-75, 0), S(-85, 0), S(-95, 0), S(-90, 0), S(-80, 0), S(-70, 0), S(-60, 0),
    S(-55, 0), S(-65, 0), S(-75, 0), S(-85, 0), S(-80, -0), S(-70, 0), S(-60, 0), S(-50, 0),
    S(-45, 0), S(-55, 0), S(-65, 0), S(-75, 0), S(-70, 0), S(-60, 0), S(-50, 0), S(-40, 0),
    S(-35, 0), S(-45, 0), S(-55, 0), S(-65, 0), S(-60, 0), S(-50, 0), S(-40, 0), S(-30, 0),
    S(-25, 0), S(-35, 0), S(-45, 0), S(-55, 0), S(-50, 0), S(-40, 0), S(-30, 0), S(-20, 0),
    S(-10, 0), S(-20, 0), S(-30, 0), S(-40, 0), S(-40, 0), S(-30, 0), S(-20, 0), S(-10, 0),
    S(15, 0), S(5, 0), S(-10, 0), S(-20, 0), S(-20, 0), S(-10, 0), S(10, 0), S(25, 0),
    S(40, 0), S(40, 0), S(25, 0), S(-10, 0), S(-10, 0), S(25, 0), S(50, 0), S(50, 0)

};

const TScore SHELTER_PAWN[64] = {
    S(0, 0), S(0, 0), S(0, 0), S(0, 0), S(0, 0), S(0, 0), S(0, 0), S(0, 0),
    S(2, 4), S(2, 4), S(2, 4), S(2, 4), S(2, 4), S(2, 4), S(2, 4), S(2, 4),
    S(2, 4), S(2, 4), S(2, 4), S(2, 4), S(2, 4), S(2, 4), S(2, 4), S(2, 4),
    S(2, 4), S(2, 4), S(2, 4), S(2, 4), S(2, 4), S(2, 4), S(2, 4), S(2, 4),
    S(5, 6), S(5, 6), S(5, 4), S(5, 4), S(5, 4), S(5, 4), S(15, 6), S(15, 6),
    S(15, 8), S(15, 8), S(10, 4), S(2, 4), S(2, 4), S(10, 4), S(15, 8), S(15, 8),
    S(30, 10), S(30, 10), S(25, 4), S(10, 4), S(5, 4), S(15, 4), S(30, 10), S(30, 10),
    S(0, 0), S(0, 0), S(0, 0), S(0, 0), S(0, 0), S(0, 0), S(0, 0), S(0, 0),
};

const TScore STORM_PAWN[64] = {
    S(0, 0), S(0, 0), S(0, 0), S(0, 0), S(0, 0), S(0, 0), S(0, 0), S(0, 0),
    S(0, 2), S(0, 2), S(0, 2), S(0, 2), S(0, 2), S(0, 2), S(0, 2), S(0, 2),
    S(0, 2), S(0, 2), S(0, 2), S(0, 2), S(0, 2), S(0, 2), S(0, 2), S(0, 2),
    S(2, 4), S(2, 4), S(2, 4), S(2, 4), S(2, 4), S(2, 4), S(2, 4), S(2, 4),
    S(10, 4), S(10, 4), S(10, 4), S(10, 4), S(10, 4), S(10, 4), S(10, 4), S(10, 4),
    S(30, 10), S(30, 10), S(30, 10), S(20, 10), S(20, 10), S(20, 10), S(30, 10), S(30, 10),
    S(20, 10), S(20, 10), S(20, 10), S(20, 10), S(20, 10), S(20, 10), S(20, 10), S(20, 10),
    S(0, 0), S(0, 0), S(0, 0), S(0, 0), S(0, 0), S(0, 0), S(0, 0), S(0, 0),
};

const TScore SHELTER_OPEN_FILES[4] = {
    S(0, 0), S(-30, -5), S(-60, -10), S(-120, -15)
};

const TScore SHELTER_OPEN_ATTACK_FILES[4] = {
    S(0, 0), S(-10, 0), S(-20, -5), S(-40, -10)
};

const TScore SHELTER_OPEN_EDGE_FILE = S(-110, -10);

const TScore SHELTER_CASTLING_KINGSIDE = S(60, 20);

const TScore SHELTER_CASTLING_QUEENSIDE = S(50, 20);

/*******************************************************************************
 * Knight Values 
 *******************************************************************************/

const TScore KNIGHT_MOBILITY[9] = {
    S(-32, -32), S(-24, -24), S(-16, -16), S(-8, -8),
    S(0, 0), S(4, 4), S(8, 8), S(12, 12), S(16, 16)
};

/*******************************************************************************
 * Bishop Values 
 *******************************************************************************/

const TScore BISHOP_MOBILITY[14] = {
    S(-24, -24), S(-18, -18), S(-12, -12), S(-6, -6),
    S(0, 0), S(2, 2), S(4, 4), S(6, 6), S(8, 8),
    S(10, 10), S(12, 12), S(14, 14), S(16, 16), S(18, 18)
};

const TScore TRAPPED_BISHOP(-80, -120);

/*******************************************************************************
 * Rook Values 
 *******************************************************************************/

const TScore ROOK_7TH = S(20, 30);
const TScore ROOK_1ST = S(10, 0); //back rank protection
const TScore ROOK_SEMIOPEN_FILE = S(12, 12);
const TScore ROOK_OPEN_FILE = S(24, 24);
const TScore ROOK_GOOD_SIDE = S(10, 30);
const TScore ROOK_WRONG_SIDE = S(-10, -20);

const TScore ROOK_MOBILITY[15] = {
    S(-40, -40), S(-20, -20), S(-10, -10), S(-8, -8),
    S(-6, -6), S(-4, -4), S(-2, -2), S(0, 0), S(0, 0),
    S(2, 2), S(4, 4), S(6, 6), S(8, 8), S(10, 10), S(12, 12)
};


/*******************************************************************************
 * Queen Values
 *******************************************************************************/

const TScore QUEEN_MOBILITY[29] = {
    S(-20, -40), S(-15, -20), S(-10, -10), S(-8, -8), S(-6, -6), S(-4, -4), S(-2, -2), S(0, 0),
    S(0, 0), S(0, 0), S(0, 0), S(0, 0), S(0, 0), S(0, 1), S(0, 2), S(0, 3),
    S(0, 4), S(0, 5), S(0, 6), S(0, 7), S(0, 8), S(0, 9), S(0, 10), S(0, 11),
    S(0, 12), S(0, 13), S(0, 14), S(0, 15), S(0, 16)
};

/*******************************************************************************
 * Main evaluation function
 *******************************************************************************/

int evaluate(TSearch * sd, int alpha, int beta) {
    if (sd->stack->eval_result != SCORE_INVALID) {
        return sd->stack->eval_result;
    }

    sd->stack->equal_pawns = sd->pos->currentPly > 0
            && sd->pos->boardFlags->pawnHash == (sd->pos->boardFlags - 1)->pawnHash
            && (sd->stack - 1)->eval_result != SCORE_INVALID;

    int result = evaluateMaterial(sd); //sets stack->phase (required)
    TScore * score = &sd->stack->eval_score;
    score->set(evaluatePawnsAndKings(sd)); //sets mobility masks (required)
    score->add(evaluateKnights(sd, WHITE));
    score->sub(evaluateKnights(sd, BLACK));
    score->add(evaluateBishops(sd, WHITE));
    score->sub(evaluateBishops(sd, BLACK));
    score->add(evaluateRooks(sd, WHITE));
    score->sub(evaluateRooks(sd, BLACK));
    score->add(evaluateQueens(sd, WHITE));
    score->sub(evaluateQueens(sd, BLACK));
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
    int pc = WKING;
    return sd->pos->pieces[pc].count == 0 && sd->pos->pieces[pc + WKING].count == 0;
}

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

    // penalty for not having pawns at all - makes it hard to win
    if (wpawns == 0) {
        result.add(VNOPAWNS);
    }
    if (bpawns == 0) {
        result.sub(VNOPAWNS);
    }

    int value = result.get(phase);

    /*
     * Special cases
     */

    if (!wpieces && !bpieces) { //only pawns left, the side with more pawns wins
        value += value;
    } else if (piecePower >= VROOK && value > 2 * VPAWN) { //winning edge and having mating material
        value += value >> 2;
    } else if (piecePower <= -VROOK && value < -2 * VPAWN) {
        value -= (-value) >> 2;
    } else if (value > 0 && piecePower < VROOK && !wpawns) { //ahead but no mating material (draw)
        value >>= 2;
    } else if (value < 0 && piecePower > -VROOK && !bpawns) {
        value >>= 2;
    } else if (!wpieces && bpieces > 0 && wpawns > 0) { //opponent only has pawns
        value += piecePower >> 1;
    } else if (!bpieces && wpieces > 0 && bpawns > 0) {
        value += piecePower >> 1;
    } else if (value > 0 && piecePower < 0) { //ahead with pawns and behind with pieces
        value -= 20 + (value >> 2); //testeval r3r1k1/5p1p/2pbbBp1/q2p4/p2P4/1P1Q2N1/P1P1RPPP/R5K1 b - - 5 1
    } else if (value < 0 && piecePower > 0) {
        value += 20 + ((-value) >> 2);
    }

    /*
     * Endgame adjustments
     */

    //Rooks and queen endgames are drawish. Reduce any small material advantage.
    if (value
            && wminors < 2
            && bminors < 2
            && wpieces <= 3
            && wpieces > 0
            && bpieces > 0
            && (wrooks || wqueens)
            && (brooks || bqueens)
            && wpieces == bpieces
            && ABS(value) > ABS(DRAWISH_QR_ENDGAME)
            && ABS(value) < 2 * VPAWN
            && ABS(piecePower) < VPAWN / 2) {
        value += cond(value > 0, value < 0, DRAWISH_QR_ENDGAME);
        if (wminors == 0 && bminors == 0 && wpieces <= 1 && bpieces <= 1) { //more drawish
            value += cond(value > 30, value < -30, DRAWISH_QR_ENDGAME >> 1);
        }
    }

    // Opposite  bishop ending is drawish
    if (value && wpieces == 1 && bpieces == 1 && wpawns != bpawns && wbishops && bbishops) {
        if (bool(pos->whiteBishops & BLACK_SQUARES) != bool(pos->blackBishops & BLACK_SQUARES)) {
            //todo: set drawflag 
            value += cond(wpawns>bpawns, bpawns>wpawns, MAX(-(ABS(value) >> 1), DRAWISH_OPP_BISHOPS));
        }
    }

    /*
     * Store and return
     */
    sd->stack->material_score = value;
    sd->stack->phase = phase;
    sd->hashTable->mtStore(sd, value, phase);
    return value;
}

void init_pct_store(TScore scores[], int wpiece) {
    int tot_mg = 0;
    int tot_eg = 0;
    int count = 0;
    for (int sq = a1; sq <= h8; sq++) {
        if (wpiece == WPAWN && (sq < a2 || sq > h7)) {
            continue;
        }
        tot_mg += scores[sq].mg;
        tot_eg += scores[sq].eg;
        count++;
    }
    int avg_mg = tot_mg / count;
    int avg_eg = tot_eg / count;
    for (int sq = a1; sq <= h8; sq++) {
        if (wpiece == WPAWN && (sq < a2 || sq > h7)) {
            scores[sq].clear();
        } else {
            scores[sq].mg -= avg_mg;
            scores[sq].eg -= avg_eg;
            scores[sq].round();
        }
        PST[wpiece][FLIP_SQUARE(sq)].set(scores[sq]);
    }
}

void init_pct() {
    TScore scores[64];
    const short mobility_scale[2][64] = {
        {
            2, 3, 4, 5, 5, 4, 3, 2,
            3, 4, 5, 6, 6, 5, 4, 3,
            4, 5, 6, 7, 7, 6, 5, 4,
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
        }
    };

    //Pawn
    for (int sq = a1; sq < 64; sq++) {
        U64 bbsq = BIT(sq);
        scores[sq].clear();
        if ((bbsq & RANK_1) || (bbsq & RANK_8)) {
            continue;
        }
        U64 caps = WPawnCaptures[sq];
        while (caps) {
            int ix = POP(caps);
            scores[sq].add_ix64(&mobility_scale, FLIP_SQUARE(ix));
        }
        scores[sq].mul(4); //pawns are the most powerful to control squares
    }
    init_pct_store(scores, WPAWN);

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
    init_pct_store(scores, WKNIGHT);

    //Bishop
    for (int sq = a1; sq < 64; sq++) {
        scores[sq].clear();
        U64 caps = BishopMoves[sq];
        while (caps) {
            int ix = POP(caps);
            scores[sq].add_ix64(&mobility_scale, FLIP_SQUARE(ix));
        }
    }
    init_pct_store(scores, WBISHOP);

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
    init_pct_store(scores, WROOK);

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
    init_pct_store(scores, WQUEEN);

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
    init_pct_store(scores, WKING);
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
        sd->stack->mob[WHITE] = (sd->stack - 1)->mob[WHITE];
        sd->stack->mob[BLACK] = (sd->stack - 1)->mob[BLACK];
        return &sd->stack->pawn_score;
    }

    //set mobility masks
    TBoard * pos = sd->pos;
    sd->stack->mob[WHITE] = ~(*pos->pawns[WHITE] | pos->pawnAttacks(BLACK) | *pos->kings[WHITE]);
    sd->stack->mob[BLACK] = ~(*pos->pawns[BLACK] | pos->pawnAttacks(WHITE) | *pos->kings[BLACK]);

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


        pawn_score->add(PST[WPAWN][isq]);

        //std::cout << "pst: " << PRINT_SCORE(PST[WPAWN][isq]);

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

        pawn_score->sub(PST[WPAWN][sq]);

        //std::cout << "pst: " << PRINT_SCORE(PST[WPAWN][sq]);

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


    pawn_score->add(PST[WKING][ISQ(wkpos, WHITE)]);
    pawn_score->sub(PST[WKING][ISQ(bkpos, BLACK)]);

    //support and attack pawns with king in the EG
    pawn_score->add(0, popCount0(KingZone[wkpos] & pos->allPawns()) * 8);
    pawn_score->sub(0, popCount0(KingZone[bkpos] & pos->allPawns()) * 8);

    //support and attack passed pawns even more
    pawn_score->add(0, popCount0(KingZone[wkpos] & passers) * 12);
    pawn_score->sub(0, popCount0(KingZone[bkpos] & passers) * 12);

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
        shelter_score_w->add(SHELTER_OPEN_FILES[popCount0(open & openW)]);
        shelter_score_w->add(SHELTER_OPEN_ATTACK_FILES[popCount0(open & openB)]);
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
        shelter_score_b->add(SHELTER_OPEN_FILES[popCount0(open & openB)]);
        shelter_score_b->add(SHELTER_OPEN_ATTACK_FILES[popCount0(open & openW)]);
        if (open & openB & openW & (FILE_A | FILE_B | FILE_H | FILE_G)) {
            shelter_score_b->add(SHELTER_OPEN_EDGE_FILE);
        }
    }
    sd->stack->passers = passers;
    sd->hashTable->ptStore(sd, pawn_score, shelter_score_w, shelter_score_b, passers);
    return &sd->stack->pawn_score;
}

inline TScore * evaluateKnights(TSearch * sd, bool us) {

    TScore * result = &sd->stack->knight_score[us];
    TBoard * pos = sd->pos;
    if (*pos->knights[us] == 0) {
        result->clear();
        return result;
    }

    /*
     * 2. Get the score from the last stack record if the previous move 
     *   a) did not change the pawn+kings structure (pawn hash) 
     *   b) did not move or capture any bishop
     */
    if (sd->stack->equal_pawns) {
        TMove * prevMove = &(sd->stack - 1)->move;
        if (prevMove->piece != KNIGHT[us] && prevMove->capture != KNIGHT[us]) {
            result->set((sd->stack - 1)->knight_score[us]);
            return result;
        }
    }

    /*
     * 3. Calculate the score and store on the stack
     */
    result->clear();
    TPiecePlacement * pp = &pos->pieces[KNIGHT[us]];
    for (int i = 0; i < pp->count; i++) {
        int sq = pp->squares[i];
        result->add(PST[WKNIGHT][ISQ(sq, us)]);
        U64 moves = KnightMoves[sq] & sd->stack->mob[us];
        result->add(KNIGHT_MOBILITY[popCount0(moves)]);
    }
    return result;
}

inline TScore * evaluateBishops(TSearch * sd, bool us) {

    TScore * result = &sd->stack->bishop_score[us];

    TBoard * pos = sd->pos;
    if (*pos->bishops[us] == 0) {
        result->clear();
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
    result->clear();
    TPiecePlacement * pc = &pos->pieces[BISHOP[us]];
    U64 occ = pos->pawnsAndKings();
    for (int i = 0; i < pc->count; i++) {
        int sq = pc->squares[i];
        result->add(PST[WBISHOP][ISQ(sq, us)]);
        U64 moves = MagicBishopMoves(sq, occ);
        int count = popCount0(moves & sd->stack->mob[us]);
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

    TBoard * pos = sd->pos;
    if (*pos->rooks[us] == 0) {
        result->clear();
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
    result->clear();
    TPiecePlacement * pc = &pos->pieces[ROOK[us]];
    bool them = !us;
    U64 fill[2] = {FILEFILL(pos->blackPawns), FILEFILL(pos->whitePawns)};
    U64 occ = pos->pawnsAndKings();
    for (int i = 0; i < pc->count; i++) {
        int sq = pc->squares[i];
        result->add(PST[WROOK][ISQ(sq, us)]);
        U64 bitSq = BIT(sq);
        if (bitSq & (~fill[us])) {
            if (bitSq & fill[them]) {
                result->add(ROOK_SEMIOPEN_FILE);
            } else {
                result->add(ROOK_OPEN_FILE);
            }
        }
        U64 moves = MagicRookMoves(sq, occ);
        int count = popCount0(moves & sd->stack->mob[us]);
        result->add(ROOK_MOBILITY[count]);

        if ((bitSq & RANK[us][1]) && (BIT(*pos->kingPos[us]) & (RANK[us][1] | RANK[us][2]))) {
            result->add(ROOK_1ST);
        }
        if (bitSq & RANK[us][7] && (BIT(*pos->kingPos[them]) & RANK[us][8])) {
            result->add(ROOK_7TH);
        }

        //Tarrasch rule: place rook behind passers
        if (moves & sd->stack->passers) {
            U64 front[2];
            front[BLACK] = southFill(bitSq) & moves & sd->stack->passers & (WHITE_SIDE | RANK_5);
            front[WHITE] = northFill(bitSq) & moves & sd->stack->passers & (BLACK_SIDE | RANK_4);
            if (front[us] & *pos->pawns[us]) { //supporting a passer from behind
                result->add(ROOK_GOOD_SIDE);
            } else if (front[them] & *pos->pawns[them]) { //attacking a passer from behind
                result->add(ROOK_GOOD_SIDE);
            } else if (front[them] & *pos->pawns[us]) { //supporting from the wrong side
                result->add(ROOK_WRONG_SIDE);
            } else if (front[us] & *pos->pawns[them]) { //attacking from the wrong side
                result->add(ROOK_WRONG_SIDE);
            }
        }

    }
    return result;
}

inline TScore * evaluateQueens(TSearch * sd, bool us) {

    TScore * result = &sd->stack->queen_score[us];

    TBoard * pos = sd->pos;
    if (*pos->queens[us] == 0) {
        result->clear();
        return result;
    }

    /*
     * 2. Get the score from the last stack record if the previous move 
     *   a) did not change the pawn+kings structure (pawn hash) 
     *   b) did not move or capture any bishop
     */
    if (sd->stack->equal_pawns) {
        TMove * prevMove = &(sd->stack - 1)->move;
        if (prevMove->piece != QUEEN[us] && prevMove->capture != QUEEN[us]) {
            result->set((sd->stack - 1)->queen_score[us]);
            return result;
        }
    }

    /*
     * 3. Calculate the score and store on the stack
     */
    bool them = !us;
    result->clear();
    result->sub(sd->stack->shelter_score[them]);
    if (pos->getPieces(us) == *pos->queens[us]) {
        result->half();
    }
    TPiecePlacement * pp = &pos->pieces[QUEEN[us]];
    U64 occ = pos->pawnsAndKings();
    for (int i = 0; i < pp->count; i++) {
        int sq = pp->squares[i];
        result->add(PST[WQUEEN][ISQ(sq, us)]);
        U64 moves = MagicQueenMoves(sq, occ);
        int count = popCount0(moves & sd->stack->mob[us]);
        result->add(QUEEN_MOBILITY[count]);
    }
    return result;
}

inline TScore * evaluatePassers(TSearch * sd, bool us) {
    TScore * result = &sd->stack->passer_score[us];
    result->clear();
    if (sd->stack->phase < 0 || sd->stack->passers == 0
            || (sd->stack->passers & *sd->pos->pawns[us]) == 0) {
        return result;
    }
    bool them = !us;
    U64 passers = sd->stack->passers & *sd->pos->pawns[us];
    int unstoppable = 0;
    bool pVsK = sd->pos->getPieces(them) == 0;
    U64 exclude = SIDE[us] & ~(RANK_4 | RANK_5);
    while (passers) {
        int sq = POP(passers);
        if (pVsK) {
            unstoppable = MAX(unstoppable, evaluatePasserVsK(sd, us, sq));
        }
        if (BIT(sq) & exclude) {
            continue;
        }
        int ix = us == WHITE ? FLIP_SQUARE(sq) : sq;
        TScore bonus;
        bonus.set(PASSED_PAWN[ix]);
        bonus.half();
        int to = forwardSq(sq, us);
        do {
            if (BIT(to) & sd->pos->allPieces) {
                break; //blocked
            }
            result->add(bonus);
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
            result->add(bonus);
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

