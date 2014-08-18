/* 
 * File:   test_polyglot.cpp
 * Author: Hajewiet
 *
 * Created on 7-mei-2011, 9:52:07
 */

#include <stdlib.h>
#include <iostream>

#include "board.h"
#include "book.h"
#include "search.h"
#include "bbmoves.h"
#include <time.h>
/*
 * Simple C++ Test Suite
 */

void testPolyglotKeys() {
    std::cout << "test_polyglot test testPolyglotKeys" << std::endl;

    board_t * board = new board_t();

    //starting position
    board->init("rnbqkbnr/pppppppp/8/8/8/8/PPPPPPPP/RNBQKBNR w KQkq - 0 1");
    U64 key = book_t::polyglot_key(board);
    if (key != C64(0x463b96181691fc9c)) {
        std::cout << "%TEST_FAILED% time=0 testname=testPolyglotKeys (test_polyglot) message=key mismatch" << std::endl;
    }

    /* position after e2e4
     * FEN=rnbqkbnr/pppppppp/8/8/4P3/8/PPPP1PPP/RNBQKBNR b KQkq e3 0 1
     * key=823c9b50fd114196
     */
    move_t * move = new move_t();
    move->set(WPAWN, e2, e4);
    board->forward(move);

    board_t * board_test = new board_t();
    board_test->init("rnbqkbnr/pppppppp/8/8/4P3/8/PPPP1PPP/RNBQKBNR b KQkq e3 0 1");

    key = book_t::polyglot_key(board);
    if (key != C64(0x823c9b50fd114196)) {
        std::cout << "%TEST_FAILED% time=0 testname=testPolyglotKeys (test_polyglot) message=key mismatch after e2-e4" << std::endl;
    }
    if (book_t::polyglot_key(board_test) != key) {
        std::cout << "%TEST_FAILED% time=0 testname=testPolyglotKeys (test_polyglot) message=key mismatch after e2-e4 (board_test)" << std::endl;
    }

    /* position after e2e4 d7d5
     * FEN=rnbqkbnr/ppp1pppp/8/3p4/4P3/8/PPPP1PPP/RNBQKBNR w KQkq d6 0 2
     * key=0756b94461c50fb0
     */
    move->set(BPAWN, d7, d5);
    board->forward(move);
    board_test->init("rnbqkbnr/ppp1pppp/8/3p4/4P3/8/PPPP1PPP/RNBQKBNR w KQkq d6 0 2");
    key = book_t::polyglot_key(board);
    if (key != C64(0x0756b94461c50fb0)) {
        std::cout << "%TEST_FAILED% time=0 testname=testPolyglotKeys (test_polyglot) message=key mismatch after e2e4 d7d5" << std::endl;
    }
    if (book_t::polyglot_key(board_test) != key) {
        std::cout << "%TEST_FAILED% time=0 testname=testPolyglotKeys (test_polyglot) message=key mismatch after e2e4 d7d5 (board_test)" << std::endl;
    }
    /* position after e2e4 d7d5 e4e5
     * FEN=rnbqkbnr/ppp1pppp/8/3pP3/8/8/PPPP1PPP/RNBQKBNR b KQkq - 0 2
     * key=662fafb965db29d4
     */
    move->set(WPAWN, e4, e5);
    board->forward(move);
    board_test->init("rnbqkbnr/ppp1pppp/8/3pP3/8/8/PPPP1PPP/RNBQKBNR b KQkq - 0 2");
    key = book_t::polyglot_key(board);
    if (key != C64(0x662fafb965db29d4)) {
        std::cout << "%TEST_FAILED% time=0 testname=testPolyglotKeys (test_polyglot) message=key mismatch after e2e4 d7d5 e4e5" << std::endl;
    }
    if (book_t::polyglot_key(board_test) != key) {
        std::cout << "%TEST_FAILED% time=0 testname=testPolyglotKeys (test_polyglot) message=key mismatch after e2e4 d7d5 e4e5 (board_test)" << std::endl;
    }

    /* position after e2e4 d7d5 e4e5 f7f5
     * FEN=rnbqkbnr/ppp1p1pp/8/3pPp2/8/8/PPPP1PPP/RNBQKBNR w KQkq f6 0 3
     * key=22a48b5a8e47ff78
     */
    move->set(BPAWN, f7, f5);
    board->forward(move);
    board_test->init("rnbqkbnr/ppp1p1pp/8/3pPp2/8/8/PPPP1PPP/RNBQKBNR w KQkq f6 0 3");
    key = book_t::polyglot_key(board);
    if (key != C64(0x22a48b5a8e47ff78)) {
        std::cout << "%TEST_FAILED% time=0 testname=testPolyglotKeys (test_polyglot) message=key mismatch after e2e4 d7d5 e4e5 f7f5" << std::endl;
    }
    if (book_t::polyglot_key(board_test) != key) {
        std::cout << "%TEST_FAILED% time=0 testname=testPolyglotKeys (test_polyglot) message=key mismatch after e2e4 d7d5 e4e5 f7f5 (board_test)" << std::endl;
    }
    /* position after e2e4 d7d5 e4e5 f7f5 e1e2
     * FEN=rnbqkbnr/ppp1p1pp/8/3pPp2/8/8/PPPPKPPP/RNBQ1BNR b kq - 0 3
     * key=652a607ca3f242c1
     */
    move->set(WKING, e1, e2);
    board->forward(move);
    board_test->init("rnbqkbnr/ppp1p1pp/8/3pPp2/8/8/PPPPKPPP/RNBQ1BNR b kq - 0 3");
    key = book_t::polyglot_key(board);
    if (key != C64(0x652a607ca3f242c1)) {
        std::cout << "%TEST_FAILED% time=0 testname=testPolyglotKeys (test_polyglot) message=key mismatch after e2e4 d7d5 e4e5 f7f5 e1e2" << std::endl;
    }
    if (book_t::polyglot_key(board_test) != key) {
        std::cout << "%TEST_FAILED% time=0 testname=testPolyglotKeys (test_polyglot) message=key mismatch after e2e4 d7d5 e4e5 f7f5 e1e2 (board_test)" << std::endl;
    }
    /* position after e2e4 d7d5 e4e5 f7f5 e1e2 e8f7
     * FEN=rnbq1bnr/ppp1pkpp/8/3pPp2/8/8/PPPPKPPP/RNBQ1BNR w - - 0 4
     * key=00fdd303c946bdd9
     */
    move->set(BKING, e8, f7);
    board->forward(move);
    board_test->init("rnbq1bnr/ppp1pkpp/8/3pPp2/8/8/PPPPKPPP/RNBQ1BNR w - - 0 4");
    key = book_t::polyglot_key(board);
    if (key != C64(0x00fdd303c946bdd9)) {
        std::cout << "%TEST_FAILED% time=0 testname=testPolyglotKeys (test_polyglot) message=key mismatch after e2e4 d7d5 e4e5 f7f5 e1e2 e8f7" << std::endl;
    }
    if (book_t::polyglot_key(board_test) != key) {
        std::cout << "%TEST_FAILED% time=0 testname=testPolyglotKeys (test_polyglot) message=key mismatch after e2e4 d7d5 e4e5 f7f5 e1e2 e8f7 (board_test)" << std::endl;
    }
    /* position after a2a4 b7b5 h2h4 b5b4 c2c4
     * FEN=rnbqkbnr/p1pppppp/8/8/PpP4P/8/1P1PPPP1/RNBQKBNR b KQkq c3 0 3
     * key=3c8123ea7b067637
     */
    board->init("rnbqkbnr/p1pppppp/8/8/PpP4P/8/1P1PPPP1/RNBQKBNR b KQkq c3 0 3");
    key = book_t::polyglot_key(board);
    if (key != C64(0x3c8123ea7b067637)) {
        std::cout << "%TEST_FAILED% time=0 testname=testPolyglotKeys (test_polyglot) message=key mismatch after a2a4 b7b5 h2h4 b5b4 c2c4" << std::endl;
    }
    /* position after a2a4 b7b5 h2h4 b5b4 c2c4 b4c3 a1a3
     * FEN=rnbqkbnr/p1pppppp/8/8/P6P/R1p5/1P1PPPP1/1NBQKBNR b Kkq - 0 4
     * key=5c3f9b829b279560
     */
    board->init("rnbqkbnr/p1pppppp/8/8/P6P/R1p5/1P1PPPP1/1NBQKBNR b Kkq - 0 4");
    key = book_t::polyglot_key(board);
    if (key != C64(0x5c3f9b829b279560)) {
        std::cout << "%TEST_FAILED% time=0 testname=testPolyglotKeys (test_polyglot) message=key mismatch after a2a4 b7b5 h2h4 b5b4 c2c4 b4c3 a1a3" << std::endl;
    }

    delete board;
    delete board_test;
    delete move;

}

int main(int argc, char** argv) {
    
    time_t begin;
    time_t beginTest;
    time_t now;
    
    time(&begin);

    magic::init();
    
    std::cout << "%SUITE_STARTING% test_polyglot" << std::endl;
    std::cout << "%SUITE_STARTED%" << std::endl;

    std::cout << "%TEST_STARTED% test1 (test_polyglot)" << std::endl;
    time(&beginTest);
    testPolyglotKeys();
    time(&now);
    std::cout << "%TEST_FINISHED% time=0 testPolyglotKeys (test_polyglot)" << std::endl;

    std::cout << "%SUITE_FINISHED% time=" << difftime(now, begin) << std::endl;

    return (EXIT_SUCCESS);
}

