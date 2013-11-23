/* 
 * File:   evaluate.h
 * Author: Hajewiet
 *
 * Created on 13 mei 2011, 13:30
 */

#ifndef EVALUATE_H
#define	EVALUATE_H

#include "board.h"
#include <cstdlib>
#include <iostream>
#include "score.h"




class TSearch;

int evaluate(TSearch * searchData, int alpha, int beta);

void init_pct(TSCORE_PCT & pct);

bool skipExp(TSearch * sd);

enum EVALUATION_CONSTANTS {
    MAX_PIECES = 16,
    MAX_CLOSED_POSITION = 32, //maximum value indicating how much closed the position is
    GRAIN_SIZE = 3, //powers of 2, used for rounding evaluation score down (and hopefully get more cutoffs)
    GRAIN = 0xFFFFFFFF & ~((1 << GRAIN_SIZE) - 1)
};


const int MAX_EVALUATION_COMPONENTS = 16;

enum EVALUATION_COMPONENTS {
    SCORE_MATERIAL = 0,
    SCORE_PAWNS,
    SCORE_SHELTER_W,
    SCORE_SHELTER_B,
    SCORE_EXP,
    SCORE_ROOKS,
    SCORE_BISHOPS,
    SCORE_EVAL
};

/*******************************************************************************
 * Generic evaluation bonuses
 *******************************************************************************/

const short VPAWN = 100;
const short VKNIGHT = 315;
const short VBISHOP = 330;
const short VROOK = 500;
const short VQUEEN = 925;
const short VKING = 20000;

const TScore SVPAWN = S(100, 100);
const TScore SVKNIGHT = S(325, 325);
const TScore SVBISHOP = S(325, 325);
const TScore SVROOK = S(500, 500);
const TScore SVQUEEN = S(950, 950);
const TScore SVKING = S(20000, 20000);

const short PIECE_VALUE[13] = {
    0, VPAWN, VKNIGHT, VBISHOP, VROOK, VQUEEN, VKING,
    VPAWN, VKNIGHT, VBISHOP, VROOK, VQUEEN, VKING
};

const TScore PIECE_SCORE[13] = {
    S(0, 0), SVPAWN, SVKNIGHT, SVBISHOP, SVROOK, SVQUEEN, SVKING,
    SVPAWN, SVKNIGHT, SVBISHOP, SVROOK, SVQUEEN, SVKING
};

/*******************************************************************************
 * Material Evaluation Values 
 *******************************************************************************/

enum MaterialValues {
    MATERIAL_AHEAD_TRESHOLD = 80,
    VNOPAWNS = -40,
    VBISHOPPAIR = 50,
    DRAWISH_QR_ENDGAME = -20,
    DRAWISH_OPP_BISHOPS = -50
};

const TScore DEFENDED_PAWN[2] = {S(0, 4), S(4, 8)};
const TScore ISOLATED_PAWN[2] = {S(-10, -20), S(-20, -20)};
const TScore WEAK_PAWN[2] = {S(-8, -16), S(-16, -16)};
const TScore DOUBLED_PAWN = S(-4, -8);

const TScore PASSED_PAWN[64] = {
    S(0, 0), S(0, 0), S(0, 0), S(0, 0), S(0, 0), S(0, 0), S(0, 0), S(0, 0),
    S(60, 150), S(60, 150), S(60, 150), S(60, 150), S(60, 150), S(60, 150), S(60, 150), S(60, 150),
    S(40, 80), S(40, 80), S(40, 80), S(40, 80), S(40, 80), S(40, 80), S(40, 80), S(40, 80),
    S(30, 50), S(30, 50), S(30, 50), S(30, 50), S(30, 50), S(30, 50), S(30, 50), S(30, 50),
    S(20, 30), S(20, 30), S(20, 30), S(20, 30), S(20, 30), S(20, 30), S(20, 30), S(20, 30),
    S(20, 20), S(20, 20), S(20, 20), S(20, 20), S(20, 20), S(20, 20), S(20, 20), S(20, 20),
    S(20, 20), S(20, 20), S(20, 20), S(20, 20), S(20, 20), S(20, 20), S(20, 20), S(20, 20),
    S(0, 0), S(0, 0), S(0, 0), S(0, 0), S(0, 0), S(0, 0), S(0, 0), S(0, 0),
};

const TScore CONNECED_PASSED_PAWN[64] = {
    S(0, 0), S(0, 0), S(0, 0), S(0, 0), S(0, 0), S(0, 0), S(0, 0), S(0, 0),
    S(95, 95), S(95, 95), S(95, 95), S(95, 95), S(95, 95), S(95, 95), S(95, 95), S(95, 95),
    S(50, 50), S(50, 50), S(50, 50), S(50, 50), S(50, 50), S(50, 50), S(50, 50), S(50, 50),
    S(30, 30), S(30, 30), S(30, 30), S(30, 30), S(30, 30), S(30, 30), S(30, 30), S(30, 30),
    S(10, 10), S(10, 10), S(10, 10), S(10, 10), S(10, 10), S(10, 10), S(10, 10), S(10, 10),
    S(5, 5), S(5, 5), S(5, 5), S(5, 5), S(5, 5), S(5, 5), S(5, 5), S(5, 5),
    S(5, 5), S(5, 5), S(5, 5), S(5, 5), S(5, 5), S(5, 5), S(5, 5), S(5, 5),
    S(0, 0), S(0, 0), S(0, 0), S(0, 0), S(0, 0), S(0, 0), S(0, 0), S(0, 0),
};

const TScore CANDIDATE[64] = {
    S(0, 0), S(0, 0), S(0, 0), S(0, 0), S(0, 0), S(0, 0), S(0, 0), S(0, 0),
    S(0, 0), S(0, 0), S(0, 0), S(0, 0), S(0, 0), S(0, 0), S(0, 0), S(0, 0),
    S(32, 50), S(32, 50), S(32, 50), S(32, 50), S(32, 50), S(32, 50), S(32, 50), S(32, 50),
    S(24, 40), S(24, 40), S(24, 40), S(24, 40), S(24, 40), S(24, 40), S(24, 40), S(24, 40),
    S(18, 30), S(18, 30), S(18, 30), S(18, 30), S(18, 30), S(18, 30), S(18, 30), S(18, 30),
    S(10, 10), S(10, 10), S(10, 10), S(10, 10), S(10, 10), S(10, 10), S(10, 10), S(10, 10),
    S(10, 10), S(10, 10), S(10, 10), S(10, 10), S(10, 10), S(10, 10), S(10, 10), S(10, 10),
    S(0, 0), S(0, 0), S(0, 0), S(0, 0), S(0, 0), S(0, 0), S(0, 0), S(0, 0),
};

const TScore SHELTER_KPOS[64] = {
    S(-65, 0), S(-75, 0), S(-85, 0), S(-95, 0), S(-90, 0), S(-80, 0), S(-70, 0), S(-60, 0),
    S(-55, 0), S(-65, 0), S(-75, 0), S(-85, 0), S(-80, -0), S(-70, 0), S(-60, 0), S(-50, 0),
    S(-45, 0), S(-55, 0), S(-65, 0), S(-75, 0), S(-70, 0), S(-60, 0), S(-50, 0), S(-40, 0),
    S(-35, 0), S(-45, 0), S(-55, 0), S(-65, 0), S(-60, 0), S(-50, 0), S(-40, 0), S(-30, 0),
    S(-25, 0), S(-35, 0), S(-45, 0), S(-55, 0), S(-50, 0), S(-40, 0), S(-30, 0), S(-20, 0),
    S(-10, 0), S(-20, 0), S(-30, 0), S(-40, 0), S(-40, 0), S(-30, 0), S(-20, 0), S(-10, 0),
    S(15, 0), S(5, 0), S(-10, 0), S(-20, 0), S(-20, 0), S(-10, 0), S(10, 0), S(25, 0),
    S(40, 0), S(40, 0), S(25, 0), S(-10, 0), S(-10, 0), S(25, 0), S(50, 0), S(50, 0)

};

const TScore SHELTER_PAWN[64] = {
    S(0, 0), S(0, 0), S(0, 0), S(0, 0), S(0, 0), S(0, 0), S(0, 0), S(0, 0),
    S(2, 4), S(2, 4), S(2, 4), S(2, 4), S(2, 4), S(2, 4), S(2, 4), S(2, 4),
    S(2, 4), S(2, 4), S(2, 4), S(2, 4), S(2, 4), S(2, 4), S(2, 4), S(2, 4),
    S(2, 4), S(2, 4), S(2, 4), S(2, 4), S(2, 4), S(2, 4), S(2, 4), S(2, 4),
    S(5, 6), S(5, 6), S(5, 4), S(5, 4), S(5, 4), S(5, 4), S(15, 6), S(15, 6),
    S(15, 8), S(15, 8), S(10, 4), S(2, 4), S(2, 4), S(10, 4), S(15, 8), S(15, 8),
    S(30, 10), S(30, 10), S(25, 4), S(10, 4), S(5, 4), S(15, 4), S(30, 10), S(30, 10),
    S(0, 0), S(0, 0), S(0, 0), S(0, 0), S(0, 0), S(0, 0), S(0, 0), S(0, 0),
};

const TScore STORM_PAWN[64] = {
    S(0, 0), S(0, 0), S(0, 0), S(0, 0), S(0, 0), S(0, 0), S(0, 0), S(0, 0),
    S(0, 2), S(0, 2), S(0, 2), S(0, 2), S(0, 2), S(0, 2), S(0, 2), S(0, 2),
    S(0, 2), S(0, 2), S(0, 2), S(0, 2), S(0, 2), S(0, 2), S(0, 2), S(0, 2),
    S(2, 4), S(2, 4), S(2, 4), S(2, 4), S(2, 4), S(2, 4), S(2, 4), S(2, 4),
    S(10, 4), S(10, 4), S(10, 4), S(10, 4), S(10, 4), S(10, 4), S(10, 4), S(10, 4),
    S(20, 10), S(20, 10), S(20, 10), S(20, 10), S(20, 10), S(20, 10), S(20, 10), S(20, 10),
    S(20, 10), S(20, 10), S(20, 10), S(20, 10), S(20, 10), S(20, 10), S(20, 10), S(20, 10),
    S(0, 0), S(0, 0), S(0, 0), S(0, 0), S(0, 0), S(0, 0), S(0, 0), S(0, 0),
};


const TScore SHELTER_OPEN_FILES[4] = {
    S(0, 0), S(-30, -5), S(-60, -10), S(-120, -15)
};

const TScore SHELTER_OPEN_ATTACK_FILES[4] = {
    S(0, 0), S(-10, 0), S(-20, -5), S(-40, -10)
};

const TScore SHELTER_OPEN_EDGE_FILE = S(-110, -10);

const TScore SHELTER_CASTLING_KINGSIDE = S(60, 20);

const TScore SHELTER_CASTLING_QUEENSIDE = S(50, 20);

const TScore ROOK_MOBILITY[15] = {
    S(-40, -40), S(-30, -30), S(-20, -20), S(0, -10),
    S(2, 0), S(4, 4), S(6, 6), S(8, 8), S(10, 10),
    S(12, 12), S(14, 14), S(16, 16), S(18, 18), S(20, 20), S(22, 22)
};

const TScore ROOK_SEMIOPEN_FILE = S(12, 12);
const TScore ROOK_OPEN_FILE = S(24, 24);
const TScore ROOK_SHELTER_PROTECT = S(10, 0);
const TScore ROOK_TARRASCH_SUPPORT = S(10, 30);
const TScore ROOK_TARRASCH_ATTACK = S(10, 20);
const TScore ROOK_WRONG_TARRASCH_SUPPORT = S(-10, -20);
const TScore ROOK_WRONG_TARRASCH_ATTACK = S(-10, -20);

const TScore BISHOP_MOBILITY[15] = {
    S(-40, -50), S(-20, -30), S(-10, -20), S(0, -10),
    S(2, 0), S(4, 4), S(6, 6), S(8, 8), S(10, 10),
    S(12, 12), S(14, 14), S(16, 16), S(18, 18), S(20, 20), S(22, 22)
};

const TScore TRAPPED_BISHOP(-80, -120);

/*******************************************************************************
 * Helper functions
 *******************************************************************************/

inline int cond(bool whiteValue, short value) {
    return whiteValue ? value : -value;
}

inline int cond(bool whiteCondition, bool blackCondition, short value) {
    if (whiteCondition != blackCondition) {
        return whiteCondition ? value : -value;
    }
    return 0;
}

inline int cond(bool whiteCondition, bool blackCondition, const short values[], unsigned char indexW, unsigned char indexB) {
    if (whiteCondition != blackCondition) {
        return whiteCondition ? values[indexW] : -values[indexB];
    }
    return 0;
}

inline int factor(char wFactor, char bFactor, const short values[], unsigned char indexW, unsigned char indexB) {
    return wFactor * values[indexW] - bFactor * values[indexB];
}

inline int factor(char wFactor, char bFactor, const short values[], unsigned char index) {
    return factor(wFactor, bFactor, values, index, index);
}

inline int phasedScore(int gameProgress, int middleGameValue, int endGameValue) {
    assert(gameProgress >= 0 && gameProgress <= MAX_GAMEPHASES);
    return (middleGameValue * (MAX_GAMEPHASES - gameProgress) + endGameValue * gameProgress) >> GAMEPHASE_SCORE_BSR;
}

#endif	/* EVALUATE_H */

