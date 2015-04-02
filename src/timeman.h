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
 * File: timeman.h
 * Time Manager, calculating how much time to spent on thinking, this is roughly 
 * the time left shared by the amount of moves left. The amount of moves
 * left is assumed to be no more than 30. 
 * In emergency situations more time can be used. For easy moves less time can be used.
 *
 * Created on 16 mei 2011, 22:37
 */

#ifndef TIMEMAN_H
#define	TIMEMAN_H

#include "bits.h"

class time_manager_t;

namespace time_man {
    const double ONE_MS = CLOCKS_PER_SEC / 1000;
    const int OVERHEAD_TIME = 500;
    const int INFINITE_TIME = 24 * 60 * 60 * 1000;
};

class time_manager_t {
private:
    clock_t start; //in ticks
    clock_t max; //in ticks
    int tot; 

public:
    time_manager_t();
    void clear();
    void set(int my_time, int opp_time, int my_inc, int opp_inc, int moves_left);
    
    clock_t ticks(int time_in_ms) {
        return time_in_ms * time_man::ONE_MS;
    }

    void set_start() {
        start = clock();
    }

    void set_max(int ms) {
        max = start + ticks(ms);
    }

    bool time_is_up() {
        return clock() >= max;
    }

    int elapsed() {
        clock_t ticks = clock() - start;
        return ticks / time_man::ONE_MS;
    }
    
    int reserved() {
        if (tot <= 0) {
            tot = (max - start) / time_man::ONE_MS;
        }
        return tot;
    }
};

#endif	/* TIMEMANAGER_H */

