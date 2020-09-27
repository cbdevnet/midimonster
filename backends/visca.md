### The `visca` backend

The `visca` backend provides control of compatible PTZ (Pan, Tilt, Zoom) controllable cameras
via the network. The VISCA protocol has, with some variations, been implemented by multiple manufacturers
in their camera equipment. There may be some specific limits on the command set depending on the make
and model of your equipment.

This backend can connect to both UDP and TCP based camera control interfaces.

#### Global configuration

The `visca` backend does not take any global configuration.

#### Instance configuration

| Option	| Example value		| Default value 	| Description							|
|---------------|-----------------------|-----------------------|---------------------------------------------------------------|
| `id`		| `5`			| `1`			| VISCA Camera address (normally 1 for network communication	|
| `connect`	| `10.10.10.1 5678`	| none			| Camera network address and port. Default connection is TCP, when optionally suffixed with the `udp` keyword, connection will be UDP |
| `device`	| `/dev/ttyUSB0`	| none			| (Linux only) Device node for a serial port adapter connecting to the camera |

#### Channel specification

Each instance exposes the following channels

* `pan`: Pan axis
* `tilt`: Tilt axis
* `panspeed`: Pan speed
* `tiltspeed`: Tilt speed
* `zoom`: Zoom position
* `focus`: Focus position
* `memory<n>`: Call memory <n> (if incoming event value is greater than 0.9)

Example mappings:

```
control.pan > visca.pan
control.tilt > visca.tilt
control.btn1 > visca.memory1
```

#### Known bugs / problems

Value readback / Inquiry is not yet implemented. This backend currently only does output.

Some manufacturers use VISCA, but require special framing for command flow control. This may be implemented
in the future if there is sufficient interest.

Please file a ticket if you can confirm this backend working/nonworking with a new make or model
of camera so we can add it to the compatability list!
