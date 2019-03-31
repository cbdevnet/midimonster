### The `loopback` backend

This backend allows the user to create logical mapping channels, for example to exchange triggering
channels easier later. All events that are input are immediately output again on the same channel.

#### Global configuration

All global configuration is ignored.

#### Instance configuration

All instance configuration is ignored

#### Channel specification

A channel may have any string for a name.

Example mapping:
```
loop.foo < loop.bar123
```

#### Known bugs / problems

It is possible (and very easy) to configure loops using this backend. Triggering a loop
will create a deadlock, preventing any other backends from generating events.
Be careful with bidirectional channel mappings, as any input will be immediately
output to the same channel again.