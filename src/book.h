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

#include "movegen.h"

struct book_entry_t {
    U64 key;
    unsigned short move;
    unsigned short weight;
    unsigned short learn1;
    unsigned short learn2;
};

class book_t : private std::ifstream {
    
public:
    
    book_t() {
        book_size = 0;
        file_name = "";
    }
    
    ~book_t() {
        close();
    }
    void open(const std::string& file_name);
    void close();
    const std::string get_file_name();
    static U64 polyglot_key(board_t* pos);
    int find(board_t * pos, move::list_t * list);
    void read_polyglot_move(board_t * pos, move_t * move, int polyglot_move);

private:
    std::string file_name;
    int book_size;  
    U64 read_integer(int size);
    void read_entry(book_entry_t &e, int n);
    int find_key(U64 key);

    book_t& operator>>(U64& n) {
        n = read_integer(8);
        return *this;
    }

    book_t& operator>>(uint16_t &n) {
        n = (uint16_t) read_integer(2);
        return *this;
    }

    void operator>>(book_entry_t &e) {
        *this >> e.key >> e.move >> e.weight >> e.learn1 >> e.learn2;
    }
    
};

namespace book {
    void open(const std::string& file_name);
    void close();
    int find(board_t * pos, move::list_t * list);
};

#endif	/* BOOK_H */

