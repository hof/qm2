#include "experience.h"
#include <algorithm>
#include <cmath>
#include <fstream>
#include <mutex>
#include <tuple>
#include <string>

namespace experience {
namespace {
std::mutex mutex;
std::shared_ptr<const book_t> current;

uint64_t read_le(const unsigned char * bytes, unsigned size) {
    uint64_t value = 0;
    for (unsigned i = 0; i < size; ++i) value |= uint64_t(bytes[i]) << (8 * i);
    return value;
}
}

double entry_t::factor() const {
    const double own_games = double(own_wins) + own_draws + own_losses;
    const double games = own_games ? own_games : double(wins) + draws + losses;
    const double earned = own_games ? own_wins + 0.5 * own_draws : wins + 0.5 * draws;
    const double prior = own_games ? 32 : 64;
    const double score_rate = (earned + prior / 2) / (games + prior);
    // Preserve exploration, while accumulated results outweigh the neutral prior.
    return 0.1 + 0.9 * std::pow(2 * score_rate, 4);
}

const entry_t * book_t::find(uint64_t key, uint32_t move) const {
    const auto wanted = std::make_pair(key, move);
    auto it = std::lower_bound(entries.begin(), entries.end(), wanted,
        [](const entry_t & entry, const auto & value) {
            return std::make_pair(entry.key, entry.move) < value;
        });
    return it != entries.end() && it->key == key && it->move == move ? &*it : nullptr;
}

std::shared_ptr<const book_t> snapshot() {
    std::lock_guard<std::mutex> lock(mutex);
    return current;
}

bool set_path(const std::string & path) {
    std::shared_ptr<const book_t> next;
    if (!path.empty() && path != "<empty>") {
        std::ifstream input(path, std::ios::binary);
        unsigned char header[16];
        if (!input.read(reinterpret_cast<char *>(header), 16)
            || std::string(reinterpret_cast<char *>(header), 8) != "MXEXP001") return false;
        const uint64_t count = read_le(header + 8, 8);
        if (count > 2000000) return false;
        input.seekg(0, std::ios::end);
        if (input.tellg() != std::streamoff(16 + 80 * count)) return false;
        input.seekg(16);
        std::vector<entry_t> entries;
        entries.reserve(count);
        for (uint64_t i = 0; i < count; ++i) {
            unsigned char bytes[80];
            if (!input.read(reinterpret_cast<char *>(bytes), 80)) return false;
            entry_t entry{};
            entry.key = read_le(bytes, 8);
            entry.move = read_le(bytes + 8, 4);
            entry.wins = read_le(bytes + 12, 4);
            entry.draws = read_le(bytes + 16, 4);
            entry.losses = read_le(bytes + 20, 4);
            entry.depth = read_le(bytes + 24, 4);
            entry.score = int32_t(uint32_t(read_le(bytes + 28, 4)));
            entry.nodes = read_le(bytes + 32, 8);
            entry.think_ms = read_le(bytes + 40, 8);
            entry.clock = read_le(bytes + 48, 4);
            entry.score_type = read_le(bytes + 52, 4);
            entry.engine = read_le(bytes + 56, 8);
            entry.own_wins = read_le(bytes + 64, 4);
            entry.own_draws = read_le(bytes + 68, 4);
            entry.own_losses = read_le(bytes + 72, 4);
            if (read_le(bytes + 76, 4) || entry.own_wins > entry.wins
                || entry.own_draws > entry.draws || entry.own_losses > entry.losses
                || entry.move >> 12 > 4 || (entry.move & 63) == ((entry.move >> 6) & 63)
                || entry.depth < 0 || entry.depth > 128 || entry.score_type > 1
                || (!entries.empty() && std::make_pair(entries.back().key, entries.back().move)
                    >= std::make_pair(entry.key, entry.move))) return false;
            entries.push_back(entry);
        }
        if (input.peek() != std::char_traits<char>::eof()) return false;
        next = std::make_shared<const book_t>(std::move(entries));
    }
    std::lock_guard<std::mutex> lock(mutex);
    current = std::move(next);
    return true;
}
}
