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
 * File: score.h
 * Score structure to hold two scores, one for the middle game and one for the end game
 * A single score value is interpolated by the mg and eg values using the game phase.
 * In maxima, the game phase ranges from 0 (opening with all pieces still on the board) 
 * to 16 (an endgame with pawns and kings)
 * This idea is based on similar principles found in the Fruit engine by Fabien Letouzey.
 *
 * Created on 21 september 2013, 23:09
 */

#ifndef SCORE_H
#define	SCORE_H

namespace score {

    enum constants {
        MAX_PHASE = 16,
        INF = 32000,
        ILLEGAL = 32001,
        INVALID = 32002,
        MATE = 30000,
        DEEPEST_MATE = 29500,
        WIN = 5000,
        DRAW = 0,
        LOWERBOUND = 1,
        UPPERBOUND = 2,
        EXACT = 3
    };

    /**
     * Returns distance to mate
     * @param score 
     * @return ply distance 
     */
    inline int mate_in_ply(int score) {
        return score > DEEPEST_MATE ? MATE - score : 0;
    }

    /**
     * Returns distance to getting mated
     * @param score
     * @return ply distance
     */
    inline int mated_in_ply(int score) {
        return score < -DEEPEST_MATE ? MATE + score : 0;
    }

    /**
     * Test if score is a mate score
     * @param score score to test
     * @return true if mate(d), false otherwise
     */
    inline bool is_mate(int score) {
        return score < -DEEPEST_MATE || score > DEEPEST_MATE;
    }

    /**
     * Returns if a score is a lower bound, upper bound or exact
     * @param score score to test
     * @return bound flag
     */
    inline int flags(int score, int alpha, int beta) {
        return score >= beta ? LOWERBOUND : (score <= alpha ? UPPERBOUND : EXACT);
    }
}

struct score_t {
    int16_t mg; //middle game value
    int16_t eg; //end game value

    score_t() : mg(0), eg(0) {
    }

    score_t(const score_t & s) {
        mg = s.mg;
        eg = s.eg;
    }

    score_t(short x, short y) {
        mg = x;
        eg = y;
    }

    score_t(short x) {
        mg = x;
        eg = x;
    }

    void print() {
        std::cout << "(" << mg << ", " << eg << ") ";
    }

    void print(int phase) {
        print();
        std::cout << "-> " << get(phase);
    }

    void print(std::string txt) {
        std::cout << txt << ": (" << mg << ", " << eg << ") ";
    }

    void add(const score_t * s) {
        mg += s->mg;
        eg += s->eg;
    }

    short get(short phase) {
        assert(phase >= 0 && phase <= score::MAX_PHASE);
        return (mg * (score::MAX_PHASE - phase) + eg * phase) / score::MAX_PHASE;
    }

    void set(const score_t & s) {
        mg = s.mg;
        eg = s.eg;
    }

    void set(const score_t * s) {
        mg = s->mg;
        eg = s->eg;
    }

    void nset(const score_t & s) {
        mg = -s.mg;
        eg = -s.eg;
    }

    void set(const short x, const short y) {
        mg = x;
        eg = y;
    }

    void add(const short x, const short y) {
        mg += x;
        eg += y;
    }

    void add(const score_t & s) {
        mg += s.mg;
        eg += s.eg;
    }

    void sub(const score_t & s) {
        mg -= s.mg;
        eg -= s.eg;
    }

    void sub(const score_t * s) {
        mg -= s->mg;
        eg -= s->eg;
    }

    void sub(const short x, const short y) {
        mg -= x;
        eg -= y;
    }

    void mul(const double x) {
        mg *= x;
        eg *= x;
    }

    void mul(const double & x, const double & y) {
        mg *= x;
        eg *= y;
    }
    
    void mul256(const short x) {
        mg = int(mg * x) / 256;
        eg = int(eg * x) / 256;
    }
    
    void mul256(const short x, const short y) {
        mg = (mg * x) / 256;
        eg = (eg * y) / 256;
    }

    void round() {
        mg = (2 * mg) / 2;
        eg = (2 * eg) / 2;
    }

    void max(const score_t & s) {
        mg = mg >= s.mg ? mg : s.mg;
        eg = eg >= s.eg ? eg : s.eg;
    }

    void min(const score_t & s) {
        mg = mg <= s.mg ? mg : s.mg;
        eg = eg <= s.eg ? eg : s.eg;
    }

    void clear() {
        mg = 0;
        eg = 0;
    }

    void half() {
        mg = mg / 2;
        eg = eg / 2;
    }

    bool valid() {
        return mg != score::INVALID && eg != score::INVALID;
    }

};

typedef score_t pst_t[7][64];

#define S(x,y) score_t(x,y)

#define MUL256(x,y) (((x)*(y))/256)

#define PRINT_SCORE(s) "(" << (int)(s).mg << ", " << (int)(s).eg << ") "

#endif	/* SCORE_H */

