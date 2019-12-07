### The `lua` backend

The `lua` backend provides a flexible programming environment, allowing users to route and manipulate
events using the Lua programming language.

Every instance has it's own interpreter state which can be loaded with custom handler scripts.

To process incoming channel events, the MIDIMonster calls corresponding Lua functions (if they exist)
with the value (as a Lua `number` type) as parameter.

The following functions are provided within the Lua interpreter for interaction with the MIDIMonster

| Function			| Usage example			| Description				|
|-------------------------------|-------------------------------|---------------------------------------|
| `output(string, number)`	| `output("foo", 0.75)`		| Output a value event to a channel	|
| `interval(function, number)`	| `interval(update, 100)`	| Register a function to be called periodically. Intervals are milliseconds (rounded to the nearest 10 ms) |
| `input_value(string)`		| `input_value("foo")`		| Get the last input value on a channel	|
| `output_value(string)`	| `output_value("bar")`		| Get the last output value on a channel |


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

| Option	| Example value		| Default value 	| Description		|
|---------------|-----------------------|-----------------------|-----------------------|
| `script`	| `script.lua`		| none			| Lua source file (relative to configuration file)|

A single instance may have multiple `source` options specified, which will all be read cumulatively.

#### Channel specification

Channel names may be any valid Lua function name.

Example mapping:
```
lua1.foo > lua2.bar
```

#### Known bugs / problems

Using any of the interface functions (`output`, `interval`, `input_value`, `output_value`) as an
input channel name to a Lua instance will not call any handler functions.
Using these names as arguments to the output and value interface functions works as intended.

Output values will not trigger corresponding input event handlers unless the channel is mapped
back in the MIDIMonster configuration.
