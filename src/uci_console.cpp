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
 * File: uci_console.cpp
 * Handling text input from the console following the UCI protocol by Stefan-Meyer Kahlen.
 * See http://wbec-ridderkerk.nl/html/UCIProtocol.html
 */

#include "uci_console.h"

#define __DO_LOG

namespace uci {

    const std::string start_fen = "rnbqkbnr/pppppppp/8/8/8/8/PPPPPPPP/RNBQKBNR w KQkq - 0 1";
    std::string fen = start_fen;
    bool _silent = false;

    std::string itoa(int64_t number) {
        std::stringstream tmp;
        tmp << number;
        return tmp.str();
    }

    template<class any_t>
    any_t atoi(const std::string& s) {
        std::istringstream stream(s);
        any_t t;
        stream >> t;
        return t;
    }

    void silent(bool on) {
        _silent = on;
    }

    void out(std::string str) {
        if (_silent) {
            return;
        }
#ifdef DO_LOG
        std::ofstream myfile;
        myfile.open("uci.log", std::ios::app);
        myfile << "< " << str << std::endl;
        myfile.close();
#endif
        std::cout << str << std::endl;
    }

    /**
     * Handle input command from stdin
     * @param cmd input from stdin
     * @return true unless the input command requests an exit
     */
    bool in(std::string cmd) {
#ifdef DO_LOG
        std::ofstream myfile;
        myfile.open("uci.log", std::ios::app);
        myfile << "> " << cmd << std::endl;
        myfile.close();
#endif
        bool result = true;
        input_parser_t parser(cmd);

        std::string token;

        if (parser >> token) { // operator>>() skips any whitespace
            if (token == "quit" || token == "exit" || token == "bye") {
                handle_stop();
                result = false;
            } else if (token == "uci") {
                result = handle_uci();
            } else if (token == "isready") {
                result = handle_isready();
            } else if (token == "stop") {
                result = handle_stop();
            } else if (token == "ucinewgame") {
                result = handle_ucinewgame();
            } else if (token == "go") {
                result = handle_go(parser);
            } else if (token == "position") {
                result = handle_position(parser);
            } else if (token == "forward") {
                result = handle_forward(parser);
            } else if (token == "setoption") {
                result = handle_setoption(parser);
            } else if (token == "ponderhit") {
                result = handle_ponderhit();
            } else if (token == "eval") {
                result = handle_eval(parser);
            } else if (token == "learn") {
                result = handle_learn(parser);
            }
        }
        return result;
    }

    bool handle_uci() {
        send_id();
        send_options();
        send_ok();
        return true;
    }

    bool handle_isready() {
        send_ready();
        return true;
    }

    bool handle_ucinewgame() {
        fen = start_fen;
        engine::new_game(fen);
        return true;
    }

    bool handle_stop() {
        engine::stop();
        return true;
    }

    bool handle_go(input_parser_t &parser) {
        bool result = true;
        std::string token;
        engine::settings()->clear();
        while (parser >> token) {
            if (token == "infinite") {
                engine::settings()->max_depth = MAX_PLY;
            } else if (token == "ponder") {
                engine::settings()->max_depth = MAX_PLY;
                engine::settings()->ponder = true;
            } else if (token == "wtime") {
                parser >> engine::settings()->white_time;
            } else if (token == "btime") {
                parser >> engine::settings()->black_time;
            } else if (token == "winc") {
                parser >> engine::settings()->white_increment;
            } else if (token == "binc") {
                parser >> engine::settings()->black_increment;
            } else if (token == "movestogo") {
                parser >> engine::settings()->moves_left;
            } else if (token == "depth") {
                parser >> engine::settings()->max_depth;
            } else if (token == "nodes") {
                parser >> engine::settings()->max_nodes;
            } else if (token == "movetime") {
                parser >> engine::settings()->max_time_per_move;
            } else if (token == "searchmoves") {

            }
        }
        engine::set_ponder(engine::settings()->ponder);
        engine::set_position(fen);
        engine::go();
        return result;
    }

    bool handle_position(input_parser_t & parser) {
        bool result = true;
        std::string token;
        if (parser >> token) {
            if (token == "startpos") {
                fen = start_fen;
                parser >> token;
            } else if (token == "fen") {
                fen = "";
                while (parser >> token && token != "moves") {
                    fen += token;
                    fen += ' ';
                }
            } else { //should never happen
                fen = start_fen;
                parser >> token;
            }
            board_t brd;
            brd.init(fen.c_str());
            if (token == "moves") {
                rep_table::store(brd.stack->fifty_count, brd.stack->tt_key);
                while (parser >> token) {
                    move_t move;
                    move.set(&brd, token.c_str());
                    brd.forward(&move);
                    rep_table::store(brd.stack->fifty_count, brd.stack->tt_key);
                    if (brd.ply > MAX_PLY - 2) {
                        brd.init(brd.to_string().c_str()); //preventing overflow
                    }
                }
            }
            fen = brd.to_string();
        }
        return result;
    }

    bool handle_forward(input_parser_t & parser) {
        bool result = true;
        std::string token;
        board_t brd;
        move_t move;
        brd.init(fen.c_str());
        while (parser >> token) {
            move.set(&brd, token.c_str());
            brd.forward(&move);
        }
        fen = brd.to_string();
        return result;
    }

    bool handle_setoption(input_parser_t & parser) {
        bool result = true;
        bool handled = false;
        std::string token, name, value;
        name = "unknown";
        if ((parser >> token) || token == "name") {
            if (parser >> name) {
                while (parser >> token && token != "value") {
                    name += (" " + token);
                }
                if (token == "value") {
                    if (parser >> value) {
                        while (parser >> token) {
                            value += (" " + token);
                        }
                        
                        //parse option value
                        options::option_t * opt = options::get_option(name.c_str());
                        handled = std::string(opt->key) == name;
                        opt->value = 0;
                        if (opt->type == options::BOOL) {
                            opt->value = value == "true" || value == "1";
                        } else if (opt->type == options::INT) {
                            opt->value = atoi<int>(value);
                        } else if (name == "Wild") {
                            if (value == "losers" || value == "17") {
                                opt->value = 17;
                            } 
                        } 
                        
                        //handle option
                        if (name == "Hash") {
                            trans_table::set_size(opt->value);
                        }
                    }
                }
            }
        }
        if (!handled) {
            send_unknown_option(name);
        }
        return result;
    }

    bool handle_ponderhit() {
        engine::set_ponder(false);
        return true;
    }

    /*
     * XLearn can be used to determine if a new evaluation or search 
     * feature gives better performance, and what is the ideal score
     * 
     */
    bool handle_learn(input_parser_t & parser) {
        engine::settings()->clear();
        engine::set_ponder(false);
        engine::set_position(fen);
        engine::learn();
        return true;
    }

    /*
     * Eval prints the evaluation result on the current position
     */
    bool handle_eval(input_parser_t & parser) {
        engine::settings()->clear();
        engine::set_ponder(false);
        engine::settings()->max_depth = 1;
        std::string token;
        std::string pfen = "";
        while (parser >> token) {
            pfen += token;
            pfen += ' ';
        }
        if (pfen == "") {
            pfen = fen;
        }
        engine::new_game(pfen);
        engine::analyse();
        return true;
    }

    void send_id() {
        out("id name Maxima 2.0");
        out("id author Hermen Reitsma and Erik van het Hof");
    }

    void send_options() {
        for (int i = 1; i < options::length; i++) {
            const options::option_t * opt = &options::PARAM[i];
            out(std::string("option name ") + std::string(opt->key) + std::string(" ") + std::string(opt->uci_option));
        }
    }

    void send_ok() {
        out("uciok");
    }

    void send_ready() {
        out("readyok");
    }

    void send_unknown_option(std::string option) {
        out("No such option: " + option);
    }

    void send_string(std::string msg) {
        out("info string " + msg);
    }

    void send_pv(int cp_score, int depth, int sel_depth, U64 nodes, int time, const char * pv, int flag) {
        std::string msg = "info depth " + itoa(depth) + " seldepth " + itoa(MAX(depth, sel_depth));
        if (ABS(cp_score) < score::DEEPEST_MATE) {
            msg += " score cp " + itoa(cp_score);
            if (flag == score::UPPERBOUND) {
                msg += " upperbound";
            } else if (flag == score::LOWERBOUND) {
                msg += " lowerbound";
            }
        } else { //mate score
            int mate_in_moves = (score::MATE - ABS(cp_score) + 1) / 2;
            msg += " score mate ";
            msg += itoa(cp_score > 0 ? mate_in_moves : -mate_in_moves);
        }
        msg += " nodes " + itoa(nodes) + " time " + itoa(time) + " nps ";
        int nps = time < 50 ? nodes : (1000 * U64(nodes)) / time;
        msg += itoa(nps) + " pv " + pv;
        out(msg);
    }

    void send_bestmove(move_t move, move_t ponder_move) {
        if (move.piece > 0) {
            std::string msg = "bestmove " + move.to_string();
            if (ponder_move.piece > 0) {
                msg += " ponder " + ponder_move.to_string();
            }
            out(msg);
        }
    }
};
