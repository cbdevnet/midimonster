### The `winmidi` backend

This backend provides read-write access to the MIDI protocol via the Windows Multimedia API.

It is only available when building for Windows. Care has been taken to keep the configuration
syntax similar to the `midi` backend, but due to differences in the internal programming interfaces,
some deviations may still be present.

#### Global configuration

| Option	| Example value		| Default value 	| Description		|
|---------------|-----------------------|-----------------------|-----------------------|
| `list`	| `on`                  | `off`                 | List available input/output devices on startup |
| `detect`      | `on`                  | `off`                 | Output channel specifications for any events coming in on configured instances to help with configuration. |

#### Instance configuration

| Option		| Example value		| Default value 	| Description		|
|-----------------------|-----------------------|-----------------------|-----------------------|
| `read` / `source`	| `2`			| none			| MIDI device to connect for input |
| `write` / `target`	| `DeviceName`		| none			| MIDI device to connect for output |
| `epn-tx`		| `short`		| `full`		| Configure whether to clear the active parameter number after transmitting an `nrpn` or `rpn` parameter. |

Input/output device names may either be prefixes of MIDI device names or numeric indices corresponding
to the listing shown at startup when using the global `list` option.

#### Channel specification

The `winmidi` backend supports mapping different MIDI events as MIDIMonster channels. The currently supported event types are

* `cc` - Control Changes
* `note` - Note On/Off messages (also known as note velocity)
* `pressure` - Note pressure/aftertouch messages
* `aftertouch` - Channel-wide aftertouch messages
* `pitch` - Channel pitchbend messages
* `program` - Channel program change messages
* `rpn` - Registered parameter numbers (14-bit extension)
* `nrpn` - Non-registered parameter numbers (14-bit extension)

A MIDIMonster channel is specified using the syntax `channel<channel>.<type><index>`. The shorthand `ch` may be
used instead of the word `channel` (Note that `channel` here refers to the MIDI channel number).

The `pitch`, `aftertouch` and `program` messages/events are channel-wide, thus they can be specified as `channel<channel>.<type>`.

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
midi2.ch0.nrpn900 > midi1.ch1.rpn1
midi2.ch15.note1 > midi1.ch2.program
```

#### Known bugs / problems

Extended parameter numbers (EPNs, the `rpn` and `nrpn` control types) will also generate events on the controls (CC 101 through
98, 38 and 6) that are used as the lower layer transport. When using EPNs, mapping those controls is probably not useful.

EPN control types support only the full 14-bit transfer encoding, not the shorter variant transmitting only the 7
high-order bits. This may be changed if there is sufficient interest in the functionality.

Currently, no Note Off messages are sent (instead, Note On messages with a velocity of 0 are
generated, which amount to the same thing according to the spec). This may be implemented as
a configuration option at a later time.

As this is a Windows-only backend, testing may not be as frequent or thorough as for the Linux / multiplatform
backends.
