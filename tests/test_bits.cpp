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
 * File:   test_bits.cpp
 *
 * Created on 28-aug-2013, 11:40:29
 */

#include <stdlib.h>
#include <iostream>
#include <iomanip>  //setw
#include <stdio.h>

#include "engine.h"

/*
 * Simple C++ Test Suite
 */


int main() {

    /*
     * Initialize
     */

    std::cout << "%SUITE_STARTING% test_bits" << std::endl;
    std::cout << "%SUITE_STARTED%" << std::endl;
    std::cout << "%TEST_STARTED% bits (test_bists)\n" << std::endl;


    clock_t begin;
    clock_t now;
    begin = clock();

    /*
     * Create some 64 bit integers, making sure to include the boundaries
     */

    U64 bit0 = C64(1) << 0;
    U64 bit1 = C64(1) << 1;
    U64 bit20 = C64(1) << 20;
    U64 bit31 = C64(1) << 31;
    U64 bit32 = C64(1) << 32;
    U64 bit40 = C64(1) << 40;
    U64 bit62 = C64(1) << 62;
    U64 bit63 = C64(1) << 63;

    U64 x0 = bit0 | bit63;
    U64 x1 = bit1 | bit20 | bit31 | bit32 | bit40 | bit62;
    U64 x2 = bit20 | bit31 | bit32 | bit40;
    U64 x3 = bit31 | bit32;
    U64 x4 = bit1 | bit20 | bit31;
    U64 x5 = bit32 | bit40 | bit62;

    /*
     * bsf test
     */
    std::cout << "1. bsf test" << std::endl;
    if (bsf(x0) != 0) {
        std::cout << "%TEST_FAILED% time=0 testname=test_bits (test_bits) message=bsf(x0) != 0: " << bsf(x0) << std::endl;
    }
    if (bsf(x1) != 1) {
        std::cout << "%TEST_FAILED% time=0 testname=test_bits (test_bits) message=bsf(x1) != 1: " << bsf(x1) << std::endl;
    }
    if (bsf(x2) != 20) {
        std::cout << "%TEST_FAILED% time=0 testname=test_bits (test_bits) message=bsf(x2) != 20: " << bsf(x2) << std::endl;
    }
    if (bsf(x3) != 31) {
        std::cout << "%TEST_FAILED% time=0 testname=test_bits (test_bits) message=bsf(x3) != 31: " << bsf(x3) << std::endl;
    }
    if (bsf(x4) != 1) {
        std::cout << "%TEST_FAILED% time=0 testname=test_bits (test_bits) message=bsf(x4) != 1: " << bsf(x4) << std::endl;
    }
    if (bsf(x5) != 32) {
        std::cout << "%TEST_FAILED% time=0 testname=test_bits (test_bits) message=bsf(x5) != 32: " << bsf(x5) << std::endl;
    }
    std::cout << "   done" << std::endl << std::endl;

    /*
     * bsr test
     */
    std::cout << "2. bsr test" << std::endl;
    if (bsr(x0) != 63) {
        std::cout << "%TEST_FAILED% time=0 testname=test_bits (test_bits) message=bsr(x0) != 63: " << bsr(x0) << std::endl;
    }
    if (bsr(x1) != 62) {
        std::cout << "%TEST_FAILED% time=0 testname=test_bits (test_bits) message=bsr(x1) != 62: " << bsr(x1) << std::endl;
    }
    if (bsr(x2) != 40) {
        std::cout << "%TEST_FAILED% time=0 testname=test_bits (test_bits) message=bsr(x2) != 40: " << bsr(x2) << std::endl;
    }
    if (bsr(x3) != 32) {
        std::cout << "%TEST_FAILED% time=0 testname=test_bits (test_bits) message=bsr(x3) != 32: " << bsr(x3) << std::endl;
    }
    if (bsr(x4) != 31) {
        std::cout << "%TEST_FAILED% time=0 testname=test_bits (test_bits) message=bsr(x4) != 31: " << bsr(x4) << std::endl;
    }
    if (bsr(x5) != 62) {
        std::cout << "%TEST_FAILED% time=0 testname=test_bits (test_bits) message=bsr(x5) != 62: " << bsr(x5) << std::endl;
    }
    std::cout << "   done" << std::endl << std::endl;

    /*
     * PopCount test
     */
    std::cout << "3. popcnt test" << std::endl;
    if (popcnt(x0) != 2) {
        std::cout << "%TEST_FAILED% time=0 testname=test_bits (test_bits) message=popcnt(x0) != 2: " << popcnt(x0) << std::endl;
    }
    if (popcnt(x1) != 6) {
        std::cout << "%TEST_FAILED% time=0 testname=test_bits (test_bits) message=popcnt(x1) != 6: " << popcnt(x1) << std::endl;
    }
    if (popcnt(x2) != 4) {
        std::cout << "%TEST_FAILED% time=0 testname=test_bits (test_bits) message=popcnt(x2) != 4: " << popcnt(x2) << std::endl;
    }
    if (popcnt(x3) != 2) {
        std::cout << "%TEST_FAILED% time=0 testname=test_bits (test_bits) message=popcnt(x3) != 2: " << popcnt(x3) << std::endl;
    }
    if (popcnt(x4) != 3) {
        std::cout << "%TEST_FAILED% time=0 testname=test_bits (test_bits) message=popcnt(x4) != 3: " << popcnt(x4) << std::endl;
    }
    if (popcnt(x5) != 3) {
        std::cout << "%TEST_FAILED% time=0 testname=test_bits (test_bits) message=popcnt(x5) != 3: " << popcnt(x5) << std::endl;
    }
    std::cout << "   done" << std::endl << std::endl;


    /*
     * North / South fill tests
     */

    U64 bb = BIT(a4);
    int sq = bsf(bb);
    if (sq != a4) {
        std::cout << "%TEST_FAILED% time=0 testname=test_bits (test_bits) message=bb=SQ2BB(a4);BB2SQ(bb) != a4: " << sq << std::endl;
    }
    bb |= BIT(b7) | BIT(g6) | BIT(h1);

    U64 nfill = FRONTFILL(bb);
    U64 nfill_expected = BIT(a4) | BIT(a5) | BIT(a6) | BIT(a7) | BIT(a8)
            | BIT(b7) | BIT(b8) | BIT(g6) | BIT(g7) | BIT(g8) | (FILE_H);

    if (nfill != nfill_expected) {
        std::cout << "%TEST_FAILED% time=0 testname=test_bits (test_bits) message=nfill != nfill_expected: " << nfill << " != " << nfill_expected << std::endl;
        bb_print("nfill", nfill);
        bb_print("nfill_expected", nfill_expected);
    }

    U64 sfill = FRONTFILL_B(bb);
    U64 sfill_expected = BIT(a4) | BIT(a3) | BIT(a2) | BIT(a1) | (FILE_B ^ BIT(b8))
            | BIT(h1) | (FILE_G ^ BIT(g7) ^ BIT(g8));
    if (sfill != sfill_expected) {
        std::cout << "%TEST_FAILED% time=0 testname=test_bits (test_bits) message=sfill != sfill_expected: " << sfill << " != " << sfill_expected << std::endl;
        bb_print("sfill", sfill);
        bb_print("sfill_expected", sfill_expected);
    }

    U64 ffill = FILEFILL(bb);
    U64 ffill_expected = FILE_A | FILE_B | FILE_G | FILE_H;

    if (ffill != ffill_expected) {
        std::cout << "%TEST_FAILED% time=0 testname=test_bits (test_bits) message=sfill != sfill_expected: " << ffill << " != " << ffill_expected << std::endl;
        bb_print("ffill", ffill);
        bb_print("ffill_expected", ffill_expected);
    }

    U64 openf = ~FILEFILL(bb);
    U64 openf_expected = FILE_C | FILE_D | FILE_E | FILE_F;

    if (openf != openf_expected) {
        std::cout << "%TEST_FAILED% time=0 testname=test_bits (test_bits) message=openf != openf_expected: " << openf << " != " << openf_expected << std::endl;
        bb_print("openf", openf);
        bb_print("openf_expected", openf_expected);
    }

    U64 nshift = UP1(RANK_8 | RANK_1);
    if (nshift != RANK_2) {
        std::cout << "%TEST_FAILED% time=0 testname=test_bits (test_bits) message=nshift != nshift_expected: " << nshift << " != " << RANK_2 << std::endl;
        bb_print("nshift", nshift);
        bb_print("nshift_expected", RANK_2);
    }

    U64 sshift = DOWN1(RANK_8 | RANK_1);
    if (sshift != RANK_7) {
        std::cout << "%TEST_FAILED% time=0 testname=test_bits (test_bits) message=sshift != sshift_expected: " << sshift << " != " << RANK_7 << std::endl;
        bb_print("sshift", nshift);
        bb_print("sshift_expected", RANK_2);
    }


    /*
     * Finalize 
     */
    now = clock();
    int elapsed = (now - begin) / CLOCKS_PER_SEC;

    std::cout << "%TEST_FINISHED% time=" << elapsed << " test_bists (test_bits)" << std::endl;
    std::cout << "%SUITE_FINISHED% time=0" << elapsed << std::endl;

    return (EXIT_SUCCESS);
}

