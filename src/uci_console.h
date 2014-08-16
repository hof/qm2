

#ifndef UCI_CONSOLE_H
#define	UCI_CONSOLE_H

#include <sstream>
#include <cstdlib>
#include <iostream>
#include <istream>
#include <fstream>
#include <string.h>
#include <sys/time.h>
#include <sys/types.h>
#include <unistd.h>

#include "engine.h"

namespace uci {
    typedef std::istringstream input_parser_t;
    
    bool in(std::string cmd);
    void out(std::string str);
    
    void silent(bool on);
    
    bool handle_uci();
    bool handle_isready();
    bool handle_ucinewgame();
    bool handle_stop();
    bool handle_ponderhit();
    bool handle_go(input_parser_t & p);
    bool handle_position(input_parser_t & p);
    bool handle_forward(input_parser_t & p);
    bool handle_setoption(input_parser_t &parser);
    bool handle_eval(input_parser_t &parser);
    bool handle_learn(input_parser_t &parser);
    
    void send_id();
    void send_options();
    void send_ok();   
    void send_ready();
    void send_pv(int cp_score, int depth, int sel_depth, U64 nodes, int time, const char * pv, int flag); 
    void send_bestmove(move_t move, move_t ponder_move);
    void send_unknown_option(std::string option);
}

#endif	/* UCI_CONSOLE_H */

