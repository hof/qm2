// One minimal trigger per search feature: a position and window where the
// feature must fire, plus one principled case where it must not.
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

move_t make(search_t & s, const char * text) {
    move_t move{};
    move.set(&s.brd, text);
    return move;
}

bool reverse_futility() {
    auto s = position();
    int value = 0;
    bool ok = s->reverse_futility(0, 2, 1000, value) && value >= 0;
    ok &= !s->reverse_futility(1000, 2, 0, value);
    return check(ok, "reverse futility cuts a node far above beta");
}

bool razoring() {
    auto s = position();
    int value = 0;
    // Quiescence confirms the node cannot reach a far higher alpha.
    bool ok = s->razor_node(1000, 1001, 1, -1000, value) && value <= 1000;
    ok &= !s->razor_node(-1000, -999, 1, 1000, value);
    return check(ok && !s->stop_all && s->brd.ply == 1, "razoring drops a hopeless shallow node");
}

bool futility() {
    auto s = position();
    auto quiet = make(*s, "d2d3");
    bool ok = s->prune_futility(quiet, 1, 1, 0, 1000, 1001, 0, 0);
    ok &= !s->prune_futility(quiet, 1, 0, 0, 1000, 1001, 0, 0);
    return check(ok, "futility skips a quiet move that cannot reach alpha");
}

bool late_move_pruning() {
    auto s = position();
    auto quiet = make(*s, "d2d3");
    bool ok = s->prune_quiet(quiet, 1, 60, 1, 0);
    ok &= !s->prune_quiet(quiet, 1, 0, 1, 0);
    return check(ok, "late quiet moves are pruned at shallow depth");
}

bool see_pruning() {
    auto s = position();
    // Nxe5 loses the knight to Nxe5.
    auto losing = make(*s, "f3e5");
    bool ok = s->prune_main_see(losing, 1, 1, 0);
    ok &= !s->prune_main_see(losing, 1, 0, 0);
    return check(ok, "a losing capture is pruned by SEE");
}

bool late_move_reduction() {
    auto s = position();
    auto quiet = make(*s, "d2d3");
    bool ok = s->reduction(10, 20, s->reduction_estimate(10, 20, quiet, false, true, 0)) > 0;
    ok &= s->reduction(10, 0, s->reduction_estimate(10, 0, quiet, true, false, 0)) == 0;
    return check(ok, "late quiet moves are reduced, the first move is not");
}

bool quiescence_futility() {
    // c4xd5 is an equal exchange: it cannot lift stand pat to a far higher alpha.
    const char * fen = "r3k2r/ppp2ppp/2n1bn2/3p4/2PPp3/4BN2/PPP2PPP/R2QK2R w KQkq - 0 1";
    auto s = std::make_unique<search_t>(fen);
    s->tablebase_limit = s->draw_contempt = 0;
    const int alpha = evaluate(s.get()) + 1000;
    s->qsearch(alpha, alpha + 1, 0);
    return check(s->pruned_nodes > 0 && s->brd.to_string() == fen,
        "quiescence futility skips captures that cannot reach alpha");
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
    bool ok = reverse_futility();
    ok &= razoring();
    ok &= futility();
    ok &= late_move_pruning();
    ok &= see_pruning();
    ok &= late_move_reduction();
    ok &= quiescence_futility();
    ok &= tt_cutoff();
    return ok ? EXIT_SUCCESS : EXIT_FAILURE;
}
