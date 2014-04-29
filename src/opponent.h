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
 * File: opponent.h
 * Structure for holding information about the chess opponent
 *
 * Created on October 19, 2013, 4:25 PM
 */

#ifndef OPPONENT_H
#define	OPPONENT_H

struct TOpponent {
    std::string Title;
    std::string Name;
    bool Computer;
    int Rating;

    TOpponent() {
        clear();
    }

    void clear() {
        Title = "";
        Name = "Unknown";
        Computer = true;
        Rating = 0;
    }

    void copy(TOpponent * c) {
        Title = c->Title;
        Name = c->Name;
        Computer = c->Computer;
        Rating = c->Rating;
    }
    
    int DrawContempt(int myRating=2400) {
        int result = 0;
        if (Rating > 0) {
            int diff = myRating - Rating;
            result = Computer? 2 * diff/10 : 3*diff/10; //2400 vs 2200: +- 40 : +- 60
            result = MIN(result, 250);
            result = MAX(result, -250);
        } else {
            result = Computer? -16: -60;
        }
        return result;
    }

    void print() {
        if (Title != "" && Title != "none") {
            std::cout << Title << " ";
        }
        std::cout << Name;
        if (Computer) {
            std::cout << " (C)";
        }
        if (Rating > 0) {
            std::cout << " " << Rating;
        }
    }
};


#endif	/* OPPONENT_H */

