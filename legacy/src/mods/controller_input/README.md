# controller_input

**Produces:** `controller_input.dll` -> `mods\`

The right stick looks around, Start pauses, the triggers roll. Nothing else about a controller is
touched. Built to replace Xidi, a third-party WinMM-joystick wrapper, after a field investigation
traced a reproducible, large single-frame stall (measured up to 419ms) to Xidi being actively
polled.

## Why this exists

The game's entire controller surface is three WinMM calls, `joyGetNumDevs`, `joyGetPosEx` and
`joyGetDevCapsA`, modelling one physical joystick. There is no second-stick concept in it at all.
Xidi's own working configuration for this game did not route the right stick or Start through that
surface either: it mapped the right stick's X axis to a synthesized mouse axis and Start to a
synthesized Escape keypress. This DLL does the same things, directly, without Xidi and without the
game's own joystick reading. Roll is different: the game's
own controls screen already binds it to Left Alt or Right Alt held plus the Left or Right arrow key
TAPPED (confirmed directly from that screen, not assumed, and confirmed again live: holding the
direction key down instead of tapping it produced a diagonal drift rather than a clean roll), so
the triggers here hold Alt and tap the arrow key repeatedly for as long as they stay pulled.

Three separate installs, two different ways of exposing an Xbox-style pad to the game (a direct
`WMAIN.EXE` import rename to a renamed Xidi build, and this project's own `xidi_bridge.dll` runtime
redirect, since removed now that this DLL replaces what it was for), both showed the same stall.
Turning Xidi off, either way, removed it in every controlled comparison. The stall is in Xidi
itself, not in how it gets loaded.

## Configuration: `[controller_input]`

| Key | Default | Meaning |
|---|---|---|
| `Enabled` | `0` | Master switch. Off by default: this is a brand new, field-untested feature. |
| `LookEnabled` | `1` | Right stick drives the camera. |
| `PauseEnabled` | `1` | Start opens the pause menu. |
| `RollEnabled` | `1` | Left/right trigger holds Alt+Left / Alt+Right. |
| `ControllerIndex` | `0` | Which XInput slot (0-3) to read. |
| `Deadzone` | `0.24` | Radial deadzone on the right stick, 0 to just under 1. |
| `LookSensitivity` | `4000.0` | Synthesized mouse counts per second at full stick deflection. |
| `TriggerThreshold` | `30` | How far a trigger must travel (0-255) before roll engages. |

`LookSensitivity` is in the same units `enhanced_input.dll`'s own `MouseDegreesPerCount` scales
from, but this DLL does not read that setting and has no dependency on `enhanced_input.dll` at
run time. At that feature's own default (0.050 degrees per count), the default here turns at 200
degrees per second at full deflection.

## How it works

No signature, no detour, no patch on the game. `XInputGetState` (Microsoft's own API, not Xidi,
not WinMM) reads the pad on this DLL's own dedicated background thread (`CreateThread`), polling
at a fixed real-time interval rather than once per rendered frame, see "Why a dedicated thread"
below for why that matters. The right stick's deflection, after a radial deadzone, is scaled by
`LookSensitivity` and by the real elapsed time since the last poll (`QueryPerformanceCounter`, not
the engine's own clock), and sent as relative mouse movement via `SendInput`. A fractional
remainder is carried across polls so a small, sustained deflection still adds up correctly rather
than being truncated to nothing every time. Start is edge-detected (only the press, not the
release) and sent as a synthetic Escape key down, held for `ESCAPE_HOLD_MS` (60ms), then up, also
via `SendInput`. Each trigger, past `TriggerThreshold`, holds Alt for as long as it stays pulled
and taps its own arrow key repeatedly while it does, one tap the instant the trigger crosses the
threshold and one more every 150ms after that for as long as it stays past it, each tap the same
50ms-down shape as Escape's own press.

### Why a dedicated thread instead of common/frame_hook.h

Every other per-frame need in this tree uses `frame_hook`, and the first build of this feature did
too. Look worked immediately; skipping a playing movie with Start never did. `fmv_player`'s own
movie playback (`vlc_playback.c`) runs a dedicated message-pump loop on the game's own thread that
does not call `sys_frame`/`render_frameEnd` at all for the whole duration a movie plays, so
`frame_hook`'s site never fired during a movie and this DLL never ran. A real keyboard Escape press
still worked during a movie, because `fmv_player`'s own skip check reads global OS keyboard state,
independent of which loop the game's thread happens to be parked in. A dedicated background thread
gives this DLL that same independence: it keeps polling, and keeps able to call `SendInput`, no
matter what the game's own thread is doing.

### Why SendInput reaches the game correctly

`enhanced_input.dll`'s own raw mouse reader (`raw_mouse.c`) accepts a `WM_INPUT` relative mouse
report checking only its type field (`RIM_TYPEMOUSE`) and its relative/absolute flag; nothing in
that code, or in the `RAWMOUSE` structure Windows hands it, can tell a real device from an
injected one. `SendInput`-synthesized movement reaches it exactly like a real mouse would. This
was confirmed by reading that code directly this session, not assumed; the one thing not yet
confirmed is that it holds true in an actual play session on this specific executable, which has
its own history of raw-input quirks under Windows' application compatibility shims (see
`raw_mouse.c`'s own header comment). Treat this as reviewed, not yet field-tested.

### Why Escape is the right key for Start

Confirmed directly, this session, by decompiling `gameplay_wndproc_hotkey_handler` (`0x0043F681`):
Escape (`0x1b`) is the sole route into `gameplay_open_pause_menu` (`0x0043FAB5`) during normal
gameplay, and the engine's own state gating (a separate handler owns Escape once a menu is
already open) prevents a synthetic Escape from double-toggling anything. Two edge cases exist and
are left unguarded on purpose, because both already do something reasonable: if `dev_overlay`'s
own panel is open, a synthesized Escape closes that panel instead of reaching the game; if
`fmv_player` is mid-movie, a synthesized Escape skips the movie (via that feature's own
`GetAsyncKeyState(VK_ESCAPE)` poll) rather than opening a menu. Neither is treated as a bug here.

Also confirmed live, played with `[fmv_player] Enabled=0` so every movie fell through to the
untouched retail Bink player (`0x0046C35A`) rather than `fmv_player`'s own libVLC path: Start still
skips the movie. That function's own internal skip-key check was never decompiled this session, so
which mechanism it actually reads from is not confirmed the way the other two paths are, but
`SendInput` updates the same OS-level keyboard state that `WM_KEYDOWN` dispatch, `GetAsyncKeyState`
polling and DirectInput's device state all draw from, and it now demonstrably reaches all three
different consumers this DLL has been tested against.

### Why the triggers are Alt+tap-Left/Alt+tap-Right, and what is not confirmed about them

The binding itself is not a guess: read directly off the game's own Controls/Options screen,
Left Alt or Right Alt held plus the Left or Right arrow key **tapped**, not held, rolls in that
direction. The first version of this feature held the direction key down for as long as the
trigger stayed pulled, matching how the rest of this DLL treats a held input; played live, that
produced a diagonal drift rather than a clean roll. Whatever this game's roll handling actually
does with a continuously-held direction key, it is not the same thing a series of clean taps
produces, so this now sends the direction key as repeated taps instead (see "How it works" above
for the exact timing), matching the real input shape rather than assuming a held key would be
read the same way as several distinct presses.

Alt is shared between both triggers rather than pressed once per trigger, held from the first
trigger to engage and released only once the last one disengages, so pulling both at once does not
send two Alt-down events.

What is not confirmed: whether this game's own reading of movement/roll keys goes through
`WM_KEYDOWN`, `GetAsyncKeyState` polling, or DirectInput's own polled keyboard state. Escape was
proven, this session, to reach the first two and (very likely, per the reasoning above) the third
of those. This project's own loader exists specifically because this game already uses DirectInput
for at least some of its input (that is the whole reason a `dinput.dll` loader was needed here in
the first place), which makes DirectInput a real candidate for how roll is read, not just a
theoretical one, and DirectInput in exclusive acquisition mode has a documented history elsewhere
of not always seeing `SendInput`-synthesized keys the way non-exclusive raw input and message-based
reads do. The tap-shaped fix has not itself been played yet; field-test before trusting this.

### Why the recheck interval exists

`XInputGetState` is documented to cost more when the requested slot is not connected, because the
runtime rescans for hardware on every such call rather than answering from a cached state. Polling
an empty slot at the ordinary 125Hz cadence would reintroduce a smaller version of the exact
polling cost this DLL exists to remove. While no pad has been seen, this checks once every 500ms
instead; once one is found, the ordinary cadence begins and stays on for the rest of the session.

## Limitations

* Only the right stick, Start and the two triggers are handled. Movement, face buttons and the
  left stick do nothing here; use the game's own joystick support (via a real wrapper, or a real
  legacy joystick) if those are wanted too, or extend this DLL.
* `SendInput` is OS-level synthetic input. It will also reach any other foreground window, though
  in practice this game holds input focus while running and the pause key/mouse movement are
  harmless if they ever did not.
* A large gap between polls (an Alt-Tab, a breakpoint, the disconnected-pad recheck interval) is
  treated as zero elapsed time for the look calculation rather than firing one enormous turn when
  polling resumes.
* Runs on its own thread for the life of the process, with no shutdown path, matching this
  project's own established DLLs (loaded once, never freed, per `common/detour.h`'s own "no
  uninstall" convention). The OS reclaims the thread when the process exits.

## Testing status

Played five times, three real bugs found and fixed, all three confirmed working on replay. Look,
pause (opening and closing the menu, and skipping a movie through both `fmv_player`'s libVLC path
and the untouched retail Bink player) and roll all now work exactly as they do on keyboard and
mouse, confirmed by the player directly.

**Round one:** look worked immediately, axes correct. Opening the pause menu with Start also
worked. Closing the menu again with a second Start press did nothing. Root cause: Escape's key
down and key up were sent in the same `SendInput` call. Opening the menu is a plain `WM_KEYDOWN`
dispatch and does not care how fast the up follows; closing it goes through `TranslateMessage`
producing `WM_CHAR`, which wants the key genuinely observed as held. Fixed by holding the
synthesized Escape down for one game frame before releasing it, confirmed working on replay.

**Round two:** with closing the menu fixed, Start still could not skip a playing movie; a real
keyboard Escape press could. Root cause, found by reading `fmv_player/vlc_playback.c` directly:
movie playback runs its own dedicated message-pump loop on the game's own thread and does not call
`sys_frame`/`render_frameEnd` at all for the whole duration a movie plays. This DLL was driven
entirely by `common/frame_hook.h`'s hook on that site, so it never ran at all during a movie, never
got a chance to see Start pressed. A real keyboard press worked anyway because `fmv_player`'s own
skip check reads global OS keyboard state (`GetAsyncKeyState`), independent of which loop the
game's thread is parked in.

**Fix:** moved this DLL off `frame_hook` entirely and onto its own dedicated background thread
(`CreateThread`, polling `XInputGetState` on a fixed real-time interval rather than once per
rendered frame). That thread keeps running, and can keep calling `SendInput`, no matter what the
game's main thread is doing, including inside `fmv_player`'s pump loop. Confirmed working on
replay: look, opening the menu and closing it again all still work running on a thread instead of a
frame hook, and Start now does skip a playing movie, both through `fmv_player`'s libVLC path and,
tested separately with `[fmv_player] Enabled=0`, through the untouched retail Bink player.

**Round three:** roll, first version. Played once. Escape's own synthetic keys did reach whatever
reads this game's roll input, unlike the doubt raised above, but holding the direction key down
for as long as the trigger stayed pulled produced a diagonal drift rather than a clean roll.
Confirmed with the player themselves: real play is "hold Alt, then tap Left or Right", not hold
the direction key, which is what a held trigger was reproducing.

**Fix:** each trigger now taps its own arrow key repeatedly instead of holding it down, one tap
the instant the trigger crosses the threshold and one more every 150ms for as long as it stays
past it. Confirmed working on replay: roll now feels and behaves exactly like keyboard and mouse,
the player's own words, both directions clean rather than diagonal.
