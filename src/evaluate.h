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
 * Piece Square Tables. Factors considered:
 * - Centralize pieces
 * - Forward pieces (especially in middle game)
 * - Pawns on the edge are worth less
 *******************************************************************************/

const short PIECE_SQUARE_TABLE[WKING][GAME_PHASES][64] = {
    { //PAWN
        { 0, 0, 0, 0, 0, 0, 0, 0
            - 8, 10, 14, 20, 20, 14, 10, -8,
            -10, 8, 10, 18, 18, 10, 8, -10,
            -12, 6, 8, 16, 16, 8, 6, -12,
            -14, 2, 8, 16, 16, 8, 2, -14,
            -16, 0, 4, 8, 8, 4, 0, -16,
            -14, 2, 0, -2, -2, 0, 2, -14,
            0, 0, 0, 0, 0, 0, 0, 0},

        { 0, 0, 0, 0, 0, 0, 0, 0,
            38, 56, 58, 66, 66, 58, 56, 38,
            20, 38, 40, 48, 48, 40, 38, 20,
            4, 20, 24, 32, 32, 24, 20, 4,
            -8, 8, 12, 22, 22, 12, 8, -8,
            -14, 2, 6, 10, 10, 6, 2, -14,
            -14, 2, 6, 8, 8, 6, 2, -14,
            0, 0, 0, 0, 0, 0, 0, 0},

    },
    { //KNIGHT
        {
            -18, -8, 0, 4, 4, 0, -8, -18,
            -8, 0, 20, 24, 24, 20, 0, -8,
            -2, 18, 38, 42, 42, 38, 18, -2,
            -6, 12, 26, 30, 30, 26, 12, -6,
            -10, 8, 20, 24, 24, 20, 8, -10,
            -14, 0, 14, 18, 18, 14, 0, -14,
            -22, -12, -4, 0, 0, -4, -12, -22,
            -26, -20, -14, -12, -12, -14, -20, -26

        },
        {
            -20, -14, -4, 0, 0, -4, -14, -20,
            -14, -6, 10, 12, 12, 10, -6, -14,
            -6, 12, 26, 32, 32, 26, 12, -6,
            -10, 10, 22, 24, 24, 22, 10, -10,
            -12, 2, 16, 18, 18, 16, 2, -12,
            -16, -2, 12, 14, 14, 12, -2, -16,
            -22, -12, -4, 0, 0, -4, -12, -22,
            -26, -20, -14, -12, -12, -14, -20, -26
        },
    },
    {
        { //BISHOP

            -2, -2, -2, -2, -2, -2, -2, -2,
            -2, 4, 4, 4, 4, 4, 4, -2,
            -2, 4, 10, 10, 10, 10, 4, -2,
            -2, 4, 8, 12, 12, 8, 4, -2,
            0, 4, 8, 12, 12, 8, 4, 0,
            0, 4, 6, 6, 6, 6, 4, 0,
            0, 2, 0, -2, -2, 0, 2, 0,
            0, -2, -6, -8, -8, -6, -2, 0
        },
        { //BISHOP
            -4, -2, -4, -4, -4, -4, -2, -4,
            -4, 0, 2, 0, 0, 2, 0, -4,
            -4, 2, 6, 6, 6, 6, 2, -4,
            -2, 2, 6, 10, 10, 6, 2, -2,
            -4, 2, 6, 6, 6, 6, 2, -4,
            -2, 2, 4, 2, 2, 4, 2, -2,
            -2, 0, -2, -4, -4, -2, 0, -2,
            -2, -4, -6, -8, -8, -6, -4, -2
        }
    },
    {
        { //ROOK
            24, 26, 32, 34, 34, 32, 26, 24,
            20, 24, 28, 32, 32, 28, 24, 20,
            4, 6, 10, 14, 14, 10, 6, 4,
            -2, 0, 4, 6, 6, 4, 0, -2,
            -6, -2, 2, 4, 4, 2, -2, -6,
            -8, -6, 0, 2, 2, 0, -6, -8,
            -8, -6, 0, 2, 2, 0, -6, -8,
            -12, -8, -4, 0, 0, -4, -8, -12
        },
        { //ROOK
            6, 10, 14, 18, 18, 14, 10, 6,
            6, 10, 14, 18, 18, 14, 10, 6,
            -4, -2, 4, 6, 6, 4, -2, -4,
            -8, -4, 0, 2, 2, 0, -4, -8,
            -8, -4, 0, 2, 2, 0, -4, -8,
            -10, -8, -2, 0, 0, -2, -8, -10,
            -10, -8, -2, 0, 0, -2, -8, -10,
            -14, -10, -6, -2, -2, -6, -10, -14
        },
    },
    {
        { //QUEEN


            0, 0, 2, 2, 2, 2, 0, 0,
            -2, 2, 2, 4, 4, 2, 2, -2,
            -2, 0, 4, 4, 4, 4, 0, -2,
            -2, 0, 2, 4, 4, 2, 0, -2,
            -2, 0, 2, 4, 4, 2, 0, -2,
            -4, -2, 0, 2, 2, 0, -2, -4,
            -4, -2, -2, -2, -2, -2, -2, -4,
            -4, -4, -4, -4, -4, -4, -4, -4

        },
        { //QUEEN

            6, 8, 10, 10, 10, 10, 8, 6,
            6, 10, 12, 14, 14, 12, 10, 6,
            4, 8, 12, 14, 14, 12, 8, 4,
            4, 8, 12, 14, 14, 12, 8, 4,
            4, 8, 12, 14, 14, 12, 8, 4,
            2, 6, 10, 12, 12, 10, 6, 2,
            4, 6, 8, 8, 8, 8, 6, 4,
            2, 2, 4, 4, 4, 4, 2, 2
        },
    },
    {
        { //KING
            -136, -136, -140, -150, -150, -140, -136, -136,
            -116, -120, -126, -136, -136, -126, -120, -116,
            -96, -100, -116, -126, -126, -116, -100, -96,
            -76, -80, -96, -126, -126, -96, -80, -76,
            -56, -60, -76, -106, -106, -76, -60, -56,
            -36, -40, -56, -66, -66, -56, -40, -36,
            -16, -20, -26, -36, -36, -26, -20, -16,
            4, 4, 0, -10, -10, 0, 4, 4
        },
        { //KING
            14, 26, 30, 34, 34, 30, 26, 14,
            20, 38, 46, 54, 54, 46, 38, 20,
            18, 34, 44, 52, 52, 44, 34, 18,
            14, 28, 40, 50, 50, 40, 28, 14,
            12, 26, 36, 44, 44, 36, 26, 12,
            12, 24, 34, 42, 42, 34, 24, 12,
            10, 20, 28, 34, 34, 28, 20, 10,
            6, 12, 18, 20, 20, 18, 12, 6
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

const short TRADEDOWN_PAWNS[9] = {
    -20, -15, -10, -5, 0, 2, 4, 6, 6
};

const short PIECEPOWER_AHEAD[] = {//in amount of pawns
    25, 50, 75, 100, 125, 150, 175, 200, 225, 250, 250, 250, 250, 250, 250, 250, 250,
    250, 250, 250, 250, 250, 250, 250, 250, 250, 250, 250, 250, 250, 250, 250, 250, 250
};

const short KNIGHT_X_PIECECOUNT[MAX_PIECES + 1] = {
    -16, -12, -10, -8, -4, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0
};

const short FKNIGHT_OPPOSING_PAWNS[9] = {
    -16, -12, -8, -4, 0, 2, 4, 8, 12
};

const short BISHOPPAIR_X_PIECECOUNT[MAX_PIECES + 1] = {
    32, 24, 16, 12, 8, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0
};

const short BISHOP_X_PIECECOUNT[MAX_PIECES + 1] = {
    -16, -12, -8, -6, -4, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0
};

const short BISHOPPAIR_MINOR_OPPOSITION[10] = {
    20, -10, -25, -50, -60, -70, -70, -70, -70, -70
};

const short BISHOPPAIR_OPPOSING_PAWNS[9] = {
    20, 15, 10, 5, 2, 0, -5, -10, -15
};

const short ROOK_OPPOSING_PAWNS[9] = {//rooks get stronger against less pawns
    16, 8, 4, 2, 0, -2, -4, -8, -16
};

const short QUEEN_MINORCOUNT[MAX_PIECES + 1] = {//queens are stronger when combined with minor pieces
    -32, -16, 0, 4, 8, 16, 24, 32, 32, 32, 32, 32, 32, 32, 32, 32, 32
};

/******************************************
 * Pawns
 *****************************************/

const short ISOLATED_OPEN_PAWN[64] = {
    0, 0, 0, 0, 0, 0, 0, 0,
    -8, -12, -16, -16, -16, -16, -12, -8,
    -8, -12, -16, -16, -16, -16, -12, -8,
    -8, -12, -16, -16, -16, -16, -12, -8,
    -8, -12, -16, -16, -16, -16, -12, -8,
    -8, -12, -16, -16, -16, -16, -12, -8,
    -8, -12, -16, -16, -16, -16, -12, -8,
    0, 0, 0, 0, 0, 0, 0, 0
};

const short ISOLATED_PAWN[64] = {
    0, 0, 0, 0, 0, 0, 0, 0,
    -4, -6, -8, -8, -8, -8, -6, -4,
    -4, -6, -8, -8, -8, -8, -6, -4,
    -4, -6, -8, -8, -8, -8, -6, -4,
    -4, -6, -8, -8, -8, -8, -6, -4,
    -4, -6, -8, -8, -8, -8, -6, -4,
    -4, -6, -8, -8, -8, -8, -6, -4,
    0, 0, 0, 0, 0, 0, 0, 0
};


const short DOUBLED_PAWN[64] = {
    0, 0, 0, 0, 0, 0, 0, 0,
    -2, -3, -4, -4, -4, -4, -3, -2,
    -2, -3, -4, -4, -4, -4, -3, -2,
    -2, -3, -4, -4, -4, -4, -3, -2,
    -2, -3, -4, -4, -4, -4, -3, -2,
    -2, -3, -4, -4, -4, -4, -3, -2,
    -2, -3, -4, -4, -4, -4, -3, -2,
    0, 0, 0, 0, 0, 0, 0, 0
};

const short PASSED_PAWN[64] = {
    0, 0, 0, 0, 0, 0, 0, 0,
    0, 0, 0, 0, 0, 0, 0, 0,
    16, 20, 24, 24, 24, 24, 20, 16,
    14, 18, 20, 22, 22, 22, 18, 14,
    12, 16, 20, 20, 20, 20, 16, 12,
    10, 14, 18, 18, 18, 18, 14, 10,
    8, 12, 16, 16, 16, 16, 12, 8,
    0, 0, 0, 0, 0, 0, 0, 0
};

const short CONNECED_PASSED_PAWN[64] = {
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

const short SHELTER_KPOS[GAME_PHASES][64] = {

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

const short SHELTER_PAWN[GAME_PHASES][64] = {
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

const short SHELTER_OPEN_FILES[GAME_PHASES][4] = {
    { //middle game
        0, -20, -40, -60
    },
    { //end game
        0, -2, -4, -6
    },
};

const short SHELTER_CASTLING_KINGSIDE[GAME_PHASES] = {50, 10};
const short SHELTER_CASTLING_QUEENSIDE[GAME_PHASES] = {40, 10};

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

