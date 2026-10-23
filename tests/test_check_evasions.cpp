#include "engine.h"
#include "movepicker.h"
#include <set>
#include <vector>

namespace {

using moves_t = std::set<uint16_t>;

moves_t generated(board_t & board, bool restricted, int & candidates) {
    move::list_t list;
    if (restricted) {
        list.evasion_targets = move::evasion_targets(board);
    }
    move::gen_captures(&board, &list);
    move::gen_promotions(&board, &list);
    move::gen_castles(&board, &list);
    move::gen_quiet_moves(&board, &list);
    candidates = int(list.last - list.first);
    moves_t legal;
    for (auto * move = list.first; move != list.last; ++move) {
        if (!move::allows_evasion(*move, list.evasion_targets)) {
            std::abort();
        }
        if (board.legal(move)) {
            legal.insert(move->to_compact());
        }
    }
    return legal;
}

bool compare(search_t & search, int depth, int & positions, int & checked, int & removed) {
    auto & board = search.brd;
    int full_count = 0, restricted_count = 0;
    const auto full = generated(board, false, full_count);
    const auto restricted = generated(board, true, restricted_count);
    ++positions;
    checked += board.in_check();
    removed += full_count - restricted_count;
    if (full != restricted || restricted_count > full_count) {
        std::cerr << "evasion set mismatch: " << board.to_string() << '\n';
        return false;
    }
    if (depth == 0) {
        return true;
    }
    for (auto code : full) {
        move_t move{};
        move.set_compact(&board, code);
        board.forward(&move);
        if (!compare(search, depth - 1, positions, checked, removed)) {
            return false;
        }
        board.backward(&move);
    }
    return true;
}

bool picker(const char * fen, bool enabled) {
    auto worker = std::make_unique<search_t>(fen);
    auto & search = *worker;
    search.evasions_enabled = enabled;
    search.stack->in_check = search.brd.in_check();
    int count = 0;
    const auto expected = generated(search.brd, false, count);
    moves_t actual;
    // Supply both legal and irrelevant cached hints: they share the same mask.
    move::list_t list;
    move::gen_captures(&search.brd, &list);
    move::gen_promotions(&search.brd, &list);
    move::gen_quiet_moves(&search.brd, &list);
    for (int hint_kind = 0; hint_kind < 3; ++hint_kind) {
        for (auto * hint = list.first; hint != list.last; ++hint) {
            search.stack->tt_move.clear();
            search.stack->killer[0].clear();
            search.stack->killer[1].clear();
            if (hint_kind == 0) {
                search.stack->tt_move = *hint;
            } else if (!hint->capture && !hint->promotion && !hint->castle) {
                search.stack->killer[hint_kind - 1] = *hint;
            }
            actual.clear();
            for (auto * move = move::first(&search, -1); move; move = move::next(&search, -1)) {
                if (enabled && !move::allows_evasion(*move, move::evasion_targets(search.brd))) {
                    return false;
                }
                if (search.brd.legal(move) && !actual.insert(move->to_compact()).second) {
                    return false;
                }
            }
            if (actual != expected) {
                std::cerr << "checked qsearch picker mismatch: " << fen << '\n';
                return false;
            }
        }
    }
    return true;
}
}

int main() {
    magic::init();
    uci::silent(true);
    // Slider/leaper checks, double check, EP escapes and pins, illegal
    // EP discovered check, promotions, double-push blocks, pins and castles.
    const std::vector<const char *> fixtures = {
        "6k1/4r3/8/8/8/8/3P4/R3K2R w KQ - 0 1",
        "4r2k/8/8/8/1b6/8/7P/R3K2R w KQ - 0 1",
        "7k/8/8/3pP3/4K3/8/8/8 w - d6 0 1",
        "4r2k/8/8/3pP3/4K3/8/8/8 w - d6 0 1",
        "7k/8/8/r2pP2K/8/8/8/8 w - d6 0 1",
        "1r5k/P7/8/8/8/8/8/1K6 w - - 0 1",
        "7k/8/8/8/r6K/8/3P4/8 w - - 0 1",
        "7k/8/8/8/8/5n2/8/4K3 w - - 0 1",
        "7k/6Q1/5K2/8/8/8/8/8 b - - 99 1",
        "4k3/8/5N2/8/8/8/8/7K b - - 0 1",
        "4k3/3p4/8/1B6/8/8/8/4R2K b - - 0 1",
        "r3k2r/ppp2ppp/2n1bn2/3qp3/8/2N1PN2/PPP2PPP/R2QKB1R w KQkq - 0 1"
    };
    int positions = 0, checked = 0, removed = 0;
    for (auto fen : fixtures) {
        auto search = std::make_unique<search_t>(fen);
        if (!compare(*search, 2, positions, checked, removed)) {
            return EXIT_FAILURE;
        }
        if (search->brd.in_check() && (!picker(fen, true) || !picker(fen, false))) {
            return EXIT_FAILURE;
        }
    }
    options::get_option("CheckEvasions")->value = 0;
    auto disabled = std::make_unique<search_t>(fixtures.front());
    options::get_option("CheckEvasions")->value = 1;
    auto enabled = std::make_unique<search_t>(fixtures.front());
    if (disabled->evasions_enabled || !enabled->evasions_enabled || positions < 1000 || !checked || !removed) {
        return EXIT_FAILURE;
    }
    std::cout << positions << " positions, " << checked << " checks, " << removed
              << " irrelevant candidates avoided\n";
    return EXIT_SUCCESS;
}
