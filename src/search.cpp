#include "search.h"
#include <cstdlib>
#include <iostream>
#include <string.h>

void TSearch::printMovePath() {
    for (int i = 0; i <= pos->currentPly - 1; i++) {
        std::cout << getStack(i)->move.asString() << " ";
    }
    std::cout << std::endl;
}

void TSearch::poll() {
    nodesUntilPoll = NODESBETWEENPOLLS;
    stopSearch = (timeManager->timeIsUp() && (!ponder || (outputHandler && outputHandler->enginePonder == false)))
            || (outputHandler && outputHandler->engineStop == true)
            || (maxNodes > 0 && nodes > maxNodes);
}

void TSearch::debugPrint() {
    std::cout << std::endl << "Debug Information: " << std::endl;
    std::cout << "FEN: " << pos->asFen() << std::endl;
    std::cout << "Hash: " << pos->boardFlags->hashCode << std::endl;
    std::cout << "Nodes: " << nodes << std::endl;
    std::cout << "Skip nullmove: " << this->skipNull << std::endl;
    std::cout << "Path: ";
    for (int i = 0; i <= pos->currentPly - 1; i++) {
        std::cout << getStack(i)->move.asString() << " ";
    }
    exit(0);
}

std::string TSearch::getPVString() {
    std::string result = "";
    for (int i = 0; i < stack->pvCount; i++) {
        result += stack->pvMoves[i].asString() + " ";
    }
    return result;
}

int TSearch::initRootMoves() {
    int result = 0;
    root.MoveCount = 0;
    root.FiftyCount = pos->boardFlags->fiftyCount;
    setNodeType(-SCORE_INFINITE, SCORE_INFINITE);
    hashTable->repStore(this, pos->boardFlags->hashCode, pos->boardFlags->fiftyCount);
    hashTable->ttLookup(this, 0, -SCORE_INFINITE, SCORE_INFINITE);
    for (TMove * move = movePicker->pickFirstMove(this, 0, -SCORE_INFINITE, SCORE_INFINITE, 0);
            move; move = movePicker->pickNextMove(this, 0, -SCORE_INFINITE, SCORE_INFINITE, 0)) {
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
    stack->evaluationScore = SCORE_INVALID;
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
    int extend = 0;

    forward(&rMove->Move, rMove->GivesCheck);
    if (rMove->GivesCheck) {
        extend = HALF_PLY;
        pos->boardFlags->checkerSq = rMove->checkerSq;
        pos->boardFlags->checkers = rMove->checkers;
    }
    int bestScore = -pvs(-beta, -alpha, depth - ONE_PLY + extend);
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
        if (!rMove->Move.equals(&stack->pvMoves[0]) && outputHandler) {
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

        extend = rMove->GivesCheck;
        nodesBeforeMove = nodes;

        forward(&rMove->Move, rMove->GivesCheck);
        if (rMove->GivesCheck) {
            pos->boardFlags->checkerSq = rMove->checkerSq;
            pos->boardFlags->checkers = rMove->checkers;
        }
        int score = -pvs(-alpha - 1, -alpha, depth - ONE_PLY + extend);
        if (score > alpha && score < beta && stopSearch == false) {
            score = -pvs(-beta, -alpha, depth - ONE_PLY + extend);
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

    if (stopSearch) {
        return alpha;
    }

    /* 
     * 1. Mate distance pruning: 
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

    /*
     * 2. If no more depth remaining, return quiescence value
     */
    if (depth <= HALF_PLY) {
        stack->pvCount = 0;
        if (pos->currentPly > selDepth) {
            selDepth = pos->currentPly;
        }
        int score = qsearch(alpha, beta, 0);
        return score;
    }
    nodes++;
    if (nodesUntilPoll <= 0) {
        poll();
    }


    /*
     * 3. Abort and return obvious draws 
     */
    if (pos->boardFlags->fiftyCount > 1) {
        if (pos->boardFlags->fiftyCount >= 100) {
            return SCORE_DRAW;
        }
        int stopPly = pos->currentPly - pos->boardFlags->fiftyCount;
        for (int ply = pos->currentPly - 4; ply >= stopPly; ply -= 2) {
            if (ply >= 0 && getStack(ply)->hashCode == pos->boardFlags->hashCode) {
                return SCORE_DRAW;
            } else if (ply < 0 && hashTable->repTable[root.FiftyCount + ply] == pos->boardFlags->hashCode) {
                return SCORE_DRAW;

            }
        }
    }

    if (pos->isDraw()) {
        stack->pvCount = 0;
        return SCORE_DRAW;
    }

    if (pos->currentPly >= MAX_PLY) {
        return alpha;
    }

    /*
     * 4. Transposition table lookup
     */
    int type = setNodeType(alpha, beta);
    depth = MAX(0, depth);
    hashTable->ttLookup(this, depth, alpha, beta);
    if (stack->ttScore != TT_EMPTY && excludedMove.piece == EMPTY) {
        stack->pvCount = 0;
        return stack->ttScore;
    }

    /*
     * 5. Null move pruning and razoring
     */
    stack->hashCode = pos->boardFlags->hashCode;
    assert(type != UNKNOWN);
    assert(type == PVNODE || alpha + 1 == beta);
    bool inCheck = stack->inCheck;
    int extendNode = 0;
    int eval = evaluate(this, alpha, beta);

    if (!skipNull
            && type != PVNODE
            && depth <= LOW_DEPTH
            && !inCheck
            && eval < beta - FUTILITY_MARGIN[depth]
            && beta < SCORE_MATE - MAX_PLY
            && pos->hasPieces(pos->boardFlags->WTM)
            && !pos->promotingPawns(pos->boardFlags->WTM)) {
        int rbeta = beta - FUTILITY_MARGIN[depth];
        int qvalue = qsearch(alpha, beta, 0);
        if (qvalue < rbeta) {
            return qvalue;
        }
    }

    if (!skipNull
            && type != PVNODE
            && depth <= LOW_DEPTH
            && !inCheck
            && eval >= beta
            && beta < SCORE_MATE - MAX_PLY
            && pos->hasPieces(pos->boardFlags->WTM)
            && eval - FUTILITY_MARGIN[depth] > beta) {
        return eval - FUTILITY_MARGIN[depth];
    }
    if (!skipNull
            && depth > ONE_PLY
            && !inCheck
            && eval >= beta
            && beta < SCORE_MATE - MAX_PLY
            && pos->hasPieces(pos->boardFlags->WTM)) {
        forward();
        int nullDepth = MAX(0, depth - ONE_PLY - NullReduction(depth, eval - beta));
        int score = -pvs(-beta, -alpha, nullDepth);
        backward();
        if (score >= beta) {
            if (score >= SCORE_MATE - MAX_PLY) {
                score = beta; //do not return unproven mate scores
            }
            return score;
        } else if (alpha > (-SCORE_MATE + MAX_PLY) && score < (-SCORE_MATE + MAX_PLY)) {
            extendNode = HALF_PLY; //mate threat extension
        }

        if ((stack - 1)->reduce && (eval - score) > VPAWN) {
            TMove * prevMove = &(stack - 1)->move;
            TMove * threatMove = &(stack + 1)->bestMove;
            if (threatMove->piece
                    && (prevMove->ssq == threatMove->tsq || prevMove->tsq == threatMove->ssq)) {
                extendNode = (stack - 1)->reduce;
            }
        }
    } else {
        skipNull = false;
    }


    /*
     * Endgame extension: 
     * Increase depth with two ply when reaching pawns/kings endgame
     */
    assert(stack->gamePhase >= 0 && stack->gamePhase <= 16);
    TMove * previous = &(stack - 1)->move;
    if (stack->gamePhase == 16
            && previous->capture
            && previous->capture != WPAWN
            && previous->capture != BPAWN
            && pos->pawnsAndKings() == pos->allPieces) {
        extendNode = 2 * ONE_PLY;
    }

    /*
     * 7. Principle variation search (PVS). 
     * Generate the first move from pv, hash or internal iterative deepening,
     * all handled by the movepicker. 
     * If no move is returned, the position is either MATE or STALEMATE, 
     * otherwise search the first move with full alpha beta window.
     */
    TMove * firstMove = movePicker->pickFirstMove(this, depth, alpha, beta, 0);
    if (!firstMove) {
        stack->pvCount = 0;
        return inCheck ? -SCORE_MATE + pos->currentPly : SCORE_DRAW;
    }

    /*
     * 8. Single reply to check extension.
     */
    if (inCheck && !firstMove->capture && extendNode < ONE_PLY) {
        TMove * move2 = movePicker->pickNextMove(this, depth, alpha, beta, 0);
        if (!move2) {
            extendNode = ONE_PLY;
        } else {
            TMove * move3 = movePicker->pickNextMove(this, depth, alpha, beta, 0);
            if (!move3) {
                extendNode = ONE_PLY;
            } else {
                movePicker->push(this, move3, SCORE_INFINITE - 3); //push move back on the list
            }
            movePicker->push(this, move2, SCORE_INFINITE - 2); //push move back on the list
        }
    }

    stack->bestMove.setMove(firstMove);
    int givesCheck = pos->givesCheck(firstMove);

    /*
     * 9. Move based extensions.
     * These are applied to the first (and supposedly best) move only.
     */
    int extendMove = 0;
    bool singular = false;
    if (!extendNode) {
        int see = pos->SEE(firstMove);
        if (givesCheck > 0) {
            extendMove = HALF_PLY;
            if (givesCheck == 2 || firstMove->capture || type == PVNODE) {
                extendMove = ONE_PLY;
            }
        } else if (firstMove->piece == WPAWN && firstMove->tsq >= a7 && firstMove->tsq <= h7) {
            extendMove = ONE_PLY; //promotion or pawn push to 7th rank
        } else if (firstMove->piece == BPAWN && firstMove->tsq <= h2 && firstMove->tsq >= a2) {
            extendMove = ONE_PLY; //promotion or pawn push to 7th rank
        } else if (depth > LOW_DEPTH && firstMove->capture && previous->capture && previous->tsq == firstMove->tsq) {
            extendMove = HALF_PLY; //recapture
            if (depth >= HIGH_DEPTH || type == PVNODE) {
                extendMove = ONE_PLY;
            }
        } else if (depth > LOW_DEPTH && firstMove->capture && see < 0) { //sacrifice
            extendMove = HALF_PLY;
            if (depth >= HIGH_DEPTH || type == PVNODE) {
                extendMove = ONE_PLY;
            }
        } else if (type == PVNODE
                && depth >= HIGH_DEPTH
                && stack->moveList.stage < CAPTURES
                && see == 0
                && excludedMove.piece == EMPTY) { //singular extension 
            if (firstMove->capture || firstMove->promotion || givesCheck || pos->checksPiece(firstMove)) {
                extendMove = HALF_PLY;
                singular = true;
            } else {
                stack->reduce = 0;
                forward(firstMove, givesCheck);
                int firstMoveScore = -pvs(-beta, -alpha, depth - 6 * ONE_PLY);
                backward(firstMove);
                if (firstMoveScore > alpha) {
                    tempList.copy(&stack->moveList);
                    excludedMove.setMove(firstMove);
                    HASH_EXCLUDED_MOVE(pos->boardFlags->hashCode);
                    int scoreX = pvs(alpha, beta, depth - 6 * ONE_PLY);
                    HASH_EXCLUDED_MOVE(pos->boardFlags->hashCode);
                    firstMove->setMove(&excludedMove);
                    stack->bestMove.setMove(firstMove);
                    excludedMove.clear();
                    stack->moveList.copy(&tempList);
                    if (scoreX < (firstMoveScore - (depth >> 1))) {
                        extendMove = HALF_PLY;
                        if (scoreX < firstMoveScore - depth) {
                            extendMove = ONE_PLY;
                        }
                        singular = true;
                    }
                }
            }
        }
        singular = singular || ((firstMove->capture || firstMove->promotion) && see > 0);

    }

    stack->reduce = 0;
    forward(firstMove, givesCheck);
        
    int bestScore = -pvs(-beta, -alpha, depth - ONE_PLY + extendNode + extendMove);
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
     * 10. Futility pruning
     * Prepare move generation by determining the futility pruning gap on low depths
     * If there is a gap, normal quiet moves are skipped and only checks are generated. 
     */
    int fGap = 0;
    if (!extendNode
            && !inCheck
            && type != PVNODE
            && depth <= LOW_DEPTH
            && eval + VKNIGHT < alpha
            && alpha > -SCORE_MATE + MAX_PLY
            && pos->hasPieces(pos->boardFlags->WTM)) {
        int fMargin = MIN(PIECE_VALUE[pos->topPiece(!pos->boardFlags->WTM)], FUTILITY_MARGIN[depth]);
        if (fMargin + eval < alpha) {
            fGap = alpha - eval + fMargin;
        }
    }

    /*
     * 11. Search remaining moves with a zero width window, in the following order:
     * - Captures and promotions with a positive SEE value 
     * - Killer Moves
     * - Castling Moves
     * - Non captures with a positive static score
     * - All remaining moves
     */

    int searchedMoves = 1;

    while (TMove * move = movePicker->pickNextMove(this, depth, alpha, beta, fGap)) {


        assert(stack->bestMove.equals(move) == false);
        assert(firstMove->equals(move) == false);

        /*
         * 12. Ignore moves that lead to draw by repetition
         * if alpha is better than draw
         */
        if (alpha > SCORE_DRAW
                && pos->boardFlags->fiftyCount >= 2
                && pos->currentPly >= 2) {
            TMove * myLastMove = &(stack - 2)->move;
            if (myLastMove->tsq == move->ssq
                    && myLastMove->ssq == move->tsq) {
                continue;
            }
        }

        givesCheck = pos->givesCheck(move);

        int phase = stack->moveList.stage;

        /*
         * 13. Pruning and reductions 
         */
        int reduce = 0;



        if (phase >= STOP /* remaining moves (no captures, castling, promotions) */
                && givesCheck == 0
                && !inCheck
                && !(move->piece == WPAWN && move->tsq >= a7 && move->tsq <= h7)
                && !(move->piece == BPAWN && move->tsq <= h2 && move->tsq >= a2)) {
            if (fGap
                    && !move->capture
                    && !move->promotion) {
                continue;
            }
            if (depth > ONE_PLY) {
                reduce += type != PVNODE;
                reduce += type == CUTNODE;
                reduce += singular;
                if (reduce > 0 && pos->active(move)) {
                    reduce = MAX(reduce, ONE_PLY);
                } else {
                    reduce = MIN(2 * ONE_PLY + reduce, BSR(searchedMoves + 1) + reduce);
                }
            }
        }

        extendMove = givesCheck > 0
                && !extendNode
                && (givesCheck > 1
                || phase < STOP
                || move->capture
                || move->promotion
                || type == PVNODE
                || pos->SEE(move) >= 0);

        stack->reduce = reduce;
        forward(move, givesCheck);
        int score = -pvs(-alpha - 1, -alpha, depth - ONE_PLY - reduce + extendMove + extendNode);
        if (score > alpha && reduce) { //full depth research
            (stack - 1)->reduce = 0;
            score = -pvs(-alpha - 1, -alpha, depth - ONE_PLY + extendMove + extendNode);
        }
        if (score > alpha && score < beta) {
            (stack - 1)->reduce = 0;
            score = -pvs(-beta, -alpha, depth - ONE_PLY + extendMove + extendNode);
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

int TSearch::qsearch(int alpha, int beta, int qPly) {

    if (stopSearch) {
        return alpha;
    }
    nodes++;
    if (--nodesUntilPoll <= 0) {
        poll();
    }

    //return obvious draws

    if (pos->boardFlags->fiftyCount > 1) {
        if (pos->boardFlags->fiftyCount >= 100) {
            return SCORE_DRAW;
        }
        int stopPly = pos->currentPly - pos->boardFlags->fiftyCount;
        for (int ply = pos->currentPly - 4; ply >= stopPly; ply -= 2) {
            if (ply >= 0 && getStack(ply)->hashCode == pos->boardFlags->hashCode) {
                return SCORE_DRAW;
            } else if (ply < 0 && hashTable->repTable[root.FiftyCount + ply] == pos->boardFlags->hashCode) {
                return SCORE_DRAW;
            }
        }
    }

    if (!stack->inCheck) {
        if (pos->isDraw()) {
            return SCORE_DRAW;
        }
        int score = evaluate(this, alpha, beta); //stand-pat score
        if (score >= beta || pos->currentPly >= MAX_PLY) { //fail high
            return score;
        }

        TMove * move = movePicker->pickFirstQuiescenceMove(this, qPly, alpha, beta, 0);
        if (!move) {
            return score;
        }
        alpha = MAX(score, alpha);
        stack->hashCode = pos->boardFlags->hashCode;

        int base = score;

        do {
            int givesCheck = pos->givesCheck(move);
            if (givesCheck == 0 && alpha + 1 >= beta) {
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

            forward(move, givesCheck);
            score = -qsearch(-beta, -alpha, qPly + 1);
            backward(move);
            if (score >= beta) {
                stack->bestMove.setMove(move);
                return beta;
            } else if (score > alpha) {
                stack->bestMove.setMove(move);
                alpha = score;
            }
        } while (move = movePicker->pickNextMove(this, qPly, alpha, beta, alpha - base));
    } else { // in check
        if (pos->currentPly >= MAX_PLY) {
            return alpha;
        }
        assert(pos->boardFlags->checkers);
        int score = -SCORE_INFINITE;

        TMove * move = movePicker->pickFirstQuiescenceMove(this, qPly, alpha, beta, 0);
        if (!move) {
            return -SCORE_MATE + pos->currentPly;
        }
        stack->hashCode = pos->boardFlags->hashCode;
        do {
            bool givesCheck = pos->givesCheck(move);
            forward(move, givesCheck);
            score = -qsearch(-beta, -alpha, qPly + 1);
            backward(move);
            if (score >= beta) {
                stack->bestMove.setMove(move);
                return beta;
            } else if (score > alpha) {
                stack->bestMove.setMove(move);
                alpha = score;
            }
        } while (move = movePicker->pickNextMove(this, qPly, alpha, beta, 0));
    }
    return alpha;
}

