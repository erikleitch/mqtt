#include "MosClient.h"

#include <errno.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <sys/select.h>

#include "ExceptionUtils.h"

using namespace std;

using namespace nifutil;

MosClient MosClient::instance_;

/**.......................................................................
 * Constructor.
 */
MosClient::MosClient()
{
    mosq_        = 0;
    connected_   = false;
    initialized_ = false;
    log_         = false;
    mosCommsId_  = 0;
    host_        = "localhost";
    port_        = 1883;
    useCerts_    = false;
    keepAlive_   = 60;
    embedded_    = false;
    name_        = "mosclient";

#if WITH_ERL
    msgEnv_      = enif_alloc_env();
#endif
}

MosClient::MosClient(MosClient& mos)
{
}

MosClient::MosClient(const MosClient& mos)
{
}

/**.......................................................................
 * Destructor.
 */
MosClient::~MosClient()
{
    //------------------------------------------------------------
    // Kill any spawned process
    //------------------------------------------------------------
    
    (void) pthread_kill(mosCommsId_, SIGKILL);

    //------------------------------------------------------------
    // Destroy the mosquitto session that was allocated in initAndRun
    //------------------------------------------------------------
    
    if(mosq_) {
        mosquitto_destroy(mosq_);
        mosq_ = 0;
    }

    //------------------------------------------------------------
    // Clean up the library if initialized
    //------------------------------------------------------------
    
    if(initialized_)
        mosquitto_lib_cleanup();

#if WITH_ERL
    //------------------------------------------------------------
    // Clear any environments that were allocated
    //------------------------------------------------------------
    
    for(std::list<std::pair<ErlNifEnv*, ErlNifPid> >::iterator iter=notificationList_.begin();
        iter != notificationList_.end(); iter++) {
        enif_free_env(iter->first);
    }

    if(msgEnv_)
        enif_free_env(msgEnv_);
#endif
}

/**.......................................................................
 * Private method to initialize and run the client. Called from
 * THREAD_START function
 */
void MosClient::initAndRun()
{
    bool clean_session = true;

    mosquitto_lib_init();
    mosq_ = mosquitto_new(NULL, clean_session, this);

    initialized_ = true;
    
    if(!mosq_)
        ThrowRuntimeError("Unable to allocate new mos session");

    mosquitto_log_callback_set(mosq_, log_callback);
    mosquitto_connect_callback_set(mosq_, connect_callback);
    mosquitto_disconnect_callback_set(mosq_, disconnect_callback);
    mosquitto_message_callback_set(mosq_, message_callback);
    mosquitto_subscribe_callback_set(mosq_, subscribe_callback);

    //------------------------------------------------------------
    // Initialize DB if not running as embedded, and we are using
    // leveldb
    //------------------------------------------------------------

#if WITH_LEVELDB
    if(!embedded_) {
        std::string dbName_ = "/tmp/" + name_;
        db_.open(dbName_);
        commandTopic_ = name_ + "/command";
    }
#endif
    
    //------------------------------------------------------------
    // Initialize timeout to 0, which will cause select to exit immediately
    //------------------------------------------------------------

    struct timeval timeout;
    struct timeval* timeOutPtr = &timeout;
    
    timeout.tv_sec  = 0;
    timeout.tv_usec = 0;

    do {

        select(0, NULL, NULL, NULL, timeOutPtr);
        
        try {

            COUT("MQTT Attempting to reconnect...");

            if(useCerts_)
                certConfig();

            int retVal=0;
            retVal = mosquitto_connect(mosq_, host_.c_str(), port_, keepAlive_);
            
            if(retVal != MOSQ_ERR_SUCCESS) {
                COUT("MQTT Unable to connect: return was: " << (retVal==MOSQ_ERR_INVAL ? "Invalid parameters" : "system error"));
                ThrowRuntimeError("Unable to connect");
            } else {
                COUT("MQTT Successfully connected");
            }
            
            mosquitto_loop_forever(mosq_, -1, 1);
            
        } catch(...) {
            COUT("MQTT Caught an error talking to broker -- attempting to reconnect in 1 second");
            timeout.tv_sec  = 1;
            timeout.tv_usec = 0;
        }
        
    } while(true);
}

//=======================================================================
// Mosquitto library comms loop callbacks
//=======================================================================

//-----------------------------------------------------------------------
// Callback when a message is received
//-----------------------------------------------------------------------

void MosClient::message_callback(struct mosquitto *mosq, void *userdata,
                                 const struct mosquitto_message *message)
{
    MosClient* client = (MosClient*)userdata;

    try {
        
        if(message->payloadlen) {
            client->process(message);
        } else {
            // Empty message -- when can this occur?
        }
        
    } catch(std::runtime_error& err) {
        COUTRED("MQTT Caught an error while parsing message: " << formatMessage(message) << std::endl << "\r  " << err.what());
    } catch(...) {
        COUTRED("MQTT Caught an unknown error while parsing message: " << formatMessage(message));
    }
}

//-----------------------------------------------------------------------
// Callback when connected to broker
//-----------------------------------------------------------------------

void MosClient::connect_callback(struct mosquitto *mosq, void *userdata,
                                 int result)
{
    MosClient* client = (MosClient*)userdata;

    try {

        if(!result) {
            client->addSubscribeList(mosq);
            client->connected_ = true;
        } else {
            COUT("MQTT Connect failed");
        }

    } catch(std::runtime_error& err) {
        COUT("MQTT Caught an error in connect callback: " << err.what());
    } catch(...) {
        COUT("MQTT Caught an unknown error in connect callback");
    }
}

//-----------------------------------------------------------------------
// Callback when disconnected from broker
//-----------------------------------------------------------------------

void MosClient::disconnect_callback(struct mosquitto *mosq, void *userdata,
                                 int result)
{
    MosClient* client = (MosClient*)userdata;

    try {
        client->connected_ = false;
    } catch(std::runtime_error& err) {
        COUT("MQTT Caught an error in disconnect callback: " << err.what());
    } catch(...) {
        COUT("MQTT Caught an unknown error in disconnect callback");
    }
}

//-----------------------------------------------------------------------
// Callback on subscription
//-----------------------------------------------------------------------

void MosClient::subscribe_callback(struct mosquitto *mosq, void *userdata,
                                   int mid, int qos_count, const int *granted_qos)
{
    try {

        std::ostringstream os;
        os << "MQTT Subscribed (mid: " << mid << ") " << granted_qos[0];
        
        for(int i=1; i < qos_count; i++)
            os << ", " << granted_qos[i];
        
        COUT(os.str());

    } catch(std::runtime_error& err) {
        COUT("MQTT Caught an error in subscribe callback: " << err.what());
    } catch(...) {
        COUT("MQTT Caught an unknown error in subscribe callback");
    }
}

//-----------------------------------------------------------------------
// Callback on logging
//-----------------------------------------------------------------------

void MosClient::log_callback(struct mosquitto *mosq, void *userdata,
                             int level, const char *str)
{
//    COUT("MQTT Logged message: " << str);
}

//=======================================================================
// Public (NIF) interface to MosClient
//=======================================================================

/**-----------------------------------------------------------------------
 * Top-level call to create background thread talking to the MQTT
 * broker
 */
void MosClient::startCommsLoop()
{
    ScopedLock(instance_.mutex_);

    if(instance_.mosCommsId_ != 0)
        ThrowRuntimeError("Comms loop is already running");

    if(pthread_create(&instance_.mosCommsId_, NULL, &runMosCommsLoop, &instance_) != 0)
        ThrowRuntimeError("Unable to create comms thread");
}

#if WITH_ERL
/**-----------------------------------------------------------------------
 * Register a calling process' erlang pid to be notified on receipt of
 * messages
 */
void MosClient::registerPid(ErlNifEnv* env, ErlNifPid pid)
{
    ScopedLock(instance_.mutex_);
    instance_.notificationList_.insert(instance_.notificationList_.end(),
                                       std::pair<ErlNifEnv*, ErlNifPid>(env, pid));
}

/**.......................................................................
 * Public method to subscribe to a new topic
 */
void MosClient::subscribe(std::string topic, std::string schema, std::vector<STRING_CONV_FN_PTR> convFnVec, std::string format)
{
    ScopedLock(instance_.mutex_);
    instance_.subscribePrivate(topic, schema, convFnVec, format);
}
#endif

/**.......................................................................
 * Return a status summary 
 */
std::string MosClient::getStatusSummary()
{
    ScopedLock(mutex_);
    return instance_.getStatusSummaryPrivate();
}

/**.......................................................................
 * Toggle logging
 */
void MosClient::toggleLogging(bool log)
{
    ScopedLock(instance_.mutex_);
    instance_.log_ = log;
}

#if WITH_ERL
/**.......................................................................
 * Set options
 */
void MosClient::setOption(ErlNifEnv* env, std::string name, ERL_NIF_TERM val)
{
    if(name == "host"     ||
       name == "capath"   ||
       name == "cafile"   ||
       name == "certfile" ||
       name == "keyfile"  ||
       name == "name") {
        
        setOption(name, ErlUtil::getString(env, val));

    } else if(name == "port" ||
              name == "keepalive") {

        setOption(name, ErlUtil::getValAsInt32(env, val));

    } else if(name == "embedded") {
        setOption(name, ErlUtil::getBool(env, val));
    } else {
        ThrowRuntimeError("Unrecognized option: " << name);
    }
    
}
#endif

/**.......................................................................
 * Set a boolean option
 */
void MosClient::setOption(std::string name, bool val)
{
    ScopedLock(instance_.mutex_);

    if(instance_.mosCommsId_ != 0)
        ThrowRuntimeError("Connection options can't be changed once the comms loop has been started");

    if(name == "embedded") {
        instance_.embedded_ = val;
    } else {
        ThrowRuntimeError("Unrecognized option: " << name);
    }
}

/**.......................................................................
 * Set an integer option
 */
void MosClient::setOption(std::string name, int val)
{
    ScopedLock(instance_.mutex_);

    if(instance_.mosCommsId_ != 0)
        ThrowRuntimeError("Connection options can't be changed once the comms loop has been started");

    if(name == "port") {
        instance_.port_ = val;
    } else if(name == "keepalive") {
        instance_.keepAlive_ =  val;
    } else {
        ThrowRuntimeError("Unrecognized option: " << name);
    }
}

/**.......................................................................
 * Set a string option
 */
void MosClient::setOption(std::string name, std::string val)
{
    ScopedLock(instance_.mutex_);

    if(instance_.mosCommsId_ != 0)
        ThrowRuntimeError("Connection options can't be changed once the comms loop has been started");

    if(name == "name") {
        instance_.name_     = val;
    } else if(name == "host") {
        instance_.host_     = val;
    } else if(name == "capath") {
        instance_.useCerts_ = true;
        instance_.caPath_   = val;
    } else if(name == "cafile") {
        instance_.useCerts_ = true;
        instance_.caFile_   = val;
    } else if(name == "certfile") {
        instance_.useCerts_ = true;
        instance_.certFile_ = val;
    } else if(name == "keyfile") {
        instance_.useCerts_ = true;
        instance_.keyFile_  = val;
    } else {
        ThrowRuntimeError("Unrecognized option: " << name);
    }
}

//=======================================================================
// Utility functions
//=======================================================================

/**.......................................................................
 * Format a MOSQ error code
 */
std::string MosClient::formatMosError(int errVal)
{
    std::string retStr;
    
    switch (errVal) {
    case MOSQ_ERR_INVAL:
        retStr = "Invalid parameters";
        break;
    case MOSQ_ERR_NOMEM:
        retStr = "Out of memory";
        break;
    case MOSQ_ERR_NO_CONN:
        retStr = "Not connected to broker";
        break;
    default:
        retStr = "success";
        break;
    }

    return retStr;
}

#if WITH_ERL
/**.......................................................................
 * Format a message for return to TS.  Uses the schema supplied when
 * the topic was subscribed to convert the data to a ready-to-ingest
 * message for TS
 */
ERL_NIF_TERM MosClient::formatForTs(const struct mosquitto_message* message)
{
    //------------------------------------------------------------
    // First the msg code
    //------------------------------------------------------------
    
    std::vector<ERL_NIF_TERM> termVec;
    termVec.push_back(enif_make_atom(msgEnv_, "tsputreq"));
    
    //------------------------------------------------------------
    // First the table name
    //------------------------------------------------------------
    
    std::string topic = message->topic;
    termVec.push_back(ErlUtil::stringToBinaryTerm(msgEnv_, topic));

    //------------------------------------------------------------
    // Empty list
    //------------------------------------------------------------
    
    termVec.push_back(enif_make_list(msgEnv_, 0));

    //------------------------------------------------------------
    // Next the table data
    //------------------------------------------------------------

    ERL_NIF_TERM dataTuple = formatData(message);
    termVec.push_back(enif_make_list(msgEnv_, 1, dataTuple));

    //------------------------------------------------------------
    // Finally, return a tuple from the array we just constructed
    //------------------------------------------------------------
    
    return enif_make_tuple_from_array(msgEnv_, &termVec[0], termVec.size());
}

/**.......................................................................
 * Format data encoded as a string
 */
ERL_NIF_TERM MosClient::formatData(const struct mosquitto_message* message)
{
    Topic& topicDesc = topicMap_[message->topic];

    switch(topicDesc.format_) {
    case FORMAT_JSON:
        return formatDataJson(message, topicDesc);
        break;
    default:
        return formatDataCsv(message, topicDesc);
        break;
    }
}        

/**.......................................................................
 * Format TS data encoded as CSV string
 */
ERL_NIF_TERM MosClient::formatDataCsv(const struct mosquitto_message* message, Topic& topicDesc)
{
    std::vector<STRING_CONV_FN_PTR>& convFnVec = topicDesc.convFnVec_;
    
    unsigned nTerm = convFnVec.size();
    std::vector<ERL_NIF_TERM> dataTerms(nTerm);


    const char* str = (const char*)message->payload;
    
    std::ostringstream os;
    
    unsigned iTerm=0;
    for(int i=0; i <= message->payloadlen; i++) {
        
        if(i == message->payloadlen || str[i] == ',') {
            if(iTerm < nTerm) {
                dataTerms[iTerm] = convFnVec[iTerm](msgEnv_, os.str());
                os.str("");
                iTerm++;
            } else {
                ThrowRuntimeError("Invalid data received for schema " << message->topic << " (too many terms)"
                                  << std::endl << "\r" << "  Expected " << topicDesc.schema_);
            }
        } else {
            os << str[i];
        }
        
    }
    
    // Did we convert enough terms?
    
    if(iTerm != nTerm)
        ThrowRuntimeError("Invalid data received for schema " << message->topic << " (not enough terms)"
                          << std::endl << "\r" << "  Expected " << topicDesc.schema_);
    
    return enif_make_tuple_from_array(msgEnv_, &dataTerms[0], nTerm);
}

/**.......................................................................
 * Format TS data encoded as JSON string
 */
ERL_NIF_TERM MosClient::formatDataJson(const struct mosquitto_message* message, Topic& topicDesc)
{
    std::vector<STRING_CONV_FN_PTR>& convFnVec = topicDesc.convFnVec_;
    
    unsigned nTerm = convFnVec.size();
    std::vector<ERL_NIF_TERM> dataTerms(nTerm);


    const char* str = (const char*)message->payload;
    
    std::ostringstream os;
    
    unsigned iTerm=0;

    bool readTokens = false;
    for(int i=0; i <= message->payloadlen; i++) {

        // Json tokens are colon-separated.  Do nothing until we
        // encounter one
        
        if(str[i] == ':') {
            readTokens = true;
        } else if(readTokens) {
            
            // Skip quotation marks
            
            if(str[i] == '"')
                continue;
            
            if(i == message->payloadlen || str[i] == ',' || str[i] == '}') {

                readTokens = false;
                
                if(iTerm < nTerm) {
                    dataTerms[iTerm] = convFnVec[iTerm](msgEnv_, os.str());
                    os.str("");
                    iTerm++;
                } else {
                    ThrowRuntimeError("Invalid data received for schema " << message->topic << " (too many terms)"
                                      << std::endl << "\r" << "  Expected " << topicDesc.schema_);
                }
            } else {
                os << str[i];
            }
        }
        
    }
    
    // Did we convert enough terms?
    
    if(iTerm != nTerm)
        ThrowRuntimeError("Invalid data received for schema " << message->topic << " (not enough terms)"
                          << std::endl << "\r" << "  Expected " << topicDesc.schema_);
    
    return enif_make_tuple_from_array(msgEnv_, &dataTerms[0], nTerm);
}
#endif

/**.......................................................................
 * Log received messages to stdout
 */
void MosClient::logMessage(const struct mosquitto_message *message)
{
    std::ostringstream msg;
    for(int i=0; i < message->payloadlen; i++)
        msg << ((unsigned char*)message->payload)[i];

    COUT("MQTT Got a message on topic: " << message->topic << ": " << msg.str());
}

/**.......................................................................
 * Configure to use certificates
 */
void MosClient::certConfig()
{
    std::string caFile   = caPath_ + "/" + caFile_;
    std::string certFile = caPath_ + "/" + certFile_;
    std::string keyFile  = caPath_ + "/" + keyFile_;

    mosquitto_tls_set(mosq_, caFile.c_str(), caPath_.c_str(), certFile.c_str(), keyFile.c_str(), NULL);
    mosquitto_tls_insecure_set(mosq_, true);
}

//=======================================================================
// Private methods of MosClient
//=======================================================================

/**.......................................................................
 * Thread start-up function to pass to pthread_create
 */
THREAD_START(MosClient::runMosCommsLoop)
{
    MosClient* client = (MosClient*)arg;

    try {
        client->initAndRun();
    } catch(...) {

        COUT("MQTT Caught an error starting up comms loop... exiting");
        client->connected_ = false;

        mosquitto_destroy(client->mosq_);
        client->mosq_ = 0;
        
        mosquitto_lib_cleanup();

        client->initialized_ = false;
    }
    
    return 0;
}

#if WITH_ERL
/**.......................................................................
 * Add the topic to the list of topics we will subscribe to on connect
 * to the broker, and subscribe, if already connected
 */
void MosClient::subscribePrivate(std::string topic, std::string schema, std::vector<STRING_CONV_FN_PTR> convFnVec, std::string format)
{
    // Always add it to our subscribe queue (in case of server
    // disconnect, we need to re-subscribe when it comes back
    // online)
    //
    // Additionally, if we are connected, just subscribe right away
    
    COUT("MQTT Adding topic " << topic << " to subscription list");

    topicList_.insert(topicList_.end(), topic);
    
    int qos    = 0;
    int retVal = 0;

    if(connected_)
        retVal = mosquitto_subscribe(mosq_, NULL, topic.c_str(), qos);
    
    // And add a map entry with the schema conversion fns
    
    Topic topicDesc;
    topicDesc.convFnVec_ = convFnVec;
    topicDesc.schema_    = schema;
    topicDesc.format_    = (format == "csv" ? FORMAT_CSV : FORMAT_JSON);
    
    topicMap_[topic]     = topicDesc;
    
    // If there was an error on subscribe, throw it now
    
    if(retVal != MOSQ_ERR_SUCCESS)
        ThrowRuntimeError(formatMosError(retVal));
}
#endif

/**.......................................................................
 * Process a message received from the broker.
 */
void MosClient::process(const struct mosquitto_message *message)
{
    ScopedLock(mutex_);

    // Log to stdout if requested
    
    if(log_)
        logMessage(message);

    // If running from within erlang, possibly notify any registered
    // processes of the message
    
#if WITH_ERL
    notify(message);
#endif

    // Finally, process the message
    
    processMessage(message);
}

/**.......................................................................
 * Process a message received from the broker
 */
void MosClient::processMessage(const struct mosquitto_message *message)
{
    std::string topic = message->topic;

    // If the message was received on the command topic, process the
    // command that was sent via the MQTT broker
    
    if(topic == commandTopic_) {
        processCommand(message);

        // Else process a normal message

    } else {
        
        if(!embedded_)
            storeMessage(message);
    }
}

/**.......................................................................
 * If we are using a leveldb backing store, store the message in it
 */
void MosClient::storeMessage(const struct mosquitto_message *message)
{
#if WITH_LEVELDB
    // We assume CSV for now.  First field is the key, the topic is the 'bucket'

    std::string str((const char*)message->payload, message->payloadlen);
    size_t idx = str.find_first_of(',');
    if(idx == std::string::npos) {
        ThrowRuntimeError("Payload format should be 'key, content'");
    }

    std::string bucket  = message->topic;
    std::string key     = str.substr(0, idx);

    // Skip the comma to get to the start of the content
    
    std::string content = str.substr(idx+1, str.size() - (idx+1));

    db_.put(bucket + "_" + key, content);
#endif
}

std::string MosClient::formatMessage(const struct mosquitto_message *message)
{
    return std::string((const char*)message->payload, message->payloadlen);
}

/**.......................................................................
 * Process a command received on the command topic
 */
void MosClient::processCommand(const struct mosquitto_message *message)
{
    std::map<std::string, std::string> entryMap = decodeJson(message);
    if(entryMap.find("command") == entryMap.end()) {
        ThrowRuntimeError("Commands should be valid JSON, containing a 'command' field" << std::endl << "\r"
                          "Invalid command was: '" << formatMessage(message) << "'");
    }

    std::string command = entryMap["command"];

    //------------------------------------------------------------
    // Subscribe to a new topic
    //------------------------------------------------------------
    
    if(command == "subscribe") {
        
        if(connected_) {

            if(entryMap.find("topic") == entryMap.end())
                ThrowRuntimeError("No topic was specified");
            
            int retVal = mosquitto_subscribe(mosq_, NULL, entryMap["topic"].c_str(), 0);
            if(retVal != MOSQ_ERR_SUCCESS)
                ThrowRuntimeError(formatMosError(retVal));
        }

        //------------------------------------------------------------
        // Toggle logging on/off
        //------------------------------------------------------------
        
    } else if(command == "logging") {

        std::string value = entryMap["value"];
        if(value == "on" || value == "true")
            log_ = true;
        else
            log_ = false;

        //------------------------------------------------------------
        // Dump all stored messages to another broker
        //------------------------------------------------------------

    } else if(command == "dump") {

        dumpToBroker(entryMap);

    } else {
        ThrowRuntimeError("Unrecognized command: " << command);
    }
}

/**.......................................................................
 * Dump stored messages to a specified broker
 */
void MosClient::dumpToBroker(std::map<std::string, std::string>& entryMap)
{
    struct mosquitto* mosq = mosquitto_new(NULL, true, this);

    if(!mosq)
        ThrowRuntimeError("Error allocating new mos session");

    int retVal=0;
    retVal = mosquitto_connect(mosq, entryMap["host"].c_str(), toInt(entryMap["port"]), keepAlive_);

    if(retVal != MOSQ_ERR_SUCCESS) {
        mosquitto_destroy(mosq);
        ThrowRuntimeError("Unable to connect");
    } else {
        COUT("Successfully connected");
    }

    try {
        db_.iterStart();
        std::string levelKey, levelVal;
        while(db_.iterValid()) {
            db_.iterGet(levelKey, levelVal);
            
            size_t idx = levelKey.find_first_of('_');
            if(idx == std::string::npos) {
                COUTRED("Expected 'bucket_key'  Got: '" << levelKey << "'");
            } else {
                
                std::string bucket  = levelKey.substr(0, idx);
                std::string key     = levelKey.substr(idx+1, levelKey.size() - (idx+1));
                
                // Re-publish on our message queue (normally this would be another message queue, but POC)
                
                if(connected_) {
                    int retVal = mosquitto_publish(mosq, NULL, bucket.c_str(), levelVal.size(), &levelVal[0], 0, false);
                    if(retVal != MOSQ_ERR_SUCCESS)
                        ThrowRuntimeError(formatMosError(retVal));

                    if(log_)
                        COUTGREEN("Published Bucket = " << bucket << " Key = '" << key << "' Val = '" << levelVal << "'");
                }
            }
            db_.iterStep();
        }
        db_.iterClose();

    } catch(std::runtime_error& err) {
        COUTRED("MQTT Caught an error while parsing dump message: " << std::endl << "\r  " << err.what());
    } catch(...) {
        COUT("MQTT Caught an unknown error while parsing dump message");
    }

    mosquitto_destroy(mosq);
    db_.iterClose();
}

/**.......................................................................
 * Parse a JSON string into tokens
 */
std::map<std::string, std::string> MosClient::decodeJson(const struct mosquitto_message* message)
{
    const char* str = (const char*)message->payload;

    std::map<std::string, std::string> entryMap;
    
    bool readTokens = false;
    std::ostringstream os;
    std::string field, value;
    for(int i=0; i <= message->payloadlen; i++) {

        // Json tokens are colon-separated.  Until we encounter one,
        // we are either reading the next field name, or ignoring
        // chars.
        
        if(str[i] == ':') {


            readTokens = true;
            field = os.str();
            os.str("");

        } else if(readTokens) {

            // Skip quotation marks
            
            if(str[i] == '"')
                continue;

            // If we hit a comma (next field), end of payload
            // (incorrectly formatted json), or closing brace (end of
            // json), we are done
            
            if(i == message->payloadlen || str[i] == ',' || str[i] == '}') {

                readTokens = false;
                value = os.str();
                os.str("");

                entryMap[field] = value;

            } else {
                os << str[i];
            }
            
        } else {
            if(!(str[i] == '{' || str[i] == '"' || str[i] == '}' || str[i] == ' ')) {
                os << str[i];
            }
        }
        
    }
    
    return entryMap;
}

int MosClient::toInt(std::string str)
{
    char* sptr = (char*)str.c_str();
    char* eptr = 0;

    errno = 0;
    long val = strtol(sptr, &eptr, 10);

    if(errno != 0)
        ThrowRuntimeError("Unable to convert '" << str << "' to an int: errno = " << errno);

    if (eptr == sptr)
        ThrowRuntimeError("Unable to convert '" << str << "' to an int: eptr = " << eptr << " sptr = " << sptr);

    return val;
}

//-----------------------------------------------------------------------
// Notify registered erlang processes of a new message
//-----------------------------------------------------------------------

#if WITH_ERL
void MosClient::notify(const struct mosquitto_message *message)
{
    try {
        
        // Reuse the allocated msgEnv.  This saves us having to alloc
        // and delete one for every message received, which is both
        // operationally intensive and unnecessary
        
        ERL_NIF_TERM result;
        
        // If the topic isn't in our map, we can't format it for TS
        
        if(topicMap_.find(message->topic) == topicMap_.end()) {
            ERL_NIF_TERM topic   = enif_make_string(msgEnv_, (const char*)message->topic, ERL_NIF_LATIN1);
            ERL_NIF_TERM payload = enif_make_string_len(msgEnv_, (const char*)message->payload, message->payloadlen, ERL_NIF_LATIN1);
            result = enif_make_tuple2(msgEnv_, topic, payload);
            
            // Else use the supplied schema to format the return message
            
        } else {
            result = formatForTs(message);
        }
        
        //------------------------------------------------------------
        // Iterate over the notification list, notifying any
        // subscribers that a message has arrived
        //------------------------------------------------------------
        
        for(std::list<std::pair<ErlNifEnv*, ErlNifPid> >::iterator iter=notificationList_.begin(); 
            iter != notificationList_.end(); iter++) {
            
            ErlNifPid pid = iter->second;
            enif_send(NULL, &pid, msgEnv_, result);
        }
        
        // Ready the environment for reuse
        
        enif_clear_env(msgEnv_);
        
    } catch(std::runtime_error& err) {
        
        std::ostringstream msg;
        for(int i=0; i < message->payloadlen; i++)
            msg << ((unsigned char*)message->payload)[i];
        
        ThrowRuntimeError(err.what() << std::endl << "\r" << "(while processing message '" << msg.str() << "')");
        
    } catch(...) {
        
        std::ostringstream msg;
        for(int i=0; i < message->payloadlen; i++)
            msg << ((unsigned char*)message->payload)[i];

        ThrowRuntimeError("Caught an unknown error (while processing message '" << msg.str() << "')");
    }
}
#endif

/**.......................................................................
 * Add our subscribe queue to the server
 */
void MosClient::addSubscribeList(struct mosquitto *mosq)
{
    ScopedLock(mutex_);
    
    // Subscribe to any topics that have been requested
    
    if(!topicList_.empty()) {
        for(std::list<std::string>::iterator iter=topicList_.begin();
            iter != topicList_.end(); iter++) {

            COUT("MQTT Subscribing to topic " << *iter);

            int retVal = mosquitto_subscribe(mosq, NULL, (*iter).c_str(), 0);
            if(retVal != MOSQ_ERR_SUCCESS)
                ThrowRuntimeError(formatMosError(retVal));
        }
    }

    // If running standalone, subscribe to name/command topic too

    if(!embedded_) {
        int retVal = mosquitto_subscribe(mosq, NULL, commandTopic_.c_str(), 0);
        if(retVal != MOSQ_ERR_SUCCESS)
            ThrowRuntimeError(formatMosError(retVal));
    }
}

/**.......................................................................
 * Private method to return a status summary
 */
std::string MosClient::getStatusSummaryPrivate()
{
    ScopedLock(mutex_);
    
    std::ostringstream os;

    os << (connected_ ? GREEN : RED) << "MQTT Client is " << (connected_ ? "" : "not ") << "connected to the broker" << GREEN << std::endl << std::endl << "\r";

    os << "Host:        " << host_     << std::endl << "\r";
    os << "Port:        " << port_     << std::endl << "\r";
    os << "Using certs: " << (useCerts_ ? "true" : "false") << std::endl << std::endl << "\r";
    os << "capath     = " << caPath_   << std::endl << "\r";
    os << "cafile     = " << caFile_   << std::endl << "\r";
    os << "certfile   = " << certFile_ << std::endl << "\r";
    os << "keyfile    = " << keyFile_  << std::endl << std::endl << "\r";
    
    os << "MQTT Client is currently subscribed to the following topics: " << std::endl << "\r";

    for(std::list<std::string>::iterator iter=topicList_.begin();
        iter != topicList_.end(); iter++) {
        std::string topic = *iter;
        os << topic;

#if WITH_ERL
        os << " with schema: " << topicMap_[topic].schema_;
#endif

        os <<  std::endl << "\r";
    }

    if(topicList_.empty()) {
        os << "  (none)" << std::endl << "\r";
    }
    
    os << NORM;
    
    return os.str();
}

void MosClient::blockForever()
{
    select(0, 0, 0, 0, 0);
}

