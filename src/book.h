/* 
 * File:   book.h
 *
 * The code in this file is based on the opening book code in PolyGlot
 * by Fabien Letouzey.  PolyGlot is available under the GNU General
 * Public License, and can be downloaded from http://wbec-ridderkerk.nl
 * 
 * Created on 30 april 2011, 17:14
 */

#ifndef BOOK_H
#define	BOOK_H

#include <fstream>
#include <string>

#include "move.h"
#include "board.h"
#include "movegen.h"

struct TBookEntry {
    U64 key;
    unsigned short move;
    unsigned short weight;
    unsigned short learn1;
    unsigned short learn2;
};

class TBook : private std::ifstream {
public:

    ~TBook() {
        close();
    }
    void open(const std::string& fName);
    void close();
    const std::string file_name();
    static U64 polyglot_key(TBoard* pos);
    int findMoves(TBoard * pos, TMoveList * list);
    void readPolyglotMove(TBoard * pos, TMove * move, int polyglotMove);

private:

    TBook& operator>>(U64& n) {
        n = read_integer(8);
        return *this;
    }

    TBook& operator>>(uint16_t& n) {
        n = (uint16_t) read_integer(2);
        return *this;
    }

    void operator>>(TBookEntry& e) {
        *this >> e.key >> e.move >> e.weight >> e.learn1 >> e.learn2;
    }

    U64 read_integer(int size);
    void read_entry(TBookEntry& e, int n);
    int find_key(U64 key);

    std::string fileName;
    int bookSize;
};


#endif	/* BOOK_H */

