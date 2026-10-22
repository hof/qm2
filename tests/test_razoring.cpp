#include "engine.h"
#include <algorithm>
#include <cstring>

namespace {
constexpr auto start = "rnbqkbnr/pppppppp/8/8/8/8/PPPPPPPP/RNBQKBNR w KQkq - 0 1";
bool check(bool ok, const char * message) {
    if (!ok) {
        std::cerr << message << '\n';
    }
    return ok;
}
class probe_t : public search_t {
public:
    int children = 0;
    explicit probe_t(bool black = false) : search_t(start) {
        move_t move{};
        move.set(&brd, "e2e4");
        forward(&move, brd.gives_check(&move));
        if (!black) {
            move.set(&brd, "e7e5");
            forward(&move, brd.gives_check(&move));
        }
        beta_pruning = null_enabled = ffp_enabled = lmr_enabled = false;
        main_see_enabled = iir_enabled = iid_enabled = correction_history_enabled = false;
        tablebase_limit = draw_contempt = 0;
        evaluate(this);
        nodes = 0; // fixture moves are setup, not search
    }
    int pvs(int alpha, int beta, int depth, bool cut_node = false) override {
        if (brd.ply == entry_ply) {
            return search_t::pvs(alpha, beta, depth, cut_node);
        }
        ++children;
        stack->pv_count = 0;
        stack->tt_clocks = {0, 101};
        return 0;
    }
    int entry_ply = 0;
    int run(int alpha, int beta, int depth) {
        entry_ply = brd.ply;
        return pvs(alpha, beta, depth);
    }
};
void seed_q(search_t & s, int value, tt_mode_t mode = tt_mode_t::quiescence_entry,
        clock_range_t clocks = {0, 101}) {
    trans_table::clear();
    trans_table::store(s.brd.stack->tt_key, 0, s.brd.ply, 1, value, 0,
        score::EXACT, s.tt_context(), clocks, mode);
}
bool test_policy() {
    bool ok = true;
    for (bool black : {false, true}) {
        for (int depth = 0; depth <= MAX_PLY; ++depth) {
            for (int offset : {-1, 0, 1}) {
                auto s = std::make_unique<probe_t>(black);
                seed_q(*s, 100);
                int value = 123;
                const bool wanted = depth >= 1 && depth <= 3 && offset < 0;
                ok &= s->razor_node(100, 101, depth, 100 - 300 * depth + offset, value) == wanted;
                ok &= !wanted || (value == 100 && s->stack->tt_clocks.max == 98);
#ifdef MAXIMA_NNUE_SEARCH_AUDIT
                ok &= s->nnue_audit.counters.razoring_attempts == unsigned(wanted)
                    && s->nnue_audit.counters.razoring_cutoffs == unsigned(wanted);
#endif
            }
        }
        for (int guard = 0; guard < 13; ++guard) {
            auto s = std::make_unique<probe_t>(black);
            int alpha = 100, beta = 101, eval = -1000, value = 123;
            if (guard == 0) {
                s->razoring_enabled = false;
            } else if (guard == 1) {
                beta = 102;
            } else if (guard == 2) {
                s->stack->in_check = true;
            } else if (guard == 3) {
                s->stack->metadata.endgame = true;
            } else if (guard == 4) {
                s->iid_active = true;
            } else if (guard == 5) {
                s->verification_mode = verification_mode_t::null_move;
            } else if (guard == 6) {
                s->verification_mode = verification_mode_t::excluded_move;
            } else if (guard == 7) {
                s->stack->excluded_move = 1;
            } else if (guard == 8) {
                s->brd.stack->fifty_count = 99;
            } else if (guard == 9) {
                alpha = score::TABLEBASE - MAX_PLY;
                beta = alpha + 1;
            } else if (guard == 10) {
                beta = -score::TABLEBASE + MAX_PLY;
                alpha = beta - 1;
            } else if (guard == 11) {
                eval = -score::TABLEBASE + MAX_PLY;
            } else {
                eval = score::TABLEBASE - MAX_PLY;
            }
            seed_q(*s, 100);
            ok &= !s->razor_node(alpha, beta, 1, eval, value) && s->nodes == 0 && value == 123;
        }
    }
    auto root = std::make_unique<search_t>(start);
    int value = 123;
    ok &= !root->razor_node(100, 101, 1, -1000, value);
    auto pawns = std::make_unique<search_t>("4k3/pp6/8/8/8/8/PP6/4K3 b - - 0 1");
    move_t last{};
    last.set(&pawns->brd, "e8f8");
    pawns->forward(&last, false);
    pawns->stack->metadata.endgame = false;
    ok &= !pawns->razor_node(100, 101, 1, -1000, value);
    return check(ok, "razoring depth, strict margin, window or protection failed");
}
bool test_frame_and_q_contract() {
    bool ok = true;
    for (bool black : {false, true}) {
        for (int score_delta : {-1, 0, 1, 30000}) {
            auto s = std::make_unique<probe_t>(black);
            const int qvalue = score_delta == 30000 ? -score::TABLEBASE : 100 + score_delta;
            seed_q(*s, qvalue, tt_mode_t::quiescence_entry, {0, 80});
            s->stack->tt_clocks = {0, 90};
            s->stack->improving = true;
            s->stack->tt_move.set(&s->brd, black ? "g8f6" : "g1f3");
            s->stack->best_move = s->stack->tt_move;
            s->stack->pv_count = 1;
            s->stack->pv_moves[0] = s->stack->tt_move;
            s->stack->move_list.stage = 77;
            const auto frame = *s->stack;
            const auto fen = s->brd.to_string();
            const auto key = s->brd.stack->tt_key;
            const auto quiet = std::make_unique<quiet_history_t>(*s->quiet_history);
            const auto capture = std::make_unique<capture_history_t>(*s->capture_history);
            const auto correction = std::make_unique<correction_history_t>(*s->correction_history);
            int value = 123;
            const bool accepted = s->razor_node(100, 101, 2, -1000, value);
            auto after = *s->stack;
            after.tt_clocks = frame.tt_clocks;
            ok &= accepted == (qvalue <= 100 && score_delta != 30000) && value == qvalue
                && s->nodes == 0 && s->stack->tt_clocks.max == 80
                && std::memcmp(&after, &frame, sizeof(frame)) == 0
                && s->brd.to_string() == fen && s->brd.stack->tt_key == key
                && std::memcmp(quiet.get(), s->quiet_history.get(), sizeof(quiet_history_t)) == 0
                && std::memcmp(capture.get(), s->capture_history.get(), sizeof(capture_history_t)) == 0
                && std::memcmp(correction.get(), s->correction_history.get(), sizeof(correction_history_t)) == 0;
            int score = 0, hint = 0, flag = 0;
            ok &= !trans_table::retrieve(key, s->brd.ply, 2, score, hint, flag, s->tt_context());
        }
    }
    for (auto mode : {tt_mode_t::main_search, tt_mode_t::quiescence_tactical,
            tt_mode_t::quiescence_entry}) {
        auto s = std::make_unique<probe_t>();
        seed_q(*s, 2001, mode);
        int value = 123;
        const bool accepted = s->razor_node(2000, 2001, 1, -1000, value);
        ok &= accepted == (mode != tt_mode_t::quiescence_entry)
            && (mode != tt_mode_t::quiescence_entry || value == 2001);
    }
    return check(ok, "razoring q bound, decisive score, frame/history restoration or clock merge failed");
}
bool test_pvs() {
    bool ok = true;
    int cases = 0;
    for (bool black : {false, true}) {
        for (bool enabled : {false, true}) {
            for (int depth : {1, 2, 3, 4}) {
                for (bool pv : {false, true}) {
                    for (int delta : {-1, 0, 1}) {
                        auto s = std::make_unique<probe_t>(black);
                        s->razoring_enabled = enabled;
                        const int alpha = std::max(2000, evaluate(s.get()) + 300 * depth + 1);
                        seed_q(*s, alpha + delta);
                        const auto fen = s->brd.to_string();
                        const auto key = s->brd.stack->tt_key;
                        auto * frame = s->stack;
                        const int value = s->run(alpha, alpha + (pv ? 2 : 1), depth);
                        const bool accepted = enabled && !pv && depth <= 3 && delta <= 0;
                        const bool good = value == (accepted ? alpha + delta : 0)
                            && (s->children == 0) == accepted && s->stack == frame
                            && s->brd.to_string() == fen && s->brd.stack->tt_key == key;
                        if (!good) {
                            std::cerr << "PVS " << black << '/' << enabled << '/' << depth << '/'
                                << pv << '/' << delta << " value " << value << " children " << s->children << '\n';
                        }
                        ok &= good;
                        int score = 0, hint = 0, flag = 0;
                        const bool hit = trans_table::retrieve(key, s->brd.ply, depth,
                            score, hint, flag, s->tt_context());
                        ok &= hit != accepted && (!hit || (score == 0 && flag == score::UPPERBOUND));
                        ++cases;
                    }
                }
            }
        }
    }
    std::cout << cases << " controlled razoring PVS cases\n";
    return check(ok, "razoring PVS fallthrough, bounds, main TT isolation or board restoration failed");
}
bool test_real_q_and_cancel() {
    bool ok = true;
    for (bool black : {false, true}) {
        for (bool use_cache : {false, true}) {
            for (int limit : {0, 1}) {
                auto s = std::make_unique<probe_t>(black);
                s->q_tt_enabled = use_cache;
                const int alpha = std::max(2000, evaluate(s.get()) + 1000);
                const auto fen = s->brd.to_string();
                const auto key = s->brd.stack->tt_key;
                // A spent budget interrupts at entry; no budget lets razoring cut.
                s->game.max_nodes = limit;
                s->nodes = limit;
                trans_table::clear();
                const int result = s->run(alpha, alpha + 1, 2);
                const bool before_ok = ok;
                ok &= (limit ? result == alpha && s->stop_all : result <= alpha && !s->stop_all)
                    && s->children == 0 && s->brd.to_string() == fen && s->brd.stack->tt_key == key;
                if (before_ok && !ok) {
                    std::cerr << "real q " << black << '/' << use_cache << '/' << limit
                        << " result " << result << " alpha " << alpha << " nodes " << s->nodes
                        << " stop " << s->stop_all << " children " << s->children << '\n';
                }
                int score = 0, hint = 0, flag = 0;
                ok &= !trans_table::retrieve(key, s->brd.ply, 2, score, hint, flag, s->tt_context());
#ifdef MAXIMA_NNUE_SEARCH_AUDIT
                const auto & c = s->nnue_audit.counters;
                ok &= c.razoring_cutoffs == unsigned(!limit) && c.razoring_cancelled == 0;
#endif
            }
        }
    }
    // Cancel after qsearch has made a real capture, then verify complete undo.
    for (int limit : {1}) {
        auto s = std::make_unique<search_t>("7k/pp6/8/3q4/8/8/PP6/K2R4 b - - 0 1");
        move_t last{};
        last.set(&s->brd, "h8g8");
        s->forward(&last, false);
        s->nodes = 0; // the fixture move is setup, not search
        s->tablebase_limit = s->draw_contempt = 0;
        s->correction_history_enabled = s->q_tt_enabled = false;
        evaluate(s.get());
        const auto fen = s->brd.to_string();
        const auto key = s->brd.stack->tt_key;
        const auto before = *s->stack;
        s->game.max_nodes = limit;
        int value = 0;
        const bool accepted = s->razor_node(2000, 2001, 2, -1000, value);
        auto after = *s->stack;
        after.tt_clocks = before.tt_clocks;
        if (accepted || !s->stop_all || value != 2000 || s->nodes != U64(limit)) {
            std::cerr << "capture cancel " << limit << " accepted " << accepted
                << " stop " << s->stop_all << " value " << value << " nodes " << s->nodes << '\n';
        }
        ok &= !accepted && s->stop_all && value == 2000 && s->nodes == U64(limit)
            && s->brd.to_string() == fen && s->brd.stack->tt_key == key
            && std::memcmp(&after, &before, sizeof(before)) == 0;
    }
    // A recoverable queen capture must reject the low-static-score hypothesis.
    auto tactical = std::make_unique<search_t>("7k/pp6/8/3q4/8/8/PP6/K2R4 b - - 0 1");
    move_t move{};
    move.set(&tactical->brd, "h8g8");
    tactical->forward(&move, false);
    tactical->nodes = 0; // the fixture move is setup, not search
    tactical->tablebase_limit = tactical->draw_contempt = 0;
    tactical->correction_history_enabled = false;
    const int tactical_alpha = evaluate(tactical.get()) + 100;
    trans_table::clear();
    const auto before = *tactical->stack;
    int result = 0;
    const bool accepted = tactical->razor_node(tactical_alpha, tactical_alpha + 1, 1,
        tactical_alpha - 1000, result);
    if (accepted || result <= tactical_alpha || tactical->nodes == 0) {
        std::cerr << "tactical accepted " << accepted << " result " << result
            << " nodes " << tactical->nodes << '\n';
    }
    ok &= !accepted && result > tactical_alpha
        && tactical->nodes > 0 && tactical->brd.ply == 1
        && tactical->stack->eval_result == before.eval_result;
    return check(ok, "razoring real tactical verification, cancellation or restoration failed");
}
bool test_worker_and_terminals() {
    bool ok = options::get_value("Razoring") == 1
        && std::string(options::get_uci_option("Razoring")) == "type check default true";
    for (bool enabled : {false, true}) {
        options::get_option("Razoring")->value = enabled;
        auto s = std::make_unique<probe_t>();
        options::get_option("Razoring")->value = !enabled;
        ok &= s->razoring_enabled == enabled;
        auto royal = std::make_unique<search_t>(start, nullptr, nullptr, std::vector<U64>{}, 17);
        ok &= !royal->razoring_enabled;
    }
    options::get_option("Razoring")->value = 1;
#ifdef MAXIMA_NNUE_SEARCH_AUDIT
    for (int horizon : {1, 2}) {
        options::get_option("NNUEAuditHorizon")->value = horizon;
        auto s = std::make_unique<probe_t>();
        ok &= !s->razoring_enabled;
    }
    options::get_option("NNUEAuditHorizon")->value = 0;
#endif
    for (const char * text : {"g5g6", "g5g7"}) {
        auto s = std::make_unique<search_t>("7k/8/5K2/6Q1/8/8/8/8 w - - 0 1");
        move_t move{};
        move.set(&s->brd, text);
        s->forward(&move, s->brd.gives_check(&move));
        s->tablebase_limit = s->draw_contempt = 0;
        trans_table::clear();
        ok &= s->search_t::pvs(2000, 2001, 2) == (std::string(text) == "g5g6" ? 0 : -score::MATE + 1);
    }
    // Ordinary draw adjudication must precede a stale qsearch score.
    auto draw = std::make_unique<probe_t>();
    draw->brd.stack->fifty_count = 100;
    seed_q(*draw, 1999);
    ok &= draw->run(2000, 2001, 2) == 0 && draw->children == 0;
    return check(ok, "razoring worker, audit/variant isolation or terminal/draw precedence failed");
}
}
int main() {
    magic::init();
    uci::silent(true);
    trans_table::set_size(4);
    options::get_option("OwnBook")->value = 0;
    bool ok = test_policy();
    ok &= test_frame_and_q_contract();
    ok &= test_pvs();
    ok &= test_real_q_and_cancel();
    ok &= test_worker_and_terminals();
    return ok ? 0 : 1;
}
