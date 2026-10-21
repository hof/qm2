#include "engine.h"
#include "progress.h"
#include "w17/w17_search.h"
#include <cstring>
#include <limits>
#include <bit>

namespace {
constexpr auto position = "rnbqkbnr/pppppppp/8/8/8/8/PPPPPPPP/RNBQKBNR w KQkq - 0 1";

bool check(bool value, const char * message) {
    if (!value) {
        std::cerr << message << '\n';
    }
    return value;
}

bool feature_keys_match(const board_t & board) {
    U64 pawn = 0, nonpawn[2] = {}, minor = 0, major = 0;
    for (int sq = 0; sq < 64; ++sq) {
        const int piece = board.matrix[sq];
        if (!piece) {
            continue;
        }
        const U64 code = hash::codes[piece][sq];
        const int type = (piece - 1) % WKING + 1;
        if (type == WPAWN) {
            pawn ^= code;
        } else {
            nonpawn[piece > WKING] ^= code;
            if (type == WKNIGHT || type == WBISHOP || type == WKING) {
                minor ^= code;
            }
            if (type == WROOK || type == WQUEEN || type == WKING) {
                major ^= code;
            }
        }
    }
    return pawn == board.stack->pawn_key && minor == board.stack->minor_key
        && major == board.stack->major_key && nonpawn[0] == board.stack->nonpawn_key[0]
        && nonpawn[1] == board.stack->nonpawn_key[1];
}

bool key_tree(search_t & search, int depth) {
    bool ok = feature_keys_match(search.brd);
    if (!depth) {
        return ok;
    }
    // Includes rejected candidates: undo must restore every feature domain too.
    for (auto * move = move::first(&search, depth); move; move = move::next(&search, depth)) {
        if (search.try_forward(move, search.brd.gives_check(move))) {
            ok &= key_tree(search, depth - 1);
            search.forward();
            ok &= feature_keys_match(search.brd);
            search.backward();
            search.backward(move);
        }
        ok &= feature_keys_match(search.brd);
    }
    return ok;
}

bool rich_keys_contract() {
    bool ok = true;
    for (const char * fen : {position,
            "r3k2r/p1ppqpb1/bn2pnp1/3PN3/1p2P3/2N2Q1p/PPPBBPPP/R3K2R w KQkq - 0 1",
            "r3k2r/8/8/8/8/8/8/R3K2R b KQkq - 0 1",
            "7k/P7/8/8/8/8/7p/K7 w - - 0 1",
            "7k/P7/8/8/8/8/7p/K7 b - - 0 1",
            "7k/8/8/r4pPK/8/8/8/8 w - f6 0 1",
            "4r2k/8/8/8/8/8/4R3/4K3 w - - 0 1"}) {
        auto search = std::make_unique<search_t>(fen);
        search->stack->in_check = search->brd.in_check();
        ok &= key_tree(*search, 2);
        search->brd.do_flip();
        ok &= feature_keys_match(search->brd);
        search->brd.do_flip();
        ok &= feature_keys_match(search->brd);
    }
    return check(ok, "incremental feature keys, special moves, null/undo or flip");
}

bool rich_learning_contract() {
    bool ok = true;
    for (bool black : {false, true}) {
        for (int policy = 0; policy < 4; ++policy) {
            auto search = std::make_unique<search_t>(black
                ? "rnbqkbnr/pppppppp/8/8/8/8/PPPPPPPP/RNBQKBNR b KQkq - 0 1" : position);
            search->piece_correction_enabled = policy & 1;
            search->continuation_correction_enabled = policy & 2;
            const char * white_moves[] = {"g1f3", "g8f6", "b1c3", "b8c6"};
            const char * black_moves[] = {"g8f6", "g1f3", "b8c6", "b1c3"};
            move_t moves[4];
            for (int i = 0; i < 4; ++i) {
                ok &= search->correction_context_keys()[1] == 0;
                moves[i].set(&search->brd, black ? black_moves[i] : white_moves[i]);
                search->forward(&moves[i], false);
            }
            const auto context = search->correction_context_keys();
            const auto & last = moves[3];
            for (int i = 0; i < 2; ++i) {
                const auto & older = moves[i == 0 ? 2 : 0];
                const U64 expected = hash::codes[last.piece][last.tsq]
                    ^ std::rotl(hash::codes[older.piece][older.tsq], 1);
                ok &= context[i] == ((policy & 2) ? expected : 0);
            }
            // A null sentinel at any intermediate ply invalidates every older pair.
            for (int gap = 1; gap <= 4; ++gap) {
                const auto saved = (search->stack - gap)->current_move;
                (search->stack - gap)->current_move.clear();
                const auto keys = search->correction_context_keys();
                ok &= (keys[0] != 0) == ((policy & 2) && gap > 2) && keys[1] == 0;
                (search->stack - gap)->current_move = saved;
            }
            search->stack->best_move.set(&search->brd, black ? "e7e6" : "e2e3");
            search->stack->eval_result = 0;
            search->update_correction_history(1024, score::EXACT, 8);
            ok &= search->correction_value() == 8 + ((policy & 1) ? 32 : 0) + ((policy & 2) ? 12 : 0);
            // A bound on the wrong side of the fresh aggregate cannot train any domain.
            auto before = std::make_unique<correction_history_t>(*search->correction_history);
            search->update_correction_history(search->correction_value(), score::LOWERBOUND, 8);
            ok &= std::memcmp(before.get(), search->correction_history.get(), sizeof(*before)) == 0;
            const int value = search->correction_value();
            search->correction_history_enabled = false;
            ok &= search->corrected_static_eval(100) == 100;
            search->correction_history_enabled = true;
            for (int clock : {0, 89, 90, 99, 100}) {
                search->brd.stack->fifty_count = clock;
                ok &= search->corrected_static_eval(100) == 100 + damp_rule50(value, clock);
            }
            search->brd.stack->fifty_count = 0;
            search->forward();
            ok &= search->correction_context_keys() == std::array<U64, 2>{};
            move_t after_null;
            after_null.set(&search->brd, black ? "e2e3" : "e7e6");
            search->forward(&after_null, false);
            ok &= search->correction_context_keys() == std::array<U64, 2>{};
            search->backward(&after_null);
            search->backward();
            ok &= search->correction_value() == value;
            for (int sign : {-1, 1}) {
                auto & tables = *search->correction_history;
                const bool side = search->brd.us();
                tables.entries[side][search->brd.stack->pawn_key & 16383] = sign * tables.limit;
                const U64 features[] = {search->brd.stack->nonpawn_key[0], search->brd.stack->nonpawn_key[1],
                    search->brd.stack->minor_key, search->brd.stack->major_key};
                for (int i = 0; i < 4; ++i) {
                    tables.pieces[i][side][features[i] & 16383] = sign * tables.limit;
                }
                for (int i = 0; i < 2; ++i) {
                    if (context[i]) {
                        tables.continuation[i][side][context[i] & 16383] = sign * tables.limit;
                    }
                }
                ok &= search->correction_value() == sign * (128 + ((policy & 1) ? 512 : 0)
                    + ((policy & 2) ? 192 : 0));
                constexpr int limit = score::TABLEBASE - MAX_PLY - 1;
                ok &= search->corrected_static_eval(sign * score::MATE) == sign * limit;
            }
            for (int i = 3; i >= 0; --i) {
                search->backward(&moves[i]);
            }
            ok &= search->correction_context_keys() == std::array<U64, 2>{};
        }
    }
    // Moving a nonpawn preserves pawn correction identity but changes its own group.
    auto search = std::make_unique<search_t>(position);
    const auto old_keys = *search->brd.stack;
    move_t knight;
    knight.set(&search->brd, "g1f3");
    search->forward(&knight, false);
    ok &= search->brd.stack->pawn_key == old_keys.pawn_key
        && search->brd.stack->nonpawn_key[0] != old_keys.nonpawn_key[0]
        && search->brd.stack->nonpawn_key[1] == old_keys.nonpawn_key[1]
        && search->brd.stack->minor_key != old_keys.minor_key
        && search->brd.stack->major_key == old_keys.major_key;
    // Promotion contexts identify the piece left on the destination, for either color.
    for (bool black : {false, true}) {
        auto promoted = std::make_unique<search_t>(black
            ? "7k/8/8/8/8/8/p7/7K b - - 0 1" : "7k/P7/8/8/8/8/8/7K w - - 0 1");
        move_t promotion, reply;
        promotion.set(&promoted->brd, black ? "a2a1n" : "a7a8n");
        promoted->forward(&promotion, false);
        reply.set(&promoted->brd, black ? "h1g1" : "h8g8");
        promoted->forward(&reply, false);
        const U64 expected = hash::codes[reply.piece][reply.tsq]
            ^ std::rotl(hash::codes[promotion.promotion][promotion.tsq], 1);
        ok &= promoted->correction_context_keys()[0] == expected;

    }
    return check(ok, "rich correction domains, continuation context, policy or fresh-error bounds");
}

bool table_contract() {
    correction_history_t history;
    const U64 key = 123;
    bool ok = history.correction(true, key) == 0;
    history.update(true, key, 8, 1024);
    ok &= history.entries[1][key] == 1024 && history.correction(true, key) == 8;
    history.update(true, key, 8, -512);
    ok &= history.entries[1][key] == 1024 - 512 - 1024 * 512 / 16384;
    ok &= history.correction(false, key) == 0 && history.correction(true, key + 1) == 0;
    ok &= history.correction(true, key + correction_history_t::size) == history.correction(true, key);
    for (int error : {32768, -32768, std::numeric_limits<int>::max(), std::numeric_limits<int>::min()}) {
        for (int i = 0; i < 1000; ++i) {
            history.update(true, key, 1000, error);
            ok &= std::abs(int(history.entries[1][key])) <= correction_history_t::limit;
        }
    }
    return check(ok, "correction table arithmetic, bounds or indexing");
}

bool evaluation_contract() {
    bool ok = true;
    for (int clock : {0, 9, 10, 89, 90, 99, 100}) {
        std::string fen = position;
        fen.replace(fen.size() - 3, 1, std::to_string(clock));
        auto search = std::make_unique<search_t>(fen.c_str());
        search->correction_history_enabled = false;
        const int raw = evaluate(search.get());
        const int neural = search->neural_static_score();
        auto & entry = search->correction_history->entries[search->brd.us()][search->brd.stack->pawn_key & 16383];
        for (int stored : {-16384, -1024, 1024, 16384}) {
            entry = stored;
            search->correction_history_enabled = true;
            const int corrected = evaluate(search.get());
            ok &= corrected == raw + damp_rule50(stored / 128, clock);
            ok &= search->stack->eval_result == raw && search->neural_static_score() == neural;
            // A cached frame must see subsequent table changes immediately.
            search->correction_history_enabled = false;
            ok &= evaluate(search.get()) == raw;
        }
        search->correction_history_enabled = true;
        constexpr int bound = score::TABLEBASE - MAX_PLY - 1;
        ok &= search->corrected_static_eval(score::MATE) == bound;
        ok &= search->corrected_static_eval(-score::MATE) == -bound;
    }
    auto search = std::make_unique<search_t>("4r2k/8/8/8/8/8/8/4K3 w - - 0 1");
    search->stack->in_check = true;
    ok &= evaluate(search.get()) == score::INVALID;
#ifdef MAXIMA_NNUE_SEARCH_AUDIT
    search = std::make_unique<search_t>(position);
    search->correction_history->update(search->brd.us(), search->brd.stack->pawn_key, 8, 1024);
    for (bool fixed : {false, true}) {
        search->nnue_audit.fixed_horizon = fixed;
        search->nnue_audit.aligned_quiescence = !fixed;
        ok &= evaluate(search.get()) == search->stack->eval_result;
    }
#endif
    return check(ok, "raw caches, corrected reads, checks, clock damping or clamp");
}

bool update_contract() {
    bool ok = true;
    for (int bound : {score::EXACT, score::LOWERBOUND, score::UPPERBOUND}) {
        for (int error : {-1024, 0, 1024}) {
            for (int guard = 0; guard < 16; ++guard) {
                auto search = std::make_unique<search_t>(position);
                search->stack->best_move.set(&search->brd, "g1f3");
                search->stack->eval_result = 0;
                int depth = 8;
                int value = error;
                switch (guard) {
                    case 1: search->correction_history_enabled = false; break;
                    case 2: search->stop_all = true; break;
                    case 3: search->stack->in_check = true; break;
                    case 4: search->iid_active = true; break;
                    case 5: search->verification_mode = verification_mode_t::null_move; break;
                    case 6: search->verification_mode = verification_mode_t::excluded_move; break;
                    case 7: search->stack->excluded_move = 123; break;
                    case 8: search->stack->best_move.capture = BPAWN; break;
                    case 9: search->stack->best_move.promotion = WQUEEN; break;
                    case 10: search->stack->best_move.clear(); break;
                    case 11: search->stack->eval_result = score::INVALID; break;
                    case 12: value = score::TABLEBASE; break;
                    case 13: search->brd.stack->fifty_count = 90; break;
                    case 14: depth = 0; break;
                    case 15: search->stack->repetition_start = 2; break;
                }
                const bool eligible = !guard && error && (bound == score::EXACT
                    || (bound == score::LOWERBOUND && error > 0)
                    || (bound == score::UPPERBOUND && error < 0));
                search->update_correction_history(value, bound, depth);
                const int actual = search->correction_history->entries[search->brd.us()][search->brd.stack->pawn_key & 16383];
                ok &= actual == (eligible ? error : 0);
                const U64 keys[] = {search->brd.stack->nonpawn_key[0], search->brd.stack->nonpawn_key[1],
                    search->brd.stack->minor_key, search->brd.stack->major_key};
                for (int i = 0; i < 4; ++i) {
                    ok &= search->correction_history->pieces[i][search->brd.us()][keys[i] & 16383]
                        == (eligible ? error : 0);
                }
            }
        }
    }
    // Children can change the bucket after parent entry: train the fresh error.
    auto search = std::make_unique<search_t>(position);
    search->stack->best_move.set(&search->brd, "g1f3");
    search->stack->eval_result = 0;
    search->correction_history->update(search->brd.us(), search->brd.stack->pawn_key, 8, 1024);
    search->piece_correction_enabled = search->continuation_correction_enabled = false;
    search->update_correction_history(8, score::EXACT, 8);
    ok &= search->correction_history->correction(search->brd.us(), search->brd.stack->pawn_key) == 8;
    return check(ok, "correction update eligibility, direction or fresh-error contract");
}

// Independent child scores isolate the real parent's bound and update paths.
class probe_t : public search_t {
public:
    int child_value = 0;
    bool cancel = false;
    probe_t() : search_t(position) {
        null_enabled = beta_pruning = ffp_enabled = iid_enabled = lmr_enabled = false;
        upcoming_repetition_enabled = false;
        tablebase_limit = 0;
    }
    int pvs(int, int, int, bool = false) override {
        if (!brd.legal()) {
            std::abort();
        }
        stop_all = cancel;
        return child_value;
    }
};

bool search_contract() {
    bool ok = true;
    for (int mode = 0; mode < 6; ++mode) {
        trans_table::clear();
        auto search = std::make_unique<probe_t>();
        const auto before = search->brd.to_string();
        const U64 key = search->brd.stack->tt_key;
        const int eval = evaluate(search.get());
        const int value = eval + (mode == 1 ? -1024 : 1024);
        search->child_value = -value;
        search->cancel = mode == 3;
        search->iid_active = mode == 4;
        search->verification_mode = mode == 5 ? verification_mode_t::excluded_move : verification_mode_t::none;
        const int alpha = mode == 1 ? eval : mode == 2 ? value - 1 : eval;
        const int beta = mode == 2 ? value + 1 : alpha + 1;
        search->search_t::pvs(alpha, beta, 1);
        const int entry = search->correction_history->entries[search->brd.us()][search->brd.stack->pawn_key & 16383];
        ok &= mode < 3 ? (mode == 1 ? entry < 0 : entry > 0) : entry == 0;
        ok &= search->brd.to_string() == before && search->brd.stack->tt_key == key
            && search->stack == search->root_stack && search->brd.ply == 0;
    }
    // Cached, legal terminal and qsearch results never learn.
    for (const char * fen : {position, "7k/6Q1/5K2/8/8/8/8/8 b - - 0 1", "7k/5Q2/6K1/8/8/8/8/8 b - - 0 1"}) {
        for (int depth : {0, 2}) {
            trans_table::clear();
            auto search = std::make_unique<search_t>(fen);
            search->tablebase_limit = 0;
            if (fen == position && depth) {
                trans_table::store(search->brd.stack->tt_key, 0, 0, depth, 123, 0,
                    score::EXACT, search->tt_context());
            }
            search->pvs(-score::INF, score::INF, depth);
            const auto empty = std::make_unique<correction_history_t>();
            ok &= std::memcmp(empty.get(), search->correction_history.get(), sizeof(empty)) == 0;
        }
    }
    return check(ok, "real PVS learning/nonlearning paths or restoration");
}

bool worker_contract() {
    bool ok = true;
    for (bool enabled : {false, true}) {
        options::get_option("CorrectionHistory")->value = enabled;
        auto search = std::make_unique<search_t>(position);
        options::get_option("CorrectionHistory")->value = !enabled;
        ok &= search->correction_history_enabled == enabled;
    }
    for (const char * option : {"PieceCorrection", "ContinuationCorrection"}) {
        for (bool enabled : {false, true}) {
            options::get_option(option)->value = enabled;
            auto search = std::make_unique<search_t>(position);
            options::get_option(option)->value = !enabled;
            ok &= (std::string(option) == "PieceCorrection" ? search->piece_correction_enabled
                : search->continuation_correction_enabled) == enabled;
        }
        options::get_option(option)->value = 1;
    }
    options::get_option("CorrectionHistory")->value = 1;
    auto previous = std::make_unique<search_t>(position);
    previous->correction_history->update(true, 123, 8, 1024);
    previous->correction_history->pieces[2][0][42] = 1234;
    previous->correction_history->continuation[1][1][42] = -1234;
    auto * address = previous->correction_history.get();
    auto next = std::make_unique<search_t>(position);
    next->take_history(*previous);
    ok &= !previous->correction_history && next->correction_history.get() == address;
    ok &= next->correction_history->pieces[2][0][42] == 1234
        && next->correction_history->continuation[1][1][42] == -1234;
    auto fresh = std::make_unique<search_t>(position);
    ok &= fresh->correction_history->correction(true, 123) == 0;
    // A different model snapshot discards errors, independently of move history.
    next->evaluation_network.reset();
    fresh->take_history(*next);
    ok &= fresh->correction_history->correction(true, 123) == 0 && next->correction_history;
    auto losers = std::make_unique<w17_search_t>(position);
    ok &= !losers->correction_history_enabled && !losers->correction_history
        && !losers->piece_correction_enabled && !losers->continuation_correction_enabled;
    return check(ok, "worker option, transfer, model compatibility or royal policy");
}
}

int main() {
    magic::init();
    trans_table::set_size(16);
    return table_contract() && evaluation_contract() && update_contract()
        && search_contract() && worker_contract() && rich_keys_contract() && rich_learning_contract() ? EXIT_SUCCESS : EXIT_FAILURE;
}
