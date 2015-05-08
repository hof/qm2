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
 * File: endgame.cpp
 * Endgame evaluation functions for normal chess
 */

#include "eval_endgame.h"
#include "eval_material.h"
#include "eval.h"
#include "search.h"
#include "bits.h"
#include "score.h"
#include "eval_kpk_bb.h"

namespace eg {

    const int BONUS[2] = {-10, 10};
    const int EDGE_DISTANCE[8] = {0, 2, 3, 4, 4, 3, 2, 0};

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
        return us == WHITE ? s->stack->mt->score >= 450 :
                s->stack->mt->score <= -450;
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
        int result = 100 - 20 * king_dist; //bring the king nearby
        bool us = !them;
        if (is_1(pos->bb[BISHOP[us]]) && pos->bb[ROOK[us]] == 0 && pos->bb[QUEEN[us]] == 0) {
            int corner_dist = 0;
            if ((pos->bb[BISHOP[us]] & WHITE_SQUARES) != 0) {
                corner_dist = MIN(distance(kpos[them], a8), distance(kpos[them], h1));
            } else {
                corner_dist = MIN(distance(kpos[them], a1), distance(kpos[them], h8));
            }
            result += 250 - 50 * corner_dist; //1: to the correct corner
            result += 100 - 20 * edge_dist; //2: to the edge 
        } else {
            result += 250 - 50 * edge_dist; //1: to the edge
            result += 100 - 20 * (r_dist + f_dist); //2: to any corner
        }
        return us == WHITE ? result / div : -result / div;
    }

    int piece_distance(search_t * s, const bool us) {
        assert(gt_1(s->brd.all(us)));
        int fsq = bsf(s->brd.all(us));
        int rsq = bsr(s->brd.all(us));
        assert(fsq != rsq);
        return distance(fsq, rsq);
    }

    /**
     * Returns the amount of steps to promotion of our most advanced pawn (not
     * necessarily a passed pawn). 
     * @param s search object
     * @param us side to investigate: white (1) or black (0)
     * @return 0 (false) or the amount of steps
     */
    int most_advanced_pawn_steps(search_t * s, const bool us) {
        int psq = us == WHITE ? bsr(s->brd.bb[WPAWN]) : bsf(s->brd.bb[BPAWN]);
        int steps = us == WHITE ? 7 - RANK(psq) : RANK(psq);
        bool is_passed = s->stack->pt->passers & BIT(psq);
        bool utm = s->brd.us() == us;
        steps += !is_passed + !utm;
        return steps;
    }

    /**
     * Returns the amount of steps to promotion of our most advanced passed pawn
     * @param s search object
     * @param us side to investigate: white (1) or black (0)
     * @return 0 (false) or the amount of steps
     */
    int most_advanced_passer_steps(search_t * s, const bool us) {
        U64 passers = s->stack->pt->passers & s->brd.bb[PAWN[us]];
        if (passers == 0) {
            return 0;
        }
        int psq = us == WHITE ? bsr(passers) : bsf(passers);
        int steps = us == WHITE ? 7 - RANK(psq) : RANK(psq);
        bool utm = s->brd.us() == us;
        steps += !utm;
        return steps;
    }

    /**
     * Detects "unstoppable" passers. A passer is unstoppable
     * if it's not blocked and the promotion can't be prevented
     * @param s search object
     * @param us side to investigate: white (1) or black (0)
     * @return 0 (false) or the amount of steps to promotion of our best unstoppable passer
     */
    int unstoppable_pawn_steps(search_t * s, const bool us) {
        U64 passers = s->stack->pt->passers & s->brd.bb[PAWN[us]];
        if (passers == 0) {
            return 0;
        }
        bool them = !us;
        assert(s->brd.has_pieces(them) == false);
        bool utm = s->brd.us() == us;
        int kpos_them = s->brd.get_sq(KING[them]);
        int kpos_us = s->brd.get_sq(KING[us]);
        int result = 0;
        int best = 10;
        U64 attacks_us = s->brd.pawn_attacks(us) | KING_MOVES[kpos_us];
        while (passers && best > 1) {
            int psq = pop(passers);
            int steps_pawn = us == WHITE ? 7 - RANK(psq) : RANK(psq);

            //continue if this passed pawn can't beat the best score
            if (steps_pawn >= best) {
                continue;
            }

            //case 1: path is fully defended
            U64 path = fill_up(BIT(psq), us) ^ BIT(psq);
            bool unstoppable = (path & attacks_us) == path;

            //case 2: path is free and their king is too far away to stop us
            if (!unstoppable && (path & s->brd.bb[ALLPIECES]) == 0) {
                int steps_them = distance(kpos_them, queening_square(psq, us)) - !utm;
                unstoppable = steps_them > steps_pawn;
            }

            //update best and result score
            if (unstoppable) {
                best = steps_pawn;
                result = best;
            }
        };
        return result;
    }

    bool eg_test(search_t * s, bool pawns_us, bool pcs_us, bool pawns_them, bool pcs_them, bool us) {
        const bool them = !us;

        return pawns_us == bool(s->brd.bb[PAWN[us]])
                && pawns_them == bool(s->brd.bb[PAWN[them]])
                && pcs_us == s->brd.has_pieces(us)
                && pcs_them == s->brd.has_pieces(them)
                && (s->stack->mt->phase == 16) == (!pcs_us && !pcs_them);
    }

    int knpk(search_t * s, const int score, const bool us) {
        const bool them = !us;
        if (s->brd.bb[PAWN[us]] & EDGE & RANK[us][7]) {
            int psq = s->brd.get_sq(PAWN[us]);
            if (distance(queening_square(psq, us), s->brd.get_sq(KING[them])) <= 1) {

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
        static const int PFMUL[9] = {1, 16, 32, 64, 128, 160, 192, 224, 240};
        return mul256(score, PFMUL[s->brd.count(PAWN[us])]);
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
        const bool utm = s->brd.us() == us;
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
     * Evaluate KRPKR endgame
     */
    int krpkr(search_t * s, int score, bool us) {
        assert(s->brd.is_eg(KRPKR, us));
        const bool them = !us;
        const bool utm = s->brd.us() == us;
        const int psq = s->brd.get_sq(PAWN[us]);
        const int ksq_us = s->brd.get_sq(KING[us]);
        const int rsq_tm = s->brd.get_sq(ROOK[them]);
        const int ksq_tm = s->brd.get_sq(KING[them]);
        const int qsq = queening_square(psq, us);
        int pr = rank(psq, us);
        int kr_us = rank(ksq_us, us);
        int rr_tm = rank(rsq_tm, us);
        int dq_tm = distance(ksq_tm, qsq);
        int pr_edge = 6 - utm;

        //Philidor's position is a draw (3rd-rank defense)
        if (dq_tm <= 1 && rr_tm == 5 && kr_us < 5 && pr < pr_edge) {
            return draw(score, 8);
        }

        //Philidor's after advancing the pawn to 6th / 7th rank (endless checks)
        if (dq_tm <= 1 && pr >= 5 && rr_tm <= 1) {
            return draw(score, 16);
        }

        //Work towards building a bridge (Lucena's position)
        const int rsq_us = s->brd.get_sq(ROOK[us]);
        const int dq_us = distance(ksq_us, qsq);
        const int kf_us = FILE(ksq_us);
        const int rf_us = FILE(rsq_us);
        const int pf = FILE(psq);
        int bonus = dq_tm - dq_us;
        bonus += rank(rsq_us, us) == 3;
        bonus += kf_us == pf && rf_us == pf;
        bonus += rr_tm != 7;
        bonus += pf > 0 && pf < 7;
        score += bonus * BONUS[us];
        int steps = 7 - pr - utm;
        return draw(score, steps);
    }

    /**
     * Evaluate KQKP endgame
     */
    int kqkp(search_t * s, const int score, const bool us) {
        assert(s->brd.is_eg(KQKP, us));

        //it's a win if the pawn is not near promotion
        const bool them = !us;
        const bool utm = s->brd.us() == us;
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

        const bool them = !us;

        //KPK -> bitbase lookup
        if (max_1(s->brd.bb[PAWN[us]])) {
            const bool utm = s->brd.us() == us;
            bool won = KPK::probe(utm, s->brd.get_sq(KING[us]), s->brd.get_sq(KING[them]),
                    s->brd.get_sq(PAWN[us]), us == BLACK);
            if (won) {
                return score + win(us, 2);
            }
            return draw(score, 64);
        }

        //KPSK -> case 1: any unstoppable pawn wins the game
        int steps = unstoppable_pawn_steps(s, us);
        if (steps > 0) {
            return score + win(us, (1 + steps));
        }

        //KPSK -> case 2: any pawn defending another pawn wins the game
        if (s->brd.pawn_attacks(us) & s->brd.bb[PAWN[us]]) {
            return score + win(us, 8);
        }
        return score;
    }

    /**
     * Evaluate endgames with only kings and pawns (case 3)
     */
    const uint8_t UNSTOPPABLE_PAWN[8] = {0, 200, 150, 100, 50, 25, 25, 25};
    const uint8_t BEST_PASSER[8] = {0, 80, 60, 40, 20, 0, 0, 0};
    const int16_t UNSTOPPABLE_BONUS[2] = {-500, 500};

    int pawns_vs_pawns(search_t * s, int score, const bool us) {
        assert(eg_test(s, 1, 0, 1, 0, us));
        bool them = !us;
        int up_us = unstoppable_pawn_steps(s, us);
        int up_tm = unstoppable_pawn_steps(s, them);
        int forw_us = most_advanced_pawn_steps(s, us);
        int forw_tm = most_advanced_pawn_steps(s, them);
        assert(up_us < 8 && up_tm < 8 && up_us >= 0 && up_tm >= 0);

        //case 1: unstoppable and at least two tempos sooner to promote
        if (up_us && !up_tm && up_us < forw_tm + 2) {
            return score + UNSTOPPABLE_BONUS[us];
        }

        //case 2: reversed case 1 (they promote much sooner)
        if (up_tm && !up_us && up_tm < forw_us + 2) {
            return score - UNSTOPPABLE_BONUS[us];
        }

        //case 3: all other cases are fuzzy (?) - just reward unstoppable and most advanced passers
        int pass_us = most_advanced_passer_steps(s, us);
        int pass_tm = most_advanced_passer_steps(s, them);
        assert(pass_us < 8 && pass_tm < 8 && pass_us >= 0 && pass_tm >= 0);
        int bonus = UNSTOPPABLE_PAWN[up_us] - UNSTOPPABLE_PAWN[up_tm];
        bonus += BEST_PASSER[pass_us] - BEST_PASSER[pass_tm];
        score += us == WHITE ? bonus : -bonus;
        return score;
    }

    /**
     * Evaluate KQPSKQ endgame
     */

    int kqpskq(search_t * s, const int score, const bool us) {
        int bonus = s->stack->passer_score[us].eg / 2;
        bonus = us == WHITE ? bonus : -bonus;
        return mul256(score, 112 + 16 * s->brd.count(PAWN[us])) + bonus;
    }

    /**
     * Piece(s) vs lone king (case 4)
     */
    int pcs_vs_king(search_t * s, const int score, const bool us) {
        assert(eg_test(s, 0, 1, 0, 0, us));
        const bool them = !us;
        if (material::has_mating_power(s, us)) { //e.g. KBNK and better
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
        if (material::has_mating_power(s, us)) {
            return score + win(us) + corner_king(s, them);
        }
        int steps = unstoppable_pawn_steps(s, us);
        if (steps > 0) { //unstoppable passed pawn
            return score + win(us, (3 + steps));
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
        const bool pow_us = material::has_mating_power(s, us);
        if (!pow_us) {
            return draw(score, 0);
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

        //this endgame seems very favorable: give some extra bonus
        int bonus = 20;
        if (material::has_mating_power(s, us)) {
            bonus += 20;
        }
        if (us != WHITE) {
            bonus = -bonus;
        }

        return score + bonus;
    }

    /**
     * Pawn(s) vs piece(s) (case 9) 
     */
    int pawns_vs_pcs(search_t * s, const int score, const bool us) {
        assert(eg_test(s, 1, 0, 0, 1, us));

        if (material::has_mating_power(s, !us)) {
            return us == WHITE ? score - 20 : score + 20;
        }
        
        return us == WHITE? score + 10 : score - 10;
    }

    /**
     * Pawn(s) vs piece(s) (case 11)
     */
    int pawns_vs_pcs_n_pawns(search_t * s, const int score, const bool us) {
        assert(eg_test(s, 1, 0, 1, 1, us));
     
        if (material::has_mating_power(s, !us)) {
            return us == WHITE ? score - 20 : score + 20;
        }
        
        return score;
    }

    /**
     * Evaluate cases where both sides have pieces, but no pawns (case 12)
     */
    int pcs_vs_pcs(search_t * s, const int score, const bool us) {
        assert(eg_test(s, 0, 1, 0, 1, us));
        const bool them = !us;
        if (!material::has_mating_power(s, us)) { //no mating power -> draw 
            return draw(score, 16);
        } else if (s->brd.is_eg(KBBKN, us)) { //KBBKN is an exception
            return score + win(us, 2) + corner_king(s, them, 2) + 20 * piece_distance(s, them);
        } else if (!has_winning_edge(s, us)) { //no winning edge -> draw
            return draw(score, 16) + corner_king(s, them, 16);
        } else if (material::has_mating_power(s, them)) { //win 
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
        const bool pow_us = material::has_mating_power(s, us);
        const bool pow_them = material::has_mating_power(s, them);
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
            return draw(score, 4);
        } else if (s->brd.is_eg(KRPKR, us)) {
            return krpkr(s, score, us);
        } else if (s->brd.is_eg(KQPSKQ, us)) {
            return kqpskq(s, score, us);
        }
        return score;
    }

    /**
     * Piece(s) vs piece(s) and pawn(s) (case 14)
     */
    int pcs_vs_pcs_n_pawns(search_t * s, const int score, const bool us) {
        assert(eg_test(s, 0, 1, 1, 1, us));
        const bool pow_us = material::has_mating_power(s, us);
        const bool win_us = has_winning_edge(s, us);
        const bool them = !us;
        if (!pow_us && !win_us) {
            return draw(score, 128);
        } else if (!win_us) {
            if (!material::has_imbalance(s, us)) {
                return draw(score, 64) + corner_king(s, them, 16);
            } else if (!material::has_major_imbalance(s)) {
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
        } else if (s->brd.is_eg(KQPSKQPS, us)) {
            return mul256(score, 128 + 16 * s->brd.count(PAWN[us]));
            ;
        }
        return score;
    }

    /**
     * Main endgame evaluation function
     */
    int eval(search_t * s, const int score) {
        const bool us = (score > 0) || (score == 0 && s->brd.us()); //winning side
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
