### The `maweb` backend

This backend connects directly with the integrated *MA Web Remote* of MA Lighting consoles and OnPC
instances (GrandMA2 / GrandMA2 OnPC / GrandMA Dot2 / GrandMA Dot2 OnPC).
It grants read-write access to the console's playback controls as well as write access to most command
line and control keys.

#### Setting up the console

For the GrandMA2 enter the console configuration (`Setup` key), select `Console`/`Global Settings` and
set the `Remotes` option to `Login enabled`.
Create an additional user that is able to log into the Web Remote using `Setup`/`Console`/`User & Profiles Setup`.

For the dot2, enter the console configuration using the `Setup` key, select `Global Settings` and enable the
Web Remote. Set a web remote password using the option below the activation setting.

#### Global configuration

| Option	| Example value		| Default value		| Description							|
|---------------|-----------------------|-----------------------|---------------------------------------------------------------|
| `interval`	| `100`			| `50`			| Query interval for input data polling (in msec)		|

#### Instance configuration

| Option	| Example value		| Default value		| Description							|
|---------------|-----------------------|-----------------------|---------------------------------------------------------------|
| `host`	| `10.23.42.21 80`	| none			| Host address (and optional port) of the MA Web Remote		|
| `user`	| `midimonster`		| none			| User for the remote session (GrandMA2)			|
| `password`	| `midimonster`		| `midimonster`		| Password for the remote session				|
| `cmdline`	| `console`		| `remote`		| Commandline key handling mode (see below)			|

The per-instance command line mode may be one of `remote`, `console` or `downgrade`. The first option handles
command keys with a "virtual" commandline belonging to the Web Remote connection. Any commands entered are
not visible on the main console. The `console` mode is only available with GrandMA2 remotes and injects key events
into the main console. This mode also supports additional hardkeys that are only available on GrandMA consoles.
When connected to a dot2 console while this mode is active, the use of commandline keys will not be possible.
With the `downgrade` mode, keys are handled on the console if possible, falling back to remote handling if not.

#### Channel specification

Currently, three types of MA controls can be assigned, with each having some subcontrols

* Fader executor
* Button executor
* Command keys

##### Executors

* For the GrandMA2, executors are arranged in pages, with each page having 90 fader executors (numbered 1 through 90)
	and 90 button executors (numbered 101 through 190).
	* A fader executor consists of a `fader`, two buttons above it (`upper`, `lower`) and one `button` below it.
	* A button executor consists of a `button` control and a virtual `fader` (visible on the console in the "Action Buttons" view).
* For the dot2, executors are also arranged in pages, but the controls are non-obviously numbered.
	* For the faders, they are numerically right-to-left from the Core Fader section (Faders 6 to 1) over the F-Wing 1 (Faders 13 to 6) to
	F-Wing 2 (Faders 21 to 14).
	* Above the fader sections are two rows of 21 `button` executors, numbered 122 through 101 (lower row) and 222 through 201 (upper row),
		in the same order as the faders are.
	* Fader executors have two buttons below them (`upper` and `lower`).
	* The button executor section consists of six rows of 16 buttons, divided into two button wings. Buttons on the wings
		are once again numbered right-to-left.
		* B-Wing 1 has `button` controls 308 to 301 (top row), 408 to 401 (second row), and so on until 808 through 801 (bottom row)
		* B-Wing 2 has 316 to 309 (top row) through 816 to 809 (bottom row)

When creating a new show, only the first page is created and active. Additional pages have to be created explicitly within
the console before being usable. When mapped as outputs, `fader` controls output their value, `button` controls output 1 when the corresponding
executor is running, 0 otherwise.

These controls can be addressed like

```
mw1.page1.fader5 > mw1.page1.upper5
mw1.page3.lower3 > mw1.page2.button2
```

A button executor can likewise be mapped using the syntax

```
mw1.page2.button103 > mw1.page3.fader101
mw1.page2.button803 > mw1.page3.button516
```

##### Command keys

Command keys will be pressed when the incoming event value is greater than `0.9` and released when it is less than that.
They can be mapped using the syntax

```
mw1.<key-name>
```

The following keys are mappable in all commandline modes and work on all consoles

| Supported	| Command	| Line		| Keys		|		|		|
|---------------|---------------|---------------|---------------|---------------|---------------|
| `PREV`	| `SET`		| `NEXT`	| `TIME`	| `EDIT`	| `UPDATE`	|
| `OOPS`	| `ESC`		| `CLEAR`	| `0`		| `1`		| `2`		|
| `3`		| `4`		| `5`		| `6`		| `7`		| `8`		|
| `9`		| `PUNKT`	| `ENTER`	| `PLUS`	| `MINUS`	| `THRU`	|
| `IF`		| `AT`		| `FULL`	| `MA`		| `HIGH`	| `SOLO`	|
| `SELECT`	| `OFF`		| `ON`		| `ASSIGN`	| `COPY`	| `DELETE`	|
| `STORE`	| `GOTO`	| `PAGE`	| `MACRO`	| `PRESET`	| `SEQU`	|
| `CUE`		| `EXEC`	| `FIXTURE`	| `GROUP`	| `GO_MINUS`	| `PAUSE`	|
| `GO_PLUS`	|		|		|		|		|		|

The following keys only work when keys are being handled with a virtual command line

| Web		| Remote		| specific			|		|			|
|---------------|-----------------------|-------------------------------|---------------|-----------------------|
| `LABEL`	|`FIXTURE_CHANNEL`	| `FIXTURE_GROUP_PRESET`	| `EXEC_CUE`	| `STORE_UPDATE`	|
| `PROG_ONLY`	| `SPECIAL_DIALOGUE`	| `ODD`				| `EVEN`	| `WINGS`		|
| `RESET`	|			|				|		|			|

The following keys only work in the `console` or `downgrade` command line modes on a GrandMA2

| GrandMA2	| console	| only		|		|		|		|
|---------------|---------------|---------------|---------------|---------------|---------------|
| `CHPGPLUS`	| `CHPGMINUS`	| `FDPGPLUS`	| `FDPGMINUS`	| `BTPGPLUS`	| `BTPGMINUS`	|
| `X1`		| `X2`		| `X3`		| `X4`		| `X5`		| `X6`		|
| `X7`		| `X8`		| `X9`		| `X10`		| `X11`		| `X12`		|
| `X13`		| `X14`		| `X15`		| `X16`		| `X17`		| `X18`		|
| `X19`		| `X20`		| `V1`		| `V2`		| `V3`		| `V4`		|
| `V5`		| `V6`		| `V7`		| `V8`		| `V9`		| `V10`		|
| `NIPPLE`	| `TOOLS`	| `SETUP`	| `BACKUP`	| `BLIND`	| `FREEZE`	|
| `PREVIEW`	| `FIX`		| `TEMP`	| `TOP`		| `VIEW`	| `EFFECT`	|
| `CHANNEL`	| `MOVE`	| `BLACKOUT`	| `PLEASE`	| `LIST`	| `USER1`	|
| `USER2`	| `ALIGN`	| `HELP`	| `UP`		| `DOWN`	| `FASTREVERSE`	|
| `LEARN`	| `FASTFORWARD`	| `GO_MINUS_SMALL` | `PAUSE_SMALL` | `GO_PLUS_SMALL` |		|

#### Known bugs / problems

To properly encode the user password, this backend depends on a library providing cryptographic functions (`libssl` / `openssl`).
Since this may be a problem on some platforms, the backend can be built with this requirement disabled, which also disables the possibility
to set arbitrary passwords. The backend will always try to log in with the default password `midimonster` in this case. The user name is still
configurable.

Data input from the console is done by actively querying the state of all mapped controls, which is resource-intensive if done
at low latency. A lower input interval value will produce data with lower latency, at the cost of network & CPU usage.
Higher values will make the input "step" more, but will not consume as many CPU cycles and network bandwidth.

When requesting button executor events on the fader pages (execs 101 to 222) of a dot2 console, map at least one fader control from the 0 - 22 range
or input will not work due to strange limitations in the MA Web API.
