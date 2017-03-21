-module(mqtt_server).

-behaviour(gen_fsm).

%% API
-export([start_link/0]).
-export([init/1, wait_for_message/2, process_message/2, message/1]).
-export([code_change/4, terminate/3, handle_event/3, handle_sync_event/4, handle_info/3]).

%% ===================================================================
%% Public API
%% ===================================================================

%% @doc Starts an MQTT server

start_link() ->
    append_to_file("/tmp/debug.txt", "~p Inside RPBS start_link~n", [self()]),
    gen_fsm:start_link({local, mqtt_server}, mqtt_server, [], []).

%% @doc The gen_server init/1 callback, initializes the
%% mqtt_server.
init([]) ->
    append_to_file("/tmp/debug.txt", "~p Inside MQTT init wait_for_message next~n", [self()]),
    {ok, process_message, {nomsg}}.

wait_for_message(_Event, State) ->
    append_to_file("/tmp/debug.txt", "~p Inside MQTT wait_for_message 1 State = ~p~n", [self(), State]),
    timer:sleep(5000),
    {next_state, process_message, {msgcode}}.

process_message({message, Msg}, _State) ->
    append_to_file("/tmp/debug.txt", "~p Inside RPBS process_message 2 Msg = ~p~n", [self(), Msg]),
    {next_state, process_message, {message, none}}.

message(Msg) ->
        gen_fsm:send_event(mqtt_server, {message, Msg}).

append_to_file(Filename, Format, Data) ->
    case file:open(Filename, [append]) of                                                      
        {ok, IoDevice} ->
	                                                                          
            Bytes = io_lib:format(Format, Data),
	                                                   
            file:write(IoDevice, Bytes),
	                                                           
            file:close(IoDevice);
	                                                              
        {error, Reason} ->                                                                     
            io:format("~s open error  reason:~s~n", [Filename, Reason])                        
    end.

terminate(_Reason, _StateName, _State) ->
    ok.

code_change(_OldVsn, StateName, State, _Extra) ->
    {ok, StateName, State}.

handle_event(Event, _StateName, StateData) ->
    append_to_file("/tmp/debug.txt", "~p Inside RPBS handle_event Event = ~p~n", [self(), Event]),
    {stop, normal, StateData}.

handle_sync_event(_Event, _From, StateName, State) ->
    {reply, unknown_message, StateName, State}.

handle_info(_Info, _StateName, StateData) ->
    {stop, normal, StateData}.
