/**
 * Maxima, a chess playing program. 
 * Copyright (C) 1996-2026 Erik van het Hof and Hermen Reitsma 
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

void time_manager_t::set(const int my_time, const int opp_time, const int my_inc, const int opp_inc, const int moves_left,
        const int move_overhead, const int time_factor, const int m) {

    //determine M, the guessed amount of moves left
    double M_MAX, M_MIN;
    if (my_inc > my_time) {
        M_MAX = 1;
        M_MIN = 1;
    } else if (moves_left > 0) {
        M_MIN = 2 * MIN(moves_left, m);
        M_MAX = MIN(1.0 * moves_left, m / 2.0);
    } else if (my_inc == 0 && my_time < time_man::LOW_TIME) {

        // Slide towards the low time divisors as the clock runs down, so no single move changes the budget much.
        // From clock/2M and clock/(M/2) at two minutes to clock/4M and clock/1.5M at an empty clock.
        const double left = 1.0 * my_time / time_man::LOW_TIME;
        M_MIN = m * (4 - 2 * left);
        M_MAX = m * (1.5 - left);
    } else {
        M_MIN = 2 * m;
        M_MAX = m / 2.0;
    }

    //set the min/max time range based on time left and M
    tot_min = static_cast<int>(my_time / M_MIN);
    tot_max = static_cast<int>(my_time / M_MAX);

    // Spend less when far behind on the clock; a lead is no reason to spend more.
    const int delta = my_time - opp_time;
    if (delta < 0 && tot_max < -delta && my_inc <= opp_inc && moves_left == 0) {
        const double factor = MAX(0.5, (1.0 * my_time) / (1.0 * opp_time + 1.0));
        tot_min *= factor;
        tot_max *= factor;
    }

    // M more increments arrive before the game is decided: share clock + M*inc the same way, so the budget
    // gets inc/2 and the cap 2*inc. The increment never takes a move past half the clock.
    if (my_inc > 0) {
        tot_min += MIN(my_inc / 2, MAX(0, my_time / 2 - tot_min));
        tot_max += MIN(2 * my_inc, MAX(0, my_time / 2 - tot_max));
    }

    // A longer think on request, never past half the clock unless already allotted.
    if (time_factor != 100) {
        const int ceiling = MAX(tot_max, my_time / 2);
        tot_max = static_cast<int>(MIN(int64_t(tot_max) * time_factor / 100, int64_t(ceiling)));
        tot_min = static_cast<int>(MIN(int64_t(tot_min) * time_factor / 100, int64_t(tot_max)));
    }

    tot_min = MAX(0, tot_min - move_overhead);
    tot_max = MAX(0, tot_max - move_overhead);

    // Always a real search: the overhead must not eat the whole budget of a short clock.
    const int floor_time = MIN(time_man::MIN_SEARCH_TIME, my_time / 16);
    tot_min = MAX(tot_min, floor_time);
    tot_max = MAX(tot_max, floor_time * 2);
    if (moves_left == 1) {

        // Reach the next time control without gambling the whole clock on it.
        // Keep useful thinking time when already in severe time trouble.
        const int period_reserve = MAX(move_overhead, MIN(1000, my_time / 20));
        const int period_budget = MAX(0, my_time - period_reserve);
        tot_min = MIN(tot_min, period_budget);
        tot_max = MIN(tot_max, period_budget);
    }
    set_max(tot_max);
    set_min(tot_min);

}

time_manager_t::time_manager_t() {
    clear();
}

void time_manager_t::clear() {
    start = std::chrono::steady_clock::now();
    max = start;
    tot_min = 0;
    tot_max = 0;
}

// The budget runs from the go command: time pondered counts, and a ponderhit
// past the deadline stops at the next poll.
bool time_manager_t::handle_abort(bool pondering, int & next_poll) {
    if (pondering) {
        next_poll = 5000;
        return false;
    }
    const int left = remaining();

    // Tighten polling near the deadline without taxing the ordinary hot path.
    next_poll = left <= 50 ? 64 : left <= 250 ? 512 : 5000;
    return time_is_up();
}
