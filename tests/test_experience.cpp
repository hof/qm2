#include "experience.h"
#include <iostream>

int main() {
    experience::entry_t sample{};
    if (sample.factor() != 1) {
        return 1;
    }
    sample.wins = sample.own_wins = 1;
    if (sample.factor() <= 1 || sample.factor() >= 1.2) {
        return 1;
    }
    sample.wins = sample.own_wins = 100;
    if (sample.factor() <= 4) {
        return 1;
    }
    sample.wins = sample.own_wins = 0;
    sample.losses = 10000;
    if (sample.factor() < 0.1 || sample.factor() >= 0.11) {
        return 1;
    }
    sample.losses = 0;
    sample.draws = 10000;
    if (sample.factor() != 1) {
        return 1;
    }
    experience::entry_t stronger{}, weaker{};
    stronger.wins = 55; stronger.losses = 45;
    weaker.wins = 50; weaker.losses = 50;
    if (stronger.factor() <= weaker.factor()) {
        return 1;
    }
    if (!experience::set_path("") || experience::snapshot()) {
        return 1;
    }
    if (experience::set_path("/no/such/experience.bin") || experience::snapshot()) {
        return 1;
    }
    std::cout << "Confidence smoothing and exploration weights passed\n";
    return 0;
}
