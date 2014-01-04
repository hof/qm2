#include "search.h"
#include <cstdlib>
#include <iostream>
#include <string.h>

const short QCHECKDEPTH = 1;
const short FMARGIN[9] = {200, 200, 200, 450, 450, 600, 600, 1200, 1200};
const bool DO_NULL = true;
const bool DO_FP = true;
const bool DO_LMR = true;
const bool DO_SINGULAR = false;

const int16_t SINGULAR_THRESHOLD[17] = {
    1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1
};

bool TSearch::pondering() {
    return ponder || (outputHandler && outputHandler->enginePonder == true);
}

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
    int new_depth = depth - ONE_PLY;
    int extend_move = (bool)rMove->GivesCheck;
    int bestScore = -pvs(-beta, -alpha, new_depth + extend_move);
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
                    nodes + pruned_nodes,
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
        extend_move = (bool)rMove->GivesCheck;
        int score = -pvs(-alpha - 1, -alpha, new_depth + extend_move);
        if (score > alpha && score < beta && stopSearch == false) {
            score = -pvs(-beta, -alpha, new_depth + extend_move);
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
                        nodes + pruned_nodes,
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
    stack->phase = (stack - 1)->phase;
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

    if (!skipNull
            && DO_NULL
            && eval - FMARGIN[depth] >= beta
            && !inCheck
            && depth <= LOW_DEPTH
            && ABS(beta) < SCORE_MATE - MAX_PLY
            && pos->getPieces(pos->boardFlags->WTM)) {
        return eval - FMARGIN[depth];
    }

    bool mate_threat = false;
    if (!skipNull
            && DO_NULL
            && depth > ONE_PLY
            && !inCheck
            && eval >= beta
            && ABS(beta) < SCORE_MATE - MAX_PLY
            && pos->getPieces(pos->boardFlags->WTM)) {
        int rdepth = depth - 3 * ONE_PLY - (depth >> 2);
        forward();
        int score = -pvs(-beta, -alpha, rdepth);
        backward();
        if (score >= beta) {
            if (score >= SCORE_MATE - MAX_PLY) {
                score = beta; //do not return unproven mate scores
            }
            return score;
        } else if (alpha > (-SCORE_MATE + MAX_PLY) && score < (-SCORE_MATE + MAX_PLY)) {
            TMove * threat = &(stack + 1)->bestMove;
            if (!threat->capture && !threat->promotion) {
                updateHistoryScore(threat, rdepth);
            }
            mate_threat = true;
        }
    }
    skipNull = false;

    /*
     * 7. Principle variation search (PVS). 
     * Generate the first move from pv, hash or internal iterative deepening,
     * all handled by the movepicker. 
     * If no move is returned, the position is either MATE or STALEMATE, 
     * otherwise search the first move with full alpha beta window.
     */
    TMove * first_move = movePicker->pickFirstMove(this, depth, alpha, beta);
    if (!first_move) { //no legal move: it's checkmate or stalemate
        stack->pvCount = 0;
        return inCheck ? -SCORE_MATE + pos->currentPly : drawScore();
    }

    /*
     * 8. Node extensions
     */
    int extend = 0;
    assert(stack->phase >= 0 && stack->phase <= 16);
    if (stack->phase == 16
            && (stack - 1)->phase != 16
            && pos->boardFlags->fiftyCount == 0
            && pos->pawnsAndKings() == pos->allPieces) {
        extend = 2 * ONE_PLY;
    } else if (inCheck) {
        extend++;
        if (extend < ONE_PLY && movePicker->countEvasions(this, first_move) <= 2) {
            extend++; //only one or two replies to check
        }
    }

    if (depth > HIGH_DEPTH && extend > 0 && type != PVNODE) {
        extend--; //if depth is far from horizon, extensions are less needed
    }
    if (depth > LOW_DEPTH && extend > 0 && type != PVNODE) {
        extend--;
    }
    bool gives_check = pos->givesCheck(first_move);
    int extend_move = extend == 0 && (gives_check) && (gives_check > 1 || pos->SEE(first_move) >= 0);
    bool singular = false;
    if (DO_SINGULAR
            && depth >= HIGH_DEPTH
            && extend == 0
            && extend_move == 0
            && eval >= alpha
            && stack->phase < 8
            && stack->ttMove1.piece > 0
            && excludedMove.piece == EMPTY) {
        int r = 2 * ONE_PLY + (depth >> 1);
        int th = SINGULAR_THRESHOLD[stack->phase];
        stack->reduce = 0;
        forward(first_move, gives_check);
        int score1 = -pvs(-beta, -alpha, depth - r);
        backward(first_move);
        tempList.copy(&stack->moveList);
        excludedMove.setMove(first_move);
        HASH_EXCLUDED_MOVE(pos->boardFlags->hashCode);
        int scoreX = pvs(alpha, beta, depth - r);
        HASH_EXCLUDED_MOVE(pos->boardFlags->hashCode);
        first_move->setMove(&excludedMove);
        stack->bestMove.setMove(first_move);
        excludedMove.clear();
        stack->moveList.copy(&tempList);
        if (scoreX < (score1 - th)) {
            singular = true;
            extend_move++;
            if (scoreX < (score1 - 8)) {
                extend_move++;
            }
        }
    }
    int new_depth = depth - ONE_PLY + extend;
    stack->bestMove.setMove(first_move);
    stack->reduce = 0;
    forward(first_move, gives_check);
    int bestScore = -pvs(-beta, -alpha, new_depth + extend_move);
    backward(first_move);
    if (bestScore > alpha) {
        if (bestScore >= beta) {
            hashTable->ttStore(this, first_move->asInt(), bestScore, depth, alpha, beta);
            if (!first_move->capture && !first_move->promotion) {
                if (bestScore < SCORE_MATE - MAX_PLY) {
                    updateKillers(first_move);
                } else {
                    stack->mateKiller.setMove(first_move);
                }
                updateHistoryScore(first_move, depth);
            }
            return bestScore;
        }
        updatePV(first_move);
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
    while (TMove * move = movePicker->pickNextMove(this, depth, alpha, beta)) {
        assert(stack->bestMove.equals(move) == false);
        assert(first_move->equals(move) == false);

        gives_check = pos->givesCheck(move);

        /*
         * 11. forward futility pruning at low depths
         * (moves without potential are skipped)
         */
        if (stack->moveList.stage >= QUIET_MOVES
                && DO_FP
                && type != PVNODE
                && !move->capture
                && !inCheck
                && !gives_check
                && new_depth <= LOW_DEPTH
                && !passedPawn(move)) {
            if (eval + FMARGIN[new_depth] <= alpha) {
                pruned_nodes++;
                continue;
            }
            int mc_max = 2 + ((depth * depth) >> 2);
            if (searchedMoves > mc_max && !pos->active(move)) {
                pruned_nodes++;
                continue;
            }
            if (pos->SEE(move) < 0) {
                pruned_nodes++;
                continue;
            }
        }

        /*
         * 12. Late Move Reductions (LMR) 
         */
        int reduce = 0;
        if (stack->moveList.stage >= QUIET_MOVES
                && DO_LMR
                && !move->capture
                && !inCheck
                && !gives_check
                && !passedPawn(move)
                && !singular
                && new_depth > ONE_PLY) {
            assert(new_depth < 256);
            bool active = gives_check || passedPawn(move) || pos->active(move);
            reduce += LMR[active][type == PVNODE][MIN(63, searchedMoves)][new_depth];
            int max_reduce = new_depth - 2;
            reduce += reduce < max_reduce && type == CUTNODE;
            reduce += reduce < max_reduce && type == CUTNODE;
            reduce += reduce < max_reduce && type != PVNODE && history[move->piece][move->tsq] < 0;
        }

        stack->reduce = reduce;
        extend_move = extend == 0 && (gives_check) && (gives_check > 1 || pos->SEE(move) >= 0);
        forward(move, gives_check);
        int score = -pvs(-alpha - 1, -alpha, new_depth - reduce + extend_move);
        if (score > alpha && reduce > 0) {
            //research without reductions
            (stack - 1)->reduce = 0;
            score = -pvs(-alpha - 1, -alpha, new_depth + extend_move);
        }
        if (score > alpha && score < beta) {
            //full window research
            (stack - 1)->reduce = 0;
            score = -pvs(-beta, -alpha, new_depth + extend_move);
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
static const int MAXPOSGAIN = 200;

int TSearch::qsearch(int alpha, int beta, int qPly, int checkDepth) {

    nodes++;

    if (--nodesUntilPoll <= 0) {
        poll();
    }

    if (stopSearch || pos->currentPly >= MAX_PLY) {
        return alpha;
    }

    //return obvious draws
    stack->phase = (stack - 1)->phase;
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

        TMove * move = movePicker->pickFirstQuiescenceMove(this, qPly < checkDepth, alpha, beta);
        if (!move) { //return evalation score if there are quiescence moves
            return score;
        }
        alpha = MAX(score, alpha);
        stack->hashCode = pos->boardFlags->hashCode;
        int base = score;
        do { //loop through quiescence moves
            int givesCheck = pos->givesCheck(move);
            if (!move->capture && !move->promotion &&
                    (givesCheck == 0 || (givesCheck == 1 && pos->SEE(move) < 0))) {
                pruned_nodes++;
                continue;
            }

            //pruning (delta futility and negative see), 
            //skipped for checks
            if (givesCheck == 0 && NOTPV(alpha, beta)) {
                //1. delta futility pruning: the captured piece + max. positional gain should raise alpha
                int gain = PIECE_VALUE[move->capture];
                if (move->promotion) {
                    if (move->promotion != WQUEEN && move->promotion != BQUEEN) {
                        pruned_nodes++;
                        continue;
                    }
                    gain += PIECE_VALUE[move->promotion] - VPAWN;
                }
                if (base + gain + MAXPOSGAIN < alpha) {
                    pruned_nodes++;
                    continue;
                }

                //2. prune moves when SEE is negative
                if (pos->SEE(move) < 0) {
                    pruned_nodes++;
                    continue;
                }
            }
            forward(move, givesCheck);
            score = -qsearch(-beta, -alpha, qPly + 1, checkDepth);
            backward(move);
            if (score >= beta) {
                stack->bestMove.setMove(move);
                return score;
            } else if (score > alpha) {
                stack->bestMove.setMove(move);
                alpha = score;
            }
        } while (move = movePicker->pickNextMove(this, qPly < checkDepth, alpha, beta));
    } else { // in check
        assert(pos->boardFlags->checkers);
        int score = -SCORE_INFINITE;
        TMove * move = movePicker->pickFirstQuiescenceMove(this, 0, alpha, beta);
        if (!move) { //checkmate
            return -SCORE_MATE + pos->currentPly;
        }
        stack->hashCode = pos->boardFlags->hashCode;
        do {
            bool givesCheck = pos->givesCheck(move);
            forward(move, givesCheck);
            score = -qsearch(-beta, -alpha, qPly + 1, checkDepth);
            backward(move);
            if (score >= beta) {
                stack->bestMove.setMove(move);
                return score;
            } else if (score > alpha) {
                stack->bestMove.setMove(move);
                alpha = score;
            }
        } while (move = movePicker->pickNextMove(this, 0, alpha, beta));
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
