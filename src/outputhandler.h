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
 * File: outputhandler.h
 * Sends text strings (output) following the UCI specifications by Stefan-Meyer Kahlen.
 * See http://wbec-ridderkerk.nl/html/UCIProtocol.html
 *
 * Created on 21 juni 2011, 23:24
 */

#ifndef OUTPUTHANDLER_H
#define	OUTPUTHANDLER_H


#include <cstdlib>
#include <iostream>
#include <string.h>
#include <time.h>

#include "bits.h"
#include "move.h"
#include <fstream>
#include <sstream>

class TOutputHandler {
private:

    std::string ntos(int64_t number) {
        std::string result = "";
        std::stringstream tmp;
        tmp << number;
        result = tmp.str();
        return result;
    }

public:
    volatile bool engineStop;
    volatile bool enginePonder;

    TOutputHandler() {
        engineStop = false;
        enginePonder = false;
    }

    static inline void output(std::string str) {
        //std::ofstream myfile;
        //myfile.open("uci.log", std::ios::app);
        //myfile << "< " << str << std::endl;
        //myfile.close();
        std::cout << str << std::endl;
    }

    void sendID() {
        output("id name Maxima 2.0 dev");
        output("id author Hermen Reitsma and Erik van het Hof");
    }

    void sendOptions() {
        output("option name Hash type spin default 128 min 0 max 1024");
        output("option name Ponder type check default true");
        output("option name OwnBook type check default true");
        output("option name UCI_AnalyaseMode type check default false");
        output("option name UCI_Opponent type string");
    }

    void sendUCIOk() {
        output("uciok");
    }

    void sendUnknownOption(std::string optionName) {
        output("No such option: " + optionName);
    }

    void sendReady() {
        output("readyok");
    }

    void sendBestMove(move_t bestMove, move_t ponderMove);

    void sendPV(int cpScore, int depth, int selDepth, U64 nodes, int time, const char * pv, int type);

    void sendNPS(int nps);

    void sendEvalStats(int evaluations, int pawnEvaluations, int fullEvaluations);

    void sendHashTableStats(int ttHits, int ptHits, int mtHits, int etHits);





};

#endif	/* OUTPUTHANDLER_H */

