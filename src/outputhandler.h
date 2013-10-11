/* 
 * File:   outputhandler.h
 * Author: Hajewiet
 *
 * Created on 21 juni 2011, 23:24
 */

#ifndef OUTPUTHANDLER_H
#define	OUTPUTHANDLER_H


#include <cstdlib>
#include <iostream>
#include <string.h>

#include "defs.h"
#include "move.h"
#include <fstream>


class TOutputHandler {
private:

    std::string ntos(int number) {
        std::string result = "";
        char buf[128];
        sprintf(buf, "%d", number);
        result = buf;
        return result;
    }

    std::string n64tos(U64 number) {
        std::string result = "";
        char buf[128];
        sprintf(buf, "%llu", number);
        result = buf;
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
        output("id name QM2");
        output("id author QM2 team");
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

    void sendBestMove(TMove bestMove, TMove ponderMove);

    void sendPV(int cpScore, int depth, int selDepth, U64 nodes, int time, const char * pv, int type);

    void sendNPS(int nps);

    void sendEvalStats(int evaluations, int pawnEvaluations, int fullEvaluations);

    void sendHashTableStats(int ttHits, int ptHits, int mtHits, int etHits);





};

#endif	/* OUTPUTHANDLER_H */

