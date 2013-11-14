/* 
 * File:   opponent.h
 * Author: Twin
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

