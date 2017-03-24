// -------------------------------------------------------------------
//
// nifutil: Erlang Wrapper for LevelDB (http://code.google.com/p/leveldb/)
//
// Copyright (c) 2011-2013 Basho Technologies, Inc. All Rights Reserved.
//
// This file is provided to you under the Apache License,
// Version 2.0 (the "License"); you may not use this file
// except in compliance with the License.  You may obtain
// a copy of the License at
//
//   http://www.apache.org/licenses/LICENSE-2.0
//
// Unless required by applicable law or agreed to in writing,
// software distributed under the License is distributed on an
// "AS IS" BASIS, WITHOUT WARRANTIES OR CONDITIONS OF ANY
// KIND, either express or implied.  See the License for the
// specific language governing permissions and limitations
// under the License.
//
// -------------------------------------------------------------------
#ifndef INCL_MUTEX_H
#define INCL_MUTEX_H

#include <pthread.h>

namespace nifutil {


/**
 * Autoinitializing mutex object
 */
class Mutex
{
protected:
    pthread_mutex_t mutex_;

public:
    Mutex() {pthread_mutex_init(&mutex_, NULL);};

    ~Mutex() {pthread_mutex_destroy(&mutex_);};

    pthread_mutex_t & get() {return(mutex_);};

    void lock() {pthread_mutex_lock(&mutex_);};

    void unlock() {pthread_mutex_unlock(&mutex_);};

private:
    Mutex(const Mutex & rhs);             // no copy
    Mutex & operator=(const Mutex & rhs); // no assignment

};  // class Mutex


/**
 * Automatic lock and unlock of mutex
 */
class MutexLock
{
protected:

    Mutex & mutexObject_;

public:

    explicit MutexLock(Mutex & MutexObject)
        : mutexObject_(MutexObject)
    {mutexObject_.lock();};

    ~MutexLock() {mutexObject_.unlock();};

private:

    MutexLock();                                  // no default constructor
    MutexLock(const MutexLock & rhs);             // no copy constructor
    MutexLock & operator=(const MutexLock & rhs); // no assignment constructor
};  // class MutexLock

} // namespace nifutil


#endif  // INCL_MUTEX_H
