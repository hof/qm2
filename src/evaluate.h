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
 * File: evaluate.h
 * Evalution functions for traditional chess
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

int evaluate(TSearch * searchData);

void init_pst();
extern TSCORE_PST PST;
extern const TScore TEMPO[2];

enum EVALUATION_CONSTANTS {
    MAX_PIECES = 16,
    MAX_CLOSED_POSITION = 32, //maximum value indicating how much closed the position is
    GRAIN_SIZE = 4 //used for rounding evaluation score (and hopefully get more cutoffs and less "noise")
};

/*******************************************************************************
 * Generic evaluation bonuses
 *******************************************************************************/

const short VPAWN = 100;
const short VKNIGHT = 325;
const short VBISHOP = 325;
const short VROOK = 500;
const short VQUEEN = 925;
const short VKING = 20000;

const short PIECE_VALUE[13] = {
    0, VPAWN, VKNIGHT, VBISHOP, VROOK, VQUEEN, VKING,
    VPAWN, VKNIGHT, VBISHOP, VROOK, VQUEEN, VKING
};

#endif	/* EVALUATE_H */

