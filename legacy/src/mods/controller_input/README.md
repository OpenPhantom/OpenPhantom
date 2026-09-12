# controller_input

**Produces:** `controller_input.dll` -> `mods\`

The right stick looks around, Start pauses, the triggers roll. Nothing else about a controller is
touched. Built to replace Xidi, a third-party WinMM-joystick wrapper, after a field investigation
traced a reproducible, large single-frame stall (measured up to 419ms) to Xidi being actively
polled.

## Supported executables

Any. This DLL patches no engine code and resolves no pattern: it reads the pad through XInput on
a thread of its own and hands the game what it already understands, mouse motion and key presses,
through `SendInput`. The addresses below are where the game was read to prove those reach it.

## Why this exists

The game's entire controller surface is three WinMM calls, `joyGetNumDevs`, `joyGetPosEx` and
`joyGetDevCapsA`, modelling one physical joystick. There is no second-stick concept in it at all.
Xidi's own working configuration for this game did not route the right stick or Start through that
surface either: it mapped the right stick's X axis to a synthesised mouse axis and Start to a
synthesised Escape keypress. This DLL does the same things, directly, without Xidi and without the
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
| `Enabled` | `1` | Master switch. On by default: played and confirmed working across several sessions. |
| `LookEnabled` | `1` | Right stick drives the camera. |
| `LookVertical` | `0` | Send the stick's vertical axis as well. Off because this game has no vertical camera; see below. |
| `PauseEnabled` | `1` | Start opens the pause menu. |
| `RollEnabled` | `1` | Left/right trigger holds Alt and taps Left / Right while it is pulled. |
| `ControllerIndex` | `0` | Which XInput slot (0-3) to read. |
| `Deadzone` | `0.24` | Radial deadzone on the right stick, 0 to just under 1. |
| `LookSensitivity` | `4000.0` | Synthesised mouse counts per second at full stick deflection, 1 to 100000. |
| `TriggerThreshold` | `30` | How far a trigger must travel (0-255) before roll engages. |

### The vertical axis is off

This game's camera does not pitch. `enhanced_input`'s own view turn says so in `raw_mouse.h`:
it drains the vertical axis only for the menu pointer, and its free look is horizontal
throughout. A synthesised vertical count is therefore not a look anywhere, and the poll spends
work producing one that nothing is waiting for.

**It is not why the right stick walked the player.** That was found in the same session, and
the measurement put it somewhere else entirely: the game's own joystick bindings bind the pad's
R axis, which is the right stick's vertical on an Xbox pad seen through WinMM, to the same
forward and back control as the left stick's Y. Setting `JOYENABLE=0` in `obi.ini` stopped the
walking with this setting already off, which puts it on the engine's own reading and not on
anything synthesised here. That defect belongs to `enhanced_input`, where it is now fixed by
answering the engine's own reading of that axis with a zero; this setting does not address it
and was never going to. See **The right stick walked the player** in that module's README.

What stands is the narrower claim: the count buys nothing. The axis is kept behind
`LookVertical` rather than deleted, because two things do read a vertical mouse movement: the
menu pointer, and `dev_overlay`'s free camera, which pitches from the screen pointer and is the
only vertical look in this project. Left and right are unaffected either way.

`LookSensitivity` is in the same units `enhanced_input.dll`'s own `MouseDegreesPerCount` scales
from, but this DLL does not read that setting and has no dependency on `enhanced_input.dll` at
run time. At that feature's own default (0.050 degrees per count), the default here turns at 200
degrees per second at full deflection.

## How it works

No signature, no detour, no patch on the game. `XInputGetState` (Microsoft's own API, not Xidi,
not WinMM) reads the pad on this DLL's own dedicated background thread (`CreateThread`), polling
at a fixed real-time interval rather than once per rendered frame; see "Why a dedicated thread"
below for why that matters. The right stick's horizontal deflection, after a radial deadzone, is
scaled by `LookSensitivity` and by the real elapsed time since the last poll
(`QueryPerformanceCounter`, not the engine's own clock), and sent as relative mouse movement via
`SendInput`. The vertical is dropped unless `LookVertical=1`. A fractional
remainder is carried across polls so a small, sustained deflection still adds up correctly rather
than being truncated to nothing every time. Start is edge-detected (only the press, not the
release) and sent as a synthetic Escape key down, held for `ESCAPE_HOLD_MS` (60ms), then up, also
via `SendInput`. Each trigger, past `TriggerThreshold`, holds Alt for as long as it stays pulled
and taps its own arrow key repeatedly while it does, one tap the instant the trigger crosses the
threshold and one more every 150ms after that for as long as it stays past it, each tap the same
50ms-down shape as Escape's own press.

A pad that disappears lets go of everything it was holding. The poll used to return on a failed
read without releasing anything, so a trigger pulled at the moment the pad dropped out left a
synthetic Alt down in the game, and in whatever took focus next, until the pad came back. The
disconnect branch now releases exactly as the focus-loss branch always did.

### The fraction owed is its own file

That fraction is the only part of this DLL with a right answer that can be checked without a
pad, and it was the part nothing checked. It now lives in `look_counts.c`: no pad, no clock, no
`SendInput`. `controller_input.c` asks it for whole counts and sends whatever comes back.

Two properties are written down as checks now. The first
is the fraction, which only shows over a run of polls: at the shipped sensitivity one
poll at a hundredth of the stick range is worth a third of a count, and a hundred of them have
to turn the view by 32 counts arriving one at a time. Nothing in a play session can report on
that, because a dead band at a tenth of the stick reads as a deadzone rather than as a fault.

The second is the cast. A sensitivity that is not a number reaches a cast to `long`, which is
undefined and here lands on the most negative value a `long` holds, sending the pointer to the
corner of the screen on every poll for the rest of the session. The settings loader already
corrects `LookSensitivity` on the way in and still does, because a player who typed something
odd deserves to be told; the arithmetic now refuses it as well, so a second caller arriving
later cannot reintroduce it.
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
injected one. `SendInput`-synthesised movement reaches it exactly like a real mouse would. This was
confirmed by reading that code directly, and confirmed again live across several play sessions on
this specific executable, which has its own history of raw-input quirks under Windows' application
compatibility shims (see `raw_mouse.c`'s own header comment) that a reading of the code alone could
not have ruled out.

### Why Escape is the right key for Start

Confirmed directly, by decompiling `gameplay_wndproc_hotkey_handler` (`0x0043F603`):
Escape (`0x1b`) is the sole route into `gameplay_open_pause_menu` (`0x0043FAB5`) during normal
gameplay, and the engine's own state gating (a separate handler owns Escape once a menu is
already open) prevents a synthetic Escape from double-toggling anything. Two edge cases exist and
are left unguarded on purpose, because both already do something reasonable: if `dev_overlay`'s
own panel is open, a synthesised Escape closes that panel instead of reaching the game; if
`fmv_player` is mid-movie, a synthesised Escape skips the movie (via that feature's own
`GetAsyncKeyState(VK_ESCAPE)` poll) rather than opening a menu. Neither is treated as a bug here.

Also confirmed live, played with `[fmv_player] Enabled=0` so every movie fell through to the
untouched retail Bink player (`0x0046C35A`) rather than `fmv_player`'s own libVLC path: Start still
skips the movie. That function's own internal skip-key check was never decompiled, so which
mechanism it actually reads from is not confirmed the way the other two paths are, but
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

Still not confirmed by reading code alone: whether this game's own reading of movement/roll keys
goes through `WM_KEYDOWN`, `GetAsyncKeyState` polling, or DirectInput's own polled keyboard state.
Escape was proven, by decompile, to reach the first two and (very likely, per the reasoning above)
the third of those. This project's own loader exists specifically because this game already uses
DirectInput for at least some of its input (that is the whole reason a `dinput.dll` loader was
needed here in the first place), which makes DirectInput a real candidate for how roll is read, not
just a theoretical one, and DirectInput in exclusive acquisition mode has a documented history
elsewhere of not always seeing `SendInput`-synthesised keys the way non-exclusive raw input and
message-based reads do. Whichever mechanism it actually is, the tap-shaped fix has since been played
and confirmed working (see Testing status below), so this is now a known-working path rather than
an open question about whether it works at all, just an open question about which of the three it
goes through.

### Why the recheck interval exists

`XInputGetState` is documented to cost more when the requested slot is not connected, because the
runtime rescans for hardware on every such call rather than answering from a cached state. Polling
an empty slot at the ordinary 125Hz cadence would reintroduce a smaller version of the exact
polling cost this DLL exists to remove. While no pad has been seen, this checks once every 500ms
instead; once one is found, the ordinary cadence begins and stays on for the rest of the session.

## The deadzone is the shared one

The radial deadzone is `common/stick.c`, the same code `enhanced_input.dll` runs on the left stick.
This DLL used to carry a private copy of it. The two had already drifted: the copy had lost the
guard against a nonsense `Deadzone` and the guard against a centred stick, so `Deadzone=0` plus a
stick at rest divided by its own zero magnitude.

The shared version also had the diagonal wrong, and both did. It clamped the magnitude to 1 and
then divided by it to get the direction, which leaves the direction unnormalised: a stick held to
the corner of a square range arrived as 32767,32767, divided 1,1 by 1, and came back 1.41 long. A
diagonal therefore looked 41 percent faster than a straight push, on the right stick as a faster
turn and on the left stick as faster movement. The direction is now taken from the true magnitude
and the speed capped afterwards.

It only shows on a pad that reports a square range. A stick whose hardware is circular never sends
the corner. Steam Input does, so it was visible on the Deck.

`legacy/unittests/stick.c` covers it: the corner case reads 1.414 against the old code and 1.000
against this one.

## Nothing is injected unless the game has focus

`SendInput` does not aim at a window. It goes to whatever has focus. That is the reason this mod
works at all without the game cooperating, and it is also how a controller mod ends up typing
into somebody else's application.

Every injection is therefore gated on this process owning the foreground window. Without that gate,
a stick pushed while the game is alt tabbed moved the mouse in the player's browser, a held trigger
fired Alt chords into it, and Start sent it an Escape. The check compares the foreground window's
owning process to this one, byte for byte what `dev_overlay` already does before it reads a held
key, so the pattern was in the tree and this code simply was not using it.

**One thing deliberately still happens while unfocused: releasing.** A synthetic Alt left held down
belongs to whichever window has focus now, so losing the foreground releases it rather than
returning early. The button and trigger edges are recorded at the same moment rather than cleared,
so coming back to the game with Start or a trigger already held does not fire a press the player
made somewhere else.

The poll itself keeps running while unfocused, so the pad stays tracked and a return to the game is
immediate.

## Your controller has to be an XInput one

This reads the pad with `XInputGetState` only. An Xbox pad works as it is; anything else has to be
presented as one.

**Add the game to Steam as a non-Steam game and launch it from there.** Steam Input then presents
almost any controller as an XInput device, a Steam Deck's own controls included. This is the route
confirmed end to end, on a Deck in desktop mode, where launching from Steam gives the window modes
and every input feature here together. The alternative is something that emulates XInput directly,
such as DS4Windows for a PlayStation pad.

Without one of those, a DirectInput-only device is invisible here: an older or off-brand pad, a
PlayStation controller plugged straight in, a flight stick. The game's own joystick support and its
own Controls screen still read such a device, with the shipped faults `enhanced_input` documents.

Note what is NOT the explanation, because it was guessed and then refuted by a log: Wine does supply
XInput for any pad it recognises, with or without Steam, so a Steam Deck in desktop mode outside
Steam still reports a device. It reports one that sends nothing, which is a different fault and is
described in `enhanced_input`'s README.

## Planned: reading more than XInput

Wanted, not written. Recorded here so the next person starts from the constraint rather than the
options.

**Why XInput and not something wider in the first place.** `XInputGetState` answers the whole
question in one call and answers it in normalised form: sticks already scaled, triggers already
0..255, buttons already named by their position on a known layout. Everything below returns raw
device state and leaves the caller to work out which axis is which, where the centre is, and what
the buttons are called. That mapping problem is exactly what the engine's own joystick path gets
wrong, in the ways `enhanced_input` sets out, so a second implementation of it is a second chance
to get it wrong.

**The candidates, and what each actually costs.**

* **SDL's game controller layer.** The pragmatic answer. It normalises hundreds of devices onto the
  same Xbox-shaped layout this code already speaks, using a community mapping database, so the
  internal shape of `controller_input.c` would barely change: one backend swap behind the same
  "give me sticks, triggers and buttons" call. The price is a new third-party binary to ship, with
  the licence, the notices file and the checksum list that go with it, and one more DLL to load
  under Wine.
* **DirectInput 8.** No new dependency, and it is what the devices this is for actually speak. The
  cost is the whole mapping problem by hand, per device, with no database to lean on. Workable for
  a named list of popular pads and unbounded for anything else.
* **Raw Input or HID directly.** Most control, most work, needs report descriptor parsing. Only
  worth it if the two above are both ruled out, which they are not.
* **Windows.Gaming.Input.** Modern and clean, and no use here: it is WinRT, and this has to keep
  working under Wine on the Steam Deck, where the Linux confirmation in this project depends on it.

**The shape to aim for.** One internal pad interface with XInput as the default backend, kept
dependency-free, and a second backend chosen by a setting rather than by detection, so a reader with
an unusual device opts in and a reader with an Xbox pad is never routed through anything new. Start
by making the existing code call through that interface with XInput behind it and no behaviour
change at all; that step is worth doing on its own and it makes the rest small.

**What is not known yet.** Whether Steam Input covers enough of the affected devices in practice
that the whole thing is unnecessary for anyone playing through Steam. Worth asking before building:
if the answer is yes, the honest fix is documentation rather than code.

## Limitations

* Only XInput devices are read at all. See above.
* Only the right stick, Start and the two triggers are handled *by this DLL*. The left stick is
  read by `enhanced_input`'s `PadStick`, also through XInput and for the reasons its own README
  gives, so movement is covered and is simply covered elsewhere. Face buttons are handled by
  neither; the game's own Controls screen still reads those.
* `SendInput` is OS-level synthetic input. It will also reach any other foreground window, though
  in practice this game holds input focus while running and the pause key/mouse movement are
  harmless if they ever did not.
* The right stick's vertical axis is not sent unless `LookVertical=1`, because there is no
  vertical camera to drive with it. That does not stop the right stick walking the player: the
  game's own joystick bindings do that, and the fix for it is not in this DLL. See above.
* A large gap between polls (an Alt-Tab, a breakpoint, the disconnected-pad recheck interval) is
  treated as zero elapsed time for the look calculation rather than firing one enormous turn when
  polling resumes.
* Runs on its own thread for the life of the process, with no shutdown path, matching this
  project's own established DLLs (loaded once, never freed, per `common/detour.h`'s own "no
  uninstall" convention). The OS reclaims the thread when the process exits.

## Testing status

Lifting the look arithmetic into `look_counts.c` is built, unit tested at 24 checks and played.
The look feels as it did, the player's own words, including the slow push where the carry is
doing all the work. Nothing about the numbers changed: the same fraction is carried the same
way, and an ordinary poll produces the count it always did. What is new is that a reading which
is not a number now stops the look instead of reaching the cast.

That session is what found the right stick walking the player. `LookVertical` defaulting to off
is built and played, and it did **not** stop the walking, which is how the cause was traced to
the game's own joystick bindings instead and fixed in `enhanced_input`. The setting is kept for
the narrower reason described above, and the right stick was confirmed behaving correctly in
the run that proved the other fix.

The diagonal correction and the `LookSensitivity` range check are built, unit tested and
**played**, with a pad, in the 0.4.4 build. The look turns more slowly on a diagonal than it used
to, by up to 41 percent at the corner, and a straight push up, down, left or right is unchanged to
the last decimal.

Everything below this predates that change.

Played five times, three real bugs found and fixed, all three confirmed working on replay. Look,
pause (opening and closing the menu, and skipping a movie through both `fmv_player`'s libVLC path
and the untouched retail Bink player) and roll all now work exactly as they do on keyboard and
mouse, confirmed by the player directly.

**Round one:** look worked immediately, axes correct. Opening the pause menu with Start also
worked. Closing the menu again with a second Start press did nothing. Root cause: Escape's key
down and key up were sent in the same `SendInput` call. Opening the menu is a plain `WM_KEYDOWN`
dispatch and does not care how fast the up follows; closing it goes through `TranslateMessage`
producing `WM_CHAR`, which wants the key genuinely observed as held. Fixed by holding the
synthesised Escape down for one game frame before releasing it, confirmed working on replay.

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
the direction key, which a held trigger was reproducing.

**Fix:** each trigger now taps its own arrow key repeatedly instead of holding it down, one tap
the instant the trigger crosses the threshold and one more every 150ms for as long as it stays
past it. Confirmed working on replay: roll now feels and behaves exactly like keyboard and mouse,
the player's own words, both directions clean rather than diagonal.
