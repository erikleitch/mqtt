## `mqtt` - A simple Erlang MQTT client NIF

<hr>
### What is mqtt?

```mqtt``` is a NIF library that provides a simple interface to an
embedded C++ mosquitto client (see
https://github.com/eclipse/mosquitto).  The repo provides a simple
client that can be run stand-alone in either an erlang or C++ context,
or embedded in another erlang process.

<hr>
### Compiling

The ```mqtt``` module is built on top of the mosquitto client library,
and requires the following environment variables to be defined:

* MQTT_INC_DIR pointing to the installation of the mosquitto.h header
file

* MQTT_LIB_DIR pointing to the installation of the libmosquitto library

With those defined, you should just be able to type `make`.

* MQTT_USE_LEVELDB
<hr>
### Riak Usage

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
### Manual Usage

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
		  