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
 * File:  search.h
 * Search methods for traditional chess. 
 * Maxima uses a fail-soft principle variation search (PVS) with
 * - null move pruning 
 * - futility pruning
 * - late move reductions (LMR) and late move pruning (LMP)
 *
 * Created on 13 mei 2011, 14:08
 */

#ifndef SEARCH_H
#define	SEARCH_H

#include "board.h"
#include "game.h"
#include "movepicker.h"
#include "hashtable.h"

namespace LMR {
    void init();
    void print();
};

class root_move_t {
public:
    move_t move;
    bool gives_check;
    int nodes;
    int see;
    int checker_sq;
    U64 checkers;

    void init(move_t * m, bool checks, int see_value);
    int compare(root_move_t * m, move_t * best_move);
};

class root_t {
public:
    root_move_t moves[move::MAX_MOVES];
    int move_count;
    int fifty_count;
    bool in_check;

    bool is_complex();
    bool is_easy();
    void sort_moves(move_t * best_move);
    void match_moves(move::list_t * list);
};

struct search_stack_t {
    move::list_t move_list;
    move_t searched[256];
    move_t current_move;
    move_t best_move;
    move_t tt_move;
    move_t killer[2];
    move_t pv_moves[MAX_PLY + 1];
    bool in_check;
    uint8_t pv_count;
    int16_t eval_result;
    int16_t eg_score;
    score_t eval_score;
    score_t pc_score[BKING + 1];
    score_t passer_score[2];
    pawn_table::entry_t * pt;
    material_table::entry_t * mt;
    U64 tt_key;
    int8_t king_attack[BKING + 1];
};

class search_t {
public:
    search_stack_t _stack[MAX_PLY + 1];
    game_t * game;
    board_t brd;
    root_t root;
    search_stack_t * stack;
    search_stack_t * root_stack;
    U64 nodes;
    U64 pruned_nodes;
    bool stop_all;
    bool skip_null;
    int next_poll;
    int sel_depth;
    int result_score;
    int history[BKING + 1][64];
    move_t ponder_move;
    std::string book_name;
    int wild;
    int king_attack_shelter;
    int king_attack_pieces;
    bool null_adaptive_depth;
    bool null_adaptive_value;
    bool null_verify;
    bool null_enabled;
    bool alpha_pruning;
    bool beta_pruning;
    bool pv_extensions;
    bool lmr_enabled;
    bool ffp_enabled;
    bool lmp_enabled;

    search_t(const char * fen, game_t * g = NULL) {
        init(fen, g);
    }

    virtual ~search_t() {
    };
    void init(const char * fen, game_t * g);
    void go();
    void iterative_deepening();
    int aspiration(int depth, int last_score);
    bool book_lookup();
    int pvs_root(int alpha, int beta, int depth);
    virtual int pvs(int alpha, int beta, int depth);
    int qsearch(int alpha, int beta, int depth);
    std::string pv_to_string();
    bool is_draw();
    bool abort(bool force_poll);
    bool pondering();
    int init_root_moves();
    void store_pv();
    void debug_print_search(int alpha, int beta, int depth);
    void trace_root(int alpha, int beta, int depth);
    void forward();
    void backward();
    void forward(move_t * move, bool givesCheck);
    void backward(move_t * move);
    void update_history(move_t * move);
    void update_killers(move_t * move);
    bool is_killer(move_t * const move);
    void reset_stack();

    int draw_score() {
        return 0;
    }

    bool is_recapture(move_t * move) {
        move_t * prev_move = &(stack - 1)->current_move;
        return move->capture && prev_move->capture && move->tsq == prev_move->tsq;
    }

    bool is_passed_pawn(move_t * move) {
        assert(stack->eval_result != score::INVALID);
        return BIT(move->ssq) & stack->pt->passers;
    }

    search_stack_t * get_stack(int ply) {
        assert(ply >= 0 && ply < MAX_PLY);
        return &_stack[ply];
    }

    void update_pv(move_t * move) {
        stack->pv_moves[0].set(move);
        memcpy(stack->pv_moves + 1, (stack + 1)->pv_moves, (stack + 1)->pv_count * sizeof (move_t));
        stack->pv_count = (stack + 1)->pv_count + 1;
    }
};

#endif	/* SEARCH_H */

