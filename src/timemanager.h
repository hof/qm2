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

const int TIME_UNIT = CLOCKS_PER_SEC;
const unsigned int INFINITE_TIME = 24 * 60 * 60 * 1000;

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

    int NormalizeTime(int timeInMs) {
        return (U64(timeInMs) * 1000) / CLOCKS_PER_SEC;
    }

    void setStartTime() {
        startTime = clock();
    }

    void setMaxTime(int timeInMs) {
        maxEndTime = startTime + NormalizeTime(timeInMs);
    }

    void setEndTime(int timeInMs) {
        endTime = startTime + NormalizeTime(timeInMs);
    }

    void set(int myTime, int oppTime, int myInc, int oppInc, int movesLeft);

    inline bool timeIsUp() {
        return clock() >= endTime;
    }

    inline bool available(int ms) {
        return (clock() + ms) <= endTime;
    }

    inline int available() {
        return endTime - clock();
    }

    /**
     * Return elapsed time in ms
     * @return elapsed time in ms
     */
    inline int elapsed() {
        return (CLOCKS_PER_SEC * (clock() - startTime)) / CLOCKS_PER_SEC;
    }

    inline bool requestMoreTime() {
        int available = maxEndTime - endTime;
        if (available > 500) {
            endTime += (available / 2);
            return true;
        }
        endTime = maxEndTime;
        return false;
    }

    inline bool requestLessTime() {
        if (maxEndTime != endTime) {
            int available = endTime - startTime;
            if (available > 0) {
                endTime -= available / 2;
                return true;
            }
        }
        return false;
    }
};

#endif	/* TIMEMANAGER_H */

