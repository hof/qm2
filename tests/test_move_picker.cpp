#include "engine.h"
#include <set>

namespace {

bool legality_contract() {
    int rejected = 0;
    for (const char * fen : {
            "rnbqkbnr/pppppppp/8/8/8/8/PPPPPPPP/RNBQKBNR w KQkq - 0 1",
            "r3k2r/p1ppqpb1/bn2pnp1/3PN3/1p2P3/2N2Q1p/PPPBBPPP/R3K2R w KQkq - 0 1",
            "4r2k/8/8/8/8/8/4R3/4K3 w - - 0 1",
            "4r2k/8/8/8/8/8/8/4K3 w - - 0 1",
            "k4r2/8/8/8/8/8/8/4K2R w K - 0 1",
            "7k/8/8/r4pPK/8/8/8/8 w - f6 0 1",
            "7k/P7/8/8/8/8/8/7K w - - 0 1"}) {
        auto search = std::make_unique<search_t>(fen);
        search->stack->in_check = search->brd.in_check();
        const std::string original = search->brd.to_string();
        const U64 key = search->brd.stack->tt_key;
        const U64 context = search->tt_context();
        std::set<int> seen;
        for (auto * candidate = move::first(search.get(), 1); candidate;
                candidate = move::next(search.get(), 1)) {
            move_t expected = *candidate;
            const bool legal = search->brd.legal(&expected);
            if (!seen.insert(candidate->to_int()).second) {
                return false;
            }
            const auto clocks = search->stack->tt_clocks;
            const bool accepted = search->try_forward(candidate, search->brd.gives_check(candidate));
            if (accepted != legal || candidate->en_passant != expected.en_passant
                    || candidate->castle != expected.castle) {
                std::cerr << "legality mismatch: " << fen << ' ' << candidate->to_string() << '\n';
                return false;
            }
            if (accepted) {
                search->backward(candidate);
            } else {
                ++rejected;
                if (search->stack->tt_clocks.min != clocks.min
                        || search->stack->tt_clocks.max != clocks.max) {
                    return false;
                }
            }
            if (search->brd.to_string() != original || search->brd.stack->tt_key != key
                    || search->tt_context() != context || search->stack != search->root_stack) {
                return false;
            }
        }
    }
    return rejected > 0;
}

}

int main() {
    magic::init();
    if (!legality_contract()) {
        std::cerr << "pseudo-legal picker / lazy legality contract failed\n";
        return EXIT_FAILURE;
    }
    return EXIT_SUCCESS;
}
