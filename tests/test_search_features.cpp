// One behaviour check per search feature: switching it must change the same
// search, which must leave the board and score sane.
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

// Nodes of a fixed-depth search of the test position with one feature switched.
// Returns 0 if the search left the board or score in a bad state.
U64 search_nodes(bool search_t::* feature, bool enabled) {
    trans_table::clear();
    auto s = position();
    (*s).*feature = enabled;
    const auto fen = s->brd.to_string();
    const int score = s->pvs(-score::INF, score::INF, 9);
    const bool sane = s->brd.to_string() == fen && !s->stop_all && std::abs(score) < score::TABLEBASE;
    return sane ? s->nodes : 0;
}

// A feature fires when switching it changes the same search. Its direction
// varies: a pruned move can still alter later limits, ordering and the TT.
bool fires(bool search_t::* feature, const char * name) {
    const U64 on = search_nodes(feature, true), off = search_nodes(feature, false);
    return check(on && off && on != off, name);
}

bool quiescence_futility() {
    // c4xd5 is an equal exchange: it cannot lift stand pat to a far higher alpha.
    const char * fen = "r3k2r/ppp2ppp/2n1bn2/3p4/2PPp3/4BN2/PPP2PPP/R2QK2R w KQkq - 0 1";
    U64 nodes[2];
    for (bool enabled : {false, true}) {
        auto s = std::make_unique<search_t>(fen);
        s->tablebase_limit = s->draw_contempt = 0;
        s->ffp_enabled = enabled;
        const int alpha = evaluate(s.get()) + 1000;
        const int value = s->qsearch(alpha, alpha + 1, 0);
        nodes[enabled] = s->brd.to_string() == fen && value <= alpha ? s->nodes : 0;
    }
    return check(nodes[1] < nodes[0], "quiescence futility skips captures that cannot reach alpha");
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
    bool ok = fires(&search_t::beta_pruning, "reverse futility pruning fires");
    ok &= fires(&search_t::razoring_enabled, "razoring fires");
    ok &= fires(&search_t::null_enabled, "null move pruning fires");
    ok &= fires(&search_t::probcut_enabled, "ProbCut fires");
    ok &= fires(&search_t::ffp_enabled, "futility pruning fires");
    ok &= fires(&search_t::lmp_enabled, "late move pruning fires");
    ok &= fires(&search_t::history_pruning_enabled, "history pruning fires");
    ok &= fires(&search_t::main_see_enabled, "SEE pruning fires");
    ok &= fires(&search_t::lmr_enabled, "late move reductions fire");
    ok &= quiescence_futility();
    ok &= tt_cutoff();
    return ok ? EXIT_SUCCESS : EXIT_FAILURE;
}
