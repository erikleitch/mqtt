// -------------------------------------------------------------------
// mqtt: MQTT NIF client service for Erlang/C++
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

#ifndef INCL_MQTT_H
#define INCL_MQTT_H

#include "erl_nif.h"

namespace mqtt {

ERL_NIF_TERM command(ErlNifEnv* env, int argc, const ERL_NIF_TERM argv[]);

} // namespace mqtt


#endif
