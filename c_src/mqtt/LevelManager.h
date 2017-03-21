// $Id: $

#ifndef NIFUTIL_LEVELMANAGER_H
#define NIFUTIL_LEVELMANAGER_H

#include <string>

#if WITH_LEVELDB
#include "leveldb/db.h"
#include "leveldb/slice.h"
#endif

/**
 * @file LevelManager.h
 * 
 * Tagged: Mon Jan 30 14:39:28 PST 2017
 * 
 * @version: $Revision: $, $Date: $
 * 
 * @author /bin/bash: username: command not found
 */
namespace nifutil {

    class LevelManager {
    public:

        /**
         * Constructor.
         */
        LevelManager();

        /**
         * Destructor.
         */
        virtual ~LevelManager();

        void open(std::string dbName);
        void close();
        void write(std::string key, std::string value);
        void write(std::string key, const char* cptr, size_t n);
        void put(std::string key, std::string value);
        void put(std::string key, const char* cptr, size_t n);
        std::string read(std::string key);
        std::string get(std::string key);
        void dumpDbToStdout();

        void iterStep();
        void iterGet(std::string& key, std::string& val);
        bool iterValid();
        void iterStart();
        void iterClose();

    private:

        
#if WITH_LEVELDB
        leveldb::DB* dbPtr_;
        leveldb::Iterator* iter_;
#endif
        
    }; // End class LevelManager

} // End namespace nifutil



#endif // End #ifndef NIFUTIL_LEVELMANAGER_H
