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
 * File: game.cpp
 * Holds global game information
 */

#include "game.h"

namespace options {
    
    option_t PARAM[length+1] = {
        { "", INT, 0, ""}, //dummy value
        { "Revision", STRING, 1, "type string default " MAXIMA_REVISION },
        { "Hash", INT, 256, "type spin default 256 min 0 max 1024"},
        { "Ponder", BOOL, 1, "type check default true"},
        { "OwnBook", BOOL, 1, "type check default true" },
        { "UCI_AnalyseMode", BOOL, 0, "type check default false" },
        { "UCI_Opponent", STRING, 0, "type string" },
        { "UCI_Chess960", BOOL, 0, "type check default false" },
        { "NullMove", BOOL, 1, "type check default true" },
        { "BetaPruning", BOOL, 1, "type check default true" },
        { "LMR", BOOL, 1, "type check default true" },
        { "PVExtensions", BOOL, 1, "type check default true" },
        { "FutilityPruning", BOOL, 1, "type check default true" },
        { "LateMovePruning", BOOL, 1, "type check default true" },
        { "NullVerify", BOOL, 1, "type check default true" },
        { "NullAdaptiveValue", BOOL, 1, "type check default true" },
        { "NullAdaptiveDepth", BOOL, 1, "type check default true" },
        { "Wild", STRING, 0, "type combo default standard var standard var losers" },
        { "KingAttackShelter", INT, 256, "type spin default 256 min 0 max 512" },
        { "KingAttackPieces", INT, 256, "type spin default 256 min 0 max 512" }
    };
    
    option_t * get_option(const char * key) {
        for (int i = 1; i <= length; i++) {
            if (strcmp(key, PARAM[i].key) == 0) {
                return &PARAM[i];
            }
        }
        return &PARAM[0];
    }
    
    int get_value(const char * key) {
        return get_option(key)->value;
    }

    const char * get_uci_option(const char * key) {
        return get_option(key)->uci_option;
    }
}

void game_t::clear() {
    tm.clear();
    target_move.clear();
    max_depth = MAX_PLY;
    max_time_per_move = 0;
    target_score = 0;
    white_time = 0;
    black_time = 0;
    white_increment = 0;
    black_increment = 0;
    moves_left = 0;
    max_nodes = 0;
    ponder = false;
    learn_factor = 1.0;
}

void game_t::copy(game_t* game) {
    max_depth = game->max_depth;
    max_time_per_move = game->max_time_per_move;
    target_score = game->target_score;
    target_move.set(&game->target_move);
    white_time = game->white_time;
    black_time = game->black_time;
    white_increment = game->white_increment;
    black_increment = game->black_increment;
    moves_left = game->moves_left;
    max_nodes = game->max_nodes;
    ponder = game->ponder;
    learn_factor = game->learn_factor;
    tm = game->tm;
}

void game_t::init_tm(bool us) {
    tm.set_start();
    if (max_time_per_move) {
        tm.set_min(max_time_per_move);
        tm.set_max(max_time_per_move);
    } else if (white_time || black_time) {
        tm.set(time(us), time(!us), increment(us), increment(!us), moves_left);
    } else {
        tm.set_min(time_man::INFINITE_TIME);
        tm.set_max(time_man::INFINITE_TIME);
    }

}

void game_t::test_for(move_t * move, int score, int max_n, int max_t) {
    clear();
    target_move.set(move);
    target_score = score;
    max_nodes = max_n;
    max_time_per_move = max_t? max_t : time_man::INFINITE_TIME;
    max_depth = MAX_PLY;
}

namespace game {
    game_t _game;

    game_t * instance() {
        return & _game;
    }
}

