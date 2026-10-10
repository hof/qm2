/**
 * Maxima, a chess playing program. 
 * Copyright (C) 1996-2015 Erik van het Hof and Hermen Reitsma 
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
 * File: engine.h
 * Implements the chess engine
 *
 * Created on 26 april 2011, 17:56
 */

#ifndef ENGINE_H
#define	ENGINE_H

#include <memory>
#include <mutex>

#include "eval.h"
#include "search.h"
#include "movepicker.h"
#include "book.h"
#include "threadman.h"
#include "uci_console.h"
#include "timeman.h"
#include "maxima/version.h"

struct search_result_t {
    move_t move{};
    int score = 0;
    U64 nodes = 0;
    U64 pruned_moves = 0;
    int score_flag = 0;
    int depth = 0;
    int completed_depth = 0;
    int attempted_depth = 0;
    bool interrupted = false;
    result_source_t source = result_source_t::none;
    bool target_found = false;
};

class engine_t : public threads_t {
private:
    game_t _game;
    std::string _root_fen;
    std::vector<U64> _position_history;
    search_control_t _control;
    std::unique_ptr<search_t> _search;
    mutable std::mutex _result_mutex;
    search_result_t _result;

    bool start_search(void * (*function)(void *), bool calculate_book);

    static void * _think(void * engineObjPtr);
    static void * _learn(void * engineObjPtr);
    static void * _book_calc(void * engineObjPrt);
    
    void _create_start_positions(search_t * root, book_t * book, std::string * pos, int &x, const int max);

public:
    
    engine_t();
    ~engine_t() { stop(); }
    void new_game(std::string fen);
    void copy_results(search_t * s);
    void analyse();
    
    game_t * settings() { 
        return & _game;
    }

    int cpu_count() {
        return sysconf(_SC_NPROCESSORS_ONLN);
    }

    bool think() {
        return start_search(_think, false);
    }

    void learn() {
        stop();
        _control.stop_requested.store(false);
        this->create(_learn, this);
    }

    void book_calc() {
        start_search(_book_calc, true);
    }

    void set_ponder(bool do_ponder) {
        _control.ponder.store(do_ponder);
    }

    void stop() {
        _control.stop_requested.store(true);
        this->stop_all();
    }

    bool stop_requested() const {
        return _control.stop_requested.load();
    }

    bool is_pondering() const {
        return _control.ponder.load();
    }

    void set_position(std::string fen, const std::vector<U64> & position_history = {}) {
        stop();
        _root_fen = fen;
        _position_history = position_history;
    }

    search_result_t get_result() const {
        std::lock_guard<std::mutex> lock(_result_mutex);
        return _result;
    }

    U64 get_total_nodes() {
        return get_result().nodes;
    }

    int get_score() {
        return get_result().score;
    }

    move_t get_move() {
        return get_result().move;
    }

    bool target_found() {
        return get_result().target_found;
    }

};

namespace engine {
    void stop();
    void go();
    void analyse();
    void learn();
    void book_calc();
    void new_game(std::string fen);
    void set_position(std::string fen, const std::vector<U64> & position_history = {});
    void set_ponder(bool ponder);
    bool is_stopped();
    bool is_ponder();
    game_t * settings();
    engine_t * instance();
}

#endif	/* ENGINE_H */

