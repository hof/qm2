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
 * File: game.h
 * Holds global game information
 */

#ifndef GAME_H
#define	GAME_H

#include "move.h"
#include "timeman.h"
#include "version.h"

namespace options {

    enum option_t_type {
        BOOL, INT, STRING
    };
    
    struct option_t {
        const char * key;
        int type;
        int value;
        const char * uci_option; 
    };

    const int length = 16;
    extern option_t PARAM[length+1];
    
    option_t * get_option(const char * key);
    int get_value(const char * key);
    const char * get_uci_option(const char * key);
};

class game_t {
public:
    time_manager_t tm;
    move_t target_move;
    U64 max_nodes;
    U64 max_time_per_move;
    int max_depth;
    int target_score;
    int white_time;
    int black_time;
    int white_increment;
    int black_increment;
    int moves_left;
    bool ponder;
    double learn_factor;

    void clear();
    void copy(game_t * game);
    void test_for(move_t * move, int score, int max_nodes, int max_time = 0);
    void init_tm(bool white);

    game_t() {
        clear();
    }

    int time(bool white) {
        return white ? white_time : black_time;
    }

    int increment(bool white) {
        return white ? white_increment : black_increment;
    }
};

namespace game {
    game_t * instance();
};

#endif	/* GAME_H */

