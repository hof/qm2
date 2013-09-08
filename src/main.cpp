/* 
 * File:   main.cpp
 * Author: Hajewiet
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



