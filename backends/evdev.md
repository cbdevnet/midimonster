### The `evdev` backend

This backend allows using Linux `evdev` devices such as mouses, keyboards, gamepads and joysticks
as input and output devices. All buttons and axes available to the Linux system are mappable.
Output is provided by the `uinput` kernel module, which allows creation of virtual input devices.
This functionality may require elevated privileges (such as special group membership or root access).

#### Global configuration

| Option        | Example value         | Default value         | Description           |
|---------------|-----------------------|-----------------------|-----------------------|
| `detect`      | `on`                  | `off`                 | Output channel specifications for any events coming in on configured instances to help with configuration. |

#### Instance configuration

| Option	| Example value		| Default value | Description						|
|---------------|-----------------------|---------------|-------------------------------------------------------|
| `device`	| `/dev/input/event1`	| none		| `evdev` device to use as input device			|
| `input`	| `Xbox Wireless`	| none		| Presentation name of evdev device to use as input (most-specific prefix matched), can be used instead of the `device` option |
| `output`	| `My Input Device`	| none		| Output device presentation name. Setting this option enables the instance for output	|
| `exclusive`	| `1`			| `0`		| Prevent other processes from using the device		|
| `id`		| `0x1 0x2 0x3`		| none		| Set output device bus identification (Vendor, Product and Version), optional |
| `axis.AXISNAME`| `34300 0 65536 255 4095` | none	| Specify absolute axis details (see below) for output. This is required for any absolute axis to be output. | 
| `relaxis.AXISNAME`| `65534 32767` | none	| Specify relative axis details (extent and optional initial value) for output and input (see below). |

The absolute axis details configuration (e.g. `axis.ABS_X`) is required for any absolute axis on output-enabled
instances. The configuration value contains, space-separated, the following values:

* `value`: The value to assume for the axis until an event is received
* `minimum`: The axis minimum value
* `maximum`: The axis maximum value
* `fuzz`: A value used for filtering the input stream
* `flat`: An offset, below which all deviations will be ignored
* `resolution`: Axis resolution in units per millimeter (or units per radian for rotational axes)

If an axis is not used for output, this configuration can be omitted.

For real devices, all of these parameters for every axis can be found by running `evtest` on the device.

To use the input from relative axes in absolute-value based protocols, the backend needs a reference frame to
convert the relative movements to absolute values. To invert the mapping of the relative axis, specify the `max` value
as a negative number, for example:

```
relaxis.REL_X = -1024 512
```

If relative axes are used without specifying their extents, the channel will generate normalized values
of `0`, `0.5` and `1` for any input less than, equal to and greater than `0`, respectively. As for output, only
the values `-1`, `0` and `1` are generated for the same interval.

#### Channel specification

A channel is specified by its event type and event code, separated by `.`. For a complete list of event types and codes
see the [kernel documentation](https://www.kernel.org/doc/html/v4.12/input/event-codes.html). The most interesting event types are

* `EV_KEY` for keys and buttons
* `EV_ABS` for absolute axes (such as Joysticks)
* `EV_REL` for relative axes (such as Mouses)

The `evtest` tool is useful to gather information on devices active on the local system, including names, types, codes
and configuration supported by these devices.

Example mapping:
```
ev1.EV_KEY.KEY_A > ev1.EV_ABS.ABS_X
```

Note that to map an absolute axis on an output-enabled instance, additional information such as the axis minimum
and maximum are required. These must be specified in the instance configuration. When only mapping the instance
as a channel input, this is not required.

#### Known bugs / problems

Creating an `evdev` output device requires elevated privileges, namely, write access to the system's
`/dev/uinput`. Usually, this is granted for users in the `input` group and the `root` user.

Input devices may synchronize logically connected event types (for example, X and Y axes) via `EV_SYN`-type
events. The MIDIMonster also generates these events after processing channel events, but may not keep the original
event grouping.

`EV_KEY` key-down events are sent for normalized channel values over `0.9`.

Extended event type values such as `EV_LED`, `EV_SND`, etc are recognized in the MIDIMonster configuration file
but may or may not work with the internal channel mapping and normalization code.
