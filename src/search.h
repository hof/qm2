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
#include <atomic>

// The owner keeps this alive until its search worker has been joined.
struct search_control_t {
    std::atomic<bool> stop_requested{false};
    std::atomic<bool> ponder{false};
};
#include "nnue.h"
#include <array>
#include <algorithm>

struct quiet_history_policy_t {
    bool main = true;
    bool continuation = true;
    bool pawn = true;
};

// Worker-owned tables. Piece keys include color; pawn buckets intentionally
// share colliding structures, like other bounded history heuristics.
struct quiet_history_t {
    static constexpr int limit = 16384;
    static constexpr int pawn_buckets = 512;
    int16_t main[2][64][64] = {};
    int16_t continuation[BKING + 1][64][BKING + 1][64] = {};
    int16_t pawn[pawn_buckets][BKING + 1][64] = {};

    static void update_entry(int16_t & entry, int bonus) {
        bonus = std::clamp(bonus, -limit, limit);
        entry += bonus - int(entry) * std::abs(bonus) / limit;
    }

    int continuation_score(const move_t & move, const move_t & previous) const {
        return continuation[previous.piece][previous.tsq][move.piece][move.tsq];
    }

    void update_continuation(const move_t & move, const move_t & previous, int bonus) {
        update_entry(continuation[previous.piece][previous.tsq][move.piece][move.tsq], bonus);
    }

    int score(bool us, const move_t & move, const move_t * previous, U64 pawn_key,
            const quiet_history_policy_t & policy = {}) const {
        int value = 0;
        if (policy.main) {
            value += main[us][move.ssq][move.tsq];
        }
        if (policy.pawn) {
            value += pawn[pawn_key % pawn_buckets][move.piece][move.tsq];
        }
        if (policy.continuation && previous && previous->piece) {
            value += continuation_score(move, *previous);
        }
        return value;
    }

    void update(bool us, const move_t & move, const move_t * previous, U64 pawn_key, int bonus,
            const move_t * protected_move = nullptr, const quiet_history_policy_t & policy = {}) {
        if (policy.main) {
            update_entry(main[us][move.ssq][move.tsq], bonus);
        }
        if (protected_move && protected_move->piece == move.piece
                && protected_move->tsq == move.tsq) {
            return;
        }
        if (policy.pawn) {
            update_entry(pawn[pawn_key % pawn_buckets][move.piece][move.tsq], bonus);
        }
        if (policy.continuation && previous && previous->piece) {
            update_continuation(move, *previous, bonus);
        }
    }
};
#include <algorithm>

// Worker-owned history. The move records the EP victim even on an empty target;
// non-capturing promotions use EMPTY, and the mover remains the original pawn.
struct capture_history_t {
    static constexpr int limit = 16384;
    int16_t entries[BKING + 1][64][WKING + 1] = {};

    static int victim(const move_t & move) {
        return move.capture > WKING ? move.capture - WKING : move.capture;
    }

    static bool same_entry(const move_t & a, const move_t & b) {
        return a.piece == b.piece && a.tsq == b.tsq && victim(a) == victim(b);
    }

    int score(const move_t & move) const {
        return entries[move.piece][move.tsq][victim(move)];
    }

    void update(const move_t & move, int bonus) {
        auto & entry = entries[move.piece][move.tsq][victim(move)];
        bonus = std::clamp(bonus, -limit, limit);
        entry += bonus - int(entry) * std::abs(bonus) / limit;
    }
};
#include <algorithm>

// Side-to-move pawn patterns share a bounded estimate of static-eval error.
// Collisions are intentional; this is a heuristic, never a score certificate.
struct correction_history_t {
    static constexpr int size = 16384;
    static constexpr int limit = 16384;
    static constexpr int grain = 128;
    int16_t entries[2][size] = {};
    // Separate domains keep unlike features and continuation distances independent.
    int16_t pieces[4][2][size] = {}; //white/black nonpawn, minor, major
    int16_t continuation[2][2][size] = {};

    static int bonus_for(int depth, int error) {
        return std::clamp(std::clamp(error, -32768, 32768) * std::clamp(depth, 1, 16) / 8,
            -limit / 4, limit / 4);
    }

    static bool update_entry(int16_t & entry, int bonus) {
        const int before = entry;
        entry += bonus - int(entry) * std::abs(bonus) / limit;
        return entry != before;
    }

    int correction(bool side, U64 pawn_key) const {
        return entries[side][pawn_key & (size - 1)] / grain;
    }

    bool update(bool side, U64 pawn_key, int depth, int error) {
        return update_entry(entries[side][pawn_key & (size - 1)], bonus_for(depth, error));
    }
};
#include "movepicker.h"
#include "hashtable.h"
class search_t;

int scale_network_score(int raw, const board_t & board, int halfmoves);
int evaluate(search_t * search);

// Halfmove clocks 0..13 share score bounds; later clocks use buckets of eight.
// Scores within a bucket reuse each other; clock ranges still guard draws.
inline int rule50_bucket(int halfmoves) {
    return halfmoves < 14 ? 0 : (halfmoves - 14) / 8 + 1;
}

// Halfmove-clock buckets keep TT scores apart: one fixed salt per bucket,
// mixed at compile time, is XORed into the position key.
inline constexpr auto RULE50_SALTS = [] {
    std::array<U64, 32> salts{};
    for (U64 bucket = 1; bucket < salts.size(); ++bucket) {
        U64 key = bucket ^ 0x72756c6535303031ULL;
        key = (key ^ (key >> 30)) * 0xbf58476d1ce4e5b9ULL;
        key = (key ^ (key >> 27)) * 0x94d049bb133111ebULL;
        salts[bucket] = key ^ (key >> 31);
    }
    return salts;
}();

inline U64 position_key(const board_t & board) {
    const int bucket = std::min(rule50_bucket(board.stack->fifty_count), int(RULE50_SALTS.size()) - 1);
    return board.stack->tt_key ^ RULE50_SALTS[bucket];
}

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
    int move_count; // One-based ordinal among considered legal moves; zero outside a move loop.
    move_t current_move;
    move_t best_move;
    move_t tt_move;
    move_t killer[2];
    move_t pv_moves[MAX_PLY + 1];
    bool in_check;
    int repetition_start;
    uint8_t pv_count;
    int16_t eval_result;
    bool improving;
    bool tt_pv;
    bool lmr_tt_tactical;
    bool lmr_tt_fail_low;
    bool lmr_tt_deep;
    int reduction; // Depth decrement of the move being searched, in 1/1024 plies.
    int static_eval; // Corrected static evaluation, invalid in check.
    int lmr_correction;
    int lmr_alpha_raises;
    int multiple_extensions; // Multiple-extension events on the current line.
    uint16_t excluded_move;
    U64 tt_key;
};

enum class result_source_t { none, partial, complete, fallback, book, terminal, tablebase };

class search_t {
private:
    const search_control_t * _control;
    std::shared_ptr<const experience::book_t> _experience;
    std::mt19937_64 _book_random;
    bool _book_seeded;
    bool _own_book;
    int _experience_depth;

protected:
    // Repetition keys before the root, oldest first; never includes the root itself.
    std::vector<U64> _position_history;
    static bool has_legal_move(board_t & brd);
    int ceiling_score();
    bool raise_repetition_alpha(int & alpha);

public:
    search_stack_t _stack[MAX_PLY + 1];
    game_t game;
    std::shared_ptr<const nnue::network_t> evaluation_network;
    std::vector<nnue::accumulator_t> evaluation_accumulators;
    int neural_static_score();
    board_t brd;
    root_t root;
    search_stack_t * stack;
    search_stack_t * root_stack;
    U64 nodes;
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
    std::unique_ptr<correction_history_t> correction_history;
    bool correction_history_enabled;
    bool piece_correction_enabled;
    bool continuation_correction_enabled;
    std::array<U64, 2> correction_context_keys() const;
    int correction_value() const;
    bool tt_eval_enabled;
    int corrected_static_eval(int raw);
    void update_correction_history(int value, int bound, int depth);
    bool lmp_enabled;
    bool history_pruning_enabled;
    bool tt_history_enabled;
    bool capture_history_enabled;
    bool long_continuation_enabled;
    bool threat_ordering_enabled;
    bool check_ordering_enabled;
    quiet_history_policy_t quiet_history_policy;
    bool countermoves_enabled;
    bool killers_enabled;
    bool upcoming_repetition_enabled;
    bool evasions_enabled;
    uint16_t countermoves[BKING + 1][64];
    move_t ponder_move;
    std::string book_name;
    bool ponder_clock_started;
    bool root_wtm;
    int wild;
    int draw_contempt;
    // Total reduction includes the artificial pass itself.
    bool null_verify;
    bool null_enabled;
    bool beta_pruning;
    bool lmr_enabled;
    bool lmr_history_enabled;
    bool aspiration_enabled;
    bool probcut_enabled;
    bool singular_extensions_enabled;
    bool razoring_enabled;
    bool main_see_enabled;
    bool iir_enabled;
    bool q_tt_enabled;
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
    void order_root_moves(int depth);
    void store_pv();
    void trace_root(int alpha, int beta, int depth);
    void forward();
    void backward();
    void forward(move_t * move, bool givesCheck);
    void backward(move_t * move);
    void update_history(move_t * move);
    std::array<const move_t *, 2> long_continuation_contexts() const;
    // Standard search: learned history only, queried before making the move.
    int move_history_score(const move_t & move);
    void update_capture_cutoff(const move_t & move, int depth, int searched_moves);
    void take_history(search_t & previous);
    void update_tt_history(int compact_move, int depth);
    void update_quiet_cutoff(move_t * move, int depth, int searched_moves);
    void update_killers(move_t * move);
    bool is_killer(move_t * const move);
    bool in_searched(move_t * move, int searched_moves);
    void reset_stack();
    int reduction(int depth, int searched_moves, int estimate, bool root_node = false);
    int reduction_estimate(int depth, int searched_moves, const move_t & move,
        bool pv, bool cut_node, int gives_check);
    void update_lmr_history(const move_t & move, int depth, int value, int alpha, int beta);

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

namespace repetition {
// Decode a reversible non-pawn move delta; accept only a legal quiet move.
bool find_move(board_t & board, U64 delta, move_t & move);
}

#endif	/* SEARCH_H */

