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
 * File: engine.h
 * Implements the chess engine
 *
 * Created on 26 april 2011, 17:56
 */

#ifndef ENGINE_H
#define	ENGINE_H

#include <memory>
#include <mutex>

#include "search.h"
#include "movepicker.h"
#include "book.h"

#include "worker_pool.h"

#include "uci_console.h"
#include "timeman.h"

struct search_result_t {
    move_t move{};
    int score = 0;
    U64 nodes = 0;
    std::vector<U64> worker_nodes; // Final contributions; workers retain local counters.
    int score_flag = 0;
    int depth = 0;
    int completed_depth = 0;
    int attempted_depth = 0;
    bool interrupted = false;
    result_source_t source = result_source_t::none;
    bool target_found = false;
};

// One immutable request snapshot plus atomics used by all workers of that request.
// Jobs retain this session until completion; later requests get fresh controls.
struct search_session_t {
    game_t settings;
    std::string root_fen;
    std::vector<U64> position_history;
    search_control_t control;
    std::unique_ptr<search_team_t> team;
};

class engine_t {
private:
    game_t _game;
    int _played_score = 0;  // the score of the last search whose move was played, not a ponder miss
    bool _played_scored = false;
    std::string _root_fen;
    std::vector<U64> _position_history;
    worker_pool_t _workers;
    std::shared_ptr<search_session_t> _session = std::make_shared<search_session_t>();
    std::unique_ptr<search_t> _search;
    std::vector<std::unique_ptr<search_t>> _helpers;
    void start_helpers();
    void finish_helpers();
    mutable std::mutex _result_mutex;
    search_result_t _result;

    bool start_search(void * (*function)(void *), bool calculate_book);

    static void * _think(void * engineObjPtr);
    static void * _book_calc(void * engineObjPrt);
    

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

    void book_calc() {
        start_search(_book_calc, true);
    }

    void set_ponder(bool do_ponder) {
        _session->control.ponder.store(do_ponder);
    }

    void stop_all();
    bool set_threads(int count);

    void stop() {
        _session->control.stop_requested.store(true);
        this->stop_all();
    }

    bool stop_requested() const {
        return _session->control.stop_requested.load();
    }

    bool is_pondering() const {
        return _session->control.ponder.load();
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

// Read only after every worker has completed; incomplete iterations never vote.
size_t select_search_worker(std::span<search_t * const> workers);

namespace engine {
    void stop();
    void go();
    void analyse();
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

