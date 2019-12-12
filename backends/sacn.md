### The `sacn` backend

The sACN backend provides read-write access to the Multicast-UDP based streaming ACN protocol (ANSI E1.31-2016),
used for lighting fixture control. The backend sends universe discovery frames approximately every 10 seconds,
containing all write-enabled universes.

#### Global configuration

| Option	| Example value		| Default value 	| Description		|
|---------------|-----------------------|-----------------------|-----------------------|
| `name`	| `sACN source`		| `MIDIMonster`		| sACN source name	|
| `cid`		| `0xAA 0xBB 0xCC` ...	| `MIDIMonster`		| Source CID (16 bytes)	|
| `bind`	| `0.0.0.0 5568`	| none			| Binds a network address to listen for data. This option may be set multiple times, with each descriptor being assigned an index starting from 0 to be used with the `interface` instance configuration option. At least one descriptor is required for operation. |

The `bind` configuration value can be extended by the keyword `local` to allow software on the
local host to process the sACN output frames from the MIDIMonster (e.g. `bind = 0.0.0.0 5568 local`).
This has the side effect of mirroring the output of instances on those descriptors to their input.

#### Instance configuration

| Option	| Example value		| Default value 	| Description		|
|---------------|-----------------------|-----------------------|-----------------------|
| `universe`	| `1`			| none			| Universe identifier between 1 and 63999 |
| `interface`	| `1`			| `0`			| The bound address to use for data input/output |
| `priority`	| `100`			| none			| The data priority to transmit for this instance. Setting this option enables the instance for output and includes it in the universe discovery report. |
| `destination`	| `10.2.2.2`		| Universe multicast	| Destination address for unicast output. If unset, the multicast destination for the specified universe is used. |
| `from`	| `0xAA 0xBB` ...	| none			| 16-byte input source CID filter. Setting this option filters the input stream for this universe. |
| `unicast`	| `1`			| `0`			| Prevent this instance from joining its universe multicast group |

Note that instances accepting multicast input also process unicast frames directed at them, while
instances in `unicast` mode will not receive multicast frames.

#### Channel specification

A channel is specified by it's universe index. Channel indices start at 1 and end at 512.

Example mapping:
```
sacn1.231 < sacn2.123
```

A 16-bit channel (spanning any two normal 8-bit channels in the same universe, also called a wide channel) may be mapped with the syntax
```
sacn.1+2 > sacn2.5+123
```

A normal channel that is part of a wide channel can not be mapped individually.

#### Known bugs / problems

The DMX start code of transmitted and received universes is fixed as `0`.

The (upper) limit on packet transmission rate mandated by section 6.6.1 of the sACN specification is disregarded.
The rate of packet transmission is influenced by the rate of incoming mapped events on the instance.

Universe synchronization is currently not supported, though this feature may be implemented in the future.

To use multicast input, all networking hardware in the path must support the IGMPv2 protocol.

The Linux kernel limits the number of multicast groups an interface may join to 20. An instance configured
for input automatically joins the multicast group for its universe, unless configured in `unicast` mode.
This limit can be raised by changing the kernel option in `/proc/sys/net/ipv4/igmp_max_memberships`.
