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
 * File: timemanger.cpp
 * Time Manager, calculating how much time to spent on thinking, this is roughly 
 * the time left shared by the amount of moves left. The amount of moves
 * left is assumed to be no more than 30. 
 * In emergency situations more time can be used. For easy moves less time can be used.
 */

#include "defs.h"
#include "timemanager.h"

void TTimeManager::set(int myTime, int oppTime, int myInc, int oppInc, int movesLeft) {
    static const int OVERHEAD_TIME = 500; //reserve 500ms for overhead (e.g. a slow interface)
    static const double EMERGENCY_FACTOR = 2.0; //for emergencies, multiply the time with this amount
    int M = myInc > 0? 25 : 30; //assume the game is decided after this amount of moves
    M = movesLeft ? MIN(movesLeft + 1, M) : M; //for classic time controls (X moves in Y minutes)
    int limit = myTime - OVERHEAD_TIME;

    /*
     * Base time is our time left shared by the amount of moves left
     */
    int timeForThisMove = limit / M;

    /*
     * Bonus/Penalty time when we have more/less time left than the opponent
     */
    if (myTime > oppTime && oppTime > 0 && myInc >= oppInc && M > 2) {
        double factor = MIN(10.0, (1.0 * myTime) / (1.0 * oppTime));
        timeForThisMove = factor * timeForThisMove;
    } else if (myTime < oppTime && oppTime > 0 && myInc <= oppInc && M > 2) {
        double factor = MAX(0.25, (1.0 * myTime) / (1.0 * oppTime)-0.1);
        timeForThisMove = factor*timeForThisMove;
    }

    /*
     * Bonus time when we have an increment available
     */
    timeForThisMove += myInc;


    /*
     * Make sure the time never exceeds the limit
     */
    timeForThisMove = MIN(timeForThisMove, limit);

    /*
     * Determine how much more time to give for difficult positions 
     * (never exceeding the limit)
     */
    int maxTimeForThisMove = MIN(timeForThisMove*EMERGENCY_FACTOR, limit);
    this->setEndTime(timeForThisMove);
    this->setMaxTime(maxTimeForThisMove);
    std::cout << endTime << std::endl;
}
