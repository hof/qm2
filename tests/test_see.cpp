/* 
 * File:   test_polyglot.cpp
 * Author: Hajewiet
 *
 * Created on 7-mei-2011, 9:52:07
 */


#include "board.h"
#include "move.h"

/*
 * Simple C++ Test Suite
 */


int see(board_t * brd, move_t * move) {
    int captured_piece = move->capture;
    int moving_piece = move->piece;
    int captured_val = board::PVAL[captured_piece];
    
    /*
     * 0. If the king captures, it's always a gain
     */
    if (moving_piece == WKING || moving_piece == BKING) {
        //std::cout << "Early cut (King)" << std::endl;
        return captured_val;
    }

    /*
     * 1. if a piece captures a higher valued piece 
     * return quickly as we are (almost) sure this capture gains material.
     * (e.g. pawn x knight)
     */
    int piece_val = board::PVAL[moving_piece];
    if (piece_val < captured_val) {
        //std::cout << "Early cut (cap > val)" << std::endl;
        return captured_val - piece_val;
    }

    /*
     * 2. if a piece captures a lower value piece that is defended by
     * a pawn, return a negative score quickly
     * (e.g. rook captures knight that is defended by a pawn)
     */
    int tsq = move->tsq;
    if (captured_val && piece_val > captured_val && brd->is_attacked_by_pawn(tsq, moving_piece > WKING)) {
        //std::cout << "Early cut (val > cap && defended)" << std::endl;
        return captured_val - piece_val;   
    }

    /*
     * 3. full static exchange evaluation using the swap algoritm
     */
    bool wtm = moving_piece > WKING;
    U64 attacks = brd->attacks_to(tsq);
    int gain[32];
    int depth = 0;
    U64 from_bit = BIT(move->ssq);
    U64 occ = brd->bb[ALLPIECES];
    const U64 diag_sliders = brd->bb[WBISHOP] | brd->bb[WQUEEN] | brd->bb[BBISHOP] | brd->bb[BQUEEN];
    const U64 hor_ver_sliders = brd->bb[WROOK] | brd->bb[WQUEEN] | brd->bb[BROOK] | brd->bb[BQUEEN];
    const U64 xrays = diag_sliders | hor_ver_sliders | brd->bb[WPAWN] | brd->bb[BPAWN];
    
    if (!captured_piece && (moving_piece == WPAWN || moving_piece == BPAWN)) {
        //set the non-capturing pawn move ssq bit in the attacks bitboard, 
        //so it will be removed again in the swap loop
        attacks ^= from_bit;
    }

    gain[0] = captured_val;
    do {
        depth++;
        gain[depth] = board::PVAL[moving_piece] - gain[depth - 1];
        
        //std::cout << "Depth: " << depth << " Gain: " << gain[depth];
        //bb_print("\nAttacks", attacks);
        
        attacks ^= from_bit;
        occ ^= from_bit;
        if (from_bit & xrays) {
            attacks |= magic::bishop_moves(tsq, occ) & occ & diag_sliders;
            attacks |= magic::rook_moves(tsq, occ) & occ & hor_ver_sliders;
        }
        from_bit = brd->smallest_attacker(attacks, wtm, moving_piece);
        wtm = !wtm;
    } while (from_bit);
    while (--depth) {
        gain[depth - 1] = -MAX(-gain[depth - 1], gain[depth]);
    }
    return gain[0];
}

bool assert_see(board_t * brd, move_t * mv, int expected) {
    int val_1 = brd->see(mv);
    int val_2 = see(brd, mv);
    std::cout << brd->to_string() << " " << mv->to_string();
    if (val_1 == expected && val_1 == val_2) {
        std::cout << " ok";
    } else {
        std::cout << " FAIL " << expected << " != " << val_1 << " != " << val_2;
    }
    std::cout << std::endl;
    return val_1 == expected && val_1 == val_2;
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

