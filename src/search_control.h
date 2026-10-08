#ifndef SEARCH_CONTROL_H
#define SEARCH_CONTROL_H

#include <atomic>

// The owner keeps this alive until its search worker has been joined.
struct search_control_t {
    std::atomic<bool> stop_requested{false};
    std::atomic<bool> ponder{false};
};

#endif
