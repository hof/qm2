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

#include <vector>

#include "board.h"
#include "game.h"
#include "experience.h"
#include <random>
#include "search_control.h"
#include "nnue.h"
#include "quiet_history.h"
#include "capture_history.h"
#include "movepicker.h"
#include "hashtable.h"
#include "eval.h"


class root_move_t {
public:
    move_t move;
    bool gives_check;
    bool is_dangerous;
    int64_t nodes;
    int see;
    int checker_sq;
    U64 checkers;

    void init(move_t * m, bool checks, bool dangerous, int see_value);
    int compare(root_move_t * m, move_t * best_move);
    void add_nodes(U64 count);
};

class root_t {
public:
    root_move_t moves[move::MAX_MOVES];
    int move_count;
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
#ifdef MAXIMA_NNUE_SEARCH_AUDIT
    int check_extensions;
#endif
    int repetition_start;
    uint8_t pv_count;
    int16_t eval_result;
    bool improving;
    uint16_t excluded_move;
    search_metadata_t metadata;
    U64 tt_key;
    U64 tt_history;
    clock_range_t tt_clocks;
};

enum class verification_mode_t { none, null_move, excluded_move };

enum class result_source_t { none, partial, complete, fallback, book, terminal, tablebase };

class search_t {
private:
    struct neural_cache_entry_t {
        U64 key = 0;
        int value = score::INVALID;
    };
    std::vector<neural_cache_entry_t> _neural_cache;
    const search_control_t * _control;
    std::shared_ptr<const experience::book_t> _experience;
    std::mt19937_64 _book_random;
    bool _book_seeded;
    bool _own_book;
    int _experience_depth;

protected:
    // Repetition keys before the root, oldest first; never includes the root itself.
    std::vector<U64> _position_history;
    void init_tt_history();
    static bool has_legal_move(board_t & brd);
    int ceiling_score();
    bool raise_repetition_alpha(int & alpha, int beta, move_t & witness);

public:
    search_stack_t _stack[MAX_PLY + 1];
    game_t game;
    std::shared_ptr<const nnue::network_t> evaluation_network;
    std::vector<nnue::accumulator_t> evaluation_accumulators;
    int neural_static_score();
#ifdef MAXIMA_NNUE_SEARCH_AUDIT
    nnue::audit_state_t nnue_audit;
#endif
    board_t brd;
    root_t root;
    search_stack_t * stack;
    search_stack_t * root_stack;
    U64 nodes;
    U64 pruned_nodes;
    U64 tablebase_hits;
    int tablebase_limit;
    int tablebase_probe_depth;
    bool stop_all;
    int next_poll;
    int sel_depth;
    int result_score;
    int result_flag;
    int result_depth;
    int completed_depth;
    int attempted_depth;
    bool interrupted;
    result_source_t result_source;
    int history[BKING + 1][64]; //legacy royal-losers ordering
    std::unique_ptr<quiet_history_t> quiet_history;
    std::unique_ptr<capture_history_t> capture_history;
    bool capture_history_enabled;
    quiet_history_policy_t quiet_history_policy;
    bool countermoves_enabled;
    bool killers_enabled;
    bool upcoming_repetition_enabled;
    bool evasions_enabled;
    uint16_t countermoves[BKING + 1][64];
    move_t ponder_move;
    std::string book_name;
    bool root_wtm;
    int wild;
    int draw_contempt;
    bool null_verify;
    bool null_enabled;
    bool beta_pruning;
    bool lmr_enabled;
    bool aspiration_enabled;
    bool iid_enabled;
    bool q_tt_enabled;
    bool iid_active;
    verification_mode_t verification_mode;
    bool ffp_enabled;

    search_t(const char * fen, const game_t * g = NULL,
            const search_control_t * control = NULL,
            const std::vector<U64> & position_history = {}, int variant = 0) {
        init(fen, g, control, position_history, variant);
    }

    virtual ~search_t() {
    };
    search_t(const search_t &) = delete;
    search_t & operator=(const search_t &) = delete;
    void init(const char * fen, const game_t * g,
            const search_control_t * control = NULL,
            const std::vector<U64> & position_history = {}, int variant = 0);
    void go();
    void book_calc();
    void iterative_deepening();
    int aspiration(int depth, int last_score);
    void prepare_iid_move(int alpha, int beta, int depth);
    int verification_search(int alpha, int beta, int depth, bool cut_node,
        verification_mode_t mode, uint16_t excluded_move = 0);
    bool book_lookup();
    bool tablebase_lookup();
    bool probe_tablebase(int depth, int & value);
    int pvs_root(int alpha, int beta, int depth);
    virtual int pvs(int alpha, int beta, int depth, bool cut_node = false);
    int qsearch(int alpha, int beta, int depth);
    int qstatic(int beta, int gain);
    std::string pv_to_string();
    bool is_draw();
    bool is_repetition() const;
    int upcoming_repetition(move_t & witness);
    bool is_improving() const;
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
    bool try_forward(move_t * move, bool gives_check);
    void update_history(move_t * move);
    int quiet_score(const move_t & move) const;
    int capture_score(const move_t & move);
    void update_capture_cutoff(const move_t & move, int depth, int searched_moves);
    void take_history(search_t & previous);
    void update_quiet_cutoff(move_t * move, int depth, int searched_moves);
    void update_killers(move_t * move);
    bool is_killer(move_t * const move);
    bool in_searched(move_t * move, int searched_moves);
    void reset_stack();
    U64 tt_context() const;
    int extension(move_t * move, int gives_check);
    int reduction(int depth, int searched_moves, bool is_dangerous);

    int draw_score() {
        return brd.stack->wtm == root_wtm? draw_contempt: - draw_contempt;
    }

    bool is_recapture(move_t * move) {
        if (stack == root_stack) {
            return 0;
        }
        assert(brd.ply > 0);
        move_t * prev_move = &(stack - 1)->current_move;
        return move->capture && prev_move->capture && move->tsq == prev_move->tsq;
    }

    bool is_passed_pawn(move_t * move) {
        if (move->piece != WPAWN && move->piece != BPAWN) {
            return false;
        }
        if (move->capture || stack->in_check || stack->eval_result == score::INVALID) {
            return false;
        }
        return BIT(move->ssq) & stack->metadata.passed_pawns & SIDE[brd.them()];
    }
    
    bool is_advanced_passed_pawn(move_t * move) {
        if (move->piece == WPAWN && move->tsq >= a6 && is_passed_pawn(move)) {
            return true;
        } else if (move->piece == BPAWN && move->tsq <= h3 && is_passed_pawn(move)) {
            return true;
        } else {
            return false;
        }
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

