#include "move.h"
#include "defs.h"
#include "board.h"

string TMove::asString() { //default: xboard notation
    string result = "";
    int ssq = this->ssq;
    int tsq = this->tsq;
    if (ssq == tsq) {
        result = "null";
    } else {
        result = FILE_SYMBOL(ssq);
        result += RANK_SYMBOL(ssq);
        result += FILE_SYMBOL(tsq);
        result += RANK_SYMBOL(tsq);
        if (this->promotion) {
            const char PIECE_SYMBOL[13] = {'.', 'p', 'n', 'b', 'r', 'q', 'k', 'p', 'n', 'b', 'r', 'q', 'k'};
            result += PIECE_SYMBOL[this->promotion];
        }
    }
    return result;
}

void TMove::fromString(TBoard * pos, const char * moveStr) { //default: xboard notation
    int file = *moveStr++ -'a';
    int rank = *moveStr++ -'1';
    ssq = (rank << 3) | file;
    file = *moveStr++ -'a';
    rank = *moveStr++ -'1';
    tsq = (rank << 3) | file;
    piece = pos->Matrix[ssq];
    assert(piece);
    capture = pos->Matrix[tsq];
    castle = 0;
    en_passant = false;
    if (tsq == pos->boardFlags->epSquare 
            && (piece == WPAWN || piece == BPAWN)
            && pos->boardFlags->epSquare) {
        en_passant = true;
        capture = piece == WPAWN ? BPAWN : WPAWN;
    } else if (piece == WKING && ssq == e1) {
        if (tsq == g1) {
            castle = CASTLE_K;
        } else if (tsq == c1) {
            castle = CASTLE_Q;
        } 
    } else if (piece == BKING && ssq == e8) {
        if (tsq == g8) {
            castle = CASTLE_k;
        } else if (tsq == c8) {
            castle = CASTLE_q;
        }
    }
    promotion = 0;
    if (*moveStr) {
        switch (*moveStr) {
            case 'n':
                promotion = WKNIGHT;
                break;
            case 'b':
                promotion = WBISHOP;
                break;
            case 'r':
                promotion = WROOK;
                break;
            default:
                promotion = WQUEEN;
                break;
        }
        if (pos->boardFlags->WTM == false) {
            promotion += WKING;
        }
    }
}
