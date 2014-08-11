/**
 * Maxima, a chess playing program. 
 * Copyright (C) 1996-2014 Erik van het Hof and Hermen Reitsma 
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
 * 
 * File: book.h
 * Read Polyglot format opening books
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
    static U64 polyglot_key(board_t* pos);
    int findMoves(board_t * pos, TMoveList * list);
    void readPolyglotMove(board_t * pos, TMove * move, int polyglotMove);

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

