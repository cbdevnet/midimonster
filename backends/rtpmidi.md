### The `rtpmidi` backend

This backend provides read-write access to RTP MIDI streams, which transfer MIDI data
over the network. Notably, RTP MIDI has native support in Apple devices including their
tablets.

As the specification for RTP MIDI does not normatively indicate any method
for session management, most vendors define their own standards for this.
The MIDIMonster supports the following session management methods, which are
selectable per-instance, with some methods requiring additional global configuration:

* Direct connection: The instance will send and receive data from peers configured in the
	instance configuration
* Direct connection with peer learning: The instance will send and receive data from peers
	configured in the instance configuration as well as previously unknown peers that
	voluntarily send data to the instance.
* AppleMIDI session management: The instance will be able to communicate (either as participant
	or initiator) in an AppleMIDI session, which will be announced via mDNS (better
	known as "Bonjour" to Apple users) if possible.

Note that instances that receive data from multiple peers will combine all inputs into one
stream, which may lead to inconsistencies during playback.

#### Global configuration

| Option		| Example value		| Default value 	| Description		|
|-----------------------|-----------------------|-----------------------|-----------------------|
| `detect`      	| `on`                  | `off`                 | Output channel specifications for any events coming in on configured instances to help with configuration. |
| `mdns-name`		| `computer1`		| none			| mDNS hostname to announce (`<mdns-name>.local`). Apple-mode instances will be announced via mDNS if set. |
| `mdns-interface` 	| `wlan0`		| none			| Limit addresses announced via mDNS to this interface. On Windows, this is prefix-matched against the user-editable "friendly" interface name. If this name matches an interface exactly, discovery uses exactly this device. |

#### Instance configuration

Common instance configuration parameters

| Option	| Example value		| Default value 	| Description		|
|---------------|-----------------------|-----------------------|-----------------------|
| `ssrc`	| `0xDEADBEEF`		| Randomly generated	| 32-bit synchronization source identifier |
| `mode`	| `direct`		| none			| Instance session management mode (`direct` or `apple`) |
| `peer`	| `10.1.2.3 9001`	| none			| MIDI session peer, may be specified multiple times. Bypasses session discovery (but still performs session negotiation) |

`direct` mode instance configuration parameters

| Option	| Example value		| Default value 	| Description		|
|---------------|-----------------------|-----------------------|-----------------------|
| `bind`	| `10.1.2.1 9001`	| `:: <random>`		| Local network address to bind to | 
| `learn`	| `true`		| `false`		| Accept new peers for data exchange at runtime |

`apple` mode instance configuration parameters

| Option	| Example value		| Default value 	| Description		|
|---------------|-----------------------|-----------------------|-----------------------|
| `bind`	| `10.1.2.1 9001`	| `:: <random>`		| Local network address to bind to (note that AppleMIDI requires two consecutive port numbers to be allocated). |
| `invite`	| `pad`			| none			| Devices to send invitations to when discovered (the special value `*` invites all discovered peers). May be specified multiple times. |
| `join`	| `Just Jamming`	| none			| Session for which to accept invitations (the special value `*` accepts the first invitation seen). |

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

This backend has been in development for a long time due to its complexity. There may still be bugs hidden in there.
Critical feedback and tests across multiple devices are very welcome.

The mDNS and DNS-SD implementations in this backend are extremely terse, to the point of violating the
specifications in multiple cases. Due to the complexity involved in supporting these protocols, problems
arising from this will be considered a bug only in cases where they hinder normal operation of the backend.

mDNS discovery may announce flawed records when run on a host with multiple active interfaces.

While this backend should be reasonably stable, there may be problematic edge cases simply due to the
enormous size and scope of the protocols and implementations required to make this work.
