### The `lua` backend

The `lua` backend provides a flexible programming environment, allowing users to route, generate
and  manipulate events using the Lua scripting language.

Every instance has its own interpreter state which can be loaded with custom scripts.

To process incoming channel events, the MIDIMonster calls corresponding Lua functions (if they exist)
with the value (as a Lua `number` type) as parameter. Alternatively, a designated default channel handler
which will receive events for all incoming channels may be supplied in the configuration.

The backend can also call Lua functions repeatedly using a timer, allowing users to implement time-based
functionality (such as evaluating a fixed mathematical function or outputting periodic updates).

The following functions are provided within the Lua interpreter for interaction with the MIDIMonster

| Function			| Usage example			| Description				|
|-------------------------------|-------------------------------|---------------------------------------|
| `output(string, number)`	| `output("foo", 0.75)`		| Output a value event to a channel on this instance	|
| `interval(function, number)`	| `interval(update, 100)`	| Register a function to be called periodically. Intervals are milliseconds (rounded to the nearest 10 ms). Calling `interval` on a Lua function multiple times updates the interval. Specifying `0` as interval stops periodic calls to the function. Do not call this function from within a Lua thread. |
| `cleanup_handler(function)`	| `cleanup_handler(shutdown)`	| Register a function to be called when the instance is destroyed (on MIDIMonster shutdown). One cleanup handler can be registered per instance. Calling this function when the instance already has a cleanup handler registered replaces the handler, returning the old one. |
| `input_value(string)`		| `input_value("foo")`		| Get the last input value on a channel on this instance	|
| `output_value(string)`	| `output_value("bar")`		| Get the last output value on a channel on this instance |
| `input_channel()`		| `print(input_channel())`	| Returns the name of the input channel whose handler function is currently running or `nil` if in an `interval`'ed function (or the initial parse step) |
| `timestamp()`			| `print(timestamp())`		| Returns the core timestamp for this iteration with millisecond resolution. This is not a performance timer, but intended for timeouting, etc |
| `thread(function)`		| `thread(run_show)`		| Run a function as a Lua thread (see below) |
| `sleep(number)`		| `sleep(100)`			| Suspend current thread for time specified in milliseconds |

While a channel handler executes, calling `input_value` for that channel returns the previous value. Once
the handler returns, the internal buffer is updated.

Example script:
```lua
-- This function is called when there are incoming events on input channel `bar`
-- It outputs half the input value on the channel `foo`
function bar(value)
	output("foo", value / 2);
end

-- This function is registered below to execute every second
-- It toggles output channel `bar` every time it is called by storing the next state in the variable `step`
step = 0
function toggle()
	output("bar", step * 1.0);
	step = (step + 1) % 2;
end

-- This function is registered below to run as a Lua thread
-- It loops infinitely and toggles the output channel `narf` every second
function run_show()
	while(true) do
		sleep(1000);
		output("narf", 0);
		sleep(1000);
		output("narf", 1.0);
	end
end

-- This function is registered below to be called when the MIDIMonster shuts down
function save_values()
	-- Store state to a file, for example
end

-- Register the functions
interval(toggle, 1000)
thread(run_show)
cleanup_handler(save_values)
```

Input values range between 0.0 and 1.0, output values are clamped to the same range.

Threads are implemented as Lua coroutines, not operating system threads. This means that
cooperative multithreading is required, which can be achieved by calling the `sleep(number)`
function from within a running thread. Calling that function from any other context is
not supported.

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

Using any of the interface functions (`output`, `interval`, etc.) as an input channel name to a
Lua instance will not call any handler functions. Using these names as arguments to the output and
value interface functions works as intended. When using a default handler, the default handler will
be called.

Output values will not trigger corresponding input event handlers unless the channel is mapped
back in the MIDIMonster configuration. This is intentional.

Output events generated from cleanup handlers called during shutdown will not be routed, as the core
routing facility has already shut down at this point. There are no plans to change this behaviour.

To build (and run) the `lua` backend on Windows, a compiled version of the Lua 5.3 library is required.
For various reasons (legal, separations of concern, not wanting to ship binary data in the repository),
the MIDIMonster project can not provide this file within this repository.
You will need to acquire a copy of `lua53.dll`, for example by downloading it from the [luabinaries
project](http://luabinaries.sourceforge.net/download.html).

Place this file in the project root directory and run `make lua.dll` inside the `backends/` directory
to build the backend. 
At runtime, Windows searches for the file in the same directory as `midimonster.exe`.
