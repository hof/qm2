#include "engine.h"
#include "repetition.h"
#include <array>
#include <cstring>
#include <random>

namespace {

const char * cycle_fen = "6nk/8/8/8/8/8/8/KR4N1 w - - 0 1";

bool check(const char * message, bool success) {
    if (!success) {
        std::cerr << message << '\n';
    }
    return success;
}

class probe_search_t : public search_t {
public:
    using search_t::search_t;
    using search_t::raise_repetition_alpha;
    void supply_history(std::vector<U64> keys) {
        _position_history = std::move(keys);
        stack->repetition_start = -int(_position_history.size());
        init_tt_history();
    }
};

void prepare(search_t & search) {
    search.draw_contempt = 0;
    search.null_enabled = search.beta_pruning = search.ffp_enabled = false;
    search.lmr_enabled = false;
    search.tablebase_limit = 0;
    search.stack->in_check = search.brd.in_check();
}

void play(search_t & search, const char * text) {
    move_t move{};
    move.set(&search.brd, text);
    if (!search.brd.valid(&move) || !search.brd.legal(&move)) {
        std::cerr << "illegal fixture move: " << text << '\n';
        std::abort();
    }
    search.forward(&move, search.brd.gives_check(&move));
    search.nodes = 0; // fixture moves are setup, not search
}

void reach_escape(search_t & search) {
    play(search, "g1f3");
    play(search, "g8f6");
    play(search, "f3g1");
}

std::string sparse_fen(int piece, int from, int own_king, int other_king) {
    const bool us = piece <= WKING;
    std::array<int, 64> squares{};
    squares[own_king] = KING[us];
    squares[other_king] = KING[!us];
    squares[from] = piece;
    const char * glyphs = " PNBRQKpnbrqk";
    std::string fen;
    for (int rank = 7; rank >= 0; --rank) {
        int empty = 0;
        for (int file = 0; file < 8; ++file) {
            const int pc = squares[rank * 8 + file];
            if (!pc) {
                ++empty;
                continue;
            }
            if (empty) {
                fen += char('0' + empty);
                empty = 0;
            }
            fen += glyphs[pc];
        }
        if (empty) {
            fen += char('0' + empty);
        }
        if (rank) {
            fen += '/';
        }
    }
    return fen + (us ? " w - - 3 1" : " b - - 3 1");
}

bool table_roundtrip() {
    int edges = 0;
    board_t board;
    for (int piece : {WKNIGHT, WBISHOP, WROOK, WQUEEN, WKING,
            BKNIGHT, BBISHOP, BROOK, BQUEEN, BKING}) {
        for (int from = 0; from < 64; ++from) {
            const U64 attacks = piece == WKNIGHT || piece == BKNIGHT ? KNIGHT_MOVES[from]
                : piece == WBISHOP || piece == BBISHOP ? BISHOP_MOVES[from]
                : piece == WROOK || piece == BROOK ? ROOK_MOVES[from]
                : piece == WQUEEN || piece == BQUEEN ? QUEEN_MOVES[from] : KING_MOVES[from];
            for (int to = from + 1; to < 64; ++to) {
                if (!(attacks & BIT(to))) {
                    continue;
                }
                ++edges;
                for (bool reverse : {false, true}) {
                    const int source = reverse ? to : from;
                    const int target = reverse ? from : to;
                    bool tested = false;
                    for (int own = 0; own < 64 && !tested; ++own) {
                        const bool king = piece == WKING || piece == BKING;
                        if ((king && own != source) || (!king && (own == from || own == to))) {
                            continue;
                        }
                        for (int other = 0; other < 64 && !tested; ++other) {
                            if (other == from || other == to || other == own
                                    || (KING_MOVES[own] & BIT(other))) {
                                continue;
                            }
                            board.init(sparse_fen(piece, source, own, other).c_str());
                            move_t expected{}, actual{};
                            expected.set(piece, source, target);
                            if (!board.valid(&expected) || !board.legal(&expected)) {
                                continue;
                            }
                            U64 delta = hash::codes[piece][from] ^ hash::codes[piece][to];
                            HASH_STM(delta);
                            if (!repetition::find_move(board, delta, actual) || !actual.equals(&expected)) {
                                return check("reversible move table must retain every edge in both directions", false);
                            }
                            tested = true;
                        }
                    }
                    if (!tested) {
                        return false;
                    }
                }
            }
        }
    }
    return check("reversible table must cover 3668 edges", edges == 3668);
}

bool synthetic_legality() {
    struct case_t { const char * fen; const char * move; bool expected; };
    for (const auto & test : {
            case_t{"4r2k/8/8/8/8/8/4R3/4K3 w - - 3 1", "e2f2", false},
            case_t{"4r2k/8/8/8/8/8/8/1N2K3 w - - 3 1", "b1c3", false},
            case_t{"4r2k/8/8/8/8/2B5/8/4K3 w - - 3 1", "c3e5", true},
            case_t{"4r2k/8/8/8/8/8/8/4K3 w - - 3 1", "e1d1", true},
            case_t{"4r2k/8/8/8/8/8/8/4K3 w - - 3 1", "e1e2", false},
            case_t{"7k/8/8/8/P7/8/8/R3K3 w - - 3 1", "a1a8", false},
            case_t{"r6k/8/8/8/8/8/8/4K3 w - - 3 1", "a8a7", false},
            case_t{"7k/8/8/8/8/8/P7/R3K3 w - - 3 1", "a1a2", false},
            case_t{"7k/8/8/8/8/8/8/4K2R w K - 3 1", "e1d1", false},
            case_t{"7k/8/8/8/8/8/8/4K2R w K - 3 1", "h1h2", false},
            case_t{"7k/8/8/3pP3/8/8/8/4K3 w - d6 3 1", "e1d1", false},
            case_t{"7k/8/8/3p4/8/8/8/4K3 w - d6 3 1", "e1d1", true}}) {
        auto search = std::make_unique<probe_search_t>(test.fen);
        move_t move{}, witness{};
        move.set(&search->brd, test.move);
        U64 target = search->brd.stack->repetition_key()
            ^ hash::codes[move.piece][move.ssq] ^ hash::codes[move.piece][move.tsq];
        HASH_STM(target);
        search->supply_history({target, 123, 456});
        const std::string fen = search->brd.to_string();
        const auto frame = *search->stack;
        const auto board_frame = *search->brd.stack;
        const int distance = search->upcoming_repetition(witness);
        if (!check(test.move, (distance == 3) == test.expected)
                || !check("lookup must restore active board and search frame",
                    search->brd.to_string() == fen && search->stack == search->root_stack
                    && std::memcmp(search->stack, &frame, sizeof(frame)) == 0
                    && std::memcmp(search->brd.stack, &board_frame, sizeof(board_frame)) == 0)) {
            return false;
        }
    }
    return true;
}

bool search_bounds() {
    bool success = true;
    for (bool qsearch : {false, true}) {
        for (int contempt : {-37, 0, 37}) {
            trans_table::clear();
            auto search = std::make_unique<probe_search_t>(cycle_fen);
            prepare(*search);
            reach_escape(*search);
            search->draw_contempt = contempt;
            const int draw = search->draw_score();
            const auto fen = search->brd.to_string();
            const U64 key = search->brd.stack->tt_key;
            const U64 context = search->tt_context();
            const int value = qsearch ? search->qsearch(draw - 1, draw, -1)
                : search->pvs(draw - 1, draw, 3);
            success &= check("legal escape must raise alpha and cut off before children",
                value == draw && search->nodes == 0 && search->stack->best_move.to_string() == "f6g8"
                && search->stack->pv_count == 1 && search->stack->tt_clocks.min == 3
                && search->brd.to_string() == fen && search->brd.stack->tt_key == key);
            int cached = 0, hint = 0, flag = 0;
            success &= check("early proof must not store an ordinary TT result",
                !trans_table::retrieve(key, search->brd.ply, 1, cached, hint, flag,
                    context, 3, nullptr, qsearch ? tt_mode_t::quiescence_tactical : tt_mode_t::main_search)
                && hint == 0);
            move_t witness{};
            int alpha = draw;
            success &= check("alpha at draw must skip optional detection",
                !search->raise_repetition_alpha(alpha, draw + 1, witness) && !witness.piece);
            alpha = draw - 1;
            search->upcoming_repetition_enabled = false;
            success &= check("off switch must skip optional detection",
                !search->raise_repetition_alpha(alpha, draw, witness) && alpha == draw - 1);
            search->upcoming_repetition_enabled = true;
            search->stack->excluded_move = 1;
            success &= check("a singular search must not use a repetition escape",
                !search->raise_repetition_alpha(alpha, draw, witness));
            search->stack->excluded_move = 0;
            // Mandatory draw adjudication stays active with the feature disabled.
            play(*search, "f6g8");
            search->upcoming_repetition_enabled = false;
            success &= check("normal repetition must remain mandatory above draw alpha",
                search->is_draw() && search->pvs(draw + 1, draw + 2, 2) == search->draw_score()
                && search->qsearch(draw + 1, draw + 2, -1) == search->draw_score());
        }
    }
    // A quiet draw is outside tactical qsearch's ordinary move domain.
    auto disabled = std::make_unique<probe_search_t>(cycle_fen);
    prepare(*disabled);
    reach_escape(*disabled);
    disabled->q_tt_enabled = false;
    disabled->upcoming_repetition_enabled = false;
    const int missed = disabled->qsearch(-score::INF, score::INF, -1);
    disabled->upcoming_repetition_enabled = true;
    const int saved = disabled->qsearch(-score::INF, score::INF, -1);
    success &= check("disabled tactical control must expose the quiet escape it misses",
        missed < -100 && saved == 0 && disabled->stack->best_move.to_string() == "f6g8");
    std::cout << "quiet escape control off=" << missed << " on=" << saved << '\n';
    trans_table::clear();
    auto better = std::make_unique<probe_search_t>("6nk/8/8/8/8/8/8/KR4N1 b - - 0 1");
    prepare(*better);
    play(*better, "g8f6");
    play(*better, "g1f3");
    play(*better, "f6g8");
    success &= check("an available repetition must not prevent finding a better move",
        better->pvs(-1000, 1000, 2) > 100);
    return success;
}

bool cache_clock_and_stop() {
    bool success = true;
    for (int cached : {-500, 500}) {
        trans_table::clear();
        auto search = std::make_unique<probe_search_t>(cycle_fen);
        prepare(*search);
        reach_escape(*search);
        trans_table::store(search->brd.stack->tt_key, 0, 3, 4, cached, 0,
            score::EXACT, search->tt_context());
        // Cached score cutoffs apply only to non-PV windows.
        const int result = cached > 0 ? search->pvs(cached - 1, cached, 2, true)
            : search->pvs(-1000, 1000, 2);
        success &= check("TT return must respect the proven draw floor without a false PV",
            result == std::max(0, cached)
            && (cached > 0 ? search->stack->pv_count == 0 && !search->stack->best_move.piece
                : search->nodes > 0 && search->stack->pv_count > 0));
    }
    trans_table::clear();
    auto search = std::make_unique<probe_search_t>(cycle_fen);
    prepare(*search);
    reach_escape(*search);
    const auto key = search->brd.stack->tt_key;
    const auto context = search->tt_context();
    success &= check("wide search must retain the proven draw as its best move",
        search->pvs(-1000, 1000, 1) == 0 && search->stack->best_move.to_string() == "f6g8"
        && search->stack->pv_count >= 1);
    int value = 0, move = 0, flag = 0;
    clock_range_t clocks{};
    success &= check("completed result must retain proof's minimum reversible clock",
        trans_table::retrieve(key, 3, 1, value, move, flag, context, 3, &clocks)
        && value == 0 && clocks.min == 3
        && !trans_table::retrieve(key, 3, 1, value, move, flag, context, 2) && move != 0);
    move_t last{};
    last.set(WKNIGHT, f3, g1);
    search->backward(&last);
    success &= check("undo must propagate proof's minimum clock to its parent",
        search->stack->tt_clocks.min == 2 && search->brd.stack->fifty_count == 2);

    // A better stand pat must not present the drawing witness as its winning PV.
    auto winning = std::make_unique<probe_search_t>("6nk/8/8/8/8/8/8/KR4N1 b - - 0 1");
    prepare(*winning);
    play(*winning, "g8f6");
    play(*winning, "g1f3");
    play(*winning, "f6g8");
    success &= check("stand pat above draw must clear the witness PV",
        winning->qsearch(-1000, 1000, -1) > 100 && winning->stack->pv_count == 0);
    trans_table::clear();
    trans_table::store(winning->brd.stack->tt_key, 0, 3, 4, -500, 0,
        score::EXACT, winning->tt_context());
    success &= check("a contradictory TT score must not hide a better-than-draw continuation",
        winning->pvs(-1000, 1000, 2) > 100 && winning->nodes > 0);

    search_control_t control;
    auto cancelled = std::make_unique<probe_search_t>(cycle_fen, nullptr, &control);
    prepare(*cancelled);
    reach_escape(*cancelled);
    control.stop_requested = true;
    success &= check("cancellation must take precedence over a repetition proof",
        cancelled->pvs(-1, 0, 2) == -1 && cancelled->stop_all
        && cancelled->stack->pv_count == 0 && !cancelled->stack->best_move.piece);
    // The root still uses normal move search rather than taking a descendant shortcut.
    search->reset_stack();
    move_t witness{};
    int alpha = -1000;
    success &= check("actual root must not use the early descendant mechanism",
        !search->raise_repetition_alpha(alpha, 1000, witness));
    options::get_option("UpcomingRepetition")->value = 0;
    auto snapshot = std::make_unique<probe_search_t>(cycle_fen);
    options::get_option("UpcomingRepetition")->value = 1;
    success &= check("worker feature snapshot must survive a global option change",
        !snapshot->upcoming_repetition_enabled);
    return success;
}

bool history_boundaries() {
    auto search = std::make_unique<probe_search_t>(cycle_fen);
    prepare(*search);
    move_t witness{};
    bool success = check("fresh FEN must not invent missing history", !search->upcoming_repetition(witness));
    play(*search, "g1f3");
    play(*search, "g8f6");
    search->reset_stack();
    play(*search, "f3g1");
    success &= check("a pre-root match must supply a descendant's escape", search->upcoming_repetition(witness) == 3);
    search->forward();
    success &= check("null boundary must block earlier candidate cycles", !search->upcoming_repetition(witness));
    search->backward();
    success &= check("undo null must restore candidate cycle", search->upcoming_repetition(witness) == 3);
    search->brd.stack->fifty_count = 2;
    success &= check("halfmove clock must bound the candidate scan", !search->upcoming_repetition(witness));
    search->brd.stack->fifty_count = 3;
    search->supply_history({123});
    success &= check("partial history must bound the scan", !search->upcoming_repetition(witness));
    return success;
}

bool oracle_comparison() {
    std::mt19937 random(20261019);
    int positions = 0, hits = 0;
    auto search = std::make_unique<probe_search_t>(cycle_fen);
    prepare(*search);
    for (int sample = 0; sample < 320; ++sample) {
        if (sample < 3) {
            play(*search, sample == 0 ? "g1f3" : sample == 1 ? "g8f6" : "f3g1");
        }
        const std::string fen = search->brd.to_string();
        const auto frame = *search->stack;
        move_t witness{};
        const int distance = search->upcoming_repetition(witness);
        move::list_t moves;
        move::gen_quiet_moves(&search->brd, &moves);
        bool oracle = false;
        std::vector<move_t> legal;
        for (auto * move = moves.first; move != moves.last; ++move) {
            if (move->piece == WPAWN || move->piece == BPAWN || !search->brd.legal(move)) {
                continue;
            }
            legal.push_back(*move);
            search->brd.forward(move);
            oracle |= search->is_repetition();
            search->brd.backward(move);
        }
        if (!check("lookup must agree with exhaustive legal quiet-move repetition oracle", bool(distance) == oracle)
                || !check("oracle/lookup must restore parent", search->brd.to_string() == fen
                    && std::memcmp(search->stack, &frame, sizeof(frame)) == 0)) {
            std::cerr << fen << '\n';
            return false;
        }
        ++positions;
        hits += distance != 0;
        if (sample >= 2) {
            if (legal.empty() || sample % 60 == 59) {
                search->init(cycle_fen, nullptr, nullptr, {}, 0);
                prepare(*search);
            } else {
                auto move = legal[random() % legal.size()];
                search->forward(&move, search->brd.gives_check(&move));
                if (search->brd.ply >= 24) {
                    search->reset_stack();
                }
            }
        }
    }
    std::cout << "legal repetition oracle: " << positions << " positions, " << hits << " hits\n";
    return hits > 0;
}

}

int main() {
    magic::init();
    uci::silent(true);
    return table_roundtrip() && synthetic_legality() && search_bounds()
        && cache_clock_and_stop() && history_boundaries() && oracle_comparison() ? EXIT_SUCCESS : EXIT_FAILURE;
}
