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
 * File: timeman.cpp
 * Time Manager, calculating how much time to spent on thinking, this is roughly 
 * the time left shared by the amount of moves left. The amount of moves
 * left is assumed to be no more than 30. 
 * In emergency situations more time can be used. For easy moves less time can be used.
 */

#include "timeman.h"

void time_manager_t::set(const int my_time, const int opp_time, const int my_inc, const int opp_inc, const int moves_left) {

    int M = time_man::M;

    if (my_inc > my_time) {
        M = 1;
    } else if (moves_left > 0) {
        M = MIN(moves_left, time_man::M);
    }

    const int M_MIN = M * 2;
    const int M_MAX = 1 + M / 4;
    
    assert(M_MIN > 0 && M_MAX > 0);

    tot_min = my_time / M_MIN;
    tot_max = my_time / M_MAX;
        
    //increment bonus
    if (my_inc > 0) {
        int min_inc_bonus = MIN(my_inc, my_time - tot_min);
        int max_inc_bonus = MIN(my_inc, my_time - tot_max);
        tot_min += (196 * min_inc_bonus) / 256;
        tot_max += max_inc_bonus;
    }

    //adjustment based on opponent's time
    const int delta = my_time - opp_time;
    if (delta > 0 && my_inc >= opp_inc) {
        int min_opp_bonus = MIN(delta, my_time - tot_min);
        int max_opp_bonus = MIN(delta, my_time - tot_max);
        tot_min += min_opp_bonus / 4;
        tot_max += max_opp_bonus / 2;
    } else if (delta < 0 && my_inc <= opp_inc) {
        double speed_up_factor = MAX(0.25, (1.0 * my_time) / (opp_time+1));
        tot_min = speed_up_factor * tot_min;
    }

    tot_min = MAX(0, tot_min - time_man::LAG_TIME);
    tot_max = MAX(0, tot_max - time_man::LAG_TIME);

    set_max(tot_max);
    set_min(tot_min);
    
}

time_manager_t::time_manager_t() {
    clear();
}

void time_manager_t::clear() {
    start = 0;
    min = 0;
    max = 0;
    tot_min = 0;
    tot_max = 0;
}