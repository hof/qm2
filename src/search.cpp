#include "search.h"
#include "searchdata.h"
#include "evaluate.h"
#include "movepicker.h"

int pvs_root(TSearchData *searchData, int alpha, int beta, int depth) {
    /*
     * Principle variation search (PVS). 
     * Generate the first move from pv, hash or internal iterative deepening,
     * all handled by the movepicker. 
     * If no move is returned, the position is either MATE or STALEMATE, 
     * otherwise search the first move with full alpha beta window.
     */

    assert(searchData->root.MoveCount > 0);
    int nodesBeforeMove = searchData->nodes;
    TRootMove * rMove = &searchData->root.Moves[0];
    TBoard * pos = searchData->pos;
    int extend = 0;

    searchData->forward(&rMove->Move, rMove->GivesCheck);
    if (rMove->GivesCheck) {
        extend = HALF_PLY;
        searchData->pos->boardFlags->checkerSq = rMove->checkerSq;
        searchData->pos->boardFlags->checkers = rMove->checkers;
    }
    int bestScore = -pvs(searchData, -beta, -alpha, depth - ONE_PLY + extend);
    searchData->backward(&rMove->Move);

    int sortBaseScoreForPV = 1000 * depth / ONE_PLY;
    rMove->Nodes += searchData->nodes - nodesBeforeMove;
    rMove->PV = sortBaseScoreForPV;
    rMove->Value = bestScore;

    if (searchData->stopSearch) {
        return alpha;
    } else if (bestScore > alpha) {
        if (bestScore >= beta) {
            if (!rMove->Move.equals(&searchData->stack->pvMoves[0])) {
                searchData->stack->pvMoves[0].setMove(&rMove->Move);
                searchData->stack->pvCount = 1;
            }
            return bestScore;
        }
        searchData->updatePV(&rMove->Move);
        if (!rMove->Move.equals(&searchData->stack->pvMoves[0]) && searchData->outputHandler) {
            searchData->outputHandler->sendPV(bestScore,
                    depth / ONE_PLY,
                    searchData->selDepth,
                    searchData->nodes,
                    searchData->timeManager->elapsed(),
                    searchData->getPVString().c_str(),
                    bestScore <= alpha ? FAILLOW : bestScore >= beta ? FAILHIGH : EXACT);
        }
        alpha = bestScore;
    }


    /*
     * Search remaining moves with a zero width window
     */
    searchData->stack->bestMove.setMove(&rMove->Move);
    for (int i = 1; i < searchData->root.MoveCount; i++) {
        rMove = &searchData->root.Moves[i];

        extend = rMove->GivesCheck;
        nodesBeforeMove = searchData->nodes;

        searchData->forward(&rMove->Move, rMove->GivesCheck);
        if (rMove->GivesCheck) {
            searchData->pos->boardFlags->checkerSq = rMove->checkerSq;
            searchData->pos->boardFlags->checkers = rMove->checkers;
        }
        int score = -pvs(searchData, -alpha - 1, -alpha, depth - ONE_PLY + extend);
        if (score > alpha && score < beta && searchData->stopSearch == false) {
            score = -pvs(searchData, -beta, -alpha, depth - ONE_PLY + extend);
        }
        searchData->backward(&rMove->Move);
        rMove->Nodes += searchData->nodes - nodesBeforeMove;
        rMove->Value = score;
        if (searchData->stopSearch) {
            return alpha;
        } else if (score > bestScore && score > alpha) {
            rMove->PV = sortBaseScoreForPV + i;
            if (score >= beta) {
                if (!rMove->Move.equals(&searchData->stack->pvMoves[0])) {
                    searchData->stack->pvMoves[0].setMove(&rMove->Move);
                    searchData->stack->pvCount = 1;
                }
                return score;
            }
            bestScore = score;
            searchData->stack->bestMove.setMove(&rMove->Move);
            searchData->updatePV(&rMove->Move);

            if (!searchData->stack->bestMove.equals(&searchData->stack->pvMoves[0]) && searchData->outputHandler) {
                searchData->outputHandler->sendPV(bestScore,
                        depth / ONE_PLY,
                        searchData->selDepth,
                        searchData->nodes,
                        searchData->timeManager->elapsed(),
                        searchData->getPVString().c_str(),
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
int pvs(TSearchData *searchData, int alpha, int beta, int depth) {

    if (searchData->stopSearch) {
        return alpha;
    }


    TBoard * pos = searchData->pos;
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
        searchData->stack->pvCount = 0;
        if (pos->currentPly > searchData->selDepth) {
            searchData->selDepth = pos->currentPly;
        }
        int score = qsearch(searchData, alpha, beta, 0);
        return score;
    }
    searchData->nodes++;
    if (--searchData->nodesUntilPoll <= 0) {
        searchData->poll();
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
            if (ply >= 0 && searchData->getStack(ply)->hashCode == pos->boardFlags->hashCode) {
                return SCORE_DRAW;
            } else if (ply < 0 && searchData->hashTable->repTable[searchData->root.FiftyCount + ply] == pos->boardFlags->hashCode) {
                return SCORE_DRAW;

            }
        }
    }

    if (pos->isDraw()) {
        searchData->stack->pvCount = 0;
        return SCORE_DRAW;
    }

    if (pos->currentPly >= MAX_PLY) {
        return alpha;
    }

    /*
     * 4. Transposition table lookup
     */
    int type = searchData->setNodeType(alpha, beta);
    depth = MAX(0, depth);
    searchData->hashTable->ttLookup(searchData, depth, alpha, beta);
    if (searchData->stack->ttScore != TT_EMPTY && searchData->excludedMove.piece == EMPTY) {
        searchData->stack->pvCount = 0;
        return searchData->stack->ttScore;
    }

    /*
     * 5. Null move pruning and razoring
     */
    searchData->stack->hashCode = pos->boardFlags->hashCode;
    assert(type != UNKNOWN);
    assert(type == PVNODE || alpha + 1 == beta);
    bool inCheck = searchData->stack->inCheck;
    int extendNode = 0;
    int eval = evaluate(searchData, alpha, beta);

    if (!searchData->skipNull
            && type != PVNODE
            && depth <= LOW_DEPTH
            && !inCheck
            && eval < beta - FUTILITY_MARGIN[depth]
            && beta < SCORE_MATE - MAX_PLY
            && pos->hasPieces(pos->boardFlags->WTM)
            && !pos->promotingPawns(pos->boardFlags->WTM)) {
        int rbeta = beta - FUTILITY_MARGIN[depth];
        int qvalue = qsearch(searchData, alpha, beta, 0);
        if (qvalue < rbeta) {
            return qvalue;
        }
    }

    if (!searchData->skipNull
            && type != PVNODE
            && depth <= LOW_DEPTH
            && !inCheck
            && eval >= beta
            && beta < SCORE_MATE - MAX_PLY
            && pos->hasPieces(pos->boardFlags->WTM)
            && eval - FUTILITY_MARGIN[depth] > beta) {
        return eval - FUTILITY_MARGIN[depth];
    }
    if (!searchData->skipNull
            && depth > ONE_PLY
            && !inCheck
            && eval >= beta
            && beta < SCORE_MATE - MAX_PLY
            && pos->hasPieces(pos->boardFlags->WTM)) {
        searchData->forward();
        int nullDepth = MAX(0, depth - ONE_PLY - NullReduction(depth, eval - beta));
        int score = -pvs(searchData, -beta, -alpha, nullDepth);
        searchData->backward();
        if (score >= beta) {
            if (score >= SCORE_MATE - MAX_PLY) {
                score = beta; //do not return unproven mate scores
            }
            return score;
        } else if (alpha > (-SCORE_MATE + MAX_PLY) && score < (-SCORE_MATE + MAX_PLY)) {
            extendNode = HALF_PLY; //mate threat extension
        }

        if ((searchData->stack - 1)->reduce && (eval - score) > VPAWN) {
            TMove * prevMove = &(searchData->stack - 1)->move;
            TMove * threatMove = &(searchData->stack + 1)->bestMove;
            if (threatMove->piece
                    && (prevMove->ssq == threatMove->tsq || prevMove->tsq == threatMove->ssq)) {
                extendNode = (searchData->stack - 1)->reduce;
            }
        }
    } else {
        searchData->skipNull = false;
    }

    /*
     * Endgame extension: 
     * Increase depth with two ply when reaching pawns/kings endgame
     */
    assert(searchData->stack->gamePhase >= 0 && searchData->stack->gamePhase <= 16);
    TMove * previous = &(searchData->stack - 1)->move;
    if (searchData->stack->gamePhase == 16
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
    TMovePicker * mp = searchData->movePicker;
    TMove * firstMove = mp->pickFirstMove(searchData, depth, alpha, beta, 0);
    if (!firstMove) {
        searchData->stack->pvCount = 0;
        return inCheck ? -SCORE_MATE + pos->currentPly : SCORE_DRAW;
    }

    /*
     * 8. Single reply to check extension.
     */
    if (inCheck && !firstMove->capture && extendNode < ONE_PLY) {
        TMove * move2 = mp->pickNextMove(searchData, depth, alpha, beta, 0);
        if (!move2) {
            extendNode = ONE_PLY;
        } else if (extendNode < HALF_PLY) {
            TMove * move3 = mp->pickNextMove(searchData, depth, alpha, beta, 0);
            if (!move3) {
                extendNode = HALF_PLY;
            } else {
                mp->push(searchData, move3, SCORE_INFINITE - 3); //push move back on the list
            }
            mp->push(searchData, move2, SCORE_INFINITE - 2); //push move back on the list
        }
    }

    searchData->stack->bestMove.setMove(firstMove);
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
                && searchData->stack->moveList.stage < CAPTURES
                && see == 0
                && searchData->excludedMove.piece == EMPTY) { //singular extension 
            if (firstMove->capture || firstMove->promotion || givesCheck || pos->checksPiece(firstMove)) {
                extendMove = HALF_PLY;
                singular = true;
            } else {
                searchData->stack->reduce = 0;
                searchData->forward(firstMove, givesCheck);
                int firstMoveScore = -pvs(searchData, -beta, -alpha, depth - 6 * ONE_PLY);
                searchData->backward(firstMove);
                if (firstMoveScore > alpha) {
                    searchData->tempList.copy(&searchData->stack->moveList);
                    searchData->excludedMove.setMove(firstMove);
                    HASH_EXCLUDED_MOVE(pos->boardFlags->hashCode);
                    int scoreX = pvs(searchData, alpha, beta, depth - 6 * ONE_PLY);
                    HASH_EXCLUDED_MOVE(pos->boardFlags->hashCode);
                    firstMove->setMove(&searchData->excludedMove);
                    searchData->stack->bestMove.setMove(firstMove);
                    searchData->excludedMove.clear();
                    searchData->stack->moveList.copy(&searchData->tempList);
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

    searchData->stack->reduce = 0;
    searchData->forward(firstMove, givesCheck);
    int bestScore = -pvs(searchData, -beta, -alpha, depth - ONE_PLY + extendNode + extendMove);
    searchData->backward(firstMove);
    if (bestScore > alpha) {
        if (bestScore >= beta) {
            searchData->hashTable->ttStore(searchData, firstMove->asInt(), bestScore, depth, alpha, beta);
            if (!firstMove->capture && !firstMove->promotion) {
                if (bestScore < SCORE_MATE - MAX_PLY) {
                    searchData->updateKillers(firstMove);
                } else {
                    searchData->stack->mateKiller.setMove(firstMove);
                }
                searchData->updateHistoryScore(firstMove, depth);
            }
            return bestScore;
        }

        searchData->updatePV(firstMove);
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

    while (TMove * move = mp->pickNextMove(searchData, depth, alpha, beta, fGap)) {


        assert(searchData->stack->bestMove.equals(move) == false);
        assert(firstMove->equals(move) == false);

        /*
         * 12. Ignore moves that lead to draw by repetition
         * if alpha is better than draw
         */
        if (alpha > SCORE_DRAW
                && pos->boardFlags->fiftyCount >= 2
                && pos->currentPly >= 2) {
            TMove * myLastMove = &(searchData->stack - 2)->move;
            if (myLastMove->tsq == move->ssq
                    && myLastMove->ssq == move->tsq) {
                continue;
            }
        }

        givesCheck = pos->givesCheck(move);

        int phase = searchData->stack->moveList.stage;

        /*
         * 13. Pruning and reductions 
         */
        int reduce = 0;
        if (phase >= STOP
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
                int reduceMore = type != PVNODE;
                reduceMore += type == CUTNODE;
                reduceMore += singular;
                if (reduceMore > 0 && pos->active(move)) {
                    reduceMore--;
                }
                reduce = MIN(2 * ONE_PLY + reduceMore, BSR(searchedMoves + 1) + reduceMore);
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



        searchData->stack->reduce = reduce;
        searchData->forward(move, givesCheck);
        int score = -pvs(searchData, -alpha - 1, -alpha, depth - ONE_PLY - reduce + extendMove + extendNode);
        if (score > alpha && reduce) { //full depth research
            (searchData->stack - 1)->reduce = 0;
            score = -pvs(searchData, -alpha - 1, -alpha, depth - ONE_PLY + extendMove + extendNode);
        }
        if (score > alpha && score < beta) {
            (searchData->stack - 1)->reduce = 0;
            score = -pvs(searchData, -beta, -alpha, depth - ONE_PLY + extendMove + extendNode);
        }
        searchData->backward(move);
        if (score > bestScore) {
            searchData->stack->bestMove.setMove(move);
            if (score >= beta) {
                searchData->hashTable->ttStore(searchData, move->asInt(), score, depth, alpha, beta);
                if (!move->capture && !move->promotion) {
                    if (bestScore < SCORE_MATE - MAX_PLY) {
                        searchData->updateKillers(move);
                    } else {
                        searchData->stack->mateKiller.setMove(move);
                    }
                    searchData->updateHistoryScore(move, depth);
                    int xMoves = 0;
                    for (TMove * cur = searchData->stack->moveList.first;
                            cur != searchData->stack->moveList.last; cur++) {
                        if (cur->score == MOVE_EXCLUDED && cur != move) {
                            searchData->updateHistoryScore(cur, -depth);
                        }
                    }
                    assert(xMoves <= searchedMoves);
                }

                return score;
            }
            bestScore = score;
            if (bestScore > alpha) {
                searchData->updatePV(move);
                alpha = bestScore;
            }
        }
        searchedMoves++;

    }
    searchData->hashTable->ttStore(searchData, searchData->stack->bestMove.asInt(), bestScore, depth, alpha, beta);
    return bestScore;
}

/*
 * Quiescence search
 */
static const int MAXPOSGAIN = 2 * VPAWN;

int qsearch(TSearchData * searchData, int alpha, int beta, int qPly) {

    if (searchData->stopSearch) {
        return alpha;
    }
    searchData->nodes++;
    if (--searchData->nodesUntilPoll <= 0) {
        searchData->poll();
    }
    TBoard * pos = searchData->pos;

    //return obvious draws

    if (pos->boardFlags->fiftyCount > 1) {
        if (pos->boardFlags->fiftyCount >= 100) {
            return SCORE_DRAW;
        }
        int stopPly = pos->currentPly - pos->boardFlags->fiftyCount;
        for (int ply = pos->currentPly - 4; ply >= stopPly; ply -= 2) {
            if (ply >= 0 && searchData->getStack(ply)->hashCode == pos->boardFlags->hashCode) {
                return SCORE_DRAW;
            } else if (ply < 0 && searchData->hashTable->repTable[searchData->root.FiftyCount + ply] == pos->boardFlags->hashCode) {
                return SCORE_DRAW;
            }
        }
    }

    if (!searchData->stack->inCheck) {
        if (pos->isDraw()) {
            return SCORE_DRAW;
        }
        int score = evaluate(searchData, alpha, beta); //stand-pat score
        if (score >= beta || pos->currentPly >= MAX_PLY) { //fail high
            return score;
        }

        TMovePicker * mp = searchData->movePicker;

        TMove * move = mp->pickFirstQuiescenceMove(searchData, qPly, alpha, beta, 0);
        if (!move) {
            return score;
        }
        alpha = MAX(score, alpha);
        searchData->stack->hashCode = pos->boardFlags->hashCode;

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

            searchData->forward(move, givesCheck);
            score = -qsearch(searchData, -beta, -alpha, qPly + 1);
            searchData->backward(move);
            if (score >= beta) {
                searchData->stack->bestMove.setMove(move);
                return beta;
            } else if (score > alpha) {
                searchData->stack->bestMove.setMove(move);
                alpha = score;
            }
        } while (move = mp->pickNextMove(searchData, qPly, alpha, beta, 0));
    } else { // in check
        if (pos->currentPly >= MAX_PLY) {
            return alpha;
        }
        assert(pos->boardFlags->checkers);
        int score = -SCORE_INFINITE;

        TMovePicker * mp = searchData->movePicker;
        TMove * move = mp->pickFirstQuiescenceMove(searchData, qPly, alpha, beta, 0);
        if (!move) {
            return -SCORE_MATE + pos->currentPly;
        }
        searchData->stack->hashCode = pos->boardFlags->hashCode;
        do {
            bool givesCheck = pos->givesCheck(move);
            searchData->forward(move, givesCheck);
            score = -qsearch(searchData, -beta, -alpha, qPly + 1);
            searchData->backward(move);
            if (score >= beta) {
                searchData->stack->bestMove.setMove(move);
                return beta;
            } else if (score > alpha) {
                searchData->stack->bestMove.setMove(move);
                alpha = score;
            }
        } while (move = mp->pickNextMove(searchData, qPly, alpha, beta, 0));
    }
    return alpha;
}

