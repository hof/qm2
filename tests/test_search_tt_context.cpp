#include "engine.h"

namespace {

bool check(const char * name, bool success) {
    if (!success) {
        std::cerr << name << "\n";
    }
    return success;
}

void prepare(search_t & search) {
    search.draw_contempt = 0;
    search.stack->in_check = search.brd.in_check();
    search.null_enabled = false;
    search.beta_pruning = false;
    search.lmr_enabled = false;
    search.ffp_enabled = false;
}

int run(const std::string & fen) {
    search_t search(fen.c_str());
    prepare(search);
    const int result = search.pvs(-score::INF, score::INF, 1);
    if (search.stop_all || search.brd.ply != 0 || search.brd.to_string() != fen) {
        std::cerr << "TT test search aborted or failed to restore the board\n";
        std::exit(EXIT_FAILURE);
    }
    return result;
}

bool test_rule50_buckets() {
    const std::string prefix = "5B2/3k1K2/8/2R4P/5P2/8/8/7r w - - ";
    auto first = std::make_unique<search_t>((prefix + "22 203").c_str());
    auto same = std::make_unique<search_t>((prefix + "29 203").c_str());
    auto next = std::make_unique<search_t>((prefix + "30 203").c_str());
    auto early = std::make_unique<search_t>((prefix + "0 203").c_str());
    auto late = std::make_unique<search_t>((prefix + "13 203").c_str());
    for (auto * search : {first.get(), same.get(), next.get()}) prepare(*search);
    bool success = check("clock buckets must not change board or repetition identity",
        first->brd.stack->tt_key == next->brd.stack->tt_key
        && first->brd.stack->repetition_key() == next->brd.stack->repetition_key());
    success &= check("TT keys must share within a bucket and differ across buckets",
        first->position_key() == same->position_key() && first->position_key() != next->position_key()
        && early->position_key() == late->position_key()
        && early->position_key() == early->brd.stack->tt_key);
    trans_table::clear();
    trans_table::store(first->position_key(), 0, 0, 2, 123, 0, score::EXACT);
    success &= check("different counts within a bucket must reuse TT scores",
        same->pvs(122, 123, 1, true) == 123 && same->nodes == 0);
    const int warm = next->pvs(-score::INF, score::INF, 1);
    trans_table::clear();
    success &= check("another bucket must search like a fresh table",
        warm == run(prefix + "30 203") && next->nodes > 0);
    return success;
}

bool test_fifty_move_horizon() {
    const std::string fresh = "7k/8/8/8/8/8/8/KR6 w - - 0 1";
    const std::string near_draw = "7k/8/8/8/8/8/8/KR6 w - - 99 1";
    trans_table::clear();
    const int winning_score = run(fresh);
    const int warm_draw = run(near_draw);
    trans_table::clear();
    const int draw_score = run(near_draw);
    const int warm_win = run(fresh);
    bool success = check("fixture must distinguish a rook advantage from the fifty-move draw",
        winning_score > 200 && draw_score == 0);
    success &= check("cached winning scores must not bypass a draw on the next move",
        warm_draw == draw_score);
    success &= check("cached draw scores must not erase a fresh rook advantage",
        warm_win == winning_score);
    // Near the fifty-move limit cached scores are not trusted for cutoffs.
    for (int clock : {89, 90}) {
        const std::string fen = "7k/8/8/8/8/8/8/KR6 w - - " + std::to_string(clock) + " 1";
        trans_table::clear();
        search_t search(fen.c_str());
        prepare(search);
        trans_table::store(search.position_key(), 0, 0, 4, 5000, 0, score::EXACT);
        const int value = search.pvs(4999, 5000, 1, true);
        success &= check("TT cutoffs must stop at a halfmove clock of 90",
            clock < 90 ? value == 5000 && search.nodes == 0 : search.nodes > 0);
    }
    return success;
}

bool test_non_pv_tt_bounds() {
    const char * fen = "7k/8/8/8/8/8/8/KR6 w - - 4 1";
    bool success = true;
    for (int bound : {score::EXACT, score::LOWERBOUND, score::UPPERBOUND}) {
        for (int value : {-123, 123}) {
            // Include equality at alpha and beta as well as values beyond them.
            for (int alpha : {value - 2, value - 1, value, value + 1}) {
                trans_table::clear();
                search_t search(fen);
                prepare(search);
                const U64 key = search.brd.stack->tt_key;
                search.stack->pv_count = 1;
                trans_table::store(search.position_key(), 0, 0, 2, value, 0, bound);
                const int result = search.pvs(alpha, alpha + 1, 1);
                const bool cutoff = bound == score::EXACT
                    || (bound == score::LOWERBOUND && value >= alpha + 1)
                    || (bound == score::UPPERBOUND && value <= alpha);
                success &= check("non-PV TT cutoffs must respect bound direction and equality",
                    cutoff ? search.nodes == 0 && result == value && search.stack->pv_count == 0
                        : search.nodes > 0);
                success &= check("non-PV TT probes must restore board and search state",
                    !search.stop_all && search.brd.to_string() == fen && search.brd.ply == 0
                    && search.brd.stack->tt_key == key && search.stack == search.root_stack);
            }
        }
    }
    // A fail-high needs one ply more than the searched depth; mates need none.
    trans_table::clear();
    search_t shallow(fen);
    prepare(shallow);
    trans_table::store(shallow.position_key(), 0, 0, 1, 123, 0, score::LOWERBOUND);
    success &= check("a lower bound at equal depth must not cut off",
        shallow.pvs(122, 123, 1) >= -score::INF && shallow.nodes > 0);
    trans_table::clear();
    search_t mate(fen);
    prepare(mate);
    trans_table::store(mate.position_key(), 0, 0, 1, score::MATE - 9, 0, score::LOWERBOUND);
    success &= check("a proven mate bound must cut off at any depth",
        mate.pvs(0, 1, 5) == score::MATE - 9 && mate.nodes == 0);
    return success;
}

bool test_compact_moves() {
    const char * positions[] = {
        "rnbqkbnr/pppppppp/8/8/8/8/PPPPPPPP/RNBQKBNR w KQkq - 0 1",
        "r3k2r/8/8/8/8/8/8/R3K2R w KQkq - 0 1",
        "r3k2r/8/8/8/8/8/8/R3K2R b KQkq - 0 1",
        "7k/8/8/3pP3/8/8/8/K7 w - d6 0 1",
        "k7/8/8/8/3Pp3/8/8/7K b - d3 0 1",
        "1r5k/P7/8/8/8/8/8/K7 w - - 0 1",
        "k7/8/8/8/8/8/p7/1R5K b - - 0 1",
    };
    bool success = true;
    int castles = 0, ep_captures = 0, promotions = 0;
    trans_table_t table(1);
    for (const char * fen : positions) {
        search_t search(fen);
        const int count = search.init_root_moves();
        for (int i = 0; i < count; ++i) {
            move_t & original = search.root.moves[i].move;
            move_t decoded{};
            decoded.castle = CASTLE_Q;
            decoded.en_passant = true;
            table.store(search.brd.stack->tt_key, 31, 0, 254, -1234,
                original.to_compact(), score::EXACT, 77, true);
            tt_hit_t hit;
            success &= check("packed entries must preserve move, score, depth, eval and PV",
                table.probe(search.brd.stack->tt_key, 0, 0, hit)
                && hit.score == -1234 && hit.bound == score::EXACT && hit.depth == 254
                && hit.eval == 77 && hit.pv);
            decoded.set_compact(&search.brd, hit.move);
            success &= check(original.to_string().c_str(),
                decoded.to_int() == original.to_int() && decoded.score == score::INVALID);
            castles += original.castle != 0;
            ep_captures += original.en_passant;
            promotions += original.promotion != 0;
        }
    }
    success &= check("compact move fixtures must cover all special move kinds",
        castles == 4 && ep_captures == 2 && promotions == 16);
    move_t empty{};
    empty.set_compact(nullptr, 0);
    success &= check("an empty cached move must remain empty", empty.piece == EMPTY && empty.to_compact() == 0);
    return success;
}

bool test_replacement() {
    trans_table_t table(1);
    const U64 key = 0x1234567800000001ULL;
    tt_hit_t hit;
    table.store(key, 0, 0, 0, 0, 0, 0, score::INVALID, false);
    bool success = check("an empty table must not match", !table.probe(0x42, 0, 0, hit));
    table.store(key, 0, 0, 12, 50, 111, score::LOWERBOUND, score::INVALID, false);
    table.store(key, 0, 0, 2, 70, 222, score::UPPERBOUND, score::INVALID, false);
    success &= check("a much shallower bound must keep the deeper score but take the new move",
        table.probe(key, 0, 0, hit) && hit.depth == 12 && hit.score == 50 && hit.move == 222);
    table.store(key, 0, 0, 2, 60, 0, score::EXACT, score::INVALID, false);
    success &= check("an exact bound must replace a non-exact score and keep the move",
        table.probe(key, 0, 0, hit) && hit.bound == score::EXACT && hit.score == 60 && hit.move == 222);
    table.store(key, 0, 0, 0, 0, 0, 0, 33, false);
    success &= check("an evaluation-only store must keep the score and attach the evaluation",
        table.probe(key, 0, 0, hit) && hit.bound == score::EXACT && hit.eval == 33);
    // Three tags share one cluster; a fourth evicts the shallowest entry.
    trans_table_t small(1);
    for (int i = 1; i <= 3; ++i) {
        small.store(0x1000 + i, 0, 0, i * 4, i, 0, score::EXACT, score::INVALID, false);
    }
    small.store(0x1004, 0, 0, 20, 4, 0, score::EXACT, score::INVALID, false);
    success &= check("replacement must evict the shallowest entry",
        !small.probe(0x1001, 0, 0, hit) && small.probe(0x1002, 0, 0, hit)
        && small.probe(0x1003, 0, 0, hit) && small.probe(0x1004, 0, 0, hit));
    return success;
}

}

int main() {
    magic::init();
    trans_table::set_size(4);
    bool success = test_rule50_buckets();
    success &= test_fifty_move_horizon();
    success &= test_non_pv_tt_bounds();
    success &= test_compact_moves();
    success &= test_replacement();
    return success ? EXIT_SUCCESS : EXIT_FAILURE;
}
