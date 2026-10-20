#include "engine.h"
#include "movepicker.h"
#include "w17/w17_search.h"
#include <cstring>
#include <limits>
#include <set>
#include <vector>

namespace {
constexpr auto position = "r3k2r/p1ppqpb1/bn2pnp1/3PN3/1p2P3/2N2Q1p/PPPBBPPP/R3K2R w KQkq - 0 1";

bool check(bool result, const char * message) {
    if (!result) {
        std::cerr << message << '\n';
    }
    return result;
}

move_t parsed(search_t & search, const char * text) {
    move_t move{};
    move.set(&search.brd, text);
    return move;
}

bool table_contract() {
    capture_history_t history;
    move_t move{}, shared{}, other{};
    move.set(WKNIGHT, f3, e5, BPAWN);
    shared.set(WKNIGHT, c4, e5, BPAWN);
    history.update(move, 32);
    history.update(move, 1024);
    bool gravity = history.score(move) == 32 + 1024 - 32 * 1024 / capture_history_t::limit;
    history.update(move, -capture_history_t::limit);
    gravity &= history.score(move) == -capture_history_t::limit;
    history = {};
    history.update(move, 32);
    bool ok = check(gravity && history.score(shared) == 32, "sources must share a capture-history context");
    other.set(WBISHOP, c3, e5, BPAWN);
    ok &= check(history.score(other) == 0, "mover types must remain independent");
    other.set(BKNIGHT, f7, e5, WPAWN);
    ok &= check(history.score(other) == 0, "mover colors must remain independent");
    other.set(WKNIGHT, f3, e5, BROOK);
    ok &= check(history.score(other) == 0, "victims must remain independent");
    other.set(WKNIGHT, f3, g5, BPAWN);
    ok &= check(history.score(other) == 0, "destinations must remain independent");
    for (int bonus : {32, 2048, capture_history_t::limit, -32, -2048,
            -capture_history_t::limit, std::numeric_limits<int>::min(), std::numeric_limits<int>::max()}) {
        for (int i = 0; i < 1000; ++i) {
            history.update(move, bonus);
            ok &= history.score(move) >= -capture_history_t::limit && history.score(move) <= capture_history_t::limit;
        }
    }
    return check(ok, "capture gravity must remain bounded");
}

// Isolate parent learning from deeper search and evaluation. All child moves
// still pass real make/legal/undo before this controlled leaf contract runs.
class cutoff_probe_t : public search_t {
public:
    int calls = 0;
    int noisy_calls = 0;
    int before = 1;
    bool quiet_winner = false;
    bool cancel = false;
    bool fail_low = false;
    std::vector<move_t> children;
    explicit cutoff_probe_t(const char * fen = position) : search_t(fen) {
        null_enabled = beta_pruning = lmr_enabled = ffp_enabled = iid_enabled = false;
        upcoming_repetition_enabled = false;
        tablebase_limit = 0;
    }
    int pvs(int, int, int, bool = false) override {
        if (!brd.legal()) {
            std::abort();
        }
        const auto & move = (stack - 1)->current_move;
        children.push_back(move);
        ++calls;
        const bool noisy = move.capture || move.promotion;
        const bool wins = quiet_winner ? !noisy && !move.castle : noisy && noisy_calls == before;
        noisy_calls += noisy;
        if (wins && cancel) {
            stop_all = true;
        }
        return wins && !fail_low ? -1 : 0;
    }
};

bool cutoff_contract() {
    bool ok = true;
    for (int seed : {0, 1024, -1024, capture_history_t::limit, -capture_history_t::limit}) {
        for (int depth : {1, 4, 20}) {
            for (bool quiet : {false, true}) {
                trans_table::clear();
                auto search = std::make_unique<cutoff_probe_t>();
                search->quiet_winner = quiet;
                for (auto & piece : search->capture_history->entries) {
                    for (auto & square : piece) {
                        std::fill(std::begin(square), std::end(square), seed);
                    }
                }
                const auto fen = search->brd.to_string();
                const auto key = search->brd.stack->tt_key;
                const int value = search->search_t::pvs(0, 1, depth);
                const auto & winner = search->stack->best_move;
                capture_history_t expected;
                for (auto & piece : expected.entries) {
                    for (auto & square : piece) {
                        std::fill(std::begin(square), std::end(square), seed);
                    }
                }
                const int d = std::clamp(depth, 1, 8);
                const int bonus = 32 * d * d;
                for (size_t i = 0; i + 1 < search->children.size(); ++i) {
                    const auto & failed = search->children[i];
                    if ((failed.capture || failed.promotion)
                            && (quiet || !capture_history_t::same_entry(failed, winner))) {
                        expected.update(failed, -bonus);
                    }
                }
                if (!quiet) {
                    expected.update(winner, bonus);
                }
#ifdef MAXIMA_NNUE_SEARCH_AUDIT
                const auto & counts = search->nnue_audit.counters;
                ok &= check(counts.capture_history_rewards == !quiet
                    && counts.capture_history_cutoffs == !quiet
                    && (quiet || counts.capture_cutoff_rank_sum == search->children.size())
                    && counts.capture_history_scores > 0,
                    "audit must count capture scores, completed cutoffs and searched cutoff rank");
#endif
                ok &= check(value == 1 && !search->stop_all && search->calls > 1
                    && bool(winner.capture || winner.promotion) != quiet
                    && search->brd.to_string() == fen && search->brd.stack->tt_key == key
                    && search->stack == search->root_stack
                    && std::memcmp(&expected, search->capture_history.get(), sizeof(expected)) == 0,
                    "completed normal cutoffs must reward winner and penalize searched noisy alternatives");
            }
        }
    }
    for (int mode = 0; mode < 5; ++mode) {
        trans_table::clear();
        auto search = std::make_unique<cutoff_probe_t>();
        search->cancel = mode == 0;
        search->fail_low = mode == 1;
        if (mode == 2) {
            search->verification_mode = verification_mode_t::null_move;
        }
        if (mode == 3) {
            search->verification_mode = verification_mode_t::excluded_move;
            search->stack->excluded_move = parsed(*search, "e5d7").to_compact();
        }
        if (mode == 4) {
            search->iid_active = true;
        }
        search->search_t::pvs(0, 1, 1);
        capture_history_t empty;
        ok &= check(std::memcmp(&empty, search->capture_history.get(), sizeof(empty)) == 0,
            "cancellation, fail-low and verification must not train capture history");
    }
    return ok;
}

bool nonlearning_paths() {
    bool ok = true;
    trans_table::clear();
    auto search = std::make_unique<cutoff_probe_t>();
    const auto hint = parsed(*search, "e5d7");
    trans_table::store(search->brd.stack->tt_key, search->brd.root_ply, search->brd.ply,
        8, 10, hint.to_compact(), score::LOWERBOUND, search->tt_context(), {0, 101});
    const int value = search->search_t::pvs(0, 1, 1);
    capture_history_t empty;
    ok &= check(value >= 1 && search->calls == 0
        && std::memcmp(&empty, search->capture_history.get(), sizeof(empty)) == 0,
        "TT cutoffs must not train unsearched captures");
    trans_table::clear();
    auto quiet = std::make_unique<search_t>(position);
    quiet->tablebase_limit = 0;
    quiet->qsearch(-score::INF, score::INF, 0);
    ok &= check(std::memcmp(&empty, quiet->capture_history.get(), sizeof(empty)) == 0,
        "qsearch may order by history but must not train it");
    auto pinned = std::make_unique<cutoff_probe_t>("4r2k/8/8/8/8/8/p3R2p/4K3 w - - 0 1");
    pinned->quiet_winner = true;
    auto illegal = parsed(*pinned, "e2a2");
    ok &= check(pinned->brd.valid(&illegal) && !pinned->brd.legal(&illegal),
        "pinned-capture fixture must be pseudo-legal and illegal");
    trans_table::clear();
    pinned->search_t::pvs(0, 1, 1);
    ok &= check(pinned->capture_history->score(illegal) == 0,
        "rejected illegal captures must not be penalized");
    return ok;
}

bool special_moves() {
    bool ok = true;
    for (auto fen : {"7k/8/8/3pP3/8/8/8/K7 w - d6 0 1",
            "7k/8/8/8/3Pp3/8/8/K7 b - d3 0 1",
            "1r5k/P7/8/8/8/8/8/K7 w - - 0 1",
            "7k/8/8/8/8/8/p7/1R5K b - - 0 1"}) {
        trans_table::clear();
        auto search = std::make_unique<cutoff_probe_t>(fen);
        search->before = 0;
        const int value = search->search_t::pvs(0, 1, 1);
        const auto & winner = search->stack->best_move;
        ok &= check(value == 1 && (winner.capture || winner.promotion)
            && search->capture_history->score(winner) == 32,
            "EP and promotion cutoff must train their recorded mover/victim context");
    }
    auto search = std::make_unique<search_t>("1r5k/P7/8/8/8/8/8/K7 w - - 0 1");
    auto queen = parsed(*search, "a7a8q");
    auto knight = parsed(*search, "a7a8n");
    auto capture = parsed(*search, "a7b8q");
    ok &= check(capture_history_t::victim(queen) == EMPTY
        && capture_history_t::victim(capture) == WROOK
        && capture_history_t::same_entry(queen, knight), "promotion contexts must distinguish victims");
    search->stack->searched[0] = knight;
    search->update_capture_cutoff(queen, 1, 1);
    ok &= check(search->capture_history->score(queen) == 32, "promotion alias must not penalize winner's slot");
    return ok;
}

std::vector<uint16_t> picked(search_t & search, int depth) {
    std::vector<uint16_t> result;
    for (auto * move = move::first(&search, depth); move; move = move::next(&search, depth)) {
        result.push_back(move->to_compact());
    }
    return result;
}

bool ordering_and_controls() {
    bool ok = true;
    for (int depth : {1, 0, -1}) {
        auto search = std::make_unique<search_t>("7k/3p1p2/8/4N3/8/8/8/K7 w - - 0 1");
        auto first = parsed(*search, "e5d7");
        auto second = parsed(*search, "e5f7");
        search->capture_history->update(first, -capture_history_t::limit);
        search->capture_history->update(second, capture_history_t::limit);
        std::set<uint16_t> seen;
        bool bad_seen = false;
        for (auto * move = move::first(search.get(), depth); move; move = move::next(search.get(), depth)) {
            ok &= check(seen.insert(move->to_compact()).second, "picker must not duplicate moves");
            if (move->capture || move->promotion) {
                const bool good = search->brd.see(move, 0);
                ok &= check(!good || !bad_seen, "history must never promote negative SEE across good-noisy stage");
                bad_seen |= !good;
            }
        }
        auto enabled = picked(*search, depth);
        ok &= check(std::find(enabled.begin(), enabled.end(), second.to_compact())
            < std::find(enabled.begin(), enabled.end(), first.to_compact()), "learned preference must change noisy order");
        search->capture_history_enabled = false;
        const auto disabled = picked(*search, depth);
        auto fresh = std::make_unique<search_t>("7k/3p1p2/8/4N3/8/8/8/K7 w - - 0 1");
        fresh->capture_history_enabled = false;
        ok &= check(disabled == picked(*fresh, depth), "disabled picker must ignore retained capture history");
    }
    for (int seed : {-capture_history_t::limit, capture_history_t::limit}) {
        auto search = std::make_unique<search_t>("3q3k/8/8/3p4/8/8/1p1Q4/7K w - - 0 1");
        for (auto & piece : search->capture_history->entries) {
            for (auto & square : piece) {
                std::fill(std::begin(square), std::end(square), seed);
            }
        }
        bool bad_seen = false, good_seen = false;
        for (auto * move = move::first(search.get(), 1); move; move = move::next(search.get(), 1)) {
            if (move->capture || move->promotion) {
                const bool good = search->brd.see(move, 0);
                ok &= check(!good || !bad_seen, "extreme history must preserve SEE stage separation");
                good_seen |= good;
                bad_seen |= !good;
            }
        }
        ok &= check(good_seen && bad_seen, "stage regression must exercise both SEE classes");
    }
    for (int enabled : {0, 1}) {
        options::get_option("CaptureHistory")->value = enabled;
        auto search = std::make_unique<cutoff_probe_t>();
        options::get_option("CaptureHistory")->value = !enabled;
        ok &= check(search->capture_history_enabled == bool(enabled), "worker must snapshot capture policy");
        auto move = parsed(*search, "e5d7");
        search->capture_history->update(move, 1024);
        ok &= check(search->capture_score(move) == (enabled ? 1024 : 0), "disabled history must skip scoring");
        const auto before = *search->capture_history;
        trans_table::clear();
        search->search_t::pvs(0, 1, 1);
        ok &= check(enabled || std::memcmp(&before, search->capture_history.get(), sizeof(before)) == 0,
            "disabled history must skip learning");
    }
    options::get_option("CaptureHistory")->value = 1;
    auto old = std::make_unique<search_t>(position);
    auto move = parsed(*old, "e5d7");
    old->capture_history->update(move, 1234);
    auto * address = old->capture_history.get();
    auto next = std::make_unique<search_t>(position);
    next->take_history(*old);
    auto fresh = std::make_unique<search_t>(position);
    ok &= check(!old->capture_history && next->capture_history.get() == address
        && next->capture_history->score(move) == 1234 && fresh->capture_history->score(move) == 0,
        "history ownership must transfer intact and new worker must start cold");
    auto losers = std::make_unique<w17_search_t>(position);
    ok &= check(!losers->capture_history && !losers->capture_history_enabled,
        "royal losers must not allocate or use standard capture history");
    return ok;
}
}

int main() {
    magic::init();
    return table_contract() && cutoff_contract() && nonlearning_paths() && special_moves() && ordering_and_controls()
        ? EXIT_SUCCESS : EXIT_FAILURE;
}
