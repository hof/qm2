#include "movepicker.h"
#include "search.h"
#include "move.h"

void TMovePicker::push(TSearch * searchData, TMove * move, int score) {
    TMoveList * list = &searchData->stack->moveList;
    for (TMove * m = list->first; m != list->last; m++) {
        if (move->equals(m)) {
            m->score = score;
            return;
        }
    }
    TMove * current = list->last;
    (current++)->setMove(move);
    move->score = score;
    list->last = current;
}

inline TMove * TMovePicker::popBest(TBoard * pos, TMoveList * list) {
    if (list->first == list->last) {
        return NULL;
    }
    int edge = list->minimumScore;
    do {
        TMove * best = list->first;
        for (TMove * move = best + 1; move != list->last; move++) {
            if (move->score > best->score) {
                best = move;
            }
        }
        if (best->score < edge) {
            return NULL;
        }
        if (pos->legal(best)) {
            best->score = MOVE_EXCLUDED;
            return best;
        }
        best->score = MOVE_ILLEGAL;
    } while (true);
    return NULL;
}

TMove * TMovePicker::pickFirstMove(TSearch * searchData, int depth, int alpha, int beta) {
    TMoveList * moveList = &searchData->stack->moveList;
    moveList->clear();
    moveList->stage = HASH1;
    searchData->stack->captureMask = searchData->pos->all_pieces;
    if (searchData->excludedMove.piece != EMPTY) {
        moveList->lastX++->setMove(&searchData->excludedMove);
    }
    return pickNextMove(searchData, depth, alpha, beta);
}

TMove * TMovePicker::pickFirstQuiescenceMove(TSearch * searchData, int qCheckDepth, int alpha, int beta) {
    TMoveList * moveList = &searchData->stack->moveList;
    moveList->clear();
    moveList->stage = Q_CAPTURES; //q_hash1
    searchData->stack->captureMask = searchData->pos->all_pieces;
    return pickNextMove(searchData, qCheckDepth, alpha, beta);
}

TMove * TMovePicker::pickNextMove(TSearch * searchData, int depth, int alpha, int beta) {
    U64 mask;
    TMoveList * moveList = &searchData->stack->moveList;
    TBoard * pos = searchData->pos;

    /*
     * 1. If there are moves present in the moveList, this means
     * they are already scored by the Movepicker. They are also valid (semilegal) 
     * and we can return the best one. 
     * A last-minute legality check is performed so the movepicker 
     * always returns fully legal moves.
     */
    TMove * result = popBest(pos, moveList);

    /*
     * 2. Proceed to the next stage if no move was found
     */
    if (!result) {
        switch (moveList->stage) {
            case HASH1:
                /*
                 * Return the hashmove from depth-preferred table
                 */
                result = &searchData->stack->ttMove1;
                if (result->piece && !moveList->excluded(result)) {
                    moveList->stage = HASH2;
                    moveList->lastX++->setMove(result);
                    return result;
                }
            case HASH2:
                /*
                 * Return the hashmove from always-replace table
                 */
                result = &searchData->stack->ttMove2;
                if (result->piece && !moveList->excluded(result)) {
                    moveList->stage = MATEKILLER;
                    moveList->lastX++->setMove(result);
                    return result;
                }
            case IID:
                if (depth > LOW_DEPTH && moveList->firstX == moveList->lastX) {
                    //find a good first move to try by doing a shallow search
                    bool skipNull = searchData->skipNull;
                    searchData->skipNull = true;
                    int iid_depth = depth - (2 * ONE_PLY) - (depth >> 2); //(3 * ONE_PLY);
                    iid_depth -= searchData->stack->nodeType != PVNODE;
                    iid_depth -= searchData->stack->nodeType == ALLNODE;
                    iid_depth = MAX(ONE_PLY, iid_depth);
                    int iid_score = searchData->pvs(alpha, beta, iid_depth);
                    searchData->skipNull = skipNull;
                    searchData->hashTable->ttLookup(searchData, iid_depth, alpha, beta);
                    moveList->clear();
                    result = &searchData->stack->ttMove1;
                    if (result->piece == EMPTY) {
                        result = &searchData->stack->ttMove2;
                    }
                    if (result->piece) {
                        /*
                         * Re-clear the movelist and return IID move
                         */
                        moveList->lastX++->setMove(result);
                        moveList->stage = MATEKILLER;
                        return result;
                    }
                    //at this point, the position must be (stale)mate as no move was found
                    //or the search was aborted.. continue anyway (robust))
                    moveList->stage = MATEKILLER; //this line is kept to place a breakpoint. todo: assert (stale)mate
                }
            case MATEKILLER:
                result = &searchData->stack->mateKiller;
                if (result->piece
                        && !moveList->excluded(result)
                        && pos->valid(result)
                        && pos->legal(result)) {
                    moveList->stage = CAPTURES;
                    moveList->lastX++->setMove(result);
                    return result;
                }
            case CAPTURES:
                mask = pos->all_pieces;
                if (searchData->stack->inCheck) {
                    mask &= pos->stack->checkers;
                }
                genCaptures(pos, moveList, mask);
                if (moveList->current != moveList->last) {
                    for (TMove * move = moveList->current; move != moveList->last; move++) {
                        if (moveList->excluded(move)) {
                            move->score = MOVE_EXCLUDED;
                        } else {
                            move->score = depth > LOW_DEPTH ? pos->SEE(move) : MVVLVA(move);
                        }
                    }
                    result = popBest(pos, moveList);
                    if (result) {
                        moveList->stage = PROMOTIONS;
                        return result;
                    }
                }
            case PROMOTIONS:
                genPromotions(pos, moveList);
                if (moveList->current != moveList->last) {
                    for (TMove * move = moveList->current; move != moveList->last; move++) {
                        if (moveList->excluded(move)) {
                            move->score = MOVE_EXCLUDED;
                        } else if ((move->promotion == WQUEEN || move->promotion == BQUEEN)
                                && pos->SEE(move) >= 0) {
                            move->score = 800;
                        } else {
                            move->score = -move->piece;
                        }
                    }
                    result = popBest(pos, moveList);
                    if (result) {
                        moveList->stage = KILLER1;
                        return result;
                    }
                }
            case KILLER1:
                result = &searchData->stack->killer1;
                if (result->piece
                        && pos->valid(result)
                        && !moveList->excluded(result)
                        && pos->legal(result)) {
                    moveList->lastX++->setMove(result);
                    moveList->stage = KILLER2;
                    assert(result->capture == EMPTY && result->promotion == EMPTY);
                    return result;
                }
            case KILLER2:
                result = &searchData->stack->killer2;
                if (result->piece
                        && pos->valid(result)
                        && !moveList->excluded(result)
                        && pos->legal(result)) {
                    moveList->lastX++->setMove(result);
                    moveList->stage = MINORPROMOTIONS;
                    assert(result->capture == EMPTY && result->promotion == EMPTY);
                    return result;
                }
            case MINORPROMOTIONS: //and captures with see < 0
                if (depth <= LOW_DEPTH) {
                    moveList->minimumScore = -MOVE_INFINITY;
                    result = popBest(pos, moveList);
                    if (result) {
                        moveList->stage = CASTLING;
                        return result;
                    }
                }
            case CASTLING:
                genCastles(pos, moveList);
                if (moveList->current != moveList->last) {
                    for (TMove * move = moveList->current; move != moveList->last; move++) {
                        if (moveList->excluded(move)) {
                            move->score = MOVE_EXCLUDED;
                        } else {
                            move->score = 100;
                        }
                    }
                    result = popBest(pos, moveList);
                    if (result) {
                        moveList->stage = QUIET_MOVES;
                        return result;
                    }
                }
            case QUIET_MOVES:
                moveList->minimumScore = -MOVE_INFINITY;
                genQuietMoves(pos, moveList);
                for (TMove * move = moveList->current; move != moveList->last; move++) {
                    if (moveList->excluded(move)) {
                        move->score = MOVE_EXCLUDED;
                    } else {
                        move->score = searchData->history[move->piece][move->tsq];
                    }
                }
                moveList->stage = STOP;
                result = popBest(pos, moveList);
                return result;
            case Q_HASH1:
                /*
                 * Return the hashmove from depth-preferred table
                 */
                result = &searchData->stack->ttMove1;
                if (result->piece && !moveList->excluded(result)) {
                    moveList->stage = Q_HASH2;
                    moveList->lastX++->setMove(result);
                    return result;
                }
            case Q_HASH2:
                /*
                 * Return the hashmove from always-replace table
                 */
                result = &searchData->stack->ttMove2;
                if (result->piece && !moveList->excluded(result)) {
                    moveList->stage = Q_CAPTURES;
                    moveList->lastX++->setMove(result);
                    return result;
                }
            case Q_CAPTURES:
                mask = pos->all_pieces;
                if (searchData->stack->inCheck) {
                    mask &= pos->stack->checkers;
                }
                genCaptures(pos, moveList, mask);
                for (TMove * move = moveList->current; move != moveList->last; move++) {
                    move->score = MVVLVA(move);
                }
                result = popBest(pos, moveList);
                if (result) {
                    moveList->stage = Q_PROMOTIONS;
                    return result;
                }
            case Q_PROMOTIONS:
                genPromotions(pos, moveList);
                for (TMove * move = moveList->current; move != moveList->last; move++) {
                    move->score = MVVLVA(move);
                }
                result = popBest(pos, moveList);
                if (searchData->stack->inCheck) {
                    moveList->stage = Q_EVASIONS;
                } else if (depth > 0) {
                    moveList->stage = Q_QUIET_CHECKS;
                } else {
                    moveList->stage = STOP;
                    return result;
                }
                if (result) {
                    return result;
                }
            case Q_QUIET_CHECKS:
                if (moveList->stage == Q_QUIET_CHECKS) {
                    moveList->stage = STOP;
                    //genQuietChecks(pos, moveList);
                    genQuietMoves(pos, moveList);
                    for (TMove * move = moveList->current; move != moveList->last; move++) {
                        move->score = searchData->history[move->piece][move->tsq];
                    }
                    result = popBest(pos, moveList);
                    return result;
                }
            case Q_EVASIONS:
                assert(searchData->stack->inCheck);
                genQuietMoves(pos, moveList);
                for (TMove * move = moveList->first; move != moveList->last; move++) {
                    move->score = searchData->history[move->piece][move->tsq];
                }
                moveList->stage = STOP;
                moveList->minimumScore = -MOVE_INFINITY;
                result = popBest(pos, moveList);
                return result;
            case STOP:
            default:
                return NULL;
        }
    }
    return result;
}

short TMovePicker::countEvasions(TSearch * sd, TMove * firstMove) {
    assert(firstMove != NULL);
    int result = 1;
    const short MAXLEGALCOUNT = 3;
    TMove * pushback[MAXLEGALCOUNT];

    //get and count legal moves
    while (result < MAXLEGALCOUNT) {
        TMove * m = pickNextMove(sd, 1, -SCORE_INFINITE, SCORE_INFINITE);
        if (m == NULL) {
            break;
        }
        pushback[result++] = m;
    }

    //push moves back on the list
    for (int i = firstMove != NULL; i < result; i++) {
        push(sd, pushback[i], SCORE_INFINITE - i);
    }

    return result;
}

