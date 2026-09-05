# fmv_player

**Produces:** `fmv_player.dll` -> `mods\`

Replaces the pre-rendered movies' whole playback path, not just their scale. This is the fourth
design tried here, and each of the first three taught something the next one kept.

1. `fmv_scaling.dll` (no longer in this tree) resampled Bink frames into the engine's own
   DirectDraw surface through three different APIs, and measured the same frame rate through all
   three on the reporting machine; the cost was not in which API touched the engine's surface, it
   was in touching it at all, on that machine's DirectDraw-to-Direct3D translation layer
   (`dxwrapper`). That is the reason every design since routes around the surface entirely instead
   of trying to make touching it cheaper.
2. Windows' own Media Foundation (`MFPlay`), in a borderless window inside the game's own process,
   flickered black for the whole length of every movie. Five fixes for that, each chasing a real
   hypothesis, none of them the actual cause: re-asserting `WS_EX_TOPMOST` every 200 ms; minimizing
   the game window outright; a real `WM_ERASEBKGND` bug (fixed, not the cause); dropping
   `WS_EX_TOPMOST` and all Z-order reassertion; filtering the game window's own messages out of the
   shared message loop.
3. The same `MFPlay` code, moved into a separate process launched per movie, on the theory that
   `MFPlay`'s Direct3D9Ex/EVR device sharing a process with `dxwrapper` was the constant across all
   five in-process fixes. Getting the overlay to render above the game at all across a process
   boundary needed the game's `HWND` passed over as the overlay's *owner*, which surfaced an
   unrelated real bug (blocking the game's own thread with `WaitForSingleObject(..., INFINITE)`
   while another process created a window owned by its window deadlocked the whole desktop once,
   not just the game). Once fixed, the overlay rendered on top, but taking foreground on a
   monitor-sized window made Windows' shell treat it as switching to a different fullscreen app and
   auto-minimize the game, a minimize that persisted, "random" and then immediate, through
   dropping `SetForegroundWindow` and then `WS_EX_NOACTIVATE`. Separately, standalone VLC playing
   the exact same converted file showed zero flicker from the start, which settled that `MFPlay`
   itself, not the process boundary, was the flicker's real cause; swapping it for libVLC
   (`vlc_playback.c`) fixed the flicker immediately, still running as a separate process, and that
   swap was a single-variable change: same process, same window, same file, different decoder.
   Reading `dxwrapper`'s own source (it is open source; this was checked directly) then settled the
   minimize too: this game gets a real exclusive-mode Direct3D9 device by default, translated from
   its own DirectDraw `DDSCL_EXCLUSIVE` request, and exclusive-mode devices auto-minimize on
   `WM_ACTIVATEAPP(deactivate)` as a decades-old, fundamental part of the D3D9 runtime, unrelated
   to and unaffected by Windows' Fullscreen Optimizations (confirmed by disabling that setting for
   `WMAIN.EXE` directly: no change). `dxwrapper` has a windowed-mode override that avoids this by
   never requesting exclusive mode (`EnableWindowMode=1`), confirmed via its own log to eliminate
   the minimize, but at a real cost: this game switches its own internal DirectDraw resolution
   between menus (640x480) and gameplay constantly, something true exclusive fullscreen never
   exposed because the GPU always scales the backbuffer to fill the physical screen regardless;
   windowed mode instead physically resizes the actual window on every such change, landing on
   stale intermediate sizes. A structural mismatch with this specific game, not a misconfiguration,
   and it was reverted.
4. **The current design.** `WM_ACTIVATEAPP` is specifically a *cross-process* signal; Windows
   sends it when a window belonging to a different process becomes relevant, which a separate host
   process always was, regardless of `WS_EX_NOACTIVATE` (which governs keyboard activation, not
   process identity). Moving the overlay back in-process, now that libVLC rather than `MFPlay`
   renders into it, removes that signal at the root: Windows does not raise `WM_ACTIVATEAPP` for a
   window becoming topmost within its own process.

   That is **one** reason, and this file used to claim two. The second was that excluding the game
   window from the message loop's dispatch would also stop `WM_ACTIVATEAPP` reaching its window
   procedure. It cannot. `WM_ACTIVATEAPP` is a *sent* message: the system delivers it straight to
   the target window procedure from inside `PeekMessageW` itself, and it never appears in the `MSG`
   the loop filters on. An Alt-Tab during a movie therefore does reach the engine's window
   procedure, re-entrantly, on the thread parked inside the movie call.

   And the exclusion turned out to cost more than it bought, which is the first fix below.

**The message pump no longer touches the game window's queue at all.** Excluding a window from
*dispatch* is not the same as leaving its messages alone: `PM_REMOVE` takes a message off the queue
whether or not it is then dispatched, so every `WM_MOUSEMOVE` the player made during a movie was
not deferred, it was **discarded**, and the engine came out of each movie having silently missed
all of it. The pump is now scoped to the overlay window's own handle, so the game's traffic simply
waits, in order, for the game's own pump to resume.

One thing had to survive that change. The overlay never takes activation, so a close request,
Alt+F4 as `WM_SYSKEYDOWN`/`VK_F4` or the close box as `WM_NCLBUTTONDOWN`/`HTCLOSE`, is addressed to
the *game's* window, which a scoped peek never retrieves. Left at that, the game could not be
closed until the movie ended, which for the credits is minutes and which the retail Bink path does
not do. So the game's queue is *looked at* with `PM_NOREMOVE` for exactly those two messages, and
only one of them is ever removed, immediately re-posted so the engine's own window procedure
honours it once the movie call returns. Nothing else on that queue is touched. `WM_QUIT` needs no
handling: it is a thread message with no window, a scoped peek never sees it, and it stays queued
for the game's own pump.

**libVLC loads on its own thread.** Locating a 32-bit VLC, loading two DLLs out of it and calling
`libvlc_new`, which initialises VLC's entire plugin system, is not fast, and doing it at install
time on the game's own thread puts that stall in the middle of the engine's start-up. It now runs
on a background thread started as early as this DLL is loaded, and nothing on the game's thread
ever waits for it: `video_overlay_is_ready()` is a non-blocking poll, and a movie that comes along
before the load finishes plays through Bink for that one movie. The first movie in this game plays
almost immediately, so on a cold start that is the expected outcome for the logo, and the movies
after it get the overlay. This was first written against a theory that the load was stalling the
process, which Task Manager then disproved directly; the process showed as Running throughout,
never Not Responding. It stays because it is a real improvement on its own terms, not because that
theory held.

**A stray OS "loading" cursor after the intro movies:** `SetForegroundWindow(game_window)` followed
by `SetCursor(NULL)`, both immediately after `DestroyWindow()`, once per movie. What explained it:
launching with `fmv_player` *disabled* visibly flashes the screen several times before the game
settles, real minimize/restore cycles consistent with this game's exclusive-mode Direct3D9 device
reacting to however Bink touches the display, while `fmv_player` *enabled* shows none of that,
which is this DLL doing exactly what it was built to do. Something early in start-up leaves the OS
cursor undone, and the retail path's own incidental flashing was quietly re-triggering a full
activation handshake and curing it before the player ever saw it. A smooth, flicker-free window
removes that accidental cure along with the flicker, which is why the symptom only ever appeared
with this DLL installed even though its root cause is not in this DLL. This does not repair that
cause; it replaces the accidental cure with a deliberate one. Where the actual defect lives, most
likely the loader or the window's very first activation, is not identified.

**The drawn menu cursor lands in the wrong place after the first movies.** This one only became
visible once the pump stopped eating the game's mouse messages, and two repairs were field-tested
wrong before the mechanism was understood. The first warped the real pointer to the engine's own
`(320,240)` client anchor; the drawn cursor then reliably appeared at that literal point rather
than at the menu's centre. The second warped to the window's actual client-rect centre computed at
run time, which worked once and landed wrong on a later launch with the game's resolution matching
the desktop both times.

The reason both were unreliable is that the position is an **accumulator**, not an absolute: the
window procedure does `g_menuCursorX += (client_x - 320)` and then clamps to the menu island, so a
synthetic move only ever adds a delta to whatever the cells already held, a value this DLL has no
way to know. The repair writes the two cells directly, to the middle of the island, with no message
and no prior state involved, after draining the mouse backlog that would otherwise be applied on
top of it. The four cells are found by pattern rather than hardcoded, and that is not ceremony:
they sit at different addresses in the Edit Tool's own recompile of this engine, so four constants
would have written into the wrong variables there. `menu_cursor_cells.c` carries the bytes and the
measurements.

A movie with no converted file falls straight through to the original Bink playback, unchanged, and
so does one that arrives before libVLC has finished loading.

## Configuration: `[fmv_player]`

| Key | Default | Meaning |
|---|---|---|
| `Enabled` | `1` | |
| `MovieDirectory` | `movies_hd` | Where converted files live, relative to `WMAIN.EXE`. |
| `Extension` | `mp4` | The file extension to look for. |
| `Scaling` | `letterbox` | `letterbox` keeps the movie's own shape and puts black bars where the screen differs. `stretch` fills the screen exactly. Anything else reads as `letterbox`. |
| `PostMovieHoldMs` | `2500` | How long the overlay stays up, still solid black, after a movie that was played once a level and its player already exist. `0` disables it. See "The post-movie curtain" below. |
| `PostMovieFadeMs` | `300` | How long the curtain then takes to fade from opaque to transparent before it is destroyed, instead of cutting straight to the game. `0` skips the fade. |
| `MutePostMovieAudio` | `1` | Suppress the one scripted sound call behind the curtain's own thud. Not a volume mute; dialogue and every other sound path are untouched. See "The post-movie curtain" below. |

`Scaling` is applied when the movie plays, not when it is converted, so it works on files you
already have and changing your mind costs nothing but a restart. Stretch is expressed as the
overlay window's own ratio rather than a fixed 16:9, so it stays exact on a 16:10, a 21:9 or a
rotated panel, where a hard-coded ratio would leave bars on one axis and crop on the other. The
export it needs is resolved as an optional one: a libVLC without it stays on letterbox and says so
once, rather than failing the whole load over a preference.

For a movie named `movie\arena` (the retail engine's own relative name, no extension), this looks
for `<game>\<MovieDirectory>\arena.<Extension>`; flat, with no extra `movie\` subfolder beyond
`MovieDirectory` itself, so converting your own files does not mean reproducing the retail path
structure. If that file is not there, the movie plays exactly as it always did, through Bink.

## What switches this feature off, and where to read it

Three things, checked in this order at startup, each of them saying so in `engine_fixes.log`. The
order is deliberate: the two cheap checks come before libVLC is loaded, so a machine that is never
going to play a converted movie does not pay for one during the game's own startup.

1. **The detour site does not resolve.** An unsupported build of the executable.
2. **`MovieDirectory` does not exist.** Nothing has been converted, so there is nothing to do. This
   is the normal state of a fresh installation and it is reported as information, not a warning.
3. **No 32-bit libVLC can be found.** See below.

After that, per movie: no converted file for this one, or playback that never started. Both are
logged with the path that was looked for, because "it silently kept using Bink" was the failure
mode this whole feature is most likely to present as.

## Where libVLC comes from

`vlc_locate.c` looks in three places, in this order:

1. **`<game>\mods\fmv\`**, the runtime the installer puts there. First because it is the only one
   whose version this project chose, so it is the only one a bug report can be reproduced against.
2. `%ProgramFiles(x86)%\VideoLAN\VLC`, where a 32-bit VLC installer puts itself by default.
3. The VLC installer's own registry key, `HKLM\SOFTWARE\VideoLAN\VLC`. `InstallDir` is read first
   because that is the value holding a directory; the key's unnamed default value, which VLC's
   installer writes as the full path of `vlc.exe`, is tried afterwards with its file name cut off.
   An earlier version of this fallback read that default value *as* a directory and therefore
   looked for `...\vlc.exe\libvlc.dll`, which meant it could never find anything.

It must be **32-bit**. This is a 32-bit process and a 32-bit process cannot load a 64-bit DLL under
any circumstances, an architecture wall rather than a version mismatch. Most current VLC downloads default
to 64-bit, which is exactly why the installer ships a 32-bit runtime rather than leaving it to the
machine. A player who installs the patch without that component, and has only a 64-bit VLC, gets
Bink for every movie and a log line saying why.

Nothing is linked against libVLC at build time. `LoadLibraryW` and `GetProcAddress` at run time
mean nobody building this project needs libVLC headers or an import library, and all ten exports
are required, so a libVLC that renamed or removed any of them switches this feature off rather than
calling something under a name it no longer means.

## Getting your own movies converted

This project ships no game assets, converted or otherwise; that rule does not change because the
files this DLL reads happen to be `.mp4` instead of `.bik`. `convert_movies.ps1`, in `src\tools\`
and in `tools\` in an installed copy, is a **tool**, not content: it reads the `.bik` files inside
your own legally owned copy of the game and writes `.mp4` next to nothing you did not already have
a license to.

**Simplest way, no command line:** drag your game folder onto `Convert Movies.bat`. Double-clicking
it works too; it asks for the folder instead.

It needs FFmpeg, and finds one in this order: a copy a previous run cached in
`%LOCALAPPDATA%\OpenPhantom\ffmpeg`, `ffmpeg` on `PATH`, and failing both it downloads one. That
download is **pinned to a specific version and checked against a SHA256** before it is extracted,
which is what makes a bug report reproducible: two players who hit the same problem are running the
same encoder.

A folder beside the script is deliberately **not** searched, and the reason is worth stating because
it looks like an omission. The game folder is made writable by ordinary users on purpose, because
the game keeps its saved games inside it and cannot run otherwise. An `ffmpeg.exe` found next to
this script would therefore be an executable that any user of the machine can replace, and the
installer offers to run this tool right after installing. Searching there would turn "I can write to
my own game folder" into "I can choose what runs next". `%LOCALAPPDATA%` belongs to one user.
For the same reason the installer runs the conversion as the ordinary user rather than with its own
elevated rights.

### The default is the source's own resolution, on purpose

Upscaling a ~640x405 Bink source to 1080p with Lanczos adds no detail. It mostly spends bitrate on
1999 compression artefacts, and the overlay scales whatever it is given to fill the window anyway.
The real win of this tool is getting *out of Bink*, not the resolution, so the default leaves the
picture and the frame rate exactly as they are. `-TargetHeight` is there if you want it.

### It never letterboxes into the file

When a height is asked for, the filter is `scale=-2:<height>:flags=lanczos` and there is no `pad`
after it. This matters more than it looks. An earlier version scaled into a fixed 16:9 box and
padded the rest black, which wrote the bars into the file as real pixels and **destroyed the
source's own aspect ratio**. libVLC then fitted that already-padded file into the overlay window a
second time, so on any display narrower than 16:9 the picture was boxed twice: on a 1920x1200
panel a 1080p conversion covered about 80% of the screen where a direct fit covers about 99%, and
on a 4:3 panel it was considerably worse.

With the padding gone there is exactly one aspect-ratio decision, and it is taken at playback, by
the only party that knows how big the screen actually is. That is also the whole answer to "make it
fill the monitor, and letterbox when the shape does not match": it already does, once, correctly.

## The post-movie curtain (`render_curtain.c`, `sfx_mute.c`)

Field report: right after the intro movies play going into level 6 (`fedship.b3d`, the Trade
Federation Battleship), Obi-Wan visibly drops into the map as if he had been suspended somewhere
higher, complete with a genuine falling/landing sound. Isolated live to this DLL; the retail Bink
path, which always forces a resolution switch around every movie, never shows it.

A live probe on the player's own body (`Plr_CommitPose`, `0x0044C06B`) caught the mechanism
directly: `pPlayer+0xA0` is a position-override flag that, while set, force-copies the player's real
position every substep from `pPlayer+0x124`. Watched frame by frame around the opening cutscene,
that source genuinely rises from the correct height to +1.48 units, overshoots back down to -0.04
below it, and settles exactly on the correct value again, a real, self-resolving engine transient
(a follow/settle filter driving the player toward its cutscene position, not a rendering artefact,
which is what a genuine landing sound during the same window requires), taking roughly 650 ms to
read flat once the cutscene lock fires, sometimes longer in practice. What retail's own resolution
switch buys, incidentally, is enough black-screen time that this always finishes before its picture
is ever shown again; this DLL's own faster transition does not spend that time, so the first real
frame can land mid-transient.

The curtain draws solid black for `PostMovieHoldMs` after the movie itself has already finished. It
does not block; the engine keeps ticking normally underneath, which is what actually gives the
transient somewhere to run to completion. It then fades out over `PostMovieFadeMs` instead of
cutting straight to the game. It is scoped to movies played once a level and a player already
exist: the two startup splash movies (`logo`, `bigape`) never get it, matched by the movie's own
retail name in `fmv_player.c`.

**It is drawn by the game's own renderer, not a separate window.** A first version held a borderless
overlay window open over the game instead of destroying it right away, which worked for a player
watching their own monitor and was confirmed live to be **invisible in a screen recording made
specifically to show it working**. Most "game capture" style recorders hook the game's own Present
call and grab the raw backbuffer before Windows' own compositor (DWM) ever draws a separate window
on top of it, so a window-based curtain reaches the player's eyes but never reaches a captured
frame. `render_curtain.c` instead redirects the same call `dev_overlay`'s own cheat panel redirects,
the one that closes the scene right before the page is shown, and draws a full-screen rectangle
through the engine's own filled-shape primitive (`0x00419660`, byte-identical to
`dev_overlay/overlay_sites.c`'s own `SIG_DRAW_QUAD`; what the game draws its own letterbox bars and
screen tint with), packed ARGB with the alpha carrying the fade. That is real content on the frame
the game actually presents, so any capture method shows exactly what the player sees, and a cheat
panel opened on top of it still draws on top, for the same reason: this file's hook runs as the
outer wrapper (loading after "dev_overlay" alphabetically), draws its own quad, then calls through
to dev_overlay's own hook, which paints the panel after.

**The curtain hides the picture, not the simulation.** The landing sound above is still audible
behind it, because holding the overlay open does not slow anything down; whatever plays that sound
fires at the same simulated instant regardless of how long the curtain is held.

Two earlier attempts at fixing this were each replaced in turn:

1. Muted `bapsound_setMasterVolume` (the same cell the SFX slider drives) for the transient's own
   lifetime. Confirmed live, this measurably broke something else: the level's own opening line
   ("I have a bad feeling about this") starts inside the same window as the landing sound, on the
   same engine audio channel, so muting long enough to cover the one reliably clipped the start of
   the other; Obi's first three words were silently eaten every time.
2. Suppressed `0x00417143`, the wrapper the level script interpreter's own "Sound" opcode (`0x603`,
   sitting right next to `0x604` "LOCK Player" in the same script) calls. A reasonable theory, and
   wrong: confirmed live with a diagnostics build watching every call to `bapsound_play` by name,
   opcode `0x603` never fires for this cutscene at all, so suppressing its wrapper suppressed
   nothing.

That same live capture (`[diagnostics] Audio=1`, every sound played from level load through the
first two spoken lines) shows exactly one candidate: `FSMJCON1.wav`, playing once, 2-D, right in the
gap between the level becoming visible and Obi-Wan's own first line; nothing else plays in that
window that is not an ambient loop already rejected for being out of range, or a menu sound from
well before the level even loads.

**Not one sound: a family, one per playable character.** The transient itself is not unique to
fedship.b3d either: a second live capture, playing as Qui-Gon (`iamquigon`), catches the same
transient at a DIFFERENT level's own opening (race.b3d) playing `FSUJSND1.wav` instead. Both share
the same shape: `FS`, a character letter (`M` for Obi-Wan, `U` for Qui-Gon), `J`, then a
sound-specific suffix. That shape is what `sfx_mute.c` matches on now rather than either exact
name, so Panaka's and the Queen's own versions (unconfirmed, never captured) are covered without
having to catch each one individually first. `sfx_mute.c` detours `bapsound_play` itself (`0x0041681F`,
byte-identical to `diagnostics/diag_audio.c`'s own `SIG_SOUND_PLAY`) and skips every call whose
sound record's own name matches `FS?J*` (case-insensitive, third character a wildcard) while
suppression is armed, tracking `pPlayer+0xA0` directly so suppression ends the moment the transient
it exists for actually finishes, or the curtain's own timer as a fallback cap. Every other sound,
both spoken lines included, passes through untouched.

## Why a separate window instead of drawing into the game's own surface

Everything that made the first design slow was on the far side of a DirectDraw surface neither
`fmv_player.dll` nor `video_overlay.c` owns. A borderless window sized to the game's own client
rect, owned by the game window (Windows keeps an owned window above its owner in Z-order
automatically, so no `WS_EX_TOPMOST` is needed) and `WS_EX_NOACTIVATE` (it never needs keyboard
focus, since Escape-to-skip reads `GetAsyncKeyState`, physical key state, not per-window input), sidesteps that
surface entirely: libVLC renders into that window directly via `libvlc_media_player_set_hwnd`,
which is also why `video_overlay.c` has no Direct3D or DXGI code in it at all.

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
backslash-relative movie name with no extension, matching the game's own real layout on disk,
confirmed against a legitimate install: `GAMEDATA\MOVIE\ARENA.BIK`.

```
0046C35A  55                      push ebp
0046C35B  8B EC                   mov ebp,esp
0046C35D  81 EC 90 00 00 00       sub esp,0x90
0046C363  A1 F0 77 4B 00          mov eax,[004b77f0]
0046C368  89 85 74 FF FF FF       mov [ebp-0x8c],eax
0046C36E  C7 85 78 FF FF FF 01 00 00 00   mov [ebp-0x88],1
0046C378  83 3D 60 63 6D 00 00    cmp [006d6360],0     <- the gate the hook honours
0046C37F  75 07                   jnz +7
0046C381  33 C0                   xor eax,eax
0046C383  E9 5C 01 00 00          jmp 0046c4e4         <- returns 0
0046C388  83 3D 3C A4 86 00 00    cmp [0086a43c],0
0046C38F  74 07                   jz +7
0046C391  33 C0                   xor eax,eax
0046C393  E9 4C 01 00 00          jmp 0046c4e4         <- returns 0
0046C398  C7 05 3C A4 86 00 01 00 00 00   mov [0086a43c],1
```

The bare `push ebp / mov ebp,esp / sub esp,0x90` prologue shape recurs elsewhere in an 830 KB
image, which is exactly why the two-stage detour rule exists, and why this signature reaches two
branches into the function rather than stopping at the prologue. Measured against the real retail
`WMAIN.EXE` (829,952 bytes): exactly one match, all 72 bytes, at `0x0046C35A`.

**The first gate is honoured.** The retail function refuses and returns 0 whenever `[006d6360]` is
clear, so the hook hands those calls to the original rather than answering for them. What that cell
*means* has not been established; no sweep of its writers was done. That is precisely why
deferring is the safe direction: it reproduces retail behaviour whatever the answer turns out to
be. Its address is read out of the matched `cmp` operand rather than written down as a constant.

### The drawn menu cursor's cells

The one other place this DLL reads or writes engine memory, kept in `menu_cursor_cells.c` so it is
the only file here that needs a signature at all. The window procedure's `WM_MOUSEMOVE` case, at
`0x00460BCC` in retail, reached only after the engine's own recentring call at `0x0046A115` has
confirmed the message is real movement rather than the echo of its own warp:

```
00460BCC  0F BF 55 14           movsx edx, word ptr [ebp+0x14]   ; client x
00460BD0  A1 98 6C 4B 00        mov   eax,[g_menuCursorX]        ; 0x004B6C98
00460BD5  8D 8C 10 C0 FE FF FF  lea   ecx,[eax+edx-0x140]        ; += client_x - 320
00460BDC  89 0D 98 6C 4B 00     mov   [g_menuCursorX],ecx
...                             the same shape for Y, 0x004B6C9C, -0xF0
00460C04  A1 58 FD 6C 00        mov   eax,[g_menuOriginX]        ; 0x006CFD58
...                             clamp to [origin, origin+0x25F] x [origin, origin+0x1BF]
00460C41  8B 15 5C FD 6C 00     mov   edx,[g_menuOriginY]        ; 0x006CFD5C
```

The two origin cells are the same pair `enhanced_resolution`'s cursor cage documents and repoints,
which is a cross-check on all four addresses from a second, independently written direction.

**The addresses above are what the pattern finds, not what the code contains.** Measured over every
image available: the block sits at `0x00460BCC` in all five retail executables including the German
one, and at `0x00460B6C` in the Edit Tool's own recompile, where the cells are `0x004B6C48` /
`0x004B6C4C` and `0x006CFD08` / `0x006CFD0C`. Four hardcoded constants would have written two
dwords into whatever else lives at `0x004B6C98` on that build, silently, because a wrong address in
the same data section is perfectly writable. The pattern is 64 bytes with five operands wildcarded,
one match per image, and five cross-checks before any of it is used: each cell must be the same in
its load and its store, the Y cells must sit four bytes past the X cells in both pairs, the origin
pair must be reachable at a fixed distance with the expected opcode, and every cell must lie inside
the host image.

## Closing the game during a movie

**This game cannot be closed with Alt+F4 at any time, and that is the engine's own decision.** Its
window procedure at `0x0049905E` takes `WM_CLOSE` in the switch and returns zero without reaching
`DefWindowProcA`, so the default destroy never happens, and because the switch takes it the message
never reaches the chained handlers either. Only `WM_DESTROY` calls `PostQuitMessage`, and the game's
own quit path is what raises that. Confirmed in play with no movie involved.

The close box is still read off the game's queue here, because a close request that the engine
chooses to discard is still a request this loop should not eat on the way past.

**What was actually wrong here**, and an external audit found it: `WM_SYSKEYDOWN` with `VK_F4` was in
the peeked range and could never match. A filtered peek returns the first message in its range,
Alt+F4 queues `VK_MENU` before `VK_F4`, and holding Alt autorepeats more `VK_MENU` behind it, so the
F4 was never examined. That dead branch is gone, along with the comment that described the mechanism
at length without it being able to work.

The audit's stated consequence, that a player could not close the game for the length of a movie,
does not follow: they cannot close it during ordinary gameplay either. Detecting the combination
properly was tried and did exactly nothing useful, ending the movie and then posting a close the
engine discarded. Making Alt+F4 genuinely close the game would be overriding a decision the engine
took for itself, which is a behaviour change rather than a repair and does not belong in the movie
player.

## Known limitations

* **`[0086a43c]` is not reproduced.** The retail function latches it on entry; the hook does not.
  It has the shape of a re-entrancy guard, and the hook blocks the game's only thread for the whole
  movie, so nothing can observe it in between, but that is reasoning, not a sweep.
* **The success return value is `1`, and only its non-zeroness is evidenced.** All three refusal
  paths above return 0, so zero means "did not play". Which non-zero value the retail function
  returns on success has not been read out of the image, and no caller has been shown to
  distinguish one from another.
* **Escape skips a movie outright.** There is no seek and no pause. It is now edge-triggered and
  only counts while a window of this process has the foreground, so a key held from the previous
  movie no longer skips the next one and a key pressed in another application does not skip this
  one. If the retail game disables skipping on a specific movie, this does not know that.
* **Audio does not route through the retail Miles Sound System.** libVLC plays its own audio track.
  The claim that nothing else in the audio pipeline runs during a movie has been **withdrawn**: the
  music does not stream on the game's thread, it runs on a multimedia timer into a looping buffer
  that nothing stops, and the frame hook that would pause it is frozen for the length of the movie.
  Whether the music is audible under a converted movie has not been tested.
* **A folder created while the game is running is not noticed.** The check happens once, at
  startup, like every other setting here.
* **`libdirectdraw_plugin.dll` is deliberately not installed** with the bundled runtime. It would
  let libVLC pick a DirectDraw video output, which on this install is the translation layer the
  whole feature exists to route around.
* **No hardware certainty.** libVLC's own choice of decoder depends on the codec of the converted
  file and what the system offers. H.264 has broad hardware decode support; an unusual codec choice
  in the converter may fall back to software.

## Testing status

Be precise about which claim is which, because these are three different things.

**Design 4 was live-tested on the reporting machine** in its earlier form: no flicker, no minimize,
movies playing at full quality over the game window.

**Three of the later fixes were live-tested, on the reporting machine, in the form they were
written there:** the message pump no longer discarding the game's messages (confirmed to fix the
pointer confinement into the menu, which used to start working only after an Alt-Tab);
`SetForegroundWindow` plus `SetCursor(NULL)` after every movie (confirmed to fix the stray OS
cursor, after three other attempts were each compiled, installed and ruled out when the symptom did
not change); and writing the drawn cursor's cells directly (confirmed to land centre on every
launch across repeated testing, where two earlier attempts each looked right once and then were
not).

**The form in this tree is accepted in game**, in the 1.5.0 build, which was played through by
hand. It compiles with the configured 32-bit MSVC toolset, `/W4 /WX`, zero warnings, and
`movie_path.c` is covered by `unittests/movie_path.c`. The two things that had differed from the
last tested form have both been played since: the close request recognised with `PM_NOREMOVE` on
the game's queue rather than being lost with the rest of the exclusion, and the four cursor cells
resolved by pattern rather than written down as constants. That pattern is measured across all
six available images, one match each, with all five of its cross-checks passing, including on the
recompile where the cells move.

The rest of what changed after design 4's last play session has been played too: the libVLC
search order and the registry fix, the logging, the black fill moving after `ShowWindow`, the
startup ordering, the honoured playback gate, the Escape edge trigger and the teardown. The
detour signature is measured against the real retail `WMAIN.EXE` and counted for uniqueness: one
match, at the address named above.

`video_overlay.c`, `vlc_locate.c`, `vlc_runtime.c` and `vlc_playback.c` have no engine dependency and therefore no
byte evidence to verify the same way, and no behaviour a unit test can observe without a live
window and a real video file. They rest on documented Win32 window-ownership and message-delivery
behaviour and on libVLC's own long-stable C ABI.

**What to check first in `engine_fixes.log`,** in the order the code actually writes them:

1. `the engine's playback gate is at ... and is honoured before every movie`. If it is missing, the
   gate could not be resolved and the hook is playing movies the retail engine would have refused.
2. `the drawn menu cursor lives at ... read out of the window procedure's own operands at ...`. If
   instead you get `the menu cursor accumulator did not resolve`, movies still play and only the
   cursor recentring is off.
3. `libVLC is loading on its own thread ...`, then `using the ... libVLC in ...` and `libVLC ready,
   plugins from ...`. These say which of the three candidates answered and that the plugin set was
   complete enough for libVLC to start. The last one arrives on the background thread, so it may
   appear after the interception line below rather than before it.
4. `movie playback intercepted at ...`. The detour took.
5. Then **one line per movie**: `playing "..." from ...`, or `no converted file for "..." at ...`,
   or, where a converted file does exist but libVLC cannot take it, either `libVLC is still loading
   in the background` (temporary, the next movie asks again) or `no usable 32-bit libVLC was found`
   (permanent for the session, so it is said once). Every branch of the hook logs, so a log that
   shows step 4 and then nothing per movie means the hook is not being reached, not that it is
   quietly failing.

   A machine with nothing converted never mentions libVLC at all: the readiness question is asked
   only once there is a file to play, which is also why "no usable libVLC" is a warning rather than
   a note. It means a converted movie was found and could not be used.

Two deliberate tests are worth doing by hand:

* Press **Alt+F4 during a converted movie.** The game should close, and the log should carry `the
  player asked to close the game during playback`. If it does not close, the `PM_NOREMOVE` probe is
  not seeing the request.
* **Move the mouse during a movie**, then look at where the drawn menu cursor is when the menu comes
  back. It should be in the middle of the menu, every launch, regardless of where the pointer was.
