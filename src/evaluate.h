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

const TScore SVPAWN = S(95, 110);
const TScore SVKNIGHT = S(315, 300);
const TScore SVBISHOP = S(330, 320);
const TScore SVROOK = S(465, 500);
const TScore SVQUEEN = S(925, 875);
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
    VNOPAWNS = -80,
    VBISHOPPAIR = 25,
    DRAWISH_QR_ENDGAME = -20,
    DRAWISH_OPP_BISHOPS = -50
};

const short TRADEDOWN_PIECES[MAX_PIECES + 1] = {
    40, 30, 20, 10, 0, -10, -15, -20, -25, -30, -35, -40, -45, -50, -55, -60, -64
};

const short TRADEDOWN_PAWNS[9] = {
    -20, -15, -10, -5, 0, 2, 4, 6, 6
};

const short PIECEPOWER_AHEAD[] = {//in amount of pawns
    15, 25, 35, 45, 60, 75, 90, 105, 120, 135, 150, 165, 190, 205, 220, 235, 250,
    250, 250, 250, 250, 250, 250, 250, 250, 250, 250, 250, 250, 250, 250, 250, 250, 250
};

/******************************************
 * Pawns
 *****************************************/

const TScore ISOLATED_OPEN_PAWN[64] = {
    S(0, 0), S(0, 0), S(0, 0), S(0, 0), S(0, 0), S(0, 0), S(0, 0), S(0, 0),
    S(0, 0), S(0, 0), S(0, 0), S(0, 0), S(0, 0), S(0, 0), S(0, 0), S(0, 0),
    S(-12, -24), S(-14, -26), S(-16, -28), S(-18, -30), S(-18, -30), S(-16, -28), S(-14, -26), S(-12, -24),
    S(-12, -24), S(-14, -26), S(-16, -28), S(-18, -30), S(-18, -30), S(-16, -28), S(-14, -26), S(-12, -24),
    S(-12, -24), S(-14, -26), S(-16, -28), S(-18, -30), S(-18, -30), S(-16, -28), S(-14, -26), S(-12, -24),
    S(-12, -24), S(-14, -26), S(-16, -28), S(-18, -30), S(-18, -30), S(-16, -28), S(-14, -26), S(-12, -24),
    S(-12, -24), S(-14, -26), S(-16, -28), S(-18, -30), S(-18, -30), S(-16, -28), S(-14, -26), S(-12, -24),
    S(0, 0), S(0, 0), S(0, 0), S(0, 0), S(0, 0), S(0, 0), S(0, 0), S(0, 0)
};

const TScore ISOLATED_PAWN[64] = {
    S(0, 0), S(0, 0), S(0, 0), S(0, 0), S(0, 0), S(0, 0), S(0, 0), S(0, 0),
    S(0, 0), S(0, 0), S(0, 0), S(0, 0), S(0, 0), S(0, 0), S(0, 0), S(0, 0),
    S(-8, -20), S(-10, -22), S(-12, -24), S(-14, -26), S(-14, -26), S(-12, -24), S(-10, -22), S(-8, -20),
    S(-8, -20), S(-10, -22), S(-12, -24), S(-14, -26), S(-14, -26), S(-12, -24), S(-10, -22), S(-8, -20),
    S(-8, -20), S(-10, -22), S(-12, -24), S(-14, -26), S(-14, -26), S(-12, -24), S(-10, -22), S(-8, -20),
    S(-8, -20), S(-10, -22), S(-12, -24), S(-14, -26), S(-14, -26), S(-12, -24), S(-10, -22), S(-8, -20),
    S(-8, -20), S(-10, -22), S(-12, -24), S(-14, -26), S(-14, -26), S(-12, -24), S(-10, -22), S(-8, -20),
    S(0, 0), S(0, 0), S(0, 0), S(0, 0), S(0, 0), S(0, 0), S(0, 0), S(0, 0)
};


const TScore DOUBLED_PAWN[64] = {
    S(0, 0), S(0, 0), S(0, 0), S(0, 0), S(0, 0), S(0, 0), S(0, 0), S(0, 0),
    S(0, 0), S(0, 0), S(0, 0), S(0, 0), S(0, 0), S(0, 0), S(0, 0), S(0, 0),
    S(-2, -5), S(-2, -5), S(-2, -5), S(-2, -5), S(-2, -5), S(-2, -5), S(-2, -5), S(-2, -5),
    S(-2, -5), S(-2, -5), S(-2, -5), S(-2, -5), S(-2, -5), S(-2, -5), S(-2, -5), S(-2, -5),
    S(-2, -5), S(-2, -5), S(-2, -5), S(-2, -5), S(-2, -5), S(-2, -5), S(-2, -5), S(-2, -5),
    S(-2, -5), S(-2, -5), S(-2, -5), S(-2, -5), S(-2, -5), S(-2, -5), S(-2, -5), S(-2, -5),
    S(-2, -5), S(-2, -5), S(-2, -5), S(-2, -5), S(-2, -5), S(-2, -5), S(-2, -5), S(-2, -5),
    S(0, 0), S(0, 0), S(0, 0), S(0, 0), S(0, 0), S(0, 0), S(0, 0), S(0, 0)
};

const TScore PASSED_PAWN[64] = {
    S(0, 0), S(0, 0), S(0, 0), S(0, 0), S(0, 0), S(0, 0), S(0, 0), S(0, 0),
    S(100, 120), S(60, 80), S(60, 80), S(60, 80), S(60, 80), S(60, 80), S(60, 80), S(100, 120),
    S(50, 70), S(40, 60), S(40, 60), S(40, 60), S(40, 60), S(40, 60), S(40, 60), S(50, 70),
    S(35, 60), S(25, 50), S(25, 50), S(25, 50), S(25, 50), S(25, 50), S(25, 50), S(35, 60),
    S(25, 40), S(15, 30), S(15, 30), S(15, 30), S(15, 30), S(15, 30), S(15, 30), S(25, 40),
    S(20, 30), S(10, 20), S(10, 20), S(10, 20), S(10, 20), S(10, 20), S(10, 20), S(20, 30),
    S(20, 30), S(10, 20), S(10, 20), S(10, 20), S(10, 20), S(10, 20), S(10, 20), S(20, 30),
    S(0, 0), S(0, 0), S(0, 0), S(0, 0), S(0, 0), S(0, 0), S(0, 0), S(0, 0),
};

const TScore CONNECED_PASSED_PAWN[64] = {
    S(0, 0), S(0, 0), S(0, 0), S(0, 0), S(0, 0), S(0, 0), S(0, 0), S(0, 0),
    S(45, 80), S(40, 80), S(40, 80), S(40, 80), S(40, 80), S(40, 80), S(40, 80), S(40, 80),
    S(30, 60), S(30, 60), S(30, 60), S(30, 60), S(30, 60), S(30, 60), S(30, 60), S(30, 60),
    S(20, 45), S(20, 45), S(20, 45), S(20, 45), S(20, 45), S(20, 45), S(20, 45), S(20, 45),
    S(10, 30), S(10, 30), S(10, 30), S(10, 30), S(10, 30), S(10, 30), S(10, 30), S(10, 30),
    S(5, 15), S(5, 15), S(5, 15), S(5, 15), S(5, 15), S(5, 15), S(5, 15), S(5, 15),
    S(5, 10), S(5, 10), S(5, 10), S(5, 10), S(5, 10), S(5, 10), S(5, 10), S(5, 10),
    S(0, 0), S(0, 0), S(0, 0), S(0, 0), S(0, 0), S(0, 0), S(0, 0), S(0, 0),
};

/*
 * King Shelter 
 */

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
    S(25, 8), S(25, 8), S(20, 4), S(2, 4), S(2, 4), S(10, 4), S(25, 8), S(25, 8),
    S(40, 10), S(40, 10), S(35, 4), S(10, 4), S(5, 4), S(25, 4), S(40, 10), S(40, 10),
    S(0, 0), S(0, 0), S(0, 0), S(0, 0), S(0, 0), S(0, 0), S(0, 0), S(0, 0),
};

const TScore STORM_PAWN[64] = {
    S(0, 0), S(0, 0), S(0, 0), S(0, 0), S(0, 0), S(0, 0), S(0, 0), S(0, 0),
    S(0, 2), S(0, 2), S(0, 2), S(0, 2), S(0, 2), S(0, 2), S(0, 2), S(0, 2),
    S(0, 2), S(0, 2), S(0, 2), S(0, 2), S(0, 2), S(0, 2), S(0, 2), S(0, 2),
    S(2, 4), S(2, 4), S(2, 4), S(2, 4), S(2, 4), S(2, 4), S(2, 4), S(2, 4),
    S(4, 4), S(4, 4), S(4, 4), S(4, 4), S(4, 4), S(4, 4), S(4, 4), S(4, 4),
    S(10, 4), S(10, 4), S(10, 4), S(10, 4), S(10, 4), S(10, 4), S(10, 4), S(10, 4),
    S(20, 4), S(20, 4), S(20, 4), S(20, 4), S(20, 4), S(20, 4), S(20, 4), S(20, 4),
    S(0, 0), S(0, 0), S(0, 0), S(0, 0), S(0, 0), S(0, 0), S(0, 0), S(0, 0),
};


const TScore SHELTER_OPEN_FILES[4] = {
    S(10, 0), S(-20, -5), S(-50, -10), S(-100, -15)
};

const TScore SHELTER_OPEN_EDGE_FILE = S(-110, -10);

const TScore SHELTER_CASTLING_KINGSIDE = S(50, 10);
const TScore SHELTER_CASTLING_QUEENSIDE = S(40, 10);


int evaluate(TSearch * searchData, int alpha, int beta);

void init_pct(TSCORE_PCT & pct);

TScore * evaluateExp(TSearch * searchData);
bool skipExp(TSearch * sd);

short evaluateMaterial(TSearch * searchData);

TScore * evaluatePawns(TSearch * searchData);

TScore * evaluateRooks(TSearch * searchData);

TScore * evaluateBishops(TSearch * searchData);

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

