# fmv_player

**Produces:** `fmv_player.dll` -> `mods\`

Replaces the pre-rendered movies' whole playback path, not just their scale. This is the fourth
design tried here, and each of the first three taught something the next one kept.

1. `fmv_scaling.dll` (no longer in this tree) resampled Bink frames into the engine's own
   DirectDraw surface through three different APIs, and measured the same frame rate through all
   three on the reporting machine - the cost was not in which API touched the engine's surface, it
   was in touching it at all, on that machine's DirectDraw-to-Direct3D translation layer
   (`dxwrapper`). That is the reason every design since routes around the surface entirely instead
   of trying to make touching it cheaper.
2. Windows' own Media Foundation (`MFPlay`), in a borderless window inside the game's own process,
   flickered black for the whole length of every movie. Five fixes for that, each chasing a real
   hypothesis, none of them the actual cause: re-asserting `WS_EX_TOPMOST` every 200 ms; minimizing
   the game window outright; a real `WM_ERASEBKGND` bug (fixed, not the cause); dropping
   `WS_EX_TOPMOST` and all Z-order reassertion; filtering the game window's own messages out of the
   shared message loop.
3. The same `MFPlay` code, moved into a separate process (`fmv_player_host.exe`, no longer in this
   tree) launched per movie, on the theory that `MFPlay`'s Direct3D9Ex/EVR device sharing a process
   with `dxwrapper` was the constant across all five in-process fixes. Getting the overlay to render
   above the game at all in a separate process needed the game window's `HWND` passed across the
   process boundary as the overlay's *owner*, which surfaced an unrelated real bug (blocking the
   game's own thread with `WaitForSingleObject(..., INFINITE)` while another process created a
   window owned by its window deadlocked the whole desktop once, not just the game). Once fixed, the
   overlay rendered on top, but taking foreground on a monitor-sized window made Windows' shell treat
   it as switching to a different fullscreen app and auto-minimize the game - a minimize that
   persisted, "random" and then immediate, through dropping `SetForegroundWindow` and then
   `WS_EX_NOACTIVATE`. Separately, standalone VLC playing the exact same converted file showed zero
   flicker from the start, which settled that `MFPlay` itself, not the process boundary, was the
   flicker's real cause - swapping it for libVLC (`vlc_playback.c`) fixed the flicker immediately,
   still running as a separate process. Reading `dxwrapper`'s own source (it is open source; this
   was checked directly) then settled the minimize too: this game gets a real exclusive-mode
   Direct3D9 device by default, translated from its own DirectDraw `DDSCL_EXCLUSIVE` request, and
   exclusive-mode devices auto-minimize on `WM_ACTIVATEAPP(deactivate)` as a decades-old, fundamental
   part of the D3D9 runtime - unrelated to and unaffected by Windows' Fullscreen Optimizations
   (confirmed by disabling that setting for `WMAIN.EXE` directly: no change). `dxwrapper` has a
   windowed-mode override that avoids this by never requesting exclusive mode
   (`EnableWindowMode=1`), confirmed via its own log to eliminate the minimize - but at a real cost:
   this game switches its own internal DirectDraw resolution between menus (640x480) and gameplay
   (its full configured resolution) constantly, something true exclusive fullscreen never exposed
   because the GPU always scales the backbuffer to fill the physical screen regardless - windowed
   mode instead physically resizes the actual window on every such change, landing on stale
   intermediate sizes. A structural mismatch with this specific game, not a misconfiguration, and it
   was reverted.
4. **The current design.** `WM_ACTIVATEAPP` is specifically a *cross-process* signal - Windows sends
   it when a window belonging to a different process becomes relevant, which `fmv_player_host.exe`
   always was, regardless of `WS_EX_NOACTIVATE` (which governs keyboard activation, not process
   identity). Moving the overlay back in-process, now that libVLC rather than `MFPlay` is what
   actually renders into it, removes that signal at the root: Windows does not send
   `WM_ACTIVATEAPP` for a window becoming topmost within its own process, and that is the only
   defense needed. An earlier version of this file also had `vlc_playback_play_blocking()` pump with
   `hWnd=NULL` and drop, unread, whatever arrived for the game window while a movie played - a
   second, redundant guard against the same cross-process signal this in-process design already
   cannot produce. It cost more than it bought: `PM_REMOVE` takes a message off the queue whether or
   not it is dispatched, so every `WM_SETCURSOR`, `WM_ACTIVATE` and `WM_MOUSEMOVE` meant for the game
   window during a movie was silently discarded rather than deferred. The pump is now scoped to the
   overlay window's own handle, so the game window's queue is never touched here at all. This alone
   was not the fix for the defect below - it is a real bug in its own right, fixed on its own
   merits - see the note there for what actually was.

**A field report reproduced a Windows "still starting" cursor stuck at a fixed point on screen,
present before the first movie ever played, invisible while a movie was actually playing, and back
once the movies were done - cleared only by an Alt-Tab.** The message-pump fix above improved a
related symptom (the pointer confinement into the menu, driven by the same kind of message traffic,
started working immediately instead of only after an Alt-Tab) but the stray cursor itself did not go
away, and removing `fmv_player.dll` from `mods\` alone made it stop, 100% reproducible either way.
Four more fixes were tried in sequence against the same field report, each compiled, installed and
live-tested rather than assumed fixed. Three were ruled out outright: making `video_overlay_init()`'s
libVLC load lazy instead of running unconditionally at install time (no change - this game's first
movie plays immediately, so lazy loading bought no real delay); having `overlay_window_proc()`
answer `WM_SETCURSOR` the same way the game's own window does (no change on its own); and
reconfiguring `dxwrapper` (`EnableWindowMode=1`) to avoid requesting an exclusive-mode Direct3D9
device at all (no change, then reverted). The fourth, moving the lazy load onto its own thread
(`vlc_playback_init_async()`, `_beginthreadex`, never waited on - `vlc_playback_is_ready()` /
`video_overlay_is_ready()` is a live, non-blocking poll, falling through to the retail Bink path for
whichever movie wants to play before that comes back true) was reasoned from a stall theory that
Task Manager then disproved directly: the game's process showed as Running, never Not Responding,
for the whole time the cursor was stuck, which is not what a genuinely blocked thread looks like.
That fix did not resolve the cursor either. It remains in place regardless, because it is a real
improvement on its own terms - the game's thread is now never blocked by this DLL's libVLC load, at
any point in a session, whatever else was or was not the actual cause here.

What finally settled it was a field report of a DIFFERENT symptom: launching with `fmv_player`
*disabled* (retail Bink movies) visibly flashes the screen several times before the game settles -
real minimize/restore cycles, consistent with this game's exclusive-mode Direct3D9 device reacting
to however Bink's own playback touches the display - while `fmv_player` *enabled* shows none of that
flashing at all, which is this DLL doing exactly what it was built to do (no flicker, no minimize).
Put together with the cursor's own timing - present before the first movie ever played, invisible
while the overlay was actually presenting video, back once it stopped - the shape of the defect
changed entirely: something early in startup, before either playback path ever runs, leaves the OS
cursor undone, and the retail path's own incidental flashing was quietly re-triggering a full
activation handshake a few times before the player ever reached the menu, curing it before it was
ever seen. This DLL's smooth window removes that incidental cure along with the flicker, which is
why the cursor problem only ever showed up with `fmv_player` installed even though its root cause was
never inside this DLL's own code.

**The actual fix, field-confirmed:** `SetForegroundWindow(game_window)` followed by
`SetCursor(NULL)`, both called immediately after `DestroyWindow()` in
`video_overlay_play_blocking()`, once per movie - at least twice in a typical session (the logo,
then whatever plays after it). This does not repair the underlying defect, which is not identified
and is not inside this DLL; it replaces the accidental cure the retail path's flashing used to
provide with a deliberate one, at the same point in the sequence.

**A second, separate defect surfaced once the OS cursor fix above was in and visible: the drawn
menu cursor itself, not the OS one, started appearing in the wrong place after the first two
movies.** `vlc_playback.c`'s message pump is scoped to the overlay window's own handle (a fix
covered above), so the game window's own `WM_MOUSEMOVE` messages are no longer eaten while a movie
plays - they simply queue up, unprocessed, for as long as the movie runs. Delivering that whole
backlog to the engine's own recentre hook (`cursor_anchor.c`) in one burst the moment the normal
pump resumed computed one large, spurious delta against its `(320, 240)` anchor, throwing the
drawn cursor off-screen. Draining that backlog (`PeekMessageW(..., WM_MOUSEFIRST, WM_MOUSELAST,
PM_REMOVE)` for the game window, discarding rather than dispatching) fixed that, but left the real
cursor wherever it had drifted to during the movie, which the menu's own first-frame setup reads
to seed the drawn cursor's position - landing it in the wrong place, self-correcting on the very
next real mouse move. A first attempt warped the real cursor to that same `(320, 240)` before
resuming, reasoning it was the engine's own "centre" - and made it worse: the drawn cursor now
reliably appeared at that literal point, the exact spot the earlier OS-cursor defect used to sit
at. `(320, 240)` is client `(640, 480)`'s own midpoint, only the real screen centre too when the
window is exactly that size, the engine's original 640x480 design; this window is the size of the
whole desktop (`FitWindowToMode` is off by default), and `MenuKeepsResolution` draws the menu
centred in *that* with `(W-640)/2, (H-480)/2`. The real screen centre, at any resolution, is the
window's own client centre, not the engine's hardcoded one. **Field-confirmed fix:** compute that
centre at runtime (`GetClientRect` + `ClientToScreen`) and warp there instead - correct at whatever
resolution the game is actually running.

A movie with no converted file falls straight through to the original Bink playback, unchanged.

## Configuration: `[fmv_player]`

| Key | Default | Meaning |
|---|---|---|
| `Enabled` | `1` | |
| `MovieDirectory` | `movies\_hd` | Where converted files live, relative to `WMAIN.EXE`. |
| `Extension` | `mp4` | The file extension to look for. |

For a movie named `movie\arena` (the retail engine's own relative name, no extension), this looks
for `<game>\<MovieDirectory>\arena.<Extension>` - flat, no extra `movie\` subfolder beyond
`MovieDirectory` itself, so converting your own files does not mean reproducing the retail path
structure. If that file is not there, the movie plays exactly as it always did, through Bink, at
whatever `enhanced_resolution.c` and the retail engine already do. Nothing here needs a single
file converted to be safe to install.

## Requires a 32-bit libVLC install

`vlc_playback.c` loads `libvlc.dll`/`libvlccore.dll` dynamically at runtime (`LoadLibraryW`, not a
linked SDK - see that file's own header comment for why) from a **32-bit** VLC install: this whole
project is 32-bit only, the same as the game itself, and a 32-bit process cannot load a 64-bit DLL
under any circumstances. It checks `%ProgramFiles(x86)%\VideoLAN\VLC\libvlc.dll` first, then the
VLC installer's own registry key for a non-default location. Most current VLC downloads default to
64-bit; if the only VLC on the machine is 64-bit, this DLL simply cannot find a usable one and falls
back to Bink for every movie, safely, the same as any other missing dependency here - installing a
separate 32-bit VLC build (VideoLAN ships both, and they can coexist) alongside an existing 64-bit
one is enough.

## Getting your own movies converted

This project ships no game assets, converted or otherwise; that rule does not change because the
files this DLL reads happen to be `.mp4` instead of `.bik`. `convert_movies.ps1`, in `src\tools\`
(`tools\`, alongside `WMAIN.EXE`, in an installed copy - every standalone script this project ships
lives there; unlike the mods, a `.ps1`/`.bat` pair has no build step, so its source is directly what
ships, one place to look regardless of which one you're after), is a **tool**, not
content: it reads the `.bik` files inside your own legally owned copy of the game and writes `.mp4`
next to nothing you didn't already have a license to. It needs [FFmpeg](https://ffmpeg.org/) -
FFmpeg's own decoder for Bink 1/2 (`libavcodec/bink.c`) is what makes this possible without RAD's
SDK - but not a manual install: if FFmpeg isn't already on `PATH`, the script downloads a portable
copy for you automatically (from gyan.dev, one of the Windows build sources FFmpeg's own site
recommends, since FFmpeg itself doesn't publish official Windows binaries) and caches it in
`tools\ffmpeg\`, so that only ever happens once.

**Simplest way to run it, no command line needed:** drag your game folder (the one `WMAIN.EXE` is
in) onto `Convert Movies.bat`, next to `convert_movies.ps1` in `tools\`. Double-clicking it with
nothing dragged onto it works too - it asks for the folder instead. Either way it then asks what
size to convert to (1080p, 1440p, 4K, the source's own original size, or a custom size you type
in - 1080p is preselected, so pressing Enter is enough if you don't have a preference), and, unless
you kept the original size, whether to letterbox (black bars, no distortion) or stretch to fill the
screen - the source is roughly 4:3, so either one is a real trade-off, not a default anyone should
have picked silently on your behalf. Both end the same way `convert_movies.ps1` always did: nothing
is touched outside the output folder, and it
never re-encodes a file that's already there unless told to.

The command line form still works too, for anyone who wants it, and skips both prompts (game
folder, size) whenever the corresponding parameter is given:

```powershell
.\convert_movies.ps1 -GameDirectory "C:\Path\To\The Phantom Menace"
```

writes into `<GameDirectory>\movies\_hd` by default, matching `MovieDirectory` above, upscaled to
1080p at a constant 60 fps (this is a real option, not just resampling: FFmpeg's Lanczos scaler run
once, offline, generally beats scaling the same frame every time it's shown, which is what the
game's own presentation would otherwise do - and a standard 60 fps plays back more reliably than
this source's own odd ~15 fps, which can judder on displays whose refresh rate isn't a clean
multiple of it, even though it doesn't invent any real motion the source didn't have). 1080p, not
the source's native ~640x405 and not 4K, is the resolution default deliberately: `fmv_player`'s
overlay window scales whatever resolution it's given to fill the screen either way, and this game's
Bink 1 movies were never rendered with anywhere near 1080p of real detail in the first place, so
going further roughly quadruples file size for a difference nobody will actually see. To keep the
source's own resolution and frame rate untouched instead:

```powershell
.\convert_movies.ps1 -GameDirectory "C:\Path\To\The Phantom Menace" -TargetWidth 0 -TargetHeight 0 -TargetFps 0
```

or pass any other `-TargetWidth`/`-TargetHeight`/`-TargetFps` for something else entirely;
`-Stretch` fills a 16:9 frame exactly instead of letterboxing the source's own ~4:3 into it.

It walks `GAMEDATA\MOVIE\*.BIK`, flattens each name (`ARENA.BIK` -> `arena.mp4`, matching how the
hook above looks them up), and skips anything already converted. Re-run it any time; it will not
re-encode a file whose output already exists.

**If you wrote or are running your own version of this script**: do not pipe `ffmpeg`'s output
through `2>&1 | Out-Null` under `$ErrorActionPreference = "Stop"` (this script's own default,
needed for everything else in it to fail loudly). FFmpeg writes its entire routine progress
display to stderr, not stdout; merging that into the error stream turns every normal progress line
into what PowerShell treats as a terminating error, and the script aborts on the very first file.
This cost an earlier version of this exact script a real debugging session before the fix was
simply not redirecting ffmpeg's output at all, which is what it does now. Pick your own encode
settings inside the script if the defaults still are not what you want - it is deliberately a
script, not a black box, so this is a starting point, not a mandate.

## Byte basis

All four movie call sites (intro/logo, in-level cutscenes, the arena replay, credits) funnel
through one function, confirmed by an xref sweep of its four `UNCONDITIONAL_CALL` callers inside
`0x0043EB2A`:

```
0043EB93  LEA EDX,[EBP-0x84]      ; a local buffer already filled with e.g. "movie\arena"
0043EB99  PUSH EDX
0043EB9A  CALL 0x0046C35A         ; the movie player - THIS is what this file detours
0043EB9F  ADD ESP,0xC             ; caller cleans 12 bytes: __cdecl, 3 arguments
```

`ADD ESP,0xC` after every one of the four call sites confirms the calling convention directly:
`int __cdecl(const char *name, int param2, int param3)`. `name` is a plain ANSI, null-terminated,
backslash-relative movie name with no extension (`"movie\arena"`, `"movie\scene1"`, ...), matching
the game's own real layout on disk - confirmed against a legitimate install:
`GAMEDATA\MOVIE\ARENA.BIK`. `param2` and `param3` govern details of the *retail* Bink path only (a
clear-and-present timing flag and an opaque per-call context cell); this DLL's hook never reads
either, because when it takes over it never runs any of the retail code that would have.

```
0046C35A  55                      push ebp
0046C35B  8B EC                   mov ebp,esp
0046C35D  81 EC 90 00 00 00       sub esp,0x90
0046C363  A1 F0 77 4B 00          mov eax,[004b77f0]
0046C368  89 85 74 FF FF FF       mov [ebp-0x8c],eax
0046C36E  C7 85 78 FF FF FF 01 00 00 00   mov [ebp-0x88],1
0046C378  83 3D 60 63 6D 00 00    cmp [006d6360],0
0046C37F  75 07                   jnz +7
0046C381  33 C0                   xor eax,eax
0046C383  E9 5C 01 00 00          jmp 0046c4e4
0046C388  83 3D 3C A4 86 00 00    cmp [0086a43c],0
0046C38F  74 07                   jz +7
0046C391  33 C0                   xor eax,eax
0046C393  E9 4C 01 00 00          jmp 0046c4e4
0046C398  C7 05 3C A4 86 00 01 00 00 00   mov [0086a43c],1
```

The bare `push ebp / mov ebp,esp / sub esp,0x90` prologue shape recurs elsewhere in an 830 KB
image, which is exactly why `common/signature.h`'s two-stage detour rule exists, and why this
signature reaches two branches into the function rather than stopping at the prologue. Measured
against the real retail `WMAIN.EXE` (829,952 bytes): exactly one match, all 72 bytes, at
`0x0046C35A`.

The detour uses `common/detour.h`'s chained-hook mechanism rather than a one-off call-site redirect
(unlike this DLL's first predecessor): this is a **function being replaced wholesale**, not one
call inside a larger function, and other DLLs in this tree already share this exact chaining
convention for functions more than one feature might reasonably want to sit in front of.

## Why a separate window instead of drawing into the game's own surface

Everything that made the first design slow was on the far side of a DirectDraw surface neither
`fmv_player.dll` nor `video_overlay.c` owns. A borderless window sized to match the game's own
client rect, owned by the game window (Windows keeps an owned window above its owner in Z-order
automatically - no `WS_EX_TOPMOST` needed) and `WS_EX_NOACTIVATE` (it never needs keyboard focus -
Escape-to-skip reads `GetAsyncKeyState`, physical key state, not per-window input), sidesteps that
surface entirely: libVLC renders into that window directly via `libvlc_media_player_set_hwnd`,
which is also why `video_overlay.c` has no Direct3D/DXGI/rendering code in it at all - libVLC
manages that itself.

## Known limitations

* **Escape skips a movie outright.** There is no seek, no pause, nothing else - `GetAsyncKeyState`
  is polled once per loop iteration inside `vlc_playback_play_blocking()` and any press ends
  playback immediately, the same as reaching the end of the file. If the retail game itself
  disables skipping on a specific movie, this does not know that and does not try to find out;
  every converted movie is skippable.
* **Audio does not route through the retail Miles Sound System.** libVLC plays its own audio track
  directly. This is not expected to matter (nothing else in the audio pipeline runs during a movie
  either way), but it is a genuinely different code path from retail, worth naming.
* **`param2`/`param3` are not replicated.** They govern retail-only details (see the byte basis
  above); a movie played through this DLL skips whatever they would otherwise have set up. No
  effect has been observed to depend on this, but it has not been proven absent either.
* **The overlay assumes the game has exactly one visible top-level window**, found via
  `EnumWindows` and NOT re-validated against any engine-specific signature. On a setup where that
  assumption does not hold (unusual wrapper window arrangements, for instance),
  `find_game_window()` may size the overlay against the wrong window, or find none at all, in which
  case `video_overlay_play_blocking()` returns false and the hook falls back to the original Bink
  path for that movie - the same fallback that runs when no converted file exists at all, or no
  32-bit libVLC install can be found.
* **No hardware certainty.** libVLC's own choice of decoder (hardware or software) is outside this
  project's control and depends on the codec the converted file actually uses and what the system
  offers. H.264 in particular has broad hardware decode support on anything from the last decade;
  an unusual codec choice in `convert_movies.ps1` may fall back to software decode, which is still
  very likely faster than the retail path was but is not guaranteed to be.

## Testing status

**Live-tested on the reporting machine through design 4 above: no flicker, no minimize, movies play
at full quality over the game window.** The message-pump fix was confirmed live to fix the
pointer-confinement half of the stray-cursor symptom. `SetForegroundWindow()` plus `SetCursor(NULL)`
after every movie (`video_overlay_play_blocking()`) is **field-confirmed live to fix the OS cursor
itself.** Three attempts before it - lazy `libvlc_new()`, `WM_SETCURSOR` handling on the overlay
window alone, and `dxwrapper`'s `EnableWindowMode=1` - were each compiled, installed and live-tested
in turn, and each ruled out when the symptom did not change; `vlc_playback_init_async()` (loading
libVLC on its own thread) also did not fix the cursor on its own, though it remains in place as a
genuine improvement - the game's thread now never blocks on that load regardless. The follow-on
drawn-cursor defect (see the design-history section above) is **also field-confirmed fixed**: the
stale-message drain plus a real-time client-centre warp (`GetClientRect` + `ClientToScreen`, not
the engine's hardcoded `(320, 240)`) puts the drawn cursor exactly where it should be after every
movie, confirmed correct on the reporting machine's own resolution.

What running down four wrong fixes established, worth keeping: the retail Bink path, not just this
DLL's overlay, was ALSO masking the same underlying defect. Task Manager showed the game's process as
Running throughout, never Not Responding, which ruled out Windows' shell "still starting" heuristic
directly. And launching with `fmv_player` disabled (retail Bink movies) visibly flashes the screen
several times before the game settles - real minimize/restore cycles, consistent with this game's
exclusive-mode Direct3D9 device reacting to however Bink's own playback path touches the display -
while `fmv_player` enabled shows none of that flashing at all, which is this DLL doing exactly what
it was built to do. The two facts together explain the paradox: something early in startup, before
either playback path ever runs, leaves the OS cursor undone, and the retail path's own incidental
flashing was quietly re-triggering a full activation handshake a few times before the player ever
reached the menu, curing it before it was ever seen. This DLL's smooth, flicker-free window removed
that incidental cure along with the flicker, which is why the cursor problem only ever showed up
with `fmv_player` installed even though its root cause was never inside this DLL's own code. The fix
above does not repair that root cause - it replaces the accidental cure with a deliberate one, once
per movie, which is already at least twice in every session (the logo, then whatever plays after
it). Where the actual defect lives (most likely the base loader or the window's very first
activation, well outside this DLL) is not identified and is not this DLL's problem to fix. The
detour signature is measured against the real retail
`WMAIN.EXE` (829,952 bytes), independently re-extracted from the executable's own `.text` section
with a standalone PE parser and counted for uniqueness: one match, at the address this file names.
`/W4 /WX` clean throughout. The window-management code in `video_overlay.c` and the libVLC-loading
code in `vlc_playback.c` have no engine dependency and therefore no byte evidence to verify the
same way, and have no behaviour a unit test can observe without a live window and a real video
file - they rest on documented Win32 window-ownership/activation behaviour and on libVLC's own
long-stable C ABI (checked directly against its source, not from memory, where this project's own
reasoning depended on it).
