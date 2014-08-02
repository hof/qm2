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
 * File: evlauate.cpp
 * Evalution functions for traditional chess
 */

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
int short evaluateEndgame(TSearch * sd, short score);

/*******************************************************************************
 * Material Evaluation Values 
 *******************************************************************************/

enum MaterialValues {
    MATERIAL_AHEAD_THRESHOLD = 240, //all values are in centipawns
    VNOPAWNS = -40,
    VBISHOPPAIR = 40
};

const TScore TEMPO[2] = {S(-10, 0), S(10, 0)};

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
const TScore SVROOK = S(VROOK, VROOK + 50);
const TScore SVQUEEN = S(VQUEEN, VQUEEN + 100);
const TScore SVKING = S(VKING, VKING);

const short VMATING_POWER = 20;
const short VMATING_MATERIAL = 50;

const short REDUNDANT_ROOK = -10;
const short REDUNDANT_KNIGHT = -8;
const short REDUNDANT_QUEEN = -20;

uint8_t MFLAG_EG = 1; //do endgame evaluation
uint8_t MFLAG_KING_ATTACK_FORCE_W = 2; //do  king attack evaluation (white)
uint8_t MFLAG_KING_ATTACK_FORCE_B = 4; //do king attack evaluation (black)
uint8_t MFLAG_IMBALANCE = 8; //material imbalance
uint8_t MFLAG_MATING_POWER_W = 16;
uint8_t MFLAG_MATING_POWER_B = 32;

const short TRADEDOWN_PAWNS_MUL[9] = {
    210, 226, 238, 248, 256, 256, 256, 256, 256
};

const short ATTACKED_PIECE = -32; //piece attacked by a pawn

/*******************************************************************************
 * Pawn Values 
 *******************************************************************************/

uint8_t PFLAG_CLOSED_CENTER = 1;

const TScore DEFENDED_PAWN[2] = {S(0, 2), S(2, 4)}; //closed, open file
const TScore ISOLATED_PAWN[2] = {S(-10, -20), S(-20, -20)}; //closed, open file
const TScore WEAK_PAWN[2] = {S(-12, -8), S(-16, -10)}; //closed, open file
const TScore DOUBLED_PAWN = S(-10, -20);

const TScore PASSED_PAWN[64] = {
    S(0, 0), S(0, 0), S(0, 0), S(0, 0), S(0, 0), S(0, 0), S(0, 0), S(0, 0),
    S(150, 170), S(140, 160), S(130, 150), S(120, 140), S(120, 140), S(130, 150), S(140, 160), S(150, 170),
    S(65, 100), S(50, 80), S(50, 80), S(40, 60), S(40, 60), S(50, 80), S(50, 80), S(65, 100),
    S(40, 60), S(30, 40), S(30, 40), S(20, 30), S(20, 30), S(30, 40), S(30, 40), S(40, 60),
    S(20, 30), S(15, 20), S(15, 20), S(10, 15), S(10, 15), S(15, 20), S(15, 20), S(20, 30),
    S(10, 20), S(8, 10), S(8, 10), S(8, 10), S(8, 10), S(8, 10), S(8, 10), S(10, 20),
    S(10, 20), S(8, 10), S(8, 10), S(8, 10), S(8, 10), S(8, 10), S(8, 10), S(10, 20),
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
    9, 9, 9, 9, 9, 9, 9, 9,
    9, 9, 9, 9, 9, 9, 9, 9,
    9, 9, 9, 9, 9, 9, 9, 9,
    6, 6, 7, 8, 8, 7, 6, 6,
    3, 3, 4, 5, 5, 4, 3, 3,
    1, 1, 2, 4, 4, 2, 1, 1,
    0, 0, 1, 3, 3, 1, 0, 0,
    0, 0, 1, 3, 3, 1, 0, 0
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
    0, 0, 0, 0, 0, 0, 0, 0,
    0, 0, 0, 0, 0, 0, 0, 0,
    1, 1, 1, 1, 1, 1, 1, 1,
    1, 1, 1, 1, 1, 1, 1, 1,
    2, 2, 2, 2, 2, 2, 2, 2,
    0, 0, 0, 0, 0, 0, 0, 0,
    0, 0, 0, 0, 0, 0, 0, 0
};

const int8_t SHELTER_OPEN_FILES[4] = {//attack units for having open files on our king
    -1, 2, 3, 4
};

const int8_t SHELTER_OPEN_EDGE_FILE = 3; //attack units for open file on the edge (e.g. open h-line)

const int8_t SHELTER_CASTLING_KINGSIDE = -3; //attack units for having the option to safely  castle kingside 

const int8_t SHELTER_CASTLING_QUEENSIDE = -2; //attack units for having the right to safely castle queenside

const TScore BLOCKED_CENTER_PAWN = S(-10, -4);

#define KA_ENCODE(p,s) (MIN(p,3)|(MIN(s,15)<<2))
#define KA_UNITS(k) ((k) & 3)
#define KA_SQUARES(k) ((k) >> 2)

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

const TScore KNIGHT_OUTPOST[64] = {
    S(0, 0), S(0, 0), S(0, 0), S(0, 0), S(0, 0), S(0, 0), S(0, 0), S(0, 0),
    S(8, 2), S(8, 2), S(8, 2), S(8, 2), S(8, 2), S(8, 2), S(8, 2), S(8, 2),
    S(8, 2), S(8, 2), S(12, 2), S(16, 2), S(16, 2), S(12, 2), S(8, 2), S(8, 2),
    S(8, 2), S(8, 2), S(8, 2), S(8, 2), S(8, 2), S(8, 2), S(8, 2), S(8, 2),
    S(8, 2), S(8, 2), S(8, 2), S(8, 2), S(8, 2), S(8, 2), S(8, 2), S(8, 2),
    S(8, 2), S(8, 2), S(8, 2), S(8, 2), S(8, 2), S(8, 2), S(8, 2), S(8, 2),
    S(0, 0), S(0, 0), S(0, 0), S(0, 0), S(0, 0), S(0, 0), S(0, 0), S(0, 0),
    S(0, 0), S(0, 0), S(0, 0), S(0, 0), S(0, 0), S(0, 0), S(0, 0), S(0, 0)
};

/*******************************************************************************
 * Bishop Values 
 *******************************************************************************/

const TScore BISHOP_MOBILITY[14] = {
    S(-30, -30), S(-20, -20), S(-12, -12), S(-6, -6),
    S(-2, -2), S(0, 0), S(4, 4), S(8, 8), S(12, 12),
    S(14, 14), S(16, 16), S(18, 18), S(20, 20), S(22, 22)
};

const TScore TRAPPED_BISHOP = S(-60, -80);

const TScore ACTIVE_BISHOP = S(6, 6);

U64 BISHOP_PATTERNS[2] = {//black, white
    BIT(d6) | BIT(d7) | BIT(e6) | BIT(d7) | BIT(a2) | BIT(h2),
    BIT(d3) | BIT(d2) | BIT(e3) | BIT(d2) | BIT(a7) | BIT(h7),
};

const TScore BISHOP_OUTPOST[64] = {
    S(-10, -10), S(4, 2), S(4, 2), S(4, 2), S(4, 2), S(4, 2), S(4, 2), S(-10, -10),
    S(4, 2), S(8, 2), S(8, 2), S(8, 2), S(8, 2), S(8, 2), S(8, 2), S(4, 2),
    S(4, 2), S(8, 2), S(8, 2), S(8, 2), S(8, 2), S(8, 2), S(8, 2), S(4, 2),
    S(4, 2), S(8, 2), S(8, 2), S(8, 2), S(8, 2), S(8, 2), S(8, 2), S(4, 2),
    S(4, 2), S(8, 2), S(8, 2), S(8, 2), S(8, 2), S(8, 2), S(8, 2), S(4, 2),
    S(4, 2), S(8, 2), S(8, 2), S(8, 2), S(8, 2), S(8, 2), S(8, 2), S(4, 2),
    S(0, 0), S(0, 0), S(0, 0), S(0, 0), S(0, 0), S(0, 0), S(0, 0), S(0, 0),
    S(0, 0), S(0, 0), S(0, 0), S(0, 0), S(0, 0), S(0, 0), S(0, 0), S(0, 0)
};

/*******************************************************************************
 * Rook Values 
 *******************************************************************************/

const TScore ROOK_7TH = S(10, 10);
const TScore ROOK_1ST = S(10, 0); //back rank protection
const TScore ROOK_SEMIOPEN_FILE = S(10, 10);
const TScore ROOK_OPEN_FILE = S(17, 17);
const TScore ROOK_GOOD_SIDE = S(8, 16); //Rule of Tarrasch 
const TScore ROOK_WRONG_SIDE = S(-8, -16);
const TScore ROOK_CLOSED_FILE = S(-5, -5);
const short ROOK_ATTACK = 12;

U64 ROOK_PATTERNS[2] = {//black, white
    BIT(h8) | BIT(g8) | BIT(h7) | BIT(g7) | BIT(a8) | BIT(b8) | BIT(a7) | BIT(b7),
    BIT(h1) | BIT(g1) | BIT(h2) | BIT(g2) | BIT(a1) | BIT(b1) | BIT(a2) | BIT(b2)
};

const TScore TRAPPED_ROOK = S(-40, -80);

/*******************************************************************************
 * Queen Values
 *******************************************************************************/

const TScore QUEEN_MOBILITY[29] = {
    S(-10, -20), S(-4, -10), S(-2, -4), S(0, -2), S(1, 0), S(2, 2), S(3, 4), S(4, 6),
    S(5, 8), S(6, 10), S(6, 12), S(7, 14), S(7, 16), S(8, 17), S(8, 18), S(9, 19),
    S(9, 20), S(10, 21), S(10, 21), S(11, 22), S(11, 22), S(11, 22), S(12, 23), S(12, 23),
    S(13, 23), S(13, 24), S(13, 24), S(14, 25), S(14, 25)
};

/*******************************************************************************
 * Main evaluation function
 *******************************************************************************/

int evaluate(TSearch * sd) {
    if (sd->stack->eval_result != SCORE_INVALID) {
        return sd->stack->eval_result;
    }

    sd->stack->equal_pawns = sd->pos->current_ply > 0
            && sd->pos->stack->pawn_hash == (sd->pos->stack - 1)->pawn_hash
            && (sd->stack - 1)->eval_result != SCORE_INVALID;

    bool wtm = sd->pos->stack->wtm;
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
    score->add(evaluateKingAttack(sd, WHITE)); //must be after piece evals
    score->sub(evaluateKingAttack(sd, BLACK)); //must be after piece evals
    score->add(TEMPO[wtm]);
    result += score->get(sd->stack->phase);
    sd->stack->eg_score = result;
    if (sd->stack->material_flags & MFLAG_EG) {
        sd->stack->eg_score = result;
        result = evaluateEndgame(sd, result);
    }
    if (!wtm) {
        result = -result;
        sd->stack->eg_score = -sd->stack->eg_score;
    }

    result = (result / GRAIN_SIZE) * GRAIN_SIZE;
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
    if (sd->pos->current_ply > 0 &&
            (sd->pos->stack - 1)->material_hash == sd->pos->stack->material_hash
            && (sd->stack - 1)->eval_result != SCORE_INVALID) {
        sd->stack->material_score = (sd->stack - 1)->material_score;
        sd->stack->phase = (sd->stack - 1)->phase;
        sd->stack->material_flags = (sd->stack - 1)->material_flags;
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

    /*
     * Game phase
     */
    int phase = MAX_GAMEPHASES /* 16 */
            - (wminors + bminors) /* max: 8 */
            - (wrooks + brooks) /* max:4 */
            - 2 * (wqueens + bqueens) /* max: 4 */;
    phase = MAX(0, phase);

    /*
     * Material count evaluation
     */
    if (wknights != bknights) {
        result.mg += (wknights - bknights) * SVKNIGHT.mg;
        result.eg += (wknights - bknights) * SVKNIGHT.eg;
        if (wknights > 1) {
            result.add(REDUNDANT_KNIGHT);
        }
        if (bknights > 1) {
            result.sub(REDUNDANT_KNIGHT);
        }
    }
    if (wbishops != bbishops) {
        result.mg += (wbishops - bbishops) * SVBISHOP.mg;
        result.eg += (wbishops - bbishops) * SVBISHOP.eg;
    }
    if (wrooks != brooks) {
        result.mg += (wrooks - brooks) * SVROOK.mg;
        result.eg += (wrooks - brooks) * SVROOK.eg;
        if (wrooks > 1) {
            result.add(REDUNDANT_ROOK);
        }
        if (brooks > 1) {
            result.sub(REDUNDANT_ROOK);
        }
    }
    if (wqueens != bqueens) {
        result.mg += (wqueens - bqueens) * SVQUEEN.mg;
        result.eg += (wqueens - bqueens) * SVQUEEN.eg;
        if (wqueens > 1) {
            result.add(REDUNDANT_QUEEN);
        }
        if (bqueens > 1) {
            result.sub(REDUNDANT_QUEEN);
        }
    }
    if (wpawns != bpawns) {
        result.mg += (wpawns - bpawns) * SVPAWN.mg;
        result.eg += (wpawns - bpawns) * SVPAWN.eg;
    }

    /*
     * Material Balance
     */
    uint8_t flags = 0;
    bool balance = (wminors == bminors) && (wrooks + 2 * wqueens) == (brooks + 2 * bqueens);
    if (!balance) { //material imbalance

        flags |= MFLAG_IMBALANCE;
        int minors_ix = MAX(0, 4 + wminors - bminors);
        int majors_ix = MAX(0, 4 + wrooks + 2 * wqueens - brooks - 2 * bqueens);
        result.add(IMBALANCE[MIN(majors_ix, 8)][MIN(minors_ix, 8)]);
    }

    /*
     * Set Flags
     */
    bool mating_power_w = wrooks || wqueens || wminors > 2 || (wminors == 2 && wbishops > 0);
    bool mating_power_b = brooks || bqueens || bminors > 2 || (bminors == 2 && bbishops > 0);

    if (mating_power_w) {
        flags |= MFLAG_MATING_POWER_W;
        if (wqueens > 0 && (wpieces > 2 || wrooks > 0 || wqueens > 1)) {
            flags |= MFLAG_KING_ATTACK_FORCE_W;
        }
    }
    if (mating_power_b) {
        flags |= MFLAG_MATING_POWER_B;
        if (bqueens > 0 && (bpieces > 2 || brooks > 0 || bqueens > 1)) {
            flags |= MFLAG_KING_ATTACK_FORCE_B;
        }
    }
    if (wpawns <= 1 || bpawns <= 1 || !mating_power_w || !mating_power_b) {
        flags |= MFLAG_EG;
    }

    /*
     * Store result in material table and return
     */
    int value = result.get(phase);
    sd->stack->material_score = value;
    sd->stack->material_flags = flags;
    sd->stack->phase = phase;
    sd->hashTable->mtStore(sd);
    return value;
}

void init_pst_store(TScore scores[], int wpiece) {
    /*  tuned multipliers:        X    P    N    B    R    Q    K  */
    const double PCMUL_MG[7] = {0.0, 0.6, 1.0, 0.6, 1.0, 1.0, 1.0};
    const double PCMUL_EG[7] = {0.0, 1.0, 1.0, 0.6, 1.0, 1.0, 1.0};
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
            scores[sq].mg *= PCMUL_MG[wpiece];
            scores[sq].eg *= PCMUL_EG[wpiece];
            scores[sq].round();
        }
        int isq = FLIP_SQUARE(sq);
        PST[wpiece][isq].set(scores[sq]);
    }
}

void init_pst() {
    TScore scores[64];
    const TScore mobility_weight[64] = {
        S(2, 2), S(2, 2), S(2, 2), S(2, 2), S(2, 2), S(2, 2), S(2, 2), S(2, 2),
        S(2, 2), S(2, 2), S(2, 2), S(2, 2), S(2, 2), S(2, 2), S(2, 2), S(2, 2),
        S(2, 2), S(2, 2), S(2, 2), S(2, 2), S(2, 2), S(2, 2), S(2, 2), S(2, 2),
        S(2, 2), S(2, 2), S(2, 2), S(3, 2), S(3, 2), S(2, 2), S(2, 2), S(2, 2),
        S(1, 2), S(1, 2), S(1, 2), S(2, 2), S(2, 2), S(1, 2), S(1, 2), S(1, 2),
        S(1, 2), S(1, 2), S(1, 2), S(1, 2), S(1, 2), S(1, 2), S(1, 2), S(1, 2),
        S(1, 2), S(1, 2), S(1, 2), S(1, 2), S(1, 2), S(1, 2), S(1, 2), S(1, 2),
        S(1, 2), S(1, 2), S(1, 2), S(1, 2), S(1, 2), S(1, 2), S(1, 2), S(1, 2)
    };
    const short ROOK_FILE_BONUS[8] = {-4, -4, 0, 4, 4, 0, -4, -4};
    const short PAWN_FILE[8] = {-15, -5, 0, 10, 10, 0, -5, -15};
    const short PROGRESS[8] = {-3, -2, -1, 0, 1, 2, 1, 0};

    //Pawn
    for (int sq = a1; sq <= h8; sq++) {
        scores[sq].clear();
        U64 bbsq = BIT(sq);
        if ((bbsq & RANK_1) || (bbsq & RANK_8)) {
            continue;
        }
        scores[sq].add(mobility_weight[FLIP_SQUARE(sq)]);
        U64 caps = WPAWN_CAPTURES[sq];
        while (caps) {
            int ix = POP(caps);
            scores[sq].add(mobility_weight[FLIP_SQUARE(ix)]);
        }
        scores[sq].mul(8);
        scores[sq].mg += PAWN_FILE[FILE(sq)];
        if (bbsq & CENTER) {
            scores[sq].mg += 5;
        }
        scores[sq].eg = 0;
    }
    init_pst_store(scores, WPAWN);

    //Knight
    for (int sq = a1; sq < 64; sq++) {
        scores[sq].clear();
        scores[sq].add(mobility_weight[FLIP_SQUARE(sq)]);
        U64 caps = KNIGHT_MOVES[sq];
        while (caps) {
            int ix = POP(caps);
            scores[sq].add(mobility_weight[FLIP_SQUARE(ix)]);
        }
        scores[sq].mul(3); //mobility is extra important because knights move slow
        scores[sq].add(KNIGHT_RANK[RANK(sq)]);
    }
    init_pst_store(scores, WKNIGHT);

    //Bishop
    for (int sq = a1; sq < 64; sq++) {
        scores[sq].clear();
        scores[sq].add(mobility_weight[FLIP_SQUARE(sq)]);
        U64 caps = BISHOP_MOVES[sq];
        while (caps) {
            int ix = POP(caps);
            scores[sq].add(mobility_weight[FLIP_SQUARE(ix)]);
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
        scores[sq].mg += ROOK_FILE_BONUS[FILE(sq)];
    }
    init_pst_store(scores, WROOK);

    //Queen
    for (int sq = a1; sq < 64; sq++) {
        scores[sq].clear();
        scores[sq].add(mobility_weight[FLIP_SQUARE(sq)]);
        U64 caps = QUEEN_MOVES[sq];
        while (caps) {
            int ix = POP(caps);
            scores[sq].add(mobility_weight[FLIP_SQUARE(ix)]);
        }
        scores[sq].mg = ROOK_FILE_BONUS[FILE(sq)] / 2;
        scores[sq].mg += sq <= h1 ? -4 : 0;
    }
    init_pst_store(scores, WQUEEN);

    //King
    for (int sq = a1; sq < 64; sq++) {
        scores[sq].clear();
        scores[sq].add(mobility_weight[FLIP_SQUARE(sq)]);
        U64 caps = KING_MOVES[sq];
        while (caps) {
            int ix = POP(caps);
            scores[sq].add(mobility_weight[FLIP_SQUARE(ix)]);
        }
        if (BIT(sq) & LARGE_CENTER) {
            scores[sq].eg += 4;
        }
        if (BIT(sq) & CENTER) {
            scores[sq].eg += 5;
        }
        if (BIT(sq) & EDGE) {
            scores[sq].eg -= 2;
        }
        scores[sq].eg += PROGRESS[RANK(sq)];
        scores[sq].mg = 0;
        scores[sq].eg *= 3;
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
        sd->stack->attack[WHITE] = (sd->stack - 1)->attack[WHITE];
        sd->stack->attack[BLACK] = (sd->stack - 1)->attack[BLACK];
        sd->stack->king_attack_zone[WHITE] = (sd->stack - 1)->king_attack_zone[WHITE];
        sd->stack->king_attack_zone[BLACK] = (sd->stack - 1)->king_attack_zone[BLACK];
        sd->stack->king_attack[WPAWN] = (sd->stack - 1)->king_attack[WPAWN];
        sd->stack->king_attack[BPAWN] = (sd->stack - 1)->king_attack[BPAWN];
        sd->stack->pawn_flags = (sd->stack - 1)->pawn_flags;
        return &sd->stack->pawn_score;
    }

    //set mobility and attack masks
    TBoard * pos = sd->pos;
    int wkpos = *pos->white_king_sq;
    int bkpos = *pos->black_king_sq;
    sd->stack->mob[WHITE] = ~(*pos->pawns[WHITE] | pos->pawnAttacks(BLACK) | *pos->kings[WHITE]);
    sd->stack->mob[BLACK] = ~(*pos->pawns[BLACK] | pos->pawnAttacks(WHITE) | *pos->kings[BLACK]);
    sd->stack->attack[WHITE] = (*pos->pawns[BLACK] | *pos->kings[BLACK]);
    sd->stack->attack[BLACK] = (*pos->pawns[WHITE] | *pos->kings[WHITE]);
    sd->stack->king_attack_zone[WHITE] = MagicQueenMoves(bkpos, pos->pawnsAndKings()) & sd->stack->mob[WHITE];
    sd->stack->king_attack_zone[BLACK] = MagicQueenMoves(wkpos, pos->pawnsAndKings()) & sd->stack->mob[BLACK];

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

    sd->stack->king_attack[WPAWN] = 0;
    sd->stack->king_attack[BPAWN] = 0;
    sd->stack->pawn_flags = 0;
    pawn_score->clear();

    U64 passers = 0;
    U64 openW = ~FILEFILL(pos->white_pawns);
    U64 openB = ~FILEFILL(pos->black_pawns);
    U64 wUp = UP1(pos->white_pawns);
    U64 bDown = DOWN1(pos->black_pawns);
    U64 upLeftW = UPLEFT1(pos->white_pawns);
    U64 upRightW = UPRIGHT1(pos->white_pawns);
    U64 downLeftB = DOWNLEFT1(pos->black_pawns);
    U64 downRightB = DOWNRIGHT1(pos->black_pawns);
    U64 wAttacks = upRightW | upLeftW;
    U64 bAttacks = downLeftB | downRightB;
    U64 wBlocked = DOWN1(wUp & pos->black_pawns) & ~bAttacks;
    U64 bBlocked = UP1(bDown & pos->white_pawns) & ~wAttacks;
    U64 wSafe = (upLeftW & upRightW) | ~bAttacks | ((upLeftW ^ upRightW) & ~(downLeftB & downRightB));
    U64 bSafe = (downLeftB & downRightB) | ~wAttacks | ((downLeftB ^ downRightB) & ~(upLeftW & upRightW));
    U64 wMoves = UP1(pos->white_pawns & ~wBlocked) & wSafe;
    U64 bMoves = DOWN1(pos->black_pawns & ~bBlocked) & bSafe;
    wMoves |= UP1(wMoves & RANK_3 & ~bAttacks & ~DOWN1((pos->black_pawns | pos->white_pawns))) & wSafe;
    bMoves |= DOWN1(bMoves & RANK_6 & ~wAttacks & ~UP1((pos->white_pawns | pos->black_pawns))) & bSafe;
    U64 wAttackRange = wAttacks | UPLEFT1(wMoves) | UPRIGHT1(wMoves);
    U64 bAttackRange = bAttacks | DOWNLEFT1(bMoves) | DOWNRIGHT1(bMoves);
    U64 wIsolated = pos->white_pawns & ~(FILEFILL(wAttacks));
    U64 bIsolated = pos->black_pawns & ~(FILEFILL(bAttacks));
    U64 wDoubled = DOWN1(southFill(pos->white_pawns)) & pos->white_pawns;
    U64 bDoubled = UP1(northFill(pos->black_pawns)) & pos->black_pawns;

    U64 kcz_w = KING_ZONE[wkpos];
    U64 kcz_b = KING_ZONE[bkpos];

    int blocked_center_pawns = 0;

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
        bool passed = !doubled && !(up & (bAttacks | pos->black_pawns));
        bool candidate = open && !doubled && !passed && !(up & ~wSafe);

#ifdef PRINT_PAWN_EVAL
        std::cout << "WP " << PRINT_SQUARE(sq) << ": ";
#endif

        pawn_score->add(PST[WPAWN][isq]);

#ifdef PRINT_PAWN_EVAL        
        std::cout << "pst: " << PRINT_SCORE(PST[WPAWN][isq]);
#endif

        if (isolated) {
            pawn_score->add(ISOLATED_PAWN[open]);

#ifdef PRINT_PAWN_EVAL
            std::cout << "isolated: " << PRINT_SCORE(ISOLATED_PAWN[open]);
#endif

        } else if (weak) {
            pawn_score->add(WEAK_PAWN[open]);

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

        }

        if (blocked && (sqBit & CENTER)) {
            blocked_center_pawns++;
        }

        sd->stack->king_attack[WPAWN] += popCount0(WPAWN_CAPTURES[sq] & kcz_b);


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
        bool passed = !doubled && !(down & (wAttacks | pos->white_pawns));
        bool candidate = open && !doubled && !passed && !(down & ~bSafe);

#ifdef PRINT_PAWN_EVAL
        std::cout << "BP " << PRINT_SQUARE(sq) << ": ";
#endif

        pawn_score->sub(PST[WPAWN][sq]);

#ifdef PRINT_PAWN_EVAL
        std::cout << "pst: " << PRINT_SCORE(PST[WPAWN][sq]);
#endif

        if (isolated) {
            pawn_score->sub(ISOLATED_PAWN[open]);
#ifdef PRINT_PAWN_EVAL
            std::cout << "isolated: " << PRINT_SCORE(ISOLATED_PAWN[open]);
#endif
        } else if (weak) {
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

        }

        if (blocked && (sqBit & CENTER)) {
            blocked_center_pawns++;
        }

#ifdef PRINT_PAWN_EVAL
        std::cout << std::endl;
#endif

        sd->stack->king_attack[BPAWN] += popCount0(BPAWN_CAPTURES[sq] & kcz_w);

    }

    if (blocked_center_pawns > 2) {
        sd->stack->pawn_flags |= PFLAG_CLOSED_CENTER;
    }

#ifdef PRINT_PAWN_EVAL
    std::cout << "total: ";
    pawn_score->print();
    std::cout << std::endl;
#endif

    pawn_score->add(PST[WKING][ISQ(wkpos, WHITE)]);
    pawn_score->sub(PST[WKING][ISQ(bkpos, BLACK)]);

    /*
     * 4. Calculate King Shelter Attack units
     */
    sd->stack->king_attack[BPAWN] += SHELTER_KPOS[FLIP_SQUARE(wkpos)];
#ifdef PRINT_PAWN_EVAL
    std::cout << "attack on WK (pos): " << (int) sd->stack->king_attack[BPAWN] << std::endl;
#endif

    //1. reward having the right to castle
    if (pos->stack->castling_flags & CASTLE_K
            && ((pos->matrix[h2] == WPAWN && pos->matrix[g2] == WPAWN)
            || (pos->matrix[f2] == WPAWN && pos->matrix[h2] == WPAWN && pos->matrix[g3] == WPAWN)
            || (pos->matrix[h3] == WPAWN && pos->matrix[g2] == WPAWN && pos->matrix[f2] == WPAWN))) {
        sd->stack->king_attack[BPAWN] += SHELTER_CASTLING_KINGSIDE;
    } else if (pos->stack->castling_flags & CASTLE_Q
            && ((pos->matrix[a2] == WPAWN && pos->matrix[b2] == WPAWN && pos->matrix[c2] == WPAWN)
            || (pos->matrix[a2] == WPAWN && pos->matrix[b3] == WPAWN && pos->matrix[c2] == WPAWN))) {
        sd->stack->king_attack[BPAWN] += SHELTER_CASTLING_QUEENSIDE;
    }
#ifdef PRINT_PAWN_EVAL
    std::cout << "attack on WK (castling): " << (int) sd->stack->king_attack[BPAWN] << std::endl;
#endif

    //2. rewards for having shelter and storm pawns
    U64 kingFront = (FORWARD_RANKS[RANK(wkpos)] | KING_MOVES[wkpos]) & PAWN_SCOPE[FILE(wkpos)];
    U64 shelterPawns = kingFront & pos->white_pawns;
    while (shelterPawns) {
        int sq = POP(shelterPawns);
        sd->stack->king_attack[BPAWN] += SHELTER_PAWN[FLIP_SQUARE(sq)];
    }
#ifdef PRINT_PAWN_EVAL
    std::cout << "attack on WK (shelter): " << (int) sd->stack->king_attack[BPAWN] << std::endl;
#endif

    U64 stormPawns = kingFront & pos->black_pawns;
    while (stormPawns) {
        int sq = POP(stormPawns);
        sd->stack->king_attack[BPAWN] += STORM_PAWN[FLIP_SQUARE(sq)];
    }
#ifdef PRINT_PAWN_EVAL
    std::cout << "attack on WK (storm): " << (int) sd->stack->king_attack[BPAWN] << std::endl;
#endif

    //3. penalize (half)open files on the king
    U64 open = (openW | openB) & kingFront & RANK_8;
    if (open) {
        sd->stack->king_attack[BPAWN] += SHELTER_OPEN_FILES[popCount0(open)];
        if (open & (FILE_A | FILE_H)) {
            sd->stack->king_attack[BPAWN] += SHELTER_OPEN_EDGE_FILE;
        }
    }

#ifdef PRINT_PAWN_EVAL
    std::cout << "attack on WK (open files): " << (int) sd->stack->king_attack[BPAWN] << std::endl;
#endif

    //black king shelter
    sd->stack->king_attack[WPAWN] += SHELTER_KPOS[bkpos];

#ifdef PRINT_PAWN_EVAL
    std::cout << "attack on BK (pos): " << (int) sd->stack->king_attack[WPAWN] << std::endl;
#endif
    //1. reward having the right to castle safely
    if (pos->stack->castling_flags & CASTLE_k
            && ((pos->matrix[h7] == BPAWN && pos->matrix[g7] == BPAWN)
            || (pos->matrix[f7] == BPAWN && pos->matrix[h7] == BPAWN && pos->matrix[g6] == BPAWN)
            || (pos->matrix[h6] == BPAWN && pos->matrix[g7] == BPAWN && pos->matrix[f7] == BPAWN))) {
        sd->stack->king_attack[WPAWN] += SHELTER_CASTLING_KINGSIDE;
    } else if (pos->stack->castling_flags & CASTLE_q
            && ((pos->matrix[a7] == BPAWN && pos->matrix[b7] == BPAWN && pos->matrix[c7] == BPAWN)
            || (pos->matrix[a7] == BPAWN && pos->matrix[b6] == BPAWN && pos->matrix[c7] == BPAWN))) {
        sd->stack->king_attack[WPAWN] += SHELTER_CASTLING_QUEENSIDE;
    }

#ifdef PRINT_PAWN_EVAL
    std::cout << "attack on BK (castling): " << (int) sd->stack->king_attack[WPAWN] << std::endl;
#endif
    //2. reward having pawns in front of the king
    kingFront = (BACKWARD_RANKS[RANK(bkpos)] | KING_MOVES[bkpos]) & PAWN_SCOPE[FILE(bkpos)];
    shelterPawns = kingFront & pos->black_pawns;
    while (shelterPawns) {
        int sq = POP(shelterPawns);
        sd->stack->king_attack[WPAWN] += SHELTER_PAWN[sq];
    }

#ifdef PRINT_PAWN_EVAL
    std::cout << "attack on BK (shelter): " << (int) sd->stack->king_attack[WPAWN] << std::endl;
#endif
    stormPawns = kingFront & pos->white_pawns;
    while (stormPawns) {
        int sq = POP(stormPawns);
        sd->stack->king_attack[WPAWN] += STORM_PAWN[sq];
    }

#ifdef PRINT_PAWN_EVAL
    std::cout << "attack on BK (storm): " << (int) sd->stack->king_attack[WPAWN] << std::endl;
#endif
    //3. penalize (half)open files on the king
    open = (openW | openB) & kingFront & RANK_1;
    if (open) {
        sd->stack->king_attack[WPAWN] += SHELTER_OPEN_FILES[popCount0(open)];
        if (open & (FILE_A | FILE_H)) {
            sd->stack->king_attack[WPAWN] += SHELTER_OPEN_EDGE_FILE;
        }
    }

#ifdef PRINT_PAWN_EVAL
    std::cout << "attack on BK (open files): " << (int) sd->stack->king_attack[WPAWN] << std::endl;
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
    sd->stack->king_attack[pc] = 0;

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
            sd->stack->king_attack[pc] = (sd->stack - 1)->king_attack[pc];
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
    int kpos = *pos->king_sq[them];
    U64 kaz = KNIGHT_MOVES[kpos] | KING_ZONE[kpos]; //king attack zone
    int ka_units = 0;
    int ka_squares = 0;
    for (int i = 0; i < pp->count; i++) {
        int sq = pp->squares[i];
        result->add(PST[WKNIGHT][ISQ(sq, us)]);
        U64 moves = KNIGHT_MOVES[sq] & sd->stack->mob[us];
        int mob_count = popCount0(moves);
        result->add(KNIGHT_MOBILITY[mob_count]);
        result->add(KNIGHT_PAWN_WIDTH[pawn_width]);
        result->add(KNIGHT_PAWN_COUNT[pawn_count]);

        if (pos->attackedByPawn(sq, us)) {
            result->add(KNIGHT_OUTPOST[ISQ(sq, us)]);
        }
        if (pos->attackedByPawn(sq, them)) {
            result->add(ATTACKED_PIECE);
        }
        if (moves & kaz) {
            ka_units++;
            ka_squares += popCount(moves & kaz);
        }
    }
    sd->stack->king_attack[pc] = KA_ENCODE(ka_units, ka_squares);
    return result;
}

inline TScore * evaluateBishops(TSearch * sd, bool us) {
    TScore * result = &sd->stack->bishop_score[us];
    TBoard * pos = sd->pos;
    int pc = BISHOP[us];
    result->clear();
    sd->stack->king_attack[pc] = 0;

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
            sd->stack->king_attack[pc] = (sd->stack - 1)->king_attack[pc];
            return result;
        }
    }

    /*
     * 3. Calculate the score and store on the stack
     */
    TPiecePlacement * pp = &pos->pieces[pc];

    //bishop pair
    if (pp->count > 1 && pos->bishopPair(us) && (sd->stack->pawn_flags & PFLAG_CLOSED_CENTER) == 0) {
        result->add(VBISHOPPAIR);
    }

    U64 occ = pos->pawnsAndKings();
    bool them = !us;
    int kpos = *pos->king_sq[them];
    U64 kaz = (sd->stack->king_attack_zone[us] & BISHOP_MOVES[kpos]) | KING_ZONE[kpos]; //king attack zone; 
    int ka_units = 0;
    int ka_squares = 0;
    for (int i = 0; i < pp->count; i++) {
        int sq = pp->squares[i];
        result->add(PST[WBISHOP][ISQ(sq, us)]);
        U64 moves = MagicBishopMoves(sq, occ) & sd->stack->mob[us];
        int count = popCount0(moves);
        result->add(BISHOP_MOBILITY[count]);
        if (moves & sd->stack->attack[us]) {
            result->add(ACTIVE_BISHOP);
        } else {
            result->sub(ACTIVE_BISHOP);
        }
        if (pos->attackedByPawn(sq, us)) {
            result->add(BISHOP_OUTPOST[ISQ(sq, us)]);
        }
        if (pos->attackedByPawn(sq, them)) {
            result->add(ATTACKED_PIECE);
        }

        //patterns
        if (BIT(sq) & BISHOP_PATTERNS[us]) {
            if (us == WHITE) {
                if (((sq == d3 || sq == d4) && (pos->matrix[d2] == WPAWN || pos->matrix[d3] == WPAWN))) {
                    result->add(BLOCKED_CENTER_PAWN);
                } else if (((sq == e3 || sq == e4) && (pos->matrix[e2] == WPAWN || pos->matrix[e3] == WPAWN))) {
                    result->add(BLOCKED_CENTER_PAWN);
                } else if ((sq == h7 && pos->matrix[g6] == BPAWN && pos->matrix[f7] == BPAWN)
                        || (sq == a7 && pos->matrix[b6] == BPAWN && pos->matrix[c7] == BPAWN)) {
                    result->add(TRAPPED_BISHOP);
                }
            } else if (us == BLACK) {
                if (((sq == d6 || sq == d5) && (pos->matrix[d7] == BPAWN || pos->matrix[d6] == BPAWN))) {
                    result->add(BLOCKED_CENTER_PAWN);
                } else if (((sq == e6 || sq == e5) && (pos->matrix[e7] == BPAWN || pos->matrix[e6] == BPAWN))) {
                    result->add(BLOCKED_CENTER_PAWN);
                } else if ((sq == h2 && pos->matrix[g3] == WPAWN && pos->matrix[f3] == WPAWN)
                        || (sq == a2 && pos->matrix[b3] == WPAWN && pos->matrix[c2] == WPAWN)) {
                    result->add(TRAPPED_BISHOP);
                }
            }
        }
        if (moves & kaz) {
            ka_units++;
            ka_squares += popCount(moves & kaz);
        }
    }
    sd->stack->king_attack[pc] = KA_ENCODE(ka_units, ka_squares);
    return result;
}

inline TScore * evaluateRooks(TSearch * sd, bool us) {
    TScore * result = &sd->stack->rook_score[us];
    TBoard * pos = sd->pos;
    int pc = ROOK[us];
    result->clear();
    sd->stack->king_attack[pc] = 0;

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
            sd->stack->king_attack[pc] = (sd->stack - 1)->king_attack[pc];
            return result;
        }
    }

    /*
     * 3. Calculate the score and store on the stack
     */
    TPiecePlacement * pp = &pos->pieces[ROOK[us]];
    bool them = !us;
    U64 fill[2] = {FILEFILL(pos->black_pawns), FILEFILL(pos->white_pawns)};
    U64 occ = pos->pawnsAndKings();
    if ((*pos->rooks[us] & RANK[us][1]) && (BIT(*pos->king_sq[us]) & (RANK[us][1] | RANK[us][2]))) {
        result->add(ROOK_1ST); //at least one rook is protecting the back rank
    }
    int kpos = *pos->king_sq[them];
    U64 kaz = (sd->stack->king_attack_zone[us] & ROOK_MOVES[kpos]) | KING_ZONE[kpos]; //king attack zone
    int ka_units = 0;
    int ka_squares = 0;

    for (int i = 0; i < pp->count; i++) {
        int sq = pp->squares[i];
        result->add(PST[WROOK][ISQ(sq, us)]);
        U64 bitSq = BIT(sq);
        if (bitSq & fill[us]) {
            result->add(ROOK_CLOSED_FILE);
            //trapped rook pattern
            if (bitSq & ROOK_PATTERNS[us]) {
                int kpos_us = *pos->king_sq[us];
                if (us == WHITE && (kpos_us == g1 || kpos_us == h1 || kpos_us == f1)
                        && (sq == h1 || sq == g1 || sq == h2 || sq == g2)) {
                    result->add(TRAPPED_ROOK);
                } else if (us == WHITE && (kpos_us == a1 || kpos_us == b1 || kpos_us == c1)
                        && (sq == a1 || sq == b1 || sq == a2 || sq == b2)) {
                    result->add(TRAPPED_ROOK);
                } else if (us == BLACK && (kpos_us == g8 || kpos_us == h8 || kpos_us == f8)
                        && (sq == h8 || sq == g8 || sq == h7 || sq == g7)) {
                    result->add(TRAPPED_ROOK);
                } else if (us == BLACK && (kpos_us == a8 || kpos_us == b8 || kpos_us == c8)
                        && (sq == a8 || sq == b8 || sq == a7 || sq == b7)) {
                    result->add(TRAPPED_ROOK);
                }
            }
        } else if (bitSq & fill[them]) {
            result->add(ROOK_SEMIOPEN_FILE);
        } else {
            result->add(ROOK_OPEN_FILE);
        }

        if (pos->attackedByPawn(sq, them)) {
            result->add(ATTACKED_PIECE);
        }

        U64 moves = MagicRookMoves(sq, occ) & sd->stack->mob[us];
        if (moves & sd->stack->attack[us]) {
            result->add(popCount(moves & sd->stack->attack[us]) * ROOK_ATTACK);
        }

        if ((bitSq & RANK[us][7]) && (BIT(*pos->king_sq[them]) & BACKRANKS[us])) {
            result->add(ROOK_7TH);
        }

        //Tarrasch Rule: place rook behind passers
        U64 tpass = moves & sd->stack->passers; //touched passers
        if (tpass) {
            U64 front[2];
            front[BLACK] = southFill(bitSq) & tpass;
            front[WHITE] = northFill(bitSq) & tpass;
            if (front[us] & *pos->pawns[us] & SIDE[them]) { //supporting a passer from behind
                result->add(ROOK_GOOD_SIDE);
            } else if (front[them] & *pos->pawns[them] & SIDE[us]) { //attacking a passer from behind
                result->add(ROOK_GOOD_SIDE);
            } else if (front[them] & *pos->pawns[us] & SIDE[them]) { //supporting from the wrong side
                result->add(ROOK_WRONG_SIDE);
            } else if (front[us] & *pos->pawns[them] & SIDE[us]) { //attacking from the wrong side
                result->add(ROOK_WRONG_SIDE);
            }
        }
        if (moves & kaz) {
            ka_units++;
            ka_squares += popCount(moves & kaz);
        }
    }
    sd->stack->king_attack[pc] = KA_ENCODE(ka_units, ka_squares);
    return result;
}

inline TScore * evaluateQueens(TSearch * sd, bool us) {
    TScore * result = &sd->stack->queen_score[us];
    TBoard * pos = sd->pos;
    int pc = QUEEN[us];
    result->clear();
    sd->stack->king_attack[pc] = 0;

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
            sd->stack->king_attack[pc] = (sd->stack - 1)->king_attack[pc];
            return result;
        }
    }

    /*
     * 3. Calculate the score and store on the stack
     */
    bool them = !us;
    TPiecePlacement * pp = &pos->pieces[QUEEN[us]];
    U64 occ = pos->pawnsAndKings();
    int kpos = *pos->king_sq[them];
    U64 kaz = sd->stack->king_attack_zone[us] | KING_ZONE[kpos]; //king attack zone
    int ka_units = 0;
    int ka_squares = 0;
    for (int i = 0; i < pp->count; i++) {
        int sq = pp->squares[i];
        result->add(PST[WQUEEN][ISQ(sq, us)]);
        U64 moves = MagicQueenMoves(sq, occ) & sd->stack->mob[us];
        int count = popCount0(moves);
        result->add(QUEEN_MOBILITY[count]);
        if (pos->attackedByPawn(sq, them)) {
            result->add(ATTACKED_PIECE);
        }
        result->add(10 - distance_rank(sq, kpos) - distance_file(sq, kpos));
        if (moves & kaz) {
            ka_units++;
            ka_squares += popCount(moves & kaz);
        }
    }
    sd->stack->king_attack[pc] = KA_ENCODE(ka_units, ka_squares);
    return result;
}

inline TScore * evaluatePassers(TSearch * sd, bool us) {
    TScore * result = &sd->stack->passer_score[us];
    result->clear();
    U64 passers = sd->stack->passers & *sd->pos->pawns[us];
    if (passers == 0) {
        return result;
    }
    bool them = !us;
    int unstoppable = 0;
    bool p_vs_k = sd->pos->getPieces(them) == 0;
    U64 exclude = SIDE[us] & ~(RANK_4 | RANK_5);
    while (passers) {
        int sq = POP(passers);
#ifdef PRINT_PASSED_PAWN
        std::cout << "\npassed pawn " << sq << ": ";
#endif
        int ix = us == WHITE ? FLIP_SQUARE(sq) : sq;
        TScore bonus;
        bonus.set(PASSED_PAWN[ix]);
        result->add(bonus);

#ifdef PRINT_PASSED_PAWN
        std::cout << "base ";
        bonus.print();
#endif
        if (p_vs_k) {
            unstoppable = MAX(unstoppable, evaluatePasserVsK(sd, us, sq));
        }
        if (BIT(sq) & exclude) {
            continue;
        }
        bonus.half();
        int r = RANK(sq) - 1;
        if (us == BLACK) {
            r = 5 - r;
        }
        int to = forwardSq(sq, us);

        //consider distance of king
        if (unstoppable == 0) {
            int kdist_us_bonus = distance(*sd->pos->king_sq[us], to) * r * (r - 1);
            int kdist_them_bonus = distance(*sd->pos->king_sq[them], to) * r * (r - 1) * 2;


#ifdef PRINT_PASSED_PAWN
            std::cout << "distance: " << kdist_them_bonus - kdist_us_bonus;
#endif
            result->add(0, kdist_them_bonus - kdist_us_bonus);
        }

        //connected passers
        U64 bit_sq = BIT(sq);
        U64 connection_mask = RIGHT1(bit_sq) | LEFT1(bit_sq);
        if (connection_mask & *sd->pos->pawns[us]) {
            result->add(10, 10 + r * 10);
#ifdef PRINT_PASSED_PAWN
            std::cout << " connected: " << 10 + r * 10;
#endif
        } else {
            bit_sq = BIT(sq + PAWNDIRECTION[them]);
            connection_mask = RIGHT1(bit_sq) | LEFT1(bit_sq);
            if (connection_mask & *sd->pos->pawns[us]) {
                result->add(5, 5 + r * 5);
#ifdef PRINT_PASSED_PAWN
                std::cout << " defended: " << 5 + r * 5;
#endif
            }
        }

        do {
            if (BIT(to) & sd->pos->all_pieces) {
                break; //blocked
            }
            sd->pos->all_pieces ^= BIT(sq); //to include rook/queen xray attacks from behind
            U64 attacks = sd->pos->attacksTo(to);
            sd->pos->all_pieces ^= BIT(sq);
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
    if (path & sd->pos->all_pieces) {
        return 0; //no, the path is blocked
    }
    bool them = !us;
    int kingThem = *sd->pos->king_sq[them];
    int queening_square = FILE(sq) + us * 56;
    if (us == WHITE) {
        if (sq <= h2) {
            sq += 8;
        }
        if (sd->pos->stack->wtm && sq <= h6) {
            sq += 8;
        }
    } else if (us == BLACK) {
        if (sq >= a7) {
            sq -= 8;
        }
        if (sd->pos->stack->wtm == false && sq >= a3) {
            sq -= 8;
        }
    }
    int prank = RANK(sq);
    int qrank = RANK(queening_square);
    int qfile = FILE(queening_square);
    int pdistance = 1 + ABS(qrank - prank);
    int kingUs = *sd->pos->king_sq[us];
    if ((path & KING_MOVES[kingUs]) == path) {
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

const int8_t KING_ATTACK_OFFSET = 9; //perfectly castled king -9 units

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
    0, 0, 0, 0, 0, 0, 0, 0,
    2, 4, 6, 8, 10, 12, 16, 20,
    24, 28, 32, 36, 42, 48, 54, 60,
    66, 72, 78, 84, 92, 100, 108, 116,
    124, 131, 138, 144, 150, 155, 160, 164,
    168, 172, 176, 180, 182, 188, 190, 192,
    194, 196, 198, 200, 202, 204, 206, 208,
    210, 212, 214, 216, 218, 220, 222, 224
};

const int16_t KING_ATTACKERS_MUL[8] = {
    128, 192, 256, 288, 312, 328, 344, 360
};

const int16_t KING_SHELTER_MUL[8] = {
    128, 160, 192, 224, 256, 272, 288, 304
};

inline TScore * evaluateKingAttack(TSearch * sd, bool us) {
    TScore * result = &sd->stack->king_score[us];
    result->clear();
    TBoard * pos = sd->pos;
    if (*pos->queens[us] == 0) {
        return result;
    }
    if (us == WHITE && (sd->stack->material_flags & MFLAG_KING_ATTACK_FORCE_W) == 0) {
        return result;
    }
    if (us == BLACK && (sd->stack->material_flags & MFLAG_KING_ATTACK_FORCE_B) == 0) {
        return result;
    }

    /*
     * 1. Shelter score
     */
    int shelter_ix = RANGE(0, 23, KING_ATTACK_OFFSET + sd->stack->king_attack[PAWN[us]]);
    result->set(KING_SHELTER[shelter_ix], 0);

    U64 kaz = sd->stack->king_attack_zone[us];

#ifdef PRINT_KING_SAFETY
    printBB("\nKing Attack Zone", kaz);
    std::cout << "Shelter: " << shelter_ix << " -> " << (int) KING_SHELTER[shelter_ix];
    std::cout << std::endl;
#endif

    kaz &= ~(RANK[us][7] | RANK[us][8] | KING_MOVES[*pos->king_sq[!us]]);
    result->add(12 * popCount0(kaz), 0);

#ifdef PRINT_KING_SAFETY
    printBB("\nKing Attack Zone (filtered)", kaz);
    std::cout << "Zone: " << 12 * popCount0(kaz);
    std::cout << "\nTotal: ";
    result->print();
    std::cout << std::endl;
#endif

    /*
     * 2. Reduce the shelter score for closed positions and 
     * if little material is left
     */

    if ((sd->stack->pawn_flags & PFLAG_CLOSED_CENTER) != 0) {
        result->half(); //reduce shelter score for closed positions
    }

    /*
     * 3. Piece Attack Score.
     * Stop if the queen is not involved in the attack
     */

    int queen_attack = sd->stack->king_attack[QUEEN[us]];
    if (queen_attack == 0) {
        return result;
    }
    int attackers = 0;
    int ka_units = KA_UNITS(queen_attack) * KING_ATTACK_UNIT[QUEEN[us]];
    int ka_squares = KA_SQUARES(queen_attack);

    /*
     * 4. Get the totals of the Rooks, Bishops and Knights attacks
     */
    for (int pc = KNIGHT[us]; pc < QUEEN[us]; pc++) {
        int attack = sd->stack->king_attack[pc];
        if (attack == 0) {
            continue;
        }
        int n = KA_UNITS(attack);
        attackers += n;
        ka_units += n * KING_ATTACK_UNIT[pc];
        ka_squares += KA_SQUARES(attack);
    }
    if (attackers == 0) {
        return result;
    }

#ifdef PRINT_KING_SAFETY
    std::cout << "Attacking Pieces: " << (attackers + 1) << std::endl;
    std::cout << "Attack Force: " << ka_units << std::endl;
    std::cout << "Controlled Squares: " << ka_squares << std::endl;
#endif

    int piece_attack_score = 0;
    int paix = 2 * ka_units + shelter_ix + ka_squares - 5;
    piece_attack_score = KING_ATTACK[RANGE(0, 63, paix)];

#ifdef PRINT_KING_SAFETY
    std::cout << "Total Piece Attack Index: " << paix << std::endl;
    std::cout << "Piece Attack Score: " << piece_attack_score << std::endl;
#endif

    piece_attack_score = MUL256(piece_attack_score, KING_ATTACKERS_MUL[RANGE(0, 7, attackers)]);

#ifdef PRINT_KING_SAFETY
    std::cout << "Corrected Score (attackers): " << piece_attack_score << std::endl;
#endif

    piece_attack_score = MUL256(piece_attack_score, KING_SHELTER_MUL[RANGE(0, 7, shelter_ix)]);

#ifdef PRINT_KING_SAFETY
    std::cout << "Corrected Score (shelter): " << piece_attack_score << std::endl;
#endif

    result->add(piece_attack_score, 0);

#ifdef PRINT_KING_SAFETY
    std::cout << "Total Piece Attack: " << piece_attack_score << std::endl;
    result->print();
    std::cout << std::endl;
#endif

    return result;
}

inline short evaluateEndgame(TSearch * s, short score) {
    static const int SCORE_SURE_WIN[2] = {-SCORE_WIN, SCORE_WIN};
    static const int BONUS[2] = {-10, 10};
    static const int PAWN_TRADE[9] = {6, 12, 14, 15, 16, 16, 16, 17, 18};

    TBoard * pos = s->pos;
    bool us = (score > 0) || (score == 0 && pos->stack->wtm); //winning side white: 1, black: 0
    bool them = !us;
    int pawn_count[2] = {pos->pieces[BPAWN].count, pos->pieces[WPAWN].count};
    bool has_pieces[2] = {pos->hasPieces(BLACK), pos->hasPieces(WHITE)};

    //endgame with only pawns (KK, KPK, KPPK, KPKP, etc.)
    if (!has_pieces[us] && !has_pieces[them]) {
        assert(s->stack->phase == 16);
        bool utm = us == WHITE && pos->stack->wtm;
        if (opposition(*pos->white_king_sq, *pos->black_king_sq)) {
            score -= 5 * BONUS[utm];
        } else {
            score += 5 * BONUS[utm];
        }
        if (pawn_count[us] == 0 && pawn_count[them] == 0) {
            return score / 8;
        }
        int dpawns = pawn_count[WHITE] - pawn_count[BLACK];
        if (dpawns > 1 || dpawns < -1) {
            score += dpawns * 50;
        }

        //@todo: heuristic / perfect result for KPK, KPKP and KPPKP endgames
        //std::cout << result << " ... ";
        return score;
    }

    bool mating_power[2] = {(s->stack->material_flags & MFLAG_MATING_POWER_B) != 0,
        (s->stack->material_flags & MFLAG_MATING_POWER_W) != 0};

    //we have nothing left to win the game


    if (!mating_power[us] && pawn_count[us] == 0) {
        return score / 32;
    }

    //we have no mating power, so we depend upon our pawns to win
    if (!mating_power[us] && has_pieces[them]) {
        score = score * PAWN_TRADE[pawn_count[us]] / 16;

    }

    bool king_on_edge[2] = {(pos->black_kings & EDGE) != 0, (pos->white_kings & EDGE) != 0};

    //opponent has nothing, we have mating power
    if (pawn_count[them] == 0 && !has_pieces[them] && mating_power[us]) {
        return score + SCORE_SURE_WIN[us] + king_on_edge[them] * BONUS[us];
    }

    bool winning_edge[2] = {score <= -VROOK, score >= VROOK};

    //we have much more material than opponent, opponent has no pawns
    if (pawn_count[them] == 0 && pawn_count[us] > 0 && winning_edge[us] && mating_power[us]) {
        return score + SCORE_SURE_WIN[us] / 8 + king_on_edge[them] * BONUS[us];
    }

    //endgame with only pieces: requires an extra major piece to win
    if (pawn_count[us] == 0 && pawn_count[them] == 0) {
        if (!mating_power[us]) {
            return score / 16;
        }
        if (!winning_edge[us]) {
            return score / 8 + king_on_edge[them] * BONUS[us];
        }
        if (!mating_power[them]) {
            return score + SCORE_SURE_WIN[us] / 4 + king_on_edge[them] * BONUS[us];
        }
        return score + SCORE_SURE_WIN[us] / 8 + king_on_edge[them] * BONUS[us];
    }

    //minor adjustments: reward having pieces, mating power and pawn(s))
    int adjust = has_pieces[us] - has_pieces[them]
            + mating_power[us] - mating_power[them]
            + (pawn_count[them] == 0) - (pawn_count[us] == 0);
    return score + (adjust * BONUS[us]);
}
