### The `visca` backend

The `visca` backend provides control of compatible PTZ (Pan, Tilt, Zoom) controllable cameras
via the network. The VISCA protocol has, with some variations, been implemented by multiple manufacturers
in their camera equipment. There may be some specific limits on the command set depending on the make
and model of your equipment.

This backend can connect to both UDP and TCP based camera control interfaces. On Linux, it can also control
devices attached to a serial/RS485 adapter.

#### Global configuration

The `visca` backend does not take any global configuration.

#### Instance configuration

| Option	| Example value		| Default value 	| Description							|
|---------------|-----------------------|-----------------------|---------------------------------------------------------------|
| `id`		| `5`			| `1`			| VISCA Camera address (normally 1 for network communication	|
| `connect`	| `10.10.10.1 5678`	| none			| Camera network address and port. Default connection is TCP, when optionally suffixed with the `udp` keyword, connection will be UDP |
| `device`	| `/dev/ttyUSB0 115200`	| none			| (Linux only) Device node for a serial port adapter connecting to the camera, optionally followed by the baudrate |
| `deadzone`	| `0.1`			| `0.1`			| Amount of event value variation to be ignored for relative movement commands |

#### Channel specification

Each instance exposes the following channels

* `pan`: Pan axis (absolute)
* `tilt`: Tilt axis (absolute)
* `panspeed`: Pan speed
* `tiltspeed`: Tilt speed
* `zoom`: Zoom position
* `focus`: Focus position
* `autofocus`: Switch between autofocus (events > 0.9) and manual focus drive mode
* `wb.auto`: Switch between automatic white balance mode (events > 0.9) and manual white balance mode
* `wb.red`, `wb.blue`: Red/Blue channel white balance gain values
* `home`: Return to home position
* `memory<n>`: Call memory <n> (if incoming event value is greater than 0.9)
* `store<n>`: Store current pan/tilt/zoom setup to memory <n> (if incoming event value is greater than 0.9)
* `move.left`, `move.right`, `move.up`, `move.down`: Move relative to the current position. Set speed is multiplied by the event value.
* `move.x`, `move.y`: Move relative to the current position along the specified axis. Set speed is multiplied by the event value scaled to the full range (ie. `0.0` to `0.5` moves in one direction, `0.5` to `1.0` in the other).


Example mappings:

```
control.pan > visca.pan
control.tilt > visca.tilt
control.btn1 > visca.memory1
control.stick_x > visca.move.x
control.stick_y > visca.move.y
```

#### Compatability list

| Manufacturer	| Exact model(s) tested		| Compatible models				| Result / Notes					|
|---------------|-------------------------------|-----------------------------------------------|-------------------------------------------------------|
| ValueHD	| VHD-V61			| Probably all ValueHD Visca-capable devices	| Everything works except for absolute focus control	|
| PTZOptics	| 				| Probably all of their PTZ cameras		| See ValueHD						|

#### Known bugs / problems

Value readback / Inquiry is not yet implemented. This backend currently only does output.

Some manufacturers use VISCA, but require special framing for command flow control. This may be implemented
in the future if there is sufficient interest. Some commands may not work with some manufacturer's cameras due to
different value ranges or command ordering.

Please file a ticket if you can confirm this backend working/nonworking with a new make or model
of camera so we can add it to the compatibility list!
