/**
 * Maxima, a chess playing program. 
 * Copyright (C) 1996-2015 Erik van het Hof and Hermen Reitsma 
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

int main(int argc, char** argv) {
    
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
    assert_see(&brd, &mv, 0);
    
    int vpawn = board::PVAL[WPAWN];
    int vminor = board::PVAL[WKNIGHT];
    int vrook = board::PVAL[WROOK];
    int vqueen = board::PVAL[WQUEEN];
    
    brd.init("r4rk1/p5p1/1p2p2p/3pPn1P/b1p3Bq/P1PQN1P1/2P2P2/R4RK1 w - - 0 2");
    mv.set(WBISHOP, g4, f5, BKNIGHT);
    assert_see(&brd, &mv, vpawn);
    mv.set(WKNIGHT, e3, f5, BKNIGHT);
    assert_see(&brd, &mv, vpawn);
    mv.set(WQUEEN, d3, f5, BKNIGHT);
    assert_see(&brd, &mv, -vrook-vpawn);   
    mv.set(BBISHOP, a4, c2, WPAWN);
    assert_see(&brd, &mv, -vminor + vpawn);  
    mv.set(BPAWN, c4, d3, WQUEEN);
    assert_see(&brd, &mv, vqueen - vpawn);  
    mv.set(BQUEEN, h4, g4, WBISHOP);
    assert_see(&brd, &mv, -vrook - vpawn);
    
    brd.init("8/8/5p2/8/k7/p1B5/Kb6/8 w - - 0 1");
    mv.set(WBISHOP, c3, f6, BPAWN);
    assert_see(&brd, &mv, -vminor + vpawn);
    mv.set(WBISHOP, c3, b2, BBISHOP);
    assert_see(&brd, &mv, vpawn);
    
    brd.init("1r6/1r1q4/1p6/8/8/1R6/KR6/1Q6 w - - 0 1");
    mv.set(WROOK, b3, b6, BPAWN);
    assert_see(&brd, &mv, vpawn);
    
    brd.init("3r2qk/5bpp/8/3B4/8/8/PPPR4/1K1R4 b - - 0 1");
    mv.set(BBISHOP, f7, d5, WBISHOP);
    assert_see(&brd, &mv, vminor);
    
    brd.init("rnbqkbnr/pppp1ppp/8/4p3/8/4P3/PPPP1PPP/RNBQKBNR w KQkq e6 0 1");
    mv.set(WPAWN, d2, d4);
    assert_see(&brd, &mv, 0);
    
    time(&now);
    std::cout << "%TEST_FINISHED% time=0 testPolyglotKeys (test_polyglot)" << std::endl;
    std::cout << "%SUITE_FINISHED% time=" << difftime(now, begin) << std::endl;

    return (EXIT_SUCCESS);
}

