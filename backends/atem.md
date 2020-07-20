### The `atem` backend

This backend connects to Blackmagic Design ATEM Video Production Switchers via the network to allow
direct control input and feedback. This enables creative avenues of controlling these devices, as well
as allowing complex workflows to be automated in new ways.

This backend is currently a work in progress.

#### Global configuration

The `atem` backend does not take any global configuration.

#### Instance configuration

| Option	| Example value		| Default value		| Description			|
|---------------|-----------------------|-----------------------|-------------------------------|
| `host`	| `10.23.23.99`		| none			| Host address of the switcher.	|

#### Channel specification

ATEM switchers offer a wide array of functionality. To break these functionalities down into manageable
logical sections and ultimately map them to MIDIMonster channels, they have been mapped to feature "subsystems".

Some of the larger models support multiple mix effects (M/Es). All MIDIMonster channels of the `atem` backend
can be prefixed with `me<n>.` to apply to a specific M/E (where that is supported by the command).
If omitted, M/E 1 is assumed as default target.

##### The `input` subsystem

This subsystems deals with assigning inputs (as well as internal buses and sources) to sinks throughout
the switcher, such as the preview and program buses as well as the keyer inputs.

Channel specifications follow the syntax `input.<inputname>.<destination>`. If `<destination>` is omitted, `preview`
is assumed as target. Commands are sent for every incoming event with a normalised value greater than `0.9`.

`<input>` may be any of the following source specifiers:

* `black`: Black color source
* `bars`: Test bars
* `color<n>`: Color generator `n`
* `in<n>`: External input `n`
* `mp<n>`: Media player `n`
* `mpkey<n>`: Media player `n` key output

`<destination>` may be any of the following sink specifiers:

* `preview`: Preview bus
* `program`: Program bus
* `usk<n>`: Upstream keyer `n` key source
* `uskfill<n>`: Upstream keyer `n` fill source
* `dsk<n>`: Downstream keyer `n` key source
* `dskfill<n>`: Downstream keyer `n` fill source
* `aux<n>`: Auxiliary output `n`

For external inputs and internal sources, the output to the MIDIMonster core from these channels is the
tally for the respective source.

On the ATEM Mini Pro, the switchable "HDMI Out" port is internally acessed as AUX1.

Example mappings:
```
control.select > atem1.me1.input.in1.program
control.select > atem1.me2.input.in1.program
atem1.input.in1.program > control.pgmtally1
atem1.input.mp1.preview > control.pvwtally5
control.select > atem1.input.in2.keyfill1
```

##### The `mediaplayer` subsystem

TBD

##### The `dsk` (downstream keyer) subsystem

TBD

##### The `usk` (upstream keyer) subsystem

TBD

##### The `colorgen` subsystem

This subsystem exposes control of the color generators using the channel specification syntax
`colorgen<n>.<parameter>`.

`<parameter>` may be one of the following:

* `hue`
* `saturation`
* `luminance`

The channels output the current values of the parameters they control back to the core.

Example mappings:
```
control.hue > atem1.colorgen1.hue
control.sat > atem1.colorgen2.saturation
control.luma > atem1.colorgen1.luminance
```

##### The `playout` subsystem

TBD

##### The `transition` subsystem

This subsystem allows control over transitions between selected sources. Commands are sent for every incoming
event with a normalised value greater than `0.9`, except for the T-Bar control which is a continuous value.

* `auto`: Start/stop an automatic transition from preview to program
* `cut`: Perform hard cut from preview to program
* `ftb`: Fade program to/from black
* `tbar`: Control transition progress

The `auto` channel outputs `1.0` to the MIDIMonster core while an automatic transition is running.
Automatic transitions also move the T-Bar, which will output the current position.

Example mappings:
```
control.cut > atem1.me2.transition.cut
control.ftb > atem1.transition.ftb
control.auto > atem1.transition.auto
control.tbar > atem1.me1.transition.tbar
```

#### Known bugs / problems

In the protocol, all transitions run from a value of `0` to a value of `10000`. The current T-Bar position
is reset to `0` after each completed transition. This may cause problems when mapping the T-Bar control to
a rotary control or non-motorized fader. A workaround for this is in consideration.

ATEM devices support device discovery via mDNS. This may be implemented in the future to automatically
detect and connect to switchers available on the local network.

You can do some things with this backend that the ATEM control application prevents you from doing.
It is beyond the knowledge of the authors if doing these things is dangerous in any way or just seen as unnecessary.
Either way, you are using this backend at your own discretion and risk.

The protocol has been reverse engineered by using an ATEM Mini Pro with protocol version `2.30` (ATEM Switchers
software update 8.3). Due to those circumstances, parts of this implementation may be or become wrong based on
incomplete understanding or future changes to the protocol. Some devices may not support all implemented features,
while some may support features that are not yet implemented.

Some features available in larger models may not be present in the device this backend was originally
developed against. If you have access to more powerful ATEM hardware and would like to see this backend's
protocol support extended for them, please contact the developers.
