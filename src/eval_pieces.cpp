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
 * File: eval_pieces.cpp
 * Piece (kight, bishop, rook, queen) evaluation functions for normal chess
 */

#include "eval_pieces.h"
#include "search.h"
#include "bits.h"
#include "score.h"

extern pst_t PST;

const int8_t MOBILITY[32] = {
    -40, -20, -10, 0, 5, 10, 15, 20,
    20, 20, 20, 20, 20, 20, 20, 20,
    20, 20, 20, 20, 20, 20, 20, 20,
    20, 20, 20, 20, 20, 20, 20, 20
};

const int8_t ATTACKS[8] = {
    -5, 5, 10, 15, 20, 20, 20, 20
};

const int8_t ATTACKED[BKING + 1] = {
    0, 0, -30, -30, -40, -50, 0, 0, -30, -30, -40, -50, 0
};

const uint8_t KNIGHT_OUTPOST[64] = {
    0, 0, 0, 0, 0, 0, 0, 0,
    0, 0, 0, 5, 5, 0, 0, 0,
    0, 0, 10, 20, 20, 10, 0, 0,
    0, 0, 5, 10, 10, 5, 0, 0,
    0, 0, 0, 5, 5, 0, 0, 0,
    0, 0, 0, 0, 0, 0, 0, 0,
    0, 0, 0, 0, 0, 0, 0, 0,
    0, 0, 0, 0, 0, 0, 0, 0 //a1..h1
};

const uint8_t BISHOP_OUTPOST[64] = {
    0, 0, 0, 0, 0, 0, 0, 0,
    0, 5, 5, 5, 5, 5, 5, 0,
    0, 5, 5, 10, 10, 5, 5, 0,
    0, 0, 5, 5, 5, 5, 0, 0,
    0, 0, 0, 5, 5, 0, 0, 0,
    0, 0, 0, 0, 0, 0, 0, 0,
    0, 0, 0, 0, 0, 0, 0, 0,
    0, 0, 0, 0, 0, 0, 0, 0 //a1..h1
};

const score_t BLOCKED_CENTER_PAWN = -10; //piece blocking a center pawn on rank 2 or 3

const int8_t VBISHOPPAIR = 50;

const score_t TRAPPED_BISHOP = S(-60, -80);

const score_t DEFENDED = S(5, 0);

U64 BISHOP_PATTERNS[2] = {//black, white
    BIT(d6) | BIT(d7) | BIT(e6) | BIT(d7) | BIT(a2) | BIT(h2),
    BIT(d3) | BIT(d2) | BIT(e3) | BIT(d2) | BIT(a7) | BIT(h7),
};

const score_t ROOK_7TH = S(10, 10);
const score_t ROOK_SEMIOPEN_FILE = S(10, 10);
const score_t ROOK_OPEN_FILE = S(17, 17);
const score_t ROOK_CLOSED_FILE = S(-5, -5);
const score_t CONNECTED_ROOKS(10, 20);


U64 ROOK_PATTERNS[2] = {//black, white
    BIT(h8) | BIT(g8) | BIT(h7) | BIT(g7) | BIT(a8) | BIT(b8) | BIT(a7) | BIT(b7),
    BIT(h1) | BIT(g1) | BIT(h2) | BIT(g2) | BIT(a1) | BIT(b1) | BIT(a2) | BIT(b2)
};

const score_t TRAPPED_ROOK = S(-40, -80);

namespace pieces {

    score_t * eval(search_t * s) {
        score_t * result = &s->stack->pc_score[0];
        result->clear();
        board_t * brd = &s->brd;
        const bool equal_pawns = brd->ply > 0
                && brd->stack->pawn_hash == (brd->stack - 1)->pawn_hash
                && score::is_valid((s->stack - 1)->eval_result);
        const int prev_pc = equal_pawns ? (s->stack - 1)->current_move.capture : EMPTY;
        const int prev_cap = equal_pawns ? (s->stack - 1)->current_move.piece : EMPTY;
        pawn_table::entry_t * pi = s->stack->pt;
        const int kpos[2] = {brd->get_sq(BKING), brd->get_sq(WKING)};
        const U64 occ = brd->pawns_kings();

        for (int pc = WKNIGHT; pc <= BQUEEN; pc++) {

            if (pc == WKING || pc == BPAWN) {
                continue;
            }

            if (equal_pawns && pc != prev_pc && pc != prev_cap) {
                s->stack->pc_score[pc] = (s->stack - 1)->pc_score[pc];
                s->stack->king_attack[pc] = (s->stack - 1)->king_attack[pc];
                result->add_us(s->stack->pc_score[pc], pc <= WKING);
                continue;
            }

            s->stack->king_attack[pc] = 0;
            s->stack->pc_score[pc].clear();

            if (brd->bb[pc] == 0) {
                continue;
            }
            
            bool us = pc <= WKING;
            score_t * sc = &s->stack->pc_score[pc];
            U64 bb_pc = brd->bb[pc];
            U64 moves;
            int ka_units = 0;
            int ka_squares = 0;
            do {
                int sq = pop(bb_pc);
                bool defended = brd->is_attacked_by_pawn(sq, us);
                if (pc == WKNIGHT || pc == BKNIGHT) {
                    moves = KNIGHT_MOVES[sq];
                    if (defended) {
                        sc->add(DEFENDED);
                        if (brd->is_outpost(sq, us)) {
                            sc->add(KNIGHT_OUTPOST[ISQ(sq, us)]);
                        }
                    }
                } else if (pc == WBISHOP || pc == BBISHOP) {
                    moves = magic::bishop_moves(sq, occ);
                    if (defended) {
                        sc->add(DEFENDED);
                        if (brd->is_outpost(sq, us)) {
                            sc->add(BISHOP_OUTPOST[ISQ(sq, us)]);
                            }
                    }
                } else if (pc == WROOK || pc == BROOK) {
                    moves = magic::rook_moves(sq, occ);
                    if (!pi->is_open_file(sq, us)) {
                        sc->add(ROOK_CLOSED_FILE);
                    } else if (pi->is_open_file(sq, !us)) {
                        sc->add(ROOK_OPEN_FILE);
                        if (moves & bb_pc & FILES[FILE(sq)]) {
                            sc->add(CONNECTED_ROOKS);
                        }
                    } else {
                        sc->add(ROOK_SEMIOPEN_FILE);
                    }
                } else {
                    assert(pc == WQUEEN || pc == BQUEEN);
                    moves = magic::queen_moves(sq, occ);
                }
                U64 safe_moves = moves & pi->mob[us];
                sc->add(PST[pc][sq]);
                sc->add(MOBILITY[popcnt0(safe_moves)]);
                sc->add(ATTACKS[popcnt0(safe_moves & pi->attack[us])]);
                if (brd->is_attacked_by_pawn(sq, !us)) {
                    sc->add(ATTACKED[pc]);
                }
                int king_attacks = popcnt0(moves & KING_ZONE[kpos[!us]]);
                ka_units += (king_attacks > 0);
                ka_squares += king_attacks;
            } while (bb_pc);

            if ((pc == WBISHOP || pc == BBISHOP) && brd->has_bishop_pair(us)
                    && (pi->flags & pawn_table::FLAG_CLOSED_CENTER) == 0) {
                sc->add(VBISHOPPAIR);
            }
            s->stack->king_attack[pc] = KA_ENCODE(ka_units, ka_squares);
            result->add_us(sc, us);
        }
        return result;
    }
}
/*

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
    

}


score_t * eval_rooks(search_t * sd, bool us) {

static const U64 BACKRANKS[2] = {RANK_1 | RANK_2, RANK_7 | RANK_8};

if ((brd->bb[ROOK[us]] & RANK[us][1]) && (BIT(brd->get_sq(KING[us])) & (RANK[us][1] | RANK[us][2]))) {
    result->add(ROOK_1ST); //at least one rook is protecting the back rank
}

while (rooks) {
    int sq = pop(rooks);
    

    
    U64 bitSq = BIT(sq);
    if ((bitSq & RANK[us][7]) && (BIT(brd->get_sq(KING[them])) & BACKRANKS[us])) {
        result->add(ROOK_7TH);
    }

    
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
    }
    //Tarrasch Rule: place rook behind passers
    U64 tpass = moves & sd->stack->pt->passers; //touched passers
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



    

while (queens) {
    int sq = pop(queens);  
    result->add(10 - distance_rank(sq, kpos) - distance_file(sq, kpos));
    
}

}
 */ 