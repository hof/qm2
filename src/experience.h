#ifndef MAXIMA_EXPERIENCE_H
#define MAXIMA_EXPERIENCE_H

#include <cstdint>
#include <memory>
#include <string>
#include <utility>
#include <vector>

namespace experience {

struct entry_t {
    uint64_t key, nodes, think_ms, engine;
    uint32_t move, wins, draws, losses, clock, score_type;
    uint32_t own_wins, own_draws, own_losses;
    int32_t depth, score;
    double factor() const;
};

class book_t {
    std::vector<entry_t> entries;
public:
    explicit book_t(std::vector<entry_t> data) : entries(std::move(data)) {}
    const entry_t * find(uint64_t key, uint32_t move) const;
};

// Publish only complete validated snapshots. Searches hold immutable ownership.
bool set_path(const std::string & path);
std::shared_ptr<const book_t> snapshot();

}
#endif
