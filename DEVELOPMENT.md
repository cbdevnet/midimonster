# MIDIMonster development guide

This document serves as a reference for contributors interested in the low-level implementation
of the MIDIMonster. It will be extended as problems come up and need solving ;)

## Basics

All rules are meant as guidelines. There may be situations where they need to be applied
in spirit rather than by the letter.

### Architectural guidelines

* Change in functionality or behaviour requires a change in documentation.
* There is more honor in deleting code than there is in adding code.
	* Corollary: Code is a liability, not an asset.
	* But: Benchmark the naive implementation before optimizing prematurely.
* The `master` branch must build successfully. Test breaking changes in a branch.
* Commit messages should be in the imperative voice ("When applied, this commit will: ").
* The working language for this repository is english.
* External dependencies are only acceptable when necessary and available from package repositories.
	* Note that external dependencies make OS portability complicated

### Code style

* Tabs for indentations, spaces for word separation
* Lines may not end in spaces or tabs
* There should be no two consecutive spaces (or spaces intermixed with tabs)
* There should be no two consecutive newlines
* All symbol names in `snake_case` except where mandated by external interfaces
* When possible, prefix symbol names with their "namespace" (ie. the relevant section or module name)
* Variables should be appropriately named for what they do
	* The name length should be (positively) correlated with usage
	* Loop counters may be one-character letters
		* Prefer to name unsigned loop counters `u` and signed ones `i`
* Place comments above the section they are commenting on
	* Use inline comments sparingly
* Do not omit '{}' brackets, even if optional (e.g. single-statement conditional bodies)
* Opening braces stay on the same line as the condition

#### C specific

* Prefer lazy designated initializers to `memset()`
* Avoid `atoi()`/`itoa()`, use `strto[u]l[l]()` and `snprintf()`
* Avoid unsafe functions without explicit bounds parameters (eg. `strcat()`). 

## Repository layout

* Keep the root directory as clean as possible
	* Files that are not related directly to the MIDIMonster implementation go into the `assets/` directory
* Prefer vendor-neutral names for configuration files where necessary

## Build pipeline

* The primary build pipeline is `make`

## Architecture

* Strive to make backends platform-portable
	* If that is not possible, try to keep the backend configuration compatible to other backends implementing the same protocol
* If there is significant potential for sharing functionality between backends, consider implementing it in `libmmbackend`
* Place a premium on keeping the MIDIMonster a lightweight tool in terms of installed dependencies and core functionality
	* If possible, prefer a local implementation to one which requires additional (dynamic) dependencies

## Language & Communication 

* All visible communication (ie. error messages, debug messages) should be complete, correct english sentences
* Strive for each output to have a concrete benefit or information to the reader
	* Corollary: If nothing happens, don't send output
		* Debug messages are somewhat exempt from this guideline
* For error messages, give enough context to reasonably allow the user to either track down the problem or report a meaningful issue

# Packaging

Packaging the MIDIMonster for release in distributions is an important task. It facilitates easy access to
the MIDIMonster functionality to a wide audience. This section is not strictly relevant for development, but touches
on some of the same principles.

As the MIDIMonster is a tool designed for interfacing between several different protocols, applications and
other tools, to use "all" functionality of the MIDIMonster would imply installing additional software the user
might not actually need. This runs counter to our goal of staying a lightweight tool for translation and control.

The recommended way to package the MIDIMonster for binary distribution would be to split the build artifacts into
multiple packages, separating out the heavier dependencies into separately installable units. If that is not an option,
marking external dependencies of backends as `optional` or `recommended` should be preferred to having them required
to be installed.

Some backends have been marked optional in the repository and are only built when using `make full`.

The recommended grouping into packaging units is as follows (without regard to platform compatibility, which
may further impact the grouping):

* Package `midimonster`: Core, Backends `evdev`, `artnet`, `osc`, `loopback`, `sacn`, `maweb`, `openpixelcontrol`, `rtpmidi`, `visca`, `mqtt`
	* External dependencies: `libevdev`, `openssl`
* Package `midimonster-programming`: Backends `lua`, `python`
	* External dependencies: `liblua`, `python3`
* Package `midimonster-media`: `midi`, `jack`, `ola`
	* External dependencies: `libasound2`, `libjack-jackd2`, `libola`
