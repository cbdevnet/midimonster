### The `ola` backend

This backend connects the MIDIMonster to the Open Lighting Architecture daemon. This can be useful
to take advantage of additional protocols implemented in OLA. This backend is currently marked as
optional and is only built with `make full` in the `backends/` directory, as the OLA is a large
dependency to require for all users.

#### Global configuration

This backend does not take any global configuration.

#### Instance configuration

| Option	| Example value		| Default value | Description						|
|---------------|-----------------------|---------------|-------------------------------------------------------|
| `universe`	| `7`			| `0`		| OLA universe to send/receive data on			|

#### Channel specification

A channel is specified by it's universe index. Channel indices start at 1 and end at 512.

Example mapping:
```
ola1.231 < in2.123
```

A 16-bit channel (spanning any two normal 8-bit channels in the same universe, also called a wide channel) may be mapped with the syntax
```
ola1.1+2 > net2.5+123
```

A normal channel that is part of a wide channel can not be mapped individually.

#### Known bugs / problems

The backend currently assumes that the OLA daemon is running on the same host as the MIDIMonster.
This may be made configurable in the future.

This backend requires `libola-dev` to be installed, which pulls in a rather large and aggressive (in terms of probing
and taking over connected hardware) daemon. It is thus marked as optional and only built when executing the `full` target
within the `backends` directory.