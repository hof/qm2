#include "uci_console.h"
#include "w17/w17_search.h"
#include <limits>

namespace uci {
extern std::string fen;
extern std::vector<U64> position_history;
}

namespace {
bool check(const char * message, bool success) {
    if (!success) std::cerr << message << '\n';
    return success;
}
}

int main() {
    magic::init();
    uci::silent(true);
    bool success = true;
    uci::in("position startpos moves g1f3 g8f6 f3g1 f6g8");
    const auto fen = uci::fen;
    const auto history = uci::position_history;
    success &= check("legal move history must be retained", history.size() == 4);
    for (const char * command : {
            "position", "position nonsense", "position startpos garbage",
            "position startpos moves zzzz", "position startpos moves e2e5",
            "position startpos moves e2e4 e7e5 g1g3", "forward e2e4 e7e5 zzzz",
            "position startpos moves e2e4q", "position startpos moves e2e4 e2e3",
            "position fen 8/8/8/8/8/8/8/8 w - - 0 1",
            "position fen 9/8/8/8/8/8/8/K6k w - - 0 1",
            "position fen 8/8/8/8/8/8/8/K6k w - - 0",
            "position fen 8/8/8/8/8/8/8/K6k w - a9 0 1",
            "position fen 8/8/8/8/8/8/8/K6k w - d6 0 1",
            "position fen 8/8/8/8/8/8/8/K6k w K - 0 1",
            "position fen 8/8/8/8/8/8/8/K6k w - - 2147483648 1",
            "position fen 8/8/8/8/8/8/8/K6k w - - 0 2147483647",
            "position fen 8/8/8/8/8/8/8/Kk6 w - - 0 1"}) {
        uci::in(command);
        success &= check(command, uci::fen == fen && uci::position_history == history);
    }
    uci::in("setoption name Hash value 16");
    for (const char * command : {"setoption bogus Hash value 0",
            "setoption name Hash value -1", "setoption name Hash value nonsense",
            "setoption name Hash value 16garbage", "setoption name Hash value 1025",
            "setoption name Hash value 2147483648"}) {
        uci::in(command);
        success &= check(command, options::get_value("Hash") == 16);
    }
    uci::in("setoption name Ponder value true");
    uci::in("setoption name Ponder value maybe");
    success &= check("invalid boolean must preserve setting", options::get_value("Ponder") == 1);
    uci::in("setoption name Unknown value 123");
    success &= check("unknown option must not mutate sentinel", options::PARAM[0].value == 0);
    const int depth = engine::settings()->max_depth;
    for (const char * command : {"go depth 0", "go depth -1", "go depth 99999",
            "go depth", "go depth 2junk", "go nodes -1", "go nodes 0",
            "go wtime 2147483648", "go movetime -1", "go nodes 18446744073709551616"}) {
        uci::in(command);
        success &= check(command, engine::is_stopped() && engine::settings()->max_depth == depth);
    }
    uci::in("setoption name Wild value losers");
    uci::in("setoption name Wild value antichess");
    success &= check("unknown variant must preserve selection", options::get_value("Wild") == 17);
    uci::in("position fen 7k/8/8/8/8/8/r7/R6K w - - 0 1");
    const auto losers_fen = uci::fen;
    uci::in("forward h1g1");
    success &= check("losers must reject a quiet move when a legal capture exists", uci::fen == losers_fen);
    uci::in("forward a1a2");
    success &= check("losers must accept the compulsory capture", uci::fen != losers_fen);
    uci::in("position fen k3r3/8/8/8/8/3p4/4P3/4K3 w - - 0 1");
    const auto pinned_fen = uci::fen;
    uci::in("forward e2e3");
    success &= check("an illegal pinned capture must not forbid a quiet losers move", uci::fen != pinned_fen);
    uci::in("setoption name Wild value standard");
    uci::in("position fen 4k3/8/8/8/8/8/8/R3K2R w KQ - 0 1 moves e1g1");
    success &= check("castling input must remain supported", uci::fen.find("R4RK1") != std::string::npos);
    uci::in("position fen 7k/8/8/3pP3/8/8/8/K7 w - d6 0 1 moves e5d6");
    success &= check("en passant input must remain supported", uci::fen.find("3P4") != std::string::npos);
    uci::in("position fen 7k/P7/8/8/8/8/8/K7 w - - 0 1 moves a7a8n");
    success &= check("underpromotion input must remain supported", uci::fen.find("N6k") == 0);
    time_manager_t timer;
    const int large_clock = std::numeric_limits<int>::max();
    timer.set(large_clock, 1, large_clock / 2, 0, large_clock);
    success &= check("large legal clocks must produce bounded positive budgets",
        timer.reserved_min() > 0 && timer.reserved_max() > 0
        && timer.reserved_min() <= large_clock && timer.reserved_max() <= large_clock);
    for (bool move_time : {false, true}) {
        game_t limits;
        limits.clock_limit = !move_time;
        limits.movetime_limit = move_time;
        limits.init_tm(true);
        success &= check("explicit zero time must expire immediately", limits.tm.time_is_up());
        game_t snapshot;
        snapshot.copy(&limits);
        success &= check("zero-time limit flags must survive snapshots",
            snapshot.clock_limit == limits.clock_limit && snapshot.movetime_limit == limits.movetime_limit);
    }
    for (int clock : {99, 100, 101, 150}) {
        for (const char * placement : {"7k/5Q2/6K1/8/8/8/p7/R7",
                                      "7k/6Q1/6K1/8/8/8/p7/R7"}) {
            const std::string position = std::string(placement) + " b - - " + std::to_string(clock) + " 1";
            w17_search_t terminal(position.c_str());
            terminal.draw_contempt = 0;
            terminal.stack->in_check = terminal.brd.in_check();
            success &= check("losers mate/stalemate must win before fifty-move draws",
                terminal.pvs(-score::INF, score::INF, 1) == score::MATE);
        }
        const std::string position = "7k/8/8/8/8/8/8/KR6 b - - " + std::to_string(clock) + " 1";
        w17_search_t playable(position.c_str());
        playable.draw_contempt = 0;
        playable.stack->in_check = playable.brd.in_check();
        success &= check("king-only losers win must precede draw", playable.pvs(-score::INF, score::INF, 1) == score::MATE);
        const std::string nonterminal = "7k/7p/8/8/8/8/8/KR6 b - - " + std::to_string(clock) + " 1";
        w17_search_t active(nonterminal.c_str());
        active.stack->in_check = active.brd.in_check();
        success &= check("playable losers position must draw at 100 halfmoves", active.w17_is_draw() == (clock >= 100));
        const std::string checked_position = "6Rk/p7/8/8/8/8/8/K7 b - - " + std::to_string(clock) + " 1";
        w17_search_t checked(checked_position.c_str());
        checked.stack->in_check = checked.brd.in_check();
        success &= check("nonterminal check must not postpone the losers draw threshold",
            checked.w17_is_draw() == (clock >= 100));
    }
    return success ? EXIT_SUCCESS : EXIT_FAILURE;
}
