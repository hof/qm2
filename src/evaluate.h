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


class TSearchData;

enum EVALUATION_CONSTANTS {
    MAX_PIECES = 16,
    MAX_CLOSED_POSITION = 32, //maximum value indicating how much closed the position is
    GRAIN_SIZE = 3, //powers of 2, used for rounding evaluation score down (and hopefully get more cutoffs)
    GRAIN = 0xFFFFFFFF & ~((1 << GRAIN_SIZE) - 1)
};


const int MAX_EVALUATION_COMPONENTS = 16;

enum EVALUATION_COMPONENTS {
    SCORE_MATERIAL = 0,
    SCORE_PAWNS = 1,
    SCORE_SHELTER_W = 2,
    SCORE_SHELTER_B = 3,
    SCORE_EXP = 4
};

/*******************************************************************************
 * Generic evaluation bonuses
 *******************************************************************************/

enum PIECE_VALUES {
    VPAWN = 100,
    VPAWN_EG = 110,
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
            40, 45, 50, 50, 50, 50, 45, 40,
            0, 10, 15, 20, 20, 15, 10, 0,
            -5, 5, 10, 15, 15, 10, 5, -5,
            -10, 0, 5, 10, 10, 5, 0, -10,
            -15, -5, 0, 5, 5, 0, -5, -15,
            -15, -5, -5, -10, -10, -5, -5, -15,
            0, 0, 0, 0, 0, 0, 0, 0},

        { 0, 0, 0, 0, 0, 0, 0, 0, //middlegame
            40, 45, 50, 50, 50, 50, 45, 40,
            0, 10, 15, 20, 20, 15, 10, 0,
            -5, 5, 10, 15, 15, 10, 5, -5,
            -10, 0, 5, 10, 10, 5, 0, -10,
            -15, -5, 0, 5, 5, 0, -5, -15,
            -15, -5, -5, -10, -10, -5, -5, -15,
            0, 0, 0, 0, 0, 0, 0, 0},

    },
    { //KNIGHT
        {
            -40, -10, -5, -5, -5, -5, -10, -40, //middlegame
            -20, -5, 0, 0, 0, 0, -5, -20,
            -15, 0, 5, 10, 10, 5, 0, -15,
            -20, -5, 0, 5, 5, 0, -5, -20,
            -20, -5, 0, 5, 5, 0, -5, -20,
            -20, -5, 0, 0, 0, 0, -5, -20,
            -20, -10, -5, -5, -5, -5, -10, -20,
            -40, -15, -10, -10, -10, -10, -15, -40
        },
        {
            -40, -15, -10, -10, -10, -10, -15, -40, //endgame
            -20, -5, 0, 1, 1, 0, -5, -20,
            -15, -5, 2, 5, 5, 2, -5, -15,
            -20, -5, 5, 7, 7, 5, -5, -20,
            -20, -5, 5, 7, 7, 5, -5, -20,
            -20, -5, 2, 5, 5, 2, -5, -20,
            -20, -5, 0, 1, 1, 0, -5, -20,
            -40, -15, -10, -10, -10, -10, -15, -40,
        },
    },
    {
        { //BISHOP
            -8, -4, -4, -4, -4, -4, -4, -8, //middlegame
            -4, 2, 5, 5, 5, 5, 2, -4,
            -4, 5, 5, 5, 5, 5, 5, -4,
            -4, 5, 5, 5, 5, 5, 5, -4,
            0, 0, 6, 3, 3, 6, 0, 0,
            0, 6, 4, 2, 2, 4, 6, 0,
            0, 4, -5, -5, -5, -5, 4, 0,
            -8, -6, -6, -8, -8, -6, -6, -8
        },
        { //BISHOP
            -8, -4, -5, -6, -6, -5, -4, -8, //endgame
            -4, 2, 1, 0, 0, 1, 2, -4,
            -5, 1, 3, 2, 2, 3, 1, -5,
            -5, 0, 2, 4, 4, 2, 0, -6,
            -6, 0, 2, 4, 4, 2, 0, -6,
            -5, 1, 3, 2, 2, 3, 1, -5,
            -4, 2, 1, 0, 0, 1, 2, -4,
            -8, -4, -5, -6, -6, -5, -4, -8,
        }
    },
    {
        { //ROOK
            20, 24, 28, 32, 32, 28, 24, 20, //middlegame
            20, 24, 28, 32, 32, 28, 24, 20,
            10, 12, 14, 16, 16, 14, 12, 10,
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
            -80, -85, -90, -95, -95, -90, -85, -80, //middlegame
            -75, -80, -85, -90, -90, -85, -80, -75,
            -70, -75, -80, -85, -85, -80, -75, -70,
            -65, -70, -75, -70, -70, -75, -70, -65,
            -50, -55, -60, -65, -65, -60, -55, -50,
            -10, -10, -25, -30, -30, -25, -15, -10,
            -5, -5, -10, -15, -15, -10, -5, -5,
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
    VBISHOPPAIR = 40,
    VBISHOP_VS_ROOK_ENDGAME = 10,
    VBISHOP_VS_PAWNS_ENDGAME = 10,
    VROOKPAIR = -8,
    VQUEEN_AND_ROOKS = -8,
    DRAWISH_QR_ENDGAME = -30,
};

const short TRADEDOWN_PIECES[MAX_PIECES + 1] = {
    40, 30, 20, 10, 0, -10, -15, -20, -25, -30, -35, -40, -45, -50, -55, -60, -64
};

const short  TRADEDOWN_PAWNS[9] = {
    -20, -15, -10, -5, 0, 2, 4, 6, 6
};

const short  PIECEPOWER_AHEAD[] = {//in amount of pawns
    25, 50, 75, 100, 125, 150, 175, 200, 225, 250, 250, 250, 250, 250, 250, 250, 250,
    250, 250, 250, 250, 250, 250, 250, 250, 250, 250, 250, 250, 250, 250, 250, 250, 250
};

const short  KNIGHT_X_PIECECOUNT[MAX_PIECES + 1] = {
    -16, -12, -10, -8, -4, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0
};

const short  FKNIGHT_OPPOSING_PAWNS[9] = {
    -16, -12, -8, -4, 0, 2, 4, 8, 12
};

const short  BISHOPPAIR_X_PIECECOUNT[MAX_PIECES + 1] = {
    32, 24, 16, 12, 8, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0
};

const short  BISHOP_X_PIECECOUNT[MAX_PIECES + 1] = {
    -16, -12, -8, -6, -4, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0
};

const short  BISHOPPAIR_MINOR_OPPOSITION[10] = {
    20, -10, -25, -50, -60, -70, -70, -70, -70, -70
};

const short  BISHOPPAIR_OPPOSING_PAWNS[9] = {
    20, 15, 10, 5, 2, 0, -5, -10, -15
};

const short  ROOK_OPPOSING_PAWNS[9] = {//rooks get stronger against less pawns
    16, 8, 4, 2, 0, -2, -4, -8, -16
};

const short  QUEEN_MINORCOUNT[MAX_PIECES + 1] = {//queens are stronger when combined with minor pieces
    -32, -16, 0, 4, 8, 16, 24, 32, 32, 32, 32, 32, 32, 32, 32, 32, 32
};

/******************************************
 * Pawns
 *****************************************/

const short  ISOLATED_OPEN_PAWN[64] = {
    0, 0, 0, 0, 0, 0, 0, 0,
    -8, -12, -16, -16, -16, -16, -12, -8,
    -8, -12, -16, -16, -16, -16, -12, -8,
    -8, -12, -16, -16, -16, -16, -12, -8,
    -8, -12, -16, -16, -16, -16, -12, -8,
    -8, -12, -16, -16, -16, -16, -12, -8,
    -8, -12, -16, -16, -16, -16, -12, -8,
    0, 0, 0, 0, 0, 0, 0, 0
};

const short  ISOLATED_PAWN[64] = {
    0, 0, 0, 0, 0, 0, 0, 0,
    -4, -6, -8, -8, -8, -8, -6, -4,
    -4, -6, -8, -8, -8, -8, -6, -4,
    -4, -6, -8, -8, -8, -8, -6, -4,
    -4, -6, -8, -8, -8, -8, -6, -4,
    -4, -6, -8, -8, -8, -8, -6, -4,
    -4, -6, -8, -8, -8, -8, -6, -4,
    0, 0, 0, 0, 0, 0, 0, 0
};


const short  DOUBLED_PAWN[64] = {
    0, 0, 0, 0, 0, 0, 0, 0,
    -2, -3, -4, -4, -4, -4, -3, -2,
    -2, -3, -4, -4, -4, -4, -3, -2,
    -2, -3, -4, -4, -4, -4, -3, -2,
    -2, -3, -4, -4, -4, -4, -3, -2,
    -2, -3, -4, -4, -4, -4, -3, -2,
    -2, -3, -4, -4, -4, -4, -3, -2,
    0, 0, 0, 0, 0, 0, 0, 0
};

const short  PASSED_PAWN[64] = {
    0, 0, 0, 0, 0, 0, 0, 0,
    0, 0, 0, 0, 0, 0, 0, 0,
    16, 20, 24, 24, 24, 24, 20, 16,
    14, 18, 20, 22, 22, 22, 18, 14,
    12, 16, 20, 20, 20, 20, 16, 12,
    10, 14, 18, 18, 18, 18, 14, 10,
    8, 12, 16, 16, 16, 16, 12, 8,
    0, 0, 0, 0, 0, 0, 0, 0
};

const short  CONNECED_PASSED_PAWN[64] = {
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

const short  SHELTER_KPOS[GAME_PHASES][64] = {

    { //middle game
        -65, -75, -85, -95, -90, -80, -70, -60,
        -55, -65, -75, -85, -80, -70, -60, -50,
        -45, -55, -65, -75, -70, -60, -50, -40,
        -35, -45, -55, -65, -60, -50, -40, -30,
        -25, -35, -45, -55, -50, -40, -30, -20,
        0, -25, -35, -45, -40, -30, -20, 5,
        15, 5, 0, 0, 0, 0, 10, 25,
        40, 40, 25, 10, 10, 25, 50, 50
    },
    { //end game
        0, 0, 0, 0, 0, 0, 0, 0,
        0, 0, 0, 0, 0, 0, 0, 0,
        0, 0, 0, 0, 0, 0, 0, 0,
        0, 0, 0, 0, 0, 0, 0, 0,
        0, 0, 0, 0, 0, 0, 0, 0,
        0, 0, 0, 0, 0, 0, 0, 0,
        0, 0, 0, 0, 0, 0, 0, 0,
        0, 0, 0, 0, 0, 0, 0, 0,
    },
};

const short  SHELTER_PAWN[GAME_PHASES][64] = {
    { //middle game
        0, 0, 0, 0, 0, 0, 0, 0,
        2, 2, 2, 2, 2, 2, 2, 2,
        2, 2, 2, 2, 2, 2, 2, 2,
        2, 2, 2, 2, 2, 2, 2, 2,
        5, 5, 5, 2, 2, 5, 5, 5,
        10, 10, 10, 2, 2, 10, 10, 10,
        20, 20, 15, 5, 5, 15, 20, 20,
        0, 0, 0, 0, 0, 0, 0, 0
    },
    { //end game
        0, 0, 0, 0, 0, 0, 0, 0,
        4, 4, 4, 4, 4, 4, 4, 4,
        4, 4, 4, 4, 4, 4, 4, 4,
        4, 4, 4, 4, 4, 4, 4, 4,
        4, 4, 4, 4, 4, 4, 4, 4,
        4, 4, 4, 4, 4, 4, 4, 4,
        4, 4, 4, 4, 4, 4, 4, 4,
        0, 0, 0, 0, 0, 0, 0, 0,
    },
};

const short  SHELTER_OPEN_FILES[GAME_PHASES][4] = {
    { //middle game
        0, -20, -40, -60
    },
    { //end game
        0, -2, -4, -6
    },
};

const short SHELTER_CASTLING_KINGSIDE[GAME_PHASES] = { 50, 10 };
const short SHELTER_CASTLING_QUEENSIDE[GAME_PHASES] = {40 , 10 };

int evaluate(TSearchData * searchData, int alpha, int beta);

TScore * evaluateExp(TSearchData * searchData);

TScore * evaluateMaterial(TSearchData * searchData);

TScore * evaluatePawns(TSearchData * searchData);

void evaluateKingShelter(TSearchData * searchData);

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

