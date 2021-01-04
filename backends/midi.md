### The `midi` backend

The MIDI backend provides read-write access to the MIDI protocol via virtual ports.

#### Global configuration

| Option	| Example value		| Default value 	| Description		|
|---------------|-----------------------|-----------------------|-----------------------|
| `name`	| `MIDIMonster`		| none			| MIDI client name	|
| `detect`      | `on`                  | `off`                 | Output channel specifications for any events coming in on configured instances to help with configuration. |

#### Instance configuration

| Option	| Example value		| Default value 	| Description		|
|---------------|-----------------------|-----------------------|-----------------------|
| `read`	| `20:0`		| none			| MIDI device to connect for input |
| `write`	| `DeviceName`		| none			| MIDI device to connect for output |
| `epn-tx`	| `short`		| `full`		| Configures whether to clear the active parameter number after transmitting an `nrpn` or `rpn` parameter |

MIDI device names may either be `client:port` portnames or prefixes of MIDI device names.
Run `aconnect -i` to list input ports and `aconnect -o` to list output ports.

Each instance also provides a virtual port, so MIDI devices can also be connected with `aconnect <sender> <receiver>`.

#### Channel specification

The MIDI backend supports mapping different MIDI events to MIDIMonster channels. The currently supported event types are

* `cc` - Control Changes
* `note` - Note On/Off messages
* `pressure` - Note pressure/aftertouch messages
* `aftertouch` - Channel-wide aftertouch messages
* `pitch` - Channel pitchbend messages
* `rpn` - Registered parameter numbers (14-bit extension)
* `nrpn` - Non-registered parameter numbers (14-bit extension)

A MIDIMonster channel is specified using the syntax `channel<channel>.<type><index>`. The shorthand `ch` may be
used instead of the word `channel` (Note that `channel` here refers to the MIDI channel number).

The `pitch` and `aftertouch` events are channel-wide, thus they can be specified as `channel<channel>.<type>`.

MIDI channels range from `0` to `15`. Each MIDI channel consists of 128 notes (numbered `0` through `127`), which
additionally each have a pressure control, 128 CC's (numbered likewise), a channel pressure control (also called
'channel aftertouch') and a pitch control which may all be mapped to individual MIDIMonster channels.

Every MIDI channel also provides `rpn` and `nrpn` controls, which are implemented on top of the MIDI protocol, using
the CC controls 101/100/99/98/38/6. Both control types have 14-bit IDs and 14-bit values.

Example mappings:
```
midi1.ch0.note9 > midi2.channel1.cc4
midi1.channel15.pressure1 > midi1.channel0.note0
midi1.ch1.aftertouch > midi2.ch2.cc0
midi1.ch0.pitch > midi2.ch1.pitch
midi1.ch0.nrpn900 > midi2.ch0.rpn1
```
#### Known bugs / problems

Extended parameter numbers (EPNs, the `rpn` and `nrpn` control types) will also generate events on the controls (CC 101 through
98, 38 and 6) that are used as the lower layer transport. When using EPNs, mapping those controls is probably not useful.

EPN control types support only the full 14-bit transfer encoding, not the shorter variant transmitting only the 7
high-order bits. This may be changed if there is sufficient interest in the functionality.

To access MIDI data, the user running MIDIMonster needs read & write access to the ALSA sequencer.
This can usually be done by adding this user to the `audio` system group.

Currently, no Note Off messages are sent (instead, Note On messages with a velocity of 0 are
generated, which amount to the same thing according to the spec). This may be implemented as
a configuration option at a later time.

To see which events your MIDI devices output, ALSA provides the `aseqdump` utility. You can
list all incoming events using `aseqdump -p <portname>`.
