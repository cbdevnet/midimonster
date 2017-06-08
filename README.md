# The MIDIMonster

Named for it's scary math, the MIDIMonster is a universal translation
tool between multi-channel absolute-value-based control and/or bus protocols,
such as MIDI, DMX/ArtNet and OSC.

It allows the user to translate channels on one protocol into channels on another
(or the same) protocol, eg

* Translate MIDI Control Changes into Notes
* Translate MIDI Notes into ArtNet
* Translate OSC messages into MIDI

## Configuration

Each protocol supported by MIDIMonster is implemented by a *backend*, which takes
global protocol-specific options and provides *instance*s, which can be configured further.

The configuration is stored in a file with a format very similar to the common
INI file format. A section is started by a header in `[]` braces, followed by
lines of the form `option = value`.

A section may either be a *backend configuration* section, started by `[backend <backend-name>]`,
an *instance configuration* section, started by `[<backend-name> <instance-name>]` or a *mapping*
section started by `[map]`.

The `[map]` section consists of lines of channel-to-channel assignments, reading like

```
instance.target-channel = instance.source-channel
```

Assignments are one-way only, so to create a bi-directional mapping two assignments are needed.
An example configuration can be found in [monster.cfg](monster.cfg).

The options accepted by the implemented backends are documented in the next section.

### The `artnet` backend

The ArtNet backend provides read-write access to the UDP-based ArtNet protocol for lighting
fixture control.

#### Global configuration

| Option	| Example value		| Default value 	| Description		|
|---------------|-----------------------|-----------------------|-----------------------|
| `bind`	| `127.0.0.1 6454`	| *none*		| What address and port to bind the ArtNet socket to |
| `net`		| `0`			| `0`			| The default net to use |

#### Instance configuration

| Option	| Example value		| Default value 	| Description		|
|---------------|-----------------------|-----------------------|-----------------------|
| `net`		| `0`			| `0`			| ArtNet net to use	|
| `uni`		| `0`			| `0`			| ArtNet universe to use|
| `output`	| `true`		| `false`		| Controls whether ArtNet frames for this universe are output (otherwise the universe is input-only) |
| `dest`	| `10.2.2.2`		| `255.255.255.255`	| Destination address for sent ArtNet frames |

#### Channel specification

A channel is specified by it's universe index. Channel indices start at 1 and end at 512.

Example mapping:
```
net1.231 = net2.123
```

#### Known bugs / problems

Currently, no keep-alive frames are sent and the minimum inter-frame-time is disregarded.

### The `midi` backend

The MIDI backend provides read-write access to the MIDI protocol via virtual ports.

#### Global configuration

| Option	| Example value		| Default value 	| Description		|
|---------------|-----------------------|-----------------------|-----------------------|
| `name`	| `MIDIMonster`		| none			| MIDI client name	|

#### Instance configuration

| Option	| Example value		| Default value 	| Description		|
|---------------|-----------------------|-----------------------|-----------------------|
| `read`	| `20:0`		| none			| MIDI device to connect for input |
| `write`	| `DeviceName`		| none			| MIDI device to connect for output |

MIDI device names may either be `client:port` portnames or prefixes of MIDI device names.

#### Channel specification

The MIDI backend supports multiple channel types

* `cc` - Control Changes
* `note` - Note On/Off messages
* `nrpn` - NRPNs (not yet implemented)

A channel is specified using `<type><channel>.<index>`.

Example mapping:
```
midi1.cc0.9 = midi2.note1.4
```
#### Known bugs / problems

Currently, no Note Off messages are sent (instead, Note On messages with a velocity of 0 are
generated, which amount to the same thing according to the spec). This may be implemented as
a configuration option at a later time.

NRPNs are not yet fully implemented, though rudimentary support is in the codebase.

The channel specification syntax is currently a bit clunky.

### The `osc` backend

TBD

## Building

This section will explain how to build the provided sources to be able to run
`midimonster`.

### Prerequisites

In order to build the MIDIMonster, you'll need some libraries that provide
support for the protocols to translate.

* libasound2-dev (for the MIDI backend)
* A C compiler
* GNUmake

### Build

Just running `make` in the source directory should do the trick.

## Development

The architecture is split into the `midimonster` core, handling mapping
and resource management, and the backends, which are shared objects loaded
at start time, which provide a protocol mapping to instances / channels.

The API and structures is more-or-less documented in [midimonster.h](midimonster.h),
more detailed documentation may follow.
