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
        const int move_overhead, const int time_factor, const int m, const int lag, const int horizon) {

    /*
     * Determine M, the guessed amount of moves left. A horizon (expected moves remaining, standard chess on a
     * sudden-death clock) replaces 2M directly: the budget is clock/horizon and the cap four budgets.
     */
    horizon_moves = horizon;
    const double m_clock = horizon > 0 ? horizon / 2.0 : m;
    double M_MAX, M_MIN;
    if (moves_left > 0) {

        // As sudden death until then, from M + M/2 moves to go every move gets the same budget, and M/2 budgets
        // remain at the control; the cap is four budgets, at most the clock shared over the moves to go.
        M_MIN = MIN(2.0 * m, moves_left + m / 2.0);
        M_MAX = MIN(1.0 * moves_left, M_MIN / 4);
    } else if (my_inc == 0 && my_time < time_man::LOW_TIME) {

        // Slide towards the low time divisors as the clock runs down, so no single move changes the budget much.
        // From clock/2M and clock/(M/2) at two minutes to clock/4M and clock/1.5M at an empty clock.
        const double left = 1.0 * my_time / time_man::LOW_TIME;
        M_MIN = m_clock * (4 - 2 * left);
        M_MAX = m_clock * (1.5 - left);
    } else {

        // An increment comes back every move, so the clock it leaves need not last as long: without a
        // horizon, spend it as if the game were decided in two thirds of the moves.
        const double moves = horizon > 0 ? m_clock : my_inc > 0 ? m * 2.0 / 3 : m;
        M_MIN = 2 * moves;
        M_MAX = moves / 2;
    }

    //set the min/max time range based on time left and M
    tot_min = static_cast<int>(my_time / M_MIN);
    tot_max = static_cast<int>(my_time / M_MAX);

    // A clock lead adds a sixteenth to the budget and an eighth to the cap. A deficit takes half as much, at most
    // half of either, so an opponent who plays fast cannot run us out of time.
    const int delta = my_time - opp_time;
    if (delta > 0 && my_inc >= opp_inc) {
        tot_min += delta / 16;
        tot_max += delta / 8;
    } else if (delta < 0 && my_inc <= opp_inc) {
        tot_min -= MIN(-delta / 32, tot_min / 2);
        tot_max -= MIN(-delta / 16, tot_max / 2);
    }

    /*
     * A normal move spends what comes back; M more increments before the game is decided give the cap 2*inc.
     * The increment never takes a move past half the clock. Every move also pays the server lag on top of
     * its think time, so only inc - lag comes back; a lag above the increment comes off every budget.
     */
    const int net_inc = my_inc - lag;
    if (net_inc > 0) {
        tot_min += MIN(net_inc, MAX(0, my_time / 2 - tot_min));
        tot_max += MIN(2 * net_inc, MAX(0, my_time / 2 - tot_max));
    } else {
        tot_min += net_inc;
        tot_max += net_inc;
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
    horizon_moves = 0;
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
