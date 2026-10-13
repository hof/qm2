#ifndef MAXIMA_NNUE_H
#define MAXIMA_NNUE_H

#include "board.h"
#include <array>
#include <memory>
#include <string>
#include <vector>

// Legacy HalfKP integer inference, GPL-3.0-or-later, like the rest of Maxima.
// Format/arithmetic reference: third_party/nnue_reference/README.md.
namespace nnue {

struct change_t {
    int piece;
    int from;
    int to;
};

struct alignas(64) accumulator_t {
    std::array<std::array<int16_t, 256>, 2> values{};
    std::array<int, 2> kings{}; // Maxima color order: Black, White.
    std::array<bool, 2> ready{};
    std::array<change_t, 3> changes{};
    int change_count = 0;
};

class network_t {
public:
    static std::shared_ptr<const network_t> load(const std::string & path, std::string & error);
    int evaluate(const board_t & board, accumulator_t & accumulator,
        const accumulator_t * previous = nullptr) const;
    void prepare(const board_t & board, const move_t * move, accumulator_t & accumulator) const;
    U64 fingerprint() const { return _fingerprint; }

private:
    std::array<int16_t, 256> _biases{};
    std::vector<int16_t> _features;
    std::array<int32_t, 32> _hidden1_biases{}, _hidden2_biases{};
    std::array<int8_t, 512 * 32> _hidden1_weights{};
    std::array<int8_t, 32 * 32> _hidden2_weights{};
    std::array<int8_t, 32> _output_weights{};
    int32_t _output_bias = 0;
    U64 _fingerprint = 0;

    void refresh(const board_t & board, accumulator_t & accumulator, bool color) const;
    void add_feature(std::array<int16_t, 256> & values, bool color,
        int king, int piece, int square, bool subtract) const;
};

// Configuration changes occur after stopping/joining the engine worker.
// Snapshots also retain their weights across later reloads or unloads.
bool set_path(const std::string & path, std::string & error);
std::shared_ptr<const network_t> snapshot();
std::string path();

}
#endif
