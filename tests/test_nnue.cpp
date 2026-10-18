#include "engine.h"
#include "nnue.h"
#include "nnue_affine.h"
#include "w17/w17_eval.h"
#include "w17/w17_search.h"
#include <bit>
#include <random>
#include <filesystem>
#include <fstream>
#include <limits>

namespace {
bool check(bool condition, const char * message) {
    if (!condition) std::cerr << message << '\n';
    return condition;
}

void put_u32(std::vector<uint8_t> & bytes, size_t offset, uint32_t value) {
    for (int i = 0; i < 4; ++i) bytes[offset + i] = (value >> (8 * i)) & 255;
}

void write_file(const std::filesystem::path & path, const std::vector<uint8_t> & bytes) {
    std::ofstream output(path, std::ios::binary);
    output.write(reinterpret_cast<const char *>(bytes.data()), bytes.size());
    if (!output) throw std::runtime_error("Cannot write test network");
}

bool check_activation_clipping() {
    std::array<std::array<int16_t, 256>, 2> values;
    // Cover every signed int16 value, both perspectives, and asymmetric lane patterns.
    for (unsigned base = 0; base < 65536; base += 256) {
        for (unsigned i = 0; i < 256; ++i) {
            values[0][i] = std::bit_cast<int16_t>(static_cast<uint16_t>(base + i));
            values[1][i] = std::bit_cast<int16_t>(static_cast<uint16_t>((base + i) ^ 0xaaaa));
        }
        for (bool us : {false, true}) {
            const auto scalar = nnue::detail::clip_activations<false>(values, us);
            const auto simd = nnue::detail::clip_activations(values, us);
            for (size_t i = 0; i < 512; ++i) {
                const int expected = std::clamp<int>(values[i < 256 ? us : !us][i % 256], 0, 127);
                if (scalar[i] != expected || simd[i] != expected)
                    return check(false, "Activation clipping changed a value, lane or perspective");
            }
        }
    }
    return true;
}

U64 reference_passers(const board_t & board) {
    U64 result = 0;
    for (int from = 8; from < 56; ++from) {
        const int piece = board.matrix[from];
        if (piece != WPAWN && piece != BPAWN) {
            continue;
        }
        const int step = piece == WPAWN ? 1 : -1;
        bool passed = true;
        for (int rank = RANK(from) + step; rank >= 0 && rank < 8; rank += step) {
            for (int file = MAX(0, FILE(from) - 1); file <= MIN(7, FILE(from) + 1); ++file) {
                const int ahead = board.matrix[rank * 8 + file];
                if (ahead == (piece == WPAWN ? BPAWN : WPAWN)
                        || (file == FILE(from) && ahead == piece)) {
                    passed = false;
                }
            }
        }
        if (passed) {
            result |= BIT(from);
        }
    }
    return result;
}

bool check_search_metadata(search_t & search) {
    auto & board = search.brd;
    bool endgame = false;
    for (bool us : {false, true}) {
        const int pawns = board.count(PAWN[us]);
        const int knights = board.count(KNIGHT[us]);
        const int bishops = board.count(BISHOP[us]);
        endgame |= pawns <= 1 || (!board.count(ROOK[us]) && !board.count(QUEEN[us])
            && (knights + bishops < 2 || (knights == 2 && bishops == 0)));
    }
    search.stack->metadata = search_metadata(board);
    return check(search.stack->metadata.endgame == endgame
        && search.stack->metadata.passed_pawns == reference_passers(board),
        "Search pruning metadata must match independent material and square-scan references");
}

bool check_metadata_positions() {
    std::mt19937_64 random(20261017);
    // Compare independent search metadata on every legal child, null and undo.
    for (const char * fen : {
            "rnbqkbnr/pppppppp/8/8/8/8/PPPPPPPP/RNBQKBNR w KQkq - 0 1",
            "r3k2r/ppp2ppp/8/8/8/8/PPP2PPP/R3K2R w KQkq - 0 1",
            "4k3/8/8/3pP3/8/8/8/4K3 w - d6 0 1",
            "1r5k/Pppppppp/8/8/8/8/1PPPPPPP/K7 w - - 0 1",
            "4r1k1/8/8/8/8/8/3B4/4K3 w - - 0 1",
            "7k/8/5KQ1/8/8/8/8/8 w - - 99 1"}) {
        auto search = std::make_unique<search_t>(fen);
        if (!check_search_metadata(*search)) {
            return false;
        }
        const U64 parent_passers = search->stack->metadata.passed_pawns;
        const bool parent_endgame = search->stack->metadata.endgame;
        std::vector<move_t> moves;
        for (auto * move = move::first(search.get(), 1); move; move = move::next(search.get(), 1)) {
            moves.push_back(*move);
        }
        for (auto & move : moves) {
            search->forward(&move, search->brd.gives_check(&move));
            if (!check_search_metadata(*search)) {
                return false;
            }
            search->backward(&move);
            if (!check(search->stack->metadata.passed_pawns == parent_passers
                    && search->stack->metadata.endgame == parent_endgame,
                    "A child metadata update must not change its parent")) {
                return false;
            }
        }
        if (!search->brd.in_check()) {
            search->forward();
            if (!check_search_metadata(*search)) {
                return false;
            }
            search->backward();
        }
        if (!check_search_metadata(*search)) {
            return false;
        }
    }
    // Actual legal play checks retained metadata with
    // evolving material, king moves and both sides to move.
    for (int game = 0; game < 24; ++game) {
        auto search = std::make_unique<search_t>(
            "rnbqkbnr/pppppppp/8/8/8/8/PPPPPPPP/RNBQKBNR w KQkq - 0 1");
        std::vector<move_t> played;
        for (int ply = 0; ply < 100; ++ply) {
            if (!check_search_metadata(*search)) {
                return false;
            }
            std::vector<move_t> moves;
            for (auto * move = move::first(search.get(), 1); move; move = move::next(search.get(), 1)) {
                moves.push_back(*move);
            }
            if (moves.empty()) {
                break;
            }
            played.push_back(moves[random() % moves.size()]);
            auto & move = played.back();
            search->forward(&move, search->brd.gives_check(&move));
        }
        while (!played.empty()) {
            search->backward(&played.back());
            played.pop_back();
            if (!check_search_metadata(*search)) {
                return false;
            }
        }
    }
    // Arbitrary disjoint pawn sets stress edge files, doubled/tripled pawns
    // and same-rank enemies. No evaluation is needed for these synthetic boards.
    auto synthetic = std::make_unique<search_t>("7k/8/8/8/8/8/8/K7 w - - 0 1");
    for (int trial = 0; trial < 20000; ++trial) {
        for (int square = 8; square < 56; ++square) {
            synthetic->brd.matrix[square] = EMPTY;
        }
        synthetic->brd.bb[WPAWN] = synthetic->brd.bb[BPAWN] = 0;
        for (bool us : {false, true}) {
            for (int count = int(random() % 9); count > 0; --count) {
                const int square = 8 + random() % 48;
                if (synthetic->brd.matrix[square] == EMPTY) {
                    synthetic->brd.matrix[square] = PAWN[us];
                    synthetic->brd.bb[PAWN[us]] |= BIT(square);
                }
            }
        }
        U64 occupied = synthetic->brd.bb[WPAWN] | synthetic->brd.bb[BPAWN]
            | synthetic->brd.bb[WKING] | synthetic->brd.bb[BKING];
        for (bool us : {false, true}) {
            for (int piece : {KNIGHT[us], BISHOP[us], ROOK[us], QUEEN[us]}) {
                synthetic->brd.bb[piece] = 0;
                for (int count = int(random() % 5); count > 0; --count) {
                    const int square = random() % 64;
                    if (!(occupied & BIT(square))) {
                        synthetic->brd.bb[piece] |= BIT(square);
                        occupied |= BIT(square);
                    }
                }
            }
        }
        if (!check_search_metadata(*synthetic)) {
            return false;
        }
    }
    return true;
}

// Independent wide arithmetic also checks the scalar production fallback.
template<size_t input_size>
bool check_affine() {
    std::array<uint8_t, input_size> input;
    std::array<int8_t, input_size * 32> weights;
    std::array<int32_t, 32> biases;
    std::mt19937 random(20261016);
    for (int trial = 0; trial < 300; ++trial) {
        for (auto & value : input) value = trial < 4 ? (trial == 0 ? 0 : 127) : random() % 128;
        for (auto & weight : weights) weight = trial < 4 ? (trial == 2 ? -128 : 127)
            : std::bit_cast<int8_t>(static_cast<uint8_t>(random()));
        for (size_t row = 0; row < 32; ++row) {
            int64_t dot = 0;
            for (size_t column = 0; column < input_size; ++column)
                dot += int64_t(input[column]) * weights[row * input_size + column];
            // Include both sides of rounding/clipping boundaries and wide bias addition.
            constexpr int targets[] = {-65, -64, -1, 0, 1, 63, 64, 65, 8127, 8128, 8191, 8192};
            biases[row] = row == 0 ? INT32_MIN : row == 1 ? INT32_MAX
                : static_cast<int32_t>(targets[(trial + row) % 12] - dot);
        }
        const auto scalar = nnue::detail::affine<input_size, false>(input, weights, biases);
        const auto simd = nnue::detail::affine(input, weights, biases);
        for (size_t row = 0; row < 32; ++row) {
            int64_t sum = biases[row];
            for (size_t column = 0; column < input_size; ++column)
                sum += int64_t(input[column]) * weights[row * input_size + column];
            const int expected = sum <= 0 ? 0 : sum >= 127 * 64 ? 127 : sum / 64;
            if (scalar[row] != expected || simd[row] != expected)
                return check(false, "Dense scalar/SIMD mismatch at an arithmetic boundary");
        }
    }
    return true;
}
}

int main() {
    const auto path = std::filesystem::temp_directory_path() / ("maxima-nnue-"
        + std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()) + ".nnue");
    struct cleanup_t {
        std::filesystem::path path;
        ~cleanup_t() { std::error_code error; std::filesystem::remove(path, error); }
    } cleanup{path};
    bool unit_checks = check_activation_clipping() && check_affine<512>() && check_affine<32>();
    unit_checks &= check(nnue::to_centipawns(208) == 100
        && nnue::to_centipawns(-208) == -100 && nnue::to_centipawns(0) == 0
        && nnue::to_centipawns(std::numeric_limits<int>::max()) == score::TABLEBASE - 1
        && nnue::to_centipawns(std::numeric_limits<int>::min()) == -score::TABLEBASE + 1,
        "Stockfish units must convert to Maxima centipawns without reserved scores or overflow");
    magic::init();
    uci::silent(true);
    std::vector<uint8_t> bytes(21022697);
    const size_t network_start = 189 + 4 + 512 + 2 * 256 * 41024;
    const size_t output_bias_offset = bytes.size() - 36;
    put_u32(bytes, 0, 0x7af32f16);
    put_u32(bytes, 4, 0x3e5aa6ee);
    put_u32(bytes, 8, 177);
    put_u32(bytes, 189, 0x5d69d7b8);
    put_u32(bytes, network_start, 0x63337156);
    put_u32(bytes, output_bias_offset, 16 * 208);
    write_file(path, bytes);
    std::string error;
    bool success = unit_checks;
    success &= check(nnue::set_path(path.string(), error), "Valid HalfKP model must load");
    auto first = nnue::snapshot();
    if (!first) return 1;
    const char * fen = "rnbqkbnr/pppppppp/8/8/8/8/PPPPPPPP/RNBQKBNR w KQkq - 0 1";
    success &= check_metadata_positions();
    auto neural_storage = std::make_unique<search_t>(fen);
    auto & neural = *neural_storage;
    success &= check(evaluate(&neural) == 100 && neural.stack->metadata == search_metadata(neural.brd),
        "NNUE score must retain only the required search metadata");
    // Raw neural scores stay constant; only the separate rule50 policy scales
    // the result, without blending or falling back to classical evaluation.
    for (const char * position : {
            "7k/8/8/8/8/8/P7/K7 w - - 0 1",
            "7k/8/8/8/8/8/P7/K7 b - - 80 1",
            "7k/8/8/8/8/8/3Q4/K7 w - - 0 1",
            "7k/8/8/8/8/8/3q4/K7 b - - 80 1"}) {
        auto position_storage = std::make_unique<search_t>(position);
        const int expected = position_storage->brd.stack->fifty_count == 80 ? 20 : 100;
        success &= check(evaluate(position_storage.get()) == expected
            && evaluate(position_storage.get()) == expected
            && position_storage->neural_static_score() == 100,
            "NNUE mode must not blend or fall back to classical scores");
    }
    for (const auto [clock, expected] : {std::pair{0, 100}, {9, 100}, {10, 90},
            {19, 90}, {20, 80}, {89, 20}, {90, 10}, {99, 10}, {100, 0}}) {
        const std::string position = "7k/8/8/8/8/8/3Q4/K7 w - - "
            + std::to_string(clock) + " 1";
        auto probe = std::make_unique<search_t>(position.c_str());
        success &= check(evaluate(probe.get()) == expected
            && evaluate(probe.get()) == expected && probe->neural_static_score() == 100,
            "Rule50 bucket boundaries must scale once and preserve raw neural scores");
    }
    // A repeated board at another clock reuses the undamped static score.
    // It must still populate owned metadata and apply the current clock once.
    {
        const char * cached_fen = "rnbqkbnr/pppppppp/8/8/8/8/PPPPPPPP/RNBQKBNR w KQkq - 8 1";
        auto cached = std::make_unique<search_t>(cached_fen);
#ifdef MAXIMA_NNUE_SEARCH_AUDIT
        // Verification mode intentionally bypasses score reuse; this fixture
        // checks the production cache behavior instead.
        cached->nnue_audit.mode = nnue::audit_mode_t::off;
#endif
        success &= check(evaluate(cached.get()) == 100, "Initial neural cache score must be undamped");
        const U64 key = cached->brd.stack->tt_key;
        std::array<move_t, 4> cycle;
        const char * moves[] = {"g1f3", "g8f6", "f3g1", "f6g8"};
        for (size_t i = 0; i < cycle.size(); ++i) {
            cycle[i].set(&cached->brd, moves[i]);
            success &= check(cached->brd.valid(&cycle[i]) && cached->brd.legal(&cycle[i]),
                "Neural cache fixture must use legal reversible moves");
            cached->forward(&cycle[i], cached->brd.gives_check(&cycle[i]));
        }
        success &= check(cached->brd.stack->tt_key == key && cached->brd.stack->fifty_count == 12,
            "A reversible cycle must revisit the board across a rule50 bucket boundary");
        success &= check(evaluate(cached.get()) == 90 && cached->neural_static_score() == 100
            && cached->stack->metadata == search_metadata(cached->brd)
            && !cached->evaluation_accumulators[cached->brd.ply].ready[0]
            && !cached->evaluation_accumulators[cached->brd.ply].ready[1],
            "Neural cache hits must skip inference and preserve clock/metadata contracts");
        for (size_t i = cycle.size(); i > 0; --i) cached->backward(&cycle[i - 1]);
        cached->init(cached_fen, nullptr, nullptr);
#ifdef MAXIMA_NNUE_SEARCH_AUDIT
        cached->nnue_audit.mode = nnue::audit_mode_t::off;
#endif
        success &= check(evaluate(cached.get()) == 100
            && cached->evaluation_accumulators[0].ready[0]
            && cached->evaluation_accumulators[0].ready[1],
            "Search reinitialization must discard its previous neural cache");
    }
    success &= check(!nnue::set_path(path.string() + ".missing", error)
        && !error.empty() && nnue::snapshot() == first, "Failed reload must retain model");
    success &= check(nnue::path() == path.string(), "Failed reload must retain path");
    for (size_t offset : {size_t(0), size_t(4), size_t(8), size_t(189), network_start}) {
        bytes[offset] ^= 1;
        write_file(path, bytes);
        success &= check(!nnue::set_path(path.string(), error) && nnue::snapshot() == first,
            "Every unsupported version/architecture hash must reject atomically");
        bytes[offset] ^= 1;
    }
    bytes.pop_back();
    write_file(path, bytes);
    success &= check(!nnue::set_path(path.string(), error), "Truncated network must reject");
    bytes.push_back(0);
    put_u32(bytes, output_bias_offset, std::numeric_limits<int32_t>::max());
    write_file(path, bytes);
    success &= check(nnue::set_path(path.string(), error), "Extreme valid-format bias must load safely");
    auto extreme_storage = std::make_unique<search_t>(fen);
    auto & extreme = *extreme_storage;
    success &= check(evaluate(&extreme) == score::TABLEBASE - 1,
        "NNUE must never synthesize tablebase/mate scores");
    success &= check(extreme.tt_context() != neural.tt_context(), "Different models need different TT contexts");
    neural.stack->eval_result = score::INVALID;
    success &= check(evaluate(&neural) == 100, "Search must own immutable model across reloads");
    auto checked_storage = std::make_unique<search_t>("7k/8/8/8/8/8/7r/K7 w - - 0 1");
    auto & checked = *checked_storage;
    checked.stack->in_check = true;
    success &= check(evaluate(&checked) == score::INVALID, "In-check evaluation contract must survive NNUE");
    auto losers_storage = std::make_unique<w17_search_t>(fen);
    auto & losers = *losers_storage;
    const int losers_score = w17_evaluate(&losers);
    success &= check(!losers.evaluation_network && losers.evaluation_accumulators.empty()
        && losers_score != score::TABLEBASE - 1, "Royal-losers must retain material-only evaluator");
    nnue::set_path("", error);
    neural.stack->eval_result = score::INVALID;
    success &= check(!nnue::snapshot() && evaluate(&neural) == 100,
        "Unload must not invalidate retained snapshots");
    success &= check(nnue::set_path(path.string(), error), "A valid network must reload after internal unload");
    auto restored = std::make_unique<search_t>(fen);
    success &= check(evaluate(restored.get()) == score::TABLEBASE - 1
        && restored->tt_context() == extreme.tt_context(), "Reloaded model must retain its score/context identity");
    return success ? 0 : 1;
}
