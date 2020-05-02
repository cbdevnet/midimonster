### The `wininput` backend

This backend allows using the mouse and keyboard as input and output channels on a Windows system.
For example, it can be used to create hotkey-like behaviour (by reading keyboard input) or to control
a computer remotely.

As Windows merges all keyboard and mouse input into a single data stream, no fine-grained per-device
access (as is available under Linux) is possible.

#### Global configuration

This backend does not take any global configuration.

#### Instance configuration

This backend does not take any instance-specific configuration.

#### Channel specification

The mouse is exposed as two channels for the position (with the origin being the upper-left corner of the desktop)

* `mouse.x`
* `mouse.y`

as well as one channel per mouse button

* `mouse.lmb`: Left mouse button
* `mouse.rmb`: Right mouse button
* `mouse.mmb`: Middle mouse button
* `mouse.xmb1`: Extra mouse button 1
* `mouse.xmb2`: Extra mouse button 2

All keys that have an [assigned virtual keycode](https://docs.microsoft.com/en-us/windows/win32/inputdev/virtual-key-codes)
are mappable as MIDIMonster channels using the syntax `key.<keyname>`, with *keyname* being one of the following specifiers:

* One of the keynames listed below (e.g., `key.enter`)
* For "simple" keys (A-z, 0-9, etc), simply the key glyph (e.g. `key.a`)
* A hexadecimal number specifying the virtual keycode

Keys are pressed once the normalized event value is greater than `0.9`, and released if under that.

The following keynames are defined in an internal mapping table:

| Key name			| Description		| Key name			| Description		|
|-------------------------------|-----------------------|-------------------------------|-----------------------|
| `backspace`			|			| `tab`				|			|
| `clear`			|			| `enter`			|			|
| `shift`			|			| `control`			|			|
| `alt`				|			| `capslock`			|			|
| `escape`			|			| `space`			|			|
| `pageup`, `pagedown`		|			| `end`				|			|
| `home`			|			| `pause`			|			|
| `numlock` 			|			| `scrolllock`			|			|
| `insert`			|			| `delete`			|			|
| `printscreen`			|			| `up`, `down`, `left`, `right`	|			|
| `select`			|			| `print`			|			|
| `execute`			|			| `help`			|			|
| `apps`			|			| `sleep`			|			|
| `num0` - `num9`		|			| `multiply`			|			|
| `plus`			|			| `comma`			|			|
| `minus`			|			| `dot`				|			|
| `divide`			|			| `f1` - `f24`			|			|
| `lwin`, `rwin`		|			| `lshift`, `rshift`		|			|
| `lctrl, `rctrl`		|			| `lmenu`, `rmenu`		|			|
| `previous`, `next`		| Browser controls	| `refresh`			| Browser controls	|
| `stop`			| Browser controls	| `search`			| Browser controls	|
| `favorites`			| Browser controls	| `homepage`			| Browser controls	|
| `mute`			|			| `voldown`, `volup`		|			|
| `nexttrack`, `prevtrack`	|			| `stopmedia`, `togglemedia`	|			|
| `mediaselect`			|			| `mail`			|			|
| `app1`, `app2`		|			| `zoom`			|			|

Example mappings:
```
generator.x > wi1.mouse.x
input.a > wi1.key.a
input.X > wi1.key.escape
```

#### Known bugs / problems

Keyboard and mouse input is subject to UIPI. You can not send input to applications that run at a higher
privilege level than the MIDIMonster.

Some antivirus applications may detect this backend as problematic because it uses the same system
interfaces to read keyboard and mouse input as any malicious application would. While it is definitely
possible to configure the MIDIMonster to do malicious things, the code itself does not log anything.
You can verify this by reading the backend code yourself.
