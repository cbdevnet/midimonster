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

A channel is specified using the syntax `channel<channel>.<type><index>`. The shorthand `ch` may be used instead
of `channel`.
The earlier syntax of `<type><channel>.<index>` is officially deprecated but still supported for compatability
reasons. This support may be removed at some future time.

Channels range from `0` to `15`. Each channel consists of 128 notes (numbered `0` through `127`) and 128 CC's
(numbered likewise), a channel pressure control (also called 'channel aftertouch') and a pitch control.
Each Note also has an additional pressure value associated with it.

Example mappings:
```
midi1.ch0.note9 > midi2.channel1.cc4
midi1.channel15.cc1 > midi1.channel0.note0
```
#### Known bugs / problems

Currently, no Note Off messages are sent (instead, Note On messages with a velocity of 0 are
generated, which amount to the same thing according to the spec). This may be implemented as
a configuration option at a later time.

NRPNs are not yet fully implemented, though rudimentary support is in the codebase.

To see which events your MIDI devices output, ALSA provides the `aseqdump` utility. You can
list all incoming events using `aseqdump -p <portname>`.