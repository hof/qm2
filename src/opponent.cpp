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
 * File: opponent.cpp
 * Structure to hold information on the opponent
 */

#include "opponent.h"

void opponent_t::clear() {
    title = "";
    name = "Unknown";
    is_computer = true;
    elo_rating = 2400;
}

void opponent_t::copy(opponent_t * c) {
    title = c->title;
    name = c->name;
    is_computer = c->is_computer;
    elo_rating = c->elo_rating;
}

std::string opponent_t::to_string() {
    std::string result = "";
    if (title != "" && title != "none") {
        result += title + " ";
    }
    result += name;
    if (is_computer) {
        result += " (C)";
    }
    if (elo_rating > 0) {
        result += " " + elo_rating;
    }
    return result;
}
