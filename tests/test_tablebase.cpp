#include "engine.h"
#include "tablebase.h"
#include <filesystem>
#include <unordered_set>

namespace {

bool check(const char * message, bool success) {
    if (!success) {
        std::cerr << message << "\n";
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

bool test_probes() {
    board_t board;
    const char * fens[] = {
        "7k/8/8/8/8/8/8/KR6 w - - 0 1",
        "7k/8/8/8/8/8/8/KR6 b - - 0 1",
        "7k/6R1/8/8/8/8/8/K7 b - - 0 1", // defender can capture
        "k7/8/2K5/8/8/8/P7/8 w - - 0 1", // rook-pawn draw
    };
    const int expected[] = {2, -2, 0, 0};
    bool success = true;
    for (int i = 0; i < 4; ++i) {
        board.init(fens[i]);
        success &= check("WDL result differs from known outcome",
            tablebase::probe_wdl(board, 5, 1, 1) == expected[i]);
        success &= check("Probe limit must disable lookup",
            tablebase::probe_wdl(board, 0, 1, 1) == -3);
        success &= check("Probe depth must avoid highest-cardinality leaf probes",
            tablebase::probe_wdl(board, 5, 0, 1) == -3);
    }
    board.init("7k/8/8/8/8/8/8/KR6 w - - 1 1");
    success &= check("WDL must reject nonzero clocks", tablebase::probe_wdl(board, 5, 1, 1) == -3);
    board.init("7k/8/8/8/8/8/8/4K2R w K - 0 1");
    success &= check("Castling rights are outside Syzygy coverage", tablebase::probe_wdl(board, 5, 1, 1) == -3);
    board.init("7k/8/8/8/8/8/8/K6R w - - 0 1"); // wrong side to move while black is checked
    tablebase::root_result_t result;
    success &= check("Invalid royal positions must not enter the decoder",
        tablebase::probe_wdl(board, 5, 1, 1) == -3 && !tablebase::probe_root(board, 5, result));
    board.init("7k/8/8/8/8/8/8/KRN5 w - - 0 1");
    success &= check("Absent material tables must fail gracefully", tablebase::probe_wdl(board, 5, 1, 1) == -3);
    return success;
}

bool test_search() {
    search_t win("7k/8/8/8/8/8/8/KR6 w - - 0 1");
    prepare(win);
    int value = 0;
    bool success = check("A solved WDL outcome must survive a full window without inventing mate",
        win.probe_tablebase(1, value)
        && value == score::TABLEBASE && !score::is_mate(value));
    success &= check("WDL win should cut off a narrow window",
        win.pvs(0, 1, 1) == score::TABLEBASE && win.nodes == 1
        && win.stack->tt_clocks.min == 0 && win.stack->tt_clocks.max == 0);
    auto full_win = std::make_unique<search_t>("7k/8/8/8/8/8/8/KR6 w - - 0 1");
    prepare(*full_win);
    success &= check("Full-window search must retain the proven win",
        full_win->pvs(-score::INF, score::INF, 1) == score::TABLEBASE && full_win->nodes == 1);
    auto full_loss = std::make_unique<search_t>("7k/8/8/8/8/8/8/KR6 b - - 0 1");
    prepare(*full_loss);
    success &= check("Full-window search must retain the proven loss",
        full_loss->pvs(-score::INF, score::INF, 1) == -score::TABLEBASE && full_loss->nodes == 1);
    search_t loss("7k/8/8/8/8/8/8/KR6 b - - 0 1");
    prepare(loss);
    success &= check("WDL loss should cut off a narrow window",
        loss.pvs(-1, 0, 1) == -score::TABLEBASE && loss.nodes == 1);
    search_t drawn("7k/6R1/8/8/8/8/8/K7 b - - 0 1");
    prepare(drawn);
    success &= check("A proven draw must stop full-window search",
        drawn.pvs(-score::INF, score::INF, 1) == 0 && drawn.nodes == 1);
    search_t clock("7k/8/8/8/8/8/8/KR6 w - - 99 1");
    prepare(clock);
    success &= check("A WDL win cannot override a fifty-move draw",
        clock.pvs(-score::INF, score::INF, 1) == 0);
    search_t mate("7k/6Q1/5K2/8/8/8/8/8 b - - 100 1");
    prepare(mate);
    success &= check("Tablebases must preserve checkmate precedence",
        mate.pvs(-score::INF, score::INF, 1) == -score::MATE);
    search_t checked("7k/7R/8/8/8/8/8/K7 b - - 0 1");
    prepare(checked);
    success &= check("Checked nodes still search their legal evasions",
        checked.pvs(-score::INF, score::INF, 1) == 0 && checked.nodes > 1);
    trans_table::clear();
    trans_table::store(win.brd.stack->tt_key, 0, 5, 8, score::TABLEBASE - 5, 0,
        score::LOWERBOUND, 7, {0, 0});
    int move, flag;
    success &= check("Cached tablebase bounds must preserve distance across plies",
        trans_table::retrieve(win.brd.stack->tt_key, 2, 1, value, move, flag, 7, 0)
        && value == score::TABLEBASE - 2);
    return success;
}

bool test_root() {
    bool success = true;
    const char * fens[] = {
        "7k/8/8/8/8/8/8/KR6 w - - 0 1",
        "kr6/8/8/8/8/8/8/7K b - - 0 1",
        "7k/4P3/4K3/8/8/8/8/8 w - - 0 1",
        "k7/8/2K5/8/8/8/P7/8 w - - 0 1",
        "7k/8/8/8/8/8/8/KR6 w - - 98 1",
    };
    const int outcomes[] = {1, 1, 1, 0, 0};
    for (int i = 0; i < 5; ++i) {
        search_t search(fens[i]);
        prepare(search);
        const std::string original = search.brd.to_string();
        search.init_root_moves();
        success &= check("Root DTZ probe must supply a legal, clock-aware move",
            search.tablebase_lookup() && search.result_score == outcomes[i] * score::TABLEBASE
            && search.tablebase_hits == 1 && search.stack->pv_count == 1
            && search.brd.valid(&search.stack->best_move) && search.brd.legal(&search.stack->best_move)
            && search.brd.to_string() == original);
        if (outcomes[i] == 1) {
            move_t chosen = search.stack->best_move;
            search.forward(&chosen, search.brd.gives_check(&chosen));
            tablebase::root_result_t child;
            success &= check("DTZ-selected move must preserve the winning outcome",
                tablebase::probe_root(search.brd, 5, child) && child.outcome == -1);
            search.backward(&chosen);
        }
    }
    board_t board;
    board.init("7k/8/8/8/8/8/8/KR6 w - - 0 1");
    const std::vector<U64> history = {board.stack->repetition_key()};
    search_t repeated("7k/8/8/8/8/8/8/KR6 w - - 8 1", nullptr, nullptr, history);
    prepare(repeated);
    repeated.init_root_moves();
    success &= check("Root shortcut must not ignore reversible game history", !repeated.tablebase_lookup());
    const std::vector<U64> unique_history = {1, 2, 3, 4};
    search_t unique("7k/8/8/8/8/8/8/KR6 w - - 8 1", nullptr, nullptr, unique_history);
    prepare(unique);
    unique.init_root_moves();
    success &= check("Unique reversible history must retain direct DTZ play", unique.tablebase_lookup());
    const std::vector<U64> duplicate_history = {1, 2, 1, 3};
    search_t duplicate("7k/8/8/8/8/8/8/KR6 w - - 8 1", nullptr, nullptr, duplicate_history);
    prepare(duplicate);
    duplicate.init_root_moves();
    success &= check("An earlier repeated position must retain history-aware search", !duplicate.tablebase_lookup());
    repeated.wild = 17;
    success &= check("Standard tables must not affect losers chess", !repeated.tablebase_lookup());
    return success;
}

bool test_conversion() {
    std::string fen = "7k/8/8/8/8/8/8/KR6 w - - 0 1";
    std::unordered_set<U64> seen;
    std::vector<U64> history;
    for (int ply = 0; ply < 100; ++ply) {
        search_t search(fen.c_str(), nullptr, nullptr, history);
        prepare(search);
        if (search.init_root_moves() == 0) {
            return check("DTZ play must end in a win rather than stalemate",
                search.brd.in_check() && search.brd.us() == BLACK);
        }
        if (!check("DTZ play must not repeat a position",
                seen.insert(search.brd.stack->repetition_key()).second)) {
            return false;
        }
        if (!check("DTZ play must keep the winning outcome before the fifty-move limit",
                search.tablebase_lookup() && std::abs(search.result_score) == score::TABLEBASE)) {
            return false;
        }
        move_t chosen = search.stack->best_move;
        history.push_back(search.brd.stack->repetition_key());
        search.forward(&chosen, search.brd.gives_check(&chosen));
        if (search.brd.stack->fifty_count == 0) {
            history.clear();
        }
        fen = search.brd.to_string();
    }
    return check("DTZ play failed to convert a rook win within fifty moves", false);
}

bool test_missing_files() {
    std::string pattern = (std::filesystem::temp_directory_path() / "maxima-syzygy-XXXXXX").string();
    const char * temporary = mkdtemp(pattern.data());
    if (!temporary) {
        return check("Could not create tablebase test directory", false);
    }
    const std::filesystem::path directory(temporary);
    std::filesystem::copy_file(std::filesystem::path(TABLEBASE_FIXTURE_PATH) / "KRvK.rtbw",
        directory / "KRvK.rtbw", std::filesystem::copy_options::overwrite_existing);
    tablebase::set_path(directory.string());
    board_t board;
    board.init("7k/8/8/8/8/8/8/KR6 w - - 0 1");
    tablebase::root_result_t result;
    bool success = check("WDL-only installations must work without claiming DTZ coverage",
        tablebase::probe_wdl(board, 5, 1, 1) == 2 && !tablebase::probe_root(board, 5, result));
    success &= check("Missing directories must leave probing disabled",
        tablebase::set_path((directory / "missing").string()) && tablebase::max_pieces() == 0);
    std::filesystem::remove_all(directory);
    return success;
}

}

int main() {
    magic::init();
    tablebase::set_path(TABLEBASE_FIXTURE_PATH);
    bool success = check("Fixture tables did not initialize", tablebase::max_pieces() == 3);
    success &= test_probes();
    success &= test_search();
    success &= test_root();
    success &= test_conversion();
    success &= test_missing_files();
    tablebase::set_path("");
    return success ? EXIT_SUCCESS : EXIT_FAILURE;
}
