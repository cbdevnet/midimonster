### The `artnet` backend

The ArtNet backend provides read-write access to the UDP-based ArtNet protocol for lighting
fixture control.

Art-Net™ Designed by and Copyright Artistic Licence Holdings Ltd.

#### Global configuration

| Option	| Example value		| Default value 	| Description		|
|---------------|-----------------------|-----------------------|-----------------------|
| `bind`	| `127.0.0.1 6454`	| none			| Binds a network address to listen for data (a socket/interface). This option may be set multiple times, with each interface being assigned an index starting from 0 to be used with the `interface` instance configuration option. At least one socket is required for operation. |
| `net`		| `0`			| `0`			| The default net to use (upper 7 bits of the 15-bit port address) |
| `detect`	| `on`, `verbose`	| `off`			| Output additional information on received data packets to help with configuring complex scenarios |

#### Instance configuration

| Option	| Example value		| Default value 	| Description		|
|---------------|-----------------------|-----------------------|-----------------------|
| `net`		| `0`			| `0`			| ArtNet `net` to use (upper 7 bits of the 15-bit port address |
| `universe`	| `0`			| `0`			| Universe identifier (lower 8 bits of the 15-bit port address) |
| `destination`	| `10.2.2.2`		| none			| Destination address for sent ArtNet frames. Setting this enables the universe for output |
| `interface`	| `1`			| `0`			| The bound address to use for data input/output |
| `realtime`	| `1`			| `0`			| Disable the recommended rate-limiting (approx. 44 packets per second) for this instance |

#### Channel specification

A channel is specified by it's universe index. Channel indices start at 1 and end at 512.

Example mapping:
```
net1.231 < net2.123
```

A 16-bit channel (spanning any two normal 8-bit channels in the same universe, also called a wide channel) may be mapped with the syntax
```
net1.1+2 > net2.5+123
```

A normal channel that is part of a wide channel can not be mapped individually.

#### Known bugs / problems

When using this backend for output with a fast event source, some events may appear to be lost due to the packet output rate limiting
mandated by the [ArtNet specification](https://artisticlicence.com/WebSiteMaster/User%20Guides/art-net.pdf) (Section `Refresh rate`).
This limit can be disabled on a per-instance basis using the `realtime` instance option.

This backend will reply to PollRequests from ArtNet controllers if binding an interface with an IPv4 address.
When binding to a wildcard address (e.g. `0.0.0.0`), the IP address reported by controllers in a `node overview` may be wrong. This can
be fixed by specifying the bind `announce` address using the syntax `bind = 0.0.0.0 6454 announce=10.0.0.1`, which will override the address
announced in the ArtPollReply.

When binding a specific IP address on Linux and OSX, no broadcast data (including ArtPoll requests) are received. There will be mechanism
to bind to a specified interface in a future release. As a workaround, bind to the wildcard interface `0.0.0.0`.

The backend itself supports IPv6, but the ArtNet spec hardcodes IPv4 address fields in some responses.
Normal input and output are well supported, while extended features such as device discovery may not work with IPv6 due to the specification ignoring the existence of anything but IPv4.
