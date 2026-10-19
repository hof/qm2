// Test-only bridge for independent Python rule/state comparisons. No UCI extension.
#include "search.h"
#include "w17/w17_search.h"
#include <algorithm>
#include <memory>
#include <sstream>
#include <stdexcept>
#include <vector>

namespace {

void require(bool condition, const char * message) {
    if (!condition) throw std::runtime_error(message);
}

std::string state(board_t & board) {
    std::ostringstream out;
    out << board.to_string() << ' ' << board.ply << ' ' << board.root_ply;
    for (U64 value : board.bb) out << ' ' << value;
    for (int value : board.matrix) out << ' ' << value;
    require(board.stack == board._stack + board.ply, "board stack pointer");
    for (int i = 0; i <= board.ply; ++i) {
        const auto & entry = board._stack[i];
        out << ' ' << entry.enpassant_sq << ' ' << entry.castling_flags
            << ' ' << entry.fifty_count << ' ' << entry.wtm << ' ' << entry.legal_enpassant
            << ' ' << entry.tt_key;
    }
    return out.str();
}

void check_rebuilt(board_t & board, bool losers) {
    auto rebuilt = std::make_unique<board_t>();
    rebuilt->init(board.to_string().c_str());
    const U64 variant_key = losers ? hash::codes[13][17] : 0;
    for (int i = 0; i <= BPIECES; ++i) require(board.bb[i] == rebuilt->bb[i], "rebuilt bitboard");
    for (int i = 0; i < 64; ++i) require(board.matrix[i] == rebuilt->matrix[i], "rebuilt matrix");
    require(board.stack->tt_key == (rebuilt->stack->tt_key ^ variant_key), "rebuilt position hash");
    require(board.stack->repetition_key() == (rebuilt->stack->repetition_key() ^ variant_key), "rebuilt repetition hash");
    require(board.stack->legal_enpassant == rebuilt->stack->legal_enpassant, "rebuilt en passant legality");
}

std::vector<move_t> legal_moves(search_t & search) {
    search.stack->in_check = search.brd.in_check();
    search.stack->tt_move.clear();
    search.stack->killer[0].clear();
    search.stack->killer[1].clear();
    std::vector<move_t> moves;
    for (move_t * move = move::first(&search, 1); move; move = move::next(&search, 1)) {
        if (search.wild == 0 && !search.brd.legal(move)) {
            continue;
        }
        moves.push_back(*move);
    }
    return moves;
}

void probe(const std::string & line) {
    // variant<TAB>root FEN<TAB>space-separated legal history
    const auto first = line.find('\t');
    const auto second = line.find('\t', first + 1);
    require(first != std::string::npos && second != std::string::npos, "probe input fields");
    const bool losers = line.substr(0, first) == "losers";
    const auto fen = line.substr(first + 1, second - first - 1);
    std::unique_ptr<search_t> search = losers
        ? std::unique_ptr<search_t>(new w17_search_t(fen.c_str()))
        : std::make_unique<search_t>(fen.c_str());
    auto & board = search->brd;
    const auto original = state(board);
    std::vector<move_t> history;
    std::vector<std::string> snapshots;
    std::istringstream input(line.substr(second + 1));
    std::string text;
    while (input >> text) {
        auto moves = legal_moves(*search);
        auto found = std::find_if(moves.begin(), moves.end(), [&](move_t & move) { return move.to_string() == text; });
        require(found != moves.end(), "history move missing");
        snapshots.push_back(state(board));
        history.push_back(*found);
        board.forward(&history.back());
        ++search->stack;
        check_rebuilt(board, losers);
    }
    const auto before = state(board);
    auto moves = legal_moves(*search);
    require(before == state(board), "move generation changed board/history");
    std::ostringstream out;
    out << board.to_string() << '|' << board.stack->repetition_key() << '|';
    for (int i = 0; i <= board.ply; ++i) out << board._stack[i].repetition_key() << ',';
    out << '|';
    for (auto & move : moves) {
        board.forward(&move);
        check_rebuilt(board, losers);
        out << move.to_string() << ':' << board.to_string() << ';';
        board.backward(&move);
        require(before == state(board), "child undo changed board/history");
    }
    while (!history.empty()) {
        board.backward(&history.back());
        --search->stack;
        require(snapshots.back() == state(board), "history undo changed board/history");
        snapshots.pop_back();
        history.pop_back();
    }
    require(original == state(board), "root restoration");
    require(search->stack == search->root_stack, "search stack restoration");
    std::cout << out.str() << '\n';
}
}

int main(int argc, char ** argv) {
    magic::init();
    std::string line;
    try {
        if (argc == 2 && std::string(argv[1]) == "--self-test") {
            for (const std::string variant : {"standard", "losers"}) {
                for (const std::string fen : {
                    "rnbqkbnr/pppppppp/8/8/8/8/PPPPPPPP/RNBQKBNR w KQkq - 0 1",
                    "4k3/3p4/8/8/8/8/8/R3K2R w KQ - 99 1",
                    "4k3/P6p/8/8/8/8/8/4K3 w - - 0 1",
                    "1r2k3/P6p/8/8/8/8/8/4K3 w - - 0 1",
                    "4k3/7p/8/3pP3/8/8/8/4K3 w - d6 0 1",
                    "k7/7p/8/r4pPK/8/8/8/8 w - f6 0 1"}) {
                    line = variant + "\t" + fen + "\t";
                    probe(line);
                }
                line = variant + "\t4k1n1/8/8/8/8/8/8/4K1N1 w - - 0 1\t"
                    "g1f3 g8f6 f3g1 f6g8 g1f3 g8f6 f3g1 f6g8";
                probe(line);
            }
        } else {
            require(argc == 1, "probe arguments");
            while (std::getline(std::cin, line)) probe(line);
        }
    } catch (const std::exception & error) {
        std::cerr << error.what() << "\ninput: " << line << '\n';
        return EXIT_FAILURE;
    }
    return EXIT_SUCCESS;
}
