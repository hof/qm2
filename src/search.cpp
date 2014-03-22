#include "search.h"
#include <cstdlib>
#include <iostream>
#include <string.h>

static const bool DO_NULL = true; //nullmove pruning
static const bool DO_FHP = true; //fail high pruning
static const bool DO_RAZOR = true; //razoring (fail low pruning)
static const bool DO_FFP = true; //forward futility pruning
static const bool DO_LMR = true; //late move reductions
static const bool DO_EXTEND_MOVE = true; //move extensions

static const short FMARGIN[12] = {200, 200, 200, 450, 450, 600, 600, 1200, 1200, 2000, 2000, 2000}; //futility margin
static const short RMARGIN = 300; //razor margin
static const short LMR_MIN = 0; //in half plies
static const short LMR_MAX = 6; //in half plies

static const bool QS_DO_DELTA = true; //qsearch delta pruning
static const bool QS_HASH_LOOKUP = false; //hash lookup in qsearch
static const short QS_CHECKDEPTH = 1; //checkdeth in qsearch
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

void TSearch::initLMR() {
    memset(LMR, 0, sizeof (LMR));
    for (int d = 3; d < 32; d++) {
        for (int m = 2; m < 64; m++) {
            int r = BSR(m - 1) + BSR(d - 2) - 2 - (m < 4) - (m < 8);
            r += (m > 16 && d > 16);
            r += (m > 24 && d > 16);
            r += r == 1;
            r = MAX(0, r);
            r = 1 + r + (r >> 1);
            r += r == 1;
            r = MIN(r, d - 2);
            r = MIN(r, LMR_MAX + 2);
            LMR[d][m] = r;
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
        pos->boardFlags->checkerSq = rMove->checkerSq;
        pos->boardFlags->checkers = rMove->checkers;
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
            pos->boardFlags->checkerSq = rMove->checkerSq;
            pos->boardFlags->checkers = rMove->checkers;
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
    if (DO_EXTEND_MOVE == false) {
        return 0;
    }
    if (gives_check > 0) {
        if (gives_check > 1) { //double check or exposed check
            return ONE_PLY;
        }
        if (move->piece == WPAWN || move->piece == BPAWN) {
            return ONE_PLY;
        }
        if (move->capture) {
            return ONE_PLY;
        }
        if (pos->SEE(move) > -200) {
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

    /*
     * 1. If no more depth remaining, return quiescence value
     */
    if (depth < ONE_PLY) {
        stack->pvCount = 0;
        selDepth = MAX(selDepth, pos->currentPly);
        return qsearch(alpha, beta, 0, QS_CHECKDEPTH);
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

    //time check
    if (nodesUntilPoll <= 0) {
        poll();
    }
    if (stopSearch || pos->currentPly >= MAX_PLY) {
        return alpha;
    }

    /*
     * 3. Return obvious draws 
     */
    if (pos->boardFlags->fiftyCount > 3) {
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

    bool in_check = stack->inCheck;
    if (!in_check && pos->isDraw()) { //draw by no mating material
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
     * 5. Pruning: Fail-high, Razoring and Nullmove
     */

    int eval = evaluate(this, alpha, beta);

    //a) Fail-high pruning (return if static eval score is already much better than beta)
    if (DO_FHP
            && !in_check
            && depth <= LOW_DEPTH
            && (eval - FMARGIN[depth]) >= beta
            && ABS(beta) < SCORE_MATE - MAX_PLY
            && pos->hasPieces(pos->boardFlags->WTM)) {
        return eval - FMARGIN[depth];
    }

    //b) Razoring (fail-low pruning, return if static eval score is much below alpha)
    int type = setNodeType(alpha, beta);
    assert(type != UNKNOWN);
    assert(type == PVNODE || alpha + 1 == beta);
    if (DO_RAZOR
            && type != PVNODE
            && depth <= LOW_DEPTH
            && !in_check
            && eval + RMARGIN < alpha) {
        if (depth < (2 * ONE_PLY)) {
            return qsearch(alpha, beta, 0, QS_CHECKDEPTH);
        }
        int rmargin = VPAWN * (depth + 1);
        if (eval + rmargin < alpha) {
            int score = qsearch(alpha, beta, 0, QS_CHECKDEPTH);
            if (score < alpha) {
                return score;
            }
        }
    }

    //c) Nullmove pruning
    stack->hashCode = pos->boardFlags->hashCode;
    bool mate_threat = false;
    int new_depth = depth - ONE_PLY;
    if (DO_NULL
            && !skipNull
            && !in_check
            && depth > ONE_PLY
            && eval >= beta
            && ABS(beta) < SCORE_MATE - MAX_PLY
            && pos->hasPieces(pos->boardFlags->WTM)) {
        int rdepth = new_depth - (2.5 * ONE_PLY) - (depth >> 3);
        forward();
        int null_score = -pvs(-beta, -alpha, rdepth);
        backward();
        if (null_score >= beta) {
            if (null_score >= SCORE_MATE - MAX_PLY) {
                return beta; // not return unproven mate scores
            }
            return null_score;
        } else if (alpha > (-SCORE_MATE + MAX_PLY) && null_score < (-SCORE_MATE + MAX_PLY)) {
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
     * all handled by the movepicker instance. 
     * If no move is returned, the position is either MATE or STALEMATE, 
     * otherwise search the first move with full alpha beta window.
     */
    TMove * first_move = movePicker->pickFirstMove(this, depth, alpha, beta);
    if (!first_move) { //no legal move: it's checkmate or stalemate
        stack->pvCount = 0;
        return in_check ? -SCORE_MATE + pos->currentPly : drawScore();
    }
    int gives_check = pos->givesCheck(first_move);
    int extend_move = extendMove(first_move, gives_check);
    
    stack->bestMove.setMove(first_move);
    stack->reduce = 0;
    forward(first_move, gives_check);
    int best = -pvs(-beta, -alpha, new_depth + extend_move);
    backward(first_move);
    if (best > alpha) {
        if (best >= beta) {
            hashTable->ttStore(this, first_move->asInt(), best, depth, alpha, beta);
            if (!first_move->capture && !first_move->promotion) {
                if (best < SCORE_MATE - MAX_PLY) {
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
    const int max_reduce = MIN(new_depth - ONE_PLY, LMR_MAX);
    while (TMove * move = movePicker->pickNextMove(this, depth, alpha, beta)) {
        assert(stack->bestMove.equals(move) == false);
        assert(first_move->equals(move) == false);
        gives_check = pos->givesCheck(move);
        bool active_move = gives_check > 0 || move->capture || move->promotion || move->castle || passedPawn(move);

        /*
         * 11. forward futility pruning at low depths
         * (moves without potential are skipped)
         */
        if (DO_FFP
                && stack->moveList.stage >= QUIET_MOVES
                && type != PVNODE
                && !in_check
                && !active_move
                && (eval < alpha || best >= alpha)
                && new_depth <= LOW_DEPTH
                ) {
            int mc_max = 2 + ((depth * depth) >> 2);
            if (searched_moves > mc_max) {
                pruned_nodes++;
                continue;
            }
            if (eval + FMARGIN[new_depth] <= alpha) {
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
        if (DO_LMR
                && new_depth > ONE_PLY
                && stack->moveList.stage >= QUIET_MOVES
                && !in_check
                && !active_move) {
            assert(new_depth >= 0);
            assert(searched_moves >= 1);
            reduce = LMR[MIN(new_depth, 31)][MIN(63, searched_moves)];
            reduce -= (type == PVNODE)*2;
            reduce += (type == CUTNODE)*2;
            reduce += (eval + 50) <= alpha;
            reduce += history[move->piece][move->tsq] < 0;
            reduce = RANGE(LMR_MIN, max_reduce, reduce);
        }
        stack->reduce = reduce;

        /*
         * 13. Search one ply deeper
         */
        forward(move, gives_check);
        int score = -pvs(-alpha - 1, -alpha, new_depth - reduce + extend_move);
        if (score > alpha && reduce > 0) {
            //research without reductions
            (stack - 1)->reduce = 0;
            score = -pvs(-alpha - 1, -alpha, new_depth + extend_move);
        }
        if (score > alpha) {
            //full window research
            (stack - 1)->reduce = 0;
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
                /*
                 * Beta Cutoff
                 * Store result in hash table, update killers and history table
                 */
                hashTable->ttStore(this, move->asInt(), score, depth, alpha, beta);
                if (!move->capture && !move->promotion) {
                    if (best < SCORE_MATE - MAX_PLY) {
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
                    assert(xMoves <= searched_moves);
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

int TSearch::qsearch(int alpha, int beta, int qPly, int checkDepth) {

    nodes++;

    //time check
    if (--nodesUntilPoll <= 0) {
        poll();
    }
    if (stopSearch || pos->currentPly >= MAX_PLY) {
        return alpha;
    }

    //return obvious draws
    if (pos->boardFlags->fiftyCount > 3) {
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

    /*
     * Hash table lookup
     */
    if (QS_HASH_LOOKUP) {
        hashTable->ttLookup(this, 0, alpha, beta);
        if (stack->ttScore != TT_EMPTY && excludedMove.piece == EMPTY) {
            stack->pvCount = 0;
            return stack->ttScore;
        }
    }

    /*
     * Check Evasion
     */
    if (stack->inCheck) {
        assert(pos->boardFlags->checkers);
        TMove * move = movePicker->pickFirstQuiescenceMove(this, 0, alpha, beta);
        if (!move) { //no move? checkmate!
            return -SCORE_MATE + pos->currentPly;
        }
        int best = -SCORE_INFINITE;
        stack->hashCode = pos->boardFlags->hashCode;
        do {
            bool givesCheck = pos->givesCheck(move);
            forward(move, givesCheck);
            int score = -qsearch(-beta, -alpha, qPly + 1, checkDepth);
            backward(move);
            if (score > best) {
                stack->bestMove.setMove(move);
                if (score >= beta) {
                    return score;
                }
                best = score;
                if (score > alpha) {
                    alpha = score;
                }
            }
        } while (move = movePicker->pickNextMove(this, 0, alpha, beta));
        return best;
    }

    /*
     * Not in check
     */

    //return obvious draws
    if (pos->isDraw()) {
        return drawScore();
    }

    int best = evaluate(this, alpha, beta); //the "stand-pat" score

    //return evaluation score is it's already above beta
    if (best >= beta) {
        return best;
    }

    TMove * move = movePicker->pickFirstQuiescenceMove(this, qPly < checkDepth, alpha, beta);

    //return evaluation score if there are no quiescence moves
    if (!move) {
        return best;
    }

    //loop through quiescence moves (captures, promotions and checks)
    bool pv_node = ISPV(alpha, beta);
    alpha = MAX(best, alpha);
    stack->hashCode = pos->boardFlags->hashCode;
    int base = best;
    do {
        int givesCheck = pos->givesCheck(move);
        if (!move->capture && !move->promotion &&
                (givesCheck == 0 || (givesCheck == 1 && !pv_node && pos->SEE(move) < 0))) {
            pruned_nodes++;
            continue;
        }

        //pruning (delta futility and negative see), skipped for checks and PV nodes
        if (QS_DO_DELTA && givesCheck <= 0 && !pv_node) {

            //1. delta futility pruning: the captured piece + max. positional gain should raise alpha
            int gain = PIECE_VALUE[move->capture];
            if (move->promotion) {
                if (move->promotion != WQUEEN && move->promotion != BQUEEN) {
                    pruned_nodes++;
                    continue;
                }
                gain += PIECE_VALUE[move->promotion] - VPAWN;
            }
            if (base + gain + QS_DELTA < alpha) {
                pruned_nodes++;
                continue;
            }

            //2. prune moves when SEE is negative
            if (PIECE_VALUE[move->piece] > PIECE_VALUE[move->capture] && pos->SEE(move) < 0) {
                pruned_nodes++;
                continue;
            }
        }

        forward(move, givesCheck);
        int score = -qsearch(-beta, -alpha, qPly + 1, checkDepth);
        backward(move);

        if (score >= beta) {
            stack->bestMove.setMove(move);
            return score;
        }
        if (score > best) {
            best = score;
            stack->bestMove.setMove(move);
            if (score > alpha) {
                alpha = score;
            }
        }
    } while (move = movePicker->pickNextMove(this, qPly < checkDepth, alpha, beta));
    return best;
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
