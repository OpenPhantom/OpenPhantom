# xidi_bridge

**Produces:** `xidi_bridge.dll` -> `mods\`

Points the game's three WinMM joystick imports at the controller wrapper, because Windows will not
let the game reach it the ordinary way.

## Supported executables

Any build that imports `joyGetNumDevs`, `joyGetPosEx` and `joyGetDevCapsA` from `winmm.dll` and
still carries its import name table. No byte pattern and no address is involved: the import
directory is read out of the executable's own PE header, so this is version agnostic and unaffected
by ASLR. If a name is missing, that one is left alone and the log says which.

## Configuration: `[xidi_bridge]`

| Key | Default | Meaning |
|---|---|---|
| `Enabled` | `1` | |
| `Library` | `xidi_winmm.dll` | File name of the controller wrapper, beside the executable |

`Library` is a file name, not a path. It is deliberately not `winmm.dll`, which is the name Windows
redirects.

## The defect

Wrapping a system library by putting a stand-in beside the executable works because the loader
searches the application's own directory before the system directory. For this executable it does
not. The module list of the running game reads:

```
winmm.dll     C:\WINDOWS\SYSTEM32\WINMM.dll
Xidi.32.dll   not loaded
DDRAW.dll     <game>\DDRAW.dll
DINPUT.dll    <game>\DINPUT.dll
DSOUND.dll    <game>\DSOUND.dll
mss32.dll     <game>\mss32.dll
```

Every other contested name comes out of the game folder. That one does not, the wrapper is never
mapped, and the game asks a WinMM that knows nothing about XInput. It then reports that no
controller is connected.

It is not the ordinary search order. A probe executable importing the same three functions, built
and run from that same folder, is answered by the wrapper. Ruled out one at a time, each by a
control run: the file name, the DPI compatibility layer, the PE version fields, bound imports, a
manifest, the KnownDLLs list, the graphics wrapper starting up first, our own libraries, and both
the file and the directory form of DotLocal redirection.

The compatibility engine is in the process (`apphelp.dll` and `AcGenral.DLL` are both mapped) and a
compatibility fix for this executable is independently visible elsewhere, where it intercepts
`RegisterRawInputDevices` and fails it. Which database entry redirects the name is not proven, and
this fix does not need it to be: it sidesteps the name.

## The fix

The wrapper is installed under a name nothing redirects and loaded from here by full path. Three
pointers in the import address table are then rewritten to its own exports:

| Import | What the game reads |
|---|---|
| `joyGetNumDevs` | how many joystick slots exist |
| `joyGetPosEx` | axes, buttons and hat |
| `joyGetDevCapsA` | the device's ranges and capabilities |

Those three are the entire joystick surface the executable imports. The other six names it takes
from WinMM are four `aux` entry points, `mciSendCommandA` and `timeGetTime`.

Rewriting three slots is also the better arrangement in its own right. Miles takes 35 names out of
WinMM, iMUSE six and Bink one; replacing the library puts a wrapper in front of the whole audio
engine and makes all 43 calls depend on it forwarding faithfully. This way they keep reaching the
system library directly.

## Limitations

* The wrapper has to be installed, under the name in `Library`. Without it this does nothing and
  says so, and the game behaves exactly as it did before.
* Only the joystick is bridged. Nothing here touches audio, timing or the media control interface.
* The game reads all three imports and gives up on the first that fails, so a partial redirect is
  reported as a warning and leaves controller support off rather than half on.
* Detecting a joystick is not the same as being able to use one. The engine binds gameplay actions
  to a controller from its own configuration, so a fresh installation may still need the controller
  set up on the game's own controls screen.

## Why this is safe to repeat

Each slot is validated before it is written: the address in it must lie inside the module answering
to `winmm.dll` at that moment. After a successful run it holds an address inside the wrapper, so a
second run finds a value outside that range and declines. The same check refuses to fight anything
else that has already redirected these imports.

Where the game did reach a `winmm.dll` from its own folder, on a machine without the redirection,
nothing is rewritten. That case is recognised from the path of the loaded module rather than
guessed at.

## Testing status

Built and linked, `/W4 /WX` clean. The defect it addresses was measured in the running game: the
module list above is from a live process, and the same folder answers a probe executable with the
wrapper. **The fix itself has not yet been accepted in game.**
