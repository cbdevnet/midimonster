# The MIDIMonster

Named for it's scary math, the MIDIMonster is a universal translation
tool between multi-channel absolute-value-based control and/or bus protocols,
such as MIDI, DMX/ArtNet and OSC.

It allows the user to translate channels on one protocol into channels on another
(or the same) protocol, eg

* Translate MIDI Control Changes into Notes
* Translate MIDI Notes into ArtNet
* Translate OSC messages into MIDI

## Configuration

Each protocol supported by MIDIMonster is implemented by a *backend*, which takes
global protocol-specific options and provides *instance*s, which can be configured further.

The configuration is stored in a file with a format very similar to the common
INI file format. A section is started by a header in `[]` braces, followed by
lines of the form `option = value`.

A section may either be a *backend configuration* section, started by `[backend <backend-name>]`,
an *instance configuration* section, started by `[<backend-name> <instance-name>]` or a *mapping*
section started by `[map]`.

The options accepted by the implemented backends are documented in the next section.

### The `artnet` backend

#### Global configuration

| Option	| Example value		| Default value 	| Description		|
|---------------|-----------------------|-----------------------|-----------------------|
| `bind`	| `127.0.0.1 6454`	| *none*		| What address and port to bind the ArtNet socket to |
| `net`		| `0`			| `0`			| The default net to use |

#### Instance configuration

### The `midi` backend

#### Global configuration

#### Instance configuration

### The `osc` backend

#### Global configuration

#### Instance configuration

## Building

This section will explain how to build the provided sources to be able to run
`midimonster`.

### Prerequisites

In order to build the MIDIMonster, you'll need some libraries that provide
support for the protocols to translate.

* libasound2-dev
* A C compiler

### Building

Just running `make` in the source directory should do the trick.
