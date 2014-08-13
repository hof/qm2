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
 * File: outputhandler.cpp
 * Sends text strings (output) following the UCI specifications by Stefan-Meyer Kahlen.
 * See http://wbec-ridderkerk.nl/html/UCIProtocol.html
 */

#include "outputhandler.h"
#include "bits.h"
#include "move.h"
#include "search.h"

void TOutputHandler::sendPV(int cpScore, int depth, int selDepth, U64 nodes, int time, const char * pv, int type) {
    std::string outputString = "info depth ";
    outputString += ntos(depth);
    outputString += " seldepth ";
    outputString += ntos(MAX(depth, selDepth));
    if (ABS(cpScore) < score::MATE-MAX_PLY) {
        outputString += " score cp "; //info score cp 14  depth 2 nodes 255 time 15 pv f1c4 f8c5 
        outputString += ntos(cpScore);
        if (type) {
            outputString += type == 1 ? " upperbound" : " lowerbound";
        }
    } else {
        int mateInMoves = (score::MATE - ABS(cpScore)+1)/2;
        outputString += " score mate "; //info mate 12 (in moves, not plies!)
        outputString += ntos(cpScore>0? mateInMoves : -mateInMoves);
    }


    outputString += " nodes ";
    outputString += ntos(nodes);
    outputString += " time ";
    outputString += ntos(time);
    outputString += " nps ";
    int nps = nodes;
    if (time > 10) {
        nps = (1000 * U64(nodes)) / (time);
    }
    outputString += ntos(nps);
    outputString += " pv ";
    outputString += pv;

    output(outputString);
}

void TOutputHandler::sendNPS(int nps) {
    std::string outputString = "info nps "; //info score cp 14  depth 2 nodes 255 time 15 pv f1c4 f8c5 
    outputString += ntos(nps);
    output(outputString);
}

void TOutputHandler::sendEvalStats(int evaluations, int pawnEvaluations, int fullEvaluations) {
    if (evaluations) {
        std::string outputString = "info string evals:";
        outputString += ntos(evaluations);
        outputString += " pawn:";
        outputString += ntos(pawnEvaluations);
        int pct = (100 * U64(pawnEvaluations)) / evaluations;
        outputString += "(";
        outputString += ntos(pct);
        outputString += "%) full:";
        pct = (100 * U64(fullEvaluations)) / evaluations;
        outputString += ntos(fullEvaluations);
        outputString += "(";
        outputString += ntos(pct);
        outputString += "%)";
        output(outputString);
    }
}

void TOutputHandler::sendHashTableStats(int ttHits, int ptHits, int mtHits, int etHits) {
    std::string outputString = "info string tt:";
    outputString += ntos(ttHits);
    outputString += "% pt:";
    outputString += ntos(ptHits);
    outputString += "% mt:";
    outputString += ntos(mtHits);
    outputString += "% et:";
    outputString += ntos(etHits);
    outputString += "%";
    output(outputString);
}

void TOutputHandler::sendBestMove(move_t bestMove, move_t ponderMove) {
    if (bestMove.piece) {
        std::string outputString = "bestmove ";
        outputString += bestMove.to_string();
        if (ponderMove.piece) {
            outputString += " ponder ";
            outputString += ponderMove.to_string();
        }
        output(outputString);
    }
}
