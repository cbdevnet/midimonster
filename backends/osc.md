### The `osc` backend

This backend offers read and write access to the Open Sound Control protocol,
spoken primarily by visual interface tools and hardware such as TouchOSC.

#### Global configuration

| Option	| Example value		| Default value 	| Description		|
|---------------|-----------------------|-----------------------|-----------------------|
| `detect`	| `on`			| `off`			| Output the path of all incoming OSC packets to allow for easier configuration. Any path filters configured using the `root` instance configuration options still apply. |

#### Instance configuration

| Option	| Example value		| Default value 	| Description		|
|---------------|-----------------------|-----------------------|-----------------------|
| `root`	| `/my/osc/path`	| none			| An OSC path prefix to be prepended to all channels |
| `bind`	| `:: 8000`		| none			| The host and port to listen on |
| `destination`	| `10.11.12.13 8001`	| none			| Remote address to send OSC data to. Setting this enables the instance for output. The special value `learn` causes the MIDImonster to always reply to the address the last incoming packet came from. A different remote port for responses can be forced with the syntax `learn@<port>` |

Note that specifying an instance root speeds up matching, as packets not matching
it are ignored early in processing.

Channels that are to be output or require a value range different from the default ranges (see below)
require special configuration, as their types and limits have to be set.

This is done by specifying *patterns* in the instance configuration using an assignment of the syntax

```
/local/osc/path = <format> <min> <max> <min> <max> ...
```

The pattern will be matched only against the local part (that is, the path excluding any configured instance root).
Patterns may contain the following expressions (conforming to the [OSC pattern matching specification](https://opensoundcontrol.stanford.edu/spec-1_0.html)):
* `?` matches any single legal character
* `*` matches zero or more legal characters
* A comma-separated list of strings inside curly braces `{}` matches any of the strings
* A string of characters within square brackets `[]` matches any character in the string
	* Two characters with a `-` between them specify a range of characters
	* An exclamation mark immediately after the opening `[` negates the meaning of the expression (ie. it matches characters not in the range)
* Any other legal character matches only itself

**format** may be any sequence of valid OSC type characters. See below for a table of supported
OSC types.

For each component of the path, the minimum and maximum values must be given separated by spaces.
Components may be accessed in the mapping section as detailed in the next section.

An example configuration for transmission of an OSC message with 2 floating point components with
a range between 0.0 and 2.0 (for example, an X-Y control), would look as follows:

```
/1/xy1 = ff 0.0 2.0 0.0 2.0
```

To configure a range of faders, an expression similar to the following line could be used

```
/1/fader* = f 0.0 1.0
```

When matching channels against the patterns to use, the first matching pattern (in the order in which they have been configured) will be used
as configuration for that channel.

#### Channel specification

A channel may be any valid OSC path, to which the instance root will be prepended if
set. Multi-value controls (such as X-Y pads) are supported by appending `:n` to the path,
where `n` is the parameter index, with the first (and default) one being `0`.

Example mapping:
```
osc1./1/xy1:0 > osc2./1/fader1
```

Note that any channel that is to be output will need to be set up in the instance
configuration.

#### Supported types & value ranges

OSC allows controls to have individual value ranges and supports different parameter types.
The following types are currently supported by the MIDIMonster:

* **i**: 32-bit signed integer
* **f**: 32-bit IEEE floating point
* **h**: 64-bit signed integer
* **d**: 64-bit double precision floating point

For each type, there is a default value range which will be assumed if the channel is not otherwise
configured using the instance configuration. Values out of a channels range will be clipped.

The default ranges are:

* **i**: `0` to `255`
* **f**: `0.0` to `1.0`
* **h**: `0` to `1024`
* **d**: `0.0` to `1.0`

#### Known bugs / problems

The OSC path match currently works on the unit of characters. This may lead to some unexpected results
when matching expressions of the form `*<expr>`.

Ping requests are not yet answered. There may be some problems using broadcast output and input.
