### The `maweb` backend

This backend connects directly with the integrated *MA Web Remote* of MA Lighting consoles and OnPC
instances (GrandMA2 / GrandMA2 OnPC / GrandMA Dot2 / GrandMA Dot2 OnPC).
It grants read-write access to the console's playback faders and buttons as well as write access to
the command line buttons.

#### Setting up the console

For the GrandMA2 enter the console configuration (`Setup` key), select `Console`/`Global Settings` and
set the `Remotes` option to `Login enabled`.
Create an additional user that is able to log into the Web Remote using `Setup`/`Console`/`User & Profiles Setup`.

For the dot2, enter the console configuration using the `Setup` key, select `Global Settings` and enable the
Web Remote. Set a web remote password using the option below the activation setting.

#### Global configuration

The `maweb` backend does not take any global configuration.

#### Instance configuration

| Option	| Example value		| Default value		| Description							|
|---------------|-----------------------|-----------------------|---------------------------------------------------------------|
| `host`	| `10.23.42.21 80`	| none			| Host address (and optional port) of the MA Web Remote		|
| `user`	| `midimonster`		| none			| User for the remote session (GrandMA2)			|
| `password`	| `midimonster`		| `midimonster`		| Password for the remote session				|

#### Channel specification

Currently, three types of channels can be assigned

##### Executors

* For the GrandMA2, executors are arranged in pages, with each page having 90 fader executors (numbered 1 through 90)
	and 90 button executors (numbered 101 through 190).
	* A fader executor consists of a `fader`, two buttons above it (`upper`, `lower`) and one `flash` button below it.
	* A button executor consists of a `button` control.
* For the dot2, executors are also arranged in pages, but the controls are non-obviously numbered.
	* For the faders, they are right-to-left from the Core Fader section (Faders 6 to 1) over the F-Wing 1 (Faders 13 to 6) to
	F-Wing 2 (Faders 21 to 14).
	* Above the fader sections are two rows of 21 `button` executors, numbered 122 through 101 (upper row) and 222 through 201 (lower row),
		in the same order as the faders are.
	* Fader executors have two buttons below them (`upper` and `lower`).
	* The button executor section consists of six rows of 18 buttons, divided into two button wings. Buttons on the wings
		are once again numbered right-to-left.
		* B-Wing 1 has `button` executors 308 to 301 (top row), 408 to 401 (second row), and so on until 808 through 801 (bottom row)
		* B-Wing 2 has 316 to 309 (top row) through 816 to 809 (bottom row)

When creating a new show, only the first page is created and active. Additional pages have to be created explicitly within
the console before being usable.

These controls can be addressed like

```
mw1.page1.fader5 > mw1.page1.upper5
mw1.page3.lower3 > mw1.page2.flash2
```

A button executor can likewise be mapped using the syntax

```
mw1.page2.button103 > mw1.page3.button101
mw1.page2.button803 > mw1.page3.button516
```

##### Command line buttons

Command line buttons will be pressed when the incoming event value is greater than `0.9` and released when it is less than that.
They can be mapped using the syntax

```
mw1.<button-name>
```

The following button names are recognized by the backend:

| Supported	| Command	| Line		| Keys		|			|				|		|
|---------------|---------------|---------------|---------------|-----------------------|-------------------------------|---------------|
| `SET`		| `PREV`	| `NEXT`	| `CLEAR`	| `FIXTURE_CHANNEL`	| `FIXTURE_GROUP_PRESET`	| `EXEC_CUE`	|
| `STORE_UPDATE`| `OOPS`	| `ESC`		| `OFF`		| `ON`			| `MA`				| `STORE`	|
| `0`		| `1`		| `2`		| `3`		| `4`			| `5`				| `6`		|
| `7`		| `8`		| `9`		| `PUNKT`	| `PLUS`		| `MINUS`			| `THRU`	|
| `IF`		| `AT`		| `FULL`	| `HIGH`	| `ENTER`		| `ASSIGN`			| `LABEL`	|
| `COPY`	| `TIME`	| `PAGE`	| `MACRO`	| `DELETE`		| `GOTO`			| `GO_PLUS`	|
| `GO_MINUS`	| `PAUSE`	| `SELECT`	| `FIXTURE`	| `SEQU`		| `CUE`				| `PRESET`	|
| `EDIT`	| `UPDATE`	| `EXEC`	| `GROUP`	| `PROG_ONLY`		| `SPECIAL_DIALOGUE` 		| `SOLO`	|
| `ODD`		| `EVEN`	| `WINGS`	| `RESET` 	| `layerMode`		| `featureSort`			| `fixtureSort`	|
| `channelSort`	| `hideName`	|		|		|			|				|		|

Note that each Web Remote connection has it's own command line, as such commands entered using this backend will not affect
the command line on the main console. To do that, you will need to use another backend to feed input to the MA, such as
the ArtNet or MIDI backends.

#### Known bugs / problems

To properly encode the user password, this backend depends on a library providing cryptographic functions (`libssl` / `openssl`).
Since this may be a problem on some platforms, the backend can be built with this requirement disabled, which also disables the possibility
to set arbitrary passwords. The backend will always try to log in with the default password `midimonster` in this case. The user name is still
configurable.

This backend is currently in active development. It therefore has some limitations:

* It outputs a lot of debug information
* It currently is write-only, channel events are only sent to the MA, not generated by it
* Fader executors (and their buttons) seem to work, I haven't tested button executors yet.
* Command line events are sent, but I'm not sure they're being handled yet
* I have so far only tested it with GradMA2 OnPC
