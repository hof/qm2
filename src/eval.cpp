/**
 * Maxima, a chess playing program. 
 * Copyright (C) 1996-2015 Erik van het Hof and Hermen Reitsma 
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

#include "eval.h"
#include "search.h"
#include "bits.h"
#include "score.h"

#include "eval_pst.h"
#include "eval_pieces.h"
#include "eval_endgame.h"
#include "eval_pawns.h"

pst_t PST;

int eval_material(search_t * sd);
score_t * eval_king_attack(search_t * sd, bool white);

/**
 * Initialize the Piece Square Tables
 */
void init_pst() {
    for (int sq = a1; sq <= h8; sq++) {
        PST[EMPTY][sq].clear();
        PST[BPAWN][sq].set(PST_PAWN_MG[sq], PST_PAWN_EG[sq]);
        PST[BKNIGHT][sq].set(PST_KNIGHT_MG[sq], PST_KNIGHT_EG[sq]);
        PST[BBISHOP][sq].set(PST_BISHOP_MG[sq], PST_BISHOP_EG[sq]);
        PST[BROOK][sq].set(PST_ROOK_MG[sq], PST_ROOK_EG[sq]);
        PST[BQUEEN][sq].set(PST_QUEEN_MG[sq], PST_QUEEN_EG[sq]);
        PST[BKING][sq].set(PST_KING_MG[sq], PST_KING_EG[sq]);
        int fsq = FLIP_SQUARE(sq);
        for (int pc = WPAWN; pc <= WKING; pc++) {
            PST[pc][fsq].set(PST[pc + 6][sq]);
        }
    }
    assert(PST[WPAWN][e5].equals(PST[BPAWN][e4]));
    assert(PST[WKNIGHT][f3].equals(PST[BKNIGHT][f6]));
    assert(PST[WBISHOP][g2].equals(PST[BBISHOP][g7]));
    assert(PST[WROOK][d7].equals(PST[BROOK][d2]));
    assert(PST[WQUEEN][h8].equals(PST[BQUEEN][h1]));
    assert(PST[WKING][a1].equals(PST[BKING][a8]));
}

/*******************************************************************************
 * Material Evaluation Values 
 *******************************************************************************/

const score_t TEMPO[2] = {S(-10, 0), S(10, 0)};

const short VMATING_POWER = 20;
const short VMATING_MATERIAL = 50;

const short REDUNDANT_ROOK = -10;
const short REDUNDANT_KNIGHT = -5;
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
    const int flags = s->stack->mt->flags;
    return us == WHITE ? (flags & MFLAG_MATING_POWER_W) != 0
            : (flags & MFLAG_MATING_POWER_B) != 0;
}

bool has_imbalance(search_t * s, bool us) {
    const int flags = s->stack->mt->flags;
    return (flags & MFLAG_IMBALANCE) != 0 && bool(flags & 4) == !us;
}

bool has_major_imbalance(search_t * s) {
    const int flags = s->stack->mt->flags;
    return (flags & 2) != 0;
}

/*******************************************************************************
 * Main evaluation function
 *******************************************************************************/

int evaluate(search_t * sd) {

    if (sd->stack->in_check && sd->brd.ply < (MAX_PLY - 1)) {
        sd->stack->eval_result = score::INVALID;
        return score::INVALID;
    }

    if (sd->stack->eval_result != score::INVALID) {
        return sd->stack->eval_result;
    }

    bool wtm = sd->brd.stack->wtm;
    score_t * score = &sd->stack->eval_score;
    int result = eval_material(sd); //sets stack->mt->phase and material flags
    score->set(TEMPO[wtm]);
    score->add(pawns::eval(sd)); //sets passers and pawns flags
    score->add(pieces::eval(sd)); //also calculates king attack information
    if (sd->stack->pt->passers) {
        score->add(pawns::eval_passed_pawns(sd, WHITE));
        score->sub(pawns::eval_passed_pawns(sd, BLACK));
    }
    score->add(eval_king_attack(sd, WHITE));
    score->sub(eval_king_attack(sd, BLACK));
    result += score->get(sd->stack->mt->phase);
    sd->stack->eg_score = result;
    if (sd->stack->mt->flags & MFLAG_EG) {
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
int eval_material(search_t * s) {

    /*
     * 1. Probe the material table
     */
    board_t * brd = &s->brd;
    s->stack->mt = material_table::retrieve(brd->stack->material_hash);
    material_table::entry_t * e = s->stack->mt;
    if (e->key == brd->stack->material_hash) {
        return e->score;
    }

    /*
     * 2. Calculate material value and store in material hash table
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
    int phase = score::MAX_PHASE /* 16 */
            - wminors - bminors /* max: 8 */
            - wrooks - brooks /* max:4 */
            - 2 * (wqueens + bqueens) /* max: 4 */;
    phase = MAX(0, phase);

    /*
     * Material count evaluation
     */
    if (wknights != bknights) {
        result.add((wknights - bknights) * VKNIGHT);
        if (wknights > 1) {
            result.add(REDUNDANT_KNIGHT);
        }
        if (bknights > 1) {
            result.sub(REDUNDANT_KNIGHT);
        }
    }
    if (wbishops != bbishops) {
        result.add((wbishops - bbishops) * VBISHOP);
    }
    if (wrooks != brooks) {
        result.add((wrooks - brooks) * VROOK);
        if (wrooks > 1) {
            result.add(REDUNDANT_ROOK);
        }
        if (brooks > 1) {
            result.sub(REDUNDANT_ROOK);
        }
    }
    if (wqueens != bqueens) {
        result.add((wqueens - bqueens) * VQUEEN);
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
    int flags = 0;
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
    }

    if (wpawns != bpawns) {
        result.add((wpawns - bpawns) * VPAWN);
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
    e->key = brd->stack->material_hash;
    e->score = result.get(phase);
    ;
    e->flags = flags;
    e->phase = phase;
    return e->score;
}

const int8_t KING_ATTACK_OFFSET = 8; //perfectly castled king -9 units

const int8_t KING_ATTACK_UNIT[BKING + 1] = {
    //  x, p, n, b, r, q, k, p, n, b, r, q, k
    /**/0, 0, 1, 1, 2, 4, 0, 0, 1, 1, 2, 4, 0
};

const int8_t KING_DEFEND_UNIT[BKING + 1] = {
    //  x, p, n, b, r, q, k, p, n, b, r, q, k
    /**/0, 0, 3, 2, 1, 0, 0, 0, 3, 2, 1, 0, 0
};

const int16_t KING_SHELTER[24] = {//structural shelter (pawns & kings)
    0, 15, 25, 30, 40, 50, 60, 75,
    90, 105, 125, 145, 160, 175, 190, 200,
    210, 220, 225, 235, 235, 240, 245, 250
};

const int16_t KING_ATTACK_PCS[16] = {
    0, 48, 128, 256, 384, 464, 496, 512, 
    512, 512, 512, 512, 512, 512, 512, 512
};

const int16_t KING_ATTACK_UNITS[16] = {
    0, 0, 0, 16, 48, 128, 256, 384, 
    464, 496, 512, 512, 512, 512, 512, 512
};

score_t * eval_king_attack(search_t * s, bool us) {
    
    int pc = KING[us];
    score_t * result = &s->stack->pc_score[pc];
    result->clear();
    
    if (us == WHITE && (s->stack->mt->flags & MFLAG_KING_ATTACK_FORCE_W) == 0) {
        return result;
    }

    if (us == BLACK && (s->stack->mt->flags & MFLAG_KING_ATTACK_FORCE_B) == 0) {
        return result;
    }

    /*
     * 1. Shelter score
     */

    board_t * brd = &s->brd;
    int attack_score = 0;
    int shelter_ix = range(0, 23, KING_ATTACK_OFFSET + s->stack->pt->king_attack[us]);
    attack_score = KING_SHELTER[shelter_ix];

    if ((RANK[us][8] & brd->bb[ROOK[!us]]) == 0) { //no rook protecting their back rank
        attack_score += 20;
    }

    /*
     * 2. Reduce the shelter score for closed positions
     */

    if ((s->stack->pt->flags & pawn_table::FLAG_CLOSED_CENTER) != 0) {
        attack_score = attack_score / 2;
    }
    
    attack_score = (s->king_attack_shelter * attack_score) / 256;

    /*
     * 3. Calculate the total piece attack score
     */

    int attack_units = 0;
    int defend_units = 0;
    int attack_pcs = 0;
    int defend_pcs = 0;
    int offs[2] = { -6, 6 };
    
    for (int pc = KNIGHT[us]; pc <= QUEEN[us]; pc++) {
        int a = KA_UNITS(s->stack->king_attack[pc]);
        int d = KD_UNITS(s->stack->king_attack[pc + offs[us]]);
        attack_pcs += a;
        defend_pcs += d;
        attack_units += a * KING_ATTACK_UNIT[pc];
        defend_units += d * KING_DEFEND_UNIT[pc];
    }
       
    int pc_attack_score = attack_units * 20 - defend_units * 5;
    pc_attack_score = (s->king_attack_pieces * pc_attack_score) / 256;
    const int pc_ix = attack_pcs + (attack_pcs==0 && defend_pcs);
    attack_score += (KING_ATTACK_PCS[pc_ix] * pc_attack_score) / 256;
    result->set(attack_score, 0);
    return result;
}

