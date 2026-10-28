/**
 * Maxima, a chess playing program. 
 * Copyright (C) 1996-2026 Erik van het Hof and Hermen Reitsma 
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
#include "w17/w17_eval.h"
#include <stdexcept>

using namespace std;

namespace engine {

    engine_t _engine;

    void stop() {
        _engine.stop();
    }

    void go() {
        _engine.think();
    }

    void analyse() {
        _engine.analyse();
    }

    void book_calc() {
        _engine.book_calc();
    }

    void new_game(std::string fen) {
        _engine.new_game(fen);
    }

    void set_position(std::string fen, const std::vector<U64> & position_history) {
        _engine.set_position(fen, position_history);
    }

    void set_ponder(bool ponder) {
        _engine.set_ponder(ponder);
    }

    bool is_stopped() {
        return _engine.stop_requested();
    }

    bool is_ponder() {
        return _engine.is_pondering();
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
    _game.clear();
}

bool engine_t::start_search(void * (*function)(void *), bool calculate_book) {
    stop();
    _control.stop_requested.store(false);
    _control.ponder.store(_game.ponder);

    // Capture the position, limits, and search options before starting the worker.
    try {
        std::unique_ptr<search_t> next_search;
        if (options::get_value("Wild") == 17) {
            next_search.reset(new w17_search_t(_root_fen.c_str(), &_game, &_control, _position_history));
        } else {
            next_search.reset(new search_t(_root_fen.c_str(), &_game, &_control, _position_history));
            if (_search && _search->wild == 0) {

                // stop() joined the old worker before ownership transfer.
                next_search->take_history(*_search);
            }
        }
        _search = std::move(next_search);
    } catch (const std::runtime_error & error) {
        uci::out(std::string("info string ERROR: ") + error.what());
        return false;
    }
    if (calculate_book) {
        _search->game.max_depth = _search->wild == 17
            ? 6 + 2 * popcnt(_search->brd.all(_search->brd.us())) : 20;
    }
    {
        std::lock_guard<std::mutex> lock(_result_mutex);
        _result = search_result_t();
    }
    if (!create(function, this)) {
        uci::out("info string ERROR: could not create search worker");
        _control.stop_requested.store(true);
        _search.reset();
        return false;
    }
    return true;
}

/**
 * Initialize the engine for starting a new game
 * @param fen position represented by a FEN string
 */
void engine_t::new_game(std::string fen) {
    stop();
    _search.reset();
    {
        std::lock_guard<std::mutex> lock(_result_mutex);
        _result = search_result_t();
    }
    trans_table::clear();
    set_position(fen);
}

/**
 * Copies search results
 * @param s search object
 */
void engine_t::copy_results(search_t * s) {
    search_result_t result;
    result.target_found = s->game.target_move.piece
        && s->stack->best_move.equals(&s->game.target_move);
    result.move = s->stack->best_move;
    result.score = s->result_score;
    result.nodes = s->nodes;
    result.score_flag = s->result_flag;
    result.depth = s->result_depth;
    result.completed_depth = s->completed_depth;
    result.attempted_depth = s->attempted_depth;
    result.interrupted = s->interrupted;
    result.source = s->result_source;
    std::lock_guard<std::mutex> lock(_result_mutex);
    _result = result;
}

/**
 * Thread function for searching and finding a best move in a chess position
 * @param engineObjPtr pointer to the (parent) engine object
 * @return NULL
 */
void * engine_t::_think(void * engine_p) {

    //initialize
    engine_t * engine = (engine_t*) engine_p;
    search_t * s = engine->_search.get();

    //think
    s->go();

    // Publish results; the owner releases search state after joining the worker.
    engine->copy_results(s);
    uci::send_bestmove(s->stack->best_move, s->ponder_move);
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

void engine_t::analyse() {
    stop();
    try {
        if (options::get_value("Wild") == 17) {
            w17_search_t search(_root_fen.c_str());
            uci::out("info string Royal losers evaluation cp " + std::to_string(w17_evaluate(&search)));
            return;
        }
        search_t search(_root_fen.c_str());
        search.stack->in_check = search.brd.in_check();
        const int value = evaluate(&search);
        uci::out(search.stack->in_check ? "info string NNUE static evaluation unavailable in check"
            : "info string NNUE evaluation cp " + std::to_string(value) + " (side to move)");
    } catch (const std::runtime_error & error) {
        uci::out(std::string("info string ERROR: ") + error.what());
    }
}

/**
 * Thread function for searching and finding plausible book moves in a chess position
 * @param engineObjPtr pointer to the (parent) engine object
 * @return NULL
 */
void * engine_t::_book_calc(void * engine_p) {

    //initialize
    engine_t * engine = (engine_t*) engine_p;
    search_t * s = engine->_search.get();

    //get book moves
    s->book_calc();

    // Publish results; the owner releases search state after joining the worker.
    engine->copy_results(s);
    pthread_exit(NULL);
    return NULL;
}
