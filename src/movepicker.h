/**
 * Maxima, a chess playing program. 
 * Copyright (C) 1996-2015 Erik van het Hof and Hermen Reitsma 
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
 * File: movepicker.h
 * Move Picker class to use by the search. The move picker handles ordered move 
 * generation in phases, typically following this order:
 
 * Standard chess returns pseudo-legal candidates. Callers own king safety.
 *
 * Created on 11 mei 2011, 19:25
 */

#ifndef MOVEPICKER_H
#define	MOVEPICKER_H

#include "movegen.h"

class search_t;

enum move_stage_t {
    HASH,
    CAPTURES,
    PROMOTIONS,
    KILLER1,
    KILLER2,
    MINORPROMOTIONS,
    CASTLING,
    QUIET_MOVES,
    STOP,
    countermove,
    quiet_init,
    good_quiets,
    bad_noisy,
    bad_quiets
};

class move_picker_t {
private:
    move_t * pop(search_t * s, move::list_t * list);
    move_t * pop_standard(search_t * s, int phase);
    move_t * next_standard(search_t * s, int depth);

public:
    move_t * first(search_t * s, int depth);
    move_t * next(search_t * s, int depth); 
};

namespace move {
    move_picker_t * picker();
    move_t * first(search_t * s, int depth);
    move_t * next(search_t * s, int depth); 
};

#endif	/* MOVEPICKER_H */

