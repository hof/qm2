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
 * File: game.cpp
 * Holds global game information
 */

#include "game.h"

namespace options {
    
    option_t PARAM[length+1] = {
        { "", INT, 0, ""}, //dummy value
        { "Revision", STRING, 1, "type string default " MAXIMA_REVISION },
        { "Hash", INT, 256, "type spin default 256 min 0 max 1024"},
        { "Move Overhead", INT, 50, "type spin default 50 min 0 max 5000" },
        { "Ponder", BOOL, 1, "type check default true"},
        { "OwnBook", BOOL, 1, "type check default true" },
        { "UCI_AnalyseMode", BOOL, 0, "type check default false" },
        { "UCI_Opponent", STRING, 0, "type string" },
        { "NullMove", BOOL, 1, "type check default true", true },
        { "AdaptiveNullMove", BOOL, 1, "type check default true", true },
        { "BetaPruning", BOOL, 1, "type check default true", true },
        { "LMR", BOOL, 1, "type check default true", true },
        { "AdaptiveLMR", BOOL, 1, "type check default true", true },
        { "ContextLMR", BOOL, 1, "type check default true", true },
        { "LMRHistory", BOOL, 1, "type check default true", true },
        { "FutilityPruning", BOOL, 1, "type check default true", true },
        { "BroaderFutility", BOOL, 1, "type check default true", true },
        { "NullVerify", BOOL, 1, "type check default true", true },
        { "Wild", STRING, 0, "type combo default standard var standard var losers", true },
        { "DrawContempt", INT, -10, "type spin default -10 min -100 max 100", true },
        { "SyzygyPath", STRING, 0, "type string default <empty>" },
        { "SyzygyProbeLimit", INT, 5, "type spin default 5 min 0 max 5", true },
        { "SyzygyProbeDepth", INT, 1, "type spin default 1 min 0 max 64", true },
        { "BookFile", STRING, 0, "type string default book.bin" },
        { "ExperienceFile", STRING, 0, "type string default <empty>" },
        { "ExperienceDepth", INT, 16, "type spin default 16 min 1 max 128" },
        { "EvalFile", STRING, 0, "type string default eval.nnue" },
        { "Aspiration", BOOL, 1, "type check default true", true },
        { "IID", BOOL, 1, "type check default true", true },
        { "IIR", BOOL, 1, "type check default true", true },
        { "Razoring", BOOL, 1, "type check default true", true },
        { "LMP", BOOL, 1, "type check default true", true },
        { "HistoryPruning", BOOL, 1, "type check default true", true },
        { "TTEval", BOOL, 1, "type check default true", true },
        { "TTHistory", BOOL, 1, "type check default true", true },
        { "ProbCut", BOOL, 1, "type check default true", true },
        { "SingularExtensions", BOOL, 1, "type check default true", true },
        { "SingularPolicy", BOOL, 1, "type check default true", true },
        { "MainSEE", BOOL, 1, "type check default true", true },
        { "QsearchTT", BOOL, 1, "type check default true", true },
        { "MainHistory", BOOL, 1, "type check default true", true },
        { "ContinuationHistory", BOOL, 1, "type check default true", true },
        { "PawnHistory", BOOL, 1, "type check default true", true },
        { "Countermoves", BOOL, 1, "type check default true", true },
        { "Killers", BOOL, 1, "type check default true", true },
        { "UpcomingRepetition", BOOL, 1, "type check default true", true },
        { "CheckEvasions", BOOL, 1, "type check default true", true },
        { "CaptureHistory", BOOL, 1, "type check default true", true },
        { "LongContinuationHistory", BOOL, 1, "type check default true", true },
        { "ThreatOrdering", BOOL, 1, "type check default true", true },
        { "CheckOrdering", BOOL, 1, "type check default true", true },
        { "CorrectionHistory", BOOL, 1, "type check default true", true },
        { "PieceCorrection", BOOL, 1, "type check default true", true },
        { "ContinuationCorrection", BOOL, 1, "type check default true", true }
#ifdef MAXIMA_NNUE_SEARCH_AUDIT
        , { "NNUEAuditHorizon", STRING, 0, "type combo default normal var normal var fixed var quiet", true }
        , { "NNUEAuditNullMove", BOOL, 0, "type check default false", true }
        , { "NNUEAuditBetaPruning", BOOL, 0, "type check default false", true }
        , { "NNUEAuditFutility", BOOL, 0, "type check default false", true }
        , { "NNUEAuditLMR", BOOL, 0, "type check default false", true }
        , { "NNUEAuditHashMoves", BOOL, 0, "type check default false", true }
        , { "NNUEAuditIID", BOOL, 0, "type check default false", true }
        , { "NNUEAuditAspiration", BOOL, 0, "type check default false", true }
        , { "NNUEAuditDelta", BOOL, 0, "type check default false", true }
        , { "NNUEAuditSEE", BOOL, 0, "type check default false", true }
        , { "NNUEAuditCheckExtensions", BOOL, 0, "type check default false", true }
#endif
    };
    
    option_t * get_option(const char * key) {
        for (int i = 1; i <= length; i++) {
            if (strcmp(key, PARAM[i].key) == 0) {
                return &PARAM[i];
            }
        }
        return &PARAM[0];
    }
    
    int get_value(const char * key) {
        return get_option(key)->value;
    }

    const char * get_uci_option(const char * key) {
        return get_option(key)->uci_option;
    }
}

void game_t::clear() {
    tm.clear();
    go_started = {};
    target_move.clear();
    max_depth = MAX_PLY;
    max_time_per_move = 0;
    target_score = 0;
    white_time = 0;
    black_time = 0;
    white_increment = 0;
    black_increment = 0;
    moves_left = 0;
    max_nodes = 0;
    ponder = false;
    infinite = false;
    clock_limit = false;
    movetime_limit = false;
    search_moves.clear();
    learn_factor = 1.0;
}

void game_t::copy(game_t* game) {
    max_depth = game->max_depth;
    max_time_per_move = game->max_time_per_move;
    target_score = game->target_score;
    target_move.set(&game->target_move);
    white_time = game->white_time;
    black_time = game->black_time;
    white_increment = game->white_increment;
    black_increment = game->black_increment;
    moves_left = game->moves_left;
    max_nodes = game->max_nodes;
    ponder = game->ponder;
    infinite = game->infinite;
    clock_limit = game->clock_limit;
    movetime_limit = game->movetime_limit;
    search_moves = game->search_moves;
    learn_factor = game->learn_factor;
    tm = game->tm;
    go_started = game->go_started;
}

void game_t::init_tm(bool us, std::chrono::steady_clock::time_point started) {
    tm.set_start(go_started == std::chrono::steady_clock::time_point{} ? started : go_started);
    if (movetime_limit || max_time_per_move) {
        tm.set_min(max_time_per_move);
        tm.set_max(max_time_per_move);
    } else if (clock_limit || white_time || black_time) {
        tm.set(time(us), time(!us), increment(us), increment(!us), moves_left,
            options::get_value("Move Overhead"));
    } else {
        tm.set_min(time_man::INFINITE_TIME);
        tm.set_max(time_man::INFINITE_TIME);
    }

}

void game_t::test_for(move_t * move, int score, int max_n, int max_t) {
    clear();
    target_move.set(move);
    target_score = score;
    max_nodes = max_n;
    max_time_per_move = max_t? max_t : time_man::INFINITE_TIME;
    max_depth = MAX_PLY;
}

namespace game {
    game_t _game;

    game_t * instance() {
        return & _game;
    }
}

