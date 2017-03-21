// $Id: $

#ifndef NIFUTIL_MOSCLIENT_H
#define NIFUTIL_MOSCLIENT_H

#include <mosquitto.h>
#include <pthread.h>

#include <list>
#include <map>
#include <queue>
#include <string>

#include "Mutex.h"

#if WITH_ERL
#include "ErlUtil.h"
#endif

#include "LevelManager.h"

//=======================================================================
// MosClient is a class that can be used in a number of different ways:
//
//  1) As a stand-alone client in a C++ program
//  2) As a stand-alone client in an erlang vm
//  3) As an embedded client in another erlang process
//
// If intended to be used with erlang, WITH_ERL=1 must be defined at
// compile time.
//
// As either of the stand-alone clients, I provide the option to run
// with a backing leveldb store.  This is the default behavior, as
// long as WITH_LEVELDB=1.  (If WITH_LEVELDB=0, the leveldb lib will
// not be built)
//
// When instantiated from erlang, you can specify the embedded option.
// If embedded=false, then a backing leveldb store will be used.  If
// embedded=true, then no backing leveldb store will be used (assumed
// that the messages will be stored elsewhere by the process in which
// we are embedded).
//=======================================================================

#define THREAD_START(fn) void* (fn)(void *arg)

namespace nifutil {

    class MosClient {
    public:

        //------------------------------------------------------------
        // A class for managing mutex protection of static resources
        //------------------------------------------------------------
        
        class ScopedLock {
        public:

            ScopedLock() {
                mutex_ = 0;
            };
            
            ScopedLock(Mutex& mutex) {
                mutex_ = &mutex;
                mutex_->Lock();
            };
            
            ~ScopedLock() {
                if(mutex_)
                    mutex_->Unlock();
            };
            
        private:

            Mutex* mutex_;
        };

        //------------------------------------------------------------
        // A class for managing messages returned to erlang
        //------------------------------------------------------------
        
        struct Message {
            bool isEmpty_;
            std::string topic_;
            std::vector<unsigned char> payload_;
        };

        //------------------------------------------------------------
        // A class for managing topic descriptors
        //------------------------------------------------------------

        enum FormatType {
            FORMAT_UNKNOWN = 0,
            FORMAT_CSV     = 1,
            FORMAT_JSON    = 2
        };
        
#if WITH_ERL
        struct Topic {
            std::vector<STRING_CONV_FN_PTR> convFnVec_;
            std::string schema_;
            FormatType format_;
        };
#endif        
        /**
         * Destructor.
         */
        virtual ~MosClient();

        //------------------------------------------------------------
        // Used from the NIF interface
        //------------------------------------------------------------
        
        static void startCommsLoop();
        static void blockForever();
            
        static std::string getStatusSummary();
        static void toggleLogging(bool log);

#if WITH_ERL
        static void subscribe(std::string topic, std::string schema, std::vector<STRING_CONV_FN_PTR> convFnVec, std::string format);
        static void registerPid(ErlNifEnv* env, ErlNifPid pid);
        static void setOption(ErlNifEnv* env, std::string name, ERL_NIF_TERM val);
#endif

        static void setOption(std::string name, int val);
        static void setOption(std::string name, std::string val);
        static void setOption(std::string name, bool val);
        
    private:
        
        /**
         * Private constructors
         */
        MosClient();
        MosClient(MosClient& mos);
        MosClient(const MosClient& mos);

        static THREAD_START(runMosCommsLoop);

        //------------------------------------------------------------
        // Callbacks used by mosquitto client library
        //------------------------------------------------------------
        
        static void message_callback(struct mosquitto *mosq, void *userdata, const struct mosquitto_message *message);
        static void connect_callback(struct mosquitto *mosq, void *userdata, int result);
        static void disconnect_callback(struct mosquitto *mosq, void *userdata, int result);
        static void subscribe_callback(struct mosquitto *mosq, void *userdata, int mid, int qos_count, const int *granted_qos);
        static void log_callback(struct mosquitto *mosq, void *userdata, int level, const char *str);


        void addSubscribeList(struct mosquitto *mosq);
        std::string getStatusSummaryPrivate();

        void process(const struct mosquitto_message *message);
        void processCommand(const struct mosquitto_message *message);
        void processMessage(const struct mosquitto_message *message);

        void initAndRun();
        void certConfig();

        //------------------------------------------------------------
        // The stand-alone interface to this class
        //------------------------------------------------------------

        LevelManager db_;
        void storeMessage(const struct mosquitto_message *message);
        std::map<std::string, std::string> decodeJson(const struct mosquitto_message* message);
        int toInt(std::string str);
        void dumpToBroker(std::map<std::string, std::string>& entryMap);

        std::string getEntry(std::map<std::string, std::string>& entryMap, std::string entry);
        std::string getEntry(std::map<std::string, std::string>& entryMap, std::string defVal, std::string entry);

#if WITH_ERL
        //------------------------------------------------------------
        // The private NIF interface to this class
        //------------------------------------------------------------
        
        void notify(const struct mosquitto_message *message);
        void subscribePrivate(std::string topic, std::string schema, std::vector<STRING_CONV_FN_PTR> convFnVec, std::string format);

        ERL_NIF_TERM formatForTs(const struct mosquitto_message* message);
        ERL_NIF_TERM formatData(const struct mosquitto_message* message);
        ERL_NIF_TERM formatDataCsv(const struct mosquitto_message* message, Topic& topicDesc);
        ERL_NIF_TERM formatDataJson(const struct mosquitto_message* message, Topic& topicDesc);

        ErlNifEnv* msgEnv_;
        std::list<std::pair<ErlNifEnv*, ErlNifPid> > notificationList_;
        std::map<std::string, Topic> topicMap_;
#endif


        static std::string formatMosError(int errVal);
        void logMessage(const struct mosquitto_message *message);
        static std::string formatMessage(const struct mosquitto_message *message);

        // Private members of this class
        
        pthread_t mosCommsId_;
        struct mosquitto* mosq_;
        static MosClient instance_;
        bool connected_;
        bool initialized_;
        bool log_;
        int port_;
        bool embedded_; // Are we being used standalone, or embedded?
        std::string name_;
        std::string host_;
        std::string caPath_;
        std::string caFile_;
        std::string certFile_;
        std::string keyFile_;

        std::string commandTopic_;
        bool useCerts_;
        int keepAlive_;
        
        std::list<std::string> topicList_;


        Mutex mutex_;

    }; // End class MosClient

} // End namespace nifutil



#endif // End #ifndef NIFUTIL_MOSCLIENT_H
