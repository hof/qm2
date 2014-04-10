#include "outputhandler.h"
#include "defs.h"
#include "move.h"
#include "search.h"

void TOutputHandler::sendPV(int cpScore, int depth, int selDepth, U64 nodes, int time, const char * pv, int type) {
    std::string outputString = "info depth ";
    outputString += ntos(depth);
    outputString += " seldepth ";
    outputString += ntos(MAX(depth, selDepth));
    if (ABS(cpScore) < SCORE_MATE-MAX_PLY) {
        outputString += " score cp "; //info score cp 14  depth 2 nodes 255 time 15 pv f1c4 f8c5 
        outputString += ntos(cpScore);
        if (type) {
            outputString += type == 1 ? " upperbound" : " lowerbound";
        }
    } else {
        int mateInMoves = (SCORE_MATE - ABS(cpScore)+1)/2;
        outputString += " score mate "; //info mate 12 (in moves, not plies!)
        outputString += ntos(cpScore>0? mateInMoves : -mateInMoves);
    }


    outputString += " nodes ";
    outputString += n64tos(nodes);
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

void TOutputHandler::sendBestMove(TMove bestMove, TMove ponderMove) {
    if (bestMove.piece) {
        std::string outputString = "bestmove ";
        outputString += bestMove.asString();
        if (ponderMove.piece) {
            outputString += " ponder ";
            outputString += ponderMove.asString();
        }
        output(outputString);
    }
}
