#include "defs.h"
#include "timemanager.h"

void TTimeManager::set(unsigned int myTime, int oppTime, int myInc, int oppInc, int movesLeft) {
    static const int OVERHEAD_TIME = 500; //reserve 500ms for overhead
    int MOVES_UNTIL_DECIDED = 15; //assume we'll have a winning position within this amount of moves from now
    if (myInc > 0) {
        MOVES_UNTIL_DECIDED = 10;
    }
    static const double DIFFICULT_POSITION_FACTOR = 2.5;
    int movesToGo = movesLeft? MIN(movesLeft+1, MOVES_UNTIL_DECIDED) : MOVES_UNTIL_DECIDED;
    int limit = myTime - OVERHEAD_TIME;
    if (movesLeft == 1) {
        limit -= OVERHEAD_TIME; //to absolutely avoid loosing on time
    }
    
    /*
     * Base time is our time left shared by the amount of moves left
     */
    int timeForThisMove = limit/movesToGo;
    
    /*
     * Bonus time when we have an increment available
     */
    if (myInc) {
        timeForThisMove += (4*myInc)/5;
    }
    /*
     * Bonus time when we have more time left than the opponent
     */
    if (myTime > oppTime && myInc >= oppInc && movesToGo > 2 && timeForThisMove > 2000) {
        timeForThisMove += (myTime-oppTime)/4;
    }
    
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
