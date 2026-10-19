#include "engine.h"
#include <vector>

namespace {
struct window_t { int alpha, beta, depth; };

// Keep the production root driver and aspiration loop. Controlled child scores
// certify bounds and retry depth without depending on evaluator accuracy.
class root_probe_t : public search_t {
public:
    std::vector<window_t> windows;
    std::vector<int> scores;
    std::string bad_first_move;
    int calls = 0;
    int cancel_call = 0;

    explicit root_probe_t(const char * fen) : search_t(fen) {
        aspiration_enabled = true;
        init_root_moves();
    }

    int pvs(int alpha, int beta, int depth) override {
        ++calls;
        ++nodes;
        stack->pv_count = 0;
        if (beta - alpha > 1) {
            windows.push_back({-beta, -alpha, depth + 1});
        }
        if (calls == cancel_call) {
            stop_all = true;
        }
        const auto move = (stack - 1)->current_move.to_string();
        const size_t index = std::min(windows.size() - 1, scores.size() - 1);
        const int value = move == bad_first_move ? -100 : scores[index];
        return -value;
    }
};

bool check(bool ok, const char * message) {
    if (!ok) {
        std::cerr << message << '\n';
    }
    return ok;
}

bool restored(root_probe_t & search, const char * fen, U64 key) {
    return search.brd.ply == 0 && search.brd.to_string() == fen
        && search.brd.stack->tt_key == key && search.stack == search.root_stack;
}
}

int main() {
    magic::init();
    uci::silent(true);
    trans_table::set_size(4);
    trans_table::disable();
    options::get_option("OwnBook")->value = 0;
    bool ok = true;
    for (const char * fen : {
            "rnbqkbnr/pppppppp/8/8/8/8/PPPPPPPP/RNBQKBNR w KQkq - 0 1",
            "rnbqkbnr/pppppppp/8/8/8/8/PPPPPPPP/RNBQKBNR b KQkq - 0 1"}) {
        for (int value : {-180, -50, 50, 180}) {
            auto search = std::make_unique<root_probe_t>(fen);
            search->scores = {value};
            const U64 key = search->brd.stack->tt_key;
            ok &= check(search->aspiration(4, 0) == value, "aspiration score differs");
            const size_t attempts = std::abs(value) == 50 ? 2 : 3;
            ok &= check(search->windows.size() == attempts, "aspiration attempt count differs");
            const int bounds[] = {50, 150, 350};
            for (size_t i = 0; i < search->windows.size(); ++i) {
                const auto & window = search->windows[i];
                ok &= check(window.depth == 4, "aspiration reduced retry depth");
                ok &= check(window.alpha == (value < 0 ? -bounds[i] : -50)
                    && window.beta == (value < 0 ? 50 : bounds[i]), "aspiration changed the nonfailed bound");
            }
            ok &= check(restored(*search, fen, key), "aspiration did not restore root");
        }
        auto unstable = std::make_unique<root_probe_t>(fen);
        unstable->scores = {100, -200, 0};
        ok &= check(unstable->aspiration(4, 0) == 0 && unstable->windows.size() == 3,
            "alternating aspiration failures differ");
        ok &= check(unstable->windows[2].alpha == -250 && unstable->windows[2].beta == 150,
            "alternating failure narrowed the root window");

        auto alternative = std::make_unique<root_probe_t>(fen);
        alternative->scores = {100};
        alternative->root.sort_moves(&alternative->stack->best_move);
        alternative->bad_first_move = alternative->root.moves[0].move.to_string();
        ok &= check(alternative->pvs_root(-50, 50, 4) == 100 && alternative->calls >= 2
            && alternative->stack->best_move.to_string() != alternative->bad_first_move,
            "root failed low before searching an improving alternative");

        auto partial = std::make_unique<root_probe_t>(fen);
        partial->scores = {-100};
        partial->cancel_call = 2;
        const U64 partial_key = partial->brd.stack->tt_key;
        partial->pvs_root(-50, 50, 4);
        ok &= check(partial->stop_all && partial->calls == 2
            && partial->result_source == result_source_t::partial
            && partial->result_flag == 0 && restored(*partial, fen, partial_key),
            "interrupted root fail-low reported an unsupported score");

        for (int previous : {0, score::WIN + 1}) {
            auto decisive = std::make_unique<root_probe_t>(fen);
            decisive->scores = {score::WIN + 1};
            ok &= check(decisive->aspiration(4, previous) == score::WIN + 1,
                "decisive aspiration score differs");
            const auto & last = decisive->windows.back();
            ok &= check(last.alpha == -score::INF && last.beta == score::INF && last.depth == 4
                && decisive->windows.size() == (previous ? 1 : 2), "decisive score did not use full window");
        }
        for (int depth : {3, 4}) {
            auto disabled = std::make_unique<root_probe_t>(fen);
            disabled->scores = {100};
            disabled->aspiration_enabled = depth == 3;
            ok &= check(disabled->aspiration(depth, 0) == 100 && disabled->windows.size() == 1
                && disabled->windows[0].alpha == -score::INF && disabled->windows[0].beta == score::INF,
                "disabled or shallow aspiration used narrow window");
        }
        auto cancelled = std::make_unique<root_probe_t>(fen);
        cancelled->scores = {180};
        cancelled->cancel_call = 1;
        const U64 key = cancelled->brd.stack->tt_key;
        cancelled->aspiration(4, 0);
        ok &= check(cancelled->stop_all && cancelled->calls == 1
            && restored(*cancelled, fen, key), "cancelled aspiration retried or changed root");
    }
    trans_table::enable();
    return ok ? 0 : 1;
}
