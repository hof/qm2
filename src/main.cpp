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
 * File: main.cpp
 * Starts a Maxima UCI engine
 * 
 * Created on 8 april 2011, 15:17
 */

#include <cstdlib>
#include <iostream>

#include "inputhandler.h"

using namespace std;

int main(int argc, char** argv) {
    TInputHandler * inputHandler = new TInputHandler();
    string cmd;
    do {
        if (!getline(cin, cmd))
            cmd = "quit";
    } while (inputHandler->handle(cmd));
    delete inputHandler;
    return 0;
}


/*
#include "uci_console.h"

using namespace std;

int main(int argc, char** argv) {
    string cmd;
    magic::init();
    do {
        if (!getline(cin, cmd))
            cmd = "quit";
    } while (uci::in(cmd));
    return 0;
}
 */
