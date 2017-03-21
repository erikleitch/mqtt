# `mqtt` - A simple Erlang MQTT client NIF

```mqtt``` is a NIF library that provides a simple interface to an
embedded C++ mosquitto client (see
https://github.com/eclipse/mosquitto).  The repo provides a simple
client that can be run stand-alone in either an erlang or C++ context,
or embedded in another erlang process.

<hr>

## Compiling

The ```mqtt``` module is built on top of the mosquitto client library,
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

## Stand-alone usage

At its most basic, both `bin/tMqtt` and `mqtt:spawnClient()` spawn a
client that connects to a default broker running on localhost.  On
startup, the clients subscribe to a special command topic, by default
called: `mosclient/command`.  JSON-formatted MQTT messages sent to
this topic of the form `{command:cmdname, arg1:val1, arg2:val2,...}`
are processed internally as commands.

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

## Stand-alone erlang usage

The erlang module supports additional functionality that lets you
configure the startup connection to the broker, via the
`mqtt:command/1` interface.  This can take a tuple of `{param, value}`
pairs, or a list of such tuples.

For example:

```
mqtt:command([
     {host,     "a1e72kiiddbupq.iot.us-east-1.amazonaws.com"},
     {port,     8883},
     {capath,   "/path/to/my/cert/files"},
     {cafile,   "root-CA.crt"},
     {certfile, "riak-sink.cert.pem"},
     {keyfile,  "riak-sink.private.key"}
]).
```

would configure the module to establish a connection to point to a host in AWS at
port 8883, with relevant security, when `mqtt:spawnClient()` is called.

The command interface gives you complete control over the client
behavior, including subscribing to topics:

```
mqtt:command({subscribe, "temperatures", [varchar,double,timestamp,varchar], json})
```

or even starting the client:

```
mqtt:command({start}).
```

Type `mqtt:command({help})` for a list of functions the module supports.

## Embedded erlang Usage

At its most basic, the ```mqtt``` module exports a single-point
interface for use within RiakTS.  Executing the function
```mqtt:startCommsLoopRiak()``` at any (single) point in the Riak
codebase causes two things to happen:

* It spawns a background C++ thread that establishes a link to a local
  MQTT broker, assumed to be listening on port 1883.

* It spawns a background erlang process that blocks on receipt of MQTT
  messages from the C++ client thread.  When spawned, the process
  subscribes to two default topics, each corresponding to a TS table: ```DeviceData``` and ```GeoCheckin```, assumed to have the schema ```[sint64, timestamp, sint64]``` and ```[varchar, varchar, timestamp, sint64, varchar, double, boolean]```, respectively.

<hr>
## Manual Usage

Additionally, the module exports a simple command interface, ```mqtt:command(CommandTuple)``` for
manual testing/manipulation of the client.

The single argument is either a tuple of ```{CommandAtom, OptionalVal1, OptionalVal2,...}```, _or_ a list of such command tuples.

Recognized commands are:

   * ```{subscribe, Topic, Schema, Format}```

       Arguments are:
       
       * Topic  -- topic name (list, i.e., ```"DeviceData" ```)
       * Schema -- schema (list of valid TS type atoms, like ```[varchar, double, timestamp]```)
       * Format -- atom (```csv``` or ```json```), optional (defaults to ```csv```)
	
	   If specified as cvs, mqtt expects string messages to be formatted as comma-separated items: `val1, val2, val3`
	   If specified as json, mqtt expects string messages to be formatted as json: `{"name1":val1, "name2":val2, "name3":val3}`

    Adds a new topic to the list of topics the client should subscribe
    to.  In the context of TS, topic corresponds directly to a TS
    table name, and the Schema to the schema for that table.  This
    schema will be used to encode the string data received from the
    MQTT broker before handing back to the erlang layer, and consists
    of a list of TS table field types, in order.

    For example: to subscribe to data for TS table GeoCheckin, use:
 
      ```mqtt:command({subscribe, "GeoCheckin", [varchar, varchar, timestamp, sint64, double, boolean]})```

   * ```{register}```

     * Registers the calling process to be notified when messages
      are received from the broker on any of the subscribed topics

   * ```{start}```

     * Starts up the background MQTT client (should be called only once)

   * ```{status}```

     * Print (and return) a status summary for the MQTT client

   * ```{logging, on|off}```

     * Toggle logging of incoming messages (to stdout/riak log)

<hr>
### Example

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
		  