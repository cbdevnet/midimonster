### The `lua` backend

The `lua` backend provides a flexible programming environment, allowing users to route, generate
and  manipulate events using the Lua scripting language.

Every instance has its own interpreter state which can be loaded with custom handler scripts.

To process incoming channel events, the MIDIMonster calls corresponding Lua functions (if they exist)
with the value (as a Lua `number` type) as parameter. Alternatively, a designated default channel handler
may be supplied in the configuration.

The following functions are provided within the Lua interpreter for interaction with the MIDIMonster

| Function			| Usage example			| Description				|
|-------------------------------|-------------------------------|---------------------------------------|
| `output(string, number)`	| `output("foo", 0.75)`		| Output a value event to a channel	|
| `interval(function, number)`	| `interval(update, 100)`	| Register a function to be called periodically. Intervals are milliseconds (rounded to the nearest 10 ms). Calling `interval` on a Lua function multiple times updates the interval. Specifying `0` as interval stops periodic calls to the function |
| `input_value(string)`		| `input_value("foo")`		| Get the last input value on a channel	|
| `output_value(string)`	| `output_value("bar")`		| Get the last output value on a channel |
| `input_channel()`		| `print(input_channel())`	| Returns the name of the input channel whose handler function is currently running or `nil` if in an `interval`'ed function (or the initial parse step) |
| `timestamp()`			| `print(timestamp())`		| Returns the core timestamp for this iteration with millisecond resolution. This is not a performance timer, but intended for timeouting, etc |

Example script:
```
function bar(value)
	output("foo", value / 2)
end

step = 0
function toggle()
	output("bar", step * 1.0)
	step = (step + 1) % 2;
end

interval(toggle, 1000)
```

Input values range between 0.0 and 1.0, output values are clamped to the same range.

#### Global configuration

The `lua` backend does not take any global configuration.

#### Instance configuration

| Option		| Example value		| Default value 	| Description		|
|-----------------------|-----------------------|-----------------------|-----------------------|
| `script`		| `script.lua`		| none			| Lua source file (relative to configuration file) |
| `default-handler`	| `handler`		| none			| Name of a function to be called as handler for all incoming channels (instead of the per-channel handlers) |

A single instance may have multiple `script` options specified, which will all be read cumulatively.

#### Channel specification

Channel names may be any valid Lua function name.

Example mapping:
```
lua1.foo > lua2.bar
```

#### Known bugs / problems

Using any of the interface functions (`output`, `interval`, `input_value`, `output_value`, `input_channel`,
`timestamp`) as an input channel name to a Lua instance will not call any handler functions.
Using these names as arguments to the output and value interface functions works as intended.

Output values will not trigger corresponding input event handlers unless the channel is mapped
back in the MIDIMonster configuration. This is intentional.

To build (and run) the `lua` backend on Windows, a compiled version of the Lua 5.3 library is required.
For various reasons (legal, separations of concern, not wanting to ship binary data in the repository),
the MIDIMonster project can not provide this file within this repository.
You will need to acquire a copy of `lua53.dll`, for example by downloading it from the [luabinaries
project](http://luabinaries.sourceforge.net/download.html).

To build the `lua` backend for Windows, place `lua53.dll` in a subdirectory `libs/` in the project root
and run `make lua.dll` inside the `backends/` directory.

At runtime, Windows searches for the file in the same directory as `midimonster.exe`.
