#include "engine.h"

namespace {

class cutoff_search_t : public search_t {
    int _quiet_before;
public:
    move_t target{};
    int quiet_calls = 0;
    int child_calls = 0;
    explicit cutoff_search_t(int quiet_before)
        : search_t("r3k2r/p1ppqpb1/bn2pnp1/3PN3/1p2P3/2N2Q1p/PPPBBPPP/R3K2R w KQkq - 0 1"),
          _quiet_before(quiet_before) {
        null_enabled = beta_pruning = lmr_enabled = ffp_enabled = false;
        iid_enabled = false;
        stack->in_check = brd.in_check();
    }
    int pvs(int, int, int, bool = false) override {
        ++child_calls;
        auto & move = (stack - 1)->current_move;
        if (!brd.legal()) {
            std::abort();
        }
        if (target.piece) {
            return target.equals(&move) ? -1 : 0;
        }
        const bool quiet = !move.capture && !move.promotion && !move.castle;
        return quiet && quiet_calls++ == _quiet_before ? -1 : 0;
    }
};

bool cutoff_updates() {
    for (int seed : {0, 1024, -1024, quiet_history_t::limit, -quiet_history_t::limit}) {
        for (int depth : {1, 4, 20}) {
            for (int quiet_before : {1, 4, 20}) {
                trans_table::clear();
                auto search = std::make_unique<cutoff_search_t>(quiet_before);
                for (auto & from : search->quiet_history->main[WHITE]) {
                    std::fill(std::begin(from), std::end(from), seed);
                }
                const auto fen = search->brd.to_string();
                const auto key = search->brd.stack->tt_key;
                const int value = search->search_t::pvs(0, 1, depth);
                const auto & best = search->stack->best_move;
                const int d = std::clamp(depth, 1, 8);
                const int bonus = 32 * d * d;
                const int reward = seed + bonus - seed * bonus / quiet_history_t::limit;
                const int penalty = seed - bonus - seed * bonus / quiet_history_t::limit;
                if (value != 1 || search->quiet_calls != quiet_before + 1 || best.capture
                        || best.promotion || best.castle || search->stop_all
                        || search->brd.to_string() != fen || search->brd.stack->tt_key != key
                        || search->stack != search->root_stack
                        || search->quiet_history->main[WHITE][best.ssq][best.tsq] != reward) {
                    std::cerr << "controlled history cutoff failed\n";
                    return false;
                }
                int quiets = 0;
                for (int i = 0; i < search->child_calls - 1; ++i) {
                    const auto & move = search->stack->searched[i];
                    if (!move.capture && !move.promotion && !move.castle) {
                        ++quiets;
                        if (search->quiet_history->main[WHITE][move.ssq][move.tsq] != penalty) {
                            return false;
                        }
                    }
                }
                if (quiets != quiet_before) {
                    return false;
                }
            }
        }
    }
    return true;
}

bool killer_alias() {
    trans_table::clear();
    auto search = std::make_unique<cutoff_search_t>(1);
    search->stack->killer[0].set(&search->brd, "c3b1");
    search->stack->killer[1].set(&search->brd, "c3d1");
    search->target = search->stack->killer[1];
    const int result = search->search_t::pvs(0, 1, 1);
    return result == 1 && search->stack->best_move.equals(&search->target)
        && search->quiet_history->main[WHITE][c3][d1] == 32
        && search->quiet_history->main[WHITE][c3][b1] == -32;
}

bool context_and_bounds() {
    auto history = std::make_unique<quiet_history_t>();
    move_t previous, other_previous, move, other_from;
    previous.set(BKNIGHT, f6, e4);
    other_previous.set(BKNIGHT, f6, g4);
    move.set(WKNIGHT, g1, f3);
    other_from.set(WKNIGHT, d2, f3);
    history->update(WHITE, move, &previous, 1, 32);
    if (history->score(WHITE, move, &previous, 1) != 96
            || history->score(WHITE, move, &other_previous, 1) != 64
            || history->score(WHITE, move, nullptr, 2) != 32
            || history->main[WHITE][other_from.ssq][other_from.tsq] != 0
            || history->main[BLACK][move.ssq][move.tsq] != 0) {
        return false;
    }
    history->update(WHITE, other_from, &previous, 1, -32, &move);
    if (history->pawn[1][WKNIGHT][f3] != 32
            || history->continuation[BKNIGHT][e4][WKNIGHT][f3] != 32
            || history->main[WHITE][d2][f3] != -32) {
        return false;
    }
    int16_t entry = 0;
    for (int bonus : {32, 2048, quiet_history_t::limit, -32, -2048, -quiet_history_t::limit}) {
        for (int i = 0; i < 1000; ++i) {
            quiet_history_t::update_entry(entry, bonus);
            if (entry < -quiet_history_t::limit || entry > quiet_history_t::limit) {
                return false;
            }
        }
    }
    return true;
}

bool component_controls() {
    const char * names[] = {"MainHistory", "ContinuationHistory", "PawnHistory", "Countermoves", "Killers"};
    for (int mask = 0; mask < 32; ++mask) {
        for (int i = 0; i < 5; ++i) {
            options::get_option(names[i])->value = (mask >> i) & 1;
        }
        trans_table::clear();
        auto search = std::make_unique<cutoff_search_t>(1);
        // A real preceding move supplies both continuation and countermove context.
        move_t previous;
        previous.set(&search->brd, "a2a3");
        search->forward(&previous, search->brd.gives_check(&previous));
        search->stack->in_check = search->brd.in_check();
        const auto policy = search->quiet_history_policy;
        if (policy.main != bool(mask & 1) || policy.continuation != bool(mask & 2)
                || policy.pawn != bool(mask & 4) || search->countermoves_enabled != bool(mask & 8)
                || search->killers_enabled != bool(mask & 16)) {
            return false;
        }
        // Changing global options after worker creation must not alter the snapshot.
        for (const char * name : names) {
            options::get_option(name)->value = 1;
        }
        const int value = search->search_t::pvs(0, 1, 1);
        const auto & best = search->stack->best_move;
        const auto & history = *search->quiet_history;
        const auto bucket = search->brd.stack->pawn_key % quiet_history_t::pawn_buckets;
        if (value != 1 || history.main[BLACK][best.ssq][best.tsq] != ((mask & 1) ? 32 : 0)
                || history.continuation[previous.piece][previous.tsq][best.piece][best.tsq]
                    != ((mask & 2) ? 32 : 0)
                || history.pawn[bucket][best.piece][best.tsq] != ((mask & 4) ? 32 : 0)
                || search->countermoves[previous.piece][previous.tsq] != ((mask & 8) ? best.to_compact() : 0)
                || bool(search->stack->killer[0].piece) != bool(mask & 16)
                || search->quiet_score(best) != 32 * ((bool(mask & 1)) + bool(mask & 2) + bool(mask & 4))) {
            std::cerr << "component cutoff/snapshot failed: " << mask << '\n';
            return false;
        }
        // Retained nonzero entries must neither score nor change while disabled.
        auto & mutable_history = *search->quiet_history;
        mutable_history.main[BLACK][best.ssq][best.tsq] = 101;
        mutable_history.continuation[previous.piece][previous.tsq][best.piece][best.tsq] = 202;
        mutable_history.pawn[bucket][best.piece][best.tsq] = 303;
        if (search->quiet_score(best) != ((mask & 1) ? 101 : 0)
                + ((mask & 2) ? 202 : 0) + ((mask & 4) ? 303 : 0)) {
            return false;
        }
        search->update_quiet_cutoff(&search->stack->best_move, 1, 0);
        if ((!(mask & 1) && mutable_history.main[BLACK][best.ssq][best.tsq] != 101)
                || (!(mask & 2) && mutable_history.continuation[previous.piece][previous.tsq][best.piece][best.tsq] != 202)
                || (!(mask & 4) && mutable_history.pawn[bucket][best.piece][best.tsq] != 303)) {
            return false;
        }
    }
    return true;
}

bool history_ownership() {
    const char * fen = "7k/8/8/8/8/8/8/K7 w - - 0 1";
    auto old = std::make_unique<search_t>(fen);
    old->quiet_history->main[WHITE][a1][a2] = -500;
    old->quiet_history->continuation[BKING][h7][WKING][a2] = 900;
    old->quiet_history->pawn[0][WKING][a2] = 300;
    old->countermoves[BKING][h7] = 123;
    auto * address = old->quiet_history.get();
    auto next = std::make_unique<search_t>(fen);
    next->take_quiet_history(*old);
    if (old->quiet_history || next->quiet_history.get() != address
            || next->quiet_history->main[WHITE][a1][a2] != -500
            || next->quiet_history->continuation[BKING][h7][WKING][a2] != 900
            || next->quiet_history->pawn[0][WKING][a2] != 300
            || next->countermoves[BKING][h7] != 123) {
        return false;
    }
    auto fresh = std::make_unique<search_t>(fen);
    return fresh->quiet_history->main[WHITE][a1][a2] == 0
        && fresh->quiet_history->continuation[BKING][h7][WKING][a2] == 0
        && fresh->quiet_history->pawn[0][WKING][a2] == 0
        && fresh->countermoves[BKING][h7] == 0;
}

}

int main() {
    magic::init();
    return cutoff_updates() && killer_alias() && context_and_bounds() && component_controls() && history_ownership() ? EXIT_SUCCESS : EXIT_FAILURE;
}
