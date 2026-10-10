#include "uci_console.h"
#include <mutex>
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
    return EXIT_SUCCESS;
}
