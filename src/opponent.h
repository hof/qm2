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
 * File: opponent.h
 * Structure for holding information about the chess opponent
 *
 * Created on October 19, 2013, 4:25 PM
 */

#ifndef OPPONENT_H
#define	OPPONENT_H

#include "bits.h"

class opponent_t {
public:
    std::string title;
    std::string name;
    bool is_computer;
    int elo_rating;

    void clear();
    void copy(opponent_t * c);
    std::string to_string();
    
    opponent_t() {
        clear();
    }
    
    int draw_contempt(int my_rating=2400) {
        return 0;
    }
};

namespace opponent {
    opponent_t * instance(); 
    int draw_contempt(int my_rating);
    std::string title();
    std::string name();
    int elo_rating();
    bool is_computer();
    void set(std::string name, int elo_rating, bool is_computer, std::string title);
};

#endif	/* OPPONENT_H */

