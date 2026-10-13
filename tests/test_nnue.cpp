#include "engine.h"
#include "nnue.h"
#include "w17/w17_eval.h"
#include "w17/w17_search.h"
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
}

int main() {
    const auto path = std::filesystem::temp_directory_path() / ("maxima-nnue-"
        + std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()) + ".nnue");
    struct cleanup_t {
        std::filesystem::path path;
        ~cleanup_t() { std::error_code error; std::filesystem::remove(path, error); }
    } cleanup{path};
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
    put_u32(bytes, output_bias_offset, 16 * 123);
    write_file(path, bytes);
    std::string error;
    bool success = check(nnue::set_path(path.string(), error), "Valid HalfKP model must load");
    auto first = nnue::snapshot();
    if (!first) return 1;
    options::get_option("UseNNUE")->value = 0;
    const char * fen = "rnbqkbnr/pppppppp/8/8/8/8/PPPPPPPP/RNBQKBNR w KQkq - 0 1";
    auto hce_storage = std::make_unique<search_t>(fen);
    auto & hce = *hce_storage;
    const int hce_score = evaluate(&hce);
    const U64 hce_context = hce.tt_context();
    options::get_option("UseNNUE")->value = 1;
    auto neural_storage = std::make_unique<search_t>(fen);
    auto & neural = *neural_storage;
    success &= check(evaluate(&neural) == 123 && neural.stack->mt && neural.stack->pt,
        "NNUE score must replace HCE while retaining material/pawn metadata");
    success &= check(neural.tt_context() != hce_context, "HCE and NNUE must not share TT score context");
    success &= check(evaluate(&hce) == hce_score && !hce.evaluation_network,
        "Existing searches must retain their evaluator");
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
    success &= check(evaluate(&neural) == 123, "Search must own immutable model across reloads");
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
    success &= check(!nnue::snapshot() && evaluate(&neural) == 123,
        "Unload must not invalidate retained snapshots");
    options::get_option("UseNNUE")->value = 0;
    auto restored_storage = std::make_unique<search_t>(fen);
    auto & restored = *restored_storage;
    success &= check(evaluate(&restored) == hce_score && restored.tt_context() == hce_context,
        "HCE behavior/context must be unchanged after NNUE unload");
    return success ? 0 : 1;
}
