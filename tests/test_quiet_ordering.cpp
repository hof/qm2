#include "engine.h"
#include "w17/w17_search.h"
#include <cstring>
#include <limits>
#include <set>

namespace {
bool check(bool value, const char * message) {
    if (!value) {
        std::cerr << message << '\n';
    }
    return value;
}

// Independent per-square oracle, rather than the picker's batch attack unions.
int threat_score(search_t & search, const move_t & move) {
    if (move.castle) {
        return 0;
    }
    auto & board = search.brd;
    const bool them = !board.stack->wtm;
    const int type = (move.piece - 1) % 6 + 1;
    U64 lesser = 0;
    if (type == WKNIGHT || type == WBISHOP || type == WROOK || type == WQUEEN) {
        lesser = board.bb[PAWN[them]];
    }
    if (type == WROOK || type == WQUEEN) {
        lesser |= board.bb[KNIGHT[them]] | board.bb[BISHOP[them]];
    }
    if (type == WQUEEN) {
        lesser |= board.bb[ROOK[them]];
    }
    const int weight = type == WQUEEN ? 12000 : type == WROOK ? 8000 : 4000;
    return weight * (int(bool(board.attacks_to(move.ssq) & lesser))
        - int(bool(board.attacks_to(move.tsq) & lesser)));
}

bool scoring_contract() {
    bool ok = true;
    int escapes = 0, entries = 0, safe_checks = 0, unsafe_checks = 0, castles = 0;
    for (const char * fen : {
            "r3k2r/p1ppqpb1/bn2pnp1/3PN3/1p2P3/2N2Q1p/PPPBBPPP/R3K2R w KQkq - 0 1",
            "r3k2r/p1ppqpb1/bn2pnp1/3PN3/1p2P3/2N2Q1p/PPPBBPPP/R3K2R b KQkq - 0 1",
            "7k/8/8/8/3p4/2N5/8/7K w - - 0 1",
            "7k/8/8/5n2/3R4/8/8/7K w - - 0 1",
            "7k/8/8/8/r2Q4/8/8/7K w - - 0 1",
            "7k/8/8/3P4/2n5/8/8/7K b - - 0 1",
            "7k/8/8/8/8/8/8/R6K w - - 0 1",
            "7k/1b6/8/8/8/8/8/R6K w - - 0 1",
            "k7/8/8/8/8/8/B7/R6K w - - 0 1",
            "5k2/8/8/8/8/8/8/4K2R w K - 0 1",
            "4r2k/8/8/8/8/8/4R3/4K3 w - - 0 1",
            "7k/P7/8/8/8/8/8/7K w - - 0 1",
            "7k/8/8/r4pPK/8/8/8/8 w - f6 0 1"}) {
        for (int depth : {-1, 0, 3}) {
            std::set<uint16_t> control;
            for (int mask = 0; mask < 4; ++mask) {
                auto search = std::make_unique<search_t>(fen);
                search->stack->in_check = search->brd.in_check();
                search->threat_ordering_enabled = mask & 1;
                search->check_ordering_enabled = mask & 2;
                // Nonzero retained history verifies addition and independence.
                for (auto & from : search->quiet_history->main[search->brd.stack->wtm]) {
                    std::fill(std::begin(from), std::end(from), -17);
                }
                auto before = std::make_unique<quiet_history_t>(*search->quiet_history);
                const auto fen_before = search->brd.to_string();
                const auto key = search->brd.stack->tt_key;
                std::set<uint16_t> seen;
                int last_quiet = std::numeric_limits<int>::max();
                bool losing_noisy = false;
                for (auto * move = move::first(search.get(), depth); move;
                        move = move::next(search.get(), depth)) {
                    ok &= check(seen.insert(move->to_compact()).second, "duplicate candidate");
                    if (move->capture || move->promotion) {
                        if (!search->brd.see(move, 0)) {
                            losing_noisy = true;
                        }
                        continue;
                    }
                    const int history = search->move_history_score(*move);
                    const int threat = threat_score(*search, *move);
                    const bool checks = !move->castle && search->brd.gives_check(move);
                    const bool safe = checks && search->brd.see(move, 0);
                    const int expected = history + ((mask & 1) ? threat : 0)
                        + ((mask & 2) && safe ? 10000 : 0);
                    ok &= check(move->score == expected, "context score differs from oracle");
                    ok &= check(move->score <= last_quiet, "quiet scores must descend");
                    ok &= check(move->score <= 0 || !losing_noisy, "positive quiet must precede losing noisy");
                    last_quiet = move->score;
                    if (mask == 3) {
                        escapes += threat > 0;
                        entries += threat < 0;
                        safe_checks += safe;
                        unsafe_checks += checks && !safe;
                        castles += bool(move->castle);
                    }
                }
                if (mask == 0) {
                    control = seen;
                }
                ok &= check(seen == control, "ordering options must preserve candidate domain");
                ok &= check(search->brd.to_string() == fen_before && search->brd.stack->tt_key == key
                    && std::memcmp(before.get(), search->quiet_history.get(), sizeof(quiet_history_t)) == 0,
                    "ordering must preserve board and learned tables");
#ifdef MAXIMA_NNUE_SEARCH_AUDIT
                const auto & counts = search->nnue_audit.counters;
                ok &= check((mask & 1) || counts.threat_ordering_scores == 0, "disabled threat counter");
                ok &= check((mask & 2) || counts.check_ordering_candidates == 0, "disabled check counter");
#endif
            }
        }
    }
    return check(ok && escapes && entries && safe_checks && unsafe_checks && castles,
        "quiet ordering coverage/scoring failed");
}

bool worker_policy() {
    bool ok = true;
    for (int mask = 0; mask < 4; ++mask) {
        options::get_option("ThreatOrdering")->value = mask & 1;
        options::get_option("CheckOrdering")->value = bool(mask & 2);
        auto search = std::make_unique<search_t>("rnbqkbnr/pppppppp/8/8/8/8/PPPPPPPP/RNBQKBNR w KQkq - 0 1");
        options::get_option("ThreatOrdering")->value = !(mask & 1);
        options::get_option("CheckOrdering")->value = !(mask & 2);
        ok &= check(search->threat_ordering_enabled == bool(mask & 1)
            && search->check_ordering_enabled == bool(mask & 2), "worker policy must be immutable");
    }
    options::get_option("ThreatOrdering")->value = 1;
    options::get_option("CheckOrdering")->value = 1;
    auto losers = std::make_unique<w17_search_t>("rnbqkbnr/pppppppp/8/8/8/8/PPPPPPPP/RNBQKBNR w KQkq - 0 1");
    return check(ok && !losers->threat_ordering_enabled && !losers->check_ordering_enabled,
        "royal losers must retain its own ordering");
}
}

int main() {
    magic::init();
    return scoring_contract() && worker_policy() ? EXIT_SUCCESS : EXIT_FAILURE;
}
