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
 * File: search.cpp
 * Search methods for traditional chess. 
 * Maxima uses a fail-soft principle variation search (PVS) with
 * - aspiration windows
 * - null move pruning 
 * - futility pruning
 * - late move reductions (LMR) and late move pruning (LMP)
 * 
 */

#include "search.h"
#include <cstdlib>
#include <iostream>
#include <string.h>

static const short FUTILITY_MARGIN = 50;
static const short LMR_MIN = 0; //in half plies
static const short LMR_MAX = 6; //in half plies

static const short QS_DELTA = 200; //qsearch delta pruning margin

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

int TSearch::initRootMoves() {
    int result = 0;
    root.MoveCount = 0;
    root.FiftyCount = pos->stack->fifty_count;
    setNodeType(-SCORE_INFINITE, SCORE_INFINITE);
    hashTable->repStore(this, pos->stack->hash_code, pos->stack->fifty_count);
    hashTable->ttLookup(this, 0, -SCORE_INFINITE, SCORE_INFINITE);
    for (TMove * move = movePicker->pickFirstMove(this, ONE_PLY, -SCORE_INFINITE, SCORE_INFINITE);
            move; move = movePicker->pickNextMove(this, ONE_PLY, -SCORE_INFINITE, SCORE_INFINITE)) {
        TRootMove * rMove = &root.Moves[root.MoveCount++];
        rMove->init(move, 1000 - root.MoveCount, pos->givesCheck(move), pos->SEE(move));
        if (rMove->GivesCheck) {
            rMove->checker_sq = (pos->stack + 1)->checker_sq;
            rMove->checkers = (pos->stack + 1)->checkers;
        }
    }
    root.InCheck = pos->inCheck();
    stack->hash_code = pos->stack->hash_code;
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

/* 
 * Copy moves 
 */
void TRoot::matchMoves(TMoveList * list) {
    if (list->first == list->last) {
        return;
    }
    for (int j = 0; j < MoveCount;) {
        bool match = false;
        TRootMove rMove = Moves[j];
        std::cout << rMove.Move.asString() << " ";
        ;
        for (TMove * m = list->first; m != list->last; m++) {
            if (rMove.Move.equals(m)) {
                match = true;
                break;
            }
        }
        if (match == false) {
            Moves[j] = Moves[MoveCount - 1];
            MoveCount--;
            continue;
        }
        j++;
    }
}

/**
 * Principle Variation Search (root node)
 * Difference with normal (non-root) search:
 * - Sending new PVs to the interface
 * - Sort order based on amount of nodes of the subtrees
 * - No pruning, reductions, extensions and hash table lookup/store
 */
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
        pos->stack->checker_sq = rMove->checker_sq;
        pos->stack->checkers = rMove->checkers;
    }
    int new_depth = depth - ONE_PLY;
    int best = -pvs(-beta, -alpha, new_depth);
    backward(&rMove->Move);
    int sortBaseScoreForPV = 1000 * depth / ONE_PLY;
    rMove->Nodes += nodes - nodesBeforeMove;
    rMove->PV = sortBaseScoreForPV;
    rMove->Value = best;
    stack->bestMove.setMove(&rMove->Move);
    if (!rMove->Move.equals(&stack->pvMoves[0])) {
        stack->pvMoves[0].setMove(&rMove->Move);
        stack->pvCount = 1;
    }
    if (stopSearch) {
        return alpha;
    }
    if (best < alpha) {
        return best; //return to adjust the aspiration window
    }
    if (best >= beta) {
        if (outputHandler && depth > LOW_DEPTH) {
            outputHandler->sendPV(best,
                    depth / ONE_PLY,
                    selDepth,
                    nodes + pruned_nodes,
                    timeManager->elapsed(),
                    getPVString().c_str(),
                    best <= alpha ? FAILLOW : best >= beta ? FAILHIGH : EXACT);
        }
        return best;
    }
    if (best > alpha) {
        updatePV(&rMove->Move);
        if (outputHandler && depth > LOW_DEPTH) {
            outputHandler->sendPV(best,
                    depth / ONE_PLY,
                    selDepth,
                    nodes + pruned_nodes,
                    timeManager->elapsed(),
                    getPVString().c_str(),
                    best <= alpha ? FAILLOW : best >= beta ? FAILHIGH : EXACT);
        }
        alpha = best;
    }

    /*
     * Search remaining moves with a zero width window
     */
    for (int i = 1; i < root.MoveCount; i++) {
        rMove = &root.Moves[i];
        nodesBeforeMove = nodes;
        forward(&rMove->Move, rMove->GivesCheck);
        if (rMove->GivesCheck) {
            pos->stack->checker_sq = rMove->checker_sq;
            pos->stack->checkers = rMove->checkers;
        }
        int score = -pvs(-alpha - 1, -alpha, new_depth);
        if (score > alpha && stopSearch == false) {
            score = -pvs(-beta, -alpha, new_depth);
        }
        backward(&rMove->Move);
        rMove->Nodes += nodes - nodesBeforeMove;
        rMove->Value = score;
        if (stopSearch) {
            return alpha;
        }
        if (score > best) {
            rMove->PV = sortBaseScoreForPV + i;
            stack->bestMove.setMove(&rMove->Move);
            if (score >= beta) {
                if (!rMove->Move.equals(&stack->pvMoves[0])) {
                    stack->pvMoves[0].setMove(&rMove->Move);
                    stack->pvCount = 1;
                }
                return score;
            }
            best = score;
            if (score > alpha) {
                updatePV(&rMove->Move);
                //trace(best, alpha, beta);
                if (outputHandler && depth > LOW_DEPTH) { //send the new pv right away
                    outputHandler->sendPV(best,
                            depth / ONE_PLY,
                            selDepth,
                            nodes + pruned_nodes,
                            timeManager->elapsed(),
                            getPVString().c_str(),
                            best <= alpha ? FAILLOW : best >= beta ? FAILHIGH : EXACT);
                }
                alpha = score;
            }
        } else {
            rMove->PV >>= 5;
        }
    }
    return best;
}

/**
 * Move Extensions
 */
int TSearch::extendMove(TMove * move, int gives_check) {
    if (gives_check > 0) {
        if (gives_check > 1) { //double check or exposed check
            return ONE_PLY;
        }
        if (move->capture) {
            return ONE_PLY;
        }
        if (move->piece == WPAWN || move->piece == BPAWN) {
            return ONE_PLY;
        }
        if (pos->SEE(move) >= 0) {
            return ONE_PLY;
        }
        return 0;
    }
    return 0;
}

/**
 * Principle Variation Search (fail-soft)
 * @param alpha lowerbound value
 * @param beta upperbound value
 * @param depth remaining search depth
 * @return score for the current node
 */
int TSearch::pvs(int alpha, int beta, int depth) {

    stack->pvCount = 0;
    
    
    /*
     * 1. If no more depth remaining, return quiescence value
     */
    if (depth < ONE_PLY) {
        selDepth = MAX(selDepth, pos->current_ply);
        return qsearch(alpha, beta, depth);
    }

    //time check
    nodes++;
    if (nodesUntilPoll <= 0) {
        poll();
    }
    if (stopSearch || pos->current_ply >= MAX_PLY) {
        return alpha;
    }
    assert(depth >= ONE_PLY && depth < 256);

    /* 
     * 2. Mate distance pruning: 
     * if we already know we will win/loose by mate in n, 
     * it is not needed to search deeper than n
     */
    if ((SCORE_MATE - pos->current_ply) < beta) {
        beta = SCORE_MATE - pos->current_ply;
        if (alpha >= (SCORE_MATE - pos->current_ply)) {
            return SCORE_MATE - pos->current_ply;
        }
    }
    if ((-SCORE_MATE + pos->current_ply) > alpha) {
        alpha = -SCORE_MATE + pos->current_ply;
        if (beta <= (-SCORE_MATE + pos->current_ply)) {
            return -SCORE_MATE + pos->current_ply;
        }
    }

    /*
     * 3. Return obvious draws 
     */
    if (pos->isDraw()) { //draw by no mating material
        return drawScore();
    }
    if (pos->stack->fifty_count > 3) {
        if (pos->stack->fifty_count >= 100) {
            return drawScore(); //draw by 50 reversible moves
        }
        int stopPly = pos->current_ply - pos->stack->fifty_count;
        for (int ply = pos->current_ply - 4; ply >= stopPly; ply -= 2) { //draw by repetition
            if (ply >= 0 && getStack(ply)->hash_code == pos->stack->hash_code) {
                return drawScore();
            } else if (ply < 0 && hashTable->repTable[root.FiftyCount + ply] == pos->stack->hash_code) {
                return drawScore();

            }
        }
    }

    /*
     * 4. Transposition table lookup
     */
    hashTable->ttLookup(this, depth, alpha, beta);
    if (stack->ttScore != TT_EMPTY) {
        return stack->ttScore;
    }

    /*
     * 5. Pruning: Fail-high and Nullmove
     */
    int eval = evaluate(this);
    int new_depth = depth - ONE_PLY;
    bool in_check = stack->inCheck;
    assert(new_depth >= 0);
    assert((stack->phase == 16) == pos->onlyPawns());

    //a) Fail-high pruning (return if static evaluation score is already much better than beta)
    if (!in_check
            && new_depth <= LOW_DEPTH
            && (eval - FUTILITY_MARGIN * depth) >= beta
            && beta > -SCORE_DEEPEST_MATE
            && pos->hasPieces(pos->stack->wtm)) {
        return eval - FUTILITY_MARGIN * depth;
    }

    int type = setNodeType(alpha, beta);
    assert(type != UNKNOWN);
    assert(type == PVNODE || alpha + 1 == beta);

    //b) Null move pruning
    stack->hash_code = pos->stack->hash_code;
    bool mate_threat = false;
    if (!skipNull
            && !in_check
            && new_depth > 0
            && eval >= beta
            && beta > -SCORE_DEEPEST_MATE
            && pos->hasPieces(pos->stack->wtm)) {
        int rdepth = new_depth - (3 * ONE_PLY);
        if (rdepth >= 8 && stack->phase < 14) {
            rdepth -= rdepth / 6;
        }
        rdepth = MAX(rdepth, 0);
        forward();
        int null_score = -pvs(-beta, -alpha, rdepth);
        backward();
        if (null_score >= beta) {
            if (null_score > SCORE_DEEPEST_MATE) {
                return beta; // not return unproven mate scores
            }
            return null_score;
        } else {
            mate_threat = null_score < -SCORE_DEEPEST_MATE;
            if (mate_threat) {
                TMove * threat = &(stack + 1)->bestMove;
                if (!threat->capture && !threat->promotion) {
                    updateHistoryScore(threat, rdepth);
                }
            }
        }
    }
    skipNull = false;

    /*
     * 7. Principle variation search (PVS). 
     * Generate the first move from pv, hash or internal iterative deepening,
     * all handled by the movepicker instance. 
     * If no move is returned, the position is either MATE or STALEMATE, 
     * otherwise search the first move with full alpha beta window.
     */
    TMove * first_move = movePicker->pickFirstMove(this, depth, alpha, beta);
    if (!first_move) { //no legal move: it's checkmate or stalemate
        return in_check ? -SCORE_MATE + pos->current_ply : drawScore();
    }
    int gives_check = pos->givesCheck(first_move);
    int extend_move = extendMove(first_move, gives_check);
    stack->bestMove.setMove(first_move);
    forward(first_move, gives_check);
    int best = -pvs(-beta, -alpha, new_depth + extend_move);
    backward(first_move);
    if (best > alpha) {
        if (best >= beta) {
            hashTable->ttStore(this, first_move->asInt(), best, depth, alpha, beta);
            if (!first_move->capture && !first_move->promotion) {
                if (best < SCORE_DEEPEST_MATE) {
                    updateKillers(first_move);
                } else {
                    stack->mateKiller.setMove(first_move);
                }
                updateHistoryScore(first_move, depth);
            }
            return best;
        }
        updatePV(first_move);
        alpha = best;
    }

    /*
     * 10. Search remaining moves with a zero width window (beta == alpha+1), 
     * in the following order:
     * - Captures and queen promotions with a positive SEE value 
     * - Killer Moves
     * - Castling
     * - Non-captures with a positive static score
     * - All remaining moves
     */
    int searched_moves = 1;
    int max_reduce = MAX(0, new_depth - ONE_PLY);
    while (TMove * move = movePicker->pickNextMove(this, depth, alpha, beta)) {
        assert(stack->bestMove.equals(move) == false);
        assert(first_move->equals(move) == false);
        gives_check = pos->givesCheck(move);
        bool skip_prune = stack->moveList.stage < QUIET_MOVES
                || in_check || gives_check > 0 || move->capture || move->promotion
                || move->castle || passedPawn(move) || beta < -SCORE_DEEPEST_MATE;

        /*
         * 11. forward futility pruning at low depths
         * (moves without potential are skipped)
         */
        if (!skip_prune
                && type != PVNODE
                && (eval < alpha || best >= alpha)
                && new_depth <= LOW_DEPTH
                ) {
            int mc_max = 2 + ((depth * depth) / 4);
            if (searched_moves > mc_max) {
                pruned_nodes++;
                continue;
            }
            if (eval + FUTILITY_MARGIN * depth <= alpha) {
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
        extend_move = extendMove(move, gives_check);
        int reduce = 0;
        if (!skip_prune && max_reduce > 0) {
            reduce = ONE_PLY;
            if (searched_moves >= 4) {
                reduce += new_depth / 4;
            }
            reduce = MIN(max_reduce, reduce);
        }
        assert(reduce == 0 || extend_move == 0);

        /*
         * 13. Go forward and search next node
         */
        forward(move, gives_check);
        int score = -pvs(-alpha - 1, -alpha, new_depth - reduce + extend_move);
        if (score > alpha && reduce > 0) {
            //research without reductions
            score = -pvs(-alpha - 1, -alpha, new_depth + extend_move);
        }
        if (type == PVNODE && score > alpha) {
            //full window research
            score = -pvs(-beta, -alpha, new_depth + extend_move);
        }
        backward(move);

        /*
         * 14. Update the best value for this node and return is the score is 
         * above beta (beta cutoff)
         */
        if (score > best) {
            stack->bestMove.setMove(move);
            if (score >= beta) {
                // Beta Cutoff, hash the results, update killers and history table
                hashTable->ttStore(this, move->asInt(), score, depth, alpha, beta);
                if (!move->capture && !move->promotion) {
                    if (best < SCORE_DEEPEST_MATE) {
                        updateKillers(move);
                    } else {
                        stack->mateKiller.setMove(move);
                    }
                    updateHistoryScore(move, depth);
                    for (TMove * cur = stack->moveList.first;
                            cur != stack->moveList.last; cur++) {
                        if (cur->score == MOVE_EXCLUDED && cur != move) {
                            updateHistoryScore(cur, -depth);
                        }
                    }
                }
                return score;
            }
            best = score;
            if (best > alpha) { //update the PV
                updatePV(move);
                alpha = best;
            }
        }
        searched_moves++;
    }

    /*
     * 15. Store the result in the hash table and return
     */
    hashTable->ttStore(this, stack->bestMove.asInt(), best, depth, alpha, beta);
    return best;
}

/*
 * Quiescence search
 */
int TSearch::qsearch(int alpha, int beta, int depth) {

    //time check
    nodes++;
    if (--nodesUntilPoll <= 0) {
        poll();
    }
    if (stopSearch || pos->current_ply >= MAX_PLY) {
        return alpha;
    }

    //return obvious draws
    if (pos->stack->fifty_count > 3) { //draw by repetition or fifty quiet moves
        if (pos->stack->fifty_count >= 100) {
            return drawScore();
        }
        int stopPly = pos->current_ply - pos->stack->fifty_count;
        for (int ply = pos->current_ply - 4; ply >= stopPly; ply -= 2) {
            if (ply >= 0 && getStack(ply)->hash_code == pos->stack->hash_code) {
                return drawScore();
            } else if (ply < 0 && hashTable->repTable[root.FiftyCount + ply] == pos->stack->hash_code) {
                return drawScore();
            }
        }
    }
    if (pos->isDraw()) { //draw by no mating material
        return drawScore();
    }

    //mate distance pruning
    if ((SCORE_MATE - pos->current_ply) < beta) {
        beta = SCORE_MATE - pos->current_ply;
        if (alpha >= (SCORE_MATE - pos->current_ply)) {
            return SCORE_MATE - pos->current_ply;
        }
    }
    if ((-SCORE_MATE + pos->current_ply) > alpha) {
        alpha = -SCORE_MATE + pos->current_ply;
        if (beta <= (-SCORE_MATE + pos->current_ply)) {
            return -SCORE_MATE + pos->current_ply;
        }
    }

    int eval = evaluate(this); //always do an eval - it's incremental
    assert((stack->phase == 16) == pos->onlyPawns());

    //if not in check, generate captures, promotions and (upto some plies ) quiet checks
    if (eval >= beta && !stack->inCheck) { //return evaluation score is it's already above beta (stand-pat idea)
        return eval;
    }

    TMove * move = movePicker->pickFirstMove(this, depth, alpha, beta);
    if (!move) { //return evaluation score if there are no quiescence moves
        return stack->inCheck ? -SCORE_MATE + pos->current_ply : eval;
    }
    if (!stack->inCheck) {
        alpha = MAX(eval, alpha);
    }
    stack->hash_code = pos->stack->hash_code;
    do { //loop through quiescence moves
        int gives_check = pos->givesCheck(move);
        if (!stack->inCheck && !move->capture && !move->promotion && !move->castle &&
                (gives_check == 0 || (gives_check == 1 && pos->SEE(move) < 0))) {
            pruned_nodes++;
            continue;
        }

        //pruning (delta futility and negative see), skipped for checks
        if (!stack->inCheck && gives_check == 0 && NOTPV(alpha, beta)) {

            //1. delta futility pruning: the captured piece + max. positional gain should raise alpha
            int gain = PIECE_VALUE[move->capture];
            if (move->promotion) {
                if (move->promotion != WQUEEN && move->promotion != BQUEEN) {
                    pruned_nodes++;
                    continue;
                }
                gain += PIECE_VALUE[move->promotion] - VPAWN;
            }
            int delta = depth >= 0 ? QS_DELTA : FUTILITY_MARGIN;
            if (eval + gain + delta < alpha) {
                pruned_nodes++;
                continue;
            }

            //2. prune moves when SEE is negative
            if (pos->SEE(move) < 0) {
                pruned_nodes++;
                continue;
            }
        }
        forward(move, gives_check);
        int score = -qsearch(-beta, -alpha, depth - ONE_PLY);
        backward(move);
        if (score >= beta) {
            stack->bestMove.setMove(move);
            return score;
        } else if (score > alpha) {
            stack->bestMove.setMove(move);
            alpha = score;
        }
    } while ((move = movePicker->pickNextMove(this, depth, alpha, beta)));
    return alpha;
}

void TSearch::debug_print_search(int alpha, int beta) {
    std::cout << "print search (" << alpha << ", " << beta << "): " << std::endl;

    TBoard pos2;
    memcpy(&pos2, pos, sizeof (TBoard));
    while (pos2.current_ply > 0) {
        TMove * move = &getStack(pos2.current_ply - 1)->move;
        move->piece ? pos2.backward(move) : pos2.backward();
    }

    std::cout << "ROOT FEN: " << pos2.asFen() << std::endl;
    std::cout << "Path ";
    for (int i = 0; i < pos->current_ply; i++) {
        std::cout << " " << getStack(i)->move.asString();
    }
    std::cout << "\nFEN: " << pos->asFen() << std::endl;
    std::cout << "Hash: " << pos->stack->hash_code << std::endl;
    std::cout << "Nodes: " << nodes << std::endl;
    std::cout << "Skip nullmove: " << this->skipNull << std::endl;

    std::cout << std::endl;
    exit(0);
}
