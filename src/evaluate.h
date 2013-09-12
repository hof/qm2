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


class TSearchData;

enum EVALUATION_CONSTANTS {
    MAX_PIECES = 16,
    MAX_PHASE_SCORE = 16,
    MAX_CLOSED_POSITION = 32, //maximum value indicating how much closed the position is
    GAME_PHASES = 2, //middlegame and endgame
    PHASE_OPENING = 0, //two phases are used: middlegame and endgame
    PHASE_MIDDLEGAME = 0,
    PHASE_ENDGAME = 1,
    GRAIN_SIZE = (1 << 3), //2^3==8, giving eval steps of 8 centipawns
    GRAIN = 0xFFFFFFFF & ~(GRAIN_SIZE - 1)
};

const int MAX_EVALUATION_COMPONENTS = 5;
enum EVALUATION_COMPONENTS {
    SCORE_MATERIAL,
    SCORE_PAWNS,
    SCORE_KINGS,
    SCORE_SHELTERW,
    SCORE_SHELTERB            
};

/*******************************************************************************
 * Generic evaluation bonuses
 *******************************************************************************/

enum PIECE_VALUES {
    VPAWN = 100,
    VKNIGHT = 325,
    VBISHOP = 325,
    VROOK = 500,
    VQUEEN = 975,
    VKING = 20000
};

const int PIECE_VALUE[13] = {
    0,
    VPAWN, VKNIGHT, VBISHOP,
    VROOK, VQUEEN, VKING,
    VPAWN, VKNIGHT, VBISHOP,
    VROOK, VQUEEN, VKING
};

/*******************************************************************************
 * Piece Square Tables
 *******************************************************************************/

const short PIECE_SQUARE_TABLE[WKING][GAME_PHASES][64] = {
    { //PAWN
        { 0, 0, 0, 0, 0, 0, 0, 0, //middlegame
            12, 22, 26, 26, 26, 26, 22, 12,
            -5, 10, 12, 15, 15, 12, 10, -5,
            -10, 5, 8, 12, 12, 8, 5, -10,
            -16, 0, 5, 10, 10, 5, 0, -16,
            -15, 1, 4, 5, 5, 4, 1, -15,
            -14, 2, 4, -6, -6, 4, 2, -14,
            0, 0, 0, 0, 0, 0, 0, 0},

        { 0, 0, 0, 0, 0, 0, 0, 0, //endgame
            30, 35, 35, 35, 35, 35, 35, 30,
            10, 15, 15, 15, 15, 15, 15, 10,
            0, 5, 10, 10, 10, 10, 5, 0,
            -5, 2, 5, 6, 6, 5, 2, -5,
            -9, -3, 0, 0, 0, 0, -3, -9,
            -10, -5, -5, -6, -6, -5, -5, -10,
            0, 0, 0, 0, 0, 0, 0, 0},

    },
    { //KNIGHT
        {
            -15, -10, -5, -5, -5, -5, -10, -15, //middlegame
            -10, -5, 0, 0, 0, 0, -5, -10,
            -10, 0, 5, 10, 10, 5, 0, -15,
            -10, -5, 0, 5, 5, 0, -5, -20,
            -10, -5, 0, 5, 5, 0, -5, -20,
            -10, -5, 0, 0, 0, 0, -5, -20,
            -15, -10, -5, -5, -5, -5, -10, -15,
            -20, -15, -10, -10, -10, -10, -15, -20
        },
        {
            -30, -15, -10, -10, -10, -10, -15, -30, //endgame
            -20, -5, 0, 1, 1, 0, -5, -20,
            -15, -5, 2, 5, 5, 2, -5, -15,
            -20, -5, 5, 7, 7, 5, -5, -20,
            -20, -5, 5, 7, 7, 5, -5, -20,
            -20, -5, 2, 5, 5, 2, -5, -20,
            -20, -5, 0, 1, 1, 0, -5, -20,
            -30, -15, -10, -10, -10, -10, -15, -30,
        },
    },
    {
        { //BISHOP
            -4, -4, -4, -4, -4, -4, -4, -4, //middlegame
            -4, 2, 5, 5, 5, 5, 2, -4,
            -4, 5, 5, 5, 5, 5, 5, -4,
            -4, 5, 5, 5, 5, 5, 5, -4,
            0, 0, 6, 3, 3, 6, 0, 0,
            0, 6, 4, 2, 2, 4, 6, 0,
            0, 4, -5, -5, -5, -5, 4, 0,
            -4, -6, -6, -8, -8, -6, -6, -4
        },
        { //BISHOP
            -3, -4, -5, -6, -6, -5, -4, -3, //endgame
            -4, 2, 1, 0, 0, 1, 2, -4,
            -5, 1, 3, 2, 2, 3, 1, -5,
            -5, 0, 2, 4, 4, 2, 0, -6,
            -6, 0, 2, 4, 4, 2, 0, -6,
            -5, 1, 3, 2, 2, 3, 1, -5,
            -4, 2, 1, 0, 0, 1, 2, -4,
            -3, -4, -5, -6, -6, -5, -4, -3,
        }
    },
    {
        { //ROOK
            20, 24, 28, 32, 32, 28, 24, 20, //middlegame
            20, 24, 28, 32, 32, 28, 24, 20,
            0, 0, 0, 0, 0, 0, 0, 0,
            0, 0, 0, 0, 0, 0, 0, 0,
            0, 0, 0, 0, 0, 0, 0, 0,
            0, 0, 0, 0, 0, 0, 0, 0,
            0, 0, 0, 0, 0, 0, 0, 0,
            0, 0, 2, 5, 5, 2, 0, 0
        },
        { //ROOK
            16, 20, 24, 28, 28, 24, 20, 16, //endgame
            20, 24, 28, 32, 32, 28, 24, 20,
            2, 5, 5, 5, 5, 5, 5, 2,
            1, 2, 2, 3, 3, 2, 2, 1,
            0, 1, 2, 3, 3, 2, 1, 0,
            0, 1, 2, 3, 3, 2, 1, 0,
            0, 0, 1, 2, 2, 1, 0, 0,
            0, 0, 1, 2, 2, 1, 0, 0
        },
    },
    {
        { //QUEEN
            -2, 0, 0, 0, 0, 0, 0, -2, //middlegame
            -5, 2, 2, 2, 2, 2, 2, -5,
            -5, 0, 0, 2, 2, 0, 0, -5,
            -5, 0, 0, 2, 2, 0, 0, -5,
            -5, 1, 1, 2, 2, 1, 1, -5,
            -2, 1, 1, 1, 1, 1, 1, -2,
            -2, 0, 1, 0, 0, 1, 0, -5,
            -2, -1, -1, 0, 0, -1, -1, -2
        },
        { //QUEEN
            -8, -4, 2, 4, 4, 2, -4, -8, //endgame
            -5, 2, 5, 10, 10, 5, 0, -5,
            -5, 0, 2, 5, 5, 2, 0, -5,
            -5, 0, 2, 10, 10, 2, 0, -5,
            -5, 0, 2, 10, 10, 2, 0, -5,
            -5, 0, 2, 5, 5, 2, 0, -5,
            -5, 0, 1, 2, 2, 1, 0, -5,
            -10, -5, 0, 1, 1, 0, -5, -10
        },
    },
    {
        { //KING
            -30, -35, -40, -45, -45, -40, -35, -30, //middlegame
            -25, -30, -35, -40, -40, -35, -30, -25,
            -20, -25, -30, -35, -35, -30, -25, -20,
            -15, -20, -25, -30, -30, -25, -20, -15,
            -10, -15, -20, -25, -25, -20, -15, -10,
            -5, -10, -15, -20, -20, -15, -10, -5,
            -2, -5, -10, -15, -15, -10, -5, -2,
            -1, 0, -1, -10, -10, -5, 0, -1
        },
        { //KING
            -15, -10, -5, -5, -5, -5, -10, -15, //endgame
            -10, -5, 0, 5, 5, 0, -5, -10,
            -5, 0, 10, 15, 15, 10, 0, -5,
            -5, 10, 15, 25, 25, 15, 10, -5,
            -5, 10, 15, 20, 20, 15, 10, -5,
            -10, 5, 10, 15, 15, 10, 5, -10,
            -15, -5, 0, 5, 5, 0, -5, -15,
            -20, -15, -10, -5, -5, -10, -15, -20
        }
    }
};

/*******************************************************************************
 * Material Evaluation Values 
 *******************************************************************************/

enum MaterialValues {
    MATERIAL_AHEAD_TRESHOLD = 80,
    VNOPAWNS = -50,
    VKNIGHT_PAIR = -12,
    VBISHOPPAIR = 75,
    VBISHOP_VS_ROOK_ENDGAME = 10,
    VBISHOP_VS_PAWNS_ENDGAME = 10,
    VROOKPAIR = -8,
    VQUEEN_AND_ROOKS = -8
};

const char TRADEDOWN_PIECES[MAX_PIECES + 1] = {
    40, 30, 20, 10, 0, -10, -15, -20, -25, -30, -35, -40, -45, -50, -55, -60, -64
};

const char TRADEDOWN_PAWNS[9] = {
    -20, -15, -10, -5, 0, 2, 4, 6, 6
};

const char PIECEPOWER_AHEAD[] = {//in amount of pawns
    25, 50, 75, 100, 125, 150, 175, 200, 225, 250, 250, 250, 250, 250, 250, 250, 250,
    250, 250, 250, 250, 250, 250, 250, 250, 250, 250, 250, 250, 250, 250, 250, 250, 250
};

const char KNIGHT_X_PIECECOUNT[MAX_PIECES + 1] = {
    -16, -12, -10, -8, -4, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0
};

const char FKNIGHT_OPPOSING_PAWNS[9] = {
    -16, -12, -8, -4, 0, 2, 4, 8, 12
};

const char BISHOPPAIR_X_PIECECOUNT[MAX_PIECES + 1] = {
    32, 24, 16, 12, 8, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0
};

const char BISHOP_X_PIECECOUNT[MAX_PIECES + 1] = {
    -16, -12, -8, -6, -4, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0
};

const char BISHOPPAIR_MINOR_OPPOSITION[10] = {
    20, -10, -25, -50, -60, -70, -70, -70, -70, -70
};

const char BISHOPPAIR_OPPOSING_PAWNS[9] = {
    20, 15, 10, 5, 2, 0, -5, -10, -15
};

const char ROOK_OPPOSING_PAWNS[9] = {//rooks get stronger against less pawns
    16, 8, 4, 2, 0, -2, -4, -8, -16
};

const char QUEEN_MINORCOUNT[MAX_PIECES + 1] = {//queens are stronger when combined with minor pieces
    -32, -16, 0, 4, 8, 16, 24, 32, 32, 32, 32, 32, 32, 32, 32, 32, 32
};

/******************************************
 * Pawns
 *****************************************/

const char ISOLATED_OPEN_PAWN[64] = {
    0, 0, 0, 0, 0, 0, 0, 0,
    -8, -12, -16, -16, -16, -16, -12, -8,
    -8, -12, -16, -16, -16, -16, -12, -8,
    -8, -12, -16, -16, -16, -16, -12, -8,
    -8, -12, -16, -16, -16, -16, -12, -8,
    -8, -12, -16, -16, -16, -16, -12, -8,
    -8, -12, -16, -16, -16, -16, -12, -8,
    0, 0, 0, 0, 0, 0, 0, 0
};

const char ISOLATED_PAWN[64] = {
    0, 0, 0, 0, 0, 0, 0, 0,
    -4, -6, -8, -8, -8, -8, -6, -4,
    -4, -6, -8, -8, -8, -8, -6, -4,
    -4, -6, -8, -8, -8, -8, -6, -4,
    -4, -6, -8, -8, -8, -8, -6, -4,
    -4, -6, -8, -8, -8, -8, -6, -4,
    -4, -6, -8, -8, -8, -8, -6, -4,
    0, 0, 0, 0, 0, 0, 0, 0
};


const char DOUBLED_PAWN[64] = {
    0, 0, 0, 0, 0, 0, 0, 0,
    -2, -3, -4, -4, -4, -4, -3, -2,
    -2, -3, -4, -4, -4, -4, -3, -2,
    -2, -3, -4, -4, -4, -4, -3, -2,
    -2, -3, -4, -4, -4, -4, -3, -2,
    -2, -3, -4, -4, -4, -4, -3, -2,
    -2, -3, -4, -4, -4, -4, -3, -2,
    0, 0, 0, 0, 0, 0, 0, 0
};

const char PASSED_PAWN[64] = {
    0, 0, 0, 0, 0, 0, 0, 0,
    0, 0, 0, 0, 0, 0, 0, 0,
    16, 20, 24, 24, 24, 24, 20, 16,
    14, 18, 20, 22, 22, 22, 18, 14,
    12, 16, 20, 20, 20, 20, 16, 12,
    10, 14, 18, 18, 18, 18, 14, 10,
    8, 12, 16, 16, 16, 16, 12, 8,
    0, 0, 0, 0, 0, 0, 0, 0
};

const char CONNECED_PASSED_PAWN[64] = {
    0, 0, 0, 0, 0, 0, 0, 0,
    90, 95, 100, 100, 100, 100, 95, 90,
    70, 75, 80, 80, 80, 80, 75, 70,
    50, 55, 60, 60, 60, 60, 55, 50,
    12, 16, 20, 20, 20, 20, 16, 12,
    10, 14, 18, 18, 18, 18, 14, 10,
    8, 12, 16, 16, 16, 16, 12, 8,
    0, 0, 0, 0, 0, 0, 0, 0
};

/*
 * King Shelter 
 */

const char SHELTER_KPOS[64] = {
    -65, -75, -85, -95, -90, -80, -70, -60,
    -55, -65, -75, -85, -80, -70, -60, -50,
    -45, -55, -65, -75, -70, -60, -50, -40,
    -35, -45, -55, -65, -60, -50, -40, -30,
    -25, -35, -45, -55, -50, -40, -30, -20,
    0, -25, -35, -45, -40, -30, -20, 5,
    15, 5, 0, 0, 0, 0, 10, 25,
    40, 40, 25, 10, 10, 25, 50, 50
};

const char SHELTER_PAWN[64] = {
    0, 0, 0, 0, 0, 0, 0, 0,
    2, 2, 2, 2, 2, 2, 2, 2,
    2, 2, 2, 2, 2, 2, 2, 2,
    2, 2, 2, 2, 2, 2, 2, 2,
    5, 5, 5, 2, 2, 5, 5, 5,
    10, 10, 10, 2, 2, 10, 10, 10,
    15, 15, 15, 5, 5, 15, 15, 15,
    0, 0, 0, 0, 0, 0, 0, 0
};

const char SHELTER_OPEN_FILES[4] = {
    0, -20, -40, -60
};

const char SHELTER_CASTLING_KINGSIDE = 60;
const char SHELTER_CASTLING_QUEENSIDE = 50;

int evaluate(TSearchData * searchData, int alpha, int beta);

int evaluateExp(TSearchData * searchData);

int evaluateMaterial(TSearchData * searchData);

int evaluatePawns(TSearchData * searchData);

int evaluateKings(TSearchData * searchData);

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

inline int cond(bool whiteCondition, bool blackCondition, const char values[], unsigned char indexW, unsigned char indexB) {
    if (whiteCondition != blackCondition) {
        return whiteCondition ? values[indexW] : -values[indexB];
    }
    return 0;
}

inline int factor(char wFactor, char bFactor, const char values[], unsigned char indexW, unsigned char indexB) {
    return wFactor * values[indexW] - bFactor * values[indexB];
}

inline int factor(char wFactor, char bFactor, const char values[], unsigned char index) {
    return factor(wFactor, bFactor, values, index, index);
}

#define MAX_GAMEPHASES 16 //use grain size of 16 gamephases
#define GAMEPHASE_BSR 2 //divide by 4 (right shift by 2) (64/4=16)
#define GAMEPHASE_SCORE_BSR 4 //divide by 16 (right shift by 4)

inline int phasedScore(int gameProgress, int middleGameValue, int endGameValue) {
    assert(gameProgress >= 0 && gameProgress <= MAX_GAMEPHASES);
    return (middleGameValue * (MAX_GAMEPHASES - gameProgress) + endGameValue * gameProgress) >> GAMEPHASE_SCORE_BSR;
}

#endif	/* EVALUATE_H */

