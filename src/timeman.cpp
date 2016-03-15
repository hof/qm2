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
 * equal to the time left shared by the amount of moves left (M). In emergency 
 * situations more time can be used. For easy moves less time can be used.
 */

#include "timeman.h"

void time_manager_t::set(const int my_time, const int opp_time, const int my_inc, const int opp_inc, const int moves_left) {

    //determine M, the guessed amount of moves left
    int M_MAX, M_MIN;
    if (my_inc > my_time) {
        M_MAX = 1;
        M_MIN = 1;
    } else if (moves_left > 0) {
        M_MIN = MIN(2 * moves_left, time_man::M_MIN);
        M_MAX = MIN(moves_left, time_man::M_MAX);
    } else if (my_inc == 0 && my_time < time_man::LOW_TIME) {
        M_MIN = time_man::M_MIN_LOW_TIME;
        M_MAX = time_man::M_MAX_LOW_TIME;
    } else {
        M_MIN = time_man::M_MIN;
        M_MAX = time_man::M_MAX;
    }

    //set the min/max time range based on time left and M
    tot_min = my_time / M_MIN;
    tot_max = my_time / M_MAX;

    //adjustment based on opponent's time
    const int delta = my_time - opp_time;
    if (delta > 0 && tot_max < delta && my_inc >= opp_inc && opp_time > 0) {
        tot_min += (32 * delta) / 256;
        tot_max += (64 * delta) / 256;
    } else if (delta < 0 && tot_max < -delta && my_inc <= opp_inc && moves_left == 0) {
        const double factor = MAX(0.5, (1.0 * my_time) / (1.0 * opp_time + 1.0));
        tot_min *= factor;
        tot_max *= factor;
    }

    //increment bonus
    if (my_inc > 0) {
        const int min_inc_bonus = MIN(my_inc, my_time - tot_min);
        const int max_inc_bonus = MIN(my_inc, my_time - tot_max);
        tot_min += (196 * min_inc_bonus) / 256;
        tot_max += max_inc_bonus;
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