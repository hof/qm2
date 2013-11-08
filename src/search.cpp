#include "search.h"
#include <cstdlib>
#include <iostream>
#include <string.h>

void TSearch::poll() {
    nodesUntilPoll = NODESBETWEENPOLLS;
    stopSearch = (timeManager->timeIsUp() && (!ponder || (outputHandler && outputHandler->enginePonder == false)))
            || (outputHandler && outputHandler->engineStop == true)
            || (maxNodes > 0 && nodes > maxNodes);
}

std::string TSearch::getPVString() {
    std::string result = "";
    for (int i = 0; i < stack->pvCount; i++) {
        result += stack->pvMoves[i].asString() + " ";
    }
    return result;
}

void TSearch::initLMR() {
    const bool PV = 1;
    const bool ACTIVE = 1;
    memset(LMR, 0, sizeof (LMR));
    for (int d = 3; d < 256; d++) {
        for (int m = 2; m < 64; m++) {
            int r = BSR(m - 1) + BSR(d - 2) - 2 - (m < 4) - (m < 8);
            r += (m > 16 && d > 16);
            r += (m > 24 && d > 16);
            r += r == 1;
            r = MIN(r, d - 2);
            r = MAX(0, r);
            int rmore = 1 + r + (r >> 1);
            rmore += rmore == 1;
            rmore = MIN(rmore, d - 2);
            r = m > 2 ? r : 0;
            int r_active = MAX(0, MIN(r - 2, d - 4));
            int rmore_active = MAX(r_active, MIN(rmore - 2, d - 4));
            LMR[0][PV][m][d] = r;
            LMR[ACTIVE][PV][m][d] = r_active;
            LMR[0][0][m][d] = rmore;
            LMR[ACTIVE][0][m][d] = rmore_active;

        }
    }
}

int TSearch::initRootMoves() {
    int result = 0;
    root.MoveCount = 0;
    root.FiftyCount = pos->boardFlags->fiftyCount;
    setNodeType(-SCORE_INFINITE, SCORE_INFINITE);
    hashTable->repStore(this, pos->boardFlags->hashCode, pos->boardFlags->fiftyCount);
    hashTable->ttLookup(this, 0, -SCORE_INFINITE, SCORE_INFINITE);
    for (TMove * move = movePicker->pickFirstMove(this, 0, -SCORE_INFINITE, SCORE_INFINITE);
            move; move = movePicker->pickNextMove(this, 0, -SCORE_INFINITE, SCORE_INFINITE)) {
        TRootMove * rMove = &root.Moves[root.MoveCount++];
        rMove->init(move, 1000 - root.MoveCount, pos->givesCheck(move), pos->active(move), pos->SEE(move));
        if (rMove->GivesCheck) {
            rMove->checkerSq = (pos->boardFlags + 1)->checkerSq;
            rMove->checkers = (pos->boardFlags + 1)->checkers;
        }
    }
    root.InCheck = pos->inCheck();
    stack->hashCode = pos->boardFlags->hashCode;
    stack->reduce = 0;
    stack->eval_result = SCORE_INVALID;
    result = root.MoveCount;
    return result;
}

/*
 * Sort moves (simple insertion sort)
 */
void TRoot::sortMoves() {
    for (int j = 1; j < MoveCount; j++) {
        TRootMove rMove = Moves[j];
        int i = j - 1;
        while (i >= 0 && rMove.compare(&Moves[i]) > 0) {
            Moves[i + 1] = Moves[i];
            i--;
        }
        Moves[i + 1] = rMove;
    }
}

int TSearch::pvs_root(int alpha, int beta, int depth) {
    /*
     * Principle variation search (PVS). 
     * Generate the first move from pv, hash or internal iterative deepening,
     * all handled by the movepicker. 
     * If no move is returned, the position is either MATE or STALEMATE, 
     * otherwise search the first move with full alpha beta window.
     */
    assert(root.MoveCount > 0);
    int nodesBeforeMove = nodes;
    TRootMove * rMove = &root.Moves[0];
    forward(&rMove->Move, rMove->GivesCheck);
    if (rMove->GivesCheck) {
        pos->boardFlags->checkerSq = rMove->checkerSq;
        pos->boardFlags->checkers = rMove->checkers;
    }
    int bestScore = -pvs(-beta, -alpha, depth - ONE_PLY);
    backward(&rMove->Move);
    int sortBaseScoreForPV = 1000 * depth / ONE_PLY;
    rMove->Nodes += nodes - nodesBeforeMove;
    rMove->PV = sortBaseScoreForPV;
    rMove->Value = bestScore;
    if (stopSearch) {
        return alpha;
    } else if (bestScore > alpha) {
        if (bestScore >= beta) {
            if (!rMove->Move.equals(&stack->pvMoves[0])) {
                stack->pvMoves[0].setMove(&rMove->Move);
                stack->pvCount = 1;
            }
            return bestScore;
        }
        updatePV(&rMove->Move);
        if (outputHandler && !rMove->Move.equals(&stack->pvMoves[0])) {
            outputHandler->sendPV(bestScore,
                    depth / ONE_PLY,
                    selDepth,
                    nodes,
                    timeManager->elapsed(),
                    getPVString().c_str(),
                    bestScore <= alpha ? FAILLOW : bestScore >= beta ? FAILHIGH : EXACT);
        }
        alpha = bestScore;
    }

    /*
     * Search remaining moves with a zero width window
     */
    stack->bestMove.setMove(&rMove->Move);
    for (int i = 1; i < root.MoveCount; i++) {
        rMove = &root.Moves[i];
        nodesBeforeMove = nodes;
        forward(&rMove->Move, rMove->GivesCheck);
        if (rMove->GivesCheck) {
            pos->boardFlags->checkerSq = rMove->checkerSq;
            pos->boardFlags->checkers = rMove->checkers;
        }
        int score = -pvs(-alpha - 1, -alpha, depth - ONE_PLY);
        if (score > alpha && score < beta && stopSearch == false) {
            score = -pvs(-beta, -alpha, depth - ONE_PLY);
        }
        backward(&rMove->Move);
        rMove->Nodes += nodes - nodesBeforeMove;
        rMove->Value = score;
        if (stopSearch) {
            return alpha;
        } else if (score > bestScore && score > alpha) {
            rMove->PV = sortBaseScoreForPV + i;
            if (score >= beta) {
                if (!rMove->Move.equals(&stack->pvMoves[0])) {
                    stack->pvMoves[0].setMove(&rMove->Move);
                    stack->pvCount = 1;
                }
                return score;
            }
            bestScore = score;
            stack->bestMove.setMove(&rMove->Move);
            updatePV(&rMove->Move);
            if (!stack->bestMove.equals(&stack->pvMoves[0]) && outputHandler) {
                outputHandler->sendPV(bestScore,
                        depth / ONE_PLY,
                        selDepth,
                        nodes,
                        timeManager->elapsed(),
                        getPVString().c_str(),
                        bestScore <= alpha ? FAILLOW : bestScore >= beta ? FAILHIGH : EXACT);
            }
            if (bestScore > alpha) {
                alpha = bestScore;
            }
        } else {
            rMove->PV >>= 5;
        }
    }
    return bestScore;
}

/**
 * Principle Variation Search (fail-soft)
 * @param searchData search metadata including the position
 * @param alpha lowerbound value
 * @param beta upperbound value
 * @param depth remaining search depth
 * @return score for the current node
 */
int TSearch::pvs(int alpha, int beta, int depth) {

    /*
     * 1. If no more depth remaining, return quiescence value
     */
    if (depth < ONE_PLY) {
        stack->pvCount = 0;
        if (pos->currentPly > selDepth) {
            selDepth = pos->currentPly;
        }
        int score = qsearch(alpha, beta, 0, QCHECKDEPTH);
        return score;
    }
    nodes++;

    /* 
     * 2. Mate distance pruning: 
     * if we already know we will win/loose by mate in n, 
     * it is not needed to search deeper than n
     */
    if ((SCORE_MATE - pos->currentPly) < beta) {
        beta = SCORE_MATE - pos->currentPly;
        if (alpha >= (SCORE_MATE - pos->currentPly)) {
            return SCORE_MATE - pos->currentPly;
        }
    }
    if ((-SCORE_MATE + pos->currentPly) > alpha) {
        alpha = -SCORE_MATE + pos->currentPly;
        if (beta <= (-SCORE_MATE + pos->currentPly)) {
            return -SCORE_MATE + pos->currentPly;
        }
    }

    if (nodesUntilPoll <= 0) {
        poll();
    }

    if (stopSearch || pos->currentPly >= MAX_PLY) {
        return alpha;
    }

    /*
     * 3. Return obvious draws 
     */
    if (pos->boardFlags->fiftyCount > 1) {
        if (pos->boardFlags->fiftyCount >= 100) {
            return drawScore(); //draw by 50 reversible moves
        }
        int stopPly = pos->currentPly - pos->boardFlags->fiftyCount;
        for (int ply = pos->currentPly - 4; ply >= stopPly; ply -= 2) { //draw by repetition
            if (ply >= 0 && getStack(ply)->hashCode == pos->boardFlags->hashCode) {
                return drawScore();
            } else if (ply < 0 && hashTable->repTable[root.FiftyCount + ply] == pos->boardFlags->hashCode) {
                return drawScore();

            }
        }
    }

    bool inCheck = stack->inCheck;
    if (!inCheck && pos->isDraw()) { //draw by no mating material
        stack->pvCount = 0;
        return drawScore();
    }

    /*
     * 4. Transposition table lookup
     */
    assert(depth >= 0);
    hashTable->ttLookup(this, depth, alpha, beta);
    if (stack->ttScore != TT_EMPTY && excludedMove.piece == EMPTY) {
        stack->pvCount = 0;
        return stack->ttScore;
    }

    /*
     * 5. Nullmove pruning
     */
    stack->hashCode = pos->boardFlags->hashCode;
    int type = setNodeType(alpha, beta);
    assert(type != UNKNOWN);
    assert(type == PVNODE || alpha + 1 == beta);
    int eval = evaluate(this, alpha, beta);
    int extend = 0;
    if (skipNull) {
        skipNull = false;
    } else if (depth > ONE_PLY
            && !inCheck
            && eval >= beta
            && ABS(beta) < SCORE_MATE - MAX_PLY
            && pos->hasPieces(pos->boardFlags->WTM)) {
        int rdepth = depth - (3 * ONE_PLY) - (depth >> 2);
        rdepth -= (eval - beta) > (VPAWN / 2);
        rdepth -= (eval - beta) > VPAWN;
        if (stack->phase >= 14) {
            rdepth = depth - 2 * ONE_PLY - (depth >> 3);
        }
        forward();
        int score = -pvs(-beta, -alpha, rdepth);
        backward();
        if (score >= beta) {
            if (score >= SCORE_MATE - MAX_PLY) {
                score = beta; //do not return unproven mate scores
            }
            return score;
        } else if (alpha > (-SCORE_MATE + MAX_PLY) && score < (-SCORE_MATE + MAX_PLY)) {
            if ((stack - 1)->reduce > 0) {
                return alpha; //fully undo the reduction
            }
            extend = HALF_PLY;
        } else if ((stack - 1)->reduce > 0) {
            TMove * threat = &(stack + 1)->bestMove;
            TMove * prev = &(stack - 1)->move;
            if (prev->ssq && threat->tsq || prev->tsq == threat->ssq) {
                extend = ((stack - 1)->reduce >> 1) + HALF_PLY; //(partly) undo the reduction
            }
        }
    }

    /*
     * 7. Principle variation search (PVS). 
     * Generate the first move from pv, hash or internal iterative deepening,
     * all handled by the movepicker. 
     * If no move is returned, the position is either MATE or STALEMATE, 
     * otherwise search the first move with full alpha beta window.
     */
    TMove * firstMove = movePicker->pickFirstMove(this, depth, alpha, beta);
    if (!firstMove) { //no legal move: it's checkmate or stalemate
        stack->pvCount = 0;
        return inCheck ? -SCORE_MATE + pos->currentPly : drawScore();
    }

    /*
     * 8. Node extensions
     * a) Endgame extension: increase depth with two ply when reaching pawns/kings endgame
     * b) check extension
     */
    assert(stack->phase >= 0 && stack->phase <= 16);
    if (stack->phase == 16
            && pos->boardFlags->fiftyCount == 0
            && pos->pawnsAndKings() == pos->allPieces) {
        TMove * previous = &(stack - 1)->move;
        if (previous->capture && previous->capture != WPAWN && previous->capture != BPAWN) {
            extend += 2 * ONE_PLY;
        }
    }
    if (inCheck && extend < ONE_PLY) {
        extend++;
        if (extend < ONE_PLY && movePicker->countEvasions(this, firstMove) <= 2) {
            extend++;
        };
    }

    /*
     * 9. Move based extensions.
     * These are applied to the first (mostly best) move only.
     * a) checks
     * b) pawn push to 7th and promotions
     * c) high depth extensions: captures
     */

    int givesCheck = pos->givesCheck(firstMove);
    int extendMove = (bool)givesCheck && extend < ONE_PLY;
    extendMove += extend == 0 && extendMove == 0 && pos->push7th(firstMove);
    extendMove += extend == 0 && extendMove == 0 && type == PVNODE && bool(firstMove->capture);

    stack->bestMove.setMove(firstMove);
    stack->reduce = 0;
    forward(firstMove, givesCheck);
    int bestScore = -pvs(-beta, -alpha, depth - ONE_PLY + extend + extendMove);
    backward(firstMove);
    if (bestScore > alpha) {
        if (bestScore >= beta) {
            hashTable->ttStore(this, firstMove->asInt(), bestScore, depth, alpha, beta);
            if (!firstMove->capture && !firstMove->promotion) {
                if (bestScore < SCORE_MATE - MAX_PLY) {
                    updateKillers(firstMove);
                } else {
                    stack->mateKiller.setMove(firstMove);
                }
                updateHistoryScore(firstMove, depth);
            }
            return bestScore;
        }
        updatePV(firstMove);
        alpha = bestScore;
    }

    /*
     * 10. Search remaining moves with a zero width window, in the following order:
     * - Captures and promotions with a positive SEE value 
     * - Killer Moves
     * - Castling Moves
     * - Non captures with a positive static score
     * - All remaining moves
     */
    int searchedMoves = 1;
    int new_depth = depth - ONE_PLY + extend;
    while (TMove * move = movePicker->pickNextMove(this, depth, alpha, beta)) {
        assert(stack->bestMove.equals(move) == false);
        assert(firstMove->equals(move) == false);

        /*
         * 11. Ignore moves that lead to draw by repetition
         * if alpha is better than draw
         */
        if (pos->boardFlags->fiftyCount >= 2
                && alpha > drawScore()
                && pos->currentPly >= 2) {
            TMove * myLastMove = &(stack - 2)->move;
            if (myLastMove->tsq == move->ssq
                    && myLastMove->ssq == move->tsq) {
                continue;
            }
        }
        givesCheck = pos->givesCheck(move);
        int reduce = 0;

        /*
         * 12. Late Move Reductions (LMR) 
         */
        if (stack->moveList.stage >= STOP
                && !move->capture
                && !inCheck
                && !givesCheck
                && new_depth > ONE_PLY
                && !move->promotion) {
            assert(new_depth < 256);
            bool active = pos->active(move);
            reduce += LMR[active][type == PVNODE][MIN(63, searchedMoves)][new_depth];
            reduce += (reduce < (new_depth - 2) && type != PVNODE && history[move->piece][move->tsq] < 0);
            reduce += (reduce < (new_depth - 4) && type == CUTNODE) * ONE_PLY;
        }

        stack->reduce = reduce;
        forward(move, givesCheck);
        int score = -pvs(-alpha - 1, -alpha, new_depth - reduce);
        if (score > alpha && reduce) { //full depth research without reductions
            (stack - 1)->reduce = 0;
            score = -pvs(-alpha - 1, -alpha, new_depth);
        }
        if (score > alpha && score < beta) { //full window research
            (stack - 1)->reduce = 0;
            score = -pvs(-beta, -alpha, new_depth);
        }
        backward(move);
        if (score > bestScore) {
            stack->bestMove.setMove(move);
            if (score >= beta) {
                hashTable->ttStore(this, move->asInt(), score, depth, alpha, beta);
                if (!move->capture && !move->promotion) {
                    if (bestScore < SCORE_MATE - MAX_PLY) {
                        updateKillers(move);
                    } else {
                        stack->mateKiller.setMove(move);
                    }
                    updateHistoryScore(move, depth);
                    int xMoves = 0;
                    for (TMove * cur = stack->moveList.first;
                            cur != stack->moveList.last; cur++) {
                        if (cur->score == MOVE_EXCLUDED && cur != move) {
                            updateHistoryScore(cur, -depth);
                        }
                    }
                    assert(xMoves <= searchedMoves);
                }
                return score;
            }
            bestScore = score;
            if (bestScore > alpha) {
                updatePV(move);
                alpha = bestScore;
            }
        }
        searchedMoves++;
    }
    hashTable->ttStore(this, stack->bestMove.asInt(), bestScore, depth, alpha, beta);
    return bestScore;
}

/*
 * Quiescence search
 */
static const int MAXPOSGAIN = 2 * VPAWN;

int TSearch::qsearch(int alpha, int beta, int qPly, int maxCheckPly) {

    nodes++;

    if (--nodesUntilPoll <= 0) {
        poll();
    }

    if (stopSearch || pos->currentPly >= MAX_PLY) {
        return alpha;
    }

    //return obvious draws
    if (pos->boardFlags->fiftyCount > 1) {
        if (pos->boardFlags->fiftyCount >= 100) {
            return drawScore();
        }
        int stopPly = pos->currentPly - pos->boardFlags->fiftyCount;
        for (int ply = pos->currentPly - 4; ply >= stopPly; ply -= 2) {
            if (ply >= 0 && getStack(ply)->hashCode == pos->boardFlags->hashCode) {
                return drawScore();
            } else if (ply < 0 && hashTable->repTable[root.FiftyCount + ply] == pos->boardFlags->hashCode) {
                return drawScore();
            }
        }
    }

    //mate distance pruning
    if ((SCORE_MATE - pos->currentPly) < beta) {
        beta = SCORE_MATE - pos->currentPly;
        if (alpha >= (SCORE_MATE - pos->currentPly)) {
            return SCORE_MATE - pos->currentPly;
        }
    }
    if ((-SCORE_MATE + pos->currentPly) > alpha) {
        alpha = -SCORE_MATE + pos->currentPly;
        if (beta <= (-SCORE_MATE + pos->currentPly)) {
            return -SCORE_MATE + pos->currentPly;
        }
    }

    //if not in check, generate captures, promotions and (upto some plies ) quiet checks
    if (!stack->inCheck) {

        if (pos->isDraw()) { //return obvious draws
            return drawScore();
        }

        int score = evaluate(this, alpha, beta);
        if (score >= beta) { //return evaluation score is it's already above beta
            return score;
        }

        TMove * move = movePicker->pickFirstQuiescenceMove(this, qPly < maxCheckPly, alpha, beta);
        if (!move) { //return evalation score if there are quiescence moves
            return score;
        }
        alpha = MAX(score, alpha);
        stack->hashCode = pos->boardFlags->hashCode;
        int base = score;
        do { //loop through quiescence moves
            int givesCheck = pos->givesCheck(move);

            //pruning (delta futility and negative see), 
            //skipped for pv nodes and checks
            if (!givesCheck && NOTPV(alpha, beta)) {
                //1. delta futility pruning: the captured piece + max. positional gain should raise alpha
                int gain = PIECE_VALUE[move->capture];
                if (move->promotion) {
                    if (move->promotion != WQUEEN && move->promotion != BQUEEN) {
                        continue;
                    }
                    gain += PIECE_VALUE[move->promotion] - VPAWN;
                }
                if (base + gain + MAXPOSGAIN < alpha) {
                    continue;
                }

                //2. prune moves when SEE is negative
                if (pos->SEE(move) < 0) {
                    continue;
                }
            }
            int newMaxChecks = maxCheckPly;
            if (newMaxChecks > 0) {
                if (!givesCheck) {
                    //stop searching for deep mates 
                    newMaxChecks = 1;
                } else if ((pos->boardFlags->WTM && pos->whiteQueens == 0)
                        || (pos->boardFlags->WTM == 0 && pos->blackQueens == 0)) {
                    //greatly reduce searching for deep mates if we have no queens
                    newMaxChecks >>= (1 + NOTPV(alpha, beta));
                }
            }
            forward(move, givesCheck);
            score = -qsearch(-beta, -alpha, qPly + 1, newMaxChecks);
            backward(move);
            if (score >= beta) {
                stack->bestMove.setMove(move);
                return beta;
            } else if (score > alpha) {
                stack->bestMove.setMove(move);
                alpha = score;
            }
        } while (move = movePicker->pickNextMove(this, qPly < maxCheckPly, alpha, beta));
    } else { // in check
        assert(pos->boardFlags->checkers);
        int score = -SCORE_INFINITE;
        TMove * move = movePicker->pickFirstQuiescenceMove(this, qPly < maxCheckPly, alpha, beta);
        if (!move) { //checkmate
            return -SCORE_MATE + pos->currentPly;
        }
        int newMaxChecks = maxCheckPly;
        if (newMaxChecks > 0) {
            if (move->capture && pos->SEE(move) >= 0) {
                newMaxChecks = 1;
            } else if (movePicker->countEvasions(this, move) > 2) {
                newMaxChecks >>= (1 + NOTPV(alpha, beta));
            }
        }
        stack->hashCode = pos->boardFlags->hashCode;
        do {
            bool givesCheck = pos->givesCheck(move);
            forward(move, givesCheck);
            score = -qsearch(-beta, -alpha, qPly + 1, newMaxChecks);
            backward(move);
            if (score >= beta) {
                stack->bestMove.setMove(move);
                return beta;
            } else if (score > alpha) {
                stack->bestMove.setMove(move);
                alpha = score;
            }
        } while (move = movePicker->pickNextMove(this, qPly < maxCheckPly, alpha, beta));
    }
    return alpha;
}

void TSearch::debug_print_search(int alpha, int beta) {
    std::cout << "print search (" << alpha << ", " << beta << "): " << std::endl;

    TBoard pos2;
    memcpy(&pos2, pos, sizeof (TBoard));
    while (pos2.currentPly > 0) {
        TMove * move = &getStack(pos2.currentPly - 1)->move;
        move->piece ? pos2.backward(move) : pos2.backward();
    }

    std::cout << "ROOT FEN: " << pos2.asFen() << std::endl;
    std::cout << "Path ";
    for (int i = 0; i < pos->currentPly; i++) {
        std::cout << " " << getStack(i)->move.asString();
        if (getStack(i)->reduce) {
            std::cout << "(r)";
        }
    }
    std::cout << "\nFEN: " << pos->asFen() << std::endl;
    std::cout << "Hash: " << pos->boardFlags->hashCode << std::endl;
    std::cout << "Nodes: " << nodes << std::endl;
    std::cout << "Skip nullmove: " << this->skipNull << std::endl;

    std::cout << std::endl;
    exit(0);
}
