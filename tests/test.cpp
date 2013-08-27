/* 
 * File:   tests.cpp
 * Author: Twin
 *
 * Created on 9-apr-2011, 10:47:34
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
    std::cout << "tests test testPolyglotKeys" << std::endl;

    TBoard * board = new TBoard();

    //starting position
    board->fromFen("rnbqkbnr/pppppppp/8/8/8/8/PPPPPPPP/RNBQKBNR w KQkq - 0 1");
    U64 key = TBook::polyglot_key(board);
    if (key != C64(0x463b96181691fc9c)) {
        std::cout << "%TEST_FAILED% time=0 testname=testPolyglotKeys (tests) message=key mismatch" << std::endl;
    }

    /* position after e2e4
     * FEN=rnbqkbnr/pppppppp/8/8/4P3/8/PPPP1PPP/RNBQKBNR b KQkq e3 0 1
     * key=823c9b50fd114196
     */
    TMove * move = new TMove();
    move->setMove(WPAWN, e2, e4);
    board->forward(move);

    TBoard * board_test = new TBoard();
    board_test->fromFen("rnbqkbnr/pppppppp/8/8/4P3/8/PPPP1PPP/RNBQKBNR b KQkq e3 0 1");

    key = TBook::polyglot_key(board);
    if (key != C64(0x823c9b50fd114196)) {
        std::cout << "%TEST_FAILED% time=0 testname=testPolyglotKeys (tests) message=key mismatch after e2-e4" << std::endl;
    }
    if (TBook::polyglot_key(board_test) != key) {
        std::cout << "%TEST_FAILED% time=0 testname=testPolyglotKeys (tests) message=key mismatch after e2-e4 (board_test)" << std::endl;
    }

    /* position after e2e4 d7d5
     * FEN=rnbqkbnr/ppp1pppp/8/3p4/4P3/8/PPPP1PPP/RNBQKBNR w KQkq d6 0 2
     * key=0756b94461c50fb0
     */
    move->setMove(BPAWN, d7, d5);
    board->forward(move);
    board_test->fromFen("rnbqkbnr/ppp1pppp/8/3p4/4P3/8/PPPP1PPP/RNBQKBNR w KQkq d6 0 2");
    key = TBook::polyglot_key(board);
    if (key != C64(0x0756b94461c50fb0)) {
        std::cout << "%TEST_FAILED% time=0 testname=testPolyglotKeys (tests) message=key mismatch after e2e4 d7d5" << std::endl;
    }
    if (TBook::polyglot_key(board_test) != key) {
        std::cout << "%TEST_FAILED% time=0 testname=testPolyglotKeys (tests) message=key mismatch after e2e4 d7d5 (board_test)" << std::endl;
    }
    /* position after e2e4 d7d5 e4e5
     * FEN=rnbqkbnr/ppp1pppp/8/3pP3/8/8/PPPP1PPP/RNBQKBNR b KQkq - 0 2
     * key=662fafb965db29d4
     */
    move->setMove(WPAWN, e4, e5);
    board->forward(move);
    board_test->fromFen("rnbqkbnr/ppp1pppp/8/3pP3/8/8/PPPP1PPP/RNBQKBNR b KQkq - 0 2");
    key = TBook::polyglot_key(board);
    if (key != C64(0x662fafb965db29d4)) {
        std::cout << "%TEST_FAILED% time=0 testname=testPolyglotKeys (tests) message=key mismatch after e2e4 d7d5 e4e5" << std::endl;
    }
    if (TBook::polyglot_key(board_test) != key) {
        std::cout << "%TEST_FAILED% time=0 testname=testPolyglotKeys (tests) message=key mismatch after e2e4 d7d5 e4e5 (board_test)" << std::endl;
    }

    /* position after e2e4 d7d5 e4e5 f7f5
     * FEN=rnbqkbnr/ppp1p1pp/8/3pPp2/8/8/PPPP1PPP/RNBQKBNR w KQkq f6 0 3
     * key=22a48b5a8e47ff78
     */
    move->setMove(BPAWN, f7, f5);
    board->forward(move);
    board_test->fromFen("rnbqkbnr/ppp1p1pp/8/3pPp2/8/8/PPPP1PPP/RNBQKBNR w KQkq f6 0 3");
    key = TBook::polyglot_key(board);
    if (key != C64(0x22a48b5a8e47ff78)) {
        std::cout << "%TEST_FAILED% time=0 testname=testPolyglotKeys (tests) message=key mismatch after e2e4 d7d5 e4e5 f7f5" << std::endl;
    }
    if (TBook::polyglot_key(board_test) != key) {
        std::cout << "%TEST_FAILED% time=0 testname=testPolyglotKeys (tests) message=key mismatch after e2e4 d7d5 e4e5 f7f5 (board_test)" << std::endl;
    }
    /* position after e2e4 d7d5 e4e5 f7f5 e1e2
     * FEN=rnbqkbnr/ppp1p1pp/8/3pPp2/8/8/PPPPKPPP/RNBQ1BNR b kq - 0 3
     * key=652a607ca3f242c1
     */
    move->setMove(WKING, e1, e2);
    board->forward(move);
    board_test->fromFen("rnbqkbnr/ppp1p1pp/8/3pPp2/8/8/PPPPKPPP/RNBQ1BNR b kq - 0 3");
    key = TBook::polyglot_key(board);
    if (key != C64(0x652a607ca3f242c1)) {
        std::cout << "%TEST_FAILED% time=0 testname=testPolyglotKeys (tests) message=key mismatch after e2e4 d7d5 e4e5 f7f5 e1e2" << std::endl;
    }
    if (TBook::polyglot_key(board_test) != key) {
        std::cout << "%TEST_FAILED% time=0 testname=testPolyglotKeys (tests) message=key mismatch after e2e4 d7d5 e4e5 f7f5 e1e2 (board_test)" << std::endl;
    }
    /* position after e2e4 d7d5 e4e5 f7f5 e1e2 e8f7
     * FEN=rnbq1bnr/ppp1pkpp/8/3pPp2/8/8/PPPPKPPP/RNBQ1BNR w - - 0 4
     * key=00fdd303c946bdd9
     */
    move->setMove(BKING, e8, f7);
    board->forward(move);
    board_test->fromFen("rnbq1bnr/ppp1pkpp/8/3pPp2/8/8/PPPPKPPP/RNBQ1BNR w - - 0 4");
    key = TBook::polyglot_key(board);
    if (key != C64(0x00fdd303c946bdd9)) {
        std::cout << "%TEST_FAILED% time=0 testname=testPolyglotKeys (tests) message=key mismatch after e2e4 d7d5 e4e5 f7f5 e1e2 e8f7" << std::endl;
    }
    if (TBook::polyglot_key(board_test) != key) {
        std::cout << "%TEST_FAILED% time=0 testname=testPolyglotKeys (tests) message=key mismatch after e2e4 d7d5 e4e5 f7f5 e1e2 e8f7 (board_test)" << std::endl;
    }
    /* position after a2a4 b7b5 h2h4 b5b4 c2c4
     * FEN=rnbqkbnr/p1pppppp/8/8/PpP4P/8/1P1PPPP1/RNBQKBNR b KQkq c3 0 3
     * key=3c8123ea7b067637
     */
    board->fromFen("rnbqkbnr/p1pppppp/8/8/PpP4P/8/1P1PPPP1/RNBQKBNR b KQkq c3 0 3");
    key = TBook::polyglot_key(board);
    if (key != C64(0x3c8123ea7b067637)) {
        std::cout << "%TEST_FAILED% time=0 testname=testPolyglotKeys (tests) message=key mismatch after a2a4 b7b5 h2h4 b5b4 c2c4" << std::endl;
    }
    /* position after a2a4 b7b5 h2h4 b5b4 c2c4 b4c3 a1a3
     * FEN=rnbqkbnr/p1pppppp/8/8/P6P/R1p5/1P1PPPP1/1NBQKBNR b Kkq - 0 4
     * key=5c3f9b829b279560
     */
    board->fromFen("rnbqkbnr/p1pppppp/8/8/P6P/R1p5/1P1PPPP1/1NBQKBNR b Kkq - 0 4");
    key = TBook::polyglot_key(board);
    if (key != C64(0x5c3f9b829b279560)) {
        std::cout << "%TEST_FAILED% time=0 testname=testPolyglotKeys (tests) message=key mismatch after a2a4 b7b5 h2h4 b5b4 c2c4 b4c3 a1a3" << std::endl;
    }

    delete board;
    delete board_test;
    delete move;

}

U64 moveGenerationPerft(TSearchData *searchData, int depth) {
    U64 result = 0;
    
    if (depth <= 0) {
        return 1;
    }

    TBoard * pos = searchData->getPosition();
    TMoveList * moveList = searchData->getMoveList();
    int count = genAllMoves(pos, moveList);
    for (int i = 0; i < count; i++) {
        TMove * move = &moveList->get(i)->move;
        //if (pos->legal(move)) {
                pos->forward(move);
                if (pos->legal()) {
                        result += depth == 1 ? 1 : moveGenerationPerft(searchData, depth - 1);
                }
                pos->backward(move);
        //}
    }
    return result;
}

void movePerftDivide(TSearchData * searchData, int depth) {
    int count = genAllMoves(searchData->getPosition(), searchData->getMoveList());
    for (int i = 0; i < count; i++) {
        TMove * move = &searchData->getMoveList()->get(i)->move;
        searchData->getPosition()->forward(move);
        if (searchData->getPosition()->legal()) {
            std::cout << move->asString() << " " << moveGenerationPerft(searchData, depth) << std::endl;
        }
        searchData->getPosition()->backward(move);

    }
}

/**
 * Move generaration test (perft test)
 */
void testMoveGeneration(string fen, int targetValues[], int maxDepth) {
    std::cout << "tests test testMoveGeneration " << fen << std::endl;
    TSearchData * searchData = new TSearchData(fen.c_str());
    for (int i = 0; i < maxDepth; i++) {
        int count = moveGenerationPerft(searchData, i + 1);
        if (false && count != targetValues[i]) {
            std::cout << "%TEST_FAILED% time=0 testname=testMoveGeneration (tests) message=depth "
                    << i + 1 << " node mismatch: " << count << std::endl;
            movePerftDivide(searchData, i);
            break;
        }
        string fen_test = searchData->getPosition()->asFen();
        if (fen.compare(fen_test) != 0) {
            std::cout << "%TEST_FAILED% time=0 testname=testMoveGeneration (tests) message=depth "
                    << i + 1 << ": fen mismatch " << fen_test << std::endl;
            break;
        }
    }
    delete searchData;
}

int main(int argc, char** argv) {
    time_t begin;
    time_t beginTest;
    time_t now;

    time(&begin);

    InitMagicMoves();
 
    std::cout << "%SUITE_STARTING% testPolyglot" << std::endl;
    std::cout << "%SUITE_STARTED%" << std::endl;

    std::cout << "%TEST_STARTED% testPolyglotKeys (testPolyglot)" << std::endl;
    time(&beginTest);
    testPolyglotKeys();
    time(&now);
    std::cout << "%TEST_FINISHED% time=" << difftime(now, beginTest) << " testPolyglotKeys (testPolyglot)" << std::endl;


    std::cout << "%TEST_STARTED% testMoveGeneration (testPolyglot)" << std::endl;
    time(&beginTest); 
    int targetValues[] = {20, 400, 8902, 197281, 4865609};
    testMoveGeneration("rnbqkbnr/pppppppp/8/8/8/8/PPPPPPPP/RNBQKBNR w KQkq - 0 1", targetValues, sizeof (targetValues)/sizeof(int));
    int targetValues2[] = {48, 2039, 97862, 4085603};
    testMoveGeneration("r3k2r/p1ppqpb1/bn2pnp1/3PN3/1p2P3/2N2Q1p/PPPBBPPP/R3K2R w KQkq - 0 1", targetValues2, sizeof (targetValues2)/sizeof(int));
    int targetValues3[] = {14, 191, 2812, 43238, 674624};
    testMoveGeneration("8/2p5/3p4/KP5r/1R3p1k/8/4P1P1/8 w - - 0 1", targetValues3, sizeof (targetValues3)/sizeof(int));

    time(&now);
    std::cout << "%TEST_FINISHED% time=" << difftime(now, beginTest) << " testMoveGeneration (testPolyglot)" << std::endl;

    std::cout << "%SUITE_FINISHED% time=" << difftime(now, begin) << std::endl;

    return (EXIT_SUCCESS);
}

