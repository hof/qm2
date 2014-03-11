#include "defs.h"
#include "timemanager.h"

void TTimeManager::set(unsigned int myTime, int oppTime, int myInc, int oppInc, int movesLeft) {
    static const int OVERHEAD_TIME = 500; //reserve 500ms for overhead
    int MOVES_LEFT = 30; //assume the game ends in this amount of moves
    static const double DIFFICULT_POSITION_FACTOR = 2.0; //if needed, double the time
    int movesToGo = movesLeft ? MIN(movesLeft + 1, MOVES_LEFT) : MOVES_LEFT;
    int limit = myTime - OVERHEAD_TIME;

    /*
     * Base time is our time left shared by the amount of moves left
     */
    int timeForThisMove = limit / movesToGo;

    /*
     * Bonus/Penalty time when we have more/less time left than the opponent
     */
    if (myTime > oppTime && oppTime > 0 && myInc >= oppInc && movesToGo > 2) {
        double factor = MIN(10.0, (1.0 * myTime) / (1.0 * oppTime));
        timeForThisMove = factor * timeForThisMove;
    } else if (myTime < oppTime && oppTime > 0 && myInc <= oppInc && movesToGo >= MOVES_LEFT) {
        double factor = MAX(0.25, (1.0 * myTime) / (1.0 * oppTime));
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
    int maxTimeForThisMove = MIN(timeForThisMove*DIFFICULT_POSITION_FACTOR, limit);
    this->setEndTime(timeForThisMove);
    this->setMaxTime(maxTimeForThisMove);
}
