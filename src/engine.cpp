#include <cstdlib>
#include <iostream>
#include <unistd.h>
#include "engine.h"
#include "board.h"
#include "movegen.h"
#include "book.h"
#include "evaluate.h"
#include "search.h"
#include "searchdata.h"
#include <time.h>
#include "bbmoves.h"
#include "timemanager.h"
#include "inputhandler.h"
#include "outputhandler.h"

using namespace std;

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

    TSearchData * searchData = new TSearchData(engine->_rootFen.c_str(),
            PIECE_SQUARE_TABLE,
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
    }

    searchData->maxNodes = maxNodes;
    searchData->learnParam = learnParam;
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
                    resultMove.setMove(bookMove);
                    engine->setMove(bookMove);
                    engine->setScore(0);
                    if (searchData->outputHandler) {
                        searchData->outputHandler->sendPV((bookMove->score) / totalBookScore, 1, 1, count, searchData->timeManager->elapsed(), resultMove.asString().c_str(), (int) EXACT);
                    }
                    break;
                }
            }
            totalBookScore = totalScore;
            randomScore = (rand() % totalScore) + 1;
        }
    }


    /*
     * Find a move by Principle Variation Search (fail-soft) in an 
     * internal iterative deepening framework with aspiration search.
     */

    if (resultMove.piece == EMPTY && searchData->initRootMoves() > 0) {
        if (searchData->root.MoveCount == 1) {
            tm->setEndTime(2);
            tm->setMaxTime(1);
        }
        searchData->getMaterialScore();
        searchData->getPawnScore();
        searchData->getKingScore();
        int alpha = -SCORE_INFINITE;
        int beta = SCORE_INFINITE;
        int prevScore = -SCORE_INFINITE;
        int aspirationWindows[] = {
            ((VPAWN / 4) & GRAIN) + 1,
            ((VROOK / 1) & GRAIN) + 1,
            ((VQUEEN / 1) & GRAIN) + 1,
            SCORE_INFINITE,
            SCORE_INFINITE,
            SCORE_INFINITE
        };
        int window = 0;
        int depth = ONE_PLY;
        int resultScore = 0;
        while (depth <= maxDepth * ONE_PLY && !searchData->stopSearch) {
            int score = pvs_root(searchData, alpha, beta, depth);
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
                    resultMove.setMove(&firstMove);
                    ponderMove.setMove(0);
                    if (searchData->stack->pvCount > 1) {
                        ponderMove.setMove(&searchData->stack->pvMoves[1]);
                    }
                    engine->setMove(&firstMove);
                    engine->setScore(resultScore);
                }
                if (searchData->outputHandler) {
                    searchData->outputHandler->sendPV(resultScore, depth / ONE_PLY, searchData->selDepth, searchData->nodes, tm->elapsed(), searchData->getPVString().c_str(), type);
                }
            }
            /* 
             * Stop conditions
             */
            if (searchData->stopSearch || (maxNodes > 0 && searchData->nodes > maxNodes)) {
                break;
            } else if (targetScore && targetMove.piece && targetMove.equals(&resultMove) && score >= targetScore) {
                engine->setTestResult(true);
                break;
            }
            /*
             * Prepare next search
             * - Increase depth if the score is between the bounds
             * - Otherwise open the aspiration window further and research 
             *   on the same depth. Open it fully in case of mate-scores.
             */
            int diffScore = ABS(score - prevScore);
            if (score <= alpha) {
                window = diffScore < VPAWN ? window + 2 : window + 3;
            } else if (score >= beta) {
                window = diffScore < VPAWN ? window + 1 : window + 2;
            } else {
                window = 0;
                if (score > VKNIGHT) {
                    window = 1 + (score > VROOK);
                }
                depth += ONE_PLY;
            }

            if (score < SCORE_MATE - MAX_PLY) {
                alpha = MAX(-SCORE_INFINITE, score - aspirationWindows[window]);
                beta = MIN(SCORE_INFINITE, score + aspirationWindows[window]);
            } else {
                alpha = -SCORE_MATE;
                beta = SCORE_MATE;
            }

            /*
             * Increase time for time based search when 
             * - We opened the aspiration window on high depths
             * - Evaluation shows large positional values
             * - PV or pondermove is not set
             */
            if ((tm->elapsed() > 1500)
                    && ((type != EXACT && diffScore > VPAWN / 4)
                    || ponderMove.piece == EMPTY)) {
                tm->requestMoreTime();
            }
            prevScore = score;

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

            searchData->outputHandler->sendEvalStats(
                    searchData->evaluations,
                    searchData->pawnEvals,
                    searchData->fullEvaluations);
            searchData->outputHandler->sendHashTableStats(hashHits, ptHits, mtHits);
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
 * - shallow depth 3 search for the best move
 * 
 * This function is mainly useful for testing the above components and the 
 * move ordering by visual inspection of the output.
 */
void TEngine::analyse() {

    TSearchData * searchData = new TSearchData(_rootFen.c_str(), PIECE_SQUARE_TABLE, _hashTable, _outputHandler);
    std::cout << searchData->pos->asFen().c_str() << std::endl;
    searchData->getMaterialScore();
    searchData->getPawnScore();
    std::cout << "1) Material score: " << searchData->stack->materialScore << std::endl;
    std::cout << "2) Game phase: " << searchData->stack->gamePhase << std::endl;
    std::cout << "3) Piece Square tables: " << phasedScore(searchData->stack->gamePhase,
            searchData->pos->boardFlags->pctMG,
            searchData->pos->boardFlags->pctEG) << std::endl;
    std::cout << "4) Shelter score for white: " << searchData->stack->shelterScoreW << std::endl;
    std::cout << "5) Shelter score for black: " << searchData->stack->shelterScoreB << std::endl;

    std::cout << "6) Evaluation:" << evaluate(searchData, -SCORE_MATE, SCORE_MATE) << std::endl;

    std::cout << "7) Quiescence:" << qsearch(searchData, -SCORE_MATE, SCORE_MATE, 0) << std::endl;
    std::cout << "8) Best move:" << std::endl;
    //TBook * book = new TBook();
    /*
     * Analyse best move doing a shallow search
     */
    TMoveList * rootMoves = &searchData->stack->moveList;
    TBoard * root = searchData->pos;
    TMovePicker * mp = searchData->movePicker;
    int alpha = -SCORE_INFINITE;
    int beta = SCORE_INFINITE;
    TMove * move = mp->pickFirstMove(searchData, 0, alpha, beta, 0);
    bool inCheck = root->inCheck();
    if (!move) {
        bool score = inCheck ? -SCORE_MATE + root->currentPly : SCORE_DRAW;
        std::cout << "No legal moves: " << score << std::endl;
    }
    while (move) {
        searchData->forward(move, root->givesCheck(move));
        std::cout << move->asString().c_str() << ": " << -pvs(searchData, -beta, -alpha, 0) << std::endl;
        searchData->backward(move);
        move = mp->pickNextMove(searchData, 0, alpha, beta, 0);
    }
    delete searchData;
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
 * Learning by self-play. A factor for experimental evaluation is determined
 * by playing GAMECOUNT shallow fixed depth games, one side of the board has 
 * the experimental evaluation enabled, the other side not. 
 * 
 * This gives a (rough) indication if the exp. evaluation is plausible. It does
 * not replace engine testing under tournament conditions. 
 * 
 * Typically, depth is set to <= 3 and the amount of games >= 1000 to obtain
 * significant results. 
 * 
 * A book.bin file is required to pre-calculate a set of random start positions
 * 
 * 1. Checks if the evaluation makes sense by giving the exp. evaluation the 
 *    exact opposite score (multiplied with factor -1). If there is no significant
 *    difference, the evaluation seems to not make any practical difference.
 * 
 * 2. Continues to find the most optimal factor, first in big steps (0.5), followed
 *    by a refined factor (0.25). 
 * 
 * The end-result is the best multiplication factor for the exp. evaluation. 
 *    
 * 
 * @param engineObjPtr pointer to engine object
 * @return void
 */
void * TEngine::_learn(void * engineObjPtr) {

    /*
     * Constants
     */

    const int MAXDEPTH = 2;
    const int GAMECOUNT = 10000; //Sample size, 920+ recommended. Use even numbers. 

    const int STOPSCORE = 300; //if the score is higher than this for both sides, the game is consider a win
    const int MAXPLIES = 200; //maximum game length in plies
    const int MAJOR_LEARN_STEPS = 4; 
    const int REFINED_LEARN_STEPS = 2;
    const int HASH_SIZE_IN_MB = 0;
    
    double learnSteps[MAJOR_LEARN_STEPS] = {-1.0, 0.5, 1.0, 1.5};
    double refinedSteps[MAJOR_LEARN_STEPS][REFINED_LEARN_STEPS] = {
        {  -1.25, -0.75 },
        {  0.25, 0.76 },
        {  0.75, 1.25 },
        { 1.25, 1.75 }
    };

    /*
     * Initialize, normally it's not needed to change anything from here
     */
    
    THashTable * hash1 = new THashTable(HASH_SIZE_IN_MB);
    THashTable * hash2 = new THashTable(HASH_SIZE_IN_MB);

    TEngine * engine = (TEngine*) engineObjPtr;
    TGameSettings game = engine->gameSettings;

    TSearchData * sd_root = new TSearchData(engine->_rootFen.c_str(),
            PIECE_SQUARE_TABLE, hash1, NULL);

    engine->gameSettings.maxDepth = MAXDEPTH;
    sd_root->timeManager->setEndTime(INFINITE_TIME);
    
    const int LEARNSTEPS[2] = {MAJOR_LEARN_STEPS, REFINED_LEARN_STEPS};
    int bestScore = 0;
    int bestStep = 0;
    int bestWins = 0;
    double bestFactor = 0.0;
    int scores[MAX(MAJOR_LEARN_STEPS, REFINED_LEARN_STEPS)];
    double* stepsTable[2] = {learnSteps, refinedSteps[1]};

    /*
     * Prepare GAMECOUNT/2 starting positions, using the opening book
     * Each starting position is played by both sides, with and without learning.
     */

    std::cout << "\nLEARNING MODE" << std::endl;
    std::cout << "Depth: " << MAXDEPTH << std::endl;
    std::cout << "Games: " << GAMECOUNT << std::endl;
    std::cout << "\nGenerating game start positions" << std::endl;

    TBook * book = new TBook();
    book->open("book.bin");

    TMoveList * bookMoves = &sd_root->stack->moveList;
    TMove actualMove;

    string start_positions[GAMECOUNT];

    int x = 0;

    //use the actual start position too
    start_positions[x++] = sd_root->pos->asFen();
    start_positions[x++] = sd_root->pos->asFen();

    srand(time(NULL));
    while (x < GAMECOUNT) {
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
        start_positions[x++] = sd_root->pos->asFen();
        if (x < GAMECOUNT) {
            start_positions[x++] = sd_root->pos->asFen();
        }
        //std::cout << sd_root->pos->asFen() << std::endl; //for debugging

        //revert to root
        while (sd_root->pos->currentPly > 0) {
            TMove * move = &(sd_root->stack - 1)->move;
            sd_root->backward(move);
        }
    }

    std::cout << "Created " << x / 2 << " random game start positions from book" << std::endl;

    THashTable * hash_list[2] = {hash1, hash2};
    TSearchData * sd_game = sd_root;
    U64 gamesPlayed = 0;
    U64 nodesPlayed = 0;
    clock_t begin;
    begin = clock();

    for (int phase = 0; phase < 2; phase++) {
        double* steps = stepsTable[phase];
        for (int step = 0; step < LEARNSTEPS[phase]; step++) {
            int stats[3] = {0, 0, 0}; //draws, wins for learning side, losses for learning side
            U64 nodes[2] = {0, 0}; //total node counts for both sides
            scores[step] = 0;
            sd_game->learnFactor = steps[step];
            std::cout << "\nFactor: " << sd_game->learnFactor << " " << std::endl;
            for (int game = 0; game < GAMECOUNT; game++) {
                sd_game->pos->fromFen(start_positions[game].c_str());
                hash1->clear();
                hash2->clear();
                int plyCount = 0;
                int prevScore = 0;
                bool gameover = false;
                do {
                    for (int side_to_move = 0; side_to_move < 2; side_to_move++) {
                        /*
                         * Toggle learnParam to 1 or 0 to enable/disable the learn subject. 
                         * Play each position twice: 
                         * 1) learning enabled for white only 
                         * 2) learning enabled for black only
                         */
                        if (game % 2 == 0) {
                            sd_game->learnParam = !side_to_move;
                        } else {
                            sd_game->learnParam = side_to_move;
                        }
                        bool learning_side = sd_game->learnParam == 1;

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
                        sd_game->getMaterialScore();
                        sd_game->getPawnScore();
                        sd_game->getKingScore();
                        sd_game->hashTable = hash_list[learning_side]; //each side has it's own hash
                        int depth = ONE_PLY;
                        int resultScore = 0;

                        /*
                         * Normal PVS search without aspiration windows
                         */
                        sd_game->stopSearch = false;
                        while (depth <= (MAXDEPTH) * ONE_PLY && !sd_game->stopSearch) {
                            int score = pvs_root(sd_game, -SCORE_INFINITE, SCORE_INFINITE, depth);
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

                        nodes[side_to_move] += sd_game->nodes;

                        //stop conditions
                        if (sd_game->pos->boardFlags->fiftyCount >= 100 || sd_game->pos->isDraw()) {
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
                        //std::cout << plyCount << ": " << actualMove.asString() << " " << sd_game->pos->asFen() << std::endl; //for debugging
                        sd_game->forward(&actualMove, sd_game->pos->givesCheck(&actualMove));
                    }

                } while (gameover == false);
                gamesPlayed++;
            }
            int points = stats[1]*2 + stats[0];
            int maxPoints = GAMECOUNT * 2;
            int wins = stats[1] - stats[2];
            U64 totalNodes = MAX(1, nodes[0] + nodes[1]);
            nodesPlayed += totalNodes;
            int score = (100 * points) / maxPoints;
            scores[step] = score;

            int nodePct = (100 * nodes[0]) / totalNodes;
            if (score >= bestScore && wins > bestWins) {
                bestScore = score;
                bestStep = step;
                bestWins = wins;
                bestFactor = steps[step];
            }
            std::cout << "Games:" << GAMECOUNT << " Win: " << stats[1] << " Loss: " << stats[2] << " Draw: " << stats[0] << " (" << score << "%, " << wins << ")" << " nodes balance: " << nodePct << "%" << std::endl;
        }
        if (phase == 0) {
            stepsTable[1] = refinedSteps[bestStep];
        }
    }
    std::cout << "\nBest factor: " << bestFactor << " (" << bestScore << "%)" << std::endl;

    clock_t end;
    end = clock();
    
    double elapsed = (1.0 + end - begin) / CLOCKS_PER_SEC;
    std::cout << "Elapsed: " << elapsed << "s. " << std::endl;
    std::cout << "Games played: " << gamesPlayed << " games. (" << gamesPlayed/elapsed << " games/sec)" << std::endl;
    std::cout << "Nodes: " << nodesPlayed << " nodes. (" << nodesPlayed/elapsed << " nodes/sec)" << std::endl;
    


    /*
     * Clean up and terminate the learning thread
     */
    delete sd_root;
    delete book;
    delete hash1;
    delete hash2;

    pthread_exit(NULL);
    return NULL;
}