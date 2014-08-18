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
 * File: timeman.cpp
 * Time Manager, calculating how much time to spent on thinking, this is roughly 
 * the time left shared by the amount of moves left. The amount of moves
 * left is assumed to be no more than 30. 
 * In emergency situations more time can be used. For easy moves less time can be used.
 */

#include "timeman.h"

void time_manager_t::set(int my_time, int opp_time, int my_inc, int opp_inc, int moves_left) {
    int M = my_inc > 0 ? 20 : 30; //assume the game is decided after this amount of moves
    M = moves_left ? MIN(moves_left + 1, M) : M; //for classic time controls (X moves in Y minutes)
    int limit = my_time - time_man::OVERHEAD_TIME;

    /*
     * Base time is our time left shared by the amount of moves left
     */
    int result = limit / M;

    /*
     * Bonus/Penalty time when we have more/less time left than the opponent
     */
    if (my_time > opp_time && opp_time > 0 && my_inc >= opp_inc && M > 2) {
        double factor = MIN(10.0, (1.0 * my_time) / (1.0 * opp_time));
        result = factor * result;
    } else if (my_time < opp_time && opp_time > 0 && my_inc <= opp_inc && M > 2) {
        double factor = MAX(0.25, (1.0 * my_time) / (1.0 * opp_time) - 0.1);
        result = factor*result;
    }

    /*
     * Bonus time when we have an increment available
     */
    result += my_inc;

    /*
     * Make sure the time never exceeds the limit
     */
    result = MIN(result, limit);
    tot = result;
    this->set_max(result);
}

time_manager_t::time_manager_t() {
    clear();
}

void time_manager_t::clear() {
    start = 0;
    max = 0;
    tot = 0;
}