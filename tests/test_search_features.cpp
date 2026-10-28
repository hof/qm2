#include "engine.h"

namespace {
// Italian opening after 3...Nf6: white to move at ply 1, quiet middlegame.
constexpr auto italian = "r1bqkbnr/pppp1ppp/2n5/4p3/2B1P3/5N2/PPPP1PPP/RNBQK2R b KQkq - 3 3";

bool check(bool ok, const char * name) {
    if (!ok) {
        std::cerr << "FAILED: " << name << '\n';
    }
    return ok;
}

std::unique_ptr<search_t> position() {
    auto s = std::make_unique<search_t>(italian);
    move_t move{};
    move.set(&s->brd, "g8f6");
    s->forward(&move, s->brd.gives_check(&move));
    s->tablebase_limit = s->draw_contempt = 0;
    s->stack->in_check = s->brd.in_check();
    s->stack->static_eval = evaluate(s.get());
    return s;
}

bool quiescence_futility() {
    // c4xd5 is an equal exchange: it cannot lift stand pat to a far higher alpha.
    const char * fen = "r3k2r/ppp2ppp/2n1bn2/3p4/2PPp3/4BN2/PPP2PPP/R2QK2R w KQkq - 0 1";
    trans_table::clear();
    auto search = std::make_unique<search_t>(fen);
    search->tablebase_limit = search->draw_contempt = 0;
    const int eval = evaluate(search.get());
    const int alpha = eval + 1000;
    const int value = search->qsearch(alpha, alpha + 1, 0);
    return check(search->brd.to_string() == fen && search->nodes == 0
        && value > eval && value <= alpha,
        "quiescence futility skips captures and retains its fail-low bound");
}

bool tt_cutoff() {
    trans_table::clear();
    auto s = position();
    const int cold = s->pvs(-score::INF, score::INF, 4);
    const U64 work = s->nodes;
    // A repeated non-PV fail-low search makes no moves and returns the stored score.
    const int warm = s->pvs(cold, cold + 1, 4, true);
    return check(s->nodes == work && warm == cold, "an exact TT entry answers a repeated non-PV search");
}
}

int main() {
    magic::init();
    uci::silent(true);
    trans_table::set_size(4);
    bool ok = quiescence_futility();
    ok &= tt_cutoff();
    return ok ? EXIT_SUCCESS : EXIT_FAILURE;
}
