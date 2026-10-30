#include "engine.h"
#include <set>

namespace {

bool legality_contract() {
    int rejected = 0;
    for (const char * fen : {
            "rnbqkbnr/pppppppp/8/8/8/8/PPPPPPPP/RNBQKBNR w KQkq - 0 1",
            "r3k2r/p1ppqpb1/bn2pnp1/3PN3/1p2P3/2N2Q1p/PPPBBPPP/R3K2R w KQkq - 0 1",
            "4r2k/8/8/8/8/8/4R3/4K3 w - - 0 1",
            "4r2k/8/8/8/8/8/8/4K3 w - - 0 1",
            "k4r2/8/8/8/8/8/8/4K2R w K - 0 1",
            "7k/8/8/r4pPK/8/8/8/8 w - f6 0 1",
            "7k/P7/8/8/8/8/8/7K w - - 0 1"}) {
        auto search = std::make_unique<search_t>(fen);
        search->stack->in_check = search->brd.in_check();
        const std::string original = search->brd.to_string();
        const U64 key = search->brd.stack->tt_key;
        const U64 pawn_key = search->brd.stack->pawn_key;
        const U64 search_key = position_key(search->brd);
        std::set<int> seen;
        for (auto * candidate = move::first(search.get(), 1); candidate;
                candidate = move::next(search.get(), 1)) {
            move_t expected = *candidate;
            const bool legal = search->brd.legal(&expected);
            if (!seen.insert(candidate->to_int()).second) {
                return false;
            }
            const bool accepted = search->brd.legal(candidate);
            if (accepted) {
                search->forward(candidate, search->brd.gives_check(candidate));
            }
            if (accepted != legal || candidate->en_passant != expected.en_passant
                    || candidate->castle != expected.castle) {
                std::cerr << "legality mismatch: " << fen << ' ' << candidate->to_string() << '\n';
                return false;
            }
            if (accepted) {
                U64 expected_key = 0;
                for (int piece : {WPAWN, BPAWN}) {
                    U64 pawns = search->brd.bb[piece];
                    while (pawns) {
                        HASH_ADD_PIECE(expected_key, piece, pop(pawns));
                    }
                }
                if (search->brd.stack->pawn_key != expected_key) {
                    return false;
                }
                search->forward();
                if (search->brd.stack->pawn_key != expected_key) {
                    return false;
                }
                search->backward();
                search->backward(candidate);
            } else {
                ++rejected;
            }
            if (search->brd.to_string() != original || search->brd.stack->tt_key != key
                    || search->brd.stack->pawn_key != pawn_key
                    || position_key(search->brd) != search_key || search->stack != search->root_stack) {
                return false;
            }
        }
    }
    return rejected > 0;
}

bool pawn_hint_geometry() {
    for (bool us : {false, true}) {
        board_t board;
        board.init(us ? "4k3/8/8/3p4/4P3/8/8/4K3 w - - 0 1"
            : "4k3/8/8/3p4/4P3/8/8/4K3 b - - 0 1");
        move_t move;
        for (const char * text : us ? std::initializer_list<const char *>{"e4e6", "e4f4"}
                : std::initializer_list<const char *>{"d5d3", "d5e5"}) {
            move.set(&board, text);
            if (board.valid(&move)) {
                return false;
            }
        }
        move.set(&board, us ? "e4d5" : "d5e4");
        if (!board.valid(&move)) {
            return false;
        }
    }
    return true;
}

bool hint_stages() {
    auto search = std::make_unique<search_t>("3q3k/8/8/3p4/8/8/1p1Q4/7K w - - 0 1");
    move_t parent;
    parent.set(&search->brd, "d2c2");
    search->forward(&parent, search->brd.gives_check(&parent));
    search->stack->killer[0].set(&search->brd, "d8e8");
    search->stack->killer[1].set(&search->brd, "d8f8");
    move_t counter;
    counter.set(&search->brd, "d8g8");
    search->countermoves[WQUEEN][c2] = counter.to_compact();
    std::set<uint16_t> seen;
    int hints = 0;
    for (auto * move = move::first(search.get(), 2); move; move = move::next(search.get(), 2)) {
        if (!seen.insert(move->to_compact()).second) {
            return false;
        }
        const bool killer = search->is_killer(move);
        const bool is_countermove = move->equals(&counter);
        if (killer || is_countermove) {
            ++hints;
            const int stage = search->stack->move_list.stage;
            if (stage != killer_2 && stage != countermove && stage != quiet_init) {
                return false;
            }
        }
    }
    if (hints != 3) {
        return false;
    }
    return true;
}

bool staging_contract() {
    auto search = std::make_unique<search_t>("3q3k/8/8/3p4/8/8/1p1Q4/7K w - - 0 1");
    search->stack->tt_move.set(&search->brd, "d2c2");
    search->stack->killer[0].set(&search->brd, "d2e2");
    search->stack->killer[1].set(&search->brd, "d2f2");
    search->quiet_history->main[WHITE][h1][g1] = 100;
    int previous_rank = -1;
    bool phases[6] = {};
    std::set<uint16_t> seen;
    for (auto * move = move::first(search.get(), 2); move; move = move::next(search.get(), 2)) {
        const bool noisy = move->capture || move->promotion;
        const int rank = move->equals(&search->stack->tt_move) ? 0
            : noisy && search->brd.see(move, 0) ? 1
            : search->is_killer(move) ? 2
            : !noisy && move->score > 0 ? 3 : noisy ? 4 : 5;
        if (rank < previous_rank || !seen.insert(move->to_compact()).second) {
            return false;
        }
        previous_rank = rank;
        phases[rank] = true;
    }
    if (!std::all_of(std::begin(phases), std::end(phases), [](bool value) { return value; })) {
        return false;
    }
    move_t parent_move = search->stack->tt_move;
    search->forward(&parent_move, search->brd.gives_check(&parent_move));
    for (bool valid : {false, true}) {
        move_t counter;
        counter.set(valid ? BQUEEN : BKING, valid ? d8 : h8, valid ? e8 : h6);
        search->countermoves[WQUEEN][c2] = counter.to_compact();
        int found = 0;
        for (auto * move = move::first(search.get(), 2); move;
                move = move::next(search.get(), 2)) {
            if (move->to_compact() == counter.to_compact()) {
                ++found;
                if (search->stack->move_list.stage != quiet_init) {
                    return false;
                }
            }
        }
        if (found != int(valid)) {
            return false;
        }
    }
    search->backward(&parent_move);
    return true;
}

bool quiescence_noisy_contract() {
    for (int depth : {0, -1, -2}) {
        for (bool hint : {false, true}) {
            auto search = std::make_unique<search_t>("3q3k/8/8/3p4/8/8/1p1Q4/7K w - - 0 1");
            if (hint) {
                search->stack->tt_move.set(&search->brd, "d2d5");
            }
            std::vector<std::string> actual;
            for (auto * move = move::first(search.get(), depth); move;
                    move = move::next(search.get(), depth)) {
                actual.push_back(move->to_string());
            }
            const std::vector<std::string> expected = hint
                ? std::vector<std::string>{"d2d5", "d2b2"}
                : std::vector<std::string>{"d2b2"};
            if (actual != expected) {
                return false;
            }
        }
        auto search = std::make_unique<search_t>("7k/8/8/1b6/8/8/4r3/3QK3 w - - 0 1");
        search->stack->in_check = search->brd.in_check();
        move_t losing_evasion;
        losing_evasion.set(&search->brd, "d1e2");
        if (!search->stack->in_check || !search->brd.legal(&losing_evasion)
                || search->brd.see(&losing_evasion, quiescence_see_threshold)) {
            return false;
        }
        int found = 0;
        for (auto * move = move::first(search.get(), depth); move;
                move = move::next(search.get(), depth)) {
            if (move->equals(&losing_evasion)) {
                ++found;
            }
        }
        if (found != 1) {
            return false;
        }
    }
    return true;
}

bool pruned_quiets_contract() {
    auto quiet_search = std::make_unique<search_t>("rnbqkbnr/pppppppp/8/8/8/8/PPPPPPPP/RNBQKBNR w KQkq - 0 1");
    quiet_search->stack->tt_move.set(&quiet_search->brd, "e2e4");
    if (!move::first(quiet_search.get(), 2) || move::next(quiet_search.get(), 2, true)) {
        return false;
    }

    // Bad noisy moves follow the quiets, so those must still be walked.
    auto search = std::make_unique<search_t>("3q3k/8/8/3p4/8/8/1p1Q4/7K w - - 0 1");
    int bad_noisy_found = 0;
    for (auto * move = move::first(search.get(), 2); move; move = move::next(search.get(), 2, true)) {
        bad_noisy_found += move->to_string() == "d2d5";
    }
    return bad_noisy_found == 1;
}

}

int main() {
    magic::init();
    if (!legality_contract() || !pawn_hint_geometry() || !staging_contract() || !hint_stages()
            || !quiescence_noisy_contract() || !pruned_quiets_contract()) {
        std::cerr << "pseudo-legal picker / lazy legality contract failed\n";
        return EXIT_FAILURE;
    }
    return EXIT_SUCCESS;
}
