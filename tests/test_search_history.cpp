#include "engine.h"
#include "w17/w17_search.h"
#include <cstring>

namespace {

class cutoff_search_t : public search_t {
    int _quiet_before;
public:
    move_t target{};
    int quiet_calls = 0;
    int child_calls = 0;
    explicit cutoff_search_t(int quiet_before, const char * fen =
            "r3k2r/p1ppqpb1/bn2pnp1/3PN3/1p2P3/2N2Q1p/PPPBBPPP/R3K2R w KQkq - 0 1")
        : search_t(fen),
          _quiet_before(quiet_before) {
        null_enabled = beta_pruning = lmr_enabled = ffp_enabled = false;
        // This artificial child contract isolates bounds/history from pruning.
        lmp_enabled = history_pruning_enabled = false;
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
                || search->move_history_score(best) != 32 * ((bool(mask & 1)) + bool(mask & 2) + bool(mask & 4))) {
            std::cerr << "component cutoff/snapshot failed: " << mask << '\n';
            return false;
        }
        // Retained nonzero entries must neither score nor change while disabled.
        auto & mutable_history = *search->quiet_history;
        mutable_history.main[BLACK][best.ssq][best.tsq] = 101;
        mutable_history.continuation[previous.piece][previous.tsq][best.piece][best.tsq] = 202;
        mutable_history.pawn[bucket][best.piece][best.tsq] = 303;
        if (search->move_history_score(best) != ((mask & 1) ? 101 : 0)
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

void play(search_t & search, const char * text) {
    move_t move;
    move.set(&search.brd, text);
    search.forward(&move, search.brd.gives_check(&move));
    if (!search.brd.legal()) {
        std::abort();
    }
}

bool long_contexts() {
    auto search = std::make_unique<search_t>("rnbqkbnr/pppppppp/8/8/8/8/PPPPPPPP/RNBQKBNR w KQkq - 0 1");
    const char * moves[] = {"g1f3", "g8f6", "b1c3", "b8c6", "e2e3", "e7e6"};
    for (int ply = 0; ply <= 6; ++ply) {
        const auto contexts = search->long_continuation_contexts();
        for (int i = 0; i < 2; ++i) {
            const int distance = 2 + i * 2;
            if (contexts[i] != (ply >= distance ? &(search->stack - distance)->current_move : nullptr)) {
                return false;
            }
        }
        // Every intervening null pass bounds both lookup and learning context.
        for (int distance = 1; distance <= 4 && distance <= ply; ++distance) {
            auto & move = (search->stack - distance)->current_move;
            const auto saved = move;
            move.clear();
            const auto bounded = search->long_continuation_contexts();
            move = saved;
            if (bool(bounded[0]) != (ply >= 2 && distance > 2)
                    || bool(bounded[1]) != (ply >= 4 && distance > 4)) {
                return false;
            }
        }
        if (ply < 6) {
            play(*search, moves[ply]);
        }
    }
    const auto fen = search->brd.to_string();
    const auto key = search->brd.stack->tt_key;
    search->forward();
    if (search->long_continuation_contexts()[0] || search->long_continuation_contexts()[1]) {
        return false;
    }
    play(*search, "a7a6");
    if (search->long_continuation_contexts()[0] || search->long_continuation_contexts()[1]) {
        return false;
    }
    auto move = (search->stack - 1)->current_move;
    search->backward(&move);
    search->backward();
    return search->brd.to_string() == fen && search->brd.stack->tt_key == key
        && search->long_continuation_contexts()[1];
}

bool long_controls_and_learning() {
    for (int immediate : {0, 1}) {
        for (int enabled : {0, 1}) {
            options::get_option("ContinuationHistory")->value = immediate;
            options::get_option("LongContinuationHistory")->value = enabled;
            trans_table::clear();
            auto search = std::make_unique<cutoff_search_t>(1, "rnbqkbnr/pppppppp/8/8/8/8/PPPPPPPP/RNBQKBNR w KQkq - 0 1");
            options::get_option("ContinuationHistory")->value = 1;
            options::get_option("LongContinuationHistory")->value = 1;
            for (const char * text : {"g1f3", "g8f6", "b1c3", "b8c6"}) {
                play(*search, text);
            }
            const auto fen = search->brd.to_string();
            const auto key = search->brd.stack->tt_key;
            const auto * frame = search->stack;
            search->stack->in_check = search->brd.in_check();
            if (search->long_continuation_enabled != bool(enabled)
                    || search->search_t::pvs(0, 1, 1) != 1) {
                return false;
            }
            const auto winner = search->stack->best_move;
            const auto failed = search->stack->searched[0];
            const auto & previous = (search->stack - 1)->current_move;
            if (search->quiet_history->continuation_score(winner, previous) != 32 * immediate) {
                return false;
            }
            for (int distance : {2, 4}) {
                const auto & context = (search->stack - distance)->current_move;
                if (search->quiet_history->continuation_score(winner, context) != 32 * enabled
                        || search->quiet_history->continuation_score(failed, context) != -32 * enabled) {
                    return false;
                }
            }
            if (search->move_history_score(winner) != 64 + 32 * immediate + 64 * enabled
                    || search->brd.to_string() != fen || search->brd.stack->tt_key != key
                    || search->stack != frame) {
                return false;
            }
            // Even-color follow-up entries are disjoint from immediate contexts.
            // Dormant values survive disabling, ownership transfer and re-enabling.
            const auto context = (search->stack - 2)->current_move;
            search->quiet_history->update_continuation(winner, context, 500);
            const int retained = search->quiet_history->continuation_score(winner, context);
            const int original = search->move_history_score(winner);
            search->long_continuation_enabled = false;
            const int disabled = search->move_history_score(winner);
            search->update_quiet_cutoff(&search->stack->best_move, 1, 0);
            if (search->quiet_history->continuation_score(winner, context) != retained
                    || (enabled && original - disabled != retained + 32)) {
                return false;
            }
            auto next = std::make_unique<search_t>("rnbqkbnr/pppppppp/8/8/8/8/PPPPPPPP/RNBQKBNR w KQkq - 0 1");
            next->take_history(*search);
            auto fresh = std::make_unique<search_t>("rnbqkbnr/pppppppp/8/8/8/8/PPPPPPPP/RNBQKBNR w KQkq - 0 1");
            if (next->quiet_history->continuation_score(winner, context) != retained
                    || fresh->quiet_history->continuation_score(winner, context) != 0) {
                return false;
            }
        }
    }
    return true;
}

bool long_special_contexts() {
    struct fixture_t { const char * fen; const char * first; const char * reply; const char * quiet; };
    const fixture_t fixtures[] = {
        {"r3k2r/8/8/8/8/8/8/R3K2R w KQkq - 0 1", "e1g1", "e8c8", "a1a2"},
        {"r3k2r/8/8/8/8/8/8/R3K2R b KQkq - 0 1", "e8g8", "e1c1", "a8a7"},
        {"7k/P7/8/8/8/8/8/4K3 w - - 0 1", "a7a8q", "h8h7", "e1e2"},
        {"7k/8/8/3pP3/8/8/8/4K3 w - d6 0 1", "e5d6", "h8h7", "e1e2"},
        {"7k/8/8/8/3Pp3/8/8/4K3 b - d3 0 1", "e4d3", "e1f1", "h8h7"},
        {"7k/8/3p4/4P3/8/8/8/4K3 w - - 0 1", "e5d6", "h8h7", "e1e2"}
    };
    for (const auto & fixture : fixtures) {
        for (int seed : {-quiet_history_t::limit, 0, quiet_history_t::limit}) {
            auto search = std::make_unique<search_t>(fixture.fen);
            play(*search, fixture.first);
            const auto context = (search->stack - 1)->current_move;
            play(*search, fixture.reply);
            if (search->long_continuation_contexts()[0] != &(search->stack - 2)->current_move
                    || search->long_continuation_contexts()[1]) {
                return false;
            }
            move_t winner;
            winner.set(&search->brd, fixture.quiet);
            auto alias = winner;
            alias.ssq = (winner.ssq + 1) % 64;
            search->stack->searched[0] = alias;
            search->quiet_history->update_continuation(winner, context, seed);
            search->update_quiet_cutoff(&winner, 1, 1);
            const int expected = seed + 32 - seed * 32 / quiet_history_t::limit;
            if (search->quiet_history->continuation_score(winner, context) != expected) {
                return false;
            }
        }
    }
    return true;
}

bool long_helper_isolation() {
    for (int mode = 0; mode < 4; ++mode) {
        trans_table::clear();
        auto search = std::make_unique<cutoff_search_t>(1, "rnbqkbnr/pppppppp/8/8/8/8/PPPPPPPP/RNBQKBNR w KQkq - 0 1");
        for (const char * text : {"g1f3", "g8f6", "b1c3", "b8c6"}) {
            play(*search, text);
        }
        search->stack->in_check = search->brd.in_check();
        search->iid_active = mode == 0;
        search->verification_mode = mode == 1 ? verification_mode_t::null_move
            : mode == 2 ? verification_mode_t::excluded_move : verification_mode_t::none;
        if (mode == 3) {
            search->stop_all = true;
        }
        search->search_t::pvs(0, 1, 1);
        for (const auto & context : {(search->stack - 2)->current_move, (search->stack - 4)->current_move}) {
            for (int piece = WPAWN; piece <= BKING; ++piece) {
                for (int square = 0; square < 64; ++square) {
                    if (search->quiet_history->continuation[context.piece][context.tsq][piece][square]) {
                        return false;
                    }
                }
            }
        }
#ifdef MAXIMA_NNUE_SEARCH_AUDIT
        if (search->nnue_audit.counters.long_history_rewards[0]
                || search->nnue_audit.counters.long_history_penalties[0]) {
            return false;
        }
#endif
    }
    auto losers = std::make_unique<w17_search_t>("rnbqkbnr/pppppppp/8/8/8/8/PPPPPPPP/RNBQKBNR w KQkq - 0 1");
    return !losers->long_continuation_enabled && !losers->quiet_history;
}

// A caller can query a TT/killer move before the picker assigns any score.
// Exercise all independent policies and special-move history classifications.
bool shared_history_access() {
    auto search = std::make_unique<search_t>("rnbqkbnr/pppppppp/8/8/8/8/PPPPPPPP/RNBQKBNR w KQkq - 0 1");
    for (const char * text : {"g1f3", "g8f6", "b1c3", "b8c6"}) {
        play(*search, text);
    }
    std::array<move_t, 6> moves{};
    moves[0].set(WKNIGHT, f3, g5);
    moves[1].set(WKNIGHT, f3, e5, BPAWN);
    moves[2].set(WPAWN, a7, a8, EMPTY, WQUEEN);
    moves[3].set(WPAWN, a7, b8, BROOK, WKNIGHT);
    moves[4].set(WPAWN, e5, d6, BPAWN);
    moves[4].en_passant = true;
    moves[5].set(WKING, e1, g1);
    moves[5].castle = 1;
    const auto pawn_bucket = search->brd.stack->pawn_key % quiet_history_t::pawn_buckets;
    for (const auto & move : moves) {
        search->quiet_history->main[WHITE][move.ssq][move.tsq] = 11;
        search->quiet_history->pawn[pawn_bucket][move.piece][move.tsq] = -13;
        for (int distance : {1, 2, 4}) {
            const auto & previous = (search->stack - distance)->current_move;
            search->quiet_history->continuation[previous.piece][previous.tsq][move.piece][move.tsq]
                = distance == 1 ? 17 : distance == 2 ? 19 : 23;
        }
        search->capture_history->entries[move.piece][move.tsq][capture_history_t::victim(move)] = -29;
    }
    const auto quiet_before = std::make_unique<quiet_history_t>(*search->quiet_history);
    const auto capture_before = *search->capture_history;
    const auto fen = search->brd.to_string();
    const auto key = search->brd.stack->tt_key;
    for (int mask = 0; mask < 32; ++mask) {
        search->quiet_history_policy = {bool(mask & 1), bool(mask & 2), bool(mask & 4)};
        search->long_continuation_enabled = mask & 8;
        search->capture_history_enabled = mask & 16;
        for (auto & move : moves) {
            const int expected = move.capture || move.promotion ? ((mask & 16) ? -29 : 0)
                : ((mask & 1) ? 11 : 0) + ((mask & 2) ? 17 : 0)
                    - ((mask & 4) ? 13 : 0) + ((mask & 8) ? 42 : 0);
            for (int picker_score : {-1000000, 0, 1000000}) {
                move.score = picker_score;
                if (search->move_history_score(move) != expected || move.score != picker_score) {
                    std::cerr << "shared history classification/policy failed\n";
                    return false;
                }
            }
        }
    }
    return std::memcmp(quiet_before.get(), search->quiet_history.get(), sizeof(quiet_history_t)) == 0
        && std::memcmp(&capture_before, search->capture_history.get(), sizeof(capture_before)) == 0
        && search->brd.to_string() == fen && search->brd.stack->tt_key == key;
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
    next->take_history(*old);
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
    return cutoff_updates() && killer_alias() && context_and_bounds() && component_controls() && long_contexts() && long_controls_and_learning() && long_helper_isolation() && long_special_contexts() && shared_history_access() && history_ownership() ? EXIT_SUCCESS : EXIT_FAILURE;
}
