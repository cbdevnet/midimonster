# The MIDIMonster

Named for it's scary math, the MIDIMonster is a universal translation
tool between multi-channel absolute-value-based control and/or bus protocols,
such as MIDI, DMX/ArtNet and OSC.

It allows the user to translate channels on one protocol into channels on another
(or the same) protocol, eg

* Translate MIDI Control Changes into Notes
* Translate MIDI Notes into ArtNet
* Translate OSC messages into MIDI
* Use an OSC app as a simple lighting controller via ArtNet
* Visualize ArtNet data using OSC servers
* Control lighting fixtures or DAWs using gamepad controllers

## Usage

The MIDImonster takes as it's first argument the name of an optional configuration file
to use (`monster.cfg` is used as default if none is specified). The configuration
file syntax is explained in the next section.

## Configuration

Each protocol supported by MIDIMonster is implemented by a *backend*, which takes
global protocol-specific options and provides *instance*s, which can be configured further.

The configuration is stored in a file with a format very similar to the common
INI file format. A section is started by a header in `[]` braces, followed by
lines of the form `option = value`.

Lines starting with a semicolon are treated as comments and ignored. Inline comments
are not currently supported.

A configuration section may either be a *backend configuration* section, started by
`[backend <backend-name>]`, an *instance configuration* section, started by
`[<backend-name> <instance-name>]` or a *mapping* section started by `[map]`.

The `[map]` section consists of lines of channel-to-channel assignments, reading like

```
instance.channel-a < instance.channel-b
instance.channel-a > instance.channel-b
instance.channel-c <> instance.channel-d
```

The first line above maps any event originating from `instance.channel-b` to be output
on `instance.channel-a` (right-to-left mapping).

The second line makes that mapping a bi-directional mapping, so both of those channels
output eachothers events.

The last line is a shorter way to create a bi-directional mapping.

An example configuration file can be found in [configs/unifest-17.cfg](configs/unifest-17.cfg).

## Backend documentation
This section documents the configuration options supported by the various backends.

### The `artnet` backend

The ArtNet backend provides read-write access to the UDP-based ArtNet protocol for lighting
fixture control.

#### Global configuration

| Option	| Example value		| Default value 	| Description		|
|---------------|-----------------------|-----------------------|-----------------------|
| `bind`	| `127.0.0.1 6454`	| none		| Binds a network address to listen for data. This option may be set multiple times, with each descriptor being assigned an index starting from 0 to be used with the `iface` instance configuration option |
| `net`		| `0`			| `0`			| The default net to use |

#### Instance configuration

| Option	| Example value		| Default value 	| Description		|
|---------------|-----------------------|-----------------------|-----------------------|
| `net`		| `0`			| `0`			| ArtNet net to use	|
| `uni`		| `0`			| `0`			| ArtNet universe to use|
| `dest`	| `10.2.2.2`		| none			| Destination address for sent ArtNet frames. Setting this enables the universe for output |
| `iface`	| `1`			| `0`			| The bound address to use for data input/output |

#### Channel specification

A channel is specified by it's universe index. Channel indices start at 1 and end at 512.

Example mapping:
```
net1.231 < net2.123
```

A 16-bit channel (spanning any two normal channels in the same universe) may be mapped with the syntax
```
net1.1+2 > net2.5+123
```

A normal channel that is part of a wide channel can not be mapped individually.

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
Run `aconnect -i` to list input ports and `aconnect -o` to list output ports.

Each instance also provides a virtual port, so MIDI devices can also be connected with `aconnect <sender> <receiver>`.

#### Channel specification

The MIDI backend supports multiple channel types

* `cc` - Control Changes
* `note` - Note On/Off messages
* `nrpn` - NRPNs (not yet implemented)

A channel is specified using `<type><channel>.<index>`.

Example mapping:
```
midi1.cc0.9 > midi2.note1.4
```
#### Known bugs / problems

Currently, no Note Off messages are sent (instead, Note On messages with a velocity of 0 are
generated, which amount to the same thing according to the spec). This may be implemented as
a configuration option at a later time.

NRPNs are not yet fully implemented, though rudimentary support is in the codebase.

The channel specification syntax is currently a bit clunky.

### The `evdev` backend

This backend allows using Linux `evdev` devices such as mouses, keyboards, gamepads and joysticks
as input and output devices. All buttons and axes available to the Linux system are mappable.
Output is provided by the `uinput` kernel module, which allows creation of virtual input devices.
This functionality may require elevated privileges (such as special group membership or root access).

#### Global configuration

This backend does not take any global configuration.

#### Instance configuration

| Option	| Example value		| Default value | Description					|
|---------------|-----------------------|---------------|-----------------------------------------------|
| `input`	| `/dev/input/event1`	| none		| `evdev` device to use as input device		|
| `exclusive`	| `1`			| `0`		| Prevent other processes from using the device	|
| `name`	| `My Input Device`	| none		| Output device presentation name. Setting this option enables the instance for output	|
| `id`		| `0x1 0x2 0x3`		| none		| Set output device bus identification (Vendor, Product and Version), optional |
| `axis.AXISNAME`| `34300 0 65536 255 4095` | none	| Specify absolute axis details (see below) for output. This is required for any absolute axis to be output.

The absolute axis details configuration is required for any absolute axis on output-enabled instances.
The configuration value contains, space-separated, the following values:

* `value`: The value to assume for the axis until an event is received
* `minimum`: The axis minimum value
* `maximum`: The axis maximum value
* `fuzz`: A value used for filtering the input stream
* `flat`: An offset, below which all deviations will be ignored
* `resolution`: Axis resolution in units per millimeter (or units per radian for rotational axes)

For real devices, all of these parameters for every axis can be found by running `evtest` on the device.

#### Channel specification

A channel is specified by its event type and event code, separated by `.`. For a complete list of event types and codes
see the kernel sources. The most interesting event types are

* `EV_KEY` for keys and buttons
* `EV_ABS` for absolute axes (such as Joysticks)
* `EV_REL` for relative axes (such as Mouses)

The `evtest` tool is useful to gather information on devices active on the local system, including types, codes
and configuration supported by these devices.

Example mapping:
```
ev1.EV_KEY.KEY_A > ev1.EV_ABS.ABS_X
```

Note that to map an absolute axis on an output-enabled instance, additional information such as the axis minimum
and maximum are required. These must be specified in the instance configuration. When only mapping the instance
as a channel input, this is not required.

#### Known bugs/problems

Creating an `evdev` output device requires elevated privileges, namely, write access to the system's
`/dev/uinput`. Usually, this is granted for users in the `input` group and the `root` user.

Input devices may synchronize logically connected event types (for example, X and Y axes) via `EV_SYN`-type
events. The MIDIMonster also generates these events after processing channel events, but may not keep the original
event grouping.

Relative axes (`EV_REL`-type events), such as generated by mouses, are currently handled in a very basic fashion,
generating only the normalized channel values of `0`, `0.5` and `1` for any input less than, equal to and greater
than `0`, respectively. As for output, only the values `-1`, `0` and `1` are generated for the same interval.

`EV_KEY` key-down events are sent for normalized channel values over `0.9`.

Extended event type values such as `EV_LED`, `EV_SND`, etc are recognized in the MIDIMonster configuration file
but may or may not work with the internal channel mapping and normalization code.

Input devices can currently only be specified by device node directly. There may be a facility to open input
devices by presentation name in the future.

### The `loopback` backend

This backend allows the user to create logical mapping channels, for example to exchange triggering
channels easier later. All events that are input are immediately output again on the same channel.

#### Global configuration

All global configuration is ignored.

#### Instance configuration

All instance configuration is ignored

#### Channel specification

A channel may have any string for a name.

Example mapping:
```
loop.foo < loop.bar123
```

#### Known bugs / problems

It is possible to configure loops using this backend. Triggering a loop
will create a deadlock, preventing any other backends from generating events.
Be careful with bidirectional channel mappings, as any input will be immediately
output to the same channel again.

### The `osc` backend

This backend offers read and write access to the Open Sound Control protocol,
spoken primarily by visual interface tools and hardware such as TouchOSC.

#### Global configuration

This backend does not take any global configuration.

#### Instance configuration

| Option	| Example value		| Default value 	| Description		|
|---------------|-----------------------|-----------------------|-----------------------|
| `root`	| `/my/osc/path`	| none			| An OSC path prefix to be prepended to all channels |
| `bind`	| `:: 8000`		| none			| The host and port to listen on |
| `dest`	| `10.11.12.13 8001`	| none			| Remote address to send OSC data to. Setting this enables the instance for output. The special value `learn` causes the MIDImonster to always reply to the address the last incoming packet came from. A different remote port for responses can be forced with the syntax `learn@<port>` |

Note that specifying an instance root speeds up matching, as packets not matching
it are ignored early in processing.

Channels that are to be output or require a value range different from the default ranges (see below)
require special configuration, as their types and limits have to be set.

This is done in the instance configuration using an assignment of the syntax

```
/local/osc/path = <format> <min> <max> <min> <max> ...
```

The OSC path to be configured must only be the local part (omitting a configured instance root).

**format** may be any sequence of valid OSC type characters. See below for a table of supported
OSC types.

For each component of the path, the minimum and maximum values must be given separated by spaces.
Components may be accessed in the mapping section as detailed in the next section.

An example configuration for transmission of an OSC message with 2 floating point components with
a range between 0.0 and 2.0 (for example, an X-Y control), would look as follows:

```
/1/xy1 = ff 0.0 2.0 0.0 2.0
```

#### Channel specification

A channel may be any valid OSC path, to which the instance root will be prepended if
set. Multi-value controls (such as X-Y pads) are supported by appending `:n` to the path,
where `n` is the parameter index, with the first (and default) one being `0`.

Example mapping:
```
osc1./1/xy1:0 > osc2./1/fader1
```

Note that any channel that is to be output will need to be set up in the instance
configuration.

#### Supported types & value ranges

OSC allows controls to have individual value ranges and supports different parameter types.
The following types are currently supported by the MIDImonster:

* **i**: 32-bit signed integer
* **f**: 32-bit IEEE floating point
* **h**: 64-bit signed integer
* **d**: 64-bit double precision floating point

For each type, there is a default value range which will be assumed if the channel is not otherwise
configured using the instance configuration. Values out of a channels range will be clipped.

The default ranges are:

* **i**: `0` to `255`
* **f**: `0.0` to `1.0`
* **h**: `0` to `1024`
* **d**: `0.0` to `1.0`

#### Known bugs / problems

Ping requests are not yet answered. There may be some problems using broadcast output and input.

## Building

This section will explain how to build the provided sources to be able to run
`midimonster`.

### Prerequisites

In order to build the MIDIMonster, you'll need some libraries that provide
support for the protocols to translate.

* libasound2-dev (for the MIDI backend)
* libevdev-dev (for the evdev backend)
* A C compiler
* GNUmake

### Build

Just running `make` in the source directory should do the trick.

## Development

The architecture is split into the `midimonster` core, handling mapping
and resource management, and the backends, which are shared objects loaded
at start time, which provide a protocol mapping to instances / channels.

The API and structures are more-or-less documented in [midimonster.h](midimonster.h),
more detailed documentation may follow.
