/* 
 * File:   test_bits.cpp
 * Author: Hajewiet
 *
 * Created on 28-aug-2013, 11:40:29
 */

#include <stdlib.h>
#include <iostream>

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
     * Finalize 
     */
    now = clock();
    int elapsed = (now - begin) / CLOCKS_PER_SEC;

    std::cout << "%TEST_FINISHED% time=" << elapsed << " test_bists (test_bits)" << std::endl;
    std::cout << "%SUITE_FINISHED% time=0" << elapsed << std::endl;

    return (EXIT_SUCCESS);
}

