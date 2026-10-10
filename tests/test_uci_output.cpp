#include "uci_console.h"
#include <mutex>
#include <limits>
#include <set>
#include <thread>
#include <vector>

namespace {

class output_buffer : public std::streambuf {
    std::mutex mutex;

    std::streamsize xsputn(const char * text, std::streamsize size) override {
        {
            std::lock_guard<std::mutex> lock(mutex);
            contents.append(text, static_cast<size_t>(size));
        }
        // Model a scheduling delay between writing a payload and its newline.
        std::this_thread::yield();
        return size;
    }

    int_type overflow(int_type value) override {
        if (!traits_type::eq_int_type(value, traits_type::eof())) {
            std::lock_guard<std::mutex> lock(mutex);
            contents.push_back(traits_type::to_char_type(value));
        }
        return traits_type::not_eof(value);
    }

public:
    std::string contents;
};

}

bool test_pv_format() {
    std::ostringstream output;
    auto * original = std::cout.rdbuf(output.rdbuf());
    for (int value : {123, -123, score::MATE - 3, -score::MATE + 3}) {
        for (int flag : {score::EXACT, score::LOWERBOUND, score::UPPERBOUND}) {
            uci::send_pv(value, 4, 5, 123, 1, "e2e4", flag);
        }
    }
    for (int elapsed : {0, 1, 49, 50, 1000})
        uci::send_pv(0, 1, 1, 3000000, elapsed, "e2e4", score::EXACT);
    // Large totals must stay unsigned and NPS must not narrow to an int.
    uci::send_pv(0, 1, 1, std::numeric_limits<U64>::max(), 1000, "", score::EXACT);
    uci::send_pv(0, 1, 1, std::numeric_limits<U64>::max(), 1, "", score::EXACT);
    std::cout.rdbuf(original);
    std::istringstream lines(output.str());
    std::string line;
    bool success = true;
    for (int value : {123, -123, score::MATE - 3, -score::MATE + 3}) {
        for (int flag : {score::EXACT, score::LOWERBOUND, score::UPPERBOUND}) {
            std::getline(lines, line);
            const std::string expected_score = std::abs(value) < score::DEEPEST_MATE
                ? "score cp " + std::to_string(value)
                : "score mate " + std::to_string(value > 0 ? 2 : -2);
            const std::string bound = flag == score::LOWERBOUND ? " lowerbound"
                : flag == score::UPPERBOUND ? " upperbound" : "";
            success &= line.find(expected_score + bound + " nodes 123 time 1 nps 123000 ")
                != std::string::npos;
        }
    }
    for (int elapsed : {0, 1, 49, 50, 1000}) {
        std::getline(lines, line);
        const U64 expected = U64(3000000000) / std::max(1, elapsed);
        success &= line.find("time " + std::to_string(elapsed) + " nps "
            + std::to_string(expected) + " ") != std::string::npos;
    }
    for (int elapsed : {1000, 1}) {
        std::getline(lines, line);
        success &= line.find("nodes 18446744073709551615 time " + std::to_string(elapsed)
            + " nps 18446744073709551615 ") != std::string::npos;
    }
    if (!success) std::cerr << "PV scores must retain bounds and NPS must use milliseconds without narrowing\n"
        << output.str();
    return success;
}

int main() {
    constexpr int worker_count = 8;
    constexpr int messages_per_worker = 256;
    output_buffer buffer;
    auto * original = std::cout.rdbuf(&buffer);
    std::vector<std::thread> workers;
    for (int worker = 0; worker < worker_count; ++worker) {
        workers.emplace_back([worker] {
            for (int message = 0; message < messages_per_worker; ++message) {
                uci::out("info string worker " + std::to_string(worker)
                    + " message " + std::to_string(message));
            }
        });
    }
    for (auto & worker : workers) worker.join();
    std::cout.rdbuf(original);
    std::set<std::string> expected;
    for (int worker = 0; worker < worker_count; ++worker) {
        for (int message = 0; message < messages_per_worker; ++message) {
            expected.insert("info string worker " + std::to_string(worker)
                + " message " + std::to_string(message));
        }
    }
    std::istringstream output(buffer.contents);
    std::string line;
    while (std::getline(output, line)) {
        if (expected.erase(line) != 1) {
            std::cerr << "concurrent UCI messages must remain complete, distinct lines\n";
            return EXIT_FAILURE;
        }
    }
    if (!expected.empty()) {
        std::cerr << "concurrent UCI output must retain every message\n";
        return EXIT_FAILURE;
    }
    return test_pv_format() ? EXIT_SUCCESS : EXIT_FAILURE;
}
