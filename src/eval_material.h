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
 * File: eval_material.h
 * Evaluation function for material balance
 */

#ifndef EVAL_MATERIAL_H
#define	EVAL_MATERIAL_H

class search_t;

namespace material {

    int eval(search_t * s);
    bool has_mating_power(search_t * s, bool us);
    bool has_imbalance(search_t * s, bool us);
    bool has_king_attack_force(search_t * s, bool us);
    bool has_major_imbalance(search_t * s);
    bool is_eg(search_t * s);
}

#endif	/* EVAL_MATERIAL_H */

