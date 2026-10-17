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
passes, so the walk settles on it. WinMM has no notion of focus, so a copy behind the other keeps
reading its own pad. The log lists every id that answers at startup, with the pad's name.

## Testing status

- Two copies started side by side on one machine; the second logged the `wKerne2` rename and both
  reached the title screen.
- Both import table entries replaced, on both copies. Not yet played with two pads.

## What it does not cover

`controller_input`'s right stick look, Start and trigger roll are synthesised with `SendInput`,
which only reaches the window in front; a copy behind another one moves, attacks and jumps from its
pad but does not look with the right stick, pause or roll from it.
