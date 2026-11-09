#include "uci_console.h"
#include "w17/w17_search.h"
#include <limits>

namespace uci {
extern std::string fen;
extern std::vector<U64> position_history;
}

namespace {
bool check(const char * message, bool success) {
    if (!success) {
        std::cerr << message << '\n';
    }
    return success;
}
}

namespace {
bool policy_cache_controls() {
    bool success = true;
    const U64 main_key = 0x1234567812345678ULL;
    const U64 q_key = 0x9876543298765432ULL;
    auto seed = [&]() {
        trans_table::clear();
        trans_table::store(main_key, 0, 0, 4, 123, 456, score::EXACT);
        trans_table::store(q_key, 0, 0, 0, 234, 567, score::LOWERBOUND);
    };
    auto retained = [&]() {
        tt_hit_t main_hit, q_hit;
        return trans_table::probe(main_key, 0, 0, main_hit) && main_hit.score == 123
            && trans_table::probe(q_key, 0, 0, q_hit) && q_hit.score == 234;
    };
    auto cleared = [&]() {
        tt_hit_t hit;
        return !trans_table::probe(main_key, 0, 0, hit) && !trans_table::probe(q_key, 0, 0, hit);
    };
    for (const char * name : {"DrawContempt", "SyzygyProbeLimit", "SyzygyProbeDepth"}) {
        const int saved = options::get_value(name);
        const std::string command = std::string("setoption name ") + name + " value ";
        seed();
        uci::in(command + std::to_string(saved));
        success &= check("unchanged policy must preserve both TT modes", retained());
        uci::in(command + "invalid");
        success &= check("invalid policy must preserve both TT modes", retained());
        uci::in(command + std::to_string(saved == 0 ? 1 : 0));
        success &= check(name, cleared());
        seed();
        uci::in(command + std::to_string(saved));
        success &= check("restoring policy must clear both TT modes", cleared());
    }
    for (const char * value : {"losers", "standard"}) {
        seed();
        uci::in(std::string("setoption name Wild value ") + value);
        success &= check("variant changes must clear both TT modes", cleared());
    }
    seed();
    uci::in("setoption name Move Overhead value 100");
    success &= check("clock overhead must preserve TT", retained());
    uci::in("setoption name Move Overhead value 50");
    trans_table::clear();
    return success;
}
}

int main() {
    magic::init();
    uci::silent(true);
    bool success = policy_cache_controls();
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
            "position fen 7k/pppppppp/p7/8/8/P7/PPPPPPPP/K7 w - - 0 1",
            "position fen 7k/pppppppp/8/8/8/P7/PPPPPPPP/K7 w - - 0 1",
            "position fen 7k/pppppppp/p7/8/8/8/PPPPPPPP/K7 w - - 0 1",
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
    success &= check("move overhead defaults to 50 ms", options::get_value("Move Overhead") == 50);
    for (int overhead : {0, 50, 200, 5000}) {
        uci::in("setoption name Move Overhead value " + std::to_string(overhead));
        game_t limits;
        limits.clock_limit = true;
        limits.white_time = limits.black_time = 1000;
        limits.moves_left = 2;
        limits.init_tm(true);
        success &= check("configured overhead reduces both clock budgets down to the search floor",
            limits.tm.reserved_min() == std::max(50, 100 - overhead)
            && limits.tm.reserved_max() == std::max(100, 500 - overhead));
        limits.movetime_limit = true;
        limits.max_time_per_move = 300;
        limits.init_tm(true);
        success &= check("explicit movetime retains its requested budget",
            limits.tm.reserved_min() == 300 && limits.tm.reserved_max() == 300);
    }
    for (const char * value : {"-1", "5001", "oops", "2147483648"}) {
        uci::in(std::string("setoption name Move Overhead value ") + value);
        success &= check("invalid overhead preserves the previous value",
            options::get_value("Move Overhead") == 5000);
    }
    {
        time_manager_t normal, longer, low_clock;
        normal.set(60000, 60000, 0, 0, 0, 0);
        longer.set(60000, 60000, 0, 0, 0, 0, 175);
        success &= check("time factor extends both budgets",
            longer.reserved_min() == normal.reserved_min() * 175 / 100
            && longer.reserved_max() == normal.reserved_max() * 175 / 100);
        low_clock.set(100, 100, 1000, 1000, 0, 0, 200);
        success &= check("time factor stays inside the existing clock limit",
            low_clock.reserved_max() <= 100 && low_clock.reserved_min() <= low_clock.reserved_max());
    }
    uci::in("setoption name Move Overhead value 50");
    success &= check("server lag defaults to 0 ms", options::get_value("Server Lag") == 0);
    {
        time_manager_t no_lag, lagged, slow_increment;
        no_lag.set(60000, 60000, 2000, 2000, 0, 0);
        lagged.set(60000, 60000, 2000, 2000, 0, 0, 100, 16, 300);
        success &= check("server lag comes off what the increment gives back",
            lagged.reserved_min() < no_lag.reserved_min() && lagged.reserved_max() < no_lag.reserved_max());
        slow_increment.set(10000, 10000, 100, 100, 0, 0, 100, 16, 300);
        success &= check("a lag above the increment still leaves a real search inside half the clock",
            slow_increment.reserved_min() >= time_man::MIN_SEARCH_TIME && slow_increment.reserved_max() <= 5000);
    }
    {
        time_manager_t period_timer;
        for (int remaining : {0, 1, 100, 999, 1000, 3999, 4000, 7338, 21363}) {
            for (int overhead : {0, 50, 100, 200, 5000}) {
                period_timer.set(remaining, remaining, 0, 0, 1, overhead);
                const int expected_max = std::max(0, remaining
                    - std::max(overhead, std::min(1000, remaining / 20)));
                success &= check("last period move reserves clock time, scaled for low clocks",
                    period_timer.reserved_max() == expected_max
                    && period_timer.reserved_min() >= 0
                    && period_timer.reserved_min() <= expected_max);
            }
        }
        period_timer.set(21363, 19741, 0, 0, 1, 100);
        success &= check("reported move-40 clock leaves one second for replenishment",
            period_timer.reserved_max() == 20363);
        period_timer.set(7338, 308343, 0, 0, 1, 100);
        success &= check("reported move-80 clock, far behind, spends at most half of it",
            period_timer.reserved_max() == 3569);
        period_timer.set(1000, 1000, 10000, 10000, 1, 100);
        success &= check("last period move cannot spend increment before receiving it",
            period_timer.reserved_max() == 900);
        period_timer.set(5000, 5000, 0, 0, 1, 50);
        success &= check("five-second clock never reserves a fixed second",
            period_timer.reserved_max() == 4750);
        period_timer.set(24000, 24000, 0, 0, 20, 100);
        success &= check("mid-period allocation spreads the clock over the assumed moves",
            period_timer.reserved_min() == 757 && period_timer.reserved_max() == 3328);
        int period_clock = 300000, first_flat = 0, last_flat = 0;
        for (int to_go = 40; to_go > 1; to_go--) {
            period_timer.set(period_clock, period_clock, 0, 0, to_go, 0);
            first_flat = to_go == 24 ? period_timer.reserved_min() : first_flat;
            last_flat = period_timer.reserved_min();
            period_clock -= last_flat;
        }
        success &= check("a 40-move period keeps the budget flat from 24 moves to go",
            first_flat > 5000 && std::abs(last_flat - first_flat) <= 2);
        time_manager_t short_game, long_game;
        short_game.set(300000, 300000, 0, 0, 0, 0, 100, 16);
        long_game.set(300000, 300000, 0, 0, 0, 0, 100, 32);
        time_manager_t level, ahead, behind, far_behind;
        level.set(140000, 140000, 0, 0, 0, 0);
        ahead.set(140000, 40000, 0, 0, 0, 0);
        behind.set(140000, 172000, 0, 0, 0, 0);
        far_behind.set(140000, 900000, 0, 0, 0, 0);
        success &= check("a clock lead adds a sixteenth to the budget and an eighth to the cap",
            ahead.reserved_min() == level.reserved_min() + 6250 && ahead.reserved_max() == level.reserved_max() + 12500);
        success &= check("a clock deficit takes half as much, never more than half",
            behind.reserved_min() == level.reserved_min() - 1000 && behind.reserved_max() == level.reserved_max() - 2000
            && far_behind.reserved_min() == level.reserved_min() - level.reserved_min() / 2
            && far_behind.reserved_max() == level.reserved_max() - level.reserved_max() / 2);
        success &= check("TimeMoves sets the budget to clock/2M and the cap to clock/(M/2)",
            short_game.reserved_min() == 9375 && short_game.reserved_max() == 37500
            && long_game.reserved_min() == 4687 && long_game.reserved_max() == 18750);
        time_manager_t increment_game;
        increment_game.set(300000, 300000, 3000, 3000, 0, 0, 100, 16);
        success &= check("an increment spends the clock as if the game were decided in two thirds of M",
            increment_game.reserved_min() == 14062 + 3000 && increment_game.reserved_max() == 56250 + 6000);
        period_timer.set(120000, 120000, 0, 0, 0, 100);
        const int budget_above = period_timer.reserved_max();
        period_timer.set(119000, 119000, 0, 0, 0, 100);
        success &= check("sudden-death budget has no step at two minutes",
            period_timer.reserved_max() > budget_above * 9 / 10);
    }
    uci::in("setoption name Ponder value true");
    uci::in("setoption name Ponder value maybe");
    success &= check("invalid boolean must preserve setting", options::get_value("Ponder") == 1);
    uci::in("setoption name Unknown value 123");
    success &= check("unknown option must not mutate sentinel", options::PARAM[0].value == 0);

    const int saved_depth = engine::settings()->max_depth;
    engine::settings()->max_depth = 7;
    uci::in("learn");
    success &= check("removed legacy tuner command must preserve UCI state",
        engine::settings()->max_depth == 7 && uci::fen == fen
        && uci::position_history == history);
    engine::settings()->max_depth = saved_depth;

    success &= check("Chess960 must not be an available option",
        std::string(options::get_option("UCI_Chess960")->key).empty());
    std::vector<int> option_values;
    for (const auto & option : options::PARAM) option_values.push_back(option.value);
    for (const char * value : {"true", "1", "false", "nonsense"}) {
        uci::in(std::string("setoption name UCI_Chess960 value ") + value);
        for (int i = 0; i <= options::length; ++i) {
            success &= check("unsupported Chess960 must preserve every option",
                options::PARAM[i].value == option_values[i]);
        }
        success &= check("unsupported Chess960 must preserve the position and history",
            uci::fen == fen && uci::position_history == history);
    }
    game_t saved_settings;
    saved_settings.copy(engine::settings());
    engine::settings()->max_depth = 4;
    engine::settings()->max_nodes = 123;
    engine::settings()->white_time = 456;
    engine::settings()->search_moves = {"a2a3"};
    game_t previous_settings;
    previous_settings.copy(engine::settings());
    for (const char * command : {"go mate 1", "go mate", "go mate 0",
            "go depth 2 mate 1", "go mate 1 nodes 1",
            "go searchmoves e2e4 mate 1 depth 2",
            "go depth 2 searchmoves h2h3 mate 1 ponder", "go infinite mate 1"}) {
        uci::in(command);
        const auto * settings = engine::settings();
        success &= check(command, engine::is_stopped()
            && settings->max_depth == previous_settings.max_depth
            && settings->max_nodes == previous_settings.max_nodes
            && settings->white_time == previous_settings.white_time
            && settings->ponder == previous_settings.ponder
            && settings->infinite == previous_settings.infinite
            && settings->search_moves == previous_settings.search_moves
            && uci::fen == fen && uci::position_history == history);
    }
    engine::settings()->copy(&saved_settings);
    const int depth = engine::settings()->max_depth;
    for (const char * command : {"go depth 0", "go depth -1", "go depth 99999",
            "go depth", "go depth 2junk", "go nodes -1", "go nodes 0",
            "go wtime 2147483648", "go movetime -1", "go nodes 18446744073709551616"}) {
        uci::in(command);
        success &= check(command, engine::is_stopped() && engine::settings()->max_depth == depth);
    }
    uci::in("setoption name Wild value losers");
    uci::in("position fen 7k/pppppppp/p7/8/8/P7/PPPPPPPP/K7 w - - 0 1");
    success &= check("losers must also reject unsupported pawn counts atomically",
        uci::fen == fen && uci::position_history == history);
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
    uci::in("position fen 7k/pppppppp/8/8/8/8/PPPPPPPP/K7 w - - 0 1");
    success &= check("eight pawns per side must remain supported",
        uci::fen == "7k/pppppppp/8/8/8/8/PPPPPPPP/K7 w - - 0 1");
    uci::in("position fen 7k/Pppppppp/8/8/8/8/1PPPPPPP/K7 w - - 0 1 moves a7a8q");
    success &= check("promotion from eight pawns must remain supported", uci::fen.find("Q6k") == 0);
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
    game_t received_limits;
    received_limits.clock_limit = true;
    received_limits.white_time = 100;
    received_limits.black_time = 100;
    received_limits.moves_left = 1;
    received_limits.go_started = std::chrono::steady_clock::now() - std::chrono::milliseconds(200);
    game_t received_snapshot;
    received_snapshot.copy(&received_limits);
    received_snapshot.init_tm(true);
    success &= check("Command handling and setup must consume the received clock budget",
        received_snapshot.tm.time_is_up() && received_snapshot.tm.elapsed() >= 200);
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
