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
#include "endgame.h"

//#define TRACE_EVAL

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
    board_t * brd = &sd->brd;
    if (brd->ply > 0 &&
            (brd->stack - 1)->material_hash == brd->stack->material_hash
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
    if (material_table::retrieve(brd->stack->material_hash, value, phase, flags)) {
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

    int wpawns = brd->count(WPAWN);
    int bpawns = brd->count(BPAWN);
    int wknights = brd->count(WKNIGHT);
    int bknights = brd->count(BKNIGHT);
    int wbishops = brd->count(WBISHOP);
    int bbishops = brd->count(BBISHOP);
    int wrooks = brd->count(WROOK);
    int brooks = brd->count(BROOK);
    int wqueens = brd->count(WQUEEN);
    int bqueens = brd->count(BQUEEN);
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
    material_table::store(brd->stack->material_hash, value, phase, flags);
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
    board_t * brd = &sd->brd;
    int wkpos = brd->get_sq(WKING);
    int bkpos = brd->get_sq(BKING);
    sd->stack->mob[WHITE] = ~(brd->bb[WPAWN] | brd->pawn_attacks(BLACK) | brd->bb[WKING]);
    sd->stack->mob[BLACK] = ~(brd->bb[BPAWN] | brd->pawn_attacks(WHITE) | brd->bb[BKING]);
    sd->stack->attack[WHITE] = brd->bb[BPAWN] | brd->bb[BKING];
    sd->stack->attack[BLACK] = brd->bb[WPAWN] | brd->bb[WKING];
    sd->stack->king_attack_zone[WHITE] = magic::queen_moves(bkpos, brd->pawns_kings()) & sd->stack->mob[WHITE];
    sd->stack->king_attack_zone[BLACK] = magic::queen_moves(wkpos, brd->pawns_kings()) & sd->stack->mob[BLACK];

    /*
     * 2. Probe the hash table for the pawn score
     */
    U64 passers;
    int king_attack[2];
    int flags;
    score_t pawn_score;

    if (pawn_table::retrieve(brd->stack->pawn_hash, passers, pawn_score, king_attack, flags)) {
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
        ~FILEFILL(brd->bb[BPAWN]),
        ~FILEFILL(brd->bb[WPAWN])
    };
    const U64 up_all[2] = {
        DOWN1(brd->bb[BPAWN]),
        UP1(brd->bb[WPAWN])
    };
    const U64 up_left[2] = {
        DOWNLEFT1(brd->bb[BPAWN]),
        UPLEFT1(brd->bb[WPAWN])
    };
    const U64 up_right[2] = {
        DOWNRIGHT1(brd->bb[BPAWN]),
        UPRIGHT1(brd->bb[WPAWN])
    };
    const U64 attacks[2] = {
        up_left[BLACK] | up_right[BLACK],
        up_right[WHITE] | up_left[WHITE]
    };
    const U64 blocked_pawns[2] = {
        UP1(up_all[BLACK] & brd->bb[WPAWN]) & ~attacks[WHITE],
        DOWN1(up_all[WHITE] & brd->bb[BPAWN]) & ~attacks[BLACK]
    };
    const U64 safe[2] = {
        (up_left[BLACK] & up_right[BLACK]) | ~attacks[WHITE] | ((up_left[BLACK] ^ up_right[BLACK]) & ~(up_left[WHITE] & up_right[WHITE])),
        (up_left[WHITE] & up_right[WHITE]) | ~attacks[BLACK] | ((up_left[WHITE] ^ up_right[WHITE]) & ~(up_left[BLACK] & up_right[BLACK]))
    };
    const U64 isolated_pawns[2] = {
        brd->bb[BPAWN] & ~(FILEFILL(attacks[BLACK])),
        brd->bb[WPAWN] & ~(FILEFILL(attacks[WHITE]))
    };
    const U64 doubled_pawns[2] = {
        UP1(fill_north(brd->bb[BPAWN])) & brd->bb[BPAWN],
        DOWN1(fill_south(brd->bb[WPAWN])) & brd->bb[WPAWN]
    };

    U64 moves[2] = {
        DOWN1(brd->bb[BPAWN] & ~blocked_pawns[BLACK]) & safe[BLACK],
        UP1(brd->bb[WPAWN] & ~blocked_pawns[WHITE]) & safe[WHITE]
    };
    moves[WHITE] |= UP1(moves[WHITE] & RANK_3 & ~attacks[BLACK] & ~DOWN1((brd->bb[BPAWN] | brd->bb[WPAWN]))) & safe[WHITE];
    moves[BLACK] |= DOWN1(moves[BLACK] & RANK_6 & ~attacks[WHITE] & ~UP1((brd->bb[WPAWN] | brd->bb[BPAWN]))) & safe[BLACK];

    const U64 attack_range[2] = {
        attacks[BLACK] | DOWNLEFT1(moves[BLACK]) | DOWNRIGHT1(moves[BLACK]),
        attacks[WHITE] | UPLEFT1(moves[WHITE]) | UPRIGHT1(moves[WHITE])
    };

    const U64 kcz[2] = {KING_ZONE[bkpos], KING_ZONE[wkpos]};
    int blocked_center_pawns = 0;

    U64 wpawns = brd->bb[WPAWN];
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
        bool passed = !doubled && !(up & (attacks[BLACK] | brd->bb[BPAWN]));
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

    U64 bpawns = brd->bb[BPAWN];
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
        bool passed = !doubled && !(down & (attacks[WHITE] | brd->bb[WPAWN]));
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
    if (brd->stack->castling_flags & CASTLE_K
            && ((brd->matrix[h2] == WPAWN && brd->matrix[g2] == WPAWN)
            || (brd->matrix[f2] == WPAWN && brd->matrix[h2] == WPAWN && brd->matrix[g3] == WPAWN)
            || (brd->matrix[h3] == WPAWN && brd->matrix[g2] == WPAWN && brd->matrix[f2] == WPAWN))) {
        king_attack[BLACK] += SHELTER_CASTLING_KINGSIDE;
    } else if (brd->stack->castling_flags & CASTLE_Q
            && ((brd->matrix[a2] == WPAWN && brd->matrix[b2] == WPAWN && brd->matrix[c2] == WPAWN)
            || (brd->matrix[a2] == WPAWN && brd->matrix[b3] == WPAWN && brd->matrix[c2] == WPAWN))) {
        king_attack[BLACK] += SHELTER_CASTLING_QUEENSIDE;
    }

    //attack units - shelter pawns
    U64 king_front = (FORWARD_RANKS[RANK(wkpos)] | KING_MOVES[wkpos]) & PAWN_SCOPE[FILE(wkpos)];
    U64 shelter_pawns = king_front & brd->bb[WPAWN];
    while (shelter_pawns) {
        int sq = pop(shelter_pawns);
        king_attack[BLACK] += SHELTER_PAWN[FLIP_SQUARE(sq)];
    }

    //attack units - storm pawns
    U64 storm_pawns = king_front & brd->bb[BPAWN];
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
    if (brd->stack->castling_flags & CASTLE_k
            && ((brd->matrix[h7] == BPAWN && brd->matrix[g7] == BPAWN)
            || (brd->matrix[f7] == BPAWN && brd->matrix[h7] == BPAWN && brd->matrix[g6] == BPAWN)
            || (brd->matrix[h6] == BPAWN && brd->matrix[g7] == BPAWN && brd->matrix[f7] == BPAWN))) {
        king_attack[WHITE] += SHELTER_CASTLING_KINGSIDE;
    } else if (brd->stack->castling_flags & CASTLE_q
            && ((brd->matrix[a7] == BPAWN && brd->matrix[b7] == BPAWN && brd->matrix[c7] == BPAWN)
            || (brd->matrix[a7] == BPAWN && brd->matrix[b6] == BPAWN && brd->matrix[c7] == BPAWN))) {
        king_attack[WHITE] += SHELTER_CASTLING_QUEENSIDE;
    }

    //attack units - shelter pawns
    king_front = (BACKWARD_RANKS[RANK(bkpos)] | KING_MOVES[bkpos]) & PAWN_SCOPE[FILE(bkpos)];
    shelter_pawns = king_front & brd->bb[BPAWN];
    while (shelter_pawns) {
        int sq = pop(shelter_pawns);
        king_attack[WHITE] += SHELTER_PAWN[sq];
    }

    //attack units - storm pawns
    storm_pawns = king_front & brd->bb[WPAWN];
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

    pawn_table::store(brd->stack->pawn_hash, passers, pawn_score, king_attack, flags);
    return &sd->stack->pawn_score;
}

score_t * eval_knights(search_t * sd, bool us) {

    score_t * result = &sd->stack->knight_score[us];
    board_t * brd = &sd->brd;
    int pc = KNIGHT[us];
    result->clear();
    sd->stack->king_attack[pc] = 0;
    U64 knights = brd->bb[pc];

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
    int pawn_width = bb_width(brd->bb[PAWN[them]]);
    int pawn_count = brd->count(PAWN[them]);
    int kpos = brd->get_sq(KING[them]);
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

        if (brd->is_attacked_by_pawn(sq, us)) {
            result->add(KNIGHT_OUTPOST[ISQ(sq, us)]);
        }
        if (brd->is_attacked_by_pawn(sq, them)) {
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
    board_t * brd = &sd->brd;
    int pc = BISHOP[us];
    result->clear();
    sd->stack->king_attack[pc] = 0;
    U64 bishops = brd->bb[pc];

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
    if (brd->has_bishop_pair(us) && (sd->stack->pawn_flags & PFLAG_CLOSED_CENTER) == 0) {
        result->add(VBISHOPPAIR);
    }

    U64 occ = brd->pawns_kings();
    bool them = !us;
    int kpos = brd->get_sq(KING[them]);
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
        if (brd->is_attacked_by_pawn(sq, us)) {
            result->add(BISHOP_OUTPOST[ISQ(sq, us)]);
        }
        if (brd->is_attacked_by_pawn(sq, them)) {
            result->add(ATTACKED_PIECE);
        }

        //patterns
        if (BIT(sq) & BISHOP_PATTERNS[us]) {
            if (us == WHITE) {
                if (((sq == d3 || sq == d4) && (brd->matrix[d2] == WPAWN || brd->matrix[d3] == WPAWN))) {
                    result->add(BLOCKED_CENTER_PAWN);
                } else if (((sq == e3 || sq == e4) && (brd->matrix[e2] == WPAWN || brd->matrix[e3] == WPAWN))) {
                    result->add(BLOCKED_CENTER_PAWN);
                } else if ((sq == h7 && brd->matrix[g6] == BPAWN && brd->matrix[f7] == BPAWN)
                        || (sq == a7 && brd->matrix[b6] == BPAWN && brd->matrix[c7] == BPAWN)) {
                    result->add(TRAPPED_BISHOP);
                }
            } else if (us == BLACK) {
                if (((sq == d6 || sq == d5) && (brd->matrix[d7] == BPAWN || brd->matrix[d6] == BPAWN))) {
                    result->add(BLOCKED_CENTER_PAWN);
                } else if (((sq == e6 || sq == e5) && (brd->matrix[e7] == BPAWN || brd->matrix[e6] == BPAWN))) {
                    result->add(BLOCKED_CENTER_PAWN);
                } else if ((sq == h2 && brd->matrix[g3] == WPAWN && brd->matrix[f3] == WPAWN)
                        || (sq == a2 && brd->matrix[b3] == WPAWN && brd->matrix[c2] == WPAWN)) {
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
    board_t * brd = &sd->brd;
    int pc = ROOK[us];
    result->clear();
    sd->stack->king_attack[pc] = 0;
    U64 rooks = brd->bb[pc];

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
    U64 fill[2] = {FILEFILL(brd->bb[BPAWN]), FILEFILL(brd->bb[WPAWN])};
    U64 occ = brd->pawns_kings();
    if ((brd->bb[ROOK[us]] & RANK[us][1]) && (BIT(brd->get_sq(KING[us])) & (RANK[us][1] | RANK[us][2]))) {
        result->add(ROOK_1ST); //at least one rook is protecting the back rank
    }
    int kpos = brd->get_sq(KING[them]);
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
                int kpos_us = brd->get_sq(KING[us]);
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

        if (brd->is_attacked_by_pawn(sq, them)) {
            result->add(ATTACKED_PIECE);
        }

        U64 moves = magic::rook_moves(sq, occ) & sd->stack->mob[us];
        if (moves & sd->stack->attack[us]) {
            result->add(popcnt(moves & sd->stack->attack[us]) * ROOK_ATTACK);
        }

        if ((bitSq & RANK[us][7]) && (BIT(brd->get_sq(KING[them])) & BACKRANKS[us])) {
            result->add(ROOK_7TH);
        }

        //Tarrasch Rule: place rook behind passers
        U64 tpass = moves & sd->stack->passers; //touched passers
        if (tpass) {
            U64 front[2];
            front[BLACK] = fill_south(bitSq) & tpass;
            front[WHITE] = fill_north(bitSq) & tpass;
            if (front[us] & brd->bb[PAWN[us]] & SIDE[them]) { //supporting a passer from behind
                result->add(ROOK_GOOD_SIDE);
            } else if (front[them] & brd->bb[PAWN[them]] & SIDE[us]) { //attacking a passer from behind
                result->add(ROOK_GOOD_SIDE);
            } else if (front[them] & brd->bb[PAWN[us]] & SIDE[them]) { //supporting from the wrong side
                result->add(ROOK_WRONG_SIDE);
            } else if (front[us] & brd->bb[PAWN[them]] & SIDE[us]) { //attacking from the wrong side
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

int eval_mate_threat(search_t * s, const U64 attacks_us, const int kpos_them, const bool us) {
    U64 kpos_bit = BIT(kpos_them);
    if ((kpos_bit & EDGE) == 0) {
        return 0;
    }
    U64 mate_squares = 0;
    if (kpos_bit & CORNER) {
        mate_squares = KING_MOVES[kpos_them];
    } else if (kpos_bit & RANK_1) {
        mate_squares = UP1(kpos_bit);
    } else if (kpos_bit & RANK_8) {
        mate_squares = DOWN1(kpos_bit);
    } else if (kpos_bit & FILE_A) {
        mate_squares = RIGHT1(kpos_bit);
    } else if (kpos_bit & FILE_H) {
        mate_squares = LEFT1(kpos_bit);
    }
    U64 target = mate_squares & attacks_us;
    if (target == 0) {
        return 0;
    }
    board_t * brd = &s->brd;
    bool them = !us;
    int result = 10;
    do {
        int sq = pop(target);
        if (brd->is_attacked_excl_queen(sq, us)) {
            result += 10;
            if (!brd->is_attacked_excl_king(sq, them)) {
                return 200;
            }
        }
    } while (target);
    return result;
}

score_t * eval_queens(search_t * sd, bool us) {
    score_t * result = &sd->stack->queen_score[us];
    board_t * brd = &sd->brd;
    int pc = QUEEN[us];
    result->clear();
    sd->stack->king_attack[pc] = 0;
    U64 queens = brd->bb[pc];

    if (queens == 0) {
        return result;
    }

    /*
     * 2. Get the score from the last stack record if the previous move 
     *   a) did not change the pawn+kings structure (pawn hash) 
     *   b) did not move or capture any bishop
     */
    if (false && sd->stack->equal_pawns) {
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
    U64 occ = brd->pawns_kings();
    int kpos = brd->get_sq(KING[them]);
    U64 kaz = sd->stack->king_attack_zone[us] | KING_ZONE[kpos]; //king attack zone
    int ka_units = 0;
    int ka_squares = 0;
    while (queens) {
        int sq = pop(queens);
        result->add(PST[WQUEEN][ISQ(sq, us)]);
        U64 moves = magic::queen_moves(sq, occ) & sd->stack->mob[us];
        int count = popcnt0(moves);
        result->add(QUEEN_MOBILITY[count]);
        if (brd->is_attacked_by_pawn(sq, them)) {
            result->add(ATTACKED_PIECE);
        }
        result->add(10 - distance_rank(sq, kpos) - distance_file(sq, kpos));
        if (moves & kaz) {
            ka_units++;
            ka_squares += popcnt(moves & kaz);
            result->add(eval_mate_threat(sd, moves, kpos, us));
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
    board_t * brd = &sd->brd;
    if (brd->bb[QUEEN[us]] == 0) {
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

    kaz &= ~(RANK[us][7] | RANK[us][8] | KING_MOVES[brd->get_sq(KING[!us])]);
    result->add(12 * popcnt0(kaz), 0);


    /*
     * 2. Reduce the shelter score for closed positions and 
     * if little material is left
     */

    if ((sd->stack->pawn_flags & PFLAG_CLOSED_CENTER) != 0) {
        result->half(); //reduce shelter score for closed positions
    }

    if (max_1(brd->bb[KNIGHT[us]] | brd->bb[BISHOP[us]] | brd->bb[ROOK[us]])) {
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
