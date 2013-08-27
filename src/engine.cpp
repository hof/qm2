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
     * internal interative deepening framework with aspiration search.
     */

    if (resultMove.piece == EMPTY && searchData->initRootMoves() > 0) {
        if (searchData->root.MoveCount == 1) {
            tm->setEndTime(2);
            tm->setMaxTime(1);
        }
        searchData->getMaterialInfo();
        searchData->getPawnInfo();
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
                window = diffScore < VPAWN ? window + 2 : window+3;
            } else if (score >= beta) {
                window = diffScore < VPAWN ? window + 1 : window+2;
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
    searchData->getMaterialInfo();
    searchData->getPawnInfo();
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