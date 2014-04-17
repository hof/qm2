#include <cstdlib>
#include <iostream>
#include <unistd.h>
#include <time.h>
#include <istream>
#include <sstream>
#include <iomanip>  //setw
#include <math.h> 

#include "engine.h"
#include "board.h"
#include "movegen.h"
#include "book.h"
#include "evaluate.h"
#include "search.h"
#include "search.h"
#include "bbmoves.h"
#include "timemanager.h"
#include "inputhandler.h"
#include "outputhandler.h"
#include "score.h"

using namespace std;

const short ONE_SECOND = 1000;

/**
 * Thread function for searching and finding a best move in a chess position
 * @param engineObjPtr pointer to the (parent) engine object
 * @return NULL
 */
void * TEngine::_think(void* engineObjPtr) {
    /*
     * Initialize:
     * - searchData object
     * - time management
     * - game settings
     * - thinking stop conditions
     */
    TEngine * engine = (TEngine*) engineObjPtr;
    TGameSettings game = engine->gameSettings;
    game.opponent.copy(&engine->gameSettings.opponent);

    TSearch * searchData = new TSearch(engine->_rootFen.c_str(),
            engine->_hashTable,
            engine->_outputHandler);

    TBoard * root = searchData->pos;
    TTimeManager * tm = searchData->timeManager;

    int maxDepth = game.maxDepth;
    int maxNodes = game.maxNodes;
    int maxTime = game.maxTimePerMove;
    int whiteTime = game.whiteTime;
    int blackTime = game.blackTime;
    int whiteInc = game.whiteIncrement;
    int blackInc = game.blackIncrement;
    int movesToGo = game.movesLeft;
    TMove targetMove = game.targetMove;
    int targetScore = game.targetScore;
    bool ponder = game.ponder;
    int learnParam = game.learnParam;
    double learnFactor = game.learnFactor;
    evaluate(searchData);



    tm->setStartTime();
    int myTime = root->boardFlags->WTM ? whiteTime : blackTime;
    int oppTime = root->boardFlags->WTM ? blackTime : whiteTime;
    int myInc = root->boardFlags->WTM ? whiteInc : blackInc;
    int oppInc = root->boardFlags->WTM ? blackInc : whiteInc;

    if (maxTime) {
        tm->setEndTime(maxTime);
        tm->setMaxTime(maxTime);
    } else if (whiteTime || blackTime) {
        tm->set(myTime, oppTime, myInc, oppInc, movesToGo);
    } else {
        tm->setEndTime(INFINITE_TIME);
        tm->setMaxTime(INFINITE_TIME);
    }

    searchData->drawContempt.set(-50, 8);
    if ((whiteTime || blackTime) && myInc == 0 && myTime < oppTime && myTime < 20000) {
        if (myTime < 2000) {
            searchData->drawContempt.set(100);
        } else {
            searchData->drawContempt.set(50);
        }
    }
    if (!root->boardFlags->WTM) {
        searchData->drawContempt.mul(-1);
    }

    searchData->maxNodes = maxNodes;
    searchData->learnFactor = learnFactor;
    searchData->ponder = ponder;

#ifdef PRINTSEARCH
    if (examineSearch) {
        searchData->examineSearch = examineSearch;
        for (int i = 0; i < examineSearch; i++) {
            searchData->examinePath[i].setMove(&game.examinePath[i]);
        }
    }
#endif

    /*
     * Claim draws by 
     * - fifty move rule
     * - lack of material
     * - repetition
     */

    /*
     * Find and play a book move if available, looking up 
     * a Polyglot Book file named "book.bin"
     */
    TMove resultMove;
    TMove ponderMove;
    resultMove.setMove(0);
    ponderMove.setMove(0);
    TBook * book = new TBook();
    book->open("book.bin");
    bool book_move = false;
    bool book_ponder_move = false;
    for (int book_step = 0; book_step < 2; book_step++) {
        TMoveList * bookMoves = &searchData->stack->moveList;
        int count = book->findMoves(root, bookMoves);
        if (count > 0) {
            srand(time(NULL));
            int randomScore = 0;
            int totalBookScore = 1;
            for (int pickMove = 0; pickMove < 2; pickMove++) {
                int totalScore = 0;
                for (TMove * bookMove = bookMoves->first; bookMove != bookMoves->last; bookMove++) {
                    totalScore += bookMove->score;
                    if (pickMove && totalScore >= randomScore) {
                        if (book_step == 0) {
                            book_move = true;
                            resultMove.setMove(bookMove);
                            engine->setMove(bookMove);
                            engine->setScore(0);
                            root->forward(&resultMove);
                        } else if (book_step == 1) {
                            book_ponder_move = true;
                            ponderMove.setMove(bookMove);
                            if (searchData->outputHandler) {
                                std::string book_pv = resultMove.asString() + " " + ponderMove.asString();
                                searchData->outputHandler->sendPV((bookMove->score) / totalBookScore, 1, 1, count, searchData->timeManager->elapsed(), book_pv.c_str(), (int) EXACT);
                            }
                        }
                        break;
                    }
                }
                totalBookScore = totalScore;
                randomScore = (rand() % totalScore) + 1;
            }
        }
    }
    if (book_move) {
        root->backward(&resultMove);
    }

    /*
     * Find a move by Principle Variation Search (fail-soft) in an 
     * internal iterative deepening framework with aspiration search.
     */

    if (book_ponder_move == false && searchData->initRootMoves() > 0) {
        if (book_move) {
            //no ponder move.. only consider book_moves, but let the engine decide which one to play
            book->findMoves(root, &searchData->stack->moveList);
            searchData->root.matchMoves(&searchData->stack->moveList);
            tm->requestLessTime();
        } else if (searchData->root.MoveCount == 1) {
            tm->requestLessTime();
        }
        searchData->stack->eval_result = evaluate(searchData);
        int alpha = -SCORE_INFINITE;
        int beta = SCORE_INFINITE;
        int prev_score = -SCORE_INFINITE;
        const int windows[] = {20, 40, 80, 160, 320, 640, 1280, SCORE_INFINITE, SCORE_INFINITE};
        const int MAX_WINDOW = 2 * VQUEEN;
        int alpha_window = 0;
        int beta_window = 0;
        int lowest = SCORE_INFINITE;
        int highest = -SCORE_INFINITE;
        int depth = ONE_PLY;
        int resultScore = 0;
        bool move_changed = false;
        bool score_changed = false;
        bool easy_move = true;
        while (depth <= maxDepth * ONE_PLY && !searchData->stopSearch) {

            int iteration_start_time = tm->elapsed();

            //std::cout << "pvs (" << alpha << ", " << beta << ", " << depth << ")" << std::endl;

            int score = searchData->pvs_root(alpha, beta, depth);
            int type = score <= alpha ? FAILLOW : score >= beta ? FAILHIGH : EXACT;

            /*
             * Update and output PV
             */
            if (!searchData->stopSearch) {
                resultScore = score;
            }
            if (searchData->stack->pvCount > 0) {
                TMove firstMove = searchData->stack->pvMoves[0];
                if (firstMove.piece) {
                    move_changed = resultMove.equals(&firstMove) == false;
                    resultMove.setMove(&firstMove);
                    ponderMove.setMove(0);
                    if (searchData->stack->pvCount > 1) {
                        ponderMove.setMove(&searchData->stack->pvMoves[1]);
                    }
                    engine->setMove(&firstMove);
                    engine->setScore(resultScore);
                }
                if (searchData->outputHandler && searchData->stopSearch) {
                    searchData->outputHandler->sendPV(resultScore, depth / ONE_PLY, searchData->selDepth,
                            searchData->nodes + searchData->pruned_nodes, tm->elapsed(), searchData->getPVString().c_str(), type);
                }
            }

            /*
             * Increase time for time based search when 
             * - We opened the aspiration window on high depths
             * - Evaluation shows large positional values
             * - PV or pondermove is not set
             */
            score_changed = ABS(prev_score - score) > (VPAWN / 4);
            easy_move &= move_changed == false;
            easy_move &= score_changed == false;
            
            if (!searchData->stopSearch && depth > HIGH_DEPTH && !book_move) {
                if (ponderMove.piece == EMPTY) {
                    tm->requestMoreTime();
                } else if (move_changed) {
                    tm->requestMoreTime();
                } else if (score_changed) {
                    tm->requestMoreTime();
                } else if (easy_move) {
                    easy_move = false;
                    tm->requestLessTime();
                }
            }

            /* 
             * Stop conditions
             */

            //stop if running a test and the move and score are found
            if (targetScore && targetMove.piece && targetMove.equals(&resultMove) && score >= targetScore) {
                engine->setTestResult(true);
                break;
            }

            //stop if stopsearch is set (time is up) or max amount of nodes is reached
            searchData->poll();
            if (searchData->stopSearch
                    || (maxNodes > 0 && searchData->nodes > maxNodes)) {
                break;
            }

            //stop if a mate is found and the iteration depth is deeper than the mate depth
            if ((MATE_IN_PLY(resultScore) && type == EXACT && depth / ONE_PLY > MATE_IN_PLY(resultScore))
                    || (MATED_IN_PLY(resultScore)) && type == EXACT && depth / ONE_PLY > MATED_IN_PLY(resultScore)) {
                break;
            }

            //stop if there is no time to find a new pv in a next iteration
            int iteration_time = tm->elapsed() - iteration_start_time;
            if (type == EXACT && !tm->available(iteration_time / 2)) {                
                break;
            }

            /*
             * Prepare next search
             * - Increase depth if the score is between the bounds
             * - Otherwise open the aspiration window further and research 
             *   on the same depth. Open it fully in case of mate-scores.
             */
            lowest = MIN(score, lowest);
            highest = MAX(score, highest);

            if (score <= alpha) {
                alpha_window++;
                alpha = MIN(lowest, score - windows[alpha_window]);
            } else if (score >= beta) {
                beta_window++;
                beta = MAX(highest, score + windows[beta_window]);
            } else {
                alpha = MIN(lowest, score - windows[0]);
                beta = MAX(highest, score + windows[0]);
                alpha_window = beta_window = 0;
                //reset highest and lowest every 5 ply
                if (depth % 10 == 0) {
                    lowest = MAX(lowest, score - windows[1]);
                    highest = MIN(highest, score + windows[1]);
                }
                depth += ONE_PLY;
            }

            if (alpha < -MAX_WINDOW) {
                alpha = -SCORE_INFINITE;
            } else {
                alpha = ((alpha + 1) & ~1) - 1; //make uneven
            }
            if (beta > MAX_WINDOW) {
                beta = SCORE_INFINITE;
            } else {
                beta = ((beta - 1) & ~1) + 1; //make uneven
            }

            prev_score = score;
            move_changed = false;
            searchData->root.sortMoves();
        }

        /*
         * Update and output search statistics
         */
        engine->setNodesSearched(searchData->nodes);
        bool sendDebugInfo = false;
        if (searchData->nodes && sendDebugInfo && searchData->outputHandler) {

            int hashHits = searchData->hashProbes ? U64(searchData->hashHits * 100) / searchData->hashProbes : 0;
            int ptHits = searchData->pawnTableProbes ? U64(searchData->pawnTableHits * 100) / searchData->pawnTableProbes : 0;
            int mtHits = searchData->materialTableProbes ? U64(searchData->materialTableHits * 100) / searchData->materialTableProbes : 0;
            int etHits = searchData->evalTableProbes ? U64(searchData->evalTableHits * 100) / searchData->evalTableProbes : 0;

            searchData->outputHandler->sendEvalStats(
                    searchData->evaluations,
                    searchData->pawnEvals,
                    searchData->fullEvaluations);
            searchData->outputHandler->sendHashTableStats(hashHits, ptHits, mtHits, etHits);
        }
    }
    if (searchData->outputHandler) {
        searchData->outputHandler->sendBestMove(resultMove, ponderMove);
    }

    /*
     * Clean up and terminate the thinking thread
     */
    delete searchData;
    delete book;
    pthread_exit(NULL);
    return NULL;
}

/**
 * Analyse a chess position by: 
 * - evaluation function
 * - quiescence search
 * - scoring each move using a minimal depth 0 search
 * 
 * This function is mainly useful for testing the above components and the 
 * move ordering by visual inspection of the output.
 */

void print_row(std::string cap, short score) {
    std::cout << std::setw(14);
    std::cout << cap;
    std::cout << " | ";
    std::cout << std::setw(32);
    std::cout << " | ";
    std::cout << std::setw(20) << score;
    std::cout << std::endl;
}

void print_row(std::string cap, TScore & w, TScore & b, int phase) {
    TScore total;
    total.clear();
    total.add(w);
    total.sub(b);
    std::cout << std::setw(14);
    std::cout << cap;
    std::cout << " | ";
    std::cout << std::setw(8) << w.mg;
    std::cout << " ";
    std::cout << std::setw(4) << w.eg;
    std::cout << "  ";
    std::cout << std::setw(9) << b.mg;
    std::cout << " ";
    std::cout << std::setw(4) << b.eg;
    std::cout << " | ";
    std::cout << std::setw(9) << total.mg;
    std::cout << " ";
    std::cout << std::setw(4) << total.eg;
    std::cout << " ";
    std::cout << std::setw(5) << total.get(phase);
    std::cout << std::endl;
}

void TEngine::analyse() {

    TSearch * s = new TSearch(_rootFen.c_str(), _hashTable, _outputHandler);
    s->stack->eval_result = evaluate(s);
    int phase = s->stack->phase;
    TScore w, b, t;
    w.clear();
    b.clear();

    std::cout << s->pos->asFen().c_str() << std::endl;
    std::cout << "Eval Component | White MG   EG | Black MG   EG |  Total MG   EG   Tot  \n";
    std::cout << "---------------+---------------+---------------+---------------------\n";
    print_row("Game phase", phase);
    print_row("Material", s->stack->material_score);
    print_row("Pawns & Kings", s->stack->pawn_score.get(phase));
    print_row("Knights", s->stack->knight_score[WHITE], s->stack->knight_score[BLACK], phase);
    print_row("Bishops", s->stack->bishop_score[WHITE], s->stack->bishop_score[BLACK], phase);
    print_row("Rooks", s->stack->rook_score[WHITE], s->stack->rook_score[BLACK], phase);
    print_row("Queens", s->stack->queen_score[WHITE], s->stack->queen_score[BLACK], phase);
    print_row("Passers", s->stack->passer_score[WHITE], s->stack->passer_score[BLACK], phase);
    print_row("King Attack", s->stack->king_score[WHITE], s->stack->king_score[BLACK], phase);
    std::cout << "---------------+---------------+---------------+---------------------\n";
    print_row("Total", s->pos->boardFlags->WTM ? s->stack->eval_result : -s->stack->eval_result);

    delete s;
}

void TEngine::setInputHandler(TInputHandler * inputHandler) {
    _inputHandler = inputHandler;
}

void TEngine::setOutputHandler(TOutputHandler * outputHandler) {
    _outputHandler = outputHandler;
}

void TEngine::testPosition(TMove bestMove, int score, int maxTime, int maxDepth) {
    _testSucces = false;
    gameSettings.targetMove = bestMove;
    gameSettings.targetScore = score;
    gameSettings.maxTimePerMove = maxTime;
    gameSettings.maxDepth = maxDepth ? maxDepth : MAX_PLY;
}

/**
 * Learning by playing self-matches. 
 * A factor for experimental evaluation is determined by playing GAMECOUNT 
 * shallow fixed depth games between engine(x) and engine(y), where x and y
 * represent a learning factor. 
 * 
 * Typically, depth is set to <= 3 and the amount of games >= 1000 to obtain
 * significant results. 
 * 
 * A book.bin file is required to pre-calculate sets of random start positions.
 * 
 * The first match is always played between engine(1) and engine(-1), or
 * full positive experimental evaluation score vs the exact opposite: the same 
 * evaluation score but negative. 
 * E.g. when the experimental evaluation is about avoiding placing the King in the center 
 * of the board during the opening phase (generally a very bad idea), engine (1)
 * will avoid it and engine(-1) will do the opposite and actually place the King 
 * in the center. This is the first step to verify the evaluation as 
 * it's expected that engine(1) will proof to be much stronger than engine(-1) with 
 * just a few games. 
 * 
 * An upperbound and lowerbound is calculated after each match. This represents
 * the maximum and minimum value for the best factor. If engine(1) was the winner 
 * of the first match, the upperbound is still unknown and remains set to it's 
 * initial value of +infinity (32000). The lowerbound is set to -1 + one minimum
 * learning step of 0.1: -0.9. 
 * 
 * The next match is played between engine(1) and engine(0), to verify if 
 * using the evaluation (engine(1) is better than not using it at all (engine(0)).
 * This step also determines the amount of games per match (with a minimum of 
 * 2000 and a maximum of 100000).
 * 
 * If the first two steps did not result in significant results, the learning process
 * is aborted because the impact of the evaluation can't be measured. 
 * 
 * Otherwise, the process continues. The best scoring engine, at this point this can
 * be engine(1), engine(0) or engine(-1), will play against an opponent engine.
 * To determine the opponent engine, it's considered the distance between  
 * the best factor to the upper or lowerbound. The higher distance is used, to try 
 * and get a better upper or lowerbound. 
 * 
 * The process goes on until best factor is close to enough (<= 0.2) to both the lower and
 * upper bound.
 * 
 * This learning method gives a (rough) indication if the exp. evaluation is 
 * plausible. It does not replace engine testing under tournament conditions.
 * 
 * Example run for learning the value of a queen:
 * 
 * First batch run (step 1:)
 
Engine(1) vs Engine(-1)
b+++++>
Games: 250 Win: 160 Loss: 80 Draw: 10 Score: 165/250 (66%, 80, Elo: 115)
Engine(1) is significantly stronger than engine(-1)

Best factor: 1
Lower bound: -0.9
Upper bound: 32000

 * So, it's better to keep a queen than to sacrifice a queen (duh!). Just 250
 * games were needed to measure a significant result. The 'b' character indicates
 * a set of 250 starting positions were generated from the book.bin file. A +, - or 
 * = sign represents 50 games, with almost significant results (+ for winning or
 * - for loosing) or equal (=).
 * The '>' is an early cutoff, no more games are needed to proof a likelihood of 
 * superiority. 
 * The lowerbound is adjusted to -0.9. The upperbound remains unknown.
 * 
 * Step 2:
 * 
Engine(1) vs Engine(0)
b==+++b+>
Games: 300 Win: 166 Loss: 121 Draw: 13 Score: 172.5/300 (57.5%, 45, Elo: 53)
Engine(1) is significantly stronger than engine(0)

Best factor: 1
Lower bound: 0.1
Upper bound: 32000
Setting batch size to 2000
 
 * It's better to have a queen than to not have queen. The batch size is set
 * to 2000 and the lowerbound is adjusted. 
 * 
 * At step 5, the match did not result in a significant result and the upperbound 
 * is set:

Engine(5) vs Engine(7)
b=====b=====b=====b=====b=====b=====b=====b====-
Games: 2000 Win: 881 Loss: 948 Draw: 171 Score: 966.5/2000 (48.325%, -67, Elo: -
12)
Engine(5) is a bit weaker than engine(7)

Best factor: 7
Lower bound: 4.3
Upper bound: 15
 * 
 * And the last step, after playing 53540 games:
 * 
 Engine(9.3) vs Engine(9.5)
b=====b=====b=====b=====b=====b=====b=====b=====
Games: 2000 Win: 897 Loss: 893 Draw: 210 Score: 1002/2000 (50.1%, 4, Elo: 1)
Engine(9.3) is a bit stronger than engine(9.5)

Best factor: 9.3
Lower bound: 9.1
Upper bound: 9.5

Elapsed: 393.439s.
Games played: 53450 games. (135.853 games/sec)
Nodes Total: 506986082 nodes. (1288601 nodes/sec)
 * 
 * So a value of 9.3 for the queen gave best results. 
 * 
 * 
 * @param engineObjPtr pointer to engine object
 * @return void
 */
void * TEngine::_learn(void * engineObjPtr) {

    /*
     * Constants
     */

    const int MAXDEPTH = 1; //default: depth 2
    const int MINGAMESCOUNT = 3350;
    const int MAXGAMESCOUNT = 100000; //step 0: 1/10, step 1: 1/2, step2: upto 1/1

    double MAX_WINDOW = 2.0; //maximum adjustment step for lower/upper bound
    double MIN_WINDOW_ADJ = 0.1; //minimum window adjustment
    double STOP_WINDOW = 0.24; //stop if the difference between lower and upperbound is <= this value
    const int STOPSCORE = 120; //if the score is higher than this for both sides, the game is consider a win
    const int MAXPLIES = 200; //maximum game length in plies
    const int HASH_SIZE_IN_MB = 1;


    /*
     * Initialize, normally it's not needed to change anything from here
     */

    THashTable * hash1 = new THashTable(HASH_SIZE_IN_MB); //each side uses it's own hash table
    THashTable * hash2 = new THashTable(HASH_SIZE_IN_MB);
    TEngine * engine = (TEngine*) engineObjPtr;
    TSearch * sd_root = new TSearch(engine->_rootFen.c_str(),
            hash1, NULL);
    TSearch * sd_game = new TSearch(engine->_rootFen.c_str(),
            hash1, NULL);

    engine->gameSettings.maxDepth = MAXDEPTH;
    sd_root->timeManager->setEndTime(INFINITE_TIME);
    sd_game->timeManager->setEndTime(INFINITE_TIME);

    int x = 0;
    double bestFactor = 1.0;
    double upperBound = SCORE_INFINITE;
    double lowerBound = -SCORE_INFINITE;


    /*
     * Prepare GAMECOUNT/2 starting positions, using the opening book
     * Each starting position is played twice, with the engine colors reversed
     */

    std::cout << "\nLEARNING MODE" << std::endl;
    std::cout << "Depth: " << MAXDEPTH << std::endl;

    TBook * book = new TBook();
    book->open("book.bin");
    string start_positions[MAXGAMESCOUNT + 1];
    for (int p = 0; p < MAXGAMESCOUNT + 1; p++) {
        start_positions[p] = "";
    }

    /*
     * Self-play, using the generated start positions once for each side
     */
    THashTable * hash_list[2] = {hash1, hash2};
    TMove actualMove;
    U64 gamesPlayed = 0;
    U64 batch = 0;
    U64 totalNodes[2] = {0, 0};
    clock_t begin;
    begin = clock();
    std::string fen = "";
    srand(time(NULL));
    double strongest = +bestFactor;
    double opponent = -bestFactor;

    U64 step = 0;
    int maxgames = MAXGAMESCOUNT / 10;
    while ((upperBound - strongest) > STOP_WINDOW || (strongest - lowerBound) > STOP_WINDOW) {
        int stats[3] = {0, 0, 0}; //draws, wins for learning side, losses for learning side
        U64 nodes[2] = {0, 0}; //total node counts for both sides
        std::cout << "\nEngine(" << strongest << ") vs Engine(" << opponent << ")" << std::endl;
        hash1->clear(); //clear hash: the evaluation scores changed.
        hash2->clear();
        batch = 0;
        x = 0;
        for (int game = 0; game < maxgames; game++) {
            //go par
            if (x <= game) {
                engine->_create_start_positions(sd_root, book, start_positions, x, maxgames);
            }

            fen = start_positions[game];
            if (fen == "") {
                upperBound = 0;
                lowerBound = 0;
                std::cout << "\nError: no start position (book.bin missing?)" << std::endl;
                break;
            }
            sd_game->pos->fromFen(fen.c_str());
            int plyCount = 0;
            int prevScore = 0;
            bool gameover = false;
            do {
                for (int side_to_move = 0; side_to_move < 2; side_to_move++) {
                    /*
                     * Toggle learnParam to 1 or 0 to enable/disable experimental evaluation
                     * Play each position twice: 
                     * 1) engine(learn) vs engine(base)
                     * 2) engine(base) vs engine(learn)
                     */
                    if (game % 2 == 0) {
                        sd_game->learnFactor = side_to_move ? strongest : opponent;
                    } else {
                        sd_game->learnFactor = side_to_move ? opponent : strongest;
                    }
                    bool learning_side = sd_game->learnFactor == strongest;


                    /*
                     * Prepare search: cleanup and reset search stack. 
                     */
                    actualMove.clear();
                    sd_game->resetStack();
                    int moveCount = sd_game->initRootMoves();
                    if (moveCount == 0) {
                        if (sd_game->pos->inCheck()) { //current engine lost
                            stats[1 + learning_side]++;
                        } else {
                            stats[0]++; //draw by stalemate. 
                        }
                        gameover = true;
                        break;

                    }
                    sd_game->stack->eval_result = evaluate(sd_game);
                    sd_game->hashTable = hash_list[learning_side]; //each side has it's own hash
                    int depth = ONE_PLY;
                    int resultScore = 0;

                    /*
                     * Normal PVS search without aspiration windows
                     */
                    sd_game->stopSearch = false;
                    while (depth <= (MAXDEPTH) * ONE_PLY && !sd_game->stopSearch) {
                        int score = sd_game->pvs_root(-SCORE_INFINITE, SCORE_INFINITE, depth);
                        if (!sd_game->stopSearch) {
                            resultScore = score;
                        }
                        if (sd_game->stack->pvCount > 0) {
                            TMove firstMove = sd_game->stack->pvMoves[0];
                            if (firstMove.piece) {
                                actualMove.setMove(&firstMove);
                            }
                        }
                        depth += ONE_PLY;
                        sd_game->root.sortMoves();
                    }

                    nodes[1 - learning_side] += sd_game->nodes;

                    //stop conditions
                    if (sd_game->pos->boardFlags->fiftyCount >= 20 || sd_game->pos->isDraw()) {
                        stats[0]++; //draw
                        gameover = true;
                        break;
                    }

                    if (resultScore > STOPSCORE && prevScore < -STOPSCORE) {
                        //current engine won and both engines agree
                        stats[2 - learning_side]++;
                        gameover = true;
                        break;
                    }

                    if (resultScore < -STOPSCORE && prevScore > STOPSCORE) {
                        //current engine lost and both engines agree
                        stats[1 + learning_side]++;
                        gameover = true;
                        break;
                    }

                    if (plyCount >= MAXPLIES) {
                        //too long game, abort as draw
                        stats[0]++; //draw
                        gameover = true;
                        break;
                    }

                    prevScore = resultScore;
                    plyCount++;
                    //std::cout << plyCount << ": " << resultScore << " " << actualMove.asString() << " " << sd_game->pos->asFen() << std::endl; //for debugging
                    sd_game->forward(&actualMove, sd_game->pos->givesCheck(&actualMove));
                }

            } while (gameover == false);
            //end go par
            gamesPlayed++;
            batch++;
            if (gamesPlayed % 50 == 0) {
                double los = 0.5 + 0.5 * erf((stats[1] - stats[2]) / sqrt(2.0 * (stats[1] + stats[2])));
                if (los > 0.8) {
                    std::cout << '+';
                } else if (los < 0.2) {
                    std::cout << '-';
                } else {
                    std::cout << '=';
                }
                std::cout.flush();
                //check if the result if significant enough before finishing the full batch
                if (batch > 200) {
                    double batch_adj = MIN(0.04, batch / 100000.0);
                    bool winner = los > (0.99 - batch_adj);
                    bool looser = los < (0.01 + batch_adj);
                    if (winner) {
                        std::cout << ">";
                        break;
                    }
                    if (looser) {
                        std::cout << '<';
                        break;
                    }
                }

            }
        }
        double points = stats[1] + 0.5 * stats[0];
        int maxPoints = batch;
        int wins = stats[1] - stats[2];
        totalNodes[0] += nodes[0];
        totalNodes[1] += nodes[1];
        double score = (100.0 * points) / maxPoints;
        int elo = round(-400.0 * log(1 / (points / maxPoints) - 1) / log(10));
        double los = 0.5 + 0.5 * erf((stats[1] - stats[2]) / sqrt(2.0 * (stats[1] + stats[2])));
        double los_p = los * 100;
        std::cout << "\nGames: " << batch << " " << stats[1] << "-" << stats[2] << "-" << stats[0]
                << " (" << (int) score << "%, Elo: " << elo << ", LOS: " << (int) los_p << "%) " << std::endl;

        double batch_adj = MIN(0.04, batch / 100000.0);
        bool winner = los > (0.99 - batch_adj);
        bool looser = los < (0.01 + batch_adj);
        if (winner) {
            //we have a winner!
            std::cout << "Engine(" << strongest << ") is significantly stronger than engine(" << opponent << ")" << std::endl;
            if (strongest > opponent) {
                lowerBound = MAX(lowerBound, opponent + 0.1);
            } else {
                upperBound = MIN(upperBound, opponent - 0.1);
            }
        } else if (looser) {
            //we have a winner!
            std::cout << "Engine(" << strongest << ") is significantly weaker than engine(" << opponent << ")" << std::endl;
            bestFactor = opponent;
            if (strongest < opponent) {
                lowerBound = MAX(lowerBound, strongest + 0.1);
            } else {
                upperBound = MIN(upperBound, strongest - 0.1);
            }
            opponent = strongest;
            strongest = bestFactor;
        } else {
            //no significant result.. adjust the bounds with care
            if (los > 0.5 && elo > 0) {
                std::cout << "Engine(" << strongest << ") is a bit stronger than engine(" << opponent << ")" << std::endl;
            } else if (los < 0.5 && elo < 0) {
                std::cout << "Engine(" << strongest << ") is a bit weaker than engine(" << opponent << ")" << std::endl;
                if (opponent < bestFactor) {
                    bestFactor = opponent; //prefer the lowest factor
                } else {
                    bestFactor = opponent;
                }
                opponent = strongest;
                strongest = bestFactor;
            } else {
                std::cout << "Engine(" << strongest << ") is equal in strength to engine(" << opponent << ")" << std::endl;
            }
            if (upperBound == SCORE_INFINITE) { //assume the bound is close
                upperBound = bestFactor + 4 * MAX_WINDOW;
            }
            if (lowerBound == -SCORE_INFINITE) { //assume the bound is close
                lowerBound = bestFactor - 4 * MAX_WINDOW;
            }
            if (strongest < opponent) {
                double adj_step = MAX(STOP_WINDOW / 2, ABS(upperBound - opponent) * ABS(los - 0.5));
                adj_step = ceil(adj_step * 10) / 10.0;
                adj_step = MAX(MIN_WINDOW_ADJ, adj_step);
                upperBound -= adj_step;
            } else if (strongest > opponent) {
                double adj_step = MAX(STOP_WINDOW / 2, ABS(lowerBound - opponent) * ABS(los - 0.5));
                adj_step = ceil(adj_step * 10) / 10.0;
                adj_step = MAX(MIN_WINDOW_ADJ, adj_step);
                lowerBound += adj_step;
            }
            if (step < 2) {
                //cancel altogether if the results are not significant in early, obvious steps (-1 vs 1) and (1 vs 0) or (-1 vs 0)
                std::cout << "Canceling. The evaluation component does not result in a significant improvement." << std::endl;
                break;
            }

        }

        std::cout << "\nBest factor: " << bestFactor << std::endl;
        std::cout << "Lower bound: " << MIN(bestFactor, lowerBound) << std::endl;
        std::cout << "Upper bound: " << MAX(bestFactor, upperBound) << std::endl;


        //determine opponent
        step++;
        if (step == 1) {
            maxgames = MAXGAMESCOUNT / 2;
            opponent = 0;
            continue;
        } else if (step == 2) {
            maxgames = 2 * batch;
            maxgames = maxgames / 1000;
            maxgames = (1 + maxgames) * 1000;
            maxgames = MAX(maxgames, MINGAMESCOUNT);
            std::cout << "Setting batch size to " << maxgames << std::endl;
        }

        double delta_a = ABS(bestFactor - lowerBound);
        double delta_b = ABS(bestFactor - upperBound);
        if (delta_b > delta_a) {
            //find better upperbound
            double window = MIN(MAX_WINDOW, delta_b / 2);
            window = MAX(window, STOP_WINDOW / 2);
            window = MAX(MIN_WINDOW_ADJ, window);
            opponent = MIN(bestFactor + window, upperBound - STOP_WINDOW / 2);
        } else {
            double window = MIN(MAX_WINDOW, delta_a / 2);
            window = MAX(window, STOP_WINDOW / 2);
            window = MAX(MIN_WINDOW_ADJ, window);
            opponent = MAX(bestFactor - window, lowerBound + STOP_WINDOW / 2);

        }
        opponent = floor(opponent * 10.0) / 10.0;
    }
    clock_t end;
    end = clock();

    double elapsed = (1.0 + end - begin) / CLOCKS_PER_SEC;
    std::cout << "\nElapsed: " << elapsed << "s. " << std::endl;
    std::cout << "Games played: " << gamesPlayed << " games. (" << gamesPlayed / elapsed << " games/sec)" << std::endl;


    U64 nodesSum = MAX(1, totalNodes[0] + totalNodes[1]);
    std::cout << "Nodes Total: " << nodesSum << " nodes. (" << int(nodesSum / elapsed) << " nodes/sec) " << std::endl;


    /*
     * Clean up and terminate the learning thread
     */
    delete sd_root;
    delete sd_game;
    delete book;
    delete hash1;
    delete hash2;

    pthread_exit(NULL);

    return NULL;
}

void TEngine::_create_start_positions(TSearch * sd_root, TBook * book, string * poslist, int &x, const int max) {

    TMoveList * bookMoves = &sd_root->stack->moveList;
    TMove actualMove;
    int gen = 0;
    while (x < max) {
        gen++;
        while (sd_root->pos->currentPly < MAX_PLY) {
            actualMove.setMove(0);
            int count = book->findMoves(sd_root->pos, bookMoves);
            if (count > 0) {
                int randomScore = 0;
                int totalBookScore = 1;
                for (int pickMove = 0; pickMove < 2; pickMove++) {
                    int totalScore = 0;
                    for (TMove * bookMove = bookMoves->first; bookMove != bookMoves->last; bookMove++) {
                        totalScore += bookMove->score;
                        if (pickMove && totalScore >= randomScore) {
                            actualMove.setMove(bookMove);
                            break;
                        }
                    }
                    totalBookScore = totalScore;
                    randomScore = (rand() % totalScore) + 1;
                }
            }
            if (actualMove.piece == 0) {
                break;
            }
            sd_root->forward(&actualMove, sd_root->pos->givesCheck(&actualMove));
        };

        //add to search positions
        poslist[x++] = sd_root->pos->asFen();
        poslist[x++] = sd_root->pos->asFen();

        //revert to root
        while (sd_root->pos->currentPly > 0) {
            TMove * move = &(sd_root->stack - 1)->move;
            sd_root->backward(move);
        }
        if (x > 0 && x % 250 == 0) {
            std::cout << "b";
            std::cout.flush();
            break;
        }
    }
}