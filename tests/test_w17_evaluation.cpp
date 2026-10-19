#include "engine.h"
#include "w17/w17_search.h"
#include "w17/w17_eval.h"

namespace {

bool check(const char * name, bool success) {
    if (!success) {
        std::cerr << name << '\n';
    }
    return success;
}

struct fixture_t {
    const char * position;
    int value; // White-relative material score used by the playing evaluator.
};

bool test_scores() {
    const fixture_t fixtures[] = {
        {"7k/7p/8/8/8/8/P7/K7", 0},
        {"7k/7p/8/8/P7/8/8/K7", 0},
        {"7k/7p/8/8/P7/2K5/8/8", 0},
        {"6rk/8/8/8/8/2K5/8/1R6", 0},
        {"6rk/7p/8/8/P7/8/8/KR6", 0},
        {"rnbqkbnr/pppppppp/8/8/P7/8/1PPPPPPP/RNBQKBNR", 0},
        // Specialized material classes retain their original scores.
        {"7k/7p/8/8/8/2K5/8/1R6", 4900},
        {"7k/7p/8/8/P7/2K5/8/1R6", 2600},
        {"6rk/8/8/8/P7/2K5/8/8", -4900},
        {"6rk/7p/8/8/P7/2K5/8/8", -2600},
        {"6rk/8/8/8/P7/2K5/8/1R6", -4900},
        {"6rk/7p/8/8/8/2K5/8/1R6", 4900},
    };
    bool success = true;
    for (const auto & fixture : fixtures) {
        for (const char * side : {" w - - 0 1", " b - - 0 1"}) {
            const std::string fen = std::string(fixture.position) + side;
            auto search = std::make_unique<w17_search_t>(fen.c_str());
            const auto original = search->brd.to_string();
            const auto tt_key = search->brd.stack->tt_key;
            const int expected = search->brd.us() ? fixture.value : -fixture.value;
            for (int repeat = 0; repeat < 2; ++repeat) {
                const int actual = w17_evaluate(search.get());
                if (actual != expected) {
                    std::cerr << fen << " expected=" << expected << " actual=" << actual << '\n';
                    success = false;
                }
                success &= check("evaluation preserves board, hashes and stack", original == search->brd.to_string()
                    && search->brd.stack->tt_key == tt_key
                    && search->brd.ply == 0 && search->stack == search->root_stack);
            }
            board_t flipped;
            flipped.init(fen.c_str());
            flipped.do_flip();
            auto mirror = std::make_unique<w17_search_t>(flipped.to_string().c_str());
            success &= check("color/rank flip preserves side-to-move score", w17_evaluate(mirror.get()) == expected);

            auto standard = std::make_unique<search_t>(fen.c_str());
            standard->stack->in_check = standard->brd.in_check();
            const int standard_cold = evaluate(standard.get());
            success &= check("standard and losers transposition identities differ",
                standard->brd.stack->tt_key != search->brd.stack->tt_key);
            success &= check("neural evaluation cannot change losers material", w17_evaluate(search.get()) == expected);
            auto standard_warm = std::make_unique<search_t>(fen.c_str());
            standard_warm->stack->in_check = standard_warm->brd.in_check();
            success &= check("losers material cannot change neural evaluation", evaluate(standard_warm.get()) == standard_cold);
        }
    }
    return success;
}

bool test_terminals() {
    const char * positions[] = {
        "7k/7p/8/8/8/8/8/K7 w - - 100 1",
        "7k/8/8/8/8/8/P7/K7 b - - 100 1",
        "7k/7p/8/8/8/8/8/K7 b - - 100 1",
        "7k/8/8/8/8/8/P7/K7 w - - 100 1",
    };
    bool success = true;
    for (int i = 0; i < 4; ++i) {
        for (int depth : {0, 1, 3}) {
            auto search = std::make_unique<w17_search_t>(positions[i]);
            success &= check("king-only terminals precede static evaluation and draws",
                search->w17_pvs(-score::INF, score::INF, -1, depth) == (i < 2 ? score::MATE : -score::MATE));
        }
    }
    return success;
}

}

int main() {
    magic::init();
    trans_table::set_size(4);
    bool success = test_scores();
    success &= test_terminals();
    return success ? EXIT_SUCCESS : EXIT_FAILURE;
}
