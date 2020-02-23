### The `openpixelcontrol` backend

This backend provides read-write access to the TCP-based OpenPixelControl protocol,
used for controlling intelligent RGB led strips.

This backend can both control a remote OpenPixelControl server as well as receive data
from OpenPixelControl clients.

#### Global configuration

This backend does not take any global configuration.

#### Instance configuration

| Option	| Example value		| Default value 	| Description		|
|---------------|-----------------------|-----------------------|-----------------------|
| `destination`	| `10.11.12.1 9001`	| none			| Destination for output data. Setting this option enables the instance for output |
| `listen`	| `10.11.12.2 9002`	| none			| Local address to wait for client connections on. Setting this enables the instance for input |
| `mode`	| `16bit`		| `8bit`		| RGB channel resolution |

#### Channel specification

Each instance can control up to 255 strips of RGB LED lights. The OpenPixelControl specification
confusingly calls these strips "channels".

Strip `0` acts as a "broadcast" strip, setting values on all other strips at once.
Consequently, components on strip 0 can only be mapped as output channels to a destination
(setting components on all strips there), not as input channels. When such messages are received from
a client, the corresponding mapped component channels on all strips will receive events.

Every single component of any LED on any string can be mapped as an individual MIDIMonster channel.
The components are laid out as sequences of Red - Green - Blue value triplets.

Channels can be specified by their sequential index (one-based).

Example mapping (data from Strip 2 LED 66's green component is mapped to the blue component of LED 2 on strip 1):
```
strip1.channel6 < strip2.channel200
```

Additionally, channels may be referred to by their color component and LED index:
```
strip1.blue2 < strip2.green66
```

#### Known bugs / problems

If the connection is lost, it is currently not reestablished and may cause exit the MIDIMonster entirely.
Thisi behaviour may be changed in future releases.
