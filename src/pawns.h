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
 * File: pawn.h
 * Pawn evaluation for traditional chess
 *
 * Created on September 26, 2014, 11:49 AM
 */

#ifndef PAWNS_H
#define	PAWNS_H

#include "search.h"
#include "score.h"

namespace pawns {
    
    score_t * eval(search_t * s);
    
}

#endif	/* ENDGAME_H */

