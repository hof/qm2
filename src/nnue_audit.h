#ifndef MAXIMA_NNUE_AUDIT_H
#define MAXIMA_NNUE_AUDIT_H

#ifdef MAXIMA_NNUE_SEARCH_AUDIT
#include <cstdint>
#include <string>
#include <vector>

class search_t;

namespace nnue {
enum class audit_mode_t { verify, full, off };

struct audit_counters_t {
    uint64_t pvs_nodes = 0, q_nodes = 0, root_calls = 0;
    uint64_t move_visits = 0, null_visits = 0;
    uint64_t evasion_nodes = 0, double_check_nodes = 0;
    uint64_t capture_history_scores = 0, capture_history_hits = 0;
    uint64_t capture_history_rewards = 0, capture_history_penalties = 0;
    uint64_t capture_history_cutoffs = 0, capture_cutoff_rank_sum = 0, capture_first_cutoffs = 0;
    uint64_t long_history_scores[2] = {}, long_history_hits[2] = {};
    uint64_t long_history_rewards[2] = {}, long_history_penalties[2] = {};
    uint64_t long_history_cutoffs = 0, long_history_rank_sum = 0, long_history_first = 0;
    uint64_t repetition_attempts = 0, repetition_hits = 0, repetition_cutoffs = 0;
    uint64_t q_repetition_hits = 0, q_repetition_cutoffs = 0;
    uint64_t researches = 0, root_researches = 0, research_fail_highs = 0;
    uint64_t hash_probes = 0, hash_moves = 0, hash_first = 0, hash_rejected = 0, hash_score_cutoffs = 0;
    uint64_t iid_calls = 0, iid_candidates = 0, iid_moves = 0, iid_cancelled = 0, iid_visits = 0;
    uint64_t verification_calls = 0, excluded_calls = 0, verification_cancelled = 0, verification_visits = 0;
    uint64_t min_iid_depth = 0, max_iid_depth = 0, min_iid_probe_depth = 0;
    uint64_t aspiration_searches = 0, aspiration_attempts = 0;
    uint64_t aspiration_fail_lows = 0, aspiration_fail_highs = 0;
    uint64_t aspiration_successes = 0, aspiration_cancelled = 0, aspiration_full_windows = 0;
    uint64_t min_aspiration_depth = 0, max_aspiration_depth = 0;
    uint64_t lmr_candidates = 0, lmr_reductions = 0, lmr_researches = 0;
    uint64_t lmr_refutations = 0, lmr_cutoffs = 0;
    uint64_t min_lmr_depth = 0, max_lmr_depth = 0, min_lmr_child_depth = 0;
    uint64_t extension_candidates = 0, extension_see_rejected = 0;
    uint64_t extensions_applied = 0;
    uint64_t max_check_extensions = 0, max_main_ply = 0, max_q_ply = 0;
    uint64_t calls = 0, cached_returns = 0;
    uint64_t reuses = 0, updates = 0;
    uint64_t root_refreshes = 0, king_refreshes = 0, parent_refreshes = 0;
    uint64_t q_delta_prunes = 0, q_see_prunes = 0;
    uint64_t q_delta_candidates = 0;
    uint64_t q_see_candidates = 0;
    uint64_t q_check_candidates = 0, q_check_rejected = 0, q_check_searched = 0;
    uint64_t q_castles = 0;
    uint64_t null_attempts = 0, null_cutoffs = 0;
    uint64_t beta_candidates = 0, beta_cutoffs = 0;
    uint64_t ffp_candidates = 0, ffp_prunes = 0;
};

struct audit_state_t {
    audit_mode_t mode = audit_mode_t::verify;
    audit_counters_t counters;
    bool q_delta = true, q_see = true;
    bool fixed_horizon = false;
    bool aligned_quiescence = false;
    bool quiet_checks = false;
    bool quiet_castling = false;
    bool check_extensions = false;
    bool hash_moves = false;
    std::string root_fen;
    struct cutoff_t {
        std::string fen;
        int alpha, beta, depth, ply, eval;
    };
    bool record_cutoffs = false;
    std::vector<cutoff_t> beta_cutoffs;
};

int audit_evaluate(search_t & search);
void audit_beta_cutoff(search_t & search, int alpha, int beta, int depth, int eval);
}
#endif
#endif
