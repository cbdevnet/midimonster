# The MIDIMonster
<img align="right" src="/assets/MIDIMonster.svg?raw=true&sanitize=true" alt="MIDIMonster Logo" width="20%">

[![Coverity Scan Build Status](https://scan.coverity.com/projects/15168/badge.svg)](https://scan.coverity.com/projects/15168)
[![CI Pipeline Status](https://ci.spacecdn.de/buildStatus/icon?job=midimonster%2Fmaster)](https://ci.spacecdn.de/blue/organizations/jenkins/midimonster/activity)
[![IRC Channel](https://static.midimonster.net/hackint-badge.svg)](https://webirc.hackint.org/#irc://irc.hackint.org/#midimonster)

Named for its scary math, the MIDIMonster is a universal control and translation
tool for multi-channel absolute-value-based control and/or bus protocols.

Currently, the MIDIMonster supports the following protocols:

| Protocol / Interface		| Operating Systems	| Notes				| Backends				|
|-------------------------------|-----------------------|-------------------------------|---------------------------------------|
| MIDI				| Linux, Windows, OSX	| Linux: via ALSA/JACK, OSX: via JACK | [`midi`](backends/midi.md), [`winmidi`](backends/winmidi.md), [`jack`](backends/jack.md) |
| ArtNet			| Linux, Windows, OSX	| Version 4			| [`artnet`](backends/artnet.md)	|
| Streaming ACN (sACN / E1.31)	| Linux, Windows, OSX	|				| [`sacn`](backends/sacn.md)		|
| OpenSoundControl (OSC)	| Linux, Windows, OSX	|				| [`osc`](backends/osc.md)		|
| MQTT				| Linux, Windows, OSX	| Protocol versions 5 and 3.1.1	| [`mqtt`](backends/mqtt.md)		|
| RTP-MIDI			| Linux, Windows, OSX	| AppleMIDI sessions supported	| [`rtpmidi`](backends/rtpmidi.md)	|
| OpenPixelControl		| Linux, Windows, OSX	| 8 Bit & 16 Bit modes		| [`openpixelcontrol`](backends/openpixelcontrol.md)	|
| Input devices (Mouse, Keyboard, etc)| Linux, Windows	|				| [`evdev`](backends/evdev.md), [`wininput`](backends/wininput.md) |
| Open Lighting Architecture	| Linux, OSX		|				| [`ola`](backends/ola.md)		|
| MA Lighting Web Remote	| Linux, Windows, OSX	| GrandMA2 and dot2 (incl. OnPC)	| [`maweb`](backends/maweb.md)	|
| JACK/LV2 Control Voltage (CV)	| Linux, OSX		|				| [`jack`](backends/jack.md)		|
| VISCA				| Linux, Windows, OSX	| PTZ Camera control over TCP/UDP	| [`visca`](backends/visca.md)	|
| Lua Scripting			| Linux, Windows, OSX	|				| [`lua`](backends/lua.md)		|
| Python Scripting		| Linux, OSX		|				| [`python`](backends/python.md)	|
| Loopback			| Linux, Windows, OSX	|				| [`loopback`](backends/loopback.md)	|

With these features, the MIDIMonster allows users to control any channel on any of these protocols, and translate any channel on
one protocol into channel(s) on any other (or the same) supported protocol, for example to:

* Translate MIDI Control Changes into MIDI Notes ([Example configuration](configs/unifest-17.cfg))
* Translate MIDI Notes into ArtNet or sACN ([Example configuration](configs/launchctl-sacn.cfg))
* Translate OSC messages into MIDI ([Example configuration](configs/midi-osc.cfg))
* Dynamically generate, route and modify events using the Lua programming language ([Example configuration](configs/lua.cfg) and [Script](configs/demo.lua))
	to create your own lighting controller or run effects on TouchOSC (Flying faders demo [configuration](configs/flying-faders.cfg) and [script](configs/flying-faders.lua))
* Use an OSC app as a simple lighting controller via ArtNet or sACN
* Visualize ArtNet data using OSC tools
* Control lighting fixtures or DAWs using gamepad controllers, trackballs, etc ([Example configuration](configs/evdev.cfg))
* Connect a device speaking RTP MIDI (for example, an iPad) to your computer or lighting console ([Example configuration](configs/rtpmidi.cfg))
* Play games, type, or control your mouse using MIDI controllers ([Example configuration](configs/midi-mouse.cfg))

If you encounter a bug or suspect a problem with a protocol implementation, please
[open an Issue](https://github.com/cbdevnet/midimonster/issues) or get in touch with us via
IRC on [Hackint in `#midimonster`](https://webirc.hackint.org/#irc://irc.hackint.org/#midimonster).
We are happy to hear from you!

# Table of Contents

* [Usage](#usage)
* [Configuration](#configuration)
* [Backend documentation](#backend-documentation)
* [Installation](#installation)
	+ [Using the installer](#using-the-installer)
	+ [Building from source](#building-from-source)
		- [Building for Linux/OSX](#building-for-linuxosx)
		- [Building for Packaging](#building-for-packaging)
		- [Building for Windows](#building-for-windows)
* [Development](#development)

## Usage

The MIDImonster takes as it's first argument the name of an optional configuration file
to use (`monster.cfg` is used as default if none is specified). The configuration
file syntax is explained in the next section.

The current MIDIMonster version can be queried by passing *-v* as command-line argument.

## Configuration

Each protocol supported by MIDIMonster is implemented by a *backend*, which takes
global protocol-specific options and provides *instance*s, which can be configured further.

The configuration is stored in a file with a format very similar to the common
INI file format. A section is started by a header in `[]` braces, followed by
lines of the form `option = value`.

Lines starting with a semicolon are treated as comments and ignored. Inline comments
are not currently supported.

Configuration files may be included recursively in other configuration files using
the syntax `[include <file>]`. This will read the referenced configuration file as
if it were inserted at that point.

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

Backend and instance configuration options can also be overridden via command line
arguments using the syntax `-b <backend>.<option>=<value>` for backend options
and `-i <instance>.<option>=<value>` for instance options. These overrides
are applied when the backend/instance is first mentioned in the configuration file.

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

To make mapping large contiguous sets of channels easier, channel names may contain certain
types of expressions specifying multiple channels at once.

Expressions of the form `{<start>..<end>}`, with *start* and *end* being positive integers,
expand to a range of channels, with the expression replaced by the incrementing or decrementing
value.

Expressions of the form `{value1,value2,value3}` (with any number of values separated by commas)
are replaced with each of the specified values in sequence.

Multiple such expressions may be used in one channel specification, with the rightmost expression
being evaluated first.

Both sides of a multi-channel assignment need to have the same number of channels, or one
side must have exactly one channel.

Example multi-channel mapping:
```
instance-a.channel{1..5} > instance-b.{a,b,c,d,e}
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
* [`rtpmidi` backend documentation](backends/rtpmidi.md)
* [`evdev` backend documentation](backends/evdev.md)
* [`loopback` backend documentation](backends/loopback.md)
* [`ola` backend documentation](backends/ola.md)
* [`osc` backend documentation](backends/osc.md)
* [`mqtt` backend documentation](backends/mqtt.md)
* [`openpixelcontrol` backend documentation](backends/openpixelcontrol.md)
* [`lua` backend documentation](backends/lua.md)
* [`python` backend documentation](backends/python.md)
* [`maweb` backend documentation](backends/maweb.md)
* [`wininput` backend documentation](backends/wininput.md)

## Installation

This section will explain how to build and install the MIDIMonster.
Development is mainly done on Linux, but builds for OSX and Windows
are possible.

Binary builds for all supported systems are available for download on the
[Release page](https://github.com/cbdevnet/midimonster/releases).

### Using the installer

The easiest way to install MIDIMonster and its dependencies on a Linux system
is the [installer script](installer.sh).

The following commands download the installer, make it executable and finally, start it:

```
wget https://raw.githubusercontent.com/cbdevnet/midimonster/master/installer.sh ./
chmod +x ./installer.sh
./installer.sh
```

The installer can also be used for automating installations or upgrades by specifying additional
command line arguments. To see a list of valid arguments, run the installer with the
`--help` argument.

The installer script can also update MIDIMonster to the latest version automatically,
using a configuration file generated during the installation.
To do so, run `midimonster-updater` as root on your system after using the installer.

If you prefer to install a Debian package you can download the `.deb` file from our
[Release page](https://github.com/cbdevnet/midimonster/releases).
To install the package, run the following command as the root user:

```
dpkg -i <file>.deb
```

### Building from source

To build the MIDIMonster directly from the sources, you'll need some libraries that provide
support for the protocols to translate. When building from source, you can also choose to
exclude backends (for example, if you don't need them or don't want to install their
prerequisites).

* `libasound2-dev` (for the ALSA MIDI backend)
* `libevdev-dev` (for the evdev backend)
* `liblua5.3-dev` (for the lua backend)
* `libola-dev` (for the optional OLA backend)
* `libjack-jackd2-dev` (for the JACK backend)
* `libssl-dev` (for the MA Web Remote backend)
* `python3-dev` (for the Python backend)
* `pkg-config` (as some projects and systems like to spread their files around)
* A C compiler
* GNUmake

To build for Windows, the package `mingw-w64` provides a cross-compiler that can
be used to build a subset of the backends as well as the core.

#### Building for Linux/OSX

For Linux and OSX, just running `make` in the source directory should do the trick.

Some backends have been marked as optional as they require rather large additional software to be installed,
for example the `ola` backend. To create a build including these, run `make full`.

To install a source build with `make install`, please familiarize yourself with the build parameters
as specified in the next section.

Backends may also be built selectively by running `make <backendfile>` in the `backends/` directory,
for example

```
make jack.so
```

#### Building for Packaging

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
