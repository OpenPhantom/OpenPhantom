# multi_instance

**Produces:** `multi_instance.dll`, goes in `mods\`. Not part of the release: a session needs
`AllowMods=multi_instance` in `[multiplayer]` on every machine that loads it.

Two or more copies of the game on one machine, each in its own folder, for split screen: each copy
gets its own window and reads its own pad.

## Configuration: `[multi_instance]`

| Key | Default | Meaning |
|---|---|---|
| `Enabled` | `1` | `0` leaves the game refusing a second copy |
| `Joystick` | `-1` | the WinMM joystick id this copy reads, 0 to 15. `-1` lets the game choose the first that answers |
| `XInputSlot` | `-1` | 0 to 3: answer the game's joystick from this XInput pad instead, in front or behind. Overrides `Joystick` |

## An Xbox pad behind another window

Windows sends an Xbox pad's WinMM/DirectInput reports only to the process in front; behind, every
axis reads centred and no button is down (measured 2026-11-11: XInput showed the stick off centre
while WinMM read exactly 32767 on every axis). The engine's buttons and turning come through WinMM,
so the copy behind lost them. `XInputSlot` replaces `joyGetPosEx` and `joyGetDevCapsA` with
`xinput_joy.c`, which lays the XInput state out the way the pad looks through WinMM in front and
copies its capabilities. While the window is in front the real WinMM reading still answers, and the
first twelve disagreements are logged, which is how the layout is checked in the field.

A read that fails answers a centred pad rather than `JOYERR_UNPLUGGED`: the engine's poll clears its
enabled and present flags on one failure (`0x0048E15B`) and never sets them again, which left a
Bluetooth pad that dropped out for a moment (error 1167, fourteen times in one session) walking
forward and back and nothing else for the rest of the level.

## The keyboard

`stdControl_openKeyboard` (`0x0048D9E8`) sets no cooperative level, so DirectInput's default,
non-exclusive and background, lets every copy read every key. `keyboard_gate.c` drains the
keyboard's buffer before the engine's poll while this copy's window is behind, and releases the keys
still down when it went behind. It runs before the poll, so `controller_input`'s background roll,
written after the poll, is untouched.

## The window class

`wkernel_createWindow` (retail `0x00498F24`) starts with `FindWindowA("wKernel", NULL)` and, on a
hit, shows *Multiple instance detected* and returns. The class string is in `.data` and is the only
copy: the guard, `RegisterClassA` and `CreateWindowExA` all push the same address. A copy that
starts while another one's window stands changes the string's last character to the first digit
from `2` to `9` that no window carries yet, which renames the class for all three calls and lets the
guard pass. The first copy is never changed. No code byte is touched.

The pattern starts six bytes into the function, after the prologue `multiplayer.dll` detours, so the
two DLLs can load in either order. The decision is made when this DLL installs, before `WinMain`, so
a launcher has to wait for one copy's window before starting the next.

## The joystick

The engine's controller surface is `joyGetNumDevs`, `joyGetPosEx` and `joyGetDevCapsA`. At startup it
walks the ids and keeps the first that answers in one global (`0x008619DC`), so every copy on a
machine takes the same pad. `Joystick` replaces the game's two import table entries for
`joyGetPosEx` and `joyGetDevCapsA` with stand-ins that ask for the configured id whatever id the game
passes, so the walk settles on it. That chooses the pad but not focus: an Xbox pad goes dead behind
another window, see above. The log lists every id that answers at startup, with the pad's name.

## Testing status

- Two copies started side by side on one machine; the second logged the `wKerne2` rename and both
  reached the title screen.
- Both import table entries replaced, on both copies. Not yet played with two pads.

## The rest of the pad

`controller_input`'s right stick look, Start and trigger roll go through `SendInput`, which only
reaches the window in front. `[controller_input] BackgroundInput=1` answers them from inside the
game for a copy behind another one; see that module's README.
