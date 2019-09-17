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

| Option	| Example value		| Default value 	| Description		|
|---------------|-----------------------|-----------------------|-----------------------|
| `read`	| `2`			| none			| MIDI device to connect for input |
| `write`	| `DeviceName`		| none			| MIDI device to connect for output |

Input/output device names may either be prefixes of MIDI device names or numeric indices corresponding
to the listing shown at startup when using the global `list` option.

#### Channel specification

The `winmidi` backend supports mapping different MIDI events as MIDIMonster channels. The currently supported event types are

* `cc` - Control Changes
* `note` - Note On/Off messages
* `pressure` - Note pressure/aftertouch messages
* `aftertouch` - Channel-wide aftertouch messages
* `pitch` - Channel pitchbend messages

A MIDIMonster channel is specified using the syntax `channel<channel>.<type><index>`. The shorthand `ch` may be
used instead of the word `channel` (Note that `channel` here refers to the MIDI channel number).

The `pitch` and `aftertouch` events are channel-wide, thus they can be specified as `channel<channel>.<type>`.

MIDI channels range from `0` to `15`. Each MIDI channel consists of 128 notes (numbered `0` through `127`), which
additionally each have a pressure control, 128 CC's (numbered likewise), a channel pressure control (also called
'channel aftertouch') and a pitch control which may all be mapped to individual MIDIMonster channels.

Example mappings:
```
midi1.ch0.note9 > midi2.channel1.cc4
midi1.channel15.pressure1 > midi1.channel0.note0
midi1.ch1.aftertouch > midi2.ch2.cc0
midi1.ch0.pitch > midi2.ch1.pitch
```

#### Known bugs / problems

Currently, no Note Off messages are sent (instead, Note On messages with a velocity of 0 are
generated, which amount to the same thing according to the spec). This may be implemented as
a configuration option at a later time.

As this is a Windows-only backend, testing may not be as frequent or thorough as for the Linux / multiplatform
backends.
