# gamepads_linux

Gamepad input for ivi-homescreen, over the `xyz.luan/gamepads` channel that the
[`gamepads`](https://pub.dev/packages/gamepads) package speaks. An app depends on
`gamepads` as usual and gets this underneath.

## Why the shell needs it

The shell reads input through libinput, which deliberately ignores joysticks and
gamepads — it serves pointers, keyboards, touch and tablets. Under the Wayland
backends input arrives from the compositor, and Wayland has no gamepad protocol
either. So a pad cannot arrive through either path the shell already has.

## Why SDL3, and not /dev/input/js*

`gamepads_linux` on pub is a GTK plugin — unusable here — and it reports the raw
kernel joystick index as the key. That index is per-device: button 0 on one pad
is not button 0 on another, leaving every app to carry its own mapping table.

SDL3 already carries that table, and its enum *is* Godot's: `SOUTH` is 0 as
`JoyButton` A is 0, the d-pad is 11/12/13/14 in both, the sticks are axes 0–3
and the triggers 4/5. Godot uses SDL's controller database, so a Godot project's
`button_index` and `axis` numbers already mean SDL's — a port gets the numbers
its InputMap was written against, with nothing to translate.

SDL3 also brings hot-plug and rumble. Rumble is not wired up yet; the channel
contract has no method for it.

## The wire

`gamepads` documents `key` as "a platform-dependant identifier". This reports
SDL's numbering as a decimal string — `"0"` for SOUTH, `"11"` for DPAD_UP — and
`type` as `"button"` or `"analog"`. Axis values are `Sint16` handed across
unscaled, matching pub's Linux implementation in letting the app divide.

Divide by 32767 and the two ranges fall out on their own: the four stick axes
are bipolar and land on -1..1, while SDL rescales the two trigger axes (4 and 5)
to 0..32767 before we ever see them, so they land on 0..1. A resting trigger
reads 0, not -32768. Godot's `JoyAxis` reports the same two ranges, so a
transliterated Godot project needs no special case here.

That is deliberately **not** what `gamepads_linux` on pub reports for the same
pad. The divergence is the point: pub's plugin emits raw evdev indices, which is
why its `LinuxMapping` needs a per-VID/PID database to interpret them. SDL has
already applied its controller database by the time we read a button, so the
indices are the abstract ones — which is also why this reports no `vendorId` or
`productId`. Consume `Gamepads.events` and map the indices directly; feeding
these into `Gamepads.normalizedEvents` would run an evdev-shaped mapping over
values that are not evdev.

## Permissions

SDL opens the kernel's input nodes, which usually belong to the `input` group. A
shell that is not in that group finds no pads and says so once.
