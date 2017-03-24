%%=======================================================================
%% mqtt: MQTT NIF client service for Erlang/C++
%%
%% Copyright (c) 2010-2016 Basho Technologies, Inc. All Rights Reserved.
%%
%% This file is provided to you under the Apache License, Version 2.0
%% (the "License"); you may not use this file except in compliance
%% with the License.  You may obtain a copy of the License at
%%
%%   http://www.apache.org/licenses/LICENSE-2.0
%%
%% Unless required by applicable law or agreed to in writing, software
%% distributed under the License is distributed on an "AS IS" BASIS,
%% WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or
%% implied.  See the License for the specific language governing
%% permissions and limitations under the License.
%%
%%=======================================================================

-module(mqtt).

-export([command/1, 
	 startCommsLoop/2, 
	 startCommsLoop/3, 
	 startCommsLoopRiak/0, 
	 startCommsLoopRiak/1, 
	 startCommsLoopRiakAlex/0, 
	 startCommsLoopNoneAlex/0, 
	 startCommsLoopPrint/0, 
	 startCommsLoopFake/0]).

-compile([export_all]).

-on_load(init/0).

-ifdef(TEST).
-compile(export_all).
-ifdef(EQC).
-include_lib("eqc/include/eqc.hrl").
-define(QC_OUT(P),
        eqc:on_output(fun(Str, Args) -> io:format(user, Str, Args) end, P)).
-endif.
-include_lib("eunit/include/eunit.hrl").
-endif.

-import(lager,[info/2]).

-include_lib("include/mqtt.hrl").

%%=======================================================================
%% Main interface to the mqtt NIF.
%%
%% Usage:
%%
%% command/1 is a multi-use function whose single argument is a tuple
%% of
%%
%% {command, val1, val2,...}
%%
%% combinations.
%%
%% Recognized commands are:
%%
%%    {subscribe, Topic, Schema, Format}
%%
%%        Topic  -- topic name (list)
%%        Schema -- schema (list of atoms)
%%        Format -- atom (csv|json), optional
%%
%%        Adds a new topic to the list of topics the client should
%%        subscribe to.  In the context of TS, topic corresponds
%%        directly to a TS table name, and the Schema to the schema
%%        for that table.  This schema will be used to encode the
%%        string data received from the MQTT broker before handing
%%        back to the erlang layer, and consists of a list of TS
%%        table field types.
%%
%%        Example: to subscribe to CSV-encoded string data for TS table GeoCheckin:
%% 
%%           {subscribe, "GeoCheckin", [varchar, varchar, timestamp, sint64, double, boolean], csv}
%%
%%    {register}
%%
%%        Registers the calling process to be notified when messages
%%        are received from the broker on any of the subscribed topics
%%
%%    {start}
%%
%%        Starts up the background MQTT client (should be called only once)
%%
%%=======================================================================

command(_Tuple) ->
    erlang:nif_error({error, not_loaded}).

%%=======================================================================
%% Spawn the MQTT client
%%=======================================================================

spawnClient() ->
    command({start}).

%%=======================================================================
%% Spawn a background erlang process, listening for messages received
%% from the MQTT broker.  Takes as its only argument a callback function
%% to be executed when a new message is received
%%=======================================================================

spawnListener(CallbackFn) ->
    spawn(mqtt, notifyServer, [CallbackFn]).

%%-----------------------------------------------------------------------
%% The function executed by the spawned listener process.  Registers
%% self to be notified when messages arrive, and goes into a wait loop
%%-----------------------------------------------------------------------

notifyServer(CallbackFn) ->
    mqtt:command({register}),
    waitForNextMessage(CallbackFn).

waitForNextMessage(CallbackFn) ->
    receive Msg ->
            CallbackFn(Msg)
    end,
    waitForNextMessage(CallbackFn).

%%=======================================================================
%% Spawn a background erlang process that periodically checks for new
%% TS tables.  As new tables are found, they are added to the list of
%% subscribed topics
%%=======================================================================

spawnScanner() ->
    spawn(mqtt, checkForNewTables, [1, sets:new()]).
    
%%-----------------------------------------------------------------------
%% Check for new tables.  If any are found, add them to the list of
%% tables subscribed to, and go to sleep for the prescribed interval.
%% If the directory structure does not yet exist, we quitely catch
%% {error, enoent} and try again later.
%%-----------------------------------------------------------------------

checkForNewTables(CheckIntervalSeconds, PreviousTables) ->
    ListRet = getModList(),
    case ListRet of

        %% Dir doesn't exist yet -- go to sleep and try again

        {error, enoent} ->
            timer:sleep(CheckIntervalSeconds * 1000),
            checkForNewTables(CheckIntervalSeconds, PreviousTables);

        %% We have a list of modules.  Extract schema and compare to
        %% the previous list

        ModList ->
            {NewTables, CurrentTables} = getCurrentTables(ModList, PreviousTables),
            [mqtt:command({subscribe, TableName, TableSchema}) || {TableName, TableSchema} <- NewTables],
            timer:sleep(CheckIntervalSeconds * 1000),
            checkForNewTables(CheckIntervalSeconds, CurrentTables)
    end.
    
%%-----------------------------------------------------------------------
%% List DDL directory.  We use the list of beam files in the DDL
%% directory to determine what TS tables currently exist.  If the
%% directory structure does not yet exist, we quietly catch {error,
%% enoent} and return it.
%%-----------------------------------------------------------------------

getModList() ->
    Ret = file:list_dir(app_helper:get_env(riak_core,platform_data_dir) ++ "/ddl_ebin"),
    case Ret of 
        {error, enoent} ->
            Ret;
        {ok, FileList} ->
            Fn =
                fun(File) ->
                        EndLoc = string:str(File, ".beam"),
                        list_to_atom(string:substr(File, 1, EndLoc-1))
                end,
            [Fn(File) || File <- FileList]
    end.

%%-----------------------------------------------------------------------
%% Get the current set of TS table schemas, and a list of any new ones
%%-----------------------------------------------------------------------

getCurrentTables(ModList, OldSchemaSet) ->

    %% Generate a listing of the current set of TS table schema from
    %% the list of modules that was passed. get_ddl() is called
    %% on each module to extract the schema from the DDL

    Fn =
        fun(Mod) ->
                {ddl_v2, Name, SchemaList, _Key1, _Key2, _Version} = Mod:get_ddl(),
                Schema = [Type || {_Vers, _Name, _Order, Type, _Null} <- SchemaList],
                {binary_to_list(Name), Schema}
        end,

    CurrentSchemaSet = sets:from_list([Fn(Mod) || Mod <- ModList]),
            
    %% Get schemas which are new since we last checked -- this is just
    %% the difference between the new set and the old set.  We return
    %% a tuple of new schemas, and the current set of known schemas
    
    NewSchemas = sets:subtract(CurrentSchemaSet, OldSchemaSet),
    {sets:to_list(NewSchemas), CurrentSchemaSet}.

%%=======================================================================
%% Communication loop framework
%%=======================================================================

startCommsLoop([], CallbackFn, StartScanner) ->
    startCommsLoop(CallbackFn, StartScanner);
startCommsLoop(OptList, CallbackFn, StartScanner) ->
    mqtt:command(OptList),
    startCommsLoop(CallbackFn, StartScanner).

startCommsLoop(CallbackFn, true) ->
    spawnClient(),
    spawnScanner(),
    spawnListener(CallbackFn);
startCommsLoop(CallbackFn, false) ->
    spawnClient(),
    spawnListener(CallbackFn).

%%-----------------------------------------------------------------------
%% Riak TS comms loop, that forwards received messages to
%% riak_kv_ts_svc for processing
%%-----------------------------------------------------------------------

startCommsLoopRiak() ->
    startCommsLoop([], fun mqtt:sendTsMsg/1, false).

startCommsLoopRiak(OptList) ->
    startCommsLoop(OptList, fun mqtt:sendTsMsg/1, false).
    
startCommsLoopRiakAlex() ->
    startCommsLoopRiak([
			{host,     "a1e72kiiddbupq.iot.us-east-1.amazonaws.com"},
			{port,     8883},
			{capath,   "/Users/eml/projects/mqtt/alexpi"},
			{cafile,   "root-CA.crt"},
			{certfile, "riak-sink.cert.pem"},
			{keyfile,  "riak-sink.private.key"}
		       ]).

sendTsMsg(MqttMsg) ->
    {Topic, ValTuple} = MqttMsg,
    TsMsg = {tsputreq, list_to_binary(Topic), [], [ValTuple]},
    State = {state,undefined,undefined,undefined,undefined},
    io:format("About to put message: ~p~n", [TsMsg]),
    Ret   = riak_kv_ts_svc:process(TsMsg, State),
    case Ret of
        {reply, {tsputresp}, State} ->
            ok;
        _ ->
            io:format("MQTT Sent Msg = ~p Got Ret = ~p~n", [TsMsg, Ret])
    end.

%%-----------------------------------------------------------------------
%% Test comms loop that just prints messages as they come in 
%%-----------------------------------------------------------------------

startCommsLoopPrint() ->
    startCommsLoop([{store, true}], fun(Msg) -> io:format("Received message: ~p~n", [Msg]) end, false).

%%-----------------------------------------------------------------------
%% Comms loop that uses cert files and prints messages received from
%% the broker
%%-----------------------------------------------------------------------

startCommsLoopNoneAlex() ->
    OptList = [{host,     "a1e72kiiddbupq.iot.us-east-1.amazonaws.com"},
	       {port,     8883},
	       {capath,   "/Users/eml/projects/mqtt/alexpi"},
	       {cafile,   "root-CA.crt"},
	       {certfile, "riak-sink.cert.pem"},
	       {keyfile,  "riak-sink.private.key"}],
    startCommsLoop(OptList, fun(Msg) -> io:format("Received message: ~p~n", [Msg]) end, false).

%%-----------------------------------------------------------------------
%% Test comms loop that forwards a fake TS message for every received
%% message. Used only for testing
%%-----------------------------------------------------------------------

startCommsLoopFake() ->
    startCommsLoop(fun mqtt:sendFakeKvMsg/1, true).

sendFakeKvMsg(_Msg) ->
    State = {state,undefined,undefined,undefined,undefined},
    Msg = {tsputreq, <<"GeoCheckin">>, [], [{<<"familyMqtt">>,<<"seriesMqtt">>,random:uniform(10000),-2,<<"binval">>,1.234,true}]},
    Ret   = riak_kv_ts_svc:process(Msg, State),
    case Ret of
        {reply, {tsputresp}, State} ->
            ok;
        _ ->
            io:format("MQTT Sent Msg = ~p Got Ret = ~p~n", [Msg, Ret])
    end.

%%=======================================================================
%% Module initialization
%%=======================================================================

-spec init() -> ok | {error, any()}.
init() ->
    SoName = case code:priv_dir(?MODULE) of
                 {error, bad_name} ->
                     case code:which(?MODULE) of
                         Filename when is_list(Filename) ->
                             filename:join([filename:dirname(Filename),"../priv", "libmqtt"]);
                         _ ->
                             filename:join("../priv", "libmqtt")
                     end;
                 Dir ->
                     filename:join(Dir, "libmqtt")
             end,

    erlang:load_nif(SoName, [{opts, application:get_all_env(mqtt)}]).
