### The `wininput` backend

This backend allows using the mouse and keyboard as input and output channels on a Windows system.
For example, it can be used to create hotkey-like behaviour (by reading keyboard input) or to control
a computer remotely.

As Windows merges all keyboard and mouse input into a single data stream, no fine-grained per-device
access (as is available under Linux) is possible.

#### Global configuration

This backend does not take any global configuration.

| Option	| Example value		| Default value		| Description				|
|---------------|-----------------------|-----------------------|---------------------------------------|
| `interval`	| `100`			| `50`			| Data polling interval in milliseconds. Lower intervals lead to higher CPU load. This value should normally not be changed. |
| `wheel`	| `4000 2000`		| `65535 0`		| Mouse wheel range and optional initial value. To invert the mouse wheel control, specify the range as a negative integer. As the mouse wheel is a relative control, we need to specify a range incoming absolute values are mapped to. This can be used control the wheel resolution and travel size. |
| `wheeldelta`	| `20`			| `1`			| Multiplier for wheel travel		|

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

The (vertical) mouse wheel can be controlled from the MIDIMonster using the `mouse.wheel` channel, but it can not be used
as an input channel due to limitations in the Windows API. All instances share one wheel control (see the section on known
bugs below). The mouse wheel sensitivity can be controlled by adjusting the absolute travel range, its initial value and
a wheel delta multiplier.

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
| `lctrl`, `rctrl`		|			| `lmenu`, `rmenu`		|			|
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

Joystick and gamepad controllers with up to 32 buttons and 6 axes plus POV hat can be mapped as inputs to the
MIDIMonster. When starting up, the MIDIMonster will output a list of all connected and usable game controllers.

Controllers can be mapped using the syntax

* `joy<n>.<axisname>` for axes, where `<n>` is the ID of the controller and `<axisname>` is one of
	* `x`, `y`: Main joystick / analog controller axes
	* `z`: Third axis / joystick rotation
	* `r`: Fourth axis / Rudder controller / Slider
	* `u`, `v`: non-specific fifth/sixth axis
* `joy<n>.button<b>` for buttons, with `<n>` again being the controller ID and `b` being the button number between
	1 and 32 (the maximum supported by Windows)

Use the Windows game controller input calibration and configuration tool to identify the axes and button IDs
relevant to your controller.

For button channels, the channel value will either be `0` or `1.0`, for axis channels it will be the normalized
value of the axis (with calibration offsets applied), with the exception of the POV axis, where the channel value
will be in some way correlated with the direction of view.

Example mappings:
```
input.joy1.x > movinghead.pan
input.joy1.y > movinghead.tilt
input.joy1.button1 > movinghead.dim
```

#### Known bugs / problems

Joysticks can only be used as input to the MIDIMonster, as Windows does not provide a method to emulate
Joystick input from user space. This is unlikely to change.

Keyboard and mouse input is subject to UIPI. You can not send input to applications that run at a higher
privilege level than the MIDIMonster. This limitation is by design and will not change.

Due to inconsistencies in the Windows API, mouse position input and output may differ for the same cursor location.
This may be correlated with the use and arrangement of multi-monitor desktops. If you encounter problems with either
receiving or sending mouse positions, please include a description of your monitor alignment in the issue.

Some antivirus applications may detect this backend as problematic because it uses the same system
interfaces to read keyboard and mouse input as any malicious application would. While it is definitely
possible to configure the MIDIMonster to do malicious things, the code itself does not log anything.
You can verify this by reading the backend code yourself.

Since the Windows input system merges all keyboard/mouse input data into one data stream, using multiple
instances of this backend is not necessary or useful. It is still supported for technical reasons.
There may be unexpected side effects when mapping the mouse wheel in multiple instances.
