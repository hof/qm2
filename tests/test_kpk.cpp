#include "engine.h"
#include "eval_endgame.h"
#include <array>
#include <algorithm>

namespace {

bool failed(int line) {
    std::cerr << "KPK assertion failed at line " << line << "\n";
    return false;
}


constexpr int state_count = 2 * 24 * 64 * 64;

int separation(int a, int b) {
    return std::max(std::abs(a % 8 - b % 8), std::abs(a / 8 - b / 8));
}

bool pawn_attacks(int pawn, int square) {
    return square / 8 == pawn / 8 + 1 && std::abs(square % 8 - pawn % 8) == 1;
}

int index(bool pawn_turn, int king, int defender, int pawn) {
    return (((pawn / 8 - 1) * 4 + pawn % 8) * 64 + king) * 128
        + defender * 2 + pawn_turn;
}

bool legal(bool pawn_turn, int king, int defender, int pawn) {
    return king != pawn && defender != pawn && separation(king, defender) > 1
        && (!pawn_turn || !pawn_attacks(pawn, defender));
}

// Independent win-attractor construction: arithmetic moves, no engine move
// generator, bitbase indexing, or embedded table contents.
bool test_bitbase() {
    std::array<bool, state_count> won{};
    bool changed;
    do {
        changed = false;
        for (int rank = 1; rank <= 6; ++rank) {
            for (int file = 0; file < 4; ++file) {
                const int pawn = rank * 8 + file;
                for (int king = 0; king < 64; ++king) {
                    for (int defender = 0; defender < 64; ++defender) {
                        for (bool pawn_turn : {false, true}) {
                            const int current = index(pawn_turn, king, defender, pawn);
                            if (won[current] || !legal(pawn_turn, king, defender, pawn)) {
                                continue;
                            }
                            bool any = false, all = true, has_move = false;
                            const auto child = [&](bool winning) {
                                any |= winning;
                                all &= winning;
                                has_move = true;
                            };
                            const int moving_king = pawn_turn ? king : defender;
                            for (int step : {-9, -8, -7, -1, 1, 7, 8, 9}) {
                                const int target = moving_king + step;
                                if (target < 0 || target >= 64 || separation(moving_king, target) != 1) {
                                    continue;
                                }
                                if (pawn_turn) {
                                    if (target != pawn && separation(target, defender) > 1) {
                                        child(won[index(false, target, defender, pawn)]);
                                    }
                                } else if (separation(target, king) > 1 && !pawn_attacks(pawn, target)) {
                                    child(target != pawn && won[index(true, king, target, pawn)]);
                                }
                            }
                            if (pawn_turn && pawn + 8 != king && pawn + 8 != defender) {
                                if (rank == 6) {
                                    // An uncapturable promotion wins; rook promotion
                                    // is available when a queen would stalemate.
                                    child(separation(pawn + 8, defender) > 1
                                        || separation(pawn + 8, king) == 1);
                                } else {
                                    child(won[index(false, king, defender, pawn + 8)]);
                                    if (rank == 1 && pawn + 16 != king && pawn + 16 != defender) {
                                        child(won[index(false, king, defender, pawn + 16)]);
                                    }
                                }
                            }
                            if (pawn_turn ? any : has_move && all) {
                                won[current] = true;
                                changed = true;
                            }
                        }
                    }
                }
            }
        }
    } while (changed);

    board_t board;
    board.init("7k/8/8/8/8/8/P7/K7 w - - 0 1");
    int checked = 0;
    for (int rank = 1; rank <= 6; ++rank) {
        for (int file = 0; file < 4; ++file) {
            const int pawn = rank * 8 + file;
            for (int king = 0; king < 64; ++king) {
                for (int defender = 0; defender < 64; ++defender) {
                    for (bool pawn_turn : {false, true}) {
                        if (!legal(pawn_turn, king, defender, pawn)) {
                            continue;
                        }
                        const int expected = won[index(pawn_turn, king, defender, pawn)];
                        for (bool black_pawn : {false, true}) {
                            for (bool mirror : {false, true}) {
                                const int transform = (black_pawn ? 56 : 0) ^ (mirror ? 7 : 0);
                                board.bb[WPAWN] = board.bb[BPAWN] = 0;
                                board.bb[PAWN[!black_pawn]] = BIT(pawn ^ transform);
                                board.bb[KING[!black_pawn]] = BIT(king ^ transform);
                                board.bb[KING[black_pawn]] = BIT(defender ^ transform);
                                board.stack->wtm = pawn_turn != black_pawn;
                                if (eg::probe_kpk(board) != expected) {
                                    std::cerr << "KPK mismatch: king=" << king << " defender=" << defender
                                        << " pawn=" << pawn << " pawn_turn=" << pawn_turn << "\n";
                                    return false;
                                }
                            }
                        }
                        ++checked;
                    }
                }
            }
        }
    }
    std::cout << "Verified " << checked << " legal KPK states in four orientations\n";
    return true;
}

void prepare(search_t & search) {
    search.draw_contempt = 0;
    search.stack->in_check = search.brd.in_check();
    search.null_enabled = false;
    search.beta_pruning = false;
    search.lmr_enabled = false;
    search.ffp_enabled = false;
}

bool test_search() {
    const char * draws[] = {
        "k7/8/2K5/8/8/8/P7/8 w - - 0 1",
        "k7/8/2K5/8/8/8/P7/8 b - - 99 1",
        "8/p7/8/8/8/2k5/8/K7 b - - 0 1",
        "8/p7/8/8/8/2k5/8/K7 w - - 100 1",
        "k7/P7/2K5/8/8/8/8/8 b - - 0 1", // stalemate
        "8/4k3/8/4K3/4P3/8/8/8 w - - 0 1", // lost opposition
    };
    for (const char * fen : draws) {
        trans_table::clear();
        for (int attempt = 0; attempt < 2; ++attempt) {
            search_t search(fen);
            prepare(search);
            // A conflicting cached win must not override an exact bitbase draw.
            trans_table::store(search.brd.stack->tt_key, 0, 0, 8, 3000, 0,
                score::EXACT, search.tt_context());
            if (eg::probe_kpk(search.brd) != 0
                || search.pvs(-score::INF, score::INF, 4) != 0 || search.nodes != 1
                || search.qsearch(-score::INF, score::INF, -1) != 0) {
                std::cerr << "KPK draw failed: " << fen << "\n";
                return failed(__LINE__);
            }
            search.draw_contempt = 17;
            if (search.pvs(-score::INF, score::INF, 2) != 17) {
                return failed(__LINE__);
            }
        }
    }
    const char * wins[] = {
        "7k/4P3/4K3/8/8/8/8/8 w - - 0 1",
        "8/8/8/8/8/4k3/4p3/7K b - - 0 1",
        "8/4k3/8/4K3/4P3/8/8/8 b - - 0 1", // defender must yield opposition
        "8/8/8/4p3/4k3/8/4K3/8 w - - 0 1",
    };
    for (const char * fen : wins) {
        trans_table::clear();
        search_t search(fen);
        prepare(search);
        const int perspective = search.brd.bb[PAWN[search.brd.us()]] ? 1 : -1;
        const int static_score = evaluate(&search);
        const int searched_score = search.pvs(-score::INF, score::INF, 2);
        if (eg::probe_kpk(search.brd) != 1
            || perspective * static_score <= 0
            || perspective * searched_score <= 0 || search.nodes <= 1
            || search.stack->best_move.piece == 0) {
            std::cerr << "KPK scores " << static_score << "/" << searched_score << ": " << fen << "\n";
            return failed(__LINE__);
        }
        move_t best = search.stack->best_move;
        if (!search.brd.valid(&best) || !search.brd.legal(&best)) {
            return failed(__LINE__);
        }
        search.forward(&best, search.brd.gives_check(&best));
        // The chosen move must preserve a KPK win or promote to a winning piece.
        bool has_reply = false;
        if (best.promotion) {
            move::list_t replies;
            move::gen_quiet_moves(&search.brd, &replies);
            move::gen_captures(&search.brd, &replies);
            for (auto * reply = replies.first; reply != replies.last; ++reply) {
                has_reply |= search.brd.legal(reply);
            }
        }
        const bool preserves_win = best.promotion ? (best.promotion == QUEEN[!search.brd.us()]
            || best.promotion == ROOK[!search.brd.us()])
            && separation(best.tsq, search.brd.get_sq(KING[search.brd.us()])) > 1
            && (search.brd.in_check() || has_reply)
            : eg::probe_kpk(search.brd) == 1;
        search.backward(&best);
        if (!preserves_win) {
            std::cerr << "KPK move " << best.to_string() << " from " << fen << "\n";
            return failed(__LINE__);
        }
        search_t warm(fen);
        prepare(warm);
        if (warm.pvs(-score::INF, score::INF, 2) != searched_score) {
            return failed(__LINE__);
        }
    }
    search_t capture("k7/8/2K5/1b6/8/8/P7/8 w - - 0 1");
    prepare(capture);
    const std::string original = capture.brd.to_string();
    move_t take;
    take.set(&capture.brd, "c6b5");
    if (!capture.brd.valid(&take) || !capture.brd.legal(&take)) {
        return failed(__LINE__);
    }
    capture.forward(&take, capture.brd.gives_check(&take));
    if (eg::probe_kpk(capture.brd) != 0
        || capture.qsearch(-score::INF, score::INF, -1) != 0) {
        return failed(__LINE__);
    }
    capture.backward(&take);
    // The certified draw supplies a lower bound; NNUE may prefer another line.
    if (capture.brd.to_string() != original
        || capture.pvs(-score::INF, score::INF, 1) < 0) {
        return failed(__LINE__);
    }

    search_t boundary("k7/8/8/8/8/8/4P3/4K3 w - - 99 1");
    prepare(boundary);
    move_t quiet;
    quiet.set(&boundary.brd, "e1d1");
    if (eg::probe_kpk(boundary.brd) != 1) {
        return failed(__LINE__);
    }
    boundary.forward(&quiet, boundary.brd.gives_check(&quiet));
    const bool clock_draw = boundary.pvs(-score::INF, score::INF, 2) == 0;
    boundary.backward(&quiet);
    search_t outside("7k/8/8/8/8/8/P7/KR6 w - - 0 1");
    return clock_draw && eg::probe_kpk(outside.brd) == -1;
}

}

int main() {
    magic::init();
    if (!test_bitbase()) {
        return EXIT_FAILURE;
    }
    if (!test_search()) {
        std::cerr << "KPK search integration failed\n";
        return EXIT_FAILURE;
    }
    return EXIT_SUCCESS;
}
