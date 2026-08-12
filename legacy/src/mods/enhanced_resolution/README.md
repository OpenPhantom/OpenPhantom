# enhanced_resolution

**Produces:** `enhanced_resolution.dll` -> `mods\`

Modern resolutions, listed in the options screen as if they had always been there, and the window,
the mouse pointer and the input devices kept in step with them.

> The game is **16-bit only**; there is no 32-bit path anywhere in it. Which resolutions actually
> exist is therefore up to your DirectDraw layer (dgVoodoo2, DDrawCompat). `LogModeTable=1` prints
> the complete table the layer reported, so you can see rather than guess.

## Supported executables

Retail `WMAIN.EXE` (EN/DE) and the Fix Pack build. On `obi.exe` the mode-list patterns do not
resolve. What does resolve there, because those functions survived the recompile unchanged and
sit 0x60 lower, is the two cursor patterns (`0x46A0B5`, `0x46A0F5`), the two input-focus patterns
(`0x48D6B9`, `0x48D16F`), `graphics_setMode` (`0x46BC25`) and `stdDisplay_getModeSize`
(`0x4937FA`). So the window fit works on `obi.exe` too, minus the pre-call size lookup, which needs
the mode table that the aspect gate anchors.

## Configuration: `[enhanced_resolution]`

| Key | Default | Meaning |
|---|---|---|
| `Enabled` | `1` | |
| `FilterModeEnumeration` | `1` | keep the engine's 64 mode records for modes it can use: a mode that is not 16 bit RGB, or a resolution a usable record already holds, is answered without taking one. The engine cancels the enumeration when those 64 are gone, so on a driver reporting three bit depths two thirds of them were spent on modes the options screen can never show |
| `WidescreenModes` | `1` | lift the 4:3 lock in the mode list |
| `MaxMenuModes` | `63` | cap for the engine's 64-slot label array; 4-63 |
| `ForceWidth` / `ForceHeight` | `0` | 0 = leave `obi.ini` alone |
| `LogModeTable` | `0` | dump the raw DirectDraw table on the first enumeration. A diagnostic, so it is off in a release |
| `MenuKeepsResolution` | `1` | stop menus switching to 640x480 |
| `FitWindowToMode` | **`0`** | **last resort.** Move and size the window to match the display mode. Only for a setup with **no graphics wrapper at all**, where the window really can end up smaller than the mode. It costs the engine's window its position at screen (0,0), which is what its own pointer handling assumes. |
| `KeepCursorInWindow` | `1` | re-centre the mouse pointer in the window's client area instead of at screen (320,240) |
| `ClipPointerToWindow` | `1` | hold the pointer inside the client area while the game window is in front, and let go the instant it is not |
| `ReacquireInputOnFocus` | `1` | send the engine the input resume it authored and never sends, so the keyboard and mouse still work after an Alt-Tab |
| `WidenMenuCursorArea` | `1` | let the **drawn menu cursor** move over the whole display mode instead of the 607x447 island the engine clamps it to. Does **not** move or rescale any menu, the engine already centres those itself. **Reported cost, not reproduced here yet:** the pause screens repair themselves through damage rectangles clipped to the same hard-coded 640x480 canvas, so a cursor moved past the island's edge cannot be erased and may stamp its blue glow onto the border until the screen closes. Every clickable widget is inside the island either way, so set this to `0` if you see that |
| `ClampMenuSpritesToIsland` | `1` | the erase-side companion of `MenuKeepsResolution`: clamp the menu toolkit's sprite draws to the 640x480 island, gated on the engine's own widget-pass flag so the HUD and the frozen pause backdrop pass through untouched. Closes the reported blue stamp the hovered button's halo left on the island's border (drawn against the screen, repaired against the canvas). Bit-identical for every sprite that fits the island, and a sprite drawn with a partial fill is passed through untouched |

## Engine locations

| Site | Retail VA | What |
|---|---|---|
| the 4:3 gate in `graphics_buildModeList` | `0x46C6D9` | two bytes, `EB 32` -> the accept label |
| `graphics_enumModes` | `0x46C932` | detoured, 9-byte prologue |
| `graphics_setResolution` | `0x46BE3D` | detoured, 6-byte prologue, **only** when `ForceWidth`/`ForceHeight` are set |
| `graphics_setMode` | `0x46BC85` | detoured, 6-byte prologue, **only** when `FitWindowToMode=1`. The choke point every valid mode change passes through |
| `stdDisplay_getModeSize` | `0x49385A` | **called, never patched**, reads back the size that is really set |
| `swmenu_enterMenuMode` | `0x45F7AC` | operand repointed at a cell holding `0x7FFFFFFF` |
| `swmenu_modeIsUsable` | `0x45F686` | the same, and both must read the SAME cell |
| `g_aRawMode`, `g_numRawModes`, `mem_free` | gate -0xA7 / -0x12C / -0xF4 | read backwards from the gate, each opcode-checked |
| `control_recentreMouse` | `0x46A115` | detoured, 7-byte prologue, the pointer anchor |
| `control_captureMouse` | `0x46A155` | detoured, 8-byte prologue, the re-anchor on activation, and the operand its `call` carries yields the engine's window handle |
| `stdControl_setFocus` | `0x48D719` | **called, never patched**. Acquire/Unacquire on the DirectInput keyboard and mouse |
| `stdControl_resync` | `0x48D1CF` | **called, never patched**, drains both device buffers, releasing everything held |
| `swrle_windowProc`, the cursor clamp | `0x460C04`..`0x460C7C` | four immediates rewritten to `W-33`/`H-33` and two origin operands repointed at zero cells, **only** when `WidenMenuCursorArea=1`. 121-byte masked signature; in `obi.exe` it resolves at `0x460BA4` |
| the DirectDraw enumeration callback | `0x4928FC` | detoured, 6-byte prologue, **only** when `FilterModeEnumeration=1`. Address free: the mode counter, the 64 cap, the 0x54 stride and the table base are all read out of the matched operands and checked before use |
| `swmenu_render`, the widget-pass bracket | `0x45DC6F`..`0x45DCB9` | **read, never patched**: address-free masked pattern over `inc g_tickCounter / mov [flag],1 / cmp [parent],1`; the flag cell is read out of the `C7 05` operand and cross-checked against the closing `mov [flag],0` at +0x41. The gate for the island clamp. In `obi.exe` it resolves at `0x45DC0F`, with the flag cell at `0x008BFB40` instead of `0x008BFBA0` |
| `texture_drawSprite` | `0x0042963B` | detoured, 9-byte prologue, **only** when `ClampMenuSpritesToIsland=1`; chains with `hud_ratio_scaling`'s detour on the same function in either load order |

## Why the gate alone is not enough

`graphics_setResolution` never tested the aspect in the first place, a widescreen resolution in
`obi.ini` already worked. The lock only ever hid modes from the **menu**. And the menu label array
holds exactly 64 entries with **no bounds check**; the next live datum behind it is the menu
descriptor. A modern driver can enumerate more 16-bit modes than that, so the enumerator is wrapped
and capped, and the dropped labels are handed back to the engine's own allocator.

## Why `FitWindowToMode` is off by default

Everything else in this DLL patches the game and leaves the presentation alone, so it behaves the
same whichever wrapper renders afterwards. Moving the game's window is the one thing here that
argues with that wrapper over the same object, and it has cost twice:

1. it broke the engine's own pointer confinement (the section below), which is why `cursor_anchor.c`
   and `focus_guard.c` exist at all;
2. it produced a **2 fps** field report. The mode went down to 640x480 with the window, came back up
   to 2560x1440 without it, and the wrapper then downscaled a 1440p image into a 640x480 window on
   every frame. Invisible under the previous wrapper, which ran exclusive fullscreen.

So it is a last resort for a setup with no wrapper at all. Turn it on only if the window really is
smaller than the display mode, and expect the pointer features below to become load-bearing when you
do, the log says which of the two states it is in.

## When it is on: it follows the mode in BOTH directions, from `graphics_setMode`

The 2 fps defect was a **wrong hook site**, not a wrong correction. The fit used to be driven from
`graphics_setResolution 0x46BE3D`, which is **not** the choke point: eight `call rel32` sites reach
`graphics_setMode 0x46BC85` and only one of them (`0x46BF67`) is inside `graphics_setResolution`.
Both mode changes a player actually triggers live in one function, `swmenu_enterMenuMode 0x45F772`,
and it is asymmetric:

```
0045F7A7  call graphics_getWidth
0045F7AC  cmp  eax,[max_menu_Width]
0045F7C8  call graphics_setResolution(640,480)   ENTERING a menu: the mode goes DOWN
0045F7F4  call graphics_setMode(savedIndex)      LEAVING  a menu: the mode goes UP
```

plus the options screen's own apply, which calls `graphics_setMode` directly twice (`0x44162C`,
`0x44163C`). A fit driven from `graphics_setResolution` therefore **heard the mode go down and never
heard it come back up**, which is exactly what the field log shows.

Three details the new site needs, and all three are handled:

* **The size of the requested mode is read from `g_aRawMode[index]` *before* the call**, using the
  same stride `0x54` and the same `+8` / `+0xC` field offsets `graphics_setMode` itself uses. It has
  to be: the device is rebuilt inside `stdDisplay_setMode` and is created with whatever size the
  window has at that moment.
* **The return value is not the signal.** `0x46BCB6` returns 1 for a mode that was *already* set and
  changes nothing; a failed change leaves the previous mode standing. So the size is **read back**
  afterwards with `stdDisplay_getModeSize` and compared against the last size the window was fitted
  to. That covers all three exits and makes the hook idempotent on a retry.
* **The window is re-checked for a few frames**, because whatever renders for the game owns the same
  window and moves it during its own device rebuild.

## Why moving the window lets the mouse pointer escape

`control_recentreMouse 0x46A115` is handed the `lParam` of a `WM_MOUSEMOVE`, which Windows fills
with **client** coordinates. It compares that against (320, 240) and, when it does not match, warps
the pointer with `SetCursorPos(320, 240)`, which takes **screen** coordinates. That is the engine's
entire mouse confinement: warp back to the middle on every movement. It also never converts between
the two spaces, and it cannot: the import table has no `ClientToScreen`, no `GetClientRect`, no
`ClipCursor` and no `ShowCursor`.

The two spaces agree for exactly one window position, the screen origin, and that is where the
engine puts its own window (`CreateWindowExA` at `0x499019` passes X = 0, Y = 0 and style
`0x80000000`, `WS_POPUP`) and where it leaves it, because both of its own `SetWindowPos` calls carry
`SWP_NOMOVE`. **`window_fit.c` is the only code that moves that window, and only when
`FitWindowToMode=1`**, so this DLL is what makes the assumption false, and with the key at its new
default of `0` it does not make it false at all. On a second monitor whose origin is at -1920,0 the
warp target lands on the
*other* display, the "already centred" test can never be true, and the pointer comes to rest outside
the game window. Two field logs of the same build, one with `monitor at 0,0` and one with
`monitor at -1920,0`, differ in exactly that and in nothing else.

`cursor_anchor.c` converts the engine's own (320, 240) into screen coordinates with
`ClientToScreen` before warping, and leaves the comparison in client space where it belongs. It is
the identity on a window at the screen origin. It adds no `ClipCursor` and no `SetCapture` of its
own; there is nothing global to hold and nothing to release on exit, and it refuses to warp at
all while the foreground window belongs to another process, so Alt-Tab always frees the pointer even
where a graphics wrapper filters `WM_ACTIVATEAPP` before the engine sees it.

The pointer is not meant to be usable in the menus either: the front end draws its own cursor from
the accumulated deltas and the window procedure answers `WM_SETCURSOR` with `SetCursor(NULL)`
unconditionally (`0x4990D3`). So there is no screen on which the OS pointer should be released, and
the anchor makes no exception for one.

## Why the coordinate repair is not enough on its own

A warp only happens when a `WM_MOUSEMOVE` arrives, and one only arrives while the pointer is over
the window, or while a mouse button is down, which is the only case in which a window that holds
the capture still gets mouse input once the pointer is over another thread's window. Cross the edge
with no button held and the loop stops feeding itself: no message, no warp, pointer gone. The
warp target makes it easy, too, because it is client (320, 240), 320 pixels from the left edge,
with a second monitor beginning one pixel further left.

And the capture the engine takes on `WM_ACTIVATEAPP` is not reaching it. In the field log the
graphics wrapper writes `WndProc::Handler Warning: filtering WM_ACTIVATEAPP: 0` and `: 1`, so
`control_captureMouse 0x46A155` and `control_releaseMouse 0x46A17E` never run. The only `SetCapture`
that survives is the one-off at the end of the engine's input startup.

`focus_guard.c` therefore holds the pointer with `ClipCursor` while the game window is the
**foreground window**, and drops the rectangle the instant it is not. The signal is polled once per
frame from the frame hook rather than taken from a message, because a message can be filtered and
`GetForegroundWindow` cannot. The test is against the *window*, not the process, so an error box
the engine puts up releases the pointer too.

## Why Alt-Tab breaks the input, and what part of that is ours

`stdControl_openMouse 0x48DA7C` sets cooperative level 6, `DISCL_NONEXCLUSIVE | DISCL_FOREGROUND`
(`0x48DAEA push 6`). `stdControl_openKeyboard 0x48D9E8` sets none at all: it creates the device,
sets the data format and the buffer size, and there is no `call [edx+0x34]` in it. Windows
unacquires a foreground device by itself when the window goes to the background.

The only function that acquires is `stdControl_setFocus 0x48D719`, and none of its five callers can
be reached by a focus change:

* `0x48D195` / `0x48D1B6`, `stdControl_open` / `stdControl_close`, driven by module messages 3 and 4;
* `0x464A8E` / `0x464AA1`, the Control module's cases for messages **0x12** and **0x13**, which are
  `setFocus(0)` and `setFocus(1)` + `stdControl_resync`. Every one of the 24 `module_broadcast`
  `0x46F3C3` and 14 `module_broadcastDt` `0x46F4A9` call sites pushes its message id as an
  immediate, and the complete set is `{3,4,5,6,7,0x10,0x16,0x17,0x18,0x19}` and
  `{8,9,0x0C,0x0D,0x0E,0x11,0x15}`. **0x12 and 0x13 are sent by nobody.**
* `0x48D656`, inside `0x48D629`, which handles `WM_ACTIVATE` and would call `setFocus`. The
  little-endian dword `29 D6 48 00` **does not occur anywhere in the 829,952-byte image** and no
  `call`/`jmp rel32` targets it, so that handler is never registered and `WM_ACTIVATE` reaches
  nothing.

So the engine authored a suspend/resume pair for its input and shipped without a sender. After a
focus loss the devices are unacquired and nothing in the retail build ever acquires them again;
`stdControl_bAcquired [0x8619C8]` stays 1 while every `GetDeviceState` fails. `ReacquireInputOnFocus`
sends the two messages that are missing, by calling exactly the leaves the engine's own case `0x13`
calls, in the same order.

**What is not ours.** The engine does not pause when it loses focus and never did: the message pump
`0x498BCA` uses `PeekMessageA` with `PM_NOREMOVE` and returns when the queue is empty, and the frame
limiter around it spins on `Sleep(0)` (`0x475BC9`). It also never changes the display mode on a
focus change, `graphics_shutdownMode 0x46C4E8` has one caller, `0x43F5CD`, on the shutdown path.
The device loss and reset an Alt-Tab causes belong entirely to the DirectDraw-to-Direct3D9 wrapper:
in the field log it creates the device with `Windowed: 0`, i.e. exclusive fullscreen, and each
`Resetting device!` takes about half a second. If Alt-Tab is to cost nothing at all, that is a
wrapper setting, not a patch here.

## Timing

`graphics_buildModeList` runs during **graphics** startup. Everything here that has to influence
the list, the 4:3 gate above all, must therefore be patched before that, which is why the loader
triggers at the host's entry point and not at `DirectInputCreateA`. When the gate is lifted after
the list has been built, the patch reports success and has no effect: the options screen then
offers only the 4:3 modes the DirectDraw layer happens to report. On one machine that was exactly
one entry, 800x600.

`hook_enum_modes` logs every mode it hands to the options screen, unconditionally. That line is
what tells the two cases apart, and it is not behind a verbose flag for that reason.

## Known limitations

* `MenuKeepsResolution=1` has a visible price: the front end, the pause screens and the loading
  screen become a 640x480 island in the middle of the picture (14.8 % of the area at 1080p), and the
  drawn menu cursor stays inside that box. What it buys is no full D3D9 device rebuild on every menu
  open and close; six of those in 22 seconds appeared in one user log, and the graphics wrapper
  hung inside exactly that rebuild.
* There is a **third** `SetCursorPos(320, 240)` at the end of the engine's input startup, inside a
  large function that is not detoured for one warp that happens once. Input startup runs after
  graphics startup, i.e. after the window has already been moved, so on a secondary monitor the
  pointer can sit outside the window from launch until the hand touches the mouse, the first
  movement then anchors it. A transient, not a standing defect.
* The anchor cannot help while the game is not the foreground application: that is deliberate, and
  it is what makes Alt-Tab work.
* The confinement is released by a **per-frame** poll, so there is a window of up to one frame
  between the foreground going away and the rectangle being dropped. Windows also drops a clip of
  its own accord when the foreground window changes, but that is not relied on here.
* If the game is killed rather than closed, `DLL_PROCESS_DETACH` does not run and the clip rectangle
  is left to the operating system to reset. This is the one release path this DLL cannot own.
* The monitor rule **changed**. It used to be "the smallest monitor that can still show the mode",
  which moved a 640x480 window from a 2560x1440 primary onto a 1920x1080 secondary and left it
  there; that is what put the window at -1920,0 in the field log. It is now, in order: the monitor
  the window is **already** on whenever that one can show the mode; then an exact size match; then
  the smallest that still fits. If nothing can show the mode the window is resized where it stands
  rather than teleported. `window_fit_choose_monitor()` is pure and `unittests/window_fit.c`
  enumerates the rule, including the regression above.
* The two menu-bolt patterns contain an absolute `.data` address. Under forced ASLR they stop
  resolving and the patch disables itself with a log line. Address-free variants were measured and
  rejected, 6 and 10 hits respectively.
* If a chosen mode fits no connected monitor, the wrapper has to scale, and that is where it once
  hung. The log warns; it does not stop you.

## Fallback behaviour

The window fit has five named branches and the log says which one was taken, including the branch
where it does nothing, because a feature that is silently absent reads exactly like one that is
silently broken:

| what failed | what happens |
|---|---|
| `FitWindowToMode=0` | nothing is hooked and the window is never touched. **Logged as an `info` line, not silence.** |
| `graphics_setMode` does not resolve | the window is not fitted at all and stays wherever it is. The rest of the DLL is unaffected; nothing else uses that site. Warned. |
| the detour cannot be installed | the same, as an error. |
| `g_aRawMode` / `g_numRawModes` did not resolve | the window can only be fitted **after** a mode change and not before it, so whatever renders may still read the old window size while rebuilding its device. Warned once. |
| `stdDisplay_getModeSize` does not resolve | the fit runs from the *requested* mode only, so a mode change the engine **rejects** would leave the window on a size that was never set. Warned. |
| the frame hook cannot be installed | the fit acts once at the mode change and does not follow up while the renderer rebuilds its device. Warned. |

The pointer anchor and the focus guard now also log **whether they are load-bearing**: with
`FitWindowToMode=0` nothing moves the window, so both are insurance and the log says so in those
words. Their behaviour is unchanged either way, and their defaults stay `1`, at window origin
(0,0) the anchor computes the identical warp and the clip rectangle is the whole desktop, so they
cost nothing there and they are what makes an unusual setup survivable.

The pointer anchor has four named branches and the log says which one was taken:

| what failed | what happens |
|---|---|
| `control_recentreMouse` does not resolve or cannot be detoured | no anchor at all; the pointer keeps the engine's behaviour and can leave the window. Warned. |
| the engine's window handle does not resolve | the anchor uses `GetActiveWindow()` instead, which inside the window procedure is the same window whenever the game is the active application. Warned. |
| `control_captureMouse` does not resolve or cannot be detoured | the pointer is still kept inside the window, but after an Alt-Tab back it is put there by the first mouse movement instead of immediately. Warned. |
| the window cannot be asked for its client origin at run time | that one warp falls through to the engine's own, once, with a single warning line, never one per message. |

The focus guard has three named branches and the log says which one was taken:

| what failed | what happens |
|---|---|
| the frame hook cannot be installed | **neither** the confinement nor the re-acquire is armed. A confinement nobody can observe losing the foreground would be held for the rest of the session, which is worse than the defect it fixes, and the re-acquire has the same single source of truth. Warned. |
| `stdControl_setFocus` does not resolve | the confinement still works; the input devices are not re-acquired, i.e. the engine's own behaviour. Warned. |
| `stdControl_resync` does not resolve | the devices are still re-acquired; what queued up while the game was away is not drained, so a key held across the switch can arrive as a fresh press. Warned. |
| `ClipCursor` refuses | the confinement is dropped and the pointer keeps the engine's own behaviour. Warned once, not once per frame. |

## Testing status

**Offline only. Nothing here has been observed running.**

`/W4 /WX` clean with zero compiler warnings; the DLL links and all six wired unit tests pass,
including the `focus_guard` test that enumerates all 64 input combinations of the release rule and
walks a whole session of Alt-Tabs, a Win key and a minimise asserting that no frame ever ends with
the pointer confined and the foreground gone.

`unittests/window_fit.c` is new and covers the monitor rule, 17 checks, including the -1920,0
regression itself. It is **not wired into `CMakeLists.txt` yet**; it was compiled and run by hand
under `/W4 /WX` against the built `engine_fixes_common.lib` and all 17 pass.

Offline signature verification passes on both retail builds. `SIG_SET_MODE_ENTRY` resolves
uniquely in **all three** builds including the recompiled `obi.exe` (`0x46BC25` there), and
so does `SIG_MODE_SIZE_ACCESSOR` (`0x4937FA` there), its two absolute `.data` operands are
wildcarded for exactly that reason. `obi.exe` still reports its expected 35 problems, unchanged.

## The menu cursor cage, and why the order of two writes matters

The engine already centres its menus. `g_menuOriginX [0x6CFD58] = (W-640)/2` and
`g_menuOriginY [0x6CFD5C] = (H-480)/2` are written at startup and on every mode change, and both
are **added** by `swwidget_draw` and by `swwidget_hitTest`, 18 references and 16 references, which
is what guarantees that what you see and what you can click cannot drift apart. **None of that is
touched.**

What is wrong is a different number: inside the window procedure the engine clamps the cursor it
*draws for its own menus* to `[originX, originX+0x25F] by [originY, originY+0x1BF]`, 607x447, i.e.
640 and 480 less the 32-pixel cursor quad. At 1920x1080 that is an island in the middle of the
screen the pointer cannot be moved out of. The cursor coordinates are absolute screen coordinates
while the hit test adds the origin to the **widget**, so widening the clamp needs no coordinate
work at all: a cursor outside the island simply hits nothing, exactly as it does today.

**The install is two writes and their order is forced.** The four clamp immediates go first and
the two origin operands second, because only one of the two partial states is survivable:

| after | cage | usable? |
|---|---|---|
| immediates only | `[origin, origin + W-33]` | yes, larger than shipped, everything reachable |
| operands only | `[0, 607]` with the widgets centred at (640,300) on 1080p | **no, not one widget reachable** |

A failure to repoint the operands therefore **rolls the immediates back** to the shipped 640x480
rather than leaving the two halves disagreeing.

**The signature contains the immediates it patches**, which is what identifies the block as the
640x480 cage in the first place. It therefore cannot resolve a second time: the addresses are cached
at install and the site is never re-resolved. If an earlier generation of this DLL is already in the
process and has already widened the cage, the resolve fails, and the log says *that*, rather than
claiming the engine was not recognised.

The refresh on a resolution change is a once-per-frame poll of the mode size, **not** a detour on
the engine's mode-change broadcast: the loading screen calls `swmenu_setSuppressModeSwitch`, and
`enterMenuMode` then returns *before* that broadcast. The clamp is recomputed absolutely from the
current width and height, never by adding a delta, so repeating it cannot drift.

At 640x480 the computed clamp is bit-identical to the constants the engine ships with. That is
asserted by a unit test and is the reason this may default to on.

**Testing status:** the arithmetic is unit-tested (25 checks). The patch itself has **not** been
run in the game.
