/* 
 * File:   test_bits.cpp
 * Author: Hajewiet
 *
 * Created on 28-aug-2013, 11:40:29
 */

#include <stdlib.h>
#include <iostream>
#include <stdio.h>

#include "engine.h"

/*
 * Simple C++ Test Suite
 */


int main(int argc, char** argv) {

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
     * BSF test
     */
    std::cout << "1. BSF test" << std::endl;
    if (BSF(x0) != 0) {
        std::cout << "%TEST_FAILED% time=0 testname=test_bits (test_bits) message=BSF(x0) != 0: " << BSF(x0) << std::endl;
    }
    if (BSF(x1) != 1) {
        std::cout << "%TEST_FAILED% time=0 testname=test_bits (test_bits) message=BSF(x1) != 1: " << BSF(x1) << std::endl;
    }
    if (BSF(x2) != 20) {
        std::cout << "%TEST_FAILED% time=0 testname=test_bits (test_bits) message=BSF(x2) != 20: " << BSF(x2) << std::endl;
    }
    if (BSF(x3) != 31) {
        std::cout << "%TEST_FAILED% time=0 testname=test_bits (test_bits) message=BSF(x3) != 31: " << BSF(x3) << std::endl;
    }
    if (BSF(x4) != 1) {
        std::cout << "%TEST_FAILED% time=0 testname=test_bits (test_bits) message=BSF(x4) != 1: " << BSF(x4) << std::endl;
    }
    if (BSF(x5) != 32) {
        std::cout << "%TEST_FAILED% time=0 testname=test_bits (test_bits) message=BSF(x5) != 32: " << BSF(x5) << std::endl;
    }
    std::cout << "   done" << std::endl << std::endl;

    /*
     * BSR test
     */
    std::cout << "2. BSR test" << std::endl;
    if (BSR(x0) != 63) {
        std::cout << "%TEST_FAILED% time=0 testname=test_bits (test_bits) message=BSR(x0) != 63: " << BSR(x0) << std::endl;
    }
    if (BSR(x1) != 62) {
        std::cout << "%TEST_FAILED% time=0 testname=test_bits (test_bits) message=BSR(x1) != 62: " << BSR(x1) << std::endl;
    }
    if (BSR(x2) != 40) {
        std::cout << "%TEST_FAILED% time=0 testname=test_bits (test_bits) message=BSR(x2) != 40: " << BSR(x2) << std::endl;
    }
    if (BSR(x3) != 32) {
        std::cout << "%TEST_FAILED% time=0 testname=test_bits (test_bits) message=BSR(x3) != 32: " << BSR(x3) << std::endl;
    }
    if (BSR(x4) != 31) {
        std::cout << "%TEST_FAILED% time=0 testname=test_bits (test_bits) message=BSR(x4) != 31: " << BSR(x4) << std::endl;
    }
    if (BSR(x5) != 62) {
        std::cout << "%TEST_FAILED% time=0 testname=test_bits (test_bits) message=BSR(x5) != 62: " << BSR(x5) << std::endl;
    }
    std::cout << "   done" << std::endl << std::endl;

    /*
     * PopCount test
     */
    std::cout << "3. popCount test" << std::endl;
    if (popCount(x0) != 2) {
        std::cout << "%TEST_FAILED% time=0 testname=test_bits (test_bits) message=popCount(x0) != 2: " << popCount(x0) << std::endl;
    }
    if (popCount(x1) != 6) {
        std::cout << "%TEST_FAILED% time=0 testname=test_bits (test_bits) message=popCount(x1) != 6: " << popCount(x1) << std::endl;
    }
    if (popCount(x2) != 4) {
        std::cout << "%TEST_FAILED% time=0 testname=test_bits (test_bits) message=popCount(x2) != 4: " << popCount(x2) << std::endl;
    }
    if (popCount(x3) != 2) {
        std::cout << "%TEST_FAILED% time=0 testname=test_bits (test_bits) message=popCount(x3) != 2: " << popCount(x3) << std::endl;
    }
    if (popCount(x4) != 3) {
        std::cout << "%TEST_FAILED% time=0 testname=test_bits (test_bits) message=popCount(x4) != 3: " << popCount(x4) << std::endl;
    }
    if (popCount(x5) != 3) {
        std::cout << "%TEST_FAILED% time=0 testname=test_bits (test_bits) message=popCount(x5) != 3: " << popCount(x5) << std::endl;
    }
    std::cout << "   done" << std::endl << std::endl;


    /*
     * North / South fill tests
     */

    U64 bb = BIT(a4);
    int sq = BSF(bb);
    if (sq != a4) {
        std::cout << "%TEST_FAILED% time=0 testname=test_bits (test_bits) message=bb=SQ2BB(a4);BB2SQ(bb) != a4: " << sq << std::endl;
    }
    bb |= BIT(b7) | BIT(g6) | BIT(h1);

    U64 nfill = FRONTFILL(bb);
    U64 nfill_expected = BIT(a4) | BIT(a5) | BIT(a6) | BIT(a7) | BIT(a8)
            | BIT(b7) | BIT(b8) | BIT(g6) | BIT(g7) | BIT(g8) | (FILE_H);

    if (nfill != nfill_expected) {
        std::cout << "%TEST_FAILED% time=0 testname=test_bits (test_bits) message=nfill != nfill_expected: " << nfill << " != " << nfill_expected << std::endl;
        printBB("nfill", nfill);
        printBB("nfill_expected", nfill_expected);
    }

    U64 sfill = FRONTFILL_B(bb);
    U64 sfill_expected = BIT(a4) | BIT(a3) | BIT(a2) | BIT(a1) | (FILE_B ^ BIT(b8))
            | BIT(h1) | (FILE_G ^ BIT(g7) ^ BIT(g8));
    if (sfill != sfill_expected) {
        std::cout << "%TEST_FAILED% time=0 testname=test_bits (test_bits) message=sfill != sfill_expected: " << sfill << " != " << sfill_expected << std::endl;
        printBB("sfill", sfill);
        printBB("sfill_expected", sfill_expected);
    }

    U64 ffill = FILEFILL(bb);
    U64 ffill_expected = FILE_A | FILE_B | FILE_G | FILE_H;

    if (ffill != ffill_expected) {
        std::cout << "%TEST_FAILED% time=0 testname=test_bits (test_bits) message=sfill != sfill_expected: " << ffill << " != " << ffill_expected << std::endl;
        printBB("ffill", ffill);
        printBB("ffill_expected", ffill_expected);
    }

    U64 openf = ~FILEFILL(bb);
    U64 openf_expected = FILE_C | FILE_D | FILE_E | FILE_F;

    if (openf != openf_expected) {
        std::cout << "%TEST_FAILED% time=0 testname=test_bits (test_bits) message=openf != openf_expected: " << openf << " != " << openf_expected << std::endl;
        printBB("openf", openf);
        printBB("openf_expected", openf_expected);
    }

    U64 nshift = NORTH1(RANK_8 | RANK_1);
    if (nshift != RANK_2) {
        std::cout << "%TEST_FAILED% time=0 testname=test_bits (test_bits) message=nshift != nshift_expected: " << nshift << " != " << RANK_2 << std::endl;
        printBB("nshift", nshift);
        printBB("nshift_expected", RANK_2);
    }

    U64 sshift = SOUTH1(RANK_8 | RANK_1);
    if (sshift != RANK_7) {
        std::cout << "%TEST_FAILED% time=0 testname=test_bits (test_bits) message=sshift != sshift_expected: " << sshift << " != " << RANK_7 << std::endl;
        printBB("sshift", nshift);
        printBB("sshift_expected", RANK_2);
    }
    
    
    for (int i = 1; i <= 128; i++) {
        std::cout << "BSR(" << (i) << ") = " << BSR(i) << std::endl;
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

