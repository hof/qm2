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
 * File: threadsManager.h
 * Class to manage pthreads
 * 
 * Created on 26 april 2011, 19:31
 */

#ifndef THREADSMANAGER_H
#define	THREADSMANAGER_H

#include <pthread.h>
#include <assert.h>

#define MAX_THREADS 16

class TThreadsManager {

public:
    bool _stopAll;

    TThreadsManager() {
        this->_threadsCount = 0;
        this->_stopAll = false;
        pthread_mutex_init(&this->_statemutex, NULL);
        pthread_cond_init(&this->_cond, NULL);
    }

    ~TThreadsManager() {
        stopAllThreads();
        pthread_mutex_destroy(&this->_statemutex);
        pthread_cond_destroy(&this->_cond);
    }

    int createThread(void* function_ptr(void *ptr), void* params) {
        if (this->_threadsCount < MAX_THREADS) {
            pthread_t thread = getThread(_threadsCount);
            int returnvalue = pthread_create( &thread, NULL, function_ptr, params);
            if (returnvalue == 0) {
                this->_threads[this->_threadsCount++] = thread;
            }
        }
        return this->_threadsCount;
    }

    void waitForThread(int index)  {
        pthread_t thread = this->getThread(index);
        if (thread && index < this->_threadsCount) {
            pthread_join(thread, NULL);
        }
    }

    void stopAllThreads() {
        this->_stopAll = true;
        for (int i = 0; i < this->_threadsCount; i++) {
            this->stopThread(i);
        }
        this->_threadsCount = 0;
        this->_stopAll = false;
    }

    void stopThread(int index) {
        assert(index >= 0 && index < _threadsCount);
        this->waitForThread(index);
    }

private:
    int _threadsCount;
    pthread_t _threads[MAX_THREADS];

    pthread_cond_t _cond; // = PTHREAD_COND_INITIALIZER;
    pthread_mutex_t _statemutex; // = PTHREAD_MUTEX_INITIALIZER;

    inline pthread_t getThread(int index) {
        return (index >= 0 && index < MAX_THREADS)? _threads[index] : NULL;
    }

    void Lock() {
        pthread_mutex_lock(&this->_statemutex);
    }

    void Release() {
        pthread_mutex_unlock(&this->_statemutex);
    }
};


#endif	/* THREADSMANAGER_H */

