#include "evaluate.h"
#include "search.h"
#include "defs.h"
#include "score.h"

//#define PRINT_PAWN_EVAL 
//#define PRINT_KING_SAFETY
//#define PRINT_PASSED_PAWN 

TSCORE_PST PST; //piece square table

inline short evaluateMaterial(TSearch * sd);
inline TScore * evaluatePawnsAndKings(TSearch * sd);
inline TScore * evaluateKnights(TSearch * sd, bool white);
inline TScore * evaluateBishops(TSearch * sd, bool white);
inline TScore * evaluateRooks(TSearch * sd, bool white);
inline TScore * evaluateQueens(TSearch * sd, bool white);
inline TScore * evaluatePassers(TSearch * sd, bool white);
inline int evaluatePasserVsK(TSearch * sd, bool white, int sq);
inline TScore * evaluateKingAttack(TSearch * sd, bool white);

/*******************************************************************************
 * Material Evaluation Values 
 *******************************************************************************/

enum MaterialValues {
    MATERIAL_AHEAD_THRESHOLD = 240, //all values are in centipawns
    VNOPAWNS = -40,
    VBISHOPPAIR = 50,
    DRAWISH_QR_ENDGAME = -20,
    DRAWISH_OPP_BISHOPS = -50
};

const TScore IMBALANCE[9][9] = {//index: major piece units, minor pieces

    //4 major pieces down (-20 pawns))
    { /*-4*/ S(-200, -100), /*-3*/ S(-200, -100), /*-2*/ S(-200, -100), /*-1*/ S(-200, -100),
        /*0 minor pieces (balance) */ S(-200, -100),
        /*+1*/ S(-200, -100), /*+2*/ S(-150, -75), /*+3*/ S(-100, -50), /*+4*/ S(-50, 0)},

    //3 major pieces down
    { /*-4*/ S(-200, -100), /*-3*/ S(-200, -100), /*-2*/ S(-200, -100), /*-1*/ S(-200, -100),
        /*0 minor pieces (balance) */ S(-200, -100),
        /*+1*/ S(-150, -75), /*+2*/ S(-100, -50), /*+3*/ S(-50, 0), /*+4*/ S(50, 0)},

    //2 major pieces down (-10 pawns))
    { /*-4*/ S(-200, -100), /*-3*/ S(-200, -100), /*-2*/ S(-200, -100), /*-1*/ S(-150, -75),
        /*0 minor pieces (balance) */ S(-200, -100),
        /*+1*/ S(-100, -50), /*+2*/ S(-50, 0), /*+3*/ S(50, 0), /*+4*/ S(100, 50)},

    //1 major piece down (-5 pawns))
    { /*-4*/ S(-200, -100), /*-3*/ S(-200, -100), /*-2*/ S(-200, -100), /*-1*/ S(-150, -75),
        /*0 minor pieces (balance) */ S(-100, -50),
        /*+1 (the exchange)*/ S(20, 0), /*+2*/ S(50, 0), /*+3*/ S(75, 0), /*+4*/ S(100, 25)},

    //balance of major pieces
    { /*-4*/ S(-120, -60), /*-3*/ S(-100, -50), /*-2*/ S(-80, -40), /*-1*/ S(-60, -40),
        /*0 minor pieces (balance) */ S(0, 0),
        /*+1*/ S(60, 40), /*+2*/ S(80, 40), /*+3*/ S(100, 50), /*+4*/ S(120, 60)},

    //1 major piece up (+5 pawns))
    { /*-4*/ S(-100, -25), /*-3*/ S(-75, 0), /*-2*/ S(-50, 0), /*-1 the exchange */ S(-20, 0),
        /*0 minor pieces (balance) */ S(100, 50),
        /*+1*/ S(150, 75), /*+2*/ S(200, 100), /*+3*/ S(200, 100), /*+4*/ S(200, 100)},

    //2 major pieces up (+10 pawns))
    { /*-4*/ S(-100, -50), /*-3*/ S(-50, 0), /*-2*/ S(50, 0), /*-1*/ S(100, 50),
        /*0 minor pieces (balance) */ S(200, 100),
        /*+1*/ S(150, 75), /*+2*/ S(200, 100), /*+3*/ S(200, 100), /*+4*/ S(200, 100)},

    //3 major pieces up (+15 pawns))
    { /*-4*/ S(-50, 0), /*-3*/ S(50, 0), /*-2*/ S(100, 50), /*-1*/ S(150, 75),
        /*0 minor pieces (balance) */ S(200, 100),
        /*+1*/ S(200, 100), /*+2*/ S(200, 100), /*+3*/ S(200, 100), /*+4*/ S(200, 100)},

    //4 major pieces up (+20 pawns))
    { /*-4*/ S(50, 0), /*-3*/ S(100, 50), /*-2*/ S(150, 75), /*-1*/ S(200, 100),
        /*0 minor pieces (balance) */ S(200, 100),
        /*+1*/ S(200, 100), /*+2*/ S(200, 100), /*+3*/ S(200, 100), /*+4*/ S(200, 100)},
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

const short VMATING_POWER = 20;
const short VMATING_MATERIAL = 50;

const short TRADEDOWN_PIECES[MAX_PIECES + 1] = {
    100, 50, 25, 10, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0
};

const short TRADEDOWN_PAWNS[9] = {
    -80, -40, -20, -10, 0, 0, 0, 0, 0
};

/*******************************************************************************
 * Pawn Values 
 *******************************************************************************/

const TScore DEFENDED_PAWN[2] = {S(0, 4), S(4, 8)}; //closed, open file
const TScore ISOLATED_PAWN[2] = {S(-10, -20), S(-20, -20)}; //closed, open file
const TScore WEAK_PAWN[2] = {S(-12, -8), S(-16, -10)}; //closed, open file
const TScore DOUBLED_PAWN = S(-10, -20);

const TScore PASSED_PAWN[64] = {
    S(0, 0), S(0, 0), S(0, 0), S(0, 0), S(0, 0), S(0, 0), S(0, 0), S(0, 0),
    S(100, 150), S(80, 120), S(80, 120), S(70, 110), S(70, 110), S(80, 120), S(80, 120), S(100, 150),
    S(65, 100), S(50, 80), S(50, 80), S(40, 60), S(40, 60), S(50, 80), S(50, 80), S(65, 100),
    S(40, 60), S(30, 40), S(30, 40), S(20, 30), S(20, 30), S(30, 40), S(30, 40), S(40, 60),
    S(20, 30), S(15, 20), S(15, 25), S(10, 15), S(10, 15), S(15, 20), S(15, 20), S(20, 30),
    S(20, 30), S(15, 20), S(15, 25), S(10, 15), S(10, 15), S(15, 20), S(15, 20), S(20, 30),
    S(20, 30), S(15, 20), S(15, 25), S(10, 15), S(10, 15), S(15, 20), S(15, 20), S(20, 30),
    S(0, 0), S(0, 0), S(0, 0), S(0, 0), S(0, 0), S(0, 0), S(0, 0), S(0, 0),
};

const TScore CONNECED_PASSED_PAWN[64] = {
    S(0, 0), S(0, 0), S(0, 0), S(0, 0), S(0, 0), S(0, 0), S(0, 0), S(0, 0),
    S(90, 110), S(90, 110), S(90, 110), S(90, 110), S(90, 110), S(90, 110), S(90, 110), S(90, 110),
    S(50, 70), S(50, 70), S(50, 70), S(40, 60), S(40, 60), S(50, 70), S(50, 70), S(50, 70),
    S(20, 20), S(20, 20), S(20, 20), S(20, 20), S(20, 20), S(20, 20), S(20, 20), S(20, 20),
    S(10, 10), S(10, 10), S(10, 10), S(10, 10), S(10, 10), S(10, 10), S(10, 10), S(10, 10),
    S(5, 5), S(5, 5), S(5, 5), S(5, 5), S(5, 5), S(5, 5), S(5, 5), S(5, 5),
    S(5, 5), S(5, 5), S(5, 5), S(5, 5), S(5, 5), S(5, 5), S(5, 5), S(5, 5),
    S(0, 0), S(0, 0), S(0, 0), S(0, 0), S(0, 0), S(0, 0), S(0, 0), S(0, 0),
};

const TScore CANDIDATE[64] = {
    S(0, 0), S(0, 0), S(0, 0), S(0, 0), S(0, 0), S(0, 0), S(0, 0), S(0, 0),
    S(0, 0), S(0, 0), S(0, 0), S(0, 0), S(0, 0), S(0, 0), S(0, 0), S(0, 0),
    S(35, 70), S(35, 70), S(35, 70), S(35, 70), S(35, 70), S(35, 70), S(35, 70), S(35, 70),
    S(20, 40), S(20, 40), S(20, 40), S(20, 40), S(20, 40), S(20, 40), S(20, 40), S(20, 40),
    S(10, 20), S(10, 20), S(10, 20), S(10, 20), S(10, 20), S(10, 20), S(10, 20), S(10, 20),
    S(5, 10), S(5, 10), S(5, 10), S(5, 10), S(5, 10), S(5, 10), S(5, 10), S(5, 10),
    S(5, 10), S(5, 10), S(5, 10), S(5, 10), S(5, 10), S(5, 10), S(5, 10), S(5, 10),
    S(0, 0), S(0, 0), S(0, 0), S(0, 0), S(0, 0), S(0, 0), S(0, 0), S(0, 0),
};

const int8_t SHELTER_KPOS[64] = {//attack units regarding king position
    6, 6, 6, 6, 6, 6, 6, 6,
    6, 6, 6, 6, 6, 6, 6, 6,
    6, 6, 6, 6, 6, 6, 6, 6,
    5, 6, 6, 6, 6, 6, 6, 5,
    3, 4, 5, 6, 6, 5, 4, 3,
    1, 2, 3, 4, 4, 3, 2, 1,
    0, 1, 2, 3, 3, 2, 1, 0,
    0, -2, -1, 1, 1, 0, -2, 0,

};

const int8_t SHELTER_PAWN[64] = {//attack units for pawns in front of the king
    0, 0, 0, 0, 0, 0, 0, 0,
    0, 0, 0, 0, 0, 0, 0, 0,
    0, 0, 0, 0, 0, 0, 0, 0,
    0, 0, 0, 0, 0, 0, 0, 0,
    -1, -1, 0, 0, 0, 0, -1, -1,
    -2, -2, -1, -1, -1, -1, -2, -2,
    -3, -4, -2, -1, -1, -2, -4, -3,
    0, 0, 0, 0, 0, 0, 0, 0
};

const int8_t STORM_PAWN[64] = {//attack units for pawns attacking the nme king
    0, 0, 0, 0, 0, 0, 0, 0,
    3, 4, 4, 4, 4, 4, 4, 3,
    3, 4, 4, 4, 4, 4, 4, 3,
    3, 4, 4, 4, 4, 4, 4, 3,
    3, 4, 4, 4, 4, 4, 4, 3,
    3, 4, 4, 4, 4, 4, 4, 3,
    3, 4, 4, 4, 4, 4, 4, 3,
    0, 0, 0, 0, 0, 0, 0, 0
};

const int8_t SHELTER_OPEN_FILES[4] = {//attack units for having open files on our king
    -1, 2, 3, 4
};

const int8_t SHELTER_OPEN_EDGE_FILE = 3; //attack units for open file on the edge (e.g. open h-line)

const int8_t SHELTER_CASTLING_KINGSIDE = -3; //attack units for having the option to safely  castle kingside 

const int8_t SHELTER_CASTLING_QUEENSIDE = -2; //attack units for having the right to safely castle queenside

const TScore BLOCKED_CENTER_PAWN = S(-10, -10);

/*******************************************************************************
 * Knight Values 
 *******************************************************************************/

const TScore KNIGHT_MOBILITY[9] = {
    S(-22, -22), S(-18, -18), S(-14, -14), S(-10, -10),
    S(-8, -8), S(-6, -6), S(-4, -4), S(-2, -2), S(0, 0)
};

const int8_t KNIGHT_RANK[8] = {-2, -1, 0, 0, 1, 2, 0, -1};

const TScore KNIGHT_PAWN_WIDTH[8] = {//indexed by opponent pawn width
    S(0, 0), S(0, 0), S(0, 0), S(0, 0), S(0, -5), S(0, -10), S(0, -15), S(0, -20)
};

const TScore KNIGHT_PAWN_COUNT[9] = {//indexed by opponent pawn count
    S(0, 0), S(0, 0), S(0, 0), S(0, 0), S(0, 0), S(0, 0), S(4, 4), S(8, 8), S(12, 12)
};
/*******************************************************************************
 * Bishop Values 
 *******************************************************************************/

const TScore BISHOP_MOBILITY[14] = {
    S(-48, -48), S(-24, -24), S(-12, -12), S(-6, -6),
    S(-2, -2), S(0, 0), S(4, 4), S(8, 8), S(12, 12),
    S(16, 16), S(18, 18), S(20, 20), S(22, 22), S(24, 24)
};

const TScore TRAPPED_BISHOP = S(-100, -120);

U64 BISHOP_PATTERNS[2] = {//black, white
    BIT(d6) | BIT(d7) | BIT(e6) | BIT(d7) | BIT(a2) | BIT(h2),
    BIT(d3) | BIT(d2) | BIT(e3) | BIT(d2) | BIT(a7) | BIT(h7),
};

/*******************************************************************************
 * Rook Values 
 *******************************************************************************/

const TScore ROOK_7TH = S(20, 40);
const TScore ROOK_1ST = S(8, 0); //back rank protection
const TScore ROOK_SEMIOPEN_FILE = S(10, 10);
const TScore ROOK_OPEN_FILE = S(20, 20);
const TScore ROOK_GOOD_SIDE = S(8, 16); //Rule of Tarrasch 
const TScore ROOK_WRONG_SIDE = S(-8, -16);

const TScore ROOK_MOBILITY[15] = {
    S(-20, -40), S(-16, -32), S(-12, -24), S(-8, -16),
    S(-6, -12), S(-4, -8), S(-2, -4), S(0, 0), S(2, 4),
    S(4, 8), S(6, 12), S(8, 16), S(10, 20), S(12, 24), S(14, 28)
};

/*******************************************************************************
 * Queen Values
 *******************************************************************************/

const TScore QUEEN_7TH = S(10, 20);

const TScore QUEEN_MOBILITY[29] = {
    S(-10, -20), S(-4, -10), S(-2, -4), S(0, -2), S(1, 0), S(2, 2), S(3, 4), S(4, 6),
    S(5, 8), S(6, 10), S(6, 12), S(7, 14), S(7, 16), S(8, 17), S(8, 18), S(9, 19),
    S(9, 20), S(10, 21), S(10, 21), S(11, 22), S(11, 22), S(11, 22), S(12, 23), S(12, 23),
    S(13, 23), S(13, 24), S(13, 24), S(14, 25), S(14, 25)
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
    score->set(evaluatePawnsAndKings(sd)); //initializes mobility and attack masks (required)
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
    score->add(evaluateKingAttack(sd, WHITE));
    score->sub(evaluateKingAttack(sd, BLACK));

    result += score->get(sd->stack->phase);
    result &= GRAIN;
    result = sd->pos->boardFlags->WTM ? result : -result;
    sd->stack->eval_result = result;

    assert(result > -VKING && result < VKING);

    return result;
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
    if (wknights != bknights) {
        result.mg += (wknights - bknights) * SVKNIGHT.mg;
        result.eg += (wknights - bknights) * SVKNIGHT.eg;
    }
    if (wbishops != bbishops) {
        result.mg += (wbishops - bbishops) * SVBISHOP.mg;
        result.eg += (wbishops - bbishops) * SVBISHOP.eg;
    }
    if (wrooks != brooks) {
        result.mg += (wrooks - brooks) * SVROOK.mg;
        result.eg += (wrooks - brooks) * SVROOK.eg;
    }
    if (wqueens != bqueens) {
        result.mg += (wqueens - bqueens) * SVQUEEN.mg;
        result.eg += (wqueens - bqueens) * SVQUEEN.eg;
    }

    // Bishop pair
    if (wbishops > 1 && pos->whiteBishopPair()) {
        result.add(VBISHOPPAIR);
    }
    if (bbishops > 1 && pos->blackBishopPair()) {
        result.sub(VBISHOPPAIR);
    }

    bool minor_balance = (wminors == bminors);
    bool major_balance = (wrooks + 2 * wqueens) == (brooks + 2 * bqueens);
    bool balance = minor_balance && major_balance;
    bool mating_power_w = wrooks || wqueens || wminors > 2 || (wminors == 2 && wbishops > 0);
    bool mating_power_b = brooks || bqueens || bminors > 2 || (bminors == 2 && bbishops > 0);
    bool mating_material_w = wpawns || mating_power_w;
    bool mating_material_b = bpawns || mating_power_b;

    if (mating_power_w) {
        result.add(VMATING_POWER);
        if (mating_material_w) {
            result.add(VMATING_MATERIAL);
        }
    }
    if (mating_power_b) {
        result.sub(VMATING_POWER);
        if (mating_material_b) {
            result.sub(VMATING_MATERIAL);
        }
    }

    if (!balance) {
        //material imbalance
        int minors_ix = MAX(0, 4 + wminors - bminors);
        int majors_ix = MAX(0, 4 + wrooks + 2 * wqueens - brooks - 2 * bqueens);
        result.add(IMBALANCE[MIN(majors_ix, 8)][MIN(minors_ix, 8)]);
    }

    int piece_power = result.get(phase);

    if (wpawns != bpawns) {
        result.mg += (wpawns - bpawns) * SVPAWN.mg;
        result.eg += (wpawns - bpawns) * SVPAWN.eg;

        // penalty for not having pawns at all - makes it hard to win
        if (wpawns == 0) {
            result.add(VNOPAWNS);
        }
        if (bpawns == 0) {
            result.sub(VNOPAWNS);
        }
    }

    int value = result.get(phase);

    //If ahead in material, trade pieces (simplify) and keep pawns
    if (piece_power > MATERIAL_AHEAD_THRESHOLD) {
        value += TRADEDOWN_PIECES[bpieces];
        value += TRADEDOWN_PAWNS[wpawns];
    } else if (piece_power < -MATERIAL_AHEAD_THRESHOLD) {
        value -= TRADEDOWN_PIECES[wpieces];
        value -= TRADEDOWN_PAWNS[bpawns];
    }


    /*
     * Special Cases / Endgame adjustments
     */
    if (wpawns == 0 && value > 0 && piece_power < 450) {
        //no pawns and less than a rook extra piece_power is mostly drawn
        value >>= 3;
    } else if (bpawns == 0 && value < 0 && piece_power > -450) {
        //same case for black
        value >>= 3;
    } else if (value
            && balance
            && wminors < 2
            && wpieces <= 3
            && wpieces > 0
            && (wrooks || wqueens)
            && ABS(value) > ABS(DRAWISH_QR_ENDGAME)
            && ABS(value) < 2 * VPAWN) {
        //Rooks and queen endgames are drawish. Reduce any small material advantage.
        value += cond(value > 0, value < 0, DRAWISH_QR_ENDGAME);
        if (wminors == 0 && wpieces <= 1) { //more drawish
            value += cond(value > 30, value < -30, DRAWISH_QR_ENDGAME >> 1);
        }
    } else if (value && wpieces == 1 && bpieces == 1 && wpawns != bpawns && wbishops && bbishops
            && (bool(pos->whiteBishops & BLACK_SQUARES) != bool(pos->blackBishops & BLACK_SQUARES))) {
        // Opposite  bishop ending is drawish
        //todo: set drawflag 
        value += cond(wpawns>bpawns, bpawns>wpawns, MAX(-(ABS(value) >> 1), DRAWISH_OPP_BISHOPS));
    }

    /*
     * Store and return
     */
    sd->stack->material_score = value;
    sd->stack->phase = phase;
    sd->hashTable->mtStore(sd, value, phase);
    return value;
}

void init_pst_store(TScore scores[], int wpiece) {
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

void init_pst() {
    TScore scores[64];
    const short mobility_scale[2][64] = {
        {
            2, 2, 2, 2, 2, 2, 2, 2,
            2, 2, 2, 2, 2, 2, 2, 2,
            2, 2, 2, 2, 2, 2, 2, 2,
            2, 2, 2, 3, 3, 2, 2, 2,
            1, 1, 1, 2, 2, 1, 1, 1,
            1, 1, 1, 1, 1, 1, 1, 1,
            1, 1, 1, 1, 1, 1, 1, 1,
            1, 1, 1, 1, 1, 1, 1, 1
        },
        {
            2, 2, 2, 2, 2, 2, 2, 2,
            2, 2, 2, 2, 2, 2, 2, 2,
            2, 2, 2, 2, 2, 2, 2, 2,
            2, 2, 2, 2, 2, 2, 2, 2,
            2, 2, 2, 2, 2, 2, 2, 2,
            2, 2, 2, 2, 2, 2, 2, 2,
            2, 2, 2, 2, 2, 2, 2, 2,
            2, 2, 2, 2, 2, 2, 2, 2
        }
    };

    //Pawn
    for (int sq = a1; sq < 64; sq++) {
        U64 bbsq = BIT(sq);
        scores[sq].clear();
        scores[sq].add_ix64(&mobility_scale, FLIP_SQUARE(sq));
        if ((bbsq & RANK_1) || (bbsq & RANK_8)) {
            continue;
        }
        U64 caps = WPawnCaptures[sq];
        while (caps) {
            int ix = POP(caps);
            scores[sq].add_ix64(&mobility_scale, FLIP_SQUARE(ix));
        }
        scores[sq].mul(8); //pawns are the most powerful to control squares
        scores[sq].eg = 3 * RANK(sq);
    }
    init_pst_store(scores, WPAWN);

    //Knight
    for (int sq = a1; sq < 64; sq++) {
        scores[sq].clear();
        scores[sq].add_ix64(&mobility_scale, FLIP_SQUARE(sq));
        U64 caps = KnightMoves[sq];
        while (caps) {
            int ix = POP(caps);
            scores[sq].add_ix64(&mobility_scale, FLIP_SQUARE(ix));
        }
        scores[sq].mul(3); //mobility is extra important because knights move slow
        scores[sq].add(KNIGHT_RANK[RANK(sq)]);
    }
    init_pst_store(scores, WKNIGHT);

    //Bishop
    for (int sq = a1; sq < 64; sq++) {
        scores[sq].clear();
        scores[sq].add_ix64(&mobility_scale, FLIP_SQUARE(sq));
        U64 caps = BishopMoves[sq];
        while (caps) {
            int ix = POP(caps);
            scores[sq].add_ix64(&mobility_scale, FLIP_SQUARE(ix));
        }
        if (sq == g2 || sq == b2) {
            scores[sq].mg += 2;
        }
        scores[sq].mul(3);
    }
    init_pst_store(scores, WBISHOP);

    //Rook
    for (int sq = a1; sq < 64; sq++) {
        scores[sq].clear();
        scores[sq].add_ix64(&mobility_scale, FLIP_SQUARE(sq));
        U64 caps = RookMoves[sq];
        while (caps) {
            int ix = POP(caps);
            scores[sq].add_ix64(&mobility_scale, FLIP_SQUARE(ix));
        }
        scores[sq].mul(2); //square control by rook is less powerful than by bishop/knight/pawn
    }
    init_pst_store(scores, WROOK);

    //Queen
    for (int sq = a1; sq < 64; sq++) {
        scores[sq].clear();
        scores[sq].add_ix64(&mobility_scale, FLIP_SQUARE(sq));
        U64 caps = QueenMoves[sq];
        U64 bbsq = BIT(sq);
        while (caps) {
            int ix = POP(caps);
            scores[sq].add_ix64(&mobility_scale, FLIP_SQUARE(ix));
        }
        scores[sq].mul(1);
    }
    init_pst_store(scores, WQUEEN);

    //King
    for (int sq = a1; sq < 64; sq++) {
        scores[sq].clear();
        scores[sq].add_ix64(&mobility_scale, FLIP_SQUARE(sq));
        U64 caps = KingMoves[sq];
        while (caps) {
            int ix = POP(caps);
            scores[sq].add_ix64(&mobility_scale, FLIP_SQUARE(ix));
        }
        if (BIT(sq) & LARGE_CENTER) {
            scores[sq].eg += 4;
        }
        if (BIT(sq) & CENTER) {
            scores[sq].eg += 5;
        }
        scores[sq].mg = 0;
        scores[sq].eg *= 2.25;
    }
    init_pst_store(scores, WKING);
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
        sd->stack->passers = (sd->stack - 1)->passers;
        sd->stack->mob[WHITE] = (sd->stack - 1)->mob[WHITE];
        sd->stack->mob[BLACK] = (sd->stack - 1)->mob[BLACK];
        sd->stack->king_attack_zone[WHITE] = (sd->stack - 1)->king_attack_zone[WHITE];
        sd->stack->king_attack_zone[BLACK] = (sd->stack - 1)->king_attack_zone[BLACK];
        sd->stack->king_attack_pc[WPAWN] = (sd->stack - 1)->king_attack_pc[WPAWN];
        sd->stack->king_attack_pc[BPAWN] = (sd->stack - 1)->king_attack_pc[BPAWN];
        sd->stack->king_attack_sq[WPAWN] = 0;
        sd->stack->king_attack_sq[BPAWN] = 0;
        return &sd->stack->pawn_score;
    }

    //set mobility and attack masks
    TBoard * pos = sd->pos;
    int wkpos = *pos->whiteKingPos;
    int bkpos = *pos->blackKingPos;
    sd->stack->mob[WHITE] = ~(*pos->pawns[WHITE] | pos->pawnAttacks(BLACK) | *pos->kings[WHITE]);
    sd->stack->mob[BLACK] = ~(*pos->pawns[BLACK] | pos->pawnAttacks(WHITE) | *pos->kings[BLACK]);
    sd->stack->king_attack_zone[WHITE] = MagicQueenMoves(bkpos, pos->pawnsAndKings()) & ~(RANK_8) & sd->stack->mob[WHITE];
    sd->stack->king_attack_zone[BLACK] = MagicQueenMoves(wkpos, pos->pawnsAndKings()) & ~(RANK_1) & sd->stack->mob[BLACK];

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

    sd->stack->king_attack_pc[WPAWN] = 0;
    sd->stack->king_attack_pc[BPAWN] = 0;
    sd->stack->king_attack_sq[WPAWN] = 0;
    sd->stack->king_attack_sq[BPAWN] = 0;
    pawn_score->clear();


    U64 passers = 0;
    U64 targets = 0;
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

#ifdef PRINT_PAWN_EVAL
        std::cout << "WP " << PRINT_SQUARE(sq) << ": ";
#endif

        pawn_score->add(PST[WPAWN][isq]);



#ifdef PRINT_PAWN_EVAL        
        std::cout << "pst: " << PRINT_SCORE(PST[WPAWN][isq]);
#endif

        if (isolated) {
            targets |= sqBit;
            pawn_score->add(ISOLATED_PAWN[open]);

#ifdef PRINT_PAWN_EVAL
            std::cout << "isolated: " << PRINT_SCORE(ISOLATED_PAWN[open]);
#endif

        } else if (weak) {
            targets |= sqBit;
            pawn_score->add(WEAK_PAWN[open]); //elo vs-1:+22, elo vs0:1

#ifdef PRINT_PAWN_EVAL
            std::cout << "weak: " << PRINT_SCORE(WEAK_PAWN[open]);
#endif
        }
        if (doubled) {
            pawn_score->add(DOUBLED_PAWN);

#ifdef PRINT_PAWN_EVAL
            std::cout << "doubled: " << PRINT_SCORE(DOUBLED_PAWN);
#endif

        }
        if (defended) {
            pawn_score->add(DEFENDED_PAWN[open]);

#ifdef PRINT_PAWN_EVAL
            std::cout << "defended: " << PRINT_SCORE(DEFENDED_PAWN[open]);
#endif

        }
        if (candidate) {
            pawn_score->add(CANDIDATE[isq]);

#ifdef PRINT_PAWN_EVAL
            std::cout << "candidate: " << PRINT_SCORE(CANDIDATE[isq]);
#endif

        } else if (passed) {

#ifdef PRINT_PAWN_EVAL
            std::cout << "passed ";
#endif
            passers |= sqBit;

            if (KingMoves[sq] & passers & pos->whitePawns) {

#ifdef PRINT_PAWN_EVAL
                std::cout << "connected: " << PRINT_SCORE(PASSED_PAWN[isq]);
#endif

                pawn_score->add(CONNECED_PASSED_PAWN[isq]);
            }
        }

#ifdef PRINT_PAWN_EVAL
        std::cout << std::endl;
#endif
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

#ifdef PRINT_PAWN_EVAL
        std::cout << "BP " << PRINT_SQUARE(sq) << ": ";
#endif

        pawn_score->sub(PST[WPAWN][sq]);

#ifdef PRINT_PAWN_EVAL
        std::cout << "pst: " << PRINT_SCORE(PST[WPAWN][sq]);
#endif

        if (isolated) {
            targets |= sqBit;
            pawn_score->sub(ISOLATED_PAWN[open]);
#ifdef PRINT_PAWN_EVAL
            std::cout << "isolated: " << PRINT_SCORE(ISOLATED_PAWN[open]);
#endif
        } else if (weak) {
            targets |= sqBit;
            pawn_score->sub(WEAK_PAWN[open]);
#ifdef PRINT_PAWN_EVAL
            std::cout << "weak: " << PRINT_SCORE(WEAK_PAWN[open]);
#endif
        }
        if (doubled) {
            pawn_score->sub(DOUBLED_PAWN);
#ifdef PRINT_PAWN_EVAL
            std::cout << "doubled: " << PRINT_SCORE(DOUBLED_PAWN);
#endif
        }
        if (defended) {
            pawn_score->sub(DEFENDED_PAWN[open]);
#ifdef PRINT_PAWN_EVAL
            std::cout << "defended: " << PRINT_SCORE(DEFENDED_PAWN[open]);
#endif
        }
        if (candidate) {
            pawn_score->sub(CANDIDATE[sq]);
#ifdef PRINT_PAWN_EVAL
            std::cout << "candidate: " << PRINT_SCORE(CANDIDATE[sq]);
#endif
        } else if (passed) {

#ifdef PRINT_PAWN_EVAL
            std::cout << "passed: ";
#endif
            passers |= sqBit;

            if (KingMoves[sq] & passers & pos->blackPawns) {
                pawn_score->sub(CONNECED_PASSED_PAWN[sq]);
#ifdef PRINT_PAWN_EVAL
                std::cout << "passer connected: " << PRINT_SCORE(CONNECED_PASSED_PAWN[sq]);
#endif
            }
        }
#ifdef PRINT_PAWN_EVAL
        std::cout << std::endl;
#endif

    }

#ifdef PRINT_PAWN_EVAL
    std::cout << "total: ";
    pawn_score->print();
    std::cout << std::endl;
#endif

    pawn_score->add(PST[WKING][ISQ(wkpos, WHITE)]);
    pawn_score->sub(PST[WKING][ISQ(bkpos, BLACK)]);

    //attack pawns with king in the EG
    //pawn_score->add(0, popCount0(KingZone[wkpos] & pos->blackPawns) * 8);
    //pawn_score->sub(0, popCount0(KingZone[bkpos] & pos->whitePawns) * 8);

    //attack especially weak pawns with king in the EG
    pawn_score->add(0, popCount0(KingZone[wkpos] & pos->blackPawns & targets) * 8);
    pawn_score->sub(0, popCount0(KingZone[bkpos] & pos->whitePawns & targets) * 8);

    //support and attack passed pawns even more
    pawn_score->add(0, popCount0(KingZone[wkpos] & passers) * 24);
    pawn_score->sub(0, popCount0(KingZone[bkpos] & passers) * 24);

    /*
     * 4. Calculate King Shelter Attack units
     */
    sd->stack->king_attack_pc[BPAWN] += SHELTER_KPOS[FLIP_SQUARE(wkpos)];

#ifdef PRINT_PAWN_EVAL
    std::cout << "attack on WK (pos): " << (int) sd->stack->king_attack_pc[BPAWN] << std::endl;
#endif

    //1. reward having the right to castle
    if (pos->boardFlags->castlingFlags & CASTLE_K
            && ((pos->Matrix[h2] == WPAWN && pos->Matrix[g2] == WPAWN)
            || (pos->Matrix[f2] == WPAWN && pos->Matrix[h2] == WPAWN && pos->Matrix[g3] == WPAWN)
            || (pos->Matrix[h3] == WPAWN && pos->Matrix[g2] == WPAWN && pos->Matrix[f2] == WPAWN))) {
        sd->stack->king_attack_pc[BPAWN] += SHELTER_CASTLING_KINGSIDE;
    } else if (pos->boardFlags->castlingFlags & CASTLE_Q
            && ((pos->Matrix[a2] == WPAWN && pos->Matrix[b2] == WPAWN && pos->Matrix[c2] == WPAWN)
            || (pos->Matrix[a2] == WPAWN && pos->Matrix[b3] == WPAWN && pos->Matrix[c2] == WPAWN))) {
        sd->stack->king_attack_pc[BPAWN] += SHELTER_CASTLING_QUEENSIDE;
    }

#ifdef PRINT_PAWN_EVAL
    std::cout << "attack on WK (castling): " << (int) sd->stack->king_attack_pc[BPAWN] << std::endl;
#endif
    //2. reward having pawns in front of the king
    U64 kingFront = FORWARD_RANKS[RANK(wkpos)] & PAWN_SCOPE[FILE(wkpos)];
    U64 shelterPawns = kingFront & pos->whitePawns;

    while (shelterPawns) {
        int sq = POP(shelterPawns);
        sd->stack->king_attack_pc[BPAWN] += SHELTER_PAWN[FLIP_SQUARE(sq)];
    }

#ifdef PRINT_PAWN_EVAL
    std::cout << "attack on WK (shelter): " << (int) sd->stack->king_attack_pc[BPAWN] << std::endl;
#endif
    U64 stormPawns = kingFront & KingZone[wkpos] & pos->blackPawns;
    while (stormPawns) {
        int sq = POP(stormPawns);
        sd->stack->king_attack_pc[BPAWN] += STORM_PAWN[FLIP_SQUARE(sq)];
    }

#ifdef PRINT_PAWN_EVAL
    std::cout << "attack on WK (storm): " << (int) sd->stack->king_attack_pc[BPAWN] << std::endl;
#endif
    //3. penalize (half)open files on the king
    U64 open = (openW | openB) & kingFront & RANK_8;
    if (open) {
        sd->stack->king_attack_pc[BPAWN] += SHELTER_OPEN_FILES[popCount0(open)];
        if (open & (FILE_A | FILE_H)) {
            sd->stack->king_attack_pc[BPAWN] += SHELTER_OPEN_EDGE_FILE;
        }
    }

#ifdef PRINT_PAWN_EVAL
    std::cout << "attack on WK (open files): " << (int) sd->stack->king_attack_pc[BPAWN] << std::endl;
#endif

    //black king shelter
    sd->stack->king_attack_pc[WPAWN] += SHELTER_KPOS[bkpos];

#ifdef PRINT_PAWN_EVAL
    std::cout << "attack on BK (pos): " << (int) sd->stack->king_attack_pc[WPAWN] << std::endl;
#endif
    //1. reward having the right to castle safely
    if (pos->boardFlags->castlingFlags & CASTLE_k
            && ((pos->Matrix[h7] == BPAWN && pos->Matrix[g7] == BPAWN)
            || (pos->Matrix[f7] == BPAWN && pos->Matrix[h7] == BPAWN && pos->Matrix[g6] == BPAWN)
            || (pos->Matrix[h6] == BPAWN && pos->Matrix[g7] == BPAWN && pos->Matrix[f7] == BPAWN))) {
        sd->stack->king_attack_pc[WPAWN] += SHELTER_CASTLING_KINGSIDE;
    } else if (pos->boardFlags->castlingFlags & CASTLE_q
            && ((pos->Matrix[a7] == BPAWN && pos->Matrix[b7] == BPAWN && pos->Matrix[c7] == BPAWN)
            || (pos->Matrix[a7] == BPAWN && pos->Matrix[b6] == BPAWN && pos->Matrix[c7] == BPAWN))) {
        sd->stack->king_attack_pc[WPAWN] += SHELTER_CASTLING_QUEENSIDE;
    }

#ifdef PRINT_PAWN_EVAL
    std::cout << "attack on BK (castling): " << (int) sd->stack->king_attack_pc[WPAWN] << std::endl;
#endif
    //2. reward having pawns in front of the king
    kingFront = BACKWARD_RANKS[RANK(bkpos)] & PAWN_SCOPE[FILE(bkpos)];
    shelterPawns = kingFront & pos->blackPawns;
    while (shelterPawns) {
        int sq = POP(shelterPawns);
        sd->stack->king_attack_pc[WPAWN] += SHELTER_PAWN[sq];
    }

#ifdef PRINT_PAWN_EVAL
    std::cout << "attack on BK (shelter): " << (int) sd->stack->king_attack_pc[WPAWN] << std::endl;
#endif
    stormPawns = kingFront & KingZone[bkpos] & pos->whitePawns;
    while (stormPawns) {
        int sq = POP(stormPawns);
        sd->stack->king_attack_pc[WPAWN] += STORM_PAWN[sq];
    }

#ifdef PRINT_PAWN_EVAL
    std::cout << "attack on BK (storm): " << (int) sd->stack->king_attack_pc[WPAWN] << std::endl;
#endif
    //3. penalize (half)open files on the king
    open = (openW | openB) & kingFront & RANK_1;
    if (open) {
        sd->stack->king_attack_pc[WPAWN] += SHELTER_OPEN_FILES[popCount0(open)];
        if (open & (FILE_A | FILE_H)) {
            sd->stack->king_attack_pc[WPAWN] += SHELTER_OPEN_EDGE_FILE;
        }
    }

#ifdef PRINT_PAWN_EVAL
    std::cout << "attack on BK (open files): " << (int) sd->stack->king_attack_pc[WPAWN] << std::endl;
#endif
    sd->stack->passers = passers;
    sd->hashTable->ptStore(sd);
    return &sd->stack->pawn_score;
}

inline TScore * evaluateKnights(TSearch * sd, bool us) {

    TScore * result = &sd->stack->knight_score[us];
    TBoard * pos = sd->pos;
    int pc = KNIGHT[us];
    result->clear();
    sd->stack->king_attack_pc[pc] = 0;
    sd->stack->king_attack_sq[pc] = 0;

    if (*pos->knights[us] == 0) {
        return result;
    }

    /*
     * 2. Get the score from the last stack record if the previous move 
     *   a) did not change the pawn+kings structure (pawn hash) 
     *   b) did not move or capture any knight
     */
    if (sd->stack->equal_pawns) {
        TMove * prevMove = &(sd->stack - 1)->move;
        if (prevMove->piece != pc && prevMove->capture != pc) {
            result->set((sd->stack - 1)->knight_score[us]);
            sd->stack->king_attack_pc[pc] = (sd->stack - 1)->king_attack_pc[pc];
            sd->stack->king_attack_sq[pc] = (sd->stack - 1)->king_attack_sq[pc];
            return result;
        }
    }

    /*
     * 3. Calculate the score and store on the stack
     */
    TPiecePlacement * pp = &pos->pieces[pc];
    bool them = !us;
    int pawn_width = BB_WIDTH(*pos->pawns[them]);
    int pawn_count = pos->pieces[PAWN[them]].count;
    int kpos = *pos->kingPos[them];
    U64 kaz = KnightMoves[kpos]; //king attack zone
    U64 kcz = KingMoves[kpos]; //king control zone
    for (int i = 0; i < pp->count; i++) {
        int sq = pp->squares[i];
        result->add(PST[WKNIGHT][ISQ(sq, us)]);
        U64 moves = KnightMoves[sq] & sd->stack->mob[us];
        int mob_count = popCount0(moves);
        result->add(KNIGHT_MOBILITY[mob_count]);
        result->add(KNIGHT_PAWN_WIDTH[pawn_width]);
        result->add(KNIGHT_PAWN_COUNT[pawn_count]);
        sd->stack->king_attack_sq[pc] += popCount0(moves & kcz);
        sd->stack->king_attack_pc[pc] += popCount0(moves & kaz);

    }
    return result;
}

inline TScore * evaluateBishops(TSearch * sd, bool us) {
    TScore * result = &sd->stack->bishop_score[us];
    TBoard * pos = sd->pos;
    int pc = BISHOP[us];
    result->clear();
    sd->stack->king_attack_pc[pc] = 0;
    sd->stack->king_attack_sq[pc] = 0;

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
        if (prevMove->piece != pc && prevMove->capture != pc) {
            result->set((sd->stack - 1)->bishop_score[us]);
            sd->stack->king_attack_pc[pc] = (sd->stack - 1)->king_attack_pc[pc];
            sd->stack->king_attack_sq[pc] = (sd->stack - 1)->king_attack_sq[pc];
            return result;
        }
    }

    /*
     * 3. Calculate the score and store on the stack
     */
    TPiecePlacement * pp = &pos->pieces[pc];
    U64 occ = pos->pawnsAndKings();
    bool them = !us;
    int kpos = *pos->kingPos[them];
    U64 kaz = sd->stack->king_attack_zone[us] & BishopMoves[kpos]; //king attack zone
    U64 kcz = KingMoves[kpos]; //king control zone
    for (int i = 0; i < pp->count; i++) {
        int sq = pp->squares[i];

        result->add(PST[WBISHOP][ISQ(sq, us)]);

        U64 moves = MagicBishopMoves(sq, occ) & sd->stack->mob[us];
        int count = popCount0(moves);
        result->add(BISHOP_MOBILITY[count]);

        sd->stack->king_attack_sq[pc] += popCount0(moves & kcz);
        sd->stack->king_attack_pc[pc] += popCount0(moves & kaz);

        //patterns
        if (BIT(sq) & BISHOP_PATTERNS[us]) {
            if (us == WHITE) {
                if (((sq == d3 || sq == d4) && (pos->Matrix[d2] == WPAWN || pos->Matrix[d3] == WPAWN))) {
                    result->add(BLOCKED_CENTER_PAWN);
                } else if (((sq == e3 || sq == e4) && (pos->Matrix[e2] == WPAWN || pos->Matrix[e3] == WPAWN))) {
                    result->add(BLOCKED_CENTER_PAWN);
                } else if ((sq == h7 && pos->Matrix[g6] == BPAWN && pos->Matrix[f7] == BPAWN)
                        || (sq == a7 && pos->Matrix[b6] == BPAWN && pos->Matrix[c7] == BPAWN)) {
                    result->add(TRAPPED_BISHOP);
                }
            } else if (us == BLACK) {
                if (((sq == d6 || sq == d5) && (pos->Matrix[d7] == BPAWN || pos->Matrix[d6] == BPAWN))) {
                    result->add(BLOCKED_CENTER_PAWN);
                } else if (((sq == e6 || sq == e5) && (pos->Matrix[e7] == BPAWN || pos->Matrix[e6] == BPAWN))) {
                    result->add(BLOCKED_CENTER_PAWN);
                } else if ((sq == h2 && pos->Matrix[g3] == WPAWN && pos->Matrix[f3] == WPAWN)
                        || (sq == a2 && pos->Matrix[b3] == WPAWN && pos->Matrix[c2] == WPAWN)) {
                    result->add(TRAPPED_BISHOP);
                }
            }
        }
    }
    return result;
}

inline TScore * evaluateRooks(TSearch * sd, bool us) {
    TScore * result = &sd->stack->rook_score[us];
    TBoard * pos = sd->pos;
    int pc = ROOK[us];
    result->clear();
    sd->stack->king_attack_pc[pc] = 0;
    sd->stack->king_attack_sq[pc] = 0;

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
        if (prevMove->piece != pc && prevMove->capture != pc) {
            result->set((sd->stack - 1)->rook_score[us]);
            sd->stack->king_attack_pc[pc] = (sd->stack - 1)->king_attack_pc[pc];
            sd->stack->king_attack_sq[pc] = (sd->stack - 1)->king_attack_sq[pc];
            return result;
        }
    }

    /*
     * 3. Calculate the score and store on the stack
     */
    TPiecePlacement * pp = &pos->pieces[ROOK[us]];
    bool them = !us;
    U64 fill[2] = {FILEFILL(pos->blackPawns), FILEFILL(pos->whitePawns)};
    U64 occ = pos->pawnsAndKings();
    if ((*pos->rooks[us] & RANK[us][1]) && (BIT(*pos->kingPos[us]) & (RANK[us][1] | RANK[us][2]))) {
        result->add(ROOK_1ST); //at least one rook is protecting the back rank
    }

    int kpos = *pos->kingPos[them];
    U64 kaz = sd->stack->king_attack_zone[us] & RookMoves[kpos]; //king attack zone
    U64 kcz = KingMoves[kpos]; //king control zone

    for (int i = 0; i < pp->count; i++) {
        int sq = pp->squares[i];
        result->add(PST[WROOK][ISQ(sq, us)]);
        U64 bitSq = BIT(sq);
        if (bitSq & (~fill[us])) {
            if (bitSq & fill[them]) {
                result->add(ROOK_SEMIOPEN_FILE);
            } else {
                result->add(ROOK_OPEN_FILE);
            }
        } else {
            result->sub(ROOK_SEMIOPEN_FILE);
        }
        U64 moves = MagicRookMoves(sq, occ) & sd->stack->mob[us];
        int count = popCount0(moves);
        result->add(ROOK_MOBILITY[count]);

        sd->stack->king_attack_sq[pc] += popCount0(moves & kcz);
        sd->stack->king_attack_pc[pc] += popCount0(moves & kaz);

        if (bitSq & RANK[us][7] && (BIT(*pos->kingPos[them]) & (RANK[us][8] | (RANK[us][7])))) {
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
    int pc = QUEEN[us];
    result->clear();
    sd->stack->king_attack_pc[pc] = 0;
    sd->stack->king_attack_sq[pc] = 0;

    if (*pos->queens[us] == 0) {
        return result;
    }

    /*
     * 2. Get the score from the last stack record if the previous move 
     *   a) did not change the pawn+kings structure (pawn hash) 
     *   b) did not move or capture any bishop
     */
    if (sd->stack->equal_pawns) {
        TMove * prevMove = &(sd->stack - 1)->move;
        if (prevMove->piece != pc && prevMove->capture != pc) {
            result->set((sd->stack - 1)->queen_score[us]);
            sd->stack->king_attack_pc[pc] = (sd->stack - 1)->king_attack_pc[pc];
            sd->stack->king_attack_sq[pc] = (sd->stack - 1)->king_attack_sq[pc];
            return result;
        }
    }

    /*
     * 3. Calculate the score and store on the stack
     */
    bool them = !us;
    TPiecePlacement * pp = &pos->pieces[QUEEN[us]];
    U64 occ = pos->pawnsAndKings();
    int kpos = *pos->kingPos[them];
    U64 kaz = sd->stack->king_attack_zone[us]; //king attack zone
    U64 kcz = KingMoves[kpos]; //king control zone
    for (int i = 0; i < pp->count; i++) {
        int sq = pp->squares[i];

        result->add(PST[WQUEEN][ISQ(sq, us)]);

        U64 moves = MagicQueenMoves(sq, occ);
        int count = popCount0(moves & sd->stack->mob[us]);
        result->add(QUEEN_MOBILITY[count]);

        sd->stack->king_attack_sq[pc] += popCount0(moves & kcz);
        sd->stack->king_attack_pc[pc] += popCount0(moves & kaz);

        if ((BIT(sq) & RANK[us][7]) && (BIT(*pos->kingPos[them]) & RANK[us][8])) {
            result->add(QUEEN_7TH);
        }
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
#ifdef PRINT_PASSED_PAWN
        std::cout << "passed pawn " << sq << ": ";
#endif
        int ix = us == WHITE ? FLIP_SQUARE(sq) : sq;
        TScore bonus;
        bonus.set(PASSED_PAWN[ix]);
        result->add(bonus);
#ifdef PRINT_PASSED_PAWN
        std::cout << "base ";
        bonus.print();
#endif
        bonus.half();
        if (pVsK) {
            unstoppable = MAX(unstoppable, evaluatePasserVsK(sd, us, sq));
        }
        if (BIT(sq) & exclude) {
            continue;
        }
        int to = forwardSq(sq, us);
        do {
            if (BIT(to) & sd->pos->allPieces) {
                break; //blocked
            }
            sd->pos->allPieces ^= BIT(sq); //to include rook/queen xray attacks from behind
            U64 attacks = sd->pos->attacksTo(to);
            sd->pos->allPieces ^= BIT(sq);
            U64 defend = attacks & sd->pos->all(them);
            U64 support = attacks & sd->pos->all(us);
            if (defend) {
                if (support == 0) {
                    break;
                }
                if (is_1(support) && gt_1(defend)) {
                    break;
                }
            }
            result->add(bonus);
#ifdef PRINT_PASSED_PAWN
            std::cout << " can advance to " << to;
            bonus.print();
#endif
            to = forwardSq(to, us);
        } while (to >= a1 && to <= h8);
    }
    result->add(0, unstoppable); //add the best unstoppable passer score
#ifdef PRINT_PASSED_PAWN
    std::cout << " unstoppable " << unstoppable;
    std::cout << " total: ";
    result->print();
    std::cout << std::endl;
#endif
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

const int8_t KING_ATTACK_OFFSET = 10; //perfectly castled king -10 units

const int8_t KING_ATTACK_UNIT[BKING + 1] = {
    //  x, p, n, b, r, q, k, p, n, b, r, q, k
    /**/0, 1, 1, 1, 2, 4, 0, 1, 1, 1, 2, 4, 0
};

const int16_t KING_SHELTER[24] = {//structural shelter (pawns & kings)
    0, 24, 32, 40, 48, 56, 64, 72,
    80, 88, 98, 106, 114, 122, 130, 138,
    146, 154, 162, 170, 178, 186, 194, 200
};

const int16_t KING_ATTACK[64] = {//indexed by attack units
    0, 0, 0, 1, 3, 5, 6, 8,
    10, 12, 16, 20, 24, 28, 32, 36,
    42, 48, 54, 60, 66, 72, 78, 84,
    92, 100, 108, 116, 124, 131, 138, 144,
    150, 155, 160, 164, 168, 172, 176, 180,
    182, 188, 190, 192, 194, 196, 198, 200,
    202, 204, 206, 208, 210, 212, 214, 216,
    218, 220, 222, 224, 226, 228, 230, 232
};

const int16_t KING_ATTACKERS_MUL[6] = {
    0, 0, 128, 192, 256, 288
};

const int16_t KING_SHELTER_MUL[24] = {
    32, 64, 96, 128, 160, 192, 224, 256,
    272, 288, 304, 320, 328, 336, 342, 350,
    354, 358, 362, 368, 370, 372, 376, 380
};

inline TScore * evaluateKingAttack(TSearch * sd, bool us) {
    TScore * result = &sd->stack->king_score[us];
    result->clear();
    TBoard * pos = sd->pos;
    if (*pos->queens[us] == 0) {
        return result;
    }

    /*
     * 1. Shelter score
     */
    int shelter_ix = RANGE(0, 23, KING_ATTACK_OFFSET + sd->stack->king_attack_pc[PAWN[us]]);

    result->set(KING_SHELTER[shelter_ix], 0);
    result->add((popCount(sd->stack->king_attack_zone[us]) - 3) * 12, 0);

#ifdef PRINT_KING_SAFETY
    std::cout << "\nKing Attack\nShelter: " << shelter_ix << " -> " << (int) KING_SHELTER[shelter_ix] << std::endl;
    printBB("attack zone", sd->stack->king_attack_zone[us]);
    result->print();
    std::cout << std::endl;
#endif

    /*
     * 2. If we have only queens, stop and return.
     * The shelter score still counts, but only half.
     */

    if (*pos->rooks[us] == 0 && *pos->bishops[us] == 0 && *pos->knights[us] == 0) {
        result->half();
        return result;
    }

    /*
     * 3. Get the totals of the piece attack scores
     */
    int attack_units = 0;
    int attackers_count = 0;
    bool queen_involved = false;
    for (int pc = KNIGHT[us]; pc <= QUEEN[us]; pc++) {
        int pcs = sd->stack->king_attack_pc[pc];
        int sqs = sd->stack->king_attack_sq[pc];
        if (pcs == 0 && sqs == 0) {
            continue;
        }
        queen_involved |= (pc == QUEEN[us]);
        attackers_count++;
        int unit = KING_ATTACK_UNIT[pc];
        attack_units += unit + (2*pcs) + sqs;
#ifdef PRINT_KING_SAFETY
        std::cout << "pc " << pc << ": " << pcs << " , sqs: " << sqs << " tot: " << attack_units << std::endl;
#endif
    }
#ifdef PRINT_KING_SAFETY
    std::cout << "attacking pieces " << attackers_count << std::endl;
    std::cout << "attack units total " << attack_units << std::endl;
#endif
    if (queen_involved && attackers_count > 1) {
        int ix = RANGE(0, 63, attack_units);
        int piece_attack_score = KING_ATTACK[ix];
        MUL256(piece_attack_score, KING_ATTACKERS_MUL[RANGE(0, 5, attackers_count)]);
        MUL256(piece_attack_score, KING_SHELTER_MUL[RANGE(0, 23, shelter_ix)]);
        result->add(piece_attack_score, 0);
#ifdef PRINT_KING_SAFETY
        std::cout << "piece attack score " << piece_attack_score << std::endl;
#endif
    }

#ifdef PRINT_KING_SAFETY
    result->print();
#endif
    
    return result;
}

