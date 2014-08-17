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
 * File: engine.h
 * Implements the chess engine
 *
 * Created on 26 april 2011, 17:56
 */

#ifndef ENGINE_H
#define	ENGINE_H

#include "evaluate.h"
#include "search.h"
#include "movepicker.h"
#include "opponent.h"
#include "book.h"
#include "threadman.h"
#include "uci_console.h"
#include "timeman.h"
#include "game.h"

class TEngine : public threads_t {
private:
    game_t _game;
    static void * _think(void * engineObjPtr);
    static void * _learn(void * engineObjPtr);
    std::string _rootFen;
    U64 _nodesSearched;
    bool _target_found;
    move_t _resultMove;
    int _resultScore;
    bool _engineStop;
    bool _enginePonder;

public:
    void _create_start_positions(search_t * root, book_t * book, std::string * pos, int &x, const int max);

    TEngine() : threads_t() {
        _nodesSearched = 0;
        _target_found = false;
        _engineStop = false;
        _enginePonder = false;
        _rootFen = "";
        _resultMove.set(0);
        _resultScore = 0;
        _game.clear();
    }
    
    game_t * settings() { 
        return & _game;
    }

    int CPUCount() {
        return sysconf(_SC_NPROCESSORS_ONLN);
    }

    void think() {
        stop();
        _engineStop = false;
        this->create(_think, this);
    }

    void learn() {
        _engineStop = false;
        this->create(_learn, this);
    }

    void setPonder(bool doPonder) {
        _enginePonder = doPonder;
    }

    void stop() {
        _engineStop = true;
        this->stop_all();
    }

    void newGame(std::string fen) {
        _resultMove.set(0);
        _resultScore = 0;
        setPosition(fen);
        trans_table::clear();
    }

    

    void setPosition(std::string fen) {
        _rootFen = fen;
    }

    void setNodesSearched(U64 nodesSearched) {
        _nodesSearched = nodesSearched;
    }

    U64 getNodesSearched() {
        return _nodesSearched;
    }

    void set_target_found(bool found) {
        _target_found = found;
    }

    void setMove(move_t * move) {
        _resultMove.set(move);
    }

    void setScore(int score) {
        _resultScore = score;
    }

    int getScore() {
        return _resultScore;
    }

    move_t getMove() {
        return _resultMove;
    }

    bool target_found() {
        return _target_found;
    }

    void analyse();


};

namespace engine {
    void stop();
    void go();
    void analyse();
    void learn();
    void new_game(std::string fen);
    void set_position(std::string fen);
    void set_ponder(bool ponder);
    bool is_stopped();
    bool is_ponder();
    game_t * settings();
    TEngine * instance();
}


#endif	/* ENGINE_H */

