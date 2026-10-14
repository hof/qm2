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
    uint64_t calls = 0, cached_returns = 0;
    uint64_t reuses = 0, updates = 0;
    uint64_t root_refreshes = 0, king_refreshes = 0, parent_refreshes = 0;
    uint64_t q_delta_prunes = 0, q_see_prunes = 0;
    uint64_t q_delta_candidates = 0;
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
