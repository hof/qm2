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
 * File: evaluate.cpp
 * Evalution functions for traditional chess
 */

#include "evaluate.h"
#include "search.h"
#include "bits.h"
#include "score.h"

#include "pst.h"
#include "kpk_bb.h"

//#define TRACE_EVAL

namespace {

    enum {
        GRAIN_SIZE = 4
    };
};

namespace eg {
    int eval(search_t * s, const int score);
}

pst_t PST;

int eval_material(search_t * sd);
int eval_unstoppable_pawn(search_t * sd, bool white, int sq);
score_t * eval_pawns_and_kings(search_t * sd);
score_t * eval_knights(search_t * sd, bool white);
score_t * eval_bishops(search_t * sd, bool white);
score_t * eval_rooks(search_t * sd, bool white);
score_t * eval_queens(search_t * sd, bool white);
score_t * eval_passed_pawns(search_t * sd, bool white);
score_t * eval_king_attack(search_t * sd, bool white);

/**
 * Initialize the Piece Square Tables
 */
void init_pst() {
    for (int sq = a1; sq <= h8; sq++) {
        PST[EMPTY][sq].mg = 0;
        PST[EMPTY][sq].eg = 0;
        PST[WPAWN][sq].mg = PST_PAWN_MG[sq];
        PST[WPAWN][sq].eg = PST_PAWN_EG[sq];
        PST[WKNIGHT][sq].mg = PST_KNIGHT_MG[sq];
        PST[WKNIGHT][sq].eg = PST_KNIGHT_EG[sq];
        PST[WBISHOP][sq].mg = PST_BISHOP_MG[sq];
        PST[WBISHOP][sq].eg = PST_BISHOP_EG[sq];
        PST[WROOK][sq].mg = PST_ROOK_MG[sq];
        PST[WROOK][sq].eg = PST_ROOK_EG[sq];
        PST[WQUEEN][sq].mg = PST_QUEEN_MG[sq];
        PST[WQUEEN][sq].eg = PST_QUEEN_EG[sq];
        PST[WKING][sq].mg = PST_KING_MG[sq];
        PST[WKING][sq].eg = PST_KING_EG[sq];
    }
}

/*******************************************************************************
 * Material Evaluation Values 
 *******************************************************************************/


const score_t TEMPO[2] = {S(-10, 0), S(10, 0)};

const score_t IMBALANCE[9][9] = {//index: major piece units, minor pieces

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

const score_t SVPAWN = S(VPAWN, VPAWN); //middle and endgame values
const score_t SVKNIGHT = S(VKNIGHT, VKNIGHT);
const score_t SVBISHOP = S(VBISHOP, VBISHOP);
const score_t SVROOK = S(VROOK, VROOK + 50);
const score_t SVQUEEN = S(VQUEEN, VQUEEN + 100);
const score_t SVKING = S(VKING, VKING);

const short VMATING_POWER = 20;
const short VMATING_MATERIAL = 50;

const short REDUNDANT_ROOK = -10;
const short REDUNDANT_KNIGHT = -8;
const short REDUNDANT_QUEEN = -20;

enum mflag_t {
    MFLAG_EG = 128,
    MFLAG_MATING_POWER_W = 64,
    MFLAG_MATING_POWER_B = 32,
    MFLAG_KING_ATTACK_FORCE_W = 16,
    MFLAG_KING_ATTACK_FORCE_B = 8,
    MFLAG_IMBALANCE = 7
};

enum mflag_imbalance_t {
    IMB_NONE = 0,
    IMB_MINOR_W = 1,
    IMB_MAJOR_W = 2,
    IMB_MINOR_B = 5,
    IMB_MAJOR_B = 6
};

/* 
 * 0  | 1   | 2   | 3   | 4   | 5 ..   7  | 
 * EG | AFW | AFB | MPW | MPB | IMBALANCE | 
 * 
 * Imbalance: 000 - 0) no imbalance
 *            001 - 1) minor imbalance favoring white
 *            010 - 2) major imbalance favoring white
 *            011 - 3) (reserved for white)
 *            100 - 4) (reserved for black)
 *            101 - 5) minor imbalance favoring black
 *            110 - 6) major imbalance favoring black
 *            111 - 7) (reserved for black) 
 */

bool has_mating_power(search_t * s, bool us) {
    const int flags = s->stack->material_flags;
    return us == WHITE ? (flags & MFLAG_MATING_POWER_W) != 0
            : (flags & MFLAG_MATING_POWER_B) != 0;
}

bool has_imbalance(search_t * s, bool us) {
    const int flags = s->stack->material_flags;
    return (flags & MFLAG_IMBALANCE) != 0 && bool(flags & 4) == !us;
}

bool has_major_imbalance(search_t * s) {
    const int flags = s->stack->material_flags;
    return (flags & 2) != 0;
}

const short TRADEDOWN_PAWNS_MUL[9] = {
    210, 226, 238, 248, 256, 256, 256, 256, 256
};

const short ATTACKED_PIECE = -32; //piece attacked by a pawn

/*******************************************************************************
 * Pawn Values 
 *******************************************************************************/

uint8_t PFLAG_CLOSED_CENTER = 1;

const score_t DEFENDED_PAWN[2] = {S(0, 0), S(4, 0)}; //closed, open file
const score_t ISOLATED_PAWN[2] = {S(-10, -20), S(-20, -20)}; //closed, open file
const score_t WEAK_PAWN[2] = {S(-12, -8), S(-16, -10)}; //closed, open file
const score_t DOUBLED_PAWN = S(-10, -20);

const score_t PASSED_PAWN[64] = {
    S(0, 0), S(0, 0), S(0, 0), S(0, 0), S(0, 0), S(0, 0), S(0, 0), S(0, 0),
    S(150, 170), S(140, 160), S(130, 150), S(120, 140), S(120, 140), S(130, 150), S(140, 160), S(150, 170),
    S(65, 100), S(50, 80), S(50, 80), S(40, 60), S(40, 60), S(50, 80), S(50, 80), S(65, 100),
    S(40, 60), S(30, 40), S(30, 40), S(20, 30), S(20, 30), S(30, 40), S(30, 40), S(40, 60),
    S(20, 30), S(15, 20), S(15, 20), S(10, 15), S(10, 15), S(15, 20), S(15, 20), S(20, 30),
    S(10, 20), S(8, 10), S(8, 10), S(8, 10), S(8, 10), S(8, 10), S(8, 10), S(10, 20),
    S(10, 20), S(8, 10), S(8, 10), S(8, 10), S(8, 10), S(8, 10), S(8, 10), S(10, 20),
    S(0, 0), S(0, 0), S(0, 0), S(0, 0), S(0, 0), S(0, 0), S(0, 0), S(0, 0),
};

const score_t CANDIDATE[64] = {
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

const score_t BLOCKED_CENTER_PAWN = S(-10, -4);

#define KA_ENCODE(p,s) (MIN(p,3)|(MIN(s,15)<<2))
#define KA_UNITS(k) ((k) & 3)
#define KA_SQUARES(k) ((k) >> 2)

/*******************************************************************************
 * Knight Values 
 *******************************************************************************/

const score_t KNIGHT_MOBILITY[9] = {
    S(-22, -22), S(-18, -18), S(-14, -14), S(-10, -10),
    S(-8, -8), S(-6, -6), S(-4, -4), S(-2, -2), S(0, 0)
};

const score_t KNIGHT_PAWN_WIDTH[8] = {//indexed by opponent pawn width
    S(0, 0), S(0, 0), S(0, 0), S(0, 0), S(0, -5), S(0, -10), S(0, -15), S(0, -20)
};

const score_t KNIGHT_PAWN_COUNT[9] = {//indexed by opponent pawn count
    S(0, 0), S(0, 0), S(0, 0), S(0, 0), S(0, 0), S(0, 0), S(4, 4), S(8, 8), S(12, 12)
};

const score_t KNIGHT_OUTPOST[64] = {
    S(0, 0), S(0, 0), S(0, 0), S(0, 0), S(0, 0), S(0, 0), S(0, 0), S(0, 0),
    S(8, 2), S(8, 2), S(8, 2), S(8, 2), S(8, 2), S(8, 2), S(8, 2), S(8, 2),
    S(8, 2), S(8, 2), S(12, 2), S(16, 2), S(16, 2), S(12, 2), S(8, 2), S(8, 2),
    S(8, 2), S(8, 2), S(8, 2), S(8, 2), S(8, 2), S(8, 2), S(8, 2), S(8, 2),
    S(8, 2), S(8, 2), S(8, 2), S(8, 2), S(8, 2), S(8, 2), S(8, 2), S(8, 2),
    S(8, 2), S(8, 2), S(8, 2), S(8, 2), S(8, 2), S(8, 2), S(8, 2), S(8, 2),
    S(0, 0), S(0, 0), S(0, 0), S(0, 0), S(0, 0), S(0, 0), S(0, 0), S(0, 0),
    S(0, 0), S(0, 0), S(0, 0), S(0, 0), S(0, 0), S(0, 0), S(0, 0), S(0, 0)
};

const score_t KNIGHT_ATTACK = S(10, 10);

/*******************************************************************************
 * Bishop Values 
 *******************************************************************************/

const int8_t VBISHOPPAIR = 40;

const score_t BISHOP_MOBILITY[14] = {
    S(-30, -30), S(-20, -20), S(-12, -12), S(-6, -6),
    S(-2, -2), S(0, 0), S(4, 4), S(8, 8), S(12, 12),
    S(14, 14), S(16, 16), S(18, 18), S(20, 20), S(22, 22)
};

const score_t TRAPPED_BISHOP = S(-60, -80);

const score_t BISHOP_ATTACK = S(10, 10);

U64 BISHOP_PATTERNS[2] = {//black, white
    BIT(d6) | BIT(d7) | BIT(e6) | BIT(d7) | BIT(a2) | BIT(h2),
    BIT(d3) | BIT(d2) | BIT(e3) | BIT(d2) | BIT(a7) | BIT(h7),
};

const score_t BISHOP_OUTPOST[64] = {
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

const score_t ROOK_7TH = S(10, 10);
const score_t ROOK_1ST = S(10, 0); //back rank protection
const score_t ROOK_SEMIOPEN_FILE = S(10, 10);
const score_t ROOK_OPEN_FILE = S(17, 17);
const score_t ROOK_GOOD_SIDE = S(8, 16); //Rule of Tarrasch 
const score_t ROOK_WRONG_SIDE = S(-8, -16);
const score_t ROOK_CLOSED_FILE = S(-5, -5);
const short ROOK_ATTACK = 12;

U64 ROOK_PATTERNS[2] = {//black, white
    BIT(h8) | BIT(g8) | BIT(h7) | BIT(g7) | BIT(a8) | BIT(b8) | BIT(a7) | BIT(b7),
    BIT(h1) | BIT(g1) | BIT(h2) | BIT(g2) | BIT(a1) | BIT(b1) | BIT(a2) | BIT(b2)
};

const score_t TRAPPED_ROOK = S(-40, -80);

/*******************************************************************************
 * Queen Values
 *******************************************************************************/

const score_t QUEEN_MOBILITY[29] = {
    S(-10, -20), S(-4, -10), S(-2, -4), S(0, -2), S(1, 0), S(2, 2), S(3, 4), S(4, 6),
    S(5, 8), S(6, 10), S(6, 12), S(7, 14), S(7, 16), S(8, 17), S(8, 18), S(9, 19),
    S(9, 20), S(10, 21), S(10, 21), S(11, 22), S(11, 22), S(11, 22), S(12, 23), S(12, 23),
    S(13, 23), S(13, 24), S(13, 24), S(14, 25), S(14, 25)
};

/*******************************************************************************
 * Main evaluation function
 *******************************************************************************/

int evaluate(search_t * sd) {

    if (sd->stack->in_check) {
        sd->stack->eval_result = score::INVALID;
        return score::INVALID;
    }

    if (sd->stack->eval_result != score::INVALID) {
        return sd->stack->eval_result;
    }

    sd->stack->equal_pawns = sd->brd.ply > 0
            && sd->brd.stack->pawn_hash == (sd->brd.stack - 1)->pawn_hash
            && (sd->stack - 1)->eval_result != score::INVALID;

    bool wtm = sd->brd.stack->wtm;
    int result = eval_material(sd); //sets stack->phase (required) 
    score_t * score = &sd->stack->eval_score;
    score->set(eval_pawns_and_kings(sd)); //initializes mobility and attack masks (required)
    score->add(eval_knights(sd, WHITE));
    score->sub(eval_knights(sd, BLACK));
    score->add(eval_bishops(sd, WHITE));
    score->sub(eval_bishops(sd, BLACK));
    score->add(eval_rooks(sd, WHITE));
    score->sub(eval_rooks(sd, BLACK));
    score->add(eval_queens(sd, WHITE));
    score->sub(eval_queens(sd, BLACK));
    score->add(eval_passed_pawns(sd, WHITE));
    score->sub(eval_passed_pawns(sd, BLACK));
    score->add(eval_king_attack(sd, WHITE)); //must be after piece evals
    score->sub(eval_king_attack(sd, BLACK)); //must be after piece evals
    score->add(TEMPO[wtm]);
    result += score->get(sd->stack->phase);
    sd->stack->eg_score = result;
    if (sd->stack->material_flags & MFLAG_EG) {
        result = eg::eval(sd, result);
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
int eval_material(search_t * sd) {

    /*
     * 1. Get the score from the last stack record if the previous move was quiet, 
     *    so the material balance did not change. This is easy to verify with 
     *    the material hash
     */
    board_t * pos = &sd->brd;
    if (pos->ply > 0 &&
            (pos->stack - 1)->material_hash == pos->stack->material_hash
            && (sd->stack - 1)->eval_result != score::INVALID) {
        sd->stack->material_score = (sd->stack - 1)->material_score;
        sd->stack->phase = (sd->stack - 1)->phase;
        sd->stack->material_flags = (sd->stack - 1)->material_flags;
        return sd->stack->material_score;
    }

    /*
     * 2. Probe the material table
     */
    int value, phase, flags;
    if (material_table::retrieve(pos->stack->material_hash, value, phase, flags)) {
        sd->stack->material_score = value;
        sd->stack->phase = phase;
        sd->stack->material_flags = flags;
        return value;
    }

    /*
     * 3. Calculate material value and store in material hash table
     */
    score_t result;
    result.clear();

    int wpawns = pos->count(WPAWN);
    int bpawns = pos->count(BPAWN);
    int wknights = pos->count(WKNIGHT);
    int bknights = pos->count(BKNIGHT);
    int wbishops = pos->count(WBISHOP);
    int bbishops = pos->count(BBISHOP);
    int wrooks = pos->count(WROOK);
    int brooks = pos->count(BROOK);
    int wqueens = pos->count(WQUEEN);
    int bqueens = pos->count(BQUEEN);
    int wminors = wknights + wbishops;
    int bminors = bknights + bbishops;
    int wpieces = wminors + wrooks + wqueens;
    int bpieces = bminors + brooks + bqueens;

    /*
     * Game phase
     */
    phase = score::MAX_PHASE /* 16 */
            - wminors - bminors /* max: 8 */
            - wrooks - brooks /* max:4 */
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


    /*
     * Material Balance
     */
    flags = 0;
    bool balance = (wminors == bminors) && (wrooks + 2 * wqueens) == (brooks + 2 * bqueens);
    if (!balance) { //material imbalance
        int power = result.get(phase);
        if (power > 450) {
            flags = IMB_MAJOR_W;
        } else if (power > 100) {
            flags = IMB_MINOR_W;
        } else if (power < -450) {
            flags = IMB_MAJOR_B;
        } else if (power < -100) {
            flags = IMB_MINOR_B;
        }
        int minors_ix = MAX(0, 4 + wminors - bminors);
        int majors_ix = MAX(0, 4 + wrooks + 2 * wqueens - brooks - 2 * bqueens);
        result.add(IMBALANCE[MIN(majors_ix, 8)][MIN(minors_ix, 8)]);
    }

    if (wpawns != bpawns) {
        result.mg += (wpawns - bpawns) * SVPAWN.mg;
        result.eg += (wpawns - bpawns) * SVPAWN.eg;
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
    value = result.get(phase);
    sd->stack->material_score = value;
    sd->stack->material_flags = flags;
    sd->stack->phase = phase;
    material_table::store(pos->stack->material_hash, value, phase, flags);
    return value;
}

/**
 * Evaluate pawn structure score 
 * @param sd search metadata object
 */

score_t * eval_pawns_and_kings(search_t * sd) {

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
    board_t * pos = &sd->brd;
    int wkpos = pos->get_sq(WKING);
    int bkpos = pos->get_sq(BKING);
    sd->stack->mob[WHITE] = ~(pos->bb[WPAWN] | pos->pawn_attacks(BLACK) | pos->bb[WKING]);
    sd->stack->mob[BLACK] = ~(pos->bb[BPAWN] | pos->pawn_attacks(WHITE) | pos->bb[BKING]);
    sd->stack->attack[WHITE] = pos->bb[BPAWN] | pos->bb[BKING];
    sd->stack->attack[BLACK] = pos->bb[WPAWN] | pos->bb[WKING];
    sd->stack->king_attack_zone[WHITE] = magic::queen_moves(bkpos, pos->pawns_kings()) & sd->stack->mob[WHITE];
    sd->stack->king_attack_zone[BLACK] = magic::queen_moves(wkpos, pos->pawns_kings()) & sd->stack->mob[BLACK];

    /*
     * 2. Probe the hash table for the pawn score
     */
    U64 passers;
    int king_attack[2];
    int flags;
    score_t pawn_score;

    if (pawn_table::retrieve(pos->stack->pawn_hash, passers, pawn_score, king_attack, flags)) {
        sd->stack->passers = passers;
        sd->stack->king_attack[BPAWN] = king_attack[BLACK];
        sd->stack->king_attack[WPAWN] = king_attack[WHITE];
        sd->stack->pawn_flags = flags;
        sd->stack->pawn_score.set(pawn_score);
        return &sd->stack->pawn_score;
    }


    /*
     * 3. Calculate pawn evaluation score
     */

    king_attack[BLACK] = 0;
    king_attack[WHITE] = 0;
    flags = 0;
    passers = 0;
    pawn_score.clear();

    const U64 open_files[2] = {
        ~FILEFILL(pos->bb[BPAWN]),
        ~FILEFILL(pos->bb[WPAWN])
    };
    const U64 up_all[2] = {
        DOWN1(pos->bb[BPAWN]),
        UP1(pos->bb[WPAWN])
    };
    const U64 up_left[2] = {
        DOWNLEFT1(pos->bb[BPAWN]),
        UPLEFT1(pos->bb[WPAWN])
    };
    const U64 up_right[2] = {
        DOWNRIGHT1(pos->bb[BPAWN]),
        UPRIGHT1(pos->bb[WPAWN])
    };
    const U64 attacks[2] = {
        up_left[BLACK] | up_right[BLACK],
        up_right[WHITE] | up_left[WHITE]
    };
    const U64 blocked_pawns[2] = {
        UP1(up_all[BLACK] & pos->bb[WPAWN]) & ~attacks[WHITE],
        DOWN1(up_all[WHITE] & pos->bb[BPAWN]) & ~attacks[BLACK]
    };
    const U64 safe[2] = {
        (up_left[BLACK] & up_right[BLACK]) | ~attacks[WHITE] | ((up_left[BLACK] ^ up_right[BLACK]) & ~(up_left[WHITE] & up_right[WHITE])),
        (up_left[WHITE] & up_right[WHITE]) | ~attacks[BLACK] | ((up_left[WHITE] ^ up_right[WHITE]) & ~(up_left[BLACK] & up_right[BLACK]))
    };
    const U64 isolated_pawns[2] = {
        pos->bb[BPAWN] & ~(FILEFILL(attacks[BLACK])),
        pos->bb[WPAWN] & ~(FILEFILL(attacks[WHITE]))
    };
    const U64 doubled_pawns[2] = {
        UP1(fill_north(pos->bb[BPAWN])) & pos->bb[BPAWN],
        DOWN1(fill_south(pos->bb[WPAWN])) & pos->bb[WPAWN]
    };

    U64 moves[2] = {
        DOWN1(pos->bb[BPAWN] & ~blocked_pawns[BLACK]) & safe[BLACK],
        UP1(pos->bb[WPAWN] & ~blocked_pawns[WHITE]) & safe[WHITE]
    };
    moves[WHITE] |= UP1(moves[WHITE] & RANK_3 & ~attacks[BLACK] & ~DOWN1((pos->bb[BPAWN] | pos->bb[WPAWN]))) & safe[WHITE];
    moves[BLACK] |= DOWN1(moves[BLACK] & RANK_6 & ~attacks[WHITE] & ~UP1((pos->bb[WPAWN] | pos->bb[BPAWN]))) & safe[BLACK];

    const U64 attack_range[2] = {
        attacks[BLACK] | DOWNLEFT1(moves[BLACK]) | DOWNRIGHT1(moves[BLACK]),
        attacks[WHITE] | UPLEFT1(moves[WHITE]) | UPRIGHT1(moves[WHITE])
    };

    const U64 kcz[2] = {KING_ZONE[bkpos], KING_ZONE[wkpos]};
    int blocked_center_pawns = 0;

    U64 wpawns = pos->bb[WPAWN];
    while (wpawns) {
        int sq = pop(wpawns);
        int isq = FLIP_SQUARE(sq);
        U64 sq_bit = BIT(sq);
        U64 up = fill_north(sq_bit);
        bool open = sq_bit & open_files[BLACK];
        bool doubled = sq_bit & doubled_pawns[WHITE];
        bool isolated = sq_bit & isolated_pawns[WHITE];
        bool defended = sq_bit & attacks[WHITE];
        bool blocked = sq_bit & blocked_pawns[WHITE];
        bool push_defend = !blocked && (UP1(sq_bit) & attack_range[WHITE]);
        bool push_double_defend = !blocked && (UP2(sq_bit & RANK_2) & moves[WHITE] & attack_range[WHITE]);
        bool lost = sq_bit & ~attack_range[WHITE];
        bool weak = !defended && lost && !push_defend && !push_double_defend;
        bool passed = !doubled && !(up & (attacks[BLACK] | pos->bb[BPAWN]));
        bool candidate = open && !doubled && !passed && !(up & ~safe[WHITE]);

        pawn_score.add(PST[WPAWN][isq]);
        
        if (isolated) {
            pawn_score.add(ISOLATED_PAWN[open]);
        } else if (weak) {
            pawn_score.add(WEAK_PAWN[open]);
        } else if (defended) {
            pawn_score.add(DEFENDED_PAWN[open]);
        }
        if (doubled) {
            pawn_score.add(DOUBLED_PAWN);
        }
        if (candidate) {
            pawn_score.add(CANDIDATE[isq]);
        } else if (passed) {
            passers |= sq_bit;
        }
        if (blocked && (sq_bit & CENTER)) {
            blocked_center_pawns++;
        }
        king_attack[WHITE] += popcnt0(WPAWN_CAPTURES[sq] & kcz[BLACK]);
    }

    U64 bpawns = pos->bb[BPAWN];
    while (bpawns) {
        int sq = pop(bpawns);
        U64 sq_bit = BIT(sq);
        U64 down = fill_south(sq_bit);
        bool open = sq_bit & open_files[WHITE];
        bool doubled = sq_bit & doubled_pawns[BLACK];
        bool isolated = sq_bit & isolated_pawns[BLACK];
        bool blocked = sq_bit & blocked_pawns[BLACK];
        bool defended = sq_bit & attacks[BLACK];
        bool push_defend = !blocked && (DOWN1(sq_bit) & attack_range[BLACK]);
        bool push_double_defend = !blocked && (DOWN2(sq_bit & RANK_7) & moves[BLACK] & attack_range[BLACK]);
        bool lost = sq_bit & ~attack_range[BLACK];
        bool weak = !defended && lost && !push_defend && !push_double_defend;
        bool passed = !doubled && !(down & (attacks[WHITE] | pos->bb[WPAWN]));
        bool candidate = open && !doubled && !passed && !(down & ~safe[BLACK]);

        pawn_score.sub(PST[WPAWN][sq]);
        
        if (isolated) {
            pawn_score.sub(ISOLATED_PAWN[open]);
        } else if (weak) {
            pawn_score.sub(WEAK_PAWN[open]);
        } else if (defended) {
            pawn_score.sub(DEFENDED_PAWN[open]);
        }
        if (doubled) {
            pawn_score.sub(DOUBLED_PAWN);
        }
        if (candidate) {
            pawn_score.sub(CANDIDATE[sq]);
        } else if (passed) {
            passers |= sq_bit;
        }

        if (blocked && (sq_bit & CENTER)) {
            blocked_center_pawns++;
        }

        king_attack[BLACK] += popcnt0(BPAWN_CAPTURES[sq] & kcz[WHITE]);
    }

    if (blocked_center_pawns > 2) {
        flags |= PFLAG_CLOSED_CENTER;
    }

    /*
     * White King
     */
    
    //position
    pawn_score.add(PST[WKING][ISQ(wkpos, WHITE)]);
    
    //threats
    U64 ka_w = KING_MOVES[wkpos] & sd->stack->mob[WHITE] & sd->stack->attack[WHITE];
    if (ka_w) {
        pawn_score.add(0, 10);
    }
    
    //attack units - shelter
    king_attack[BLACK] += SHELTER_KPOS[FLIP_SQUARE(wkpos)];
   
    //attack units - castling right
    if (pos->stack->castling_flags & CASTLE_K
            && ((pos->matrix[h2] == WPAWN && pos->matrix[g2] == WPAWN)
            || (pos->matrix[f2] == WPAWN && pos->matrix[h2] == WPAWN && pos->matrix[g3] == WPAWN)
            || (pos->matrix[h3] == WPAWN && pos->matrix[g2] == WPAWN && pos->matrix[f2] == WPAWN))) {
        king_attack[BLACK] += SHELTER_CASTLING_KINGSIDE;
    } else if (pos->stack->castling_flags & CASTLE_Q
            && ((pos->matrix[a2] == WPAWN && pos->matrix[b2] == WPAWN && pos->matrix[c2] == WPAWN)
            || (pos->matrix[a2] == WPAWN && pos->matrix[b3] == WPAWN && pos->matrix[c2] == WPAWN))) {
        king_attack[BLACK] += SHELTER_CASTLING_QUEENSIDE;
    }
    
    //attack units - shelter pawns
    U64 king_front = (FORWARD_RANKS[RANK(wkpos)] | KING_MOVES[wkpos]) & PAWN_SCOPE[FILE(wkpos)];
    U64 shelter_pawns = king_front & pos->bb[WPAWN];
    while (shelter_pawns) {
        int sq = pop(shelter_pawns);
        king_attack[BLACK] += SHELTER_PAWN[FLIP_SQUARE(sq)];
    }
    
    //attack units - storm pawns
    U64 storm_pawns = king_front & pos->bb[BPAWN];
    while (storm_pawns) {
        int sq = pop(storm_pawns);
        king_attack[BLACK] += STORM_PAWN[FLIP_SQUARE(sq)];
    }
    

    //attack units - open files
    U64 open = (open_files[WHITE] | open_files[BLACK]) & king_front & RANK_8;
    if (open) {
        king_attack[BLACK] += SHELTER_OPEN_FILES[popcnt0(open)];
        if (open & (FILE_A | FILE_H)) {
            king_attack[BLACK] += SHELTER_OPEN_EDGE_FILE;
        }
    }
     
     /*
     * Black King
     */
    
    //position
    pawn_score.sub(PST[WKING][ISQ(bkpos, BLACK)]);
    
    //threats
    U64 ka_b = KING_MOVES[bkpos] & sd->stack->mob[BLACK] & sd->stack->attack[BLACK];
    if (ka_b) {
        pawn_score.sub(0, 10);
    }

    //attack units - shelter
    king_attack[WHITE] += SHELTER_KPOS[bkpos];
    
    //attack units - castling right
    if (pos->stack->castling_flags & CASTLE_k
            && ((pos->matrix[h7] == BPAWN && pos->matrix[g7] == BPAWN)
            || (pos->matrix[f7] == BPAWN && pos->matrix[h7] == BPAWN && pos->matrix[g6] == BPAWN)
            || (pos->matrix[h6] == BPAWN && pos->matrix[g7] == BPAWN && pos->matrix[f7] == BPAWN))) {
        king_attack[WHITE] += SHELTER_CASTLING_KINGSIDE;
    } else if (pos->stack->castling_flags & CASTLE_q
            && ((pos->matrix[a7] == BPAWN && pos->matrix[b7] == BPAWN && pos->matrix[c7] == BPAWN)
            || (pos->matrix[a7] == BPAWN && pos->matrix[b6] == BPAWN && pos->matrix[c7] == BPAWN))) {
        king_attack[WHITE] += SHELTER_CASTLING_QUEENSIDE;
    }
    
    //attack units - shelter pawns
    king_front = (BACKWARD_RANKS[RANK(bkpos)] | KING_MOVES[bkpos]) & PAWN_SCOPE[FILE(bkpos)];
    shelter_pawns = king_front & pos->bb[BPAWN];
    while (shelter_pawns) {
        int sq = pop(shelter_pawns);
        king_attack[WHITE] += SHELTER_PAWN[sq];
    }
    
    //attack units - storm pawns
    storm_pawns = king_front & pos->bb[WPAWN];
    while (storm_pawns) {
        int sq = pop(storm_pawns);
        king_attack[WHITE] += STORM_PAWN[sq];
    }
    
    //attack units - open files
    open = (open_files[WHITE] | open_files[BLACK]) & king_front & RANK_1;
    if (open) {
        king_attack[WHITE] += SHELTER_OPEN_FILES[popcnt0(open)];
        if (open & (FILE_A | FILE_H)) {
            king_attack[WHITE] += SHELTER_OPEN_EDGE_FILE;
        }
    }
    
    /*
     * Persist info on the stack and return the total score
     */

    sd->stack->passers = passers;
    sd->stack->king_attack[WPAWN] = king_attack[WHITE];
    sd->stack->king_attack[BPAWN] = king_attack[BLACK];
    sd->stack->pawn_flags = flags;
    sd->stack->pawn_score.set(pawn_score);

    pawn_table::store(pos->stack->pawn_hash, passers, pawn_score, king_attack, flags);
    return &sd->stack->pawn_score;
}

score_t * eval_knights(search_t * sd, bool us) {

    score_t * result = &sd->stack->knight_score[us];
    board_t * pos = &sd->brd;
    int pc = KNIGHT[us];
    result->clear();
    sd->stack->king_attack[pc] = 0;
    U64 knights = pos->bb[pc];

    if (knights == 0) {
        return result;
    }

    /*
     * 2. Get the score from the last stack record if the previous move 
     *   a) did not change the pawn+kings structure (pawn hash) 
     *   b) did not move or capture any knight
     */
    if (sd->stack->equal_pawns) {
        move_t * prevMove = &(sd->stack - 1)->current_move;
        if (prevMove->piece != pc && prevMove->capture != pc) {
            result->set((sd->stack - 1)->knight_score[us]);
            sd->stack->king_attack[pc] = (sd->stack - 1)->king_attack[pc];
            return result;
        }
    }

    /*
     * 3. Calculate the score and store on the stack
     */


    bool them = !us;
    int pawn_width = bb_width(pos->bb[PAWN[them]]);
    int pawn_count = pos->count(PAWN[them]);
    int kpos = pos->get_sq(KING[them]);
    U64 kaz = KNIGHT_MOVES[kpos] | KING_ZONE[kpos]; //king attack zone
    int ka_units = 0;
    int ka_squares = 0;
    while (knights) {
        int sq = pop(knights);
        result->add(PST[WKNIGHT][ISQ(sq, us)]);
        U64 moves = KNIGHT_MOVES[sq] & sd->stack->mob[us];
        int mob_count = popcnt0(moves);
        result->add(KNIGHT_MOBILITY[mob_count]);
        result->add(KNIGHT_PAWN_WIDTH[pawn_width]);
        result->add(KNIGHT_PAWN_COUNT[pawn_count]);

        if (moves & sd->stack->attack[us]) {
            result->add(KNIGHT_ATTACK);
        }

        if (pos->is_attacked_by_pawn(sq, us)) {
            result->add(KNIGHT_OUTPOST[ISQ(sq, us)]);
        }
        if (pos->is_attacked_by_pawn(sq, them)) {
            result->add(ATTACKED_PIECE);
        }
        if (moves & kaz) {
            ka_units++;
            ka_squares += popcnt(moves & kaz);
        }
    }
    sd->stack->king_attack[pc] = KA_ENCODE(ka_units, ka_squares);
    return result;
}

score_t * eval_bishops(search_t * sd, bool us) {
    score_t * result = &sd->stack->bishop_score[us];
    board_t * pos = &sd->brd;
    int pc = BISHOP[us];
    result->clear();
    sd->stack->king_attack[pc] = 0;
    U64 bishops = pos->bb[pc];

    if (bishops == 0) {
        return result;
    }

    /*
     * 2. Get the score from the last stack record if the previous move 
     *   a) did not change the pawn+kings structure (pawn hash) 
     *   b) did not move or capture any bishop
     */
    if (sd->stack->equal_pawns) {
        move_t * prevMove = &(sd->stack - 1)->current_move;
        if (prevMove->piece != pc && prevMove->capture != pc) {
            result->set((sd->stack - 1)->bishop_score[us]);
            sd->stack->king_attack[pc] = (sd->stack - 1)->king_attack[pc];
            return result;
        }
    }

    /*
     * 3. Calculate the score and store on the stack
     */
    //bishop pair
    if (pos->has_bishop_pair(us) && (sd->stack->pawn_flags & PFLAG_CLOSED_CENTER) == 0) {
        result->add(VBISHOPPAIR);
    }

    U64 occ = pos->pawns_kings();
    bool them = !us;
    int kpos = pos->get_sq(KING[them]);
    U64 kaz = (sd->stack->king_attack_zone[us] & BISHOP_MOVES[kpos]) | KING_ZONE[kpos]; //king attack zone; 
    int ka_units = 0;
    int ka_squares = 0;
    while (bishops) {
        int sq = pop(bishops);
        result->add(PST[WBISHOP][ISQ(sq, us)]);
        U64 moves = magic::bishop_moves(sq, occ) & sd->stack->mob[us];
        int count = popcnt0(moves);
        result->add(BISHOP_MOBILITY[count]);
        if (moves & sd->stack->attack[us]) {
            result->add(BISHOP_ATTACK);
        } else {
            result->sub(BISHOP_ATTACK);
        }
        if (pos->is_attacked_by_pawn(sq, us)) {
            result->add(BISHOP_OUTPOST[ISQ(sq, us)]);
        }
        if (pos->is_attacked_by_pawn(sq, them)) {
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
            ka_squares += popcnt(moves & kaz);
        }
    }
    sd->stack->king_attack[pc] = KA_ENCODE(ka_units, ka_squares);
    return result;
}

score_t * eval_rooks(search_t * sd, bool us) {

    static const U64 BACKRANKS[2] = {RANK_1 | RANK_2, RANK_7 | RANK_8};

    score_t * result = &sd->stack->rook_score[us];
    board_t * pos = &sd->brd;
    int pc = ROOK[us];
    result->clear();
    sd->stack->king_attack[pc] = 0;
    U64 rooks = pos->bb[pc];

    if (rooks == 0) {
        return result;
    }

    /*
     * 2. Get the score from the last stack record if the previous move 
     *   a) did not change the pawn+kings structure (pawn hash, includes promotions to rook) 
     *   b) did not move or capture any rook
     */
    if (sd->stack->equal_pawns) {
        move_t * prevMove = &(sd->stack - 1)->current_move;
        if (prevMove->piece != pc && prevMove->capture != pc) {
            result->set((sd->stack - 1)->rook_score[us]);
            sd->stack->king_attack[pc] = (sd->stack - 1)->king_attack[pc];
            return result;
        }
    }

    /*
     * 3. Calculate the score and store on the stack
     */
    bool them = !us;
    U64 fill[2] = {FILEFILL(pos->bb[BPAWN]), FILEFILL(pos->bb[WPAWN])};
    U64 occ = pos->pawns_kings();
    if ((pos->bb[ROOK[us]] & RANK[us][1]) && (BIT(pos->get_sq(KING[us])) & (RANK[us][1] | RANK[us][2]))) {
        result->add(ROOK_1ST); //at least one rook is protecting the back rank
    }
    int kpos = pos->get_sq(KING[them]);
    U64 kaz = (sd->stack->king_attack_zone[us] & ROOK_MOVES[kpos]) | KING_ZONE[kpos]; //king attack zone
    int ka_units = 0;
    int ka_squares = 0;

    while (rooks) {
        int sq = pop(rooks);
        result->add(PST[WROOK][ISQ(sq, us)]);
        U64 bitSq = BIT(sq);
        if (bitSq & fill[us]) {
            result->add(ROOK_CLOSED_FILE);
            //trapped rook pattern
            if (bitSq & ROOK_PATTERNS[us]) {
                int kpos_us = pos->get_sq(KING[us]);
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

        if (pos->is_attacked_by_pawn(sq, them)) {
            result->add(ATTACKED_PIECE);
        }

        U64 moves = magic::rook_moves(sq, occ) & sd->stack->mob[us];
        if (moves & sd->stack->attack[us]) {
            result->add(popcnt(moves & sd->stack->attack[us]) * ROOK_ATTACK);
        }

        if ((bitSq & RANK[us][7]) && (BIT(pos->get_sq(KING[them])) & BACKRANKS[us])) {
            result->add(ROOK_7TH);
        }

        //Tarrasch Rule: place rook behind passers
        U64 tpass = moves & sd->stack->passers; //touched passers
        if (tpass) {
            U64 front[2];
            front[BLACK] = fill_south(bitSq) & tpass;
            front[WHITE] = fill_north(bitSq) & tpass;
            if (front[us] & pos->bb[PAWN[us]] & SIDE[them]) { //supporting a passer from behind
                result->add(ROOK_GOOD_SIDE);
            } else if (front[them] & pos->bb[PAWN[them]] & SIDE[us]) { //attacking a passer from behind
                result->add(ROOK_GOOD_SIDE);
            } else if (front[them] & pos->bb[PAWN[us]] & SIDE[them]) { //supporting from the wrong side
                result->add(ROOK_WRONG_SIDE);
            } else if (front[us] & pos->bb[PAWN[them]] & SIDE[us]) { //attacking from the wrong side
                result->add(ROOK_WRONG_SIDE);
            }
        }
        if (moves & kaz) {
            ka_units++;
            ka_squares += popcnt(moves & kaz);
        }
    }
    sd->stack->king_attack[pc] = KA_ENCODE(ka_units, ka_squares);
    return result;
}

score_t * eval_queens(search_t * sd, bool us) {
    score_t * result = &sd->stack->queen_score[us];
    board_t * pos = &sd->brd;
    int pc = QUEEN[us];
    result->clear();
    sd->stack->king_attack[pc] = 0;
    U64 queens = pos->bb[pc];

    if (queens == 0) {
        return result;
    }

    /*
     * 2. Get the score from the last stack record if the previous move 
     *   a) did not change the pawn+kings structure (pawn hash) 
     *   b) did not move or capture any bishop
     */
    if (sd->stack->equal_pawns) {
        move_t * prevMove = &(sd->stack - 1)->current_move;
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
    U64 occ = pos->pawns_kings();
    int kpos = pos->get_sq(KING[them]);
    U64 kaz = sd->stack->king_attack_zone[us] | KING_ZONE[kpos]; //king attack zone
    int ka_units = 0;
    int ka_squares = 0;
    while (queens) {
        int sq = pop(queens);
        result->add(PST[WQUEEN][ISQ(sq, us)]);
        U64 moves = magic::queen_moves(sq, occ) & sd->stack->mob[us];
        int count = popcnt0(moves);
        result->add(QUEEN_MOBILITY[count]);
        if (pos->is_attacked_by_pawn(sq, them)) {
            result->add(ATTACKED_PIECE);
        }
        result->add(10 - distance_rank(sq, kpos) - distance_file(sq, kpos));
        if (moves & kaz) {
            ka_units++;
            ka_squares += popcnt(moves & kaz);
        }
    }
    sd->stack->king_attack[pc] = KA_ENCODE(ka_units, ka_squares);
    return result;
}

score_t * eval_passed_pawns(search_t * sd, bool us) {
    score_t * result = &sd->stack->passer_score[us];
    result->clear();
    U64 passers = sd->stack->passers & sd->brd.bb[PAWN[us]];
    if (passers == 0) {
        return result;
    }
    bool them = !us;
    U64 exclude = SIDE[us] & ~(RANK_4 | RANK_5);
    int step = PAWNDIRECTION[us];
    score_t bonus;
    while (passers) {
        int sq = pop(passers);
        
        //set base score
        bonus.set(PASSED_PAWN[ISQ(sq, us)]);
        result->add(bonus);        
        
        //stop if the pawn is on rank 2, 3, or 4
        if (BIT(sq) & exclude) {
            continue;
        }
        
        //initialize bonus and rank
        bonus.half();
        int to = sq + step;
        int r = RANK(sq) - 1;
        if (us == BLACK) {
            r = 5 - r;
        }
        
        //king distance
        int kdist_us_bonus = distance(sd->brd.get_sq(KING[us]), to) * r * (r - 1);
        int kdist_them_bonus = distance(sd->brd.get_sq(KING[them]), to) * r * (r - 1) * 2;
        result->add(0, kdist_them_bonus - kdist_us_bonus);
        
        //connected and defended passers
        U64 bit_sq = BIT(sq);
        U64 connection_mask = RIGHT1(bit_sq) | LEFT1(bit_sq);
        if (connection_mask & sd->brd.bb[PAWN[us]]) {
            result->add(10, 10 + r * 10);
        } else {
            bit_sq = BIT(sq + PAWNDIRECTION[them]);
            connection_mask = RIGHT1(bit_sq) | LEFT1(bit_sq);
            if (connection_mask & sd->brd.bb[PAWN[us]]) {
                result->add(5, 5 + r * 5);
            }
        }

        //advancing
        do {
            if (BIT(to) & sd->brd.bb[ALLPIECES]) {
                break; //blocked
            }
            sd->brd.bb[ALLPIECES] ^= BIT(sq); //to include rook/queen xray attacks from behind
            U64 attacks = sd->brd.attacks_to(to);
            sd->brd.bb[ALLPIECES] ^= BIT(sq);
            U64 defend = attacks & sd->brd.all(them);
            U64 support = attacks & sd->brd.all(us);
            if (defend) {
                if (support == 0) {
                    break;
                }
                if (is_1(support) && gt_1(defend)) {
                    break;
                }
            }
            result->add(bonus);
            to += step;
        } while (to >= a1 && to <= h8);
    }
    if (has_imbalance(sd, them)) {
        if (has_major_imbalance(sd)) {
            result->mul256(128);
        } else {
            result->mul256(196);
        }
    }
    return result;
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

score_t * eval_king_attack(search_t * sd, bool us) {
    score_t * result = &sd->stack->king_score[us];
    result->clear();
    board_t * pos = &sd->brd;
    if (pos->bb[QUEEN[us]] == 0) {
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
    int shelter_ix = range(0, 23, KING_ATTACK_OFFSET + sd->stack->king_attack[PAWN[us]]);
    result->set(KING_SHELTER[shelter_ix], 0);

    U64 kaz = sd->stack->king_attack_zone[us];


    kaz &= ~(RANK[us][7] | RANK[us][8] | KING_MOVES[pos->get_sq(KING[!us])]);
    result->add(12 * popcnt0(kaz), 0);


    /*
     * 2. Reduce the shelter score for closed positions and 
     * if little material is left
     */

    if ((sd->stack->pawn_flags & PFLAG_CLOSED_CENTER) != 0) {
        result->half(); //reduce shelter score for closed positions
    }

    if (max_1(pos->bb[KNIGHT[us]] | pos->bb[BISHOP[us]] | pos->bb[ROOK[us]])) {
        result->half();
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

    int piece_attack_score = 0;
    int paix = 2 * ka_units + shelter_ix + ka_squares - 5;
    piece_attack_score = KING_ATTACK[range(0, 63, paix)];
    piece_attack_score = MUL256(piece_attack_score, KING_ATTACKERS_MUL[range(0, 7, attackers)]);
    piece_attack_score = MUL256(piece_attack_score, KING_SHELTER_MUL[range(0, 7, shelter_ix)]);
    result->add(piece_attack_score, 0);
    return result;
}

namespace eg {

    const int BONUS[2] = {-10, 10};
    const int EDGE_DISTANCE[8] = {0, 1, 2, 3, 3, 2, 1, 0};

    int draw(int score, int div = 256) {
        if (score == 0 || div == 0) {
            return 0;
        } else if (score > 0) {
            return MAX(GRAIN_SIZE, score / div);
        }
        return MIN(-GRAIN_SIZE, score / div);
    }

    int win(bool us, int div = 1) {
        assert(div > 0);
        return us == WHITE ? score::WIN / div : -score::WIN / div;
    }

    bool has_pieces(search_t * s, bool us) {
        return s->brd.has_pieces(us);
    }

    bool has_pawns(search_t * s, bool us) {
        return s->brd.bb[PAWN[us]] != 0;
    }

    bool has_winning_edge(search_t * s, bool us) {
        return us == WHITE ? s->stack->material_score >= VROOK :
                s->stack->material_score <= -VROOK;
    }

    /**
     * Routine to drive their king to the right corner for checkmate
     * @param s search object
     * @param white white or black king
     * @return score value related to distance to corner and distance between kings
     */
    int corner_king(search_t * s, bool them, int div = 1) {
        board_t * pos = &s->brd;
        int kpos[2] = {pos->get_sq(BKING), pos->get_sq(WKING)};
        int king_dist = distance(kpos[BLACK], kpos[WHITE]);
        int r_dist = EDGE_DISTANCE[RANK(kpos[them])];
        int f_dist = EDGE_DISTANCE[FILE(kpos[them])];
        int edge_dist = MIN(r_dist, f_dist);
        int result = 10 - 2 * king_dist; //bring the king nearby
        bool us = !them;
        if (is_1(pos->bb[BISHOP[us]]) && pos->bb[ROOK[us]] == 0 && pos->bb[QUEEN[us]] == 0) {
            int corner_dist = 0;
            if ((pos->bb[BISHOP[us]] & WHITE_SQUARES) != 0) {
                corner_dist = MIN(distance(kpos[them], a8), distance(kpos[them], h1));
            } else {
                corner_dist = MIN(distance(kpos[them], a1), distance(kpos[them], h8));
            }
            result += 250 - 50 * corner_dist; //1: to the correct corner
            result += 20 - 10 * edge_dist; //2: to the edge 
        } else {
            result += 200 - 100 * edge_dist; //1: to the edge
            result += 100 - 20 * (r_dist + f_dist); //2: to any corner
        }
        return us == WHITE ? result / div : -result / div;
    }

    /**
     * Routine to verify if our pawns are blocked
     */
    bool blocked_pawns(search_t * s, bool us) {
        U64 pawns = s->brd.bb[PAWN[us]];
        int direction = us == WHITE ? 8 : -8;
        bool them = !us;
        U64 occ = s->brd.all(them);
        while (pawns) {
            bool blocked = false;
            int sq = pop(pawns);
            do {
                sq += direction;
                if (BIT(sq) & occ) {
                    blocked = true;
                    break;
                }
                if (s->brd.is_attacked(sq, them)) {
                    blocked = true;
                    break;
                }
            } while (sq >= a2 && sq <= h7);
            if (!blocked) {
                return false;
            }
        }
        return true;
    }
    
    int piece_distance(search_t * s, const bool us) {
        assert(gt_1(s->brd.all(us)));
        int fsq = bsf(s->brd.all(us));
        int rsq = bsr(s->brd.all(us));
        assert(fsq != rsq);
        return distance(fsq, rsq);
    }

    bool has_unstoppable_pawn(search_t * s, const bool us) {
        return s->stack->passer_score[us].eg > 650;
    }

    bool eg_test(search_t * s, bool pawns_us, bool pcs_us, bool pawns_them, bool pcs_them, bool us) {
        const bool them = !us;
        return pawns_us == bool(s->brd.bb[PAWN[us]])
                && pawns_them == bool(s->brd.bb[PAWN[them]])
                && pcs_us == s->brd.has_pieces(us)
                && pcs_them == s->brd.has_pieces(them)
                && (s->stack->phase == 16) == (!pcs_us && !pcs_them);
    }

    int knpk(search_t * s, const int score, const bool us) {
        const bool them = !us;
        if (s->brd.bb[PAWN[us]] & EDGE & RANK[us][7]) {
            U64 queening_square = fill_up(s->brd.bb[PAWN[us]], us) & RANK[us][8];
            if (distance(bsf(queening_square), s->brd.get_sq(KING[them])) <= 1) {
                return draw(score, 128);
            }
        }
        return score;
    }

    int kbpsk(search_t * s, const int score, const bool us) {
        const U64 queening_squares = fill_up(s->brd.bb[PAWN[us]], us) & RANK[us][8];
        const bool all_on_edge = (s->brd.bb[PAWN[us]] & ~EDGE) == 0;
        const bool them = !us;
        if (all_on_edge && is_1(queening_squares)) { //all pawns on A or all pawns on H
            bool w1 = s->brd.bb[BISHOP[us]] & WHITE_SQUARES;
            bool w2 = queening_squares & WHITE_SQUARES;
            if (w1 != w2) { //wrong colored bishop
                U64 control_us = KING_MOVES[s->brd.get_sq(KING[us])] | s->brd.bb[KING[us]];
                if ((control_us & queening_squares) == queening_squares) {
                    return score + win(us, 8);
                }
                U64 control_them = KING_MOVES[s->brd.get_sq(KING[them])] | s->brd.bb[KING[them]];
                control_them &= ~control_us;
                if ((control_them & queening_squares) == queening_squares) {
                    return draw(score, 128);
                }
                return draw(score, 4);
            }
        }
        return score;
    }

    /**
     * Evaluate opposite bishops with pawns endgame
     */
    int opp_bishops(search_t * s, const int score, const bool us) {
        assert(s->brd.is_eg(OPP_BISHOPS, us));
        static const int PF[9] = {128, 16, 8, 4, 2, 2, 2, 2, 2};
        int pawn_count = s->brd.count(PAWN[us]);
        int pf = PF[pawn_count];
        if (blocked_pawns(s, us)) {
            pf *= 2;
        }
        return draw(score, pf);
    }

    /**
     * Evaluate KRKP endgame
     */
    int krkp(search_t * s, const int score, const bool us) {
        assert(s->brd.is_eg(KRKP, us));

        //it's a win if our king blocks the pawn
        const bool them = !us;
        const U64 promotion_path = fill_up(s->brd.bb[PAWN[them]], them) & ~s->brd.bb[PAWN[them]];
        if ((promotion_path & s->brd.bb[KING[us]]) != 0) {
            return score + win(us, 2);
        }

        //it's a win if we can block the pawn and the opponent can not stop us
        const int kpos_us = s->brd.get_sq(KING[us]);
        const int kpos_them = s->brd.get_sq(KING[them]);
        const U64 path_attacks = KING_MOVES[kpos_us] & promotion_path;
        const U64 path_defends = KING_MOVES[kpos_them] & promotion_path;
        const bool utm = s->brd.stack->wtm == (us == WHITE);
        if (utm && path_attacks != 0 && path_defends == 0) {
            return score + win(us, 2);
        }

        //it's a win if the rook is on the same file as the pawn and...
        const int pawn_sq = s->brd.get_sq(PAWN[them]);
        const int rook_sq = s->brd.get_sq(ROOK[us]);
        const int def_dist = distance(kpos_them, pawn_sq) + utm - 2;
        const bool same_file = FILE(rook_sq) == FILE(pawn_sq);
        if (same_file && def_dist > 0) {
            //...they are too far away to defend
            return score + win(us, 2);
        } else if (same_file && path_attacks != 0) {
            //...the promotion path is attacked by our king too
            return score + win(us, 2);
        }

        //it's a win if the rook can capture the pawn before it can promote or be defended
        const int prom_dist = popcnt(promotion_path) + utm;
        const int max_rook_dist = 3;
        if (max_rook_dist <= MIN(prom_dist, def_dist)) {
            return score + win(us, 4);
        }

        //other cases are drawish, especially with a far advanced and defended pawn
        int bonus = def_dist + prom_dist - same_file + bool(path_attacks)
                - bool(path_defends) - distance(kpos_us, pawn_sq) - 1;
        if (prom_dist < 3 && path_defends != 0) {
            return draw(score, 16) + BONUS[us] * bonus / 2;
        }
        return draw(score, 8) + BONUS[us] * bonus;
    }

    /**
     * Evaluate KQKP endgame
     */
    int kqkp(search_t * s, const int score, const bool us) {
        assert(s->brd.is_eg(KQKP, us));

        //it's a win if the pawn is not near promotion
        const bool them = !us;
        const bool utm = s->brd.stack->wtm == (us == WHITE);
        U64 advanced_ranks = RANK[them][7];
        if (!utm) {
            advanced_ranks |= RANK[them][6];
        }
        if ((s->brd.bb[PAWN[them]] & advanced_ranks) == 0) {
            return score + win(us, 4);
        }

        const bool on_acfh = s->brd.bb[PAWN[them]] & (FILE_A | FILE_C | FILE_F | FILE_H);
        if (!on_acfh) {
            return score;
        }

        //it's drawish if their king is supporting the pawn on file a, c, f, or h
        const int kpos_us = s->brd.get_sq(KING[us]);
        const int kpos_them = s->brd.get_sq(KING[them]);
        const U64 promotion_path = fill_up(s->brd.bb[PAWN[them]], them) & ~s->brd.bb[PAWN[them]];
        const U64 path_attacks = KING_MOVES[kpos_us] & promotion_path;
        const U64 path_defends = (s->brd.bb[KING[them]] | KING_MOVES[kpos_them]) & promotion_path;
        if (path_defends != 0 && path_attacks == 0) {
            return draw(score, 32);
        }
        return score;
    }

    /**
     * Pawns vs lone king (case 1)
     */
    int pawns_vs_king(search_t * s, const int score, const bool us) {
        assert(eg_test(s, 1, 0, 0, 0, us));
        const bool utm = s->brd.stack->wtm == (us == WHITE);
        const bool them = !us;
        if (max_1(s->brd.bb[PAWN[us]])) { //KPK
            bool won = KPK::probe(utm, s->brd.get_sq(KING[us]), s->brd.get_sq(KING[them]),
                    s->brd.get_sq(PAWN[us]), us == BLACK);
            if (won) {
                return score + win(us, 2);
            }
            return draw(score, 64);
        } else if (has_unstoppable_pawn(s, us)) { //unstoppable pawn
            return score + win(us, 2);
        }
        return score;
    }

    /**
     * Evaluate endgames with only kings and pawns (case 3)
     */
    int pawns_vs_pawns(search_t * s, int score, const bool us) {
        assert(eg_test(s, 1, 0, 1, 0, us));
        return score;
    }

    /**
     * Piece(s) vs lone king (case 4)
     */
    int pcs_vs_king(search_t * s, const int score, const bool us) {
        assert(eg_test(s, 0, 1, 0, 0, us));
        const bool them = !us;
        if (has_mating_power(s, us)) { //e.g. KBNK and better
            return score + win(us) + corner_king(s, them);
        } else { //e.g. KNNK and worse
            return draw(score, 128);
        }
    }

    /**
     * Piece(s) and pawn(s) vs lone king (case 5)
     */
    int pcs_n_pawns_vs_king(search_t * s, const int score, const bool us) {
        assert(eg_test(s, 1, 1, 0, 0, us));
        const bool them = !us;
        if (has_mating_power(s, us)) {
            return score + win(us) + corner_king(s, them);
        } else if (has_unstoppable_pawn(s, us)) { //unstoppable passed pawn
            return score + win(us, 4);
        } else if (s->brd.is_eg(KNPK, us)) { //KNPK
            return knpk(s, score, us);
        } else if (s->brd.is_eg(KBPSK, us)) { //KBPK, KBPPK, ...
            return kbpsk(s, score, us);
        }
        return score;
    }

    /**
     * Piece(s) vs pawn(s) (case 6)
     */
    int pcs_vs_pawns(search_t * s, const int score, const bool us) {
        assert(eg_test(s, 0, 1, 1, 0, us));
        const bool pow_us = has_mating_power(s, us);
        if (!pow_us) {
            return draw(score, 128);
        } else if (s->brd.is_eg(KRKP, us)) {
            return krkp(s, score, us);
        } else if (s->brd.is_eg(KQKP, us)) {
            return kqkp(s, score, us);
        }
        return score;
    }

    /**
     * Piece(s) and pawn(s) vs pawn(s) (case 7)
     */
    int pcs_n_pawns_vs_pawns(search_t * s, const int score, const bool us) {
        assert(eg_test(s, 1, 1, 1, 0, us));
        return score;
    }

    /**
     * Pawn(s) vs piece(s) (case 9)
     */
    int pawns_vs_pcs(search_t * s, const int score, const bool us) {
        assert(eg_test(s, 1, 0, 0, 1, us));
        return score;
    }

    /**
     * Pawn(s) vs piece(s) (case 11)
     */
    int pawns_vs_pcs_n_pawns(search_t * s, const int score, const bool us) {
        assert(eg_test(s, 1, 0, 1, 1, us));
        return score;
    }

    /**
     * Evaluate cases where both sides have pieces, but no pawns (case 12)
     */
    int pcs_vs_pcs(search_t * s, const int score, const bool us) {
        assert(eg_test(s, 0, 1, 0, 1, us));
        const bool them = !us;
        if (!has_mating_power(s, us)) { //no mating power -> draw 
            return draw(score, 16);
        } else if (s->brd.is_eg(KBBKN, us)) { //KBBKN is an exception
            return score + win(us, 2) + corner_king(s, them, 2) + 10 * piece_distance(s, them);
        } else if (!has_winning_edge(s, us)) { //no winning edge -> draw
            return draw(score, 16) + corner_king(s, them, 16);
        } else if (has_mating_power(s, them)) { //win 
            return score + win(us, 8) + corner_king(s, them, 8);
        } else { //win and they can impossibly win
            return score + win(us, 4) + corner_king(s, them, 4);
        }
    }

    /**
     * Piece(s) and pawn(s) vs piece(s) (case 13)
     */
    int pcs_n_pawns_vs_pcs(search_t * s, const int score, const bool us) {
        assert(eg_test(s, 1, 1, 0, 1, us));
        const bool them = !us;
        const bool pow_us = has_mating_power(s, us);
        const bool pow_them = has_mating_power(s, them);
        const bool win_us = has_winning_edge(s, us);
        if (pow_us && win_us) {
            //winning material edge
            if (pow_them) {
                return score + win(us, 8) + corner_king(s, them, 8);
            } else {
                return score + win(us, 4) + corner_king(s, them, 4);
            }
        } else if (!pow_us && max_1(s->brd.bb[PAWN[us]])) {
            //they can sacrifice their piece(s) to force a draw
            if (blocked_pawns(s, us)) {
                return draw(score, 8);
            } else {
                return draw(score, 4);
            }
        }
        return score;
    }

    /**
     * Piece(s) vs piece(s) and pawn(s) (case 14)
     */
    int pcs_vs_pcs_n_pawns(search_t * s, const int score, const bool us) {
        assert(eg_test(s, 0, 1, 1, 1, us));
        const bool pow_us = has_mating_power(s, us);
        const bool win_us = has_winning_edge(s, us);
        const bool them = !us;
        if (!pow_us && !win_us) {
            return draw(score, 128);
        } else if (!win_us) {
            if (!has_imbalance(s, us)) {
                return draw(score, 64) + corner_king(s, them, 16);
            } else if (!has_major_imbalance(s)) {
                return draw(score, 32) + corner_king(s, them, 16);
            } else {
                return draw(score, 2) + corner_king(s, them, 8);
            }
        } else { //winning edge
            return score + corner_king(s, them, 4);
        }
    }

    /**
     * Piece(s) and pawn(s) vs piece(s) and pawn(s) (case 15)
     */
    int pcs_n_pawns_vs_pcs_n_pawns(search_t * s, const int score, const bool us) {
        assert(eg_test(s, 1, 1, 1, 1, us));
        if (s->brd.is_eg(OPP_BISHOPS, us)) {
            return opp_bishops(s, score, us);
        }
        return score;
    }

    /**
     * Main endgame evaluation function
     */
    int eval(search_t * s, const int score) {
        const bool us = (score > 0) || (score == 0 && s->brd.stack->wtm); //winning side
        const bool them = !us;
        int eg_ix = has_pawns(s, us) + 2 * has_pawns(s, them)
                + 4 * has_pieces(s, us) + 8 * has_pieces(s, them);

        switch (eg_ix) { //1      4        2      8
            case 0: //  ----- ------ vs ----- ------ (KK)
                return draw(score);
            case 1: //  pawns ------ vs ----- ------
                return pawns_vs_king(s, score, us);
            case 2: //  ----- ------ vs pawns ------
                assert(false);
                return draw(score);
            case 3: //  pawns ------ vs pawns ------
                return pawns_vs_pawns(s, score, us);
            case 4: //  ----- pieces vs ----- ------
                return pcs_vs_king(s, score, us);
            case 5: // pawns pieces vs ----- ------
                return pcs_n_pawns_vs_king(s, score, us);
            case 6: //  ----- pieces vs pawns ------
                return pcs_vs_pawns(s, score, us);
            case 7: // pawns pieces vs pawns ------
                return pcs_n_pawns_vs_pawns(s, score, us);
            case 8: //  ----- ------ vs ----- pieces
                assert(false);
                return draw(score);
            case 9: //  pawns ------ vs ----- pieces
                return pawns_vs_pcs(s, score, us);
            case 10: // ----- ------ vs pawns pieces
                assert(false);
                return draw(score);
            case 11: // pawns ------ vs pawns pieces
                return pawns_vs_pcs_n_pawns(s, score, us);
            case 12: // ----- pieces vs ----- pieces
                return pcs_vs_pcs(s, score, us);
            case 13: // pawns pieces vs ----- pieces
                return pcs_n_pawns_vs_pcs(s, score, us);
            case 14: // ----- pieces vs pawns pieces
                return pcs_vs_pcs_n_pawns(s, score, us);
            case 15: // pawns pieces vs ----- pieces
                return pcs_n_pawns_vs_pcs_n_pawns(s, score, us);
            default:
                assert(false);
                return score;
        }
        return score;
    }
}


/*
 
  add<KQKP>("KQKP"); 
  add<KNPKB>("KNPKB");
  add<KRPKR>("KRPKR");
  add<KRPKB>("KRPKB");
  add<KBPKB>("KBPKB");
  add<KBPKN>("KBPKN");
  add<KBPPKB>("KBPPKB");
  add<KRPPKRP>("KRPPKRP");
}
 */
