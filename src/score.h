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

#include "defs.h"

#define MAX_GAMEPHASES 16 //use grain size of 16 gamephases

enum SCORE_CONSTANTS {
    SCORE_INFINITE = 32000,
    SCORE_ILLEGAL = 32001,
    SCORE_INVALID = 32002,
    SCORE_MATE = 30000,
    SCORE_DRAW = 0
};

#define MATE_IN_PLY(s) ((s)>SCORE_MATE-MAX_PLY? SCORE_MATE-s : 0)
#define MATED_IN_PLY(s) ((s)<-SCORE_MATE+MAX_PLY? SCORE_MATE+s : 0)

struct TScore {
    int16_t mg; //middle game value
    int16_t eg; //end game value

    TScore() : mg(0), eg(0) {
    }

    TScore(const TScore & s) {
        mg = s.mg;
        eg = s.eg;
    }

    TScore(short x, short y) {
        mg = x;
        eg = y;
    }

    TScore(short x) {
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

    inline void add(const TScore * s) {
        mg += s->mg;
        eg += s->eg;
    }

    inline short get(short phase) {
        assert(phase >= 0 && phase <= MAX_GAMEPHASES);
        return (mg * (MAX_GAMEPHASES - phase) + eg * phase) / MAX_GAMEPHASES;
    }

    inline void set(const TScore & s) {
        mg = s.mg;
        eg = s.eg;
    }

    inline void set(const TScore * s) {
        mg = s->mg;
        eg = s->eg;
    }

    inline void nset(const TScore & s) {
        mg = -s.mg;
        eg = -s.eg;
    }

    inline void set(const short x, const short y) {
        mg = x;
        eg = y;
    }

    inline void add(const short x, const short y) {
        mg += x;
        eg += y;
    }

    inline void add(const TScore & s) {
        mg += s.mg;
        eg += s.eg;
    }

    inline void sub(const TScore & s) {
        mg -= s.mg;
        eg -= s.eg;
    }

    inline void sub(const TScore * s) {
        mg -= s->mg;
        eg -= s->eg;
    }

    inline void sub(const short x, const short y) {
        mg -= x;
        eg -= y;
    }

    inline void mul(const double x) {
        mg *= x;
        eg *= x;
    }

    inline void mul(const double & x, const double & y) {
        mg *= x;
        eg *= y;
    }

    inline void round() {
        static const short GRAIN = 0xFFFF & ~((1 << 1) - 1);
        mg &= GRAIN;
        eg &= GRAIN;
    }

    inline void max(const TScore & s) {
        mg = mg >= s.mg ? mg : s.mg;
        eg = eg >= s.eg ? eg : s.eg;
    }

    inline void min(const TScore & s) {
        mg = mg <= s.mg ? mg : s.mg;
        eg = eg <= s.eg ? eg : s.eg;
    }

    inline void clear() {
        mg = 0;
        eg = 0;
    }

    inline void half() {
        mg = mg / 2;;
        eg  = eg /2;
    }

    inline bool valid() {
        return mg != SCORE_INVALID && eg != SCORE_INVALID;
    }

};

typedef TScore TSCORE_PST[7][64];

#define S(x,y) TScore(x,y)

#define MUL256(x,y) (((x)*(y))/256)

#define PRINT_SCORE(s) "(" << (int)(s).mg << ", " << (int)(s).eg << ") "

#endif	/* SCORE_H */

