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
 * File: game.cpp
 * Holds global game information
 */

#include "game.h"

void game_t::clear() {
    opponent.clear();
    target_move.set(0);
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
    opponent.copy(&game->opponent);
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
}

void game_t::test_for(move_t * move, int score, int max_t, int max_d) {
    clear();
    target_move.set(move);
    target_score = score;
    max_time_per_move = max_t;
    max_depth = max_d;
}

