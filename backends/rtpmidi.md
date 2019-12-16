### The `rtpmidi` backend

This backend provides read-write access to RTP MIDI streams, which transfer MIDI data
over the network.

As the specification for RTP MIDI does not normatively indicate any method
for session management, most vendors define their own standards for this.
The MIDIMonster supports the following session management methods, which are
selectable per-instance, with some methods requiring additional global configuration:

* Direct connection: The instance will send and receive data from peers configured in the
	instance configuration
* Direct connection with peer learning: The instance will send and receive data from peers
	configured in the instance configuration as well as previously unknown peers that
	voluntarily send data to the instance.
* AppleMIDI session management:

Note that instances that receive data from multiple peers will combine all inputs into one
stream, which may lead to inconsistencies during playback.

#### Global configuration

| Option	| Example value		| Default value 	| Description		|
|---------------|-----------------------|-----------------------|-----------------------|
| `detect`      | `on`                  | `off`                 | Output channel specifications for any events coming in on configured instances to help with configuration |
| `mdns-bind`	| `10.1.2.1 5353`	| `:: 5353`		| Bind host for the mDNS discovery server |
| `mdns-name`	| `computer1`		| none			| mDNS hostname to announce, also used as AppleMIDI peer name |

#### Instance configuration

Common instance configuration parameters

| Option	| Example value		| Default value 	| Description		|
|---------------|-----------------------|-----------------------|-----------------------|
| `ssrc`	| `0xDEADBEEF`		| Randomly generated	| 32-bit synchronization source identifier |
| `mode`	| `direct`		| none			| Instance session management mode (`direct` or `apple`) |
| `peer`	| `10.1.2.3 9001`	| none			| MIDI session peer, may be specified multiple times. Bypasses session discovery protocols |

`direct` mode instance configuration parameters

| Option	| Example value		| Default value 	| Description		|
|---------------|-----------------------|-----------------------|-----------------------|
| `bind`	| `10.1.2.1 9001`	| `:: <random>`		| Local network address to bind to | 
| `learn`	| `true`		| `false`		| Accept new peers for data exchange at runtime |

`apple` mode instance configuration parameters

| Option	| Example value		| Default value 	| Description		|
|---------------|-----------------------|-----------------------|-----------------------|
| `bind`	| `10.1.2.1 9001`	| `:: <random>`		| Local network address to bind to (note that AppleMIDI requires two consecutive port numbers to be allocated) |
| `session`	| `Just Jamming`	| `MIDIMonster`		| Session name to announce via mDNS |
| `invite`	| `pad`			| none			| Devices to send invitations to when discovered (the special value `*` invites all discovered peers). Setting this option makes the instance a session initiator. May be specified multiple times |
| `join`	| `Just Jamming`	| none			| Session for which to accept invitations (the special value `*` accepts all invitations). Setting this option makes the instance a session participant |
| `peer`	| `10.1.2.3 9001`	| none			| Configure a direct session peer, bypassing AppleMIDI discovery. May be specified multiple times |

Note that AppleMIDI session discovery requires mDNS functionality, thus the `mdns-name` global parameter
(and, depending on your setup, the `mdns-bind` parameter) need to be configured properly.

#### Channel specification

The `rtpmidi` backend supports mapping different MIDI events to MIDIMonster channels. The currently supported event types are

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
rmidi1.ch0.note9 > rmidi2.channel1.cc4
rmidi1.channel15.pressure1 > rmidi1.channel0.note0
rmidi1.ch1.aftertouch > rmidi2.ch2.cc0
rmidi1.ch0.pitch > rmidi2.ch1.pitch
```

#### Known bugs / problems
