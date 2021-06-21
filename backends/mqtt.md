### The `mqtt` backend

This backend provides input from and output to a message queueing telemetry transport (MQTT)
broker. The MQTT protocol is used in lightweight sensor/actor applications, a wide selection
of smart home implementations and as a generic message bus in many other domains.

The backend implements both the older protocol version MQTT v3.1.1 as well as the current specification
for MQTT v5.0.

#### Global configuration

This backend does not take any global configuration.

#### Instance configuration

| Option	| Example value		| Default value 	| Description				|
|---------------|-----------------------|-----------------------|---------------------------------------|
| `host`	| `mqtt://10.23.23.1`	| none			| Host or URI of the MQTT broker	|
| `user`	| `midimonster`		| none			| User name for broker authentication	|
| `password`	| `mm`			| none			| Password for broker authentication	|
| `clientid`	| `MM-main`		| random		| MQTT client identifier (generated randomly at start if unset) |
| `protocol`	| `3.1.1`		| `5`			| MQTT protocol version (`5` or `3.1.1`) to use for the connection |

The `host` option can be specified as an URI of the form `mqtt[s]://[username][:password]@host.domain[:port]`.
This allows specifying all necessary settings in one configuration option.

#### Data exchange format

The MQTT protocol places very few restrictions on the exchanged data. Thus, it is necessary to specify the input
and output data formats accepted respectively output by the MIDIMonster.

The basic format, without further channel-specific configuration, is an ASCII/UTF-8 string representing a floating
point number between `0.0` and `1.0`. The MIDIMonster will read these and use the value as the normalized event value.

Channels may be specified to use a different value range or even freeform discrete values by preconfiguring
the channels in the instance configuration section. This is done by specifying options of the form

```
<channel> = range <min> <max>
<channel> = discrete [!]<min> [!]<max> <value>
```

Example configurations:
```
/a/topic = range -10 10
/another/topic = discrete !0.0 0.5 off
/another/topic = discrete 0.5 !1.0 on
```

Note that there may be only one range configuration per topic, but there may be multiple discrete configurations.

The first channel preconfiguration example will change the channel value scale to values between `-10` and `10`.
For input channels, this sets the normalization range. The MIDIMonster will normalize the input value according to the scale.
For output channels, this sets the output scaling factors.

The second and third channel preconfigurations define two discrete values (`on` and `off`) with accompanying normalized
values. For input channels, the normalized channel value for a discrete input will be the value marked with an exclamation mark `!`.
For output channels, the output will be the first discrete value for which the range between `<min>` and `<max>` contains
the normalized channel value.

These examples mean
* For `/a/topic`, when mapped as input, the input value `5.0` will generate a normalized event value of `0.75`.
* For `/a/topic`, when mapped as output, a normalized event value `0.25` will generate an output of `-5.0`.
* For `/another/topic`, when mapped as an input, the input value `off` will generate a normalized event value of `0.0`.
* For `/another/topic`, when mapped as an output, a normalized event value of `0.75` will generate an output of `on`.

Values above the maximum or below the minimum will be clamped. The MIDIMonster will not output values out of the
configured bounds.

#### Channel specification

A channel specification may be any MQTT topic designator not containing the wildcard characters `+` and `#`.

Example mapping: 
```
mq1./midimonster/in > mq2./midimonster/out
```

#### Known bugs / problems

If the connection to a server is lost, the connection will be retried in approximately 10 seconds.
If the server rejects the connection with reason code `0x01`, a protocol failure is assumed. If the initial
connection was made with `MQTT v5.0`, it is retried with the older protocol version `MQTT v3.1.1`.

Support for TLS-secured connections is planned, but not yet implemented.
