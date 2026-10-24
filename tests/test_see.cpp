/**
 * Maxima, a chess playing program. 
 * Copyright (C) 1996-2026 Erik van het Hof and Hermen Reitsma 
 * 
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License
 * as published by the Free Software Foundation; either version 3
 * of the License, or (at your option) any later version.
 *  
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *  
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, If not, see <http://www.gnu.org/licenses/>.
 *  
 * File:   test_polyglot.cpp
 *
 * Created on 7-mei-2011, 9:52:07
 */


#include "board.h"
#include "move.h"
#include <memory>
#include <limits>
#include <sstream>

namespace {
struct certificate_t {
    const char * name;
    const char * fen;
    const char * move;
    int expected;
};
const certificate_t certificates[] = {
#include "data/see/legality.inc"
};

std::string state(const board_t & board) {
    std::ostringstream out;
    for (U64 value : board.bb) out << value << ' ';
    for (int value : board.matrix) out << value << ' ';
    out << board.ply << ' ' << board.root_ply << ' ' << board.stack;
    for (int ply = 0; ply <= board.ply; ++ply) {
        const auto & entry = board._stack[ply];
        out << ' ' << entry.tt_key
            << ' ' << entry.fifty_count << ' ' << entry.castling_flags
            << ' ' << entry.enpassant_sq << ' ' << entry.wtm << ' ' << entry.legal_enpassant;
    }
    return out.str();
}

bool legal_exchange_certificates() {
    auto board = std::make_unique<board_t>();
    bool success = true;
    for (const auto & certificate : certificates) {
        board->init(certificate.fen);
        move_t move;
        move.set(board.get(), certificate.move);
        if (!board->valid(&move) || !board->legal(&move)) {
            std::cerr << "Invalid SEE certificate: " << certificate.name << '\n';
            return false;
        }
        const auto before = state(*board);
        const int result = board->see(&move);
        // Test both equality boundaries and a wide range of thresholds
        // against the independent material certificate, including overflow edges.
        for (int threshold : {std::numeric_limits<int>::min(), std::numeric_limits<int>::max(),
                certificate.expected - 1, certificate.expected, certificate.expected + 1, 0, 1}) {
            if (board->see(&move, threshold) != (certificate.expected >= threshold)) {
                std::cerr << certificate.name << " threshold " << threshold << " differs\n";
                success = false;
            }
        }
        for (int threshold = -1500; threshold <= 1500; threshold += 25) {
            if (board->see(&move, threshold) != (certificate.expected >= threshold)) {
                std::cerr << certificate.name << " threshold " << threshold << " differs\n";
                success = false;
            }
        }
        if (result != certificate.expected || state(*board) != before) {
            std::cerr << certificate.name << " SEE " << result << " expected "
                << certificate.expected << ": " << certificate.fen << ' ' << certificate.move << '\n';
            success = false;
        }
    }
    std::cout << std::size(certificates) << " independent legal exchange certificates checked\n";
    std::cout << std::size(certificates) * 128 << " threshold comparisons checked\n";
    return success;
}
}

/*
 * Simple C++ Test Suite
 */

bool assert_see(board_t * brd, move_t * mv, int expected) {
    int see_val = brd->see(mv);
    bool ok = see_val == expected ;
    if (!ok) {
        std::cout << brd->to_string() << " " << mv->to_string();
        std::cout << " FAIL " << expected << " != " << see_val;
        std::cout << std::endl;
    }
    return ok;
}

int main() {
    bool success = true;
    
    time_t begin;
    time_t now;
    
    time(&begin);

    magic::init();
    
    std::cout << "%SUITE_STARTING% test_polyglot" << std::endl;
    std::cout << "%SUITE_STARTED%" << std::endl;
    std::cout << "%TEST_STARTED% test1 (test_see)" << std::endl;
    
    board_t brd;
    move_t mv;
    
    brd.init("r1bqkbnr/pppppppp/2n5/8/4P3/8/PPPP1PPP/RNBQKBNR w KQkq - 1 1");
    mv.set(WPAWN, d2, d4);
    success &= assert_see(&brd, &mv, 0);

    brd.init("r3k2r/ppp2ppp/8/8/8/8/PPP2PPP/R3K2R w KQkq - 0 1");
    move_t castle_move{};
    castle_move.set(&brd, "e1g1");
    success &= brd.valid(&castle_move) && brd.legal(&castle_move);
    success &= brd.see(&castle_move) == 0 && brd.see(&castle_move, 0) && !brd.see(&castle_move, 1);
    
    int vpawn = board::PVAL[WPAWN];
    int vminor = board::PVAL[WKNIGHT];
    int vrook = board::PVAL[WROOK];
    int vqueen = board::PVAL[WQUEEN];
    
    brd.init("r4rk1/p5p1/1p2p2p/3pPn1P/b1p3Bq/P1PQN1P1/2P2P2/R4RK1 w - - 0 2");
    mv.set(WBISHOP, g4, f5, BKNIGHT);
    success &= assert_see(&brd, &mv, vpawn);
    mv.set(WKNIGHT, e3, f5, BKNIGHT);
    success &= assert_see(&brd, &mv, vpawn);
    mv.set(WQUEEN, d3, f5, BKNIGHT);
    // Legal exchange oracle: ...exf5, Bxf5, declining the losing ...Rxf5.
    success &= assert_see(&brd, &mv, -vrook);
    mv.set(BBISHOP, a4, c2, WPAWN);
    success &= assert_see(&brd, &mv, -vminor + vpawn);
    mv.set(BPAWN, c4, d3, WQUEEN);
    success &= assert_see(&brd, &mv, vqueen - vpawn);
    mv.set(BQUEEN, h4, g4, WBISHOP);
    success &= assert_see(&brd, &mv, -vrook - vpawn);
    
    brd.init("8/8/5p2/8/k7/p1B5/Kb6/8 w - - 0 1");
    mv.set(WBISHOP, c3, f6, BPAWN);
    success &= assert_see(&brd, &mv, -vminor + vpawn);
    mv.set(WBISHOP, c3, b2, BBISHOP);
    success &= assert_see(&brd, &mv, vpawn);
    
    brd.init("1r5k/1r1q4/1p6/8/8/1R6/KR6/1Q6 w - - 0 1");
    mv.set(WROOK, b3, b6, BPAWN);
    success &= assert_see(&brd, &mv, vpawn);
    
    brd.init("3r2qk/5bpp/8/3B4/8/8/PPPR4/1K1R4 b - - 0 1");
    mv.set(BBISHOP, f7, d5, WBISHOP);
    success &= assert_see(&brd, &mv, vminor);
    
    brd.init("8/1p3p1p/2k1p1p1/5P2/p3K3/8/1P3PPP/8 b - - 0 1");
    mv.set(BPAWN, e6, f5, WPAWN);
    success &= assert_see(&brd, &mv, vpawn);
    
    brd.init("rnbqkbnr/pppp1ppp/8/4p3/8/4P3/PPPP1PPP/RNBQKBNR w KQkq e6 0 1");
    mv.set(WPAWN, d2, d4);
    success &= assert_see(&brd, &mv, 0);
    
    time(&now);
    std::cout << "%TEST_FINISHED% time=0 testPolyglotKeys (test_polyglot)" << std::endl;
    std::cout << "%SUITE_FINISHED% time=" << difftime(now, begin) << std::endl;

    success &= legal_exchange_certificates();
    return success ? EXIT_SUCCESS : EXIT_FAILURE;
}

