/* 
 * File:   evaluation_test.cpp
 * Author: Hajewiet
 *
 * Created on 31-mei-2011, 16:39:55
 */

#include "engine.h"

/*
 * Simple C++ Test Suite
 */

engine_t * global_engine;
bool test_stop;

void testFail(int idx, std::string operation, int score, int bound, std::string fen, engine_t * engine) {
    test_stop = true;
    std::cout << "Test " << idx << ": " << fen;
    std::cout << ": " << score << " " << operation << " " << bound << " ";
    std::cout << std::endl;
    std::cout << "%TEST_FAILED% time=0 testname=evaluation (test_evaluation) message=evaluation test failed" << std::endl;
    engine->new_game(fen.c_str());
    engine->analyse();
}

int testEval(int idx, std::string fen, int min_score, int max_score) {
    if (test_stop) {
        return 0;
    }
    search_t * s = new search_t(fen.c_str());
    int score = evaluate(s);
    if (score < min_score) {
        testFail(idx, "<", score, min_score, fen, global_engine);
    } else if (score > max_score) {
        testFail(idx, ">", score, max_score, fen, global_engine);
    }
    delete s;
    return score;
}

void testKingAttackZero(int idx, std::string fen) {
    if (test_stop) {
        return;
    }
    search_t * s = new search_t(fen.c_str());
    evaluate(s);
    int score = s->stack->pc_score[WKING].mg;
    if (score != 0) {
        testFail(idx, "!=", 0, score, fen, global_engine);
    } else {
        score = s->stack->pc_score[BKING].mg;
        if (score != 0) {
            testFail(idx, "!=", 0, score, fen, global_engine);
        }
    }
    delete s;
}

void testEvaluationSuite() {

    const int DRAW_MAX = VPAWN / 10;
    const int DRAWISH_MAX = VPAWN / 2;
    const int SCORE_MAX = 2 * score::WIN;
    const int BONUS = VPAWN / 10;
    const int HALF_PAWN = VPAWN / 2;
    const int QUARTER_PAWN = VPAWN / 4;

    /*
     * Endgames
     */
    // case 0:  ----- ------ vs ----- ------ (KK)

    testEval(0, "6k1/8/8/8/8/8/8/1K6 w - - 0 1", -DRAW_MAX, DRAW_MAX);

    // case 1:  pawns ------ vs ----- ------
    testEval(1001, "7k/8/8/8/8/8/P7/K7 w - - 0 1", score::WIN / 2, score::WIN); //KPK
    testEval(1002, "8/1k6/8/8/8/7K/7P/8 w - - 0 1", score::WIN / 2, score::WIN); //KPK
    testEval(1003, "8/2k5/8/8/8/7K/7P/8 w - - 0 1", 1, DRAW_MAX); //KPK
    testEval(1004, "2k5/8/8/8/8/8/2P5/2K5 w - - 0 1", score::WIN / 2, score::WIN); //KPK
    testEval(1005, "2k5/8/8/8/8/2P5/8/2K5 w - - 0 1", 1, DRAW_MAX); //KPK

    // case 2: ----- ------ vs pawns ------
    // this should be an impossible case, with the better side only having a king
    testEval(2001, "k7/P6p/8/8/8/8/7P/7K w - - 0 2", VPAWN/2, VPAWN);
    testEval(2002, "k7/P5pp/1P6/8/8/8/7P/7K w - - 0 1", VPAWN, score::WIN);

    // case 3: pawns ------ vs pawns ------ (pawn endings))
    testEval(3001, "8/5p2/PK1kp1p1/8/8/8/1P3P2/8 w - - 1 2", score::WIN/4, score::WIN/2); //unstoppable pawn
    testEval(3002, "8/P3K1kp/8/8/8/8/8/8 w - - 1 1", score::WIN/4, score::WIN/2); //unstoppable pawn
    testEval(3003, "7K/8/k1P5/7p/8/8/8/8 w - - 0 1", 0, VPAWN); //famous study by Réti
    testEval(3004, "6k1/p4ppp/8/8/8/8/PP3PPP/6K1 w - - 0 1", VPAWN + BONUS, VPAWN + 4 * BONUS); //isolated pawn
    testEval(3005, "2k5/1pp5/8/8/3PP3/4K3/8/8 w - - 0 1", 3 * BONUS, 7 * BONUS); //piece square tables
    testEval(3006, "8/5pk1/8/4K3/8/8/5P2/8 w - - 0 1", BONUS, HALF_PAWN); //piece square tables
    testEval(3007, "4k3/4pp2/4p3/8/8/8/3PPP2/4K3 w - - 0 1", BONUS, QUARTER_PAWN); //doubled pawn
    testEval(3008, "k7/3p4/4p3/4P3/8/3p4/3P1P2/7K w - - 0 1", -BONUS, BONUS); //backward pawns
    testEval(3009, "8/p2p1k2/4p3/4P3/PP6/4K3/8/8 w - - 0 1", 3 * BONUS, 8 * BONUS); //candidate
    testEval(3010, "8/p1pp1k2/8/2P5/PP6/4K3/8/8 w - - 0 1", 4 * BONUS, 6 * VPAWN); //better candidate
    

    // case 4:  ----- pieces vs ----- ------
    testEval(4001, "7k/8/6K1/3N4/8/8/8/8 w - - 0 1", 1, DRAW_MAX); //KNK
    testEval(4002, "7k/8/6K1/8/5B2/8/8/8 w - - 0 1", 1, DRAW_MAX); //KBK
    testEval(4003, "5k2/8/2N2K2/8/5N2/8/8/8 w - - 0 1", 1, DRAW_MAX); //KNNK
    testEval(4004, "8/8/8/8/4k3/8/7K/7R w - - 0 1", score::WIN, SCORE_MAX); //KRK
    testEval(4005, "8/8/8/8/4k3/8/Q7/K7 w - - 0 1", score::WIN, SCORE_MAX); //KQK
    testEval(4006, "8/8/8/8/4k3/8/8/K2B2B1 w - - 0 1", score::WIN, SCORE_MAX); //KBBK
    testEval(4007, "K7/2kB4/8/8/8/8/8/5N2 w - - 0 1", score::WIN, SCORE_MAX); //KBNK

    // case 5:  pawns pieces vs ----- ------

    // case 6:  ----- pieces vs pawns ------
    testEval(6001, "8/8/K7/8/8/8/4kpQ1/8 w - - 0 1", 1, DRAWISH_MAX); //KQKP
    testEval(6002, "R7/4K3/8/8/4kp2/8/8/8 w - - 66 1", 1, DRAWISH_MAX); //KRKP

    // case 7:  pawns pieces vs pawns ------
    // case 8:  ----- ------ vs ----- pieces
    // case 9:  pawns ------ vs ----- pieces
    // case 10: ----- ------ vs pawns pieces
    // case 11: pawns ------ vs pawns pieces

    // case 12: ----- pieces vs ----- pieces
    testEval(12001, "8/8/5k2/4n3/8/2K5/1R6/8 w - - 0 1", 1, VPAWN); //KRKN
    testEval(12002, "7R/8/8/2n5/6k1/r7/4K3/8 w - - 0 78", -VPAWN, -1);
    testEval(12003, "K7/8/8/8/4n3/4k3/8/B6B w - - 0 1", score::WIN / 2, score::WIN); //KBBKN is mostly won
    testEval(12004, "8/K7/8/4k3/3n4/2N5/R7/8 w - - 0 1", score::WIN / 4, score::WIN / 2);
    testEval(12005, "6r1/K7/8/4k3/3n4/2N5/R7/1R6 w - - 0 1", score::WIN / 8, score::WIN / 4);
    testEval(12006, "8/8/8/4kr2/8/8/8/3QK3 w - - 0 1", score::WIN / 8, score::WIN / 3); //KQKR
    testEval(12007, "8/8/8/4kr2/8/8/4P3/3QK3 w - - 0 1", score::WIN / 8, score::WIN / 3); //KQPKR

    // case 13: pawns pieces vs ----- pieces
    testEval(13001, "8/6k1/8/8/8/8/PK6/R7 w - - 0 1", score::WIN, SCORE_MAX); //KRPK
    testEval(13002, "8/8/8/4b3/3k4/2N5/RP6/K7 w - - 0 1", score::WIN / 4, score::WIN / 2);
    testEval(13003, "8/8/8/4b3/3k4/2N1q3/RP6/KQ6 w - - 0 1", score::WIN / 8, score::WIN / 3);
    testEval(13004, "8/8/8/8/6k1/6bp/8/6K1 w - - 0 1", -DRAW_MAX, -1); //KBPK
    testEval(13005, "8/Pk6/8/1N6/8/8/5K2/8 w - - 0 1", 1, DRAW_MAX); //KNPK
    testEval(13006, "8/8/4k3/3N1b2/3K1P2/8/8/8 w - - 7 1", 1, DRAWISH_MAX); //KNPKB

    // case 14: ----- pieces vs pawns pieces
    testEval(14001, "K2k4/PR6/8/8/2q5/8/8/8 w - - 0 169", -250, -150);
    testEval(14002, "7r/8/8/5k2/4p3/2KNR3/8/8 w - - 12 1", 0, VPAWN);
    testEval(14003, "8/8/k5r1/2P5/PQ6/KP6/4q3/8 w - - 0 1", -250, -VPAWN);

    // case 15: pawns pieces vs pawns pieces
    testEval(15001, "8/1K6/3k4/P2p4/8/4b3/4B3/8 w - - 27 1", -DRAW_MAX, DRAW_MAX); //opp. bishops


    /*
     * Opening
     */

    //start position: white has advantage of the first move
    //testEval(1, "rnbqkbnr/pppppppp/8/8/8/8/PPPPPPPP/RNBQKBNR w KQkq - 0 1", 1, SCORE_DRAW_MAX);

    //king shelter
    //testKingAttackZero(2, "r1bq1rk1/ppppbppp/2n2n2/4p3/2B1P3/3P1N2/PPP2PPP/RNBQ1RK1 w - - 3 6");






    /*    
        
        testEval(6, "8/8/4k3/3N1b2/3K1P2/8/8/8 w - - 7 1", 1, SCORE_DRAW_MAX);
       
        testEval(8, "8/8/8/2k2p2/1q6/7R/2K5/8 w - - 0 29", -SCORE_MATE, -score::WIN / 8);

        //no pawns, piece up but no mating power
        testEval(9, "8/K7/8/4k3/3p4/8/4B3/8 w - - 0 4", SCORE_DRAW_MIN, SCORE_DRAW_MAX);
    
        
     * 
     * testEval("8/8/8/pB1k4/P5r1/3K4/8/8 w - - 13 32", -VPAWN, -1); //KBPKRP -> draw
        
     */
}

int main(int argc, char** argv) {
    std::cout << "%SUITE_STARTING% evaluation_test" << std::endl;
    std::cout << "%SUITE_STARTED%" << std::endl;
    std::cout << "%TEST_STARTED% test1 (evaluation_test)" << std::endl;
    test_stop = false;
    magic::init();
    global_engine = engine::instance();
    engine::settings()->max_depth = 20;
    testEvaluationSuite();
    std::cout << "%TEST_FINISHED% time=0 test1 (evaluation_test)" << std::endl;
    std::cout << "%SUITE_FINISHED% time=0" << std::endl;
    return (EXIT_SUCCESS);
}

