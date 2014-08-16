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
#include "engine.h"
#include <cstdlib>
#include <iostream>
#include <string.h>

static const short FUTILITY_MARGIN = 50;
static const short LMR_MIN = 0; //in half plies
static const short LMR_MAX = 6; //in half plies

static const short QS_DELTA = 200; //qsearch delta pruning margin

bool TSearch::pondering() {
    return ponder || engine::is_ponder();
}

void TSearch::poll() {
    nodesUntilPoll = NODESBETWEENPOLLS;
    stopSearch = (timeManager->timeIsUp() && (!ponder || engine::is_ponder() == false))
            || engine::is_stopped()
            || (maxNodes > 0 && nodes > maxNodes);
}

std::string TSearch::getPVString() {
    std::string result = "";
    for (int i = 0; i < stack->pvCount; i++) {
        result += stack->pvMoves[i].to_string() + " ";
    }
    return result;
}

int TSearch::initRootMoves() {
    int result = 0;
    root.MoveCount = 0;
    root.FiftyCount = pos->stack->fifty_count;
    setNodeType(-score::INF, score::INF);
    rep_table::store(pos->stack->fifty_count, pos->stack->hash_code);

    int trans_move = 0;
    int trans_flags = 0;
    int score = 0;
    trans_table::retrieve(pos->stack->hash_code, 0, 0, score, trans_move, trans_flags);

    for (move_t * move = movePicker->first(this, ONE_PLY, -score::INF, score::INF);
            move; move = movePicker->next(this, ONE_PLY, -score::INF, score::INF)) {
        TRootMove * rMove = &root.Moves[root.MoveCount++];
        int move_score = 1000 - root.MoveCount;
        if (move->to_int() == trans_move) {
            move_score += 10000;
        }
        rMove->init(move, move_score, pos->gives_check(move), pos->see(move));
        if (rMove->GivesCheck) {
            rMove->checker_sq = (pos->stack + 1)->checker_sq;
            rMove->checkers = (pos->stack + 1)->checkers;
        }
    }
    root.InCheck = pos->in_check();
    stack->hash_code = pos->stack->hash_code;
    stack->eval_result = score::INVALID;
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
void TRoot::matchMoves(move::list_t * list) {
    if (list->first == list->last) {
        return;
    }
    for (int j = 0; j < MoveCount;) {
        bool match = false;
        TRootMove rMove = Moves[j];
        std::cout << rMove.Move.to_string() << " ";
        ;
        for (move_t * m = list->first; m != list->last; m++) {
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
    stack->bestMove.set(&rMove->Move);
    if (!rMove->Move.equals(&stack->pvMoves[0])) {
        stack->pvMoves[0].set(&rMove->Move);
        stack->pvCount = 1;
    }
    if (stopSearch) {
        return alpha;
    }
    if (best < alpha) {
        return best; //return to adjust the aspiration window
    }
    if (best >= beta) {
        uci::send_pv(best, depth / ONE_PLY, selDepth, nodes + pruned_nodes, timeManager->elapsed(),
                getPVString().c_str(), score::flags(best, alpha, beta));
        return best;
    }
    if (best > alpha) {
        updatePV(&rMove->Move);
        uci::send_pv(best, depth / ONE_PLY, selDepth, nodes + pruned_nodes, timeManager->elapsed(),
                    getPVString().c_str(), score::flags(best, alpha, beta));
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
            stack->bestMove.set(&rMove->Move);
            if (score >= beta) {
                if (!rMove->Move.equals(&stack->pvMoves[0])) {
                    stack->pvMoves[0].set(&rMove->Move);
                    stack->pvCount = 1;
                }
                return score;
            }
            best = score;
            if (score > alpha) {
                updatePV(&rMove->Move);
                uci::send_pv(best, depth / ONE_PLY, selDepth, nodes + pruned_nodes,
                            timeManager->elapsed(), getPVString().c_str(),
                            score::flags(best, alpha, beta));
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
int TSearch::extendMove(move_t * move, int gives_check) {
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
        if (pos->see(move) >= 0) {
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
    if ((score::MATE - pos->current_ply) < beta) {
        beta = score::MATE - pos->current_ply;
        if (alpha >= (score::MATE - pos->current_ply)) {
            return score::MATE - pos->current_ply;
        }
    }
    if ((-score::MATE + pos->current_ply) > alpha) {
        alpha = -score::MATE + pos->current_ply;
        if (beta <= (-score::MATE + pos->current_ply)) {
            return -score::MATE + pos->current_ply;
        }
    }

    /*
     * 3. Return obvious draws 
     */
    if (pos->is_draw()) { //draw by no mating material
        return drawScore();
    }
    if (pos->stack->fifty_count > 3) {
        if (pos->stack->fifty_count >= (100 + stack->in_check)) {
            return drawScore(); //draw by 50 reversible moves
        }
        int stopPly = pos->current_ply - pos->stack->fifty_count;
        for (int ply = pos->current_ply - 4; ply >= stopPly; ply -= 2) { //draw by repetition
            if (ply >= 0 && getStack(ply)->hash_code == pos->stack->hash_code) {
                return drawScore();
            } else if (ply < 0 && rep_table::retrieve(root.FiftyCount + ply) == pos->stack->hash_code) {
                return drawScore();
            }
        }
    }

    /*
     * 4. Transposition table lookup
     */
    int tmove = 0;
    int tflag = 0;
    int tscore = 0;
    if (trans_table::retrieve(pos->stack->hash_code, pos->current_ply, depth, tscore, tmove, tflag)) {
        if ((tflag == score::LOWERBOUND && tscore >= beta)
                || (tflag == score::UPPERBOUND && tscore <= alpha)
                || tflag == score::EXACT) {
            return tscore;
        }
    }
    stack->tt_move.set(tmove);

    /*
     * 5. Pruning: Fail-high and Nullmove
     */
    int eval = evaluate(this);
    int new_depth = depth - ONE_PLY;
    bool in_check = stack->in_check;
    assert(new_depth >= 0);

    //a) Fail-high pruning (return if static evaluation score is already much better than beta)
    if (!in_check
            && new_depth <= LOW_DEPTH
            && (eval - FUTILITY_MARGIN * depth) >= beta
            && beta > -score::DEEPEST_MATE
            && pos->has_pieces(pos->stack->wtm)) {
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
            && beta > -score::DEEPEST_MATE
            && pos->has_pieces(pos->stack->wtm)) {
        int rdepth = new_depth - (3 * ONE_PLY);
        rdepth = MAX(rdepth, 0);
        forward();
        int null_score = -pvs(-beta, -alpha, rdepth);
        backward();
        if (null_score >= beta) {
            if (null_score > score::DEEPEST_MATE) {
                return beta; // not return unproven mate scores
            }
            trans_table::store(pos->stack->hash_code, pos->root_ply, pos->current_ply, depth, null_score, 0, score::LOWERBOUND);
            return null_score;
        } else {
            mate_threat = null_score < -score::DEEPEST_MATE;
            if (mate_threat) {
                move_t * threat = &(stack + 1)->bestMove;
                if (!threat->capture && !threat->promotion) {
                    updateHistoryScore(threat, rdepth);
                }
            }
        }
    }



    /*
     * IID 
     */
    if (type == PVNODE && depth > LOW_DEPTH && tmove == 0) {
        skipNull = true;
        stack->bestMove.set(0);
        int iid_depth = depth - (2 * ONE_PLY) - (depth >> 2);
        iid_depth = MAX(ONE_PLY, iid_depth);
        int iid_score = pvs(alpha, beta, iid_depth);
        stack->tt_move.set(&stack->bestMove);
    }
    skipNull = false;

    /*
     * 7. Principle variation search (PVS). 
     * Generate the first move from pv, hash or internal iterative deepening,
     * all handled by the movepicker instance. 
     * If no move is returned, the position is either MATE or STALEMATE, 
     * otherwise search the first move with full alpha beta window.
     */
    move_t * first_move = movePicker->first(this, depth, alpha, beta);
    if (!first_move) { //no legal move: it's checkmate or stalemate
        return in_check ? -score::MATE + pos->current_ply : drawScore();
    }
    int gives_check = pos->gives_check(first_move);
    int extend_move = extendMove(first_move, gives_check);
    stack->bestMove.set(first_move);
    forward(first_move, gives_check);
    int best = -pvs(-beta, -alpha, new_depth + extend_move);
    backward(first_move);
    if (best > alpha) {
        if (best >= beta) {
            trans_table::store(pos->stack->hash_code, pos->root_ply, pos->current_ply, depth, best, first_move->to_int(), score::LOWERBOUND);
            if (!first_move->capture && !first_move->promotion) {
                if (best < score::DEEPEST_MATE) {
                    updateKillers(first_move);
                } else {
                    stack->mateKiller.set(first_move);
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
     * - Captures and queen promotions with a positive see value 
     * - Killer Moves
     * - Castling
     * - Non-captures with a positive static score
     * - All remaining moves
     */
    int searched_moves = 1;
    int max_reduce = MAX(0, new_depth - ONE_PLY);
    while (move_t * move = movePicker->next(this, depth, alpha, beta)) {
        assert(stack->bestMove.equals(move) == false);
        assert(first_move->equals(move) == false);
        gives_check = pos->gives_check(move);
        bool skip_prune = stack->moveList.stage < QUIET_MOVES
                || in_check || gives_check > 0 || move->capture || move->promotion
                || move->castle || passedPawn(move) || beta < -score::DEEPEST_MATE;

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
            if (pos->see(move) < 0) {
                pruned_nodes++;
                continue;
            }
        }

        /*
         * 12. Late Move Reductions (LMR) 
         */
        extend_move = extendMove(move, gives_check);
        int reduce = 0;

        if (!skip_prune && max_reduce > 0 && searched_moves >= 3) {
            reduce = searched_moves < 6 ? ONE_PLY : depth / 3;
            reduce = MIN(reduce, max_reduce);
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
            stack->bestMove.set(move);
            if (score >= beta) {
                // Beta Cutoff, hash the results, update killers and history table
                trans_table::store(pos->stack->hash_code, pos->root_ply, pos->current_ply, depth, score, move->to_int(), score::LOWERBOUND);
                if (!move->capture && !move->promotion) {
                    if (best < score::DEEPEST_MATE) {
                        updateKillers(move);
                    } else {
                        stack->mateKiller.set(move);
                    }
                    updateHistoryScore(move, depth);
                    for (move_t * cur = stack->moveList.first;
                            cur != stack->moveList.last; cur++) {
                        if (cur->score == move::EXCLUDED && cur != move) {
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
    int flag = score::flags(best, alpha, beta);
    trans_table::store(pos->stack->hash_code, pos->root_ply, pos->current_ply, depth, best, stack->bestMove.to_int(), flag);
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
        if (pos->stack->fifty_count >= (100 + stack->in_check)) {
            return drawScore();
        }
        int stopPly = pos->current_ply - pos->stack->fifty_count;
        for (int ply = pos->current_ply - 4; ply >= stopPly; ply -= 2) {
            if (ply >= 0 && getStack(ply)->hash_code == pos->stack->hash_code) {
                return drawScore();
            } else if (ply < 0 && rep_table::retrieve(root.FiftyCount + ply) == pos->stack->hash_code) {
                return drawScore();
            }
        }
    }
    if (pos->is_draw()) { //draw by no mating material
        return drawScore();
    }

    //mate distance pruning
    if ((score::MATE - pos->current_ply) < beta) {
        beta = score::MATE - pos->current_ply;
        if (alpha >= (score::MATE - pos->current_ply)) {
            return score::MATE - pos->current_ply;
        }
    }
    if ((-score::MATE + pos->current_ply) > alpha) {
        alpha = -score::MATE + pos->current_ply;
        if (beta <= (-score::MATE + pos->current_ply)) {
            return -score::MATE + pos->current_ply;
        }
    }

    int eval = evaluate(this); //always do an eval - it's incremental

    //if not in check, generate captures, promotions and (upto some plies ) quiet checks
    if (eval >= beta && !stack->in_check) { //return evaluation score is it's already above beta (stand-pat idea)
        return eval;
    }

    move_t * move = movePicker->first(this, depth, alpha, beta);
    if (!move) { //return evaluation score if there are no quiescence moves
        return stack->in_check ? -score::MATE + pos->current_ply : eval;
    }
    if (!stack->in_check) {
        alpha = MAX(eval, alpha);
    }
    stack->hash_code = pos->stack->hash_code;
    do { //loop through quiescence moves
        int gives_check = pos->gives_check(move);
        if (!stack->in_check && !move->capture && !move->promotion && !move->castle &&
                (gives_check == 0 || (gives_check == 1 && pos->see(move) < 0))) {
            pruned_nodes++;
            continue;
        }

        //pruning (delta futility and negative see), skipped for checks
        if (!stack->in_check && gives_check == 0 && NOTPV(alpha, beta)) {

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

            //2. prune moves when see is negative
            if (pos->see(move) < 0) {
                pruned_nodes++;
                continue;
            }
        }
        forward(move, gives_check);
        int score = -qsearch(-beta, -alpha, depth - ONE_PLY);
        backward(move);
        if (score >= beta) {
            stack->bestMove.set(move);
            return score;
        } else if (score > alpha) {
            stack->bestMove.set(move);
            alpha = score;
        }
    } while ((move = movePicker->next(this, depth, alpha, beta)));
    return alpha;
}

void TSearch::debug_print_search(int alpha, int beta) {
    std::cout << "print search (" << alpha << ", " << beta << "): " << std::endl;

    board_t pos2;
    memcpy(&pos2, pos, sizeof (board_t));
    while (pos2.current_ply > 0) {
        move_t * move = &getStack(pos2.current_ply - 1)->move;
        move->piece ? pos2.backward(move) : pos2.backward();
    }

    std::cout << "ROOT FEN: " << pos2.to_string() << std::endl;
    std::cout << "Path ";
    for (int i = 0; i < pos->current_ply; i++) {
        std::cout << " " << getStack(i)->move.to_string();
    }
    std::cout << "\nFEN: " << pos->to_string() << std::endl;
    std::cout << "Hash: " << pos->stack->hash_code << std::endl;
    std::cout << "Nodes: " << nodes << std::endl;
    std::cout << "Skip nullmove: " << this->skipNull << std::endl;

    std::cout << std::endl;
    exit(0);
}
