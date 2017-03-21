#include "LevelManager.h"
#include "ExceptionUtils.h"

using namespace std;
using namespace nifutil;

#if WITH_LEVELDB
using namespace leveldb;

#define CHECK_DB {                                                      \
        if(!dbPtr_) {                                                   \
            ThrowRuntimeError("DB ptr is null");                        \
        }                                                               \
    }
#endif

/**.......................................................................
 * Constructor.
 */
LevelManager::LevelManager()
{
}

/**.......................................................................
 * Open the named database file
 */
void LevelManager::open(std::string dbName)
{
    COUT("Inside open");
#if WITH_LEVELDB
    COUT("Inside open with = 1");

    Options opts;
    opts.create_if_missing = true;
    opts.limited_developer_mem = true;
    
    dbPtr_ = 0;
    Status status = DB::Open(opts, dbName, &dbPtr_);

    COUT("Status = " << status.ToString() << " dbPtr = " << dbPtr_);

    if(!status.ok())
        ThrowRuntimeError("Error opening leveldb dir: " << status.ToString());
#endif
}

/**.......................................................................
 * Close the named database file
 */
void LevelManager::close()
{
    COUT("Inside close");
#if WITH_LEVELDB
    if(dbPtr_)
        delete dbPtr_;
    dbPtr_ = 0;
#endif
}

/**.......................................................................
 * Destructor.
 */
LevelManager::~LevelManager()
{
#if WITH_LEVELDB
    if(dbPtr_) {
//        delete dbPtr_;
//        dbPtr_ = 0;
    }
#endif
}

/**.......................................................................
 * Put a string into the DB
 */
void LevelManager::put(std::string key, std::string value)
{
    return write(key, value);
}

void LevelManager::write(std::string key, std::string value)
{
#if WITH_LEVELDB
    CHECK_DB;
    
    WriteOptions opts;

    Slice keySlice(key);
    Slice valSlice(value);
    
    Status status = dbPtr_->Put(opts, keySlice, valSlice);

    if(!status.ok())
        ThrowRuntimeError("Error putting to leveldb dir: " << status.ToString());
#endif
}

void LevelManager::put(std::string key, const char* cptr, size_t n)
{
    return write(key, cptr, n);
}

void LevelManager::write(std::string key, const char* cptr, size_t n)
{
#if WITH_LEVELDB
    CHECK_DB;
    
    WriteOptions opts;

    Slice keySlice(key);
    Slice valSlice(cptr, n);
    
    Status status = dbPtr_->Put(opts, keySlice, valSlice);

    if(!status.ok())
        ThrowRuntimeError("Error putting to leveldb dir: " << status.ToString());
#endif
}

/**.......................................................................
 * Put a string into the DB
 */
std::string LevelManager::get(std::string key)
{
    return read(key);
}

std::string LevelManager::read(std::string key)
{
    std::string retStr;

#if WITH_LEVELDB

    CHECK_DB;

    ReadOptions opts;
    Slice keySlice(key);

    Status status = dbPtr_->Get(opts, keySlice, &retStr);
    
    if(!status.ok())
        ThrowRuntimeError("Error getting from leveldb dir: " << status.ToString());
#endif

    return retStr;
}

void LevelManager::dumpDbToStdout()
{
 #if WITH_LEVELDB

    CHECK_DB;

    ReadOptions opts;

    Iterator* iter = dbPtr_->NewIterator(opts);
    if(iter) {
        iter->SeekToFirst();
        
        while(iter->Valid()) {
            leveldb::Slice key   = iter->key();
            leveldb::Slice value = iter->value();
            COUT("Found key = '" << key.ToString() << "' value = '" << value.ToString());
            iter->Next();
        }

        delete iter;
        
    } else {
        ThrowRuntimeError("Error iterating");
    }
    
#endif   
}

void LevelManager::iterStart()
{
#if WITH_LEVELDB

    CHECK_DB;

    ReadOptions opts;

    iter_ = dbPtr_->NewIterator(opts);
    if(iter_) {
        iter_->SeekToFirst();
    } else {
        ThrowRuntimeError("Error initializing iterator");
    }
#endif
}

bool LevelManager::iterValid()
{
#if WITH_LEVELDB

    CHECK_DB;

    if(iter_) {
        return iter_->Valid();
    } else {
        return false;
    }
#else
    return false;
#endif
}

void LevelManager::iterGet(std::string& key, std::string& val)
{
#if WITH_LEVELDB

    CHECK_DB;
    if(iterValid()) {
        key = iter_->key().ToString();
        val = iter_->value().ToString();
    }
#endif
}

void LevelManager::iterStep()
{
#if WITH_LEVELDB

    CHECK_DB;
    if(iter_)
        iter_->Next();

#endif
}

void LevelManager::iterClose()
{
#if WITH_LEVELDB

    CHECK_DB;
    if(iter_) {
        delete iter_;
        iter_ = 0;
    }

#endif
}
