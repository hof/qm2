#include "engine.h"
#include <thread>

namespace {

const char * fen = "7k/8/8/3q4/8/8/8/K2R4 w - - 0 1";

bool check(bool condition, const char * message) {
    if (!condition) {
        std::cerr << message << "\n";
    }
    return condition;
}

bool test_configuration_snapshot() {
    game_t settings;
    settings.max_depth = 3;
    settings.max_time_per_move = 10000;
    settings.ponder = true;
    search_control_t control;
    control.ponder.store(true);
    search_t search(fen, &settings, &control);
    bool success = check(settings.tm.get_min() == 0,
        "constructing a search must not start the caller's timer");
    settings.max_depth = 0;
    settings.max_nodes = 1;
    settings.ponder = false;
    success &= check(search.game.max_depth == 3 && search.game.max_nodes == 0
        && search.pondering(), "caller mutations must not change the search request");
    control.ponder.store(false);
    success &= check(!search.pondering(), "ponderhit must reach the active search");
    const int score = search.qsearch(-score::INF, score::INF, 0);
    success &= check(score > 200 && search.nodes > 1 && !search.stop_all,
        "changing the caller's node limit must not abort its existing search");
    return success;
}

bool test_independent_cancellation() {
    search_control_t stopped_control;
    search_control_t active_control;
    stopped_control.stop_requested.store(true);
    search_t stopped(fen, nullptr, &stopped_control);
    stopped.qsearch(-score::INF, score::INF, 0);
    bool success = check(stopped.stop_all, "search must honor its own cancellation");
    engine::stop();
    search_t active(fen, nullptr, &active_control);
    const int active_score = active.qsearch(-score::INF, score::INF, 0);
    success &= check(active_score > 200 && !active.stop_all,
        "another search or the singleton must not cancel this search");
    search_t standalone(fen);
    const int standalone_score = standalone.qsearch(-score::INF, score::INF, 0);
    success &= check(standalone_score > 200 && !standalone.stop_all,
        "standalone searches must not inherit global cancellation");
    return success;
}

bool test_result_snapshot() {
    engine_t engine;
    game_t settings;
    search_t search(fen, &settings);
    search.stack->best_move.set(&search.brd, "d1d5");
    search.game.target_move = search.stack->best_move;
    search.result_score = 600;
    search.nodes = 42;
    // Publishing must use the search's target, rather than pending engine settings.
    engine.copy_results(&search);
    search_result_t result = engine.get_result();
    bool success = check(result.target_found && result.score == 600
        && result.nodes == 42 && result.move.to_int() == search.stack->best_move.to_int(),
        "move, score, nodes, and target status must be published together");
    engine.new_game(fen);
    const search_result_t cleared = engine.get_result();
    success &= check(!cleared.target_found && cleared.score == 0
        && cleared.nodes == 0 && cleared.move.piece == 0,
        "a new game must clear every result field");
    return success;
}

bool test_worker_snapshot() {
    engine_t engine;
    engine.new_game(fen);
    engine.settings()->max_depth = 1;
    if (!check(engine.think(), "could not create search worker")) {
        return false;
    }
    // These settings are for the next search and no longer belong to the worker.
    engine.settings()->max_depth = 0;
    engine.settings()->max_nodes = 1;
    engine.stop_all();
    search_result_t result = engine.get_result();
    return check(result.move.piece && result.move.to_string() == "d1d5"
        && result.score > 200 && result.nodes > 1,
        "worker must finish the captured request after pending settings change");
}

bool test_result_publication() {
    engine_t engine;
    search_t first(fen);
    search_t second(fen);
    first.stack->best_move.set(&first.brd, "d1d5");
    first.game.target_move = first.stack->best_move;
    first.result_score = 600;
    first.nodes = 42;
    second.stack->best_move.set(&second.brd, "a1b1");
    second.result_score = -100;
    second.nodes = 84;
    const int first_move = first.stack->best_move.to_int();
    const int second_move = second.stack->best_move.to_int();
    std::thread publisher([&]() {
        for (int i = 0; i < 10000; ++i) {
            engine.copy_results(i % 2 ? &first : &second);
        }
    });
    bool coherent = true;
    for (int i = 0; i < 10000; ++i) {
        search_result_t result = engine.get_result();
        if (!result.move.piece) {
            coherent &= result.score == 0 && result.nodes == 0 && !result.target_found;
        } else if (result.move.to_int() == first_move) {
            coherent &= result.score == 600 && result.nodes == 42 && result.target_found;
        } else {
            coherent &= result.move.to_int() == second_move && result.score == -100
                && result.nodes == 84 && !result.target_found;
        }
    }
    publisher.join();
    return check(coherent, "concurrent readers must never see mixed search results");
}

}

int main() {
    magic::init();
    uci::silent(true);
    bool success = test_configuration_snapshot();
    success &= test_independent_cancellation();
    success &= test_result_snapshot();
    success &= test_worker_snapshot();
    success &= test_result_publication();
    return success ? EXIT_SUCCESS : EXIT_FAILURE;
}
