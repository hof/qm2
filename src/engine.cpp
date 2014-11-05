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
 * File: engine.cpp
 * Implements the chess engine
 */

#include "engine.h"
#include "w17/w17_search.h"

using namespace std;

namespace engine {

    engine_t _engine;
    bool _stopped = false;
    bool _ponder = false;

    void stop() {
        _stopped = true;
        _engine.stop();
    }

    void go() {
        _stopped = false;
        _engine.think();
    }

    void analyse() {
        _stopped = false;
        _engine.analyse();
    }

    void learn() {
        _stopped = false;
        _engine.learn();
    }

    void new_game(std::string fen) {
        _engine.new_game(fen);
    }

    void set_position(std::string fen) {
        _engine.set_position(fen);
    }

    void set_ponder(bool ponder) {
        _ponder = ponder;
        _engine.set_ponder(ponder);
    }
    
    void set_wild(int wild) {
        _engine.set_wild(wild);
    }

    bool is_stopped() {
        return _stopped;
    }

    bool is_ponder() {
        return _ponder;
    }

    engine_t * instance() {
        return &_engine;
    }

    game_t * settings() {
        return _engine.settings();
    }
}

/**
 * Construct engine thread
 */
engine_t::engine_t() : threads_t() {
    _total_nodes = 0;
    _target_found = false;
    _stop_all = false;
    _ponder = false;
    _root_fen = "";
    _result_move.clear();
    _result_score = 0;
    _game.clear();
    _wild = 0;
}

/**
 * Initialize the engine for starting a new game
 * @param fen position represented by a FEN string
 */
void engine_t::new_game(std::string fen) {
    _result_move.clear();
    _result_score = 0;
    trans_table::clear();
    set_position(fen);
}

/**
 * Copies search results
 * @param s search object
 */
void engine_t::copy_results(search_t * s) {
    set_target_found(s->stack->best_move.equals(&_game.target_move));
    set_move(&s->stack->best_move);
    set_score(s->result_score);
    set_total_nodes(s->nodes);
}

/**
 * Thread function for searching and finding a best move in a chess position
 * @param engineObjPtr pointer to the (parent) engine object
 * @return NULL
 */
void * engine_t::_think(void * engine_p) { 
    
    //initialize
    engine_t * engine = (engine_t*) engine_p;
    search_t * s;
    if (engine->_wild == 17) {
        s = new w17_search_t(engine->_root_fen.c_str(), engine->settings());
    } else {
        s = new search_t(engine->_root_fen.c_str(), engine->settings());
    }
    
    //think
    s->go();
    
    //copy search results and clean up
    engine->copy_results(s);
    delete s;
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

void print_row(std::string cap, score_t & total, int phase) {
    std::cout << std::setw(14);
    std::cout << cap;
    std::cout << " | ";
    std::cout << std::setw(32);
    std::cout << " | ";
    std::cout << std::setw(9) << total.mg;
    std::cout << " ";
    std::cout << std::setw(4) << total.eg;
    std::cout << " ";
    std::cout << std::setw(5) << total.get(phase);
    std::cout << std::endl;
}

void print_row(std::string cap, score_t & w, score_t & b, int phase) {
    score_t total;
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

void engine_t::analyse() {

    search_t * s = new search_t(_root_fen.c_str());
    int phase = s->stack->phase;
    bool wtm = s->brd.stack->wtm;
    score_t tempo_score;
    tempo_score.set(TEMPO[wtm]);
    score_t w, b;
    w.clear();
    b.clear();
    std::cout << s->brd.to_string().c_str() << std::endl;
    std::cout << "Game Phase: " << phase << std::endl;
    std::cout << "Eval Component | White MG   EG | Black MG   EG |  Total MG   EG   Tot  \n";
    std::cout << "---------------+---------------+---------------+---------------------\n";
    print_row("Tempo", tempo_score.get(phase));
    print_row("Material", s->stack->material_score);
    print_row("Pawns & Kings", s->stack->pawn_score, phase);
    print_row("Knights", s->stack->knight_score[WHITE], s->stack->knight_score[BLACK], phase);
    print_row("Bishops", s->stack->bishop_score[WHITE], s->stack->bishop_score[BLACK], phase);
    print_row("Rooks", s->stack->rook_score[WHITE], s->stack->rook_score[BLACK], phase);
    print_row("Queens", s->stack->queen_score[WHITE], s->stack->queen_score[BLACK], phase);
    print_row("Passers", s->stack->passer_score[WHITE], s->stack->passer_score[BLACK], phase);
    print_row("King Attack", s->stack->king_score[WHITE], s->stack->king_score[BLACK], phase);
    if (s->brd.stack->wtm == false) {
        std::cout << "---------------+---------------+---------------+---------------------\n";
        print_row("Black to move", s->stack->eg_score);
    }
    if ((s->stack->material_flags & 128) != 0) {
        print_row("EG Adjustment", s->stack->eval_result - s->stack->eg_score);
    }

    std::cout << "---------------+---------------+---------------+---------------------\n";
    print_row("Total", s->stack->eval_result);

    delete s;
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
void * engine_t::_learn(void * engineObjPtr) {

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

    /*
     * Initialize, normally it's not needed to change anything from here
     */
    engine_t * engine = (engine_t*) engineObjPtr;
    trans_table::disable();
    search_t * sd_root = new search_t(engine->_root_fen.c_str());
    search_t * sd_game = new search_t(engine->_root_fen.c_str());

    engine->settings()->max_depth = MAXDEPTH;
    engine->settings()->init_tm(true);

    int x = 0;
    double bestFactor = 1.0;
    double upperBound = score::INF;
    double lowerBound = -score::INF;

    uci::silent(true);
    trans_table::disable();


    /*
     * Prepare GAMECOUNT/2 starting positions, using the opening book
     * Each starting position is played twice, with the engine colors reversed
     */

    std::cout << "\nLEARNING MODE" << std::endl;
    std::cout << "Depth: " << MAXDEPTH << std::endl;

    book_t * book = new book_t();
    book->open("book.bin");
    string start_positions[MAXGAMESCOUNT + 1];
    for (int p = 0; p < MAXGAMESCOUNT + 1; p++) {
        start_positions[p] = "";
    }

    /*
     * Self-play, using the generated start positions once for each side
     */
    move_t actualmove;
    U64 gamesPlayed = 0;
    U64 batch = 0;
    U64 totalNodes[2] = {0, 0};
    clock_t begin;
    begin = clock();
    std::string fen = "";
    
    double strongest = +bestFactor;
    double opponent = -bestFactor;

    U64 step = 0;
    int maxgames = MAXGAMESCOUNT / 10;
    while ((upperBound - strongest) > STOP_WINDOW || (strongest - lowerBound) > STOP_WINDOW) {
        int stats[3] = {0, 0, 0}; //draws, wins for learning side, losses for learning side
        U64 nodes[2] = {0, 0}; //total node counts for both sides
        std::cout << "\nEngine(" << strongest << ") vs Engine(" << opponent << ")" << std::endl;
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
            sd_game->brd.init(fen.c_str());
            int ply_count = 0;
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
                        sd_game->game->learn_factor = side_to_move ? strongest : opponent;
                    } else {
                        sd_game->game->learn_factor = side_to_move ? opponent : strongest;
                    }
                    bool learning_side = sd_game->game->learn_factor == strongest;

                    /*
                     * Prepare search: cleanup and reset search stack. 
                     */
                    actualmove.clear();
                    sd_game->reset_stack();
                    int move_count = sd_game->init_root_moves();
                    if (move_count == 0) {
                        if (sd_game->brd.in_check()) { //current engine lost
                            stats[1 + learning_side]++;
                        } else {
                            stats[0]++; //draw by stalemate. 
                        }
                        gameover = true;
                        break;

                    }
                    sd_game->stack->eval_result = evaluate(sd_game);
                    int depth = 1;
                    int resultScore = 0;

                    /*
                     * Normal PVS search without aspiration windows
                     */
                    sd_game->stop_all = false;
                    while (depth <= MAXDEPTH && !sd_game->stop_all) {
                        int score = sd_game->pvs_root(-score::INF, score::INF, depth);
                        if (!sd_game->stop_all) {
                            resultScore = score;
                        }
                        if (sd_game->stack->pv_count > 0) {
                            move_t firstmove = sd_game->stack->pv_moves[0];
                            if (firstmove.piece) {
                                actualmove.set(&firstmove);
                            }
                        }
                        depth++;
                        sd_game->root.sort_moves(&actualmove);
                    }

                    nodes[1 - learning_side] += sd_game->nodes;

                    //stop conditions
                    if (sd_game->brd.stack->fifty_count >= 20 || sd_game->brd.is_draw()) {
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

                    if (ply_count >= MAXPLIES) {
                        //too long game, abort as draw
                        stats[0]++; //draw
                        gameover = true;
                        break;
                    }

                    prevScore = resultScore;
                    ply_count++;
                    //std::cout << ply_count << ": " << resultScore << " " << actualmove.asString() << " " << sd_game->brd.asFen() << std::endl; //for debugging
                    sd_game->forward(&actualmove, sd_game->brd.gives_check(&actualmove));
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
            if (upperBound == score::INF) { //assume the bound is close
                upperBound = bestFactor + 4 * MAX_WINDOW;
            }
            if (lowerBound == -score::INF) { //assume the bound is close
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

    uci::silent(false);
    trans_table::enable();

    delete sd_root;
    delete sd_game;
    delete book;

    pthread_exit(NULL);

    return NULL;
}

void engine_t::_create_start_positions(search_t * sd_root, book_t * book, string * poslist, int &x, const int max) {

    move::list_t * bookmoves = &sd_root->stack->move_list;
    move_t actualmove;
    int gen = 0;
    while (x < max) {
        gen++;
        while (sd_root->brd.ply < MAX_PLY) {
            actualmove.clear();
            int count = book->find(&sd_root->brd, bookmoves);
            if (count > 0) {
                int randomScore = 0;
                for (int pickmove = 0; pickmove < 2; pickmove++) {
                    int totalScore = 0;
                    for (move_t * bookmove = bookmoves->first; bookmove != bookmoves->last; bookmove++) {
                        totalScore += bookmove->score;
                        if (pickmove && totalScore >= randomScore) {
                            actualmove.set(bookmove);
                            break;
                        }
                    }
                    randomScore = (rand() % totalScore) + 1;
                }
            }
            if (actualmove.piece == 0) {
                break;
            }
            sd_root->forward(&actualmove, sd_root->brd.gives_check(&actualmove));
        };

        //add to search positions
        poslist[x++] = sd_root->brd.to_string();
        poslist[x++] = sd_root->brd.to_string();

        //revert to root
        while (sd_root->brd.ply > 0) {
            move_t * move = &(sd_root->stack - 1)->current_move;
            sd_root->backward(move);
        }
        if (x > 0 && x % 250 == 0) {
            std::cout << "b";
            std::cout.flush();
            break;
        }
    }
}