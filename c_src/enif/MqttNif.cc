// -------------------------------------------------------------------
// mqtt: MQTT NIF client service for Erlang/C++
//
// Copyright (c) 2011-2015 Basho Technologies, Inc. All Rights Reserved.
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

#include <algorithm>
#include <deque>
#include <new>
#include <set>
#include <sstream>
#include <stack>
#include <stdexcept>
#include <syslog.h>
#include <utility>
#include <vector>

#include "ExceptionUtils.h"
#include "MqttNif.h"

#include "ErlUtil.h"
#include "LevelManager.h"
#include "MosClient.h"

// This NIF exports only one function.  This makes it easy to add new
// functionality without writing heaps of extra C++ connective tissue

static ErlNifFunc nif_funcs[] =
{
    {"command",  1, mqtt::command},
};

namespace mqtt {

    // Atoms (initialized in on_load)

    ERL_NIF_TERM ATOM_OK;
    ERL_NIF_TERM ATOM_ERROR;

    ERL_NIF_TERM processOptTuple(ErlNifEnv* env, ERL_NIF_TERM tuple);
}

using std::nothrow;
using namespace nifutil;

namespace mqtt {

    //------------------------------------------------------------
    // Implement the single command we support
    //------------------------------------------------------------
    
    ERL_NIF_TERM command(ErlNifEnv* env, int argc, const ERL_NIF_TERM argv[])
    {
        try {

            if(ErlUtil::isList(env, argv[0])) {
                std::vector<ERL_NIF_TERM> optTuples = ErlUtil::getListCells(env, argv[0]);
                std::vector<ERL_NIF_TERM> optRets(optTuples.size());
                
                for(unsigned i=0; i < optTuples.size(); i++) {
                    optRets[i] = processOptTuple(env, optTuples[i]);
                }

                return enif_make_list(env, optRets.size(), &optRets[0]);
                
            } else {
                return processOptTuple(env, argv[0]);
            }

            return ATOM_OK;
            
        } catch(std::runtime_error& err) {
            COUT("Caught an error: " << err.what());
            ERL_NIF_TERM msg_str  = enif_make_string(env, err.what(), ERL_NIF_LATIN1);
            return enif_make_tuple2(env, mqtt::ATOM_ERROR, msg_str);
        } catch(...) {
            ERL_NIF_TERM msg_str  = enif_make_string(env, "Unhandled exception caught", ERL_NIF_LATIN1);
            return enif_make_tuple2(env, mqtt::ATOM_ERROR, msg_str);
        }
    }

    //------------------------------------------------------------
    // Process a single options tuple that was passed to command
    //------------------------------------------------------------

    ERL_NIF_TERM processOptTuple(ErlNifEnv* env, ERL_NIF_TERM tuple)
    {
        std::vector<ERL_NIF_TERM> cells = ErlUtil::getTupleCells(env, tuple);
        std::string atom  = ErlUtil::formatTerm(env, cells[0]);

        //------------------------------------------------------------
        // Print help about recognized options
        //------------------------------------------------------------
        
        if(atom == "help") {
            
            COUTGREEN(std::endl << "\r" << "Supported commands are: ");

            COUTGREEN(std::endl << "\r" << " mqtt:command({logging,  on | off})");
            COUTGREEN("    To toggle optional logging");
            COUTGREEN(std::endl << "\r" << " mqtt:command({OptName,  Value})");
            COUTGREEN("    To configure a supported client option");
            COUTGREEN(std::endl << "\r" << " mqtt:command({register,  Pid})");
            COUTGREEN("    To register a Pid to be notified on receipt of a message");
            COUTGREEN(std::endl << "\r" << " mqtt:command({status})");
            COUTGREEN("    To print a connection status summary");
            COUTGREEN(std::endl << "\r" << " mqtt:command({start})");
            COUTGREEN("    To start the background comms loop");
            COUTGREEN(std::endl << "\r" << " mqtt:command({subscribe, TopicName, SchemaList, FormatAtom})");
            COUTGREEN("    To subscribe to topic TopicName, with SchemaList (example: [sint64, timestamp, double, varchar]) and FormatAtom (either csv or json)");

            COUTGREEN(std::endl << "\r" << "Or a list of any of the above.");
            
            return ATOM_OK;
        }
        
        //------------------------------------------------------------
        // Register to be notified when messages arrived on any of
        // the subscribed topics
        //------------------------------------------------------------
        
        if(atom == "register") {
            
            ErlNifEnv* localEnv = enif_alloc_env();
            ErlNifPid remotePid, localPid;
            ERL_NIF_TERM pidTerm = enif_make_pid(localEnv, enif_self(env, &remotePid));
            
            if(enif_get_local_pid(env, pidTerm, &localPid)==0)
                ThrowRuntimeError("Failed to create local PID");
            
            MosClient::registerPid(localEnv, localPid);
            return ATOM_OK;
        }
        
        //------------------------------------------------------------
        // Start the client connection
        //------------------------------------------------------------
        
        else if(atom == "start") {
            MosClient::startCommsLoop();
            return ATOM_OK;
        }
        
        //------------------------------------------------------------
        // Subscribe to a topic
        //------------------------------------------------------------
            
        else if(atom == "subscribe") {

            if(cells.size() < 3)
                ThrowRuntimeError("Usage: command([subscribe, Topic, Schema, Format])");

            std::string topic     = ErlUtil::getString(env, cells[1]);
            std::string schemaStr = ErlUtil::formatTerm(env, cells[2]);
                
            std::vector<ERL_NIF_TERM> schemaTerms = ErlUtil::getListCells(env, cells[2]);
            std::vector<STRING_CONV_FN_PTR> convFnVec;
                
            for(unsigned i=0; i < schemaTerms.size(); i++) {
                std::string atom = ErlUtil::getAtom(env, schemaTerms[i]);
                convFnVec.push_back(ErlUtil::getStringConvFn(atom));
            }

            std::string format = "csv";
            
            if(cells.size() == 4)
                format = ErlUtil::formatTerm(env, cells[3]);
            
            MosClient::subscribe(topic, schemaStr, convFnVec, format);
            
            return ATOM_OK;
        }

        //------------------------------------------------------------
        // Print status information about the client
        //------------------------------------------------------------
            
        else if(atom == "status") {
            std::string status = MosClient::getStatusSummary();
            COUT(status);
            return ATOM_OK;
        }

        //------------------------------------------------------------
        // Toggle logging on/off
        //------------------------------------------------------------
            
        else if(atom == "logging") {
            bool log = (ErlUtil::getAtom(env, cells[1]) == "on");
            MosClient::toggleLogging(log);
            return ATOM_OK;
        }

        //------------------------------------------------------------
        // Other options
        //------------------------------------------------------------
            
        else if(atom == "cunit") {
            std::string test = ErlUtil::getAtom(env, cells[1]);

            if(test == "leveldb_write") {
                LevelManager db;
                db.open("/tmp/levelTest");
                std::string key = ErlUtil::getAtom(env, cells[2]);
                std::string val = ErlUtil::getAtom(env, cells[3]);
                db.put(key, val);
                db.close();

            } else if(test == "leveldb_read") {
                LevelManager db;

                db.open("/tmp/levelTest");
                std::string key = ErlUtil::getAtom(env, cells[2]);
                std::string val = db.get(key);
                db.close();
                return enif_make_string(env, val.c_str(), ERL_NIF_LATIN1);

            } else {

                COUT("Unrecognized test: " << test);

            }
            
            return ATOM_OK;
        }
        
        //------------------------------------------------------------
        // Other options
        //------------------------------------------------------------

        else {
            MosClient::setOption(env, atom, cells[1]);
            return ATOM_OK;
        }
    }
}

static void on_unload(ErlNifEnv *env, void *priv_data) {}

static int on_load(ErlNifEnv* env, void** priv_data, ERL_NIF_TERM load_info)
{
    try {
        int ret_val = 0;
        
        mqtt::ATOM_OK    = enif_make_atom(env, "ok");
        mqtt::ATOM_ERROR = enif_make_atom(env, "error");
        
        COUTGREEN(std::endl << "\r" << "For information on supported commands, use mqtt:command({help})" << std::endl);

        return ret_val;
            
    } catch(std::exception& e) {
        return -1;
    } catch(...) {
        return -1;
    }
}

extern "C" {
    ERL_NIF_INIT(mqtt, nif_funcs, &on_load, NULL, NULL, &on_unload);
}

