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
#include "tablebase.h"
#include "experience.h"
#include "book.h"
#include "movegen.h"
#include <charconv>
#include <limits>

#define DO_LOG

namespace uci {

    const std::string start_fen = "rnbqkbnr/pppppppp/8/8/8/8/PPPPPPPP/RNBQKBNR w KQkq - 0 1";
    std::string fen = start_fen;
    std::vector<U64> position_history;
    bool _silent = false;

    std::string itoa(int64_t number) {
        std::stringstream tmp;
        tmp << number;
        return tmp.str();
    }

    template<class integer_t>
    bool parse_integer(const std::string & text, integer_t & value) {
        auto result = std::from_chars(text.data(), text.data() + text.size(), value);
        return result.ec == std::errc() && result.ptr == text.data() + text.size();
    }

    bool valid_fen(const std::string & text) {
        std::istringstream input(text);
        std::string placement, side, castles, ep, halfmove, fullmove, extra;
        if (!(input >> placement >> side >> castles >> ep >> halfmove >> fullmove)
                || (input >> extra) || (side != "w" && side != "b")) {
            return false;
        }
        int rank = 7, file = 0, white_kings = 0, black_kings = 0;
        int pieces[2] = {0, 0};
        char squares[64] = {};
        for (char symbol : placement) {
            if (symbol == '/') {
                if (file != 8 || rank == 0) return false;
                --rank;
                file = 0;
            } else if (symbol >= '1' && symbol <= '8') {
                file += symbol - '0';
            } else {
                if (std::string("PNBRQKpnbrqk").find(symbol) == std::string::npos || file >= 8) return false;
                if ((symbol == 'P' || symbol == 'p') && (rank == 0 || rank == 7)) return false;
                white_kings += symbol == 'K';
                black_kings += symbol == 'k';
                ++pieces[symbol >= 'a'];
                squares[rank * 8 + file] = symbol;
                ++file;
            }
            if (file > 8) return false;
        }
        if (rank != 0 || file != 8 || white_kings != 1 || black_kings != 1
                || pieces[0] > 16 || pieces[1] > 16) return false;
        if (castles != "-") {
            std::string seen;
            for (char symbol : castles) {
                if (std::string("KQkq").find(symbol) == std::string::npos
                        || seen.find(symbol) != std::string::npos) return false;
                if ((symbol == 'K' && (squares[e1] != 'K' || squares[h1] != 'R'))
                        || (symbol == 'Q' && (squares[e1] != 'K' || squares[a1] != 'R'))
                        || (symbol == 'k' && (squares[e8] != 'k' || squares[h8] != 'r'))
                        || (symbol == 'q' && (squares[e8] != 'k' || squares[a8] != 'r'))) return false;
                seen += symbol;
            }
        }
        if (ep != "-" && (ep.size() != 2 || ep[0] < 'a' || ep[0] > 'h'
                || ep[1] != (side == "w" ? '6' : '3'))) return false;
        if (ep != "-") {
            const int target = (ep[1] - '1') * 8 + ep[0] - 'a';
            const int direction = side == "w" ? -8 : 8;
            if (squares[target] || squares[target + direction] != (side == "w" ? 'p' : 'P')
                    || squares[target - direction]) return false;
        }
        int halfmove_count, fullmove_count;
        return parse_integer(halfmove, halfmove_count) && halfmove_count >= 0
            && halfmove_count <= std::numeric_limits<int>::max() - MAX_PLY
            && parse_integer(fullmove, fullmove_count) && fullmove_count >= 1
            && fullmove_count <= (std::numeric_limits<int>::max() - MAX_PLY) / 2;
    }

    bool apply_moves(board_t & board, input_parser_t & input, std::vector<U64> & history) {
        std::string token;
        while (input >> token) {
            move::list_t moves;
            moves.clear();
            move::gen_captures(&board, &moves);
            move::gen_promotions(&board, &moves);
            move::gen_castles(&board, &moves);
            move::gen_quiet_moves(&board, &moves);
            move_t * selected = nullptr;
            bool capture_available = false;
            for (move_t * move = moves.first; move != moves.last; ++move) {
                if (!board.legal(move)) continue;
                capture_available |= move->capture != 0;
                if (move->to_string() == token) selected = move;
            }
            if (!selected || (options::get_value("Wild") == 17
                    && capture_available && !selected->capture)) {
                out("info string Invalid move: " + token);
                return false;
            }
            history.push_back(board.stack->repetition_key());
            board.forward(selected);
            if (board.stack->fifty_count == 0) history.clear();
            if (board.ply > MAX_PLY - 2) board.init(board.to_string().c_str());
        }
        return true;
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
        myfile.open("maxima_uci.log", std::ios::app);
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
        myfile.open("maxima_uci.log", std::ios::app);
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
                result = handle_learn();
            } else if (token == "book") {
                result = handle_book(parser);
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

#ifdef DO_LOG
        std::remove("maxima_uci.log");
#endif     

        fen = start_fen;
        position_history.clear();
        engine::new_game(fen);
        return true;
    }

    bool handle_stop() {
        engine::stop();
        return true;
    }

    bool handle_go(input_parser_t &parser) {
        engine::stop();
        std::string token;
        game_t candidate;
        while (parser >> token) {
            if (token == "infinite") {
                candidate.max_depth = MAX_PLY;
            } else if (token == "ponder") {
                candidate.max_depth = MAX_PLY;
                candidate.ponder = true;
            } else if (token == "wtime" || token == "btime" || token == "winc"
                    || token == "binc" || token == "movestogo" || token == "depth"
                    || token == "nodes" || token == "movetime") {
                std::string value;
                U64 number = 0;
                if (!(parser >> value) || !parse_integer(value, number)
                        || (token != "nodes" && number > std::numeric_limits<int>::max())
                        || ((token == "nodes" || token == "depth" || token == "movestogo") && !number)
                        || (token == "depth" && number > MAX_PLY)) {
                    out("info string Invalid go value: " + token);
                    return true;
                }
                if (token == "wtime") candidate.white_time = static_cast<int>(number);
                if (token == "btime") candidate.black_time = static_cast<int>(number);
                if (token == "wtime" || token == "btime") candidate.clock_limit = true;
                if (token == "winc") candidate.white_increment = static_cast<int>(number);
                if (token == "binc") candidate.black_increment = static_cast<int>(number);
                if (token == "movestogo") candidate.moves_left = static_cast<int>(number);
                if (token == "depth") candidate.max_depth = static_cast<int>(number);
                if (token == "nodes") candidate.max_nodes = number;
                if (token == "movetime") candidate.max_time_per_move = number;
                if (token == "movetime") candidate.movetime_limit = true;
            } else if (token == "searchmoves") {
                const size_t count = candidate.search_moves.size();
                while (true) {
                    const auto position = parser.tellg();
                    std::string text;
                    if (!(parser >> text)) break;
                    if (text == "depth" || text == "nodes" || text == "movetime"
                            || text == "wtime" || text == "btime" || text == "winc"
                            || text == "binc" || text == "movestogo"
                            || text == "ponder" || text == "infinite" || text == "searchmoves") {
                        parser.seekg(position);
                        break;
                    }
                    board_t board;
                    board.init(fen.c_str());
                    std::vector<U64> history;
                    input_parser_t move_input(text);
                    if (!apply_moves(board, move_input, history)) return true;
                    candidate.search_moves.push_back(text);
                }
                if (candidate.search_moves.size() == count) {
                    out("info string Invalid searchmoves: expected legal root moves");
                    return true;
                }
            }
        }
        engine::settings()->copy(&candidate);
        engine::set_ponder(engine::settings()->ponder);
        engine::set_position(fen, position_history);
        engine::go();
        return true;
    }

    bool handle_position(input_parser_t & parser) {
        engine::stop();
        std::string token;
        std::string candidate;
        if (!(parser >> token)) {
            out("info string Invalid position command");
            return true;
        }
        if (token == "startpos") {
            candidate = start_fen;
            if ((parser >> token) && token != "moves") {
                out("info string Invalid position command");
                return true;
            }
        } else if (token == "fen") {
            while (parser >> token && token != "moves") candidate += token + ' ';
        } else {
            out("info string Invalid position command");
            return true;
        }
        if (!valid_fen(candidate)) {
            out("info string Invalid FEN");
            return true;
        }
        board_t board;
        board.init(candidate.c_str());
        if (!board.legal()) {
            out("info string Invalid FEN");
            return true;
        }
        std::vector<U64> history;
        if (token == "moves" && !apply_moves(board, parser, history)) return true;
        fen = board.to_string();
        position_history = std::move(history);
        return true;
    }

    bool handle_forward(input_parser_t & parser) {
        board_t brd;
        brd.init(fen.c_str());
        engine::stop();
        auto history = position_history;
        if (!apply_moves(brd, parser, history)) return true;
        fen = brd.to_string();
        position_history = std::move(history);
        return true;
    }

    bool handle_setoption(input_parser_t & parser) {
        engine::stop();
        bool result = true;
        bool handled = false;
        std::string token, name, value;
        name = "unknown";
        if ((parser >> token) && token == "name") {
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
                        if (!handled) {
                            send_unknown_option(name);
                            return true;
                        }
                        int candidate = opt->value;
                        if (opt->type == options::BOOL) {
                            if (value != "true" && value != "false" && value != "1" && value != "0") {
                                out("info string Invalid option value: " + name);
                                return true;
                            }
                            candidate = value == "true" || value == "1";
                        } else if (opt->type == options::INT) {
                            int minimum = 0, maximum = 0;
                            std::istringstream specification(opt->uci_option);
                            while (specification >> token) {
                                if (token == "min") specification >> minimum;
                                if (token == "max") specification >> maximum;
                            }
                            if (!parse_integer(value, candidate) || candidate < minimum || candidate > maximum) {
                                out("info string Invalid option value: " + name);
                                return true;
                            }
                        } else if (name == "Wild") {
                            if (value == "losers" || value == "17") {
                                candidate = 17;
                            } else if (value == "standard") {
                                candidate = 0;
                            } else {
                                out("info string Invalid option value: " + name);
                                return true;
                            }
                        }
                        opt->value = candidate;

                        //handle option
                        if (name == "ExperienceFile") {
                            out(experience::set_path(value) ? "info string Experience loaded"
                                : "info string Experience load failed; previous snapshot retained");
                        } else if (name == "ExperienceDepth") {
                            opt->value = MAX(1, MIN(128, opt->value));
                        } else if (name == "BookFile") {
                            book::set_path(value == "<empty>" ? "" : value);
                        } else if (name == "SyzygyPath") {
                            const bool initialized = tablebase::set_path(value);
                            trans_table::clear();
                            out(initialized ? "info string Syzygy supports up to "
                                + itoa(tablebase::max_pieces()) + " pieces"
                                : "info string Syzygy initialization failed");
                        } else if (name == "SyzygyProbeLimit") {
                            opt->value = MAX(0, MIN(5, opt->value));
                        } else if (name == "SyzygyProbeDepth") {
                            opt->value = MAX(0, MIN(64, opt->value));
                        } else if (name == "Hash") {
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
     * Learn can be used to determine if a new evaluation or search
     * feature gives better performance, and what is the ideal score
     *
     */
    bool handle_learn() {
        engine::stop();
        engine::settings()->clear();
        engine::set_ponder(false);
        engine::set_position(fen, position_history);
        engine::learn();
        return true;
    }


    /*
     * Eval prints the evaluation result on the current position
     */
    bool handle_eval(input_parser_t & parser) {
        engine::stop();
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
        if (!valid_fen(pfen)) {
            out("info string Invalid FEN");
            return true;
        }
        engine::new_game(pfen);
        engine::analyse();
        return true;
    }

    /*
     * Book handles commands for book making / learning
     */
    bool handle_book(input_parser_t & parser) {
        engine::stop();
        engine::settings()->clear();
        engine::set_ponder(false);
        engine::new_game(fen);
        std::string token;
        while (parser >> token) {
            if (token == "calc") {
                engine::book_calc();
            }
        }
        return true;
    }
    
    void send_id() {
        out(std::string("id name Maxima ") + std::string(MAXIMA_REVISION));
        out("id author Hermen Reitsma and Erik van het Hof");
    }

    void send_options() {
        for (int i = 1; i <= options::length; i++) {
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

    void send_pv(int cp_score, int depth, int sel_depth, U64 nodes, int time, const char * pv, int flag, U64 tablebase_hits) {
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
        msg += itoa(nps);
        if (tablebase_hits) msg += " tbhits " + itoa(tablebase_hits);
        msg += " pv ";
        msg += pv;
        out(msg);
    }

    void send_bestmove(move_t move, move_t ponder_move) {
        std::string msg = "bestmove ";
        if (move.piece > 0) {
            msg += move.to_string();
            if (ponder_move.piece > 0) {
                msg += " ponder " + ponder_move.to_string();
            }
        } else { //a null move
            msg += "0000";
        }
        out(msg);
    }
};
