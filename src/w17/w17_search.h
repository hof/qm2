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
 * File:  search.h
 * Search methods for wild 17 (losers) chess. 
 *
 * Created on 13 oct 2014, 14:08
 */

#ifndef W17_SEARCH_H
#define	W17_SEARCH_H

#include "search.h"

class w17_search_t : public search_t {

public:

    w17_search_t(const char * fen, game_t * g = NULL) : search_t(fen, g) {
        wild = 17;
        book_name = "book.w17.bin";
    }
    
};

#endif	/* W17_SEARCH_H */

