/* 
 * File:   timemanager.h
 * Author: Hajewiet
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

    int NormalizeTime(unsigned int timeInMs) {
        return (U64(timeInMs) * 1000) / CLOCKS_PER_SEC;
    }

    void setStartTime() {
        startTime = clock();
    }

    void setMaxTime(unsigned int timeInMs) {
        maxEndTime = startTime + NormalizeTime(timeInMs);
    }

    void setEndTime(unsigned int timeInMs) {
        endTime = startTime + NormalizeTime(timeInMs);
    }

    void set(unsigned int myTime, int oppTime, int myInc, int oppInc, int movesLeft);

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

