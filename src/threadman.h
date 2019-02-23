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
 * File: threadsManager.h
 * Class to manage pthreads
 * 
 * Created on 26 april 2011, 19:31
 */

#ifndef THREADMAN_H
#define	THREADMAN_H

#include <thread>

#define MAX_THREADS 256

class threads_t {
public:

    threads_t() {
        _count = 0;
        _stop_all = false;
    }

    ~threads_t() {
        stop_all();
    }

    int create(void* function_ptr(void *ptr), void* params) {
        if (_count < MAX_THREADS) {
            // _threads[_count++] =
            std::thread((*function_ptr));
        }
        return _count;
    }

    void wait_for(int ix) {
        pthread_t thread = get(ix);
        if (thread && ix < _count) {
            pthread_join(thread, NULL);
        }
    }

    void stop_all() {
        this->_stop_all = true;
        for (int i = 0; i < _count; i++) {
            this->stop(i);
        }
        _count = 0;
        _stop_all = false;
    }

    void stop(int ix) {
        wait_for(ix);
    }

private:
    int _count;
    bool _stop_all;
    std::thread _threads[MAX_THREADS];

    inline std::thread get(int index) {
        return (index >= 0 && index < MAX_THREADS) ? _threads[index] : 0;
    }
};


#endif	/* THREADMAN_H */

