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
 *
 * Created on 16 mei 2011, 22:37
 */

#ifndef TIMEMANAGER_H
#define	TIMEMANAGER_H

#include "defs.h"
#include <time.h>
#include <string.h>

const double ONE_MS = CLOCKS_PER_SEC / 1000;
const int INFINITE_TIME = 24 * 60 * 60 * 1000;

class TTimeManager {
private:
    clock_t startTime;
    clock_t endTime;
    clock_t maxEndTime;
public:

    TTimeManager() {
        startTime = 0;
        endTime = 0;
        maxEndTime = 0;
    }

    clock_t ticks(int time_in_ms) {
        return time_in_ms * ONE_MS;
    }

    void setStartTime() {
        startTime = clock();
    }

    void setMaxTime(int ms) {
        maxEndTime = startTime + ticks(ms);
    }

    void setEndTime(int ms) {
        endTime = startTime + ticks(ms);
    }

    void set(int myTime, int oppTime, int myInc, int oppInc, int movesLeft);

    inline bool timeIsUp() {
        return clock() >= endTime;
    }

    inline bool available(int ms) {
        return (clock() + ticks(ms)) <= endTime;
    }

    inline int available() {
        clock_t ticks = endTime - clock();
        return ticks / ONE_MS;
    }

    /**
     * Return elapsed time in ms
     * @return elapsed time in ms
     */
    inline int elapsed() {
        clock_t ticks = clock() - startTime;
        return ticks / ONE_MS;
    }

    inline bool requestMoreTime() {
        clock_t ticks_available = maxEndTime - endTime;
        if (ticks_available > ticks(500)) {
            endTime += (ticks_available / 2);
            return true;
        }
        endTime = maxEndTime;
        return false;
    }

    inline bool requestLessTime() {
        if (maxEndTime != endTime) {
            clock_t ticks_available = endTime - startTime;
            if (ticks_available > ticks(0)) {
                endTime -= ticks_available / 2;
                return true;
            }
        }
        return false;
    }
};

#endif	/* TIMEMANAGER_H */

