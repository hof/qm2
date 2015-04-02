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

#include "eval.h"
#include "search.h"
#include "movepicker.h"
#include "book.h"
#include "threadman.h"
#include "uci_console.h"
#include "timeman.h"
#include "version.h"

class engine_t : public threads_t {
private:
    game_t _game;
    std::string _root_fen;
    U64 _total_nodes;
    bool _target_found;
    bool _stop_all;
    bool _ponder;
    move_t _result_move;
    int _result_score;
    int _wild;
    
    static void * _think(void * engineObjPtr);
    static void * _learn(void * engineObjPtr);
    
    void _create_start_positions(search_t * root, book_t * book, std::string * pos, int &x, const int max);

public:
    
    engine_t();
    void new_game(std::string fen);
    void copy_results(search_t * s);
    void analyse();
    
    game_t * settings() { 
        return & _game;
    }

    int cpu_count() {
        return sysconf(_SC_NPROCESSORS_ONLN);
    }

    void think() {
        stop();
        _stop_all = false;
        this->create(_think, this);
    }

    void learn() {
        _stop_all = false;
        this->create(_learn, this);
    }

    void set_ponder(bool do_ponder) {
        _ponder = do_ponder;
    }

    void stop() {
        _stop_all = true;
        this->stop_all();
    }

    void set_position(std::string fen) {
        _root_fen = fen;
    }

    void set_total_nodes(U64 total_nodes) {
        _total_nodes = total_nodes;
    }

    U64 get_total_nodes() {
        return _total_nodes;
    }

    void set_target_found(bool found) {
        _target_found = found;
    }

    void set_move(move_t * move) {
        _result_move.set(move);
    }

    void set_score(int score) {
        _result_score = score;
    }
    
    void set_wild(int wild) {
        _wild = wild;
    }

    int get_score() {
        return _result_score;
    }

    move_t get_move() {
        return _result_move;
    }
    
    bool target_found() {
        return _target_found;
    }
};

namespace engine {
    void stop();
    void go();
    void analyse();
    void learn();
    void new_game(std::string fen);
    void set_position(std::string fen);
    void set_ponder(bool ponder);
    void set_wild(int wild);
    bool is_stopped();
    bool is_ponder();
    game_t * settings();
    engine_t * instance();
}

#endif	/* ENGINE_H */

