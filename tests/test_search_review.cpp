#include "engine.h"
#include "w17/w17_search.h"
#include <limits>
#include <set>
#include <sstream>

namespace {
struct fixture_t { const char * fen; const char * legal; const char * entry;
    const char * tactical; const char * castles; };
const fixture_t fixtures[] = {
#include "data/search/review.inc"
};
bool check(bool ok, const char * message) {
    if (!ok) {
        std::cerr << message << '\n';
    }
    return ok;
}
std::set<std::string> words(const std::string & text) {
    std::istringstream input(text);
    std::set<std::string> result;
    for (std::string word; input >> word;) result.insert(word);
    return result;
}
bool restored(search_t & search, search_t & original) {
    return check(search.brd.to_string() == original.brd.to_string()
        && search.brd.stack->tt_key == original.brd.stack->tt_key
        && search.stack->tt_history == original.stack->tt_history
        && search.stack->repetition_start == original.stack->repetition_start
        && search.brd.ply == 0 && search.stack == search.root_stack,
        "review search did not restore state");
}

bool test_castling() {
    bool ok = true;
    int probes = 0;
    for (const auto & fixture : fixtures) {
        search_t original(fixture.fen);
        original.stack->in_check = original.brd.in_check();
        original.draw_contempt = original.tablebase_limit = 0;
        const int eval = evaluate(&original);
        ok &= check(original.stack->in_check || original.stack->metadata.endgame,
            "castling oracle requires unpruned endgame or check evasions");
        original.init_root_moves();
        std::set<std::string> legal, castles;
        for (int i = 0; i < original.root.move_count; ++i) {
            auto & move = original.root.moves[i].move;
            legal.insert(move.to_string());
            if (move.castle) {
                castles.insert(move.to_string());
            }
        }
        ok &= check(legal == words(fixture.legal) && castles == words(fixture.castles),
            "independent castling legal set differs");
        for (int depth : {0, -1, -2}) {
            const auto tactical = words(depth == 0 ? fixture.entry : fixture.tactical);
            // Every certified child is a clock-100 draw. Stand pat is the only
            // shared evaluator dependency; no recursive engine score oracle.
            const int value = original.stack->in_check ? 0
                : tactical.empty() ? eval : std::max(eval, 0);
            for (auto window : {std::pair<int, int>{-score::INF, score::INF},
                    {value - 1, value}, {value, value + 1}, {value - 1, value + 1}}) {
                trans_table::clear();
                search_t search(fixture.fen);
                search.stack->in_check = search.brd.in_check();
                search.draw_contempt = search.tablebase_limit = 0;
                search.ffp_enabled = false; // certify the full evasion domain
                const int result = search.qsearch(window.first, window.second, depth);
                const bool valid = result <= window.first ? value <= result
                    : result >= window.second ? value >= result : value == result;
                ok &= check(valid, "castling qsearch bound differs");
                ok &= restored(search, original);
                if (window.first == -score::INF) {
                    ok &= check(search.nodes == tactical.size(),
                        "qsearch move domain differs from certified captures/evasions");
                }
                ++probes;
            }
        }
    }
    std::cout << "castling quiescence probes " << probes << '\n';
    return ok;
}

bool legal_pv(search_t & search) {
    board_t board;
    board.init(search.brd.to_string().c_str());
    std::istringstream pv(search.pv_to_string());
    for (std::string name; pv >> name;) {
        move_t move;
        move.set(&board, name.c_str());
        if (!board.valid(&move) || !board.legal(&move)) {
            return false;
        }
        board.forward(&move);
    }
    return true;
}

bool test_exact_pv() {
    bool ok = true;
    const char * fen = "rnbqkbnr/pppppppp/8/8/8/8/PPPPPPPP/RNBQKBNR w KQkq - 0 1";
    for (int wild : {0, 17}) {
        trans_table::clear();
        std::unique_ptr<search_t> search;
        if (wild == 0) {
            search = std::make_unique<search_t>(fen);
        } else {
            search = std::make_unique<w17_search_t>(fen);
        }
        search->stack->in_check = search->brd.in_check();
        search->draw_contempt = search->tablebase_limit = 0;
        const int cold = search->pvs(-score::INF, score::INF, 3);
        ok &= check(search->stack->pv_count > 0 && legal_pv(*search), "cold PV invalid");
        const U64 before = search->nodes;
        const auto state = search->brd.to_string();
        const int warm = search->pvs(-score::INF, score::INF, 3);
        ok &= check(cold == warm && search->nodes == before,
            "matching exact TT score was not reused");
        ok &= check(search->stack->pv_count == 0 && legal_pv(*search),
            "exact TT return exposed a stale/illegal PV");
        ok &= check(search->brd.to_string() == state && search->stack == search->root_stack,
            "exact TT return changed board/stack");
        // Root PV copies only the searched prefix. TT hints can extend UCI
        // text, but do not become a searched continuation or ponder move.
        move_t root_move;
        search_t parent(fen);
        root_move.set(&parent.brd, "e2e4");
        parent.forward(&root_move, false);
        trans_table::clear();
        const U64 context = parent.tt_context();
        trans_table::store(parent.brd.stack->tt_key, 0, parent.brd.ply, 3,
            42, 0, score::EXACT, context);
        parent.stack->pv_count = 2; // stale data must be discarded on entry
        ok &= check(parent.pvs(-score::INF, score::INF, 3) == 42
            && parent.stack->pv_count == 0, "exact score with no move retained stale PV");
        parent.backward(&root_move);
        parent.update_pv(&root_move);
        ok &= check(parent.stack->pv_count == 1 && parent.pv_to_string() == "e2e4 "
            && parent.ponder_move.piece == 0, "truncated exact PV fabricated continuation");
    }
    return ok;
}

struct integration_fixture_t { const char * fen; bool in_check; const char * moves; };
const integration_fixture_t integration_fixtures[] = {
#include "data/search/selectivity.inc"
};
bool test_production_integration() {
    bool ok = true;
    int probes = 0;
    for (const auto & fixture : integration_fixtures) {
        for (int depth : {2, 3}) {
            trans_table::enable();
            trans_table::clear();
            search_t original(fixture.fen);
            original.stack->in_check = original.brd.in_check();
            original.tablebase_limit = original.draw_contempt = 0;
            ok &= check(original.null_enabled && original.beta_pruning
                && original.ffp_enabled && original.lmr_enabled,
                "integration requires production pruning defaults");
            const int cold = original.pvs(-score::INF, score::INF, depth);
            ok &= check(!original.stop_all && legal_pv(original), "production cold search/PV invalid");
            const U64 before = original.nodes;
            const int warm = original.pvs(-score::INF, score::INF, depth);
            ok &= check(cold == warm && original.nodes == before,
                "production exact warm result differs");
            ok &= check(legal_pv(original), "production TT PV invalid");
            for (bool tt : {false, true}) {
                if (tt) {
                    trans_table::enable();
                } else {
                    trans_table::disable();
                }
                trans_table::clear();
                search_t baseline(fixture.fen);
                baseline.stack->in_check = baseline.brd.in_check();
                baseline.tablebase_limit = baseline.draw_contempt = 0;
                const int expected = baseline.pvs(-score::INF, score::INF, depth);
                ok &= check(legal_pv(baseline), "production cache mode PV invalid");
                for (int budget : {1, 13, 101}) {
                    trans_table::clear();
                    game_t game;
                    game.max_nodes = budget;
                    search_t stopped(fixture.fen, &game);
                    stopped.stack->in_check = stopped.brd.in_check();
                    stopped.tablebase_limit = stopped.draw_contempt = 0;
                    stopped.pvs(-score::INF, score::INF, depth);
                    ok &= restored(stopped, baseline);
                    ok &= check(stopped.nodes < static_cast<U64>(budget) || stopped.stop_all,
                        "production node budget did not stop");
                    // Restart with fresh heuristic/TT state; compare only
                    // identical deterministic settings, not selective vs exhaustive.
                    trans_table::clear();
                    search_t restart(fixture.fen);
                    restart.stack->in_check = restart.brd.in_check();
                    restart.tablebase_limit = restart.draw_contempt = 0;
                    ok &= check(restart.pvs(-score::INF, score::INF, depth) == expected,
                        "production restart differs from same-cache baseline");
                    ok &= restored(restart, baseline);
                    ok &= check(legal_pv(restart), "production restarted PV invalid");
                    ++probes;
                }
            }
        }
    }
    trans_table::enable();
    std::cout << "production cache/interruption/restart probes " << probes << '\n';
    return ok;
}

class large_root_search_t : public search_t {
public:
    std::vector<uint16_t> visited;
    explicit large_root_search_t(const char * fen) : search_t(fen) {}
    int pvs(int, int, int, bool = false) override {
        visited.push_back((stack - 1)->current_move.to_compact());
        nodes += U64{1} << 32;
        stack->pv_count = 0;
        return 0;
    }
};
bool test_root_counts() {
    bool ok = true;
    root_move_t first{}, second{};
    move_t best{};
    board_t board;
    board.init("rnbqkbnr/pppppppp/8/8/8/8/PPPPPPPP/RNBQKBNR w KQkq - 0 1");
    first.move.set(&board, "e2e4");
    second.move.set(&board, "d2d4");
    best.set(&board, "g1f3");
    first.nodes = std::numeric_limits<int64_t>::max();
    second.nodes = std::numeric_limits<int64_t>::min();
    ok &= check(first.compare(&second, &best) > 0 && second.compare(&first, &best) < 0,
        "root comparison overflowed");
    for (int64_t seed : {int64_t{-20}, int64_t{0}, int64_t{20}}) {
        first.nodes = seed;
        first.add_nodes(U64{1} << 32);
        ok &= check(first.nodes == (int64_t{1} << 32) + seed, "root work narrowed at 32 bits");
        first.add_nodes(std::numeric_limits<U64>::max());
        ok &= check(first.nodes == std::numeric_limits<int64_t>::max(), "root work did not saturate");
        first.add_nodes(1);
        ok &= check(first.nodes == std::numeric_limits<int64_t>::max(), "saturated root work wrapped");
    }
    first.nodes = -20;
    first.add_nodes(U64{1} << 63);
    ok &= check(first.nodes == std::numeric_limits<int64_t>::max() - 19,
        "negative ordering bias lost at signed boundary");
    // D1 must preserve the picker's legal order even when a separate root
    // priority would promote a later move. No completed iteration exists yet.
    large_root_search_t first_pass("r1bq1rk1/pp2ppbp/3p1np1/8/2PB4/1P2P3/P3BPPP/RN1Q1RK1 b - - 0 1");
    first_pass.init_root_moves();
    std::vector<uint16_t> picker_order;
    for (int i = 0; i < first_pass.root.move_count; ++i) {
        picker_order.push_back(first_pass.root.moves[i].move.to_compact());
        ok &= check(first_pass.root.moves[i].nodes == 0,
            "standard root work must not contain an initial SEE/danger bonus");
    }
    auto & promoted = first_pass.root.moves[first_pass.root.move_count - 1];
    promoted.nodes = int64_t{1} << 40;
    first_pass.stack->best_move = promoted.move;
    first_pass.pvs_root(-score::INF, score::INF, 1);
    ok &= check(first_pass.visited == picker_order,
        "D1 root search overrode the picker's move order");
    large_root_search_t search("rnbqkbnr/pppppppp/8/8/8/8/PPPPPPPP/RNBQKBNR w KQkq - 0 1");
    search.init_root_moves();
    std::ostringstream output;
    auto * original = std::cout.rdbuf(output.rdbuf());
    search.pvs_root(-score::INF, score::INF, 3);
    std::cout.rdbuf(original);
    for (int i = 0; i < search.root.move_count; ++i)
        ok &= check(search.root.moves[i].nodes >= (int64_t{1} << 32), "root loop narrowed visited work");
    search.root.moves[0].nodes = int64_t{1} << 40;
    ok &= check(search.root.is_easy() && !search.root.is_complex(), "root complexity narrowed 64-bit work");
    search.root.moves[1].nodes = int64_t{1} << 41;
    ok &= check(!search.root.is_easy() && search.root.is_complex(), "root complexity ordering reversed");
    search.stack->best_move.clear();
    search.root.sort_moves(&search.stack->best_move);
    ok &= check(search.root.moves[0].nodes == (int64_t{1} << 41), "root sort lost large subtree");
    return ok;
}
}
int main() {
    magic::init();
    options::get_option("OwnBook")->value = 0;
    trans_table::set_size(4);
    bool ok = test_castling();
    ok &= test_exact_pv();
    ok &= test_root_counts();
    ok &= test_production_integration();
    return ok ? EXIT_SUCCESS : EXIT_FAILURE;
}
