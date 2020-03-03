### The `python` backend

The `python` backend provides a flexible programming environment, allowing users
to route, generate and manipulate channel events using the Python 3 scripting languge.

Every instance has its own interpreter, which can be loaded with multiple Python modules.
These modules may contain member functions accepting a single `float` parameter, which can
then be used as target channels. For each incoming event, the handler function is called.

To interact with the MIDIMonster core, import the `midimonster` module from within your module.

The `midimonster` module provides the following functions:

| Function			| Usage example				| Description					|
|-------------------------------|---------------------------------------|-----------------------------------------------|
| `output(string, float)`	| `midimonster.output("foo", 0.75)`	| Output a value event to a channel		|
| `inputvalue(string)`		| `midimonster.inputvalue("foo")`	| Get the last input value on a channel		|
| `outputvalue(string)`		| `midimonster.outputvalue("bar")`	| Get the last output value on a channel 	|
| `current()`			| `print(midimonster.current())`	| Returns the name of the input channel whose handler function is currently running or `None` if the interpreter was called from another context |

Example Python module:
```
import midimonster

def in1(value):
	midimonster.output("out1", 1 - value)
```

Input values range between 0.0 and 1.0, output values are clamped to the same range.

#### Global configuration

The `python` backend does not take any global configuration.

#### Instance configuration

| Option	| Example value		| Default value 	| Description					|
|---------------|-----------------------|-----------------------|-----------------------------------------------|
| `module`	| `my_handlers.py`	| none			| (Path to) Python module source file, relative to configuration file location |

A single instance may have multiple `module` options specified. This will make all handlers available within their
module namespaces (see the section on channel specification).

#### Channel specification

Channel names may be any valid Python function name. To call handler functions in a module,
specify the channel as the functions qualified path (by prefixing it with the module name and a dot).

Example mappings:
```
py1.my_handlers.in1 < py1.foo
py1.out1 > py2.module.handler 
```

#### Known bugs / problems

Output values will not trigger corresponding input event handlers unless the channel is mapped
back in the MIDIMonster configuration. This is intentional.

Importing a Python module named `midimonster` may cause problems and is unsupported.

There is currently no functionality for cyclic execution. This may be implemented in a future
release.
