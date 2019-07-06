### The `lua` backend

The `lua` backend provides a flexible programming environment, allowing users to route and manipulate
events using the Lua programming language.

Every instance has it's own interpreter state which can be loaded with custom handler scripts.

To process incoming channel events, the MIDIMonster calls corresponding Lua functions with
the value (as a Lua `number` type) as parameter. To send output on a channel, the Lua environment
provides the function `output(channel-name, value)`.

Example script:
```
function bar(value)
	output("foo", value / 2)
end
```

Input values range between 0.0 and 1.0, output values are clamped to the same range.

#### Global configuration

The backend does not take any global configuration.

#### Instance configuration

| Option	| Example value		| Default value 	| Description		|
|---------------|-----------------------|-----------------------|-----------------------|
| `source`	| `script.lua`		| none			| Lua source file	|

A single instance may have multiple `source` options specified, which will all be read cumulatively.

#### Channel specification

Channel names may be any valid Lua function name.

Example mapping:
```
lua1.foo > lua2.bar
```

#### Known bugs / problems

Using `output` as an input channel name to a Lua instance does not work, as the interpreter has
`output` globally assigned to the event output function. Using `output` as an output channel name
via `output("output", value)` works as intended.

The path to the Lua source files is relative to the current working directory. This may lead
to problems when copying configuration between installations.
