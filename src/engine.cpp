#include <cstdlib>
#include <iostream>
#include <unistd.h>
#include "engine.h"
#include "board.h"
#include "movegen.h"
#include "book.h"
#include "evaluate.h"
#include "search.h"
#include "search.h"
#include <time.h>
#include "bbmoves.h"
#include "timemanager.h"
#include "inputhandler.h"
#include "outputhandler.h"
#include <math.h> 

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

    TSearch * searchData = new TSearch(engine->_rootFen.c_str(),
            engine->_pct,
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
        searchData->stack->evaluationScore = evaluate(searchData, 0, 0);
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
            searchData->poll();
            if (searchData->stopSearch || (maxNodes > 0 && searchData->nodes > maxNodes) 
                    || (MATE_IN_PLY(resultScore) && depth/ONE_PLY > MATE_IN_PLY(resultScore))
                    || (MATED_IN_PLY(resultScore)) && depth/ONE_PLY > MATED_IN_PLY(resultScore)) {
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
void TEngine::analyse() {

    TSearch * searchData = new TSearch(_rootFen.c_str(), _pct, _hashTable, _outputHandler);
    std::cout << searchData->pos->asFen().c_str() << std::endl;
    searchData->stack->evaluationScore = evaluate(searchData, 0, 0);
    int phase = searchData->stack->gamePhase;
    std::cout << "\n1) Piece Square tables: ";
    searchData->pos->boardFlags->pct.print(phase);
    std::cout << "\n2) Material balance: ";
    searchData->stack->scores[SCORE_MATERIAL].print(phase);
    std::cout << "\n3) Game phase: " << phase;
    std::cout << "\n4) Pawn score: ";
    searchData->stack->scores[SCORE_PAWNS].print(phase);
    std::cout << "\n5) Rook score: ";
    searchData->stack->scores[SCORE_ROOKS].print(phase);
    std::cout << "\n6) Shelter score for white: ";
    searchData->stack->scores[SCORE_SHELTER_W].print(phase);
    std::cout << "\n7) Shelter score for black: ";
    searchData->stack->scores[SCORE_SHELTER_B].print(phase);
    std::cout << "\n8) Evaluation:" << searchData->stack->evaluationScore;
    std::cout << "\n9) Quiescence:" << searchData->qsearch(-SCORE_MATE, SCORE_MATE, 0) << std::endl;
    std::cout << "\n10) Best move:" << std::endl;
    

    //loop through piece square tables
    int pct_score = 0;
    const std::string PIECE_NAME[13] = {"X", "WP", "WN",
        "WB", "WR", "WQ", "WK", "BP",
        "NB", "BB", "BR", "BQ", "BK"};
    for (int sq = a1; sq <= h8; sq++) {
        int pc = searchData->pos->Matrix[sq];
        if (pc) {
            TScore * score = &_pct[pc][sq];
            std::cout << PIECE_NAME[pc] << "@" << sq << ": ";
            score->print();
            pct_score += score->get(phase);
            std::cout << score->get(phase) << " tot: " << pct_score << std::endl;
        }

    }

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
        std::cout << move->asString().c_str() << ": " << -searchData->pvs(-beta, -alpha, 0) << std::endl;
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
 * by playing GAMECOUNT shallow fixed depth games, one side of the board, 
 * engine(learning), has the experimental evaluation enabled, the other side,
 * engine(base), not. 
 * 
 * Typically, depth is set to <= 3 and the amount of games >= 1000 to obtain
 * significant results. 
 * 
 * A book.bin file is required to pre-calculate a set of random start positions
 * 
 * 1. Checks if the evaluation seems plausible by giving the exp. evaluation the 
 *    exact opposite score (multiplied with factor -1). 
 *    Example: for testing king safety evaluation, the learning side will expose 
 *    the king in stead of protecting it. This should result in many lost games 
 *    and is much easier to measure than a positive multiplication factor. 
 * 
 *    If there is no significant difference in wins/losses, it's assumed the 
 *    evaluation does not have any practical difference and the self-play is
 *    canceled.
 * 
 * 2. Self-playing games of GAMECOUNT matches: engine(factor) vs engine(0) 
 *    for the following multiplication factors: 0.25, 0.5, 0.75, 1.0, 1.25, 1.5 
 *    and 1.75.
 * 
 * The end-result is the best multiplication factor for the exp. evaluation, and
 * the median factor. The median factor indicates if the exp. evaluation score
 * is correct or should be adjusted to a higher/lower value.
 * 
 * This learning method gives a (rough) indication if the exp. evaluation is 
 * plausible. It does not replace engine testing under tournament conditions.   
 * 
 * @param engineObjPtr pointer to engine object
 * @return void
 */
void * TEngine::_learn(void * engineObjPtr) {

    /*
     * Constants
     */

    const int MAXDEPTH = 2; //default: depth 2
    const int MAXGAMECOUNT = 20000; //Sample size, 920+ recommended. Use even numbers. 
    double START_GRAIN = 2;
    double STOP_GRAIN = 0.125;
    const int STOPSCORE = 300; //if the score is higher than this for both sides, the game is consider a win
    const int MAXPLIES = 200; //maximum game length in plies
    const int HASH_SIZE_IN_MB = 1;



    /*
     * Initialize, normally it's not needed to change anything from here
     */

    THashTable * hash1 = new THashTable(HASH_SIZE_IN_MB); //each side uses it's own hash table
    THashTable * hash2 = new THashTable(HASH_SIZE_IN_MB);
    TEngine * engine = (TEngine*) engineObjPtr;
    TSearch * sd_root = new TSearch(engine->_rootFen.c_str(),
            engine->_pct, hash1, NULL);
    TSearch * sd_game = new TSearch(engine->_rootFen.c_str(),
            engine->_pct, hash1, NULL);

    engine->gameSettings.maxDepth = MAXDEPTH;
    sd_root->timeManager->setEndTime(INFINITE_TIME);
    sd_game->timeManager->setEndTime(INFINITE_TIME);
    sd_game->learnParam = 1;

    double grain = START_GRAIN;
    int passtype = 1;
    int x = 0;
    double bestFactor = 1.0;
    double prevFactor = 0.0; //to start, it needs to be different than bestFactor
    int prevtype = 0;
    int scores[1024]; //1024 should be sufficient

    /*
     * Prepare GAMECOUNT/2 starting positions, using the opening book
     * Each starting position is played twice, with the engine colors reversed
     */

    std::cout << "\nLEARNING MODE" << std::endl;
    std::cout << "Depth: " << MAXDEPTH << std::endl;
    std::cout << "Games: " << MAXGAMECOUNT << std::endl;

    TBook * book = new TBook();
    book->open("book.bin");
    string start_positions[MAXGAMECOUNT + 1];
    for (int p = 0; p < MAXGAMECOUNT + 1; p++) {
        start_positions[p] = "";
    }
    start_positions[x++] = sd_root->pos->asFen();
    start_positions[x++] = sd_root->pos->asFen();

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

    int step = 0;
    while (grain >= STOP_GRAIN) {
        int stats[3] = {0, 0, 0}; //draws, wins for learning side, losses for learning side
        U64 nodes[2] = {0, 0}; //total node counts for both sides
        scores[step] = 0;
        double strongest = bestFactor;
        double opponent = passtype == 1 ? strongest - grain : strongest + grain;
        std::cout << "\nEngine(" << strongest << ") vs Engine(" << opponent << ")" << std::endl;
        hash1->clear(); //clear hash: the evaluation scores changed.
        hash2->clear();
        batch = 0;
        for (int game = 0; game < MAXGAMECOUNT; game++) {
            fen = start_positions[game];
            if (fen == "") {
                engine->_create_start_positions(sd_root, book, start_positions, x, MAXGAMECOUNT);
                fen = start_positions[game];
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
                    sd_game->stack->evaluationScore = evaluate(sd_game, 0, 0);
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
            batch++;
            if (gamesPlayed % 50 == 0 && gamesPlayed > 50) {
                std::cout << ".";
                std::cout.flush();
                //check if the result if significant enough before finishing the full batch
                double tp = stats[1] + 0.5 * stats[0];
                double tmax = batch;
                double tscore = tp / tmax;
                double max_sd = 1.1 / pow(batch, 0.48); //safe(?) standard deviation
                if ((tscore + max_sd) < 0.5) {
                    std::cout << "<";
                    break;
                } else if ((tscore - max_sd > 0.5)) {
                    std::cout << ">";
                    break;
                }

            }
        }
        double points = stats[1] + 0.5 * stats[0];
        int maxPoints = batch;
        int wins = stats[1] - stats[2];
        totalNodes[0] += nodes[0];
        totalNodes[1] += nodes[1];
        double score = (100.0 * points) / maxPoints;
        scores[step] = score;
        int elo = round(-400.0 * log(1 / (points / maxPoints) - 1) / log(10));
        std::cout << "\nGames: " << batch << " Win: " << stats[1] << " Loss: " << stats[2] << " Draw: " << stats[0] << " Score: " << points << "/" << maxPoints << " (" << score << "%, " << wins << ", Elo: " << elo << ") " << std::endl;

        double sdev = 1.1 / pow(batch, 0.48); //safe(?) standard deviation
        double fscore = score / 100.0;

        bool grain_reduced = false;
        if ((fscore - sdev) > 0.5) {
            //we have a winner!
            std::cout << "Engine(" << strongest << ") is significantly stronger than engine(" << opponent << ")" << std::endl;
            prevtype = 1;
            bestFactor = strongest;
        } else if ((fscore + sdev) < 0.5) {
            //we have a winner!
            std::cout << "Engine(" << strongest << ") is significantly weaker than engine(" << opponent << ")" << std::endl;
            prevtype = -1;
            bestFactor = opponent;
        } else {
            std::cout << "Engine(" << strongest << ") is equal in strength to engine(" << opponent << ")" << std::endl;
            if (bestFactor == prevFactor && prevtype == 0) {
                break;
            }
            if (passtype == 2) {
                grain = grain / 2;
                grain_reduced = true;
            }
            prevtype = 0;
        }
        if (passtype == 1) {
            passtype++; //always run the high pass
        } else {
            passtype = 1;
            if (bestFactor == prevFactor && grain_reduced == false) {
                grain = grain / 2; //reduce the grain size if the best factor 
            }
        }
        prevFactor = bestFactor;

    }


    if (bestFactor > 0) {
        std::cout << "\nBest factor: " << bestFactor << std::endl;
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