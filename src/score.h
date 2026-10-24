/**
 * Maxima, a chess playing program. 
 * Copyright (C) 1996-2026 Erik van het Hof and Hermen Reitsma 
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
 * File: score.h
 * Search score constants, bounds, mate distances and result flags.
 *
 * Created on 21 september 2013, 23:09
 */

#ifndef SCORE_H
#define	SCORE_H

namespace score {

    enum constants {
        INF = 32000,
        ILLEGAL = 32001,
        INVALID = 32002,
        MATE = 30000,
        DEEPEST_MATE = 29500,
        TABLEBASE = 28000,
        WIN = 5000,
        DRAW = 0,
        LOWERBOUND = 1,
        UPPERBOUND = 2,
        EXACT = 3
    };
    
    /**
     * Tests if a score is valid (and not INF, ILLEGAL, INVALID)
     * @param score score to test
     * @return true is the score is valid, false otherwise
     */
    inline bool is_valid(int score) {
        return score >= -MATE && score < MATE;
    }

    /**
     * Returns distance to mate
     * @param score 
     * @return ply distance 
     */
    inline int mate_in_ply(int score) {
        assert(score::is_valid(score));
        return score > DEEPEST_MATE ? MATE - score : 0;
    }

    /**
     * Returns distance to getting mated
     * @param score
     * @return ply distance
     */
    inline int mated_in_ply(int score) {
        assert(score::is_valid(score));
        return score < -DEEPEST_MATE ? MATE + score : 0;
    }

    /**
     * Test if score is a mate score
     * @param score score to test
     * @return true if mate(d), false otherwise
     */
    inline bool is_mate(int score) {
        assert(score::is_valid(score));
        return score < -DEEPEST_MATE || score > DEEPEST_MATE;
    }
    
    /**
     * Test if score reached winning level
     * @param score score to test
     * @return true if win level, false otherwise
     */
    inline bool is_win(int score) {
        assert(score::is_valid(score));
        return score < - WIN || score > WIN;
    }

    /**
     * Returns if a score is a lower bound, upper bound or exact
     * @param score score to test
     * @return bound flag
     */
    inline int flags(int score, int alpha, int beta) {
        assert(score::is_valid(score));
        return score >= beta ? LOWERBOUND : (score <= alpha ? UPPERBOUND : EXACT);
    }
    
}

#endif
