# The MIDIMonster

Named for its scary math, the MIDIMonster is a universal translation
tool between multi-channel absolute-value-based control and/or bus protocols.

Currently, the MIDIMonster supports the following protocols:

| Protocol			| Operating Systems	| Notes				| Backends			|
|-------------------------------|-----------------------|-------------------------------|-------------------------------|
| MIDI				| Linux, Windows, OSX	| Linux: via ALSA/JACK, OSX: via JACK | [`midi`](backends/midi.md), [`winmidi`](backends/winmidi.md), [`jack`](backends/jack.md) |
| ArtNet			| Linux, Windows, OSX	| Version 4			| [`artnet`](backends/artnet.md)|
| Streaming ACN (sACN / E1.31)	| Linux, Windows, OSX	|				| [`sacn`](backends/sacn.md)	|
| OpenSoundControl (OSC)	| Linux, Windows, OSX	|				| [`osc`](backends/osc.md)	|
| evdev input devices		| Linux			| Virtual output supported	| [`evdev`](backends/evdev.md)	|
| Open Lighting Architecture	| Linux, OSX		|				| [`ola`](backends/ola.md)	|
| MA Lighting Web Remote	| Linux, Windows, OSX	| GrandMA and dot2 (incl. OnPC)	| [`maweb`](backends/maweb.md)	|
| JACK/LV2 Control Voltage (CV)	| Linux, OSX		|				| [`jack`](backends/jack.md)	|

with additional flexibility provided by a [Lua scripting environment](backends/lua.md).

The MIDIMonster allows the user to translate any channel on one protocol into channel(s)
on any other (or the same) supported protocol, for example to:

* Translate MIDI Control Changes into Notes ([Example configuration](configs/unifest-17.cfg))
* Translate MIDI Notes into ArtNet or sACN ([Example configuration](configs/launchctl-sacn.cfg))
* Translate OSC messages into MIDI ([Example configuration](configs/midi-osc.cfg))
* Dynamically generate, route and modify events using the Lua programming language ([Example configuration](configs/lua.cfg) and [Script](configs/demo.lua)) to create your own lighting controller or run effects on TouchOSC (Flying faders demo [configuration](configs/flying-faders.cfg) and [script](configs/flying-faders.lua))
* Use an OSC app as a simple lighting controller via ArtNet or sACN
* Visualize ArtNet data using OSC tools
* Control lighting fixtures or DAWs using gamepad controllers, trackballs, etc ([Example configuration](configs/evdev.cfg))
* Play games, type, or control your mouse using MIDI controllers ([Example configuration](configs/midi-mouse.cfg))

[![Build Status](https://travis-ci.com/cbdevnet/midimonster.svg?branch=master)](https://travis-ci.com/cbdevnet/midimonster) [![Coverity Scan Build Status](https://scan.coverity.com/projects/15168/badge.svg)](https://scan.coverity.com/projects/15168)

# Table of Contents

  * [Usage](#usage)
  * [Configuration](#configuration)
  * [Backend documentation](#backend-documentation)
  * [Building](#building)
    + [Prerequisites](#prerequisites)
    + [Build](#build)
  * [Development](#development)

## Usage

The MIDImonster takes as it's first argument the name of an optional configuration file
to use (`monster.cfg` is used as default if none is specified). The configuration
file syntax is explained in the next section.

## Configuration

Each protocol supported by MIDIMonster is implemented by a *backend*, which takes
global protocol-specific options and provides *instance*s, which can be configured further.

The configuration is stored in a file with a format very similar to the common
INI file format. A section is started by a header in `[]` braces, followed by
lines of the form `option = value`.

Lines starting with a semicolon are treated as comments and ignored. Inline comments
are not currently supported.

Example configuration files may be found in [configs/](configs/).

### Backend and instance configuration

A configuration section may either be a *backend configuration* section, started by
`[backend <backend-name>]`, an *instance configuration* section, started by
`[<backend-name> <instance-name>]` or a *mapping* section started by `[map]`.

Backends document their global options in their [backend documentation](#backend-documentation).
Some backends may not require global configuration, in which case the configuration
section for that particular backend can be omitted.

To make an instance available for mapping channels, it requires at least the
`[<backend-name> <instance-name>]` configuration stanza. Most backends require
additional configuration for their instances.

### Channel mapping

The `[map]` section consists of lines of channel-to-channel assignments, reading like

```
instance.channel-a < instance.channel-b
instance.channel-a > instance.channel-b
instance.channel-c <> instance.channel-d
```

The first line above maps any event originating from `instance.channel-b` to be output
on `instance.channel-a` (right-to-left mapping).

The second line makes that mapping a bi-directional mapping, so both of those channels
output eachothers events.

The last line is a shorter way to create a bi-directional mapping.

### Multi-channel mapping

To make mapping large contiguous sets of channels easier, channel names may contain
expressions of the form `{<start>..<end>}`, with *start* and *end* being positive integers
delimiting a range of channels.  Multiple such expressions may be used in one channel
specification, with the rightmost expression being incremented (or decremented) first for
evaluation.

Both sides of a multi-channel assignment need to have the same number of channels, or one
side must have exactly one channel.

Example multi-channel mapping:

```
instance-a.channel{1..10} > instance-b.{10..1}
```

## Backend documentation

Every backend includes specific documentation, including the global and instance
configuration options, channel specification syntax and any known problems or other
special information. These documentation files are located in the `backends/` directory.

* [`midi` backend documentation](backends/midi.md)
* [`jack` backend documentation](backends/jack.md)
* [`winmidi` backend documentation](backends/winmidi.md)
* [`artnet` backend documentation](backends/artnet.md)
* [`sacn` backend documentation](backends/sacn.md)
* [`evdev` backend documentation](backends/evdev.md)
* [`loopback` backend documentation](backends/loopback.md)
* [`ola` backend documentation](backends/ola.md)
* [`osc` backend documentation](backends/osc.md)
* [`lua` backend documentation](backends/lua.md)
* [`maweb` backend documentation](backends/maweb.md)

## Building

This section will explain how to build the provided sources to be able to run
`midimonster`.

### Prerequisites

In order to build the MIDIMonster, you'll need some libraries that provide
support for the protocols to translate.

* `libasound2-dev` (for the ALSA MIDI backend)
* `libevdev-dev` (for the evdev backend)
* `liblua5.3-dev` (for the lua backend)
* `libola-dev` (for the optional OLA backend)
* `libjack-jackd2-dev` (for the JACK backend)
* `pkg-config` (as some projects and systems like to spread their files around)
* `libssl-dev` (for the MA Web Remote backend)
* A C compiler
* GNUmake

To build for Windows, the package `mingw-w64` provides a cross-compiler that can
be used to build a subset of the backends as well as the core.

### Build

For Linux and OSX, just running `make` in the source directory should do the trick.

The build process accepts the following parameters, either from the environment or
as arguments to the `make` invocation:

| Target	| Parameter		| Default value			| Description			|
|---------------|-----------------------|-------------------------------|-------------------------------|
| build targets	| `DEFAULT_CFG`		| `monster.cfg`			| Default configuration file	|
| build targets	| `PLUGINS`		| Linux/OSX: `./backends/`, Windows: `backends\` | Backend plugin library path	|
| `install`	| `PREFIX`		| `/usr`			| Install prefix for binaries	|
| `install`	| `DESTDIR`		| empty				| Destination directory for packaging builds	|
| `install`	| `DEFAULT_CFG`		| empty				| Install path for default configuration file	|
| `install`	| `PLUGINS`		| `$(PREFIX)/lib/midimonster`	| Install path for backend shared objects	|
| `install`	| `EXAMPLES`		| `$(PREFIX)/share/midimonster`	| Install path for example configurations	|

Note that the same variables may have different default values depending on the target. This implies that
builds that are destined to be installed require those variables to be set to the same value for the
build and `install` targets.

Some backends have been marked as optional as they require rather large additional software to be installed,
for example the `ola` backend. To create a build including these, run `make full`.

Backends may also be built selectively by running `make <backendfile>` in the `backends/` directory,
for example

```
make jack.so
```
#### Using the installer

For easy installation on Linux, the [installer script](installer.sh) can be used:

```
wget https://raw.githubusercontent.com/cbdevnet/midimonster/master/installer.sh ./
chmod +x ./installer.sh
./installer.sh
```
This tool can also update MIDImonster automatically using a configuration file generated by the installer.
To do so, run `midimonster-updater` as root on your system after using the installer.

#### Building for packaging or installation

For system-wide install or packaging builds, the following steps are recommended:

```
export PREFIX=/usr
export PLUGINS=$PREFIX/lib/midimonster
export DEFAULT_CFG=/etc/midimonster/midimonster.cfg
make clean
make full
make install
```

Depending on your configuration of `DESTDIR`, the `make install` step may require root privileges to
install the binaries to the appropriate destinations.

To create Debian packages, use the debianization and `git-buildpackage` configuration on the `debian/master`
branch. Simply running `gbp buildpackage` should build a package for the last tagged release.

#### Building for Windows

To build for Windows, you still need to compile on a Linux machine (virtual machines work well for this).

In a fresh Debian installation, you will need to install the following packages (using `apt-get install` as root):

* `build-essential`
* `pkg-config`
* `git`
* `mingw-w64`

Clone the repository and run `make windows` in the project directory.
This will build `midimonster.exe` as well as a set of backends as DLL files, which you can then copy
to the Windows machine.

Note that some backends have limitations when building on Windows (refer to the backend documentation
for detailed information).

## Development

The architecture is split into the `midimonster` core, handling mapping
and resource management, and the backends, which are shared objects loaded
at start time, which provide a protocol mapping to instances / channels.

The API and structures are more-or-less documented in [midimonster.h](midimonster.h),
more detailed documentation may follow.

To build with `clang` sanitizers and even more warnings enabled, run `make sanitize`.
This is useful to check for common errors and oversights.

For runtime leak analysis with `valgrind`, you can use `make run`.
