/* 
 * File:   score.h
 * Author: Hajewiet
 *
 * Created on 21 september 2013, 23:09
 */

#ifndef SCORE_H
#define	SCORE_H

#include "defs.h"


#define MAX_GAMEPHASES 16 //use grain size of 16 gamephases
#define GAMEPHASE_BSR 2 //divide by 4 (right shift by 2) (64/4=16)
#define GAMEPHASE_SCORE_BSR 4 //divide by 16 (right shift by 4)
#define GAME_PHASES 2 //middlegame and endgame
#define PHASE_OPENING 0 //two phases are used: middlegame and endgame
#define PHASE_MIDDLEGAME 0
#define PHASE_ENDGAME 1

enum SCORE_CONSTANTS {
    SCORE_INFINITE = 32000,
    SCORE_ILLEGAL = 32001,
    SCORE_INVALID = 32002,
    SCORE_MATE = 30000,
    SCORE_DRAW = 0
};

typedef const short TSCORE_TABLE_64[GAME_PHASES][64];
typedef const short TSCORE_TABLE_4[GAME_PHASES][4];
typedef const short TSCORE_TABLE[GAME_PHASES];

struct TScore {
    short mg; //middle game value
    short eg; //end game value

    TScore() {
        clear();
    }

    inline TScore& operator=(const TScore & s) {
        mg = s.mg;
        eg = s.eg;
        return *this;
    }

    inline TScore& operator=(const short x) {
        mg = x;
        eg = x;
        return *this;
    }

    inline TScore& operator+(const TScore & s) {
        mg += s.mg;
        eg += s.eg;
        return *this;
    }
    
    inline TScore& operator+=(const TScore & s) {
        mg += s.mg;
        eg += s.eg;
        return *this;
    }

    inline TScore& operator+(const short x) {
        mg += x;
        eg += x;
        return *this;
    }

    inline TScore& operator+=(const short x) {
        mg += x;
        eg += x;
        return *this;
    }
    
    inline TScore& operator-(const TScore & s) {
        mg -= s.mg;
        eg -= s.eg;
        return *this;
    }

    inline TScore& operator-(const short x) {
        mg -= x;
        eg -= x;
        return *this;
    }

    inline TScore& operator-=(const short x) {
        mg -= x;
        eg -= x;
        return *this;
    }

    inline TScore& operator*(const TScore & s) {
        mg *= s.mg;
        eg *= s.eg;
        return *this;
    }

    inline TScore& operator*(const short x) {
        mg *= x;
        eg *= x;
        return *this;
    }

    inline TScore& operator*=(const short x) {
        mg *= x;
        eg *= x;
        return *this;
    }

    inline TScore& operator*(const double x) {
        mg *= x;
        eg *= x;
        return *this;
    }

    inline TScore& operator*=(const double x) {
        mg *= x;
        eg *= x;
        return *this;
    }
    
    inline TScore& operator/(const short x) {
        assert(x!=0);
        mg = mg/x;
        eg = eg/x;
        return *this;
    }

    inline void add_ix64(TSCORE_TABLE_64 * table, char ix) {
        assert(ix >= 0 && ix < 64);
        mg += (*table)[0][ix];
        eg += (*table)[1][ix];
    }

    inline void add_ix4(TSCORE_TABLE_4 * table, char ix) {
        assert(ix >= 0 && ix < 4);
        mg += (*table)[0][ix];
        eg += (*table)[1][ix];
    }

    inline void add(TSCORE_TABLE * table) {
        mg += (*table)[0];
        eg += (*table)[1];
    }

    inline int get(short phase) {
        assert(phase >= 0 && phase <= MAX_GAMEPHASES);
        return (mg * (MAX_GAMEPHASES - phase) + eg * phase) >> GAMEPHASE_SCORE_BSR;
    }

    inline void clear() {
        mg = 0;
        eg = 0;
    }

    inline bool valid() {
        return mg != SCORE_INVALID && eg != SCORE_INVALID;
    }


};


#endif	/* SCORE_H */

