# `mqtt` - A simple Erlang MQTT client NIF

```mqtt``` is a NIF library that provides a simple interface to an
embedded C++ mosquitto client (see
https://github.com/eclipse/mosquitto).  The repo provides a simple
client that can be run stand-alone in either an erlang or C++ context,
or embedded in another erlang process.

<hr>

## Compiling

The `mqtt` module is built on top of the mosquitto client library,
and requires the following environment variables to be defined:

* MQTT_INC_DIR pointing to the installation of the mosquitto.h header
file

* MQTT_LIB_DIR pointing to the installation of the libmosquitto library

With those defined, you should just be able to type `make`.  The
default rules will create the executable `bin/tMqtt` which trivially
spawns a stand-alone C++ client, and `ebin/mqtt.beam`, which provides
an erlang interface to the client library.

Additionally, both the erlang and C++ standalone versions support a
leveldb backing store, if compiled with environment variable
MQTT_USE_LEVELDB set to 1.  In this case, leveldb and snappy
libraries will be built when `make` is invoked.

<hr>

## Description

At its most basic, the mqtt module allows you to spawn an MQTT client
in erlang, and register to be notified when messages arrive on any
subscribed topics.  The module provides convenience functions for
registering callbacks when messages are received.

For example,`mqtt:spawnClient/0` spawns a client that connects to a
default broker running on localhost, and `mqtt:spawnListener/1` allows
you to register a callback function that is called when messages
arrive on subscribed topics.  Alternatively, `mqtt:startCommsLoop/3`
provides a unified interface for configuring and starting the client,
as well as registering a callback.

These functions are built on top of a single-point erlang command
interface ```mqtt:command(CommandTuple)```, provided by the NIF for
manipulation of the client. 

A parallel MQTT command interface is provided by writing messages to a
special command topic, which the client automatically subscribes to on
start-up.

For documentation of both of these interfaces, see <a
href=#interface>Command Interface</a>, below.

You can subscribe to new topics on the fly, and optionally, if
compiled with `MQTT_USE_LEVELDB=1`, the client can be configured to
store messages in a local leveldb instance.  If using a backing store,
messages can be relayed to another broker by issuing the `dump`
command via either the erlang or MQTT interface.

<hr>

<a name=interface></a>
## Command Interface

The module provides a simple erlang interface for manipulating the
client, via the `mqtt:command/1` method.

The single argument is either a tuple of `{CommandAtom, OptionalVal1,
OptionalVal2,...}`, _or_ a list of such command tuples.

Additionally, the client provides a parallel MQTT command interface.
On startup, the clients subscribe to a special command topic, by
default called: `mosclient/command` (this can be modified by using the
`mqtt:command({name, Name})` option).  JSON-formatted MQTT messages
sent to this topic of the form `{command:cmdname, arg1:val1,
arg2:val2,...}` are processed internally as commands.

Both interfaces are documented below.

Recognized commands are:

   * dump

       erlang: `mqtt:command({dump, Host, Port, DelayMs})`<br>
       MQTT:   `{command:dump, host:Host, port:Port, delayms:DelayMs}`

       If storing messages, dump stored messages to the specified broker

       Arguments are:

       * Host -- the host on which the broker is running
       * Port -- the port on which the broker is listening
       * DelayMs -- delay, in ms, between writes to the broker
       
   * logging

       erlang: `mqtt:command({logging, on|off})`<br>
       MQTT:   `{command:logging, value:on|off}`

       Toggle client logging (to stdout)

   * subscribe

       erlang: `mqtt:command({subscribe, Topic, Schema, Format})`<br>
       MQTT:   `{command:subscribe, topic:Topic, schema:Schema, format:Format}`

       Adds a new topic to the list of topics the client should
       subscribe to.  (In the context of RiakTS, topic may correspond
       directly to a TS table name, and the Schema to the schema for
       that table.)  This schema will be used to encode the string data
       received from the MQTT broker before handing back to the erlang
       layer, and consists of a list of TS table field types, in
       order.

       Arguments are:
       
       * Topic  -- topic name (i.e., `"DeviceData"`)
       * Schema -- schema (list of valid type atoms, like `[varchar, double, timestamp]`)

       	    If specified, the client will attempt to format csv or
            json fields (see below) to match the specified type when
            processing MQTT messages.  If not specified, the message
            will be returned as a single binary type.
	   
       * Format -- atom (`csv` or `json`), optional (defaults to `csv`)

       	    If specified as cvs, mqtt expects string messages to be formatted as comma-separated items: `val1, val2, val3`
	    If specified as json, mqtt expects string messages to be formatted as json: `{"name1":val1, "name2":val2, "name3":val3}`

        For example, to subscribe to topic "GeoCheckin", whose messages are expected
        to be of the format: "mystring, 100012, 3, 1.234, false"

```erlang
	   mqtt:command({subscribe, "GeoCheckin", [varchar, timestamp, sint64, double, boolean], csv})
```

   * register

       erlang: `mqtt:command({register})`<br>
       MQTT: N/A

       Registers the calling process to be notified when messages are
       received from the broker on any of the subscribed topics.  If a
       schema was supplied when subscribing, the fields will be
       formatted appropriately when the calling process is notified.
       Else they will be strings.

   * start

       erlang: `mqtt:command({start})`<br>
       MQTT: N/A

       Starts up the background MQTT client (this should be called only once)

   * status

       erlang: `mqtt:command({status})`<br>
       MQTT: `{command:status}`

       Print a status summary for the MQTT client

   * [option]

       erlang: `mqtt:command({Option, Value})`<br>
       MQTT: N/A
   
       Configure the startup connection to the broker, by supplying an
       appropriate `{Option, Value}` tuple:

       Client specs:

       * `name` - an internal name for the client.  Client will
         register on topic "[name]/command" to listen for MQTT
         commands, and if using a backing store, will use
         "/tmp/[name]" as the root leveldb directory.

       * `useleveldb` - true to use a leveldb backing store.  Must have
         compiled with MQTT_USE_LEVELDB=1
       
       Connection specs:
       
       * `host` - broker to connect to, e.g. "a1e72kiiddbupq.iot.us-east-1.amazonaws.com"
       * `port` - broker port to connect to, e.g. 8883},

       Connection security

       * `capath` - path to certificate authority files, e.g. "/path/to/my/cert/files"
       * `cafile` - root certificate file, e.g. "root-CA.crt"
       * `certfile` - certificate file, e.g. "riak-sink.cert.pem"},
       * `keyfile` - keyfile, "riak-sink.private.key"

For example to configure the module to establish a connection to
       point to a host in AWS at port 8883, with relevant security,
       when `mqtt:spawnClient()` is called, use:

```erlang
           mqtt:command([
                {host,     "a1e72kiiddbupq.iot.us-east-1.amazonaws.com"},
                {port,     8883},
                {capath,   "/path/to/my/cert/files"},
                {cafile,   "root-CA.crt"},
                {certfile, "riak-sink.cert.pem"},
                {keyfile,  "riak-sink.private.key"}
              ]).
```

<hr>

## MQTT Example

For example, to tell the default client to subscribe to a new topic `newtopic`:

```
mosquitto_pub -t "mosclient/command" -m "{command:subscribe, topic:newtopic}"
```

Messages subsequently sent to `newtopic` will be processed by the
client.

If using with leveldb, CSV messages of the form `"key, content"` will
be stored in a leveldb instance, indexed by key. 

If messages are being stored, you can replay them to another broker by
sending a message like:

```
mosquitto_pub -t "mosclient/command" -m "{command:dump, host:localhost, port:1884, delayms:1}"
```

this would cause stored messages to be replayed to a local broker
listening on port 1884, with a 1-ms delay between publishing each key
(some brokers cannot be published to as fast as clients can write).

<hr>

## Extended Examples

Suppose you've previously started a mosquitto broker, as in:

```mosquitto -c /usr/local/etc/mosquitto/mosquitto.conf```

You can start up an erlang shell, and initialize the comms loop, like:

```
Erlang R16B02_basho8 (erts-5.10.3) [source] [64-bit] [smp:8:8] [async-threads:10] [hipe] [kernel-poll:false]

Eshell V5.10.3  (abort with ^G)
1> mqtt:startCommsLoopNone().
<0.41.0>
Attempting to reconnect...
Successfully connected
Subscribing to topic DeviceData
Subscribing to topic GeoCheckin
Subscribed (mid: 1) 2
Subscribed (mid: 2) 2
```

Subsequently publishing data to the broker on a subscribed topic will
now cause your erlang shell to print the messages as they are
received.  For example

```
mosquitto_pub -t "GeoCheckin" -m "familyMqtt,seriesMqtt,$iIter,1,binval,1.234,true"
```
issued from a shell will produce:

```
                  Received message: {tsputreq,<<"GeoCheckin">>,[],
                              [{<<"familyMqtt">>,<<"seriesMqtt">>,30,1,
                                <<"binval">>,1.234,true}]}
2>
```

As you can see, because the MQTT message has a known schema, the
message returned to erlang has already been correctly formatted for
handoff to TS.

You can subscribe to additional topics at any time.  For example if
you typed

```
2> mqtt:command({subscribe, "Test", [varchar]}).
ok
Subscribed (mid: 3)
```

then publishing data to the broker on the new topic, like

```mosquitto_pub -t "Test" -m "this is a test"```

will now cause the new messages to be received in erlang:

```
                  Received message: {tsputreq,<<"Test">>,[],[{<<"this is a test">>}]}
3>
```
		  