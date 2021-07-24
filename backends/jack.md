### The `jack` backend

This backend provides read-write access to the JACK Audio Connection Kit low-latency audio transport server for the
transport of control data via either JACK midi ports or control voltage (CV) inputs and outputs.

#### Global configuration

| Option	| Example value		| Default value 	| Description		|
|---------------|-----------------------|-----------------------|-----------------------|
| `debug`	| `on`			| `off`			| Print `info` level notices from the JACK connection	|
| `errors`      | `on`                  | `off`                 | Print `error` level notices from the JACK connection	|

#### Instance configuration

| Option	| Example value		| Default value 	| Description		|
|---------------|-----------------------|-----------------------|-----------------------|
| `name`	| `Controller`		| `MIDIMonster`		| Client name for the JACK connection |
| `server`	| `jackserver`		| `default`		| JACK server identifier to connect to |
| `epn-tx`	| `short`		| `full`		| Configure whether to clear the active parameter number after transmitting a MIDI `nrpn` or `rpn` parameter. |

Channels (corresponding to JACK ports) need to be configured with their type and, if applicable, value limits.
To configure a port, specify it in the instance configuration using the following syntax:

```
port_name = <type> <direction> min <minimum> max <maximum>
```

Port names may be any string except for the instance configuration keywords `name` and `server`.

The following `type` values are currently supported:

* `midi`: JACK MIDI port for transmitting MIDI event messages
* `cv`: JACK audio port for transmitting DC offset "control voltage" samples (requires `min`/`max` configuration)

`direction` may be one of `in` or `out`, as seen from the perspective of the MIDIMonster core, thus
`in` means data is being read from the JACK server and `out` transfers data into the JACK server.

The following example instance configuration would create a MIDI port sending data into JACK, a control voltage output
sending data between `-1` and `1`, and a control voltage input receiving data with values between `0` and `10`.

```
midi_out = midi out
cv_out = cv out min -1 max 1
cv_in = cv in min 0.0 max 10.0
```

Input CV samples outside the configured range will be clipped. The MIDIMonster will not generate output CV samples
outside of the configured range.

#### Channel specification

CV ports are exposed as single MIDIMonster channel and directly map to their normalised values.

MIDI ports provide subchannels for the various MIDI controls available. Each MIDI port carries
16 MIDI channels (numbered 0 through 15), each of which has 128 note controls (numbered 0 through 127),
corresponding pressure controls for each note, 128 control change (CC) controls (numbered likewise),
one channel wide "aftertouch" control and one channel-wide pitchbend control.

Every MIDI channel also provides `rpn` and `nrpn` controls, which are implemented on top of the MIDI protocol, using
the CC controls 101/100/99/98/38/6. Both control types have 14-bit IDs and 14-bit values.

A MIDI port subchannel is specified using the syntax `channel<channel>.<type><index>`. The shorthand `ch` may be
used instead of the word `channel` (Note that `channel` here refers to the MIDI channel number).

The following values are recognized for `type`:

* `cc` - Control Changes
* `note` - Note On/Off messages (also known as note velocity)
* `pressure` - Note pressure/aftertouch messages
* `aftertouch` - Channel-wide aftertouch messages
* `pitch` - Channel pitchbend messages
* `program` - Channel program change messages
* `rpn` - Registered parameter numbers (14-bit extension)
* `nrpn` - Non-registered parameter numbers (14-bit extension)

The `pitch`, `aftertouch` and `program` messages/events are channel-wide, thus they can be specified as `channel<channel>.<type>`.

Example mappings:
```
jack1.cv_in > jack1.midi_out.ch0.note3
jack1.midi_in.ch0.pitch > jack1.cv_out
jack2.midi_in.ch0.nrpn900 > jack1.midi_out.ch1.rpn1
jack1.midi_in.ch15.note1 > jack1.midi_out.ch4.program
```

The MIDI subchannel syntax is intentionally kept compatible to the different MIDI backends also supported
by the MIDIMonster

#### Known bugs / problems

MIDI extended parameter numbers (EPNs, the `rpn` and `nrpn` control types) will also generate events on the controls (CC 101 through
98, 38 and 6) that are used as the lower layer transport. When using EPNs, mapping those controls is probably not useful.

EPN control types support only the full 14-bit transfer encoding, not the shorter variant transmitting only the 7
high-order bits. This may be changed if there is sufficient interest in the functionality.

While JACK has rudimentary capabilities for transporting OSC messages, configuring and parsing such channels
with this backend would take a great amount of dedicated syntax & code. CV ports can provide fine-grained single
control channels as an alternative to MIDI. This feature may be implemented at some point in the future.
