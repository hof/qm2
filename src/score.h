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

#define MATE_IN_PLY(s) ((s)>SCORE_MATE-MAX_PLY? SCORE_MATE-s : 0)
#define MATED_IN_PLY(s) ((s)<-SCORE_MATE+MAX_PLY? SCORE_MATE+s : 0)

struct TScore {
    short mg; //middle game value
    short eg; //end game value

    TScore(): mg(0), eg(0) { }
    TScore(const TScore & s) { mg = s.mg; eg = s.eg; }
    TScore(short x, short y) { mg = x; eg = y; }
    TScore(short x) { mg = x; eg = x; }
    
    void print () {
        std::cout << "(" << mg << ", " << eg << ") ";
    }
    
    void print(int phase) {
        print();
        std::cout << "-> " << get(phase);
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
    
    inline void add(const TScore * s) {
        mg += s->mg;
        eg += s->eg;
    }

    inline short get(short phase) {
        assert(phase >= 0 && phase <= MAX_GAMEPHASES);
        return (mg * (MAX_GAMEPHASES - phase) + eg * phase) >> GAMEPHASE_SCORE_BSR;
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
        mg = mg >= s.mg? mg : s.mg;
        eg = eg >= s.eg? eg : s.eg;
    }
    
    inline void min(const TScore & s) {
        mg = mg <= s.mg? mg : s.mg;
        eg = eg <= s.eg? eg : s.eg;
    }

    inline void clear() {
        mg = 0;
        eg = 0;
    }
    
    inline void half() {
        mg >>= 1;
        eg >>= 1;
    }

    inline bool valid() {
        return mg != SCORE_INVALID && eg != SCORE_INVALID;
    }

};

typedef TScore TSCORE_PCT[13][64];
typedef TScore TSCORE_PST[7][64];

#define S(x,y) TScore(x,y)

#define PHASED_SHORT(mg,eg,phase) (((mg) * (MAX_GAMEPHASES - phase) + (eg) * (phase)) >> GAMEPHASE_SCORE_BSR)
#define PHASED_SCORE(s,phase) (((s.mg) * (MAX_GAMEPHASES - phase) + (s.eg) * (phase)) >> GAMEPHASE_SCORE_BSR)

#define PRINT_SCORE(s) "(" << (int)(s).mg << ", " << (int)(s).eg << ") "

#endif	/* SCORE_H */

