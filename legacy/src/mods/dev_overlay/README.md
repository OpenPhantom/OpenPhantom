# dev_overlay

A panel over the running game, opened with **F6 or the key below Escape**. It holds the cheats
today. The name is what the thing is, not what is in it: the diagnostics and developer tools that
come later are groups inside this same panel, and a shipped DLL cannot be renamed without
breaking every `engine_fixes.ini` that mentions it.

## Supported executables

Retail `WMAIN.EXE`. Every cheat and every row resolves its own sites by pattern, and a row whose
site did not match is drawn greyed, not hidden, so the panel says what is unavailable on
the executable in front of it. The addresses are beside each cheat below.

## What it looks like

Two tabs under a heading that reads `Dev menu`. It was `Cheatmenu` while the cheats were all it
held; it holds the spawner, the free camera, the picture, the controls and this patch's own
settings now, and a player looking for any of those was looking under the wrong word.

* **Original** holds two groups: the eleven codes the shipped console can switch on and off, and
  the sixteen it can only run once, typed in retail one backspace and one line of text at a
  time. Here they are both just rows in the same tab.
* **OpenPhantom** shows twelve headings, each named for what is under it rather than for the DLL
  that reads it, in the order they are wanted: **Cheats**, **Free camera** (that cheat with its key
  and its instructions), **Entity spawner** (any actor in the game, in front of you or where the
  mouse puts it), **Appearance** (who the player looks like), **Level selection** (the skip, and the
  level a new game starts at), **Engine** (the draw distance, the field of view, the fog and the
  subtitle size), **Controls** (the control scheme), **Multiplayer** (the key that opens the
  multiplayer chat), **Window**, **Frame rate**, **Menus** (this panel's own size and key, and
  whether this patch's settings appear on the game's own screens) and **Dismemberment**. The three
  at the top are the three used while standing in a level, which is where the panel is opened from.

  **Engine** names how the engine draws the world, and no DLL: three of them read the rows under
  it. Inside, the group keeps its name, the picture, because its slots and its row ids are written
  against it; the heading changes nothing a player has set or a session locks. **Multiplayer** is
  a heading of its own for its one row, the chat's key, and it stands directly under **Controls**,
  where a player looks for a key; **Menus**, where the row would otherwise belong, says neither
  chat nor key and starts folded near the bottom.

  It began as one group with a settings row appended, and the settings outgrew the cheats, so a
  reader had to scroll past invincibility to reach the draw distance. A second group, Utilities,
  held every setting for a while; the window rows came out of it first, because they answer a
  single question and half of them are unusable until the game is restarted, which is worth saying
  in one place and not eleven times, and the rest followed on the same argument.

  Three of those headings were the DLL and not the subject: `Enhanced resolution` held no
  resolution at all, `Enhanced input` held the control scheme and `In game options extras` held
  one question about the game's own screens. A player looking for the field of view looks under
  the picture. Two sources lost their heading in the same change and are drawn under another
  one instead: the fog under **Engine**, because the fog row that follows the draw distance
  points at a number in that group, and the game's own screens under **Menus**, because both
  answer where the settings of this patch appear. They are still their own sources, with their own
  slot numbers, and `session_lock.c` still decides by those numbers; a row moved between sources
  would have changed what a session takes away, and `unittests/overlay_session.c` is the guard
  against that, checking the taken rows by name.

The panel opens on **OpenPhantom** when the game starts, because that is the tab it is opened for:
the free camera, the spawner and the settings are wanted far more often than a retail code.
Everything starts folded the first time, and after that the panel opens where it was left: the
tab, the folds, the search text, the scroll and the open lists all survive closing it, so a
setting that has to be looked at in the game costs one keypress each way instead of four. What
does NOT survive is what was half done, a key capture waiting or a number half typed, because
either would reach into the game. A folded heading carries a short word on the right saying what
is under it, `2 on`, `ON`, the name of a chosen entry, or `session` when a running session has
taken the whole group, so a list of twelve bands answers most questions without being opened.

The word is worked out from the rows and not asked of each group, which is what keeps it from
disagreeing with what the group draws. SWITCHES are counted: a list counts none of its entries,
because one of them is always the chosen one and `1 on` beside a heading would say something
under it had been switched on. That is why **Window** reads `ON` and not `2 on`; the word answers
for its one real switch, the stretch. A heading with NO switches under it and exactly one chosen
entry carries that entry's own name instead, cut to fifteen characters with two dots if it is
longer: **Appearance** reads the model being worn, **Entity spawner** reads the behaviour its
copies are given, `Stand` or `Follow` or `Attack` or `Help`. Two chosen entries under one heading
cannot be said in one word and none has no word, so both of those read empty; the spawner's band
therefore goes quiet while its list of kinds is open, which is the rule and not an oversight. The
search box filters by name and opens a group that has matches, and clearing it puts the fold back
the way you left it. A switchable row shows its state as `ON` or
`OFF` in a chip; a row that only runs once shows `RUN` instead, in the same shape but never green;
green would say "this is on right now", which a fire-once row never is.

**A choice is not a switch, and is not drawn as one.** Five window shapes, a roster of models,
eleven levels and the list of what to spawn are each one setting with several values, and each
was drawn as one chip per entry: four rows reading `OFF` beside one reading `ON` says the window
has five settings that happen to be off. An entry of a list carries a small filled mark to the
left of its name instead, an outline when it is not the chosen one, and no chip at all. The mark
sits in the indent every row already has, so nothing on the row moved for it. It is not green:
green is this panel's word for a switch that is on.

A choice short enough to be shown whole stands on its own row instead, as a strip of words where
the chip would be, the chosen one filled. There is one of those: the behaviour of a spawned
entity, `Stand`, `Follow`, `Attack`, `Help`. Its name and its four words come to about 35 of the
41 text heights the panel leaves for them, so there are six or seven characters of real room; the
unit test holds the pair at 45 characters of its own 48, which is the same measurement taken
conservatively and fails on any change to the four words rather than only on an overflow. Clicking a
word picks it, and Left and Right walk them while the keyboard is on that row; at either end the
arrow means what it means everywhere else in the panel, which is the tab, so holding one down still
leaves the row. Either kind shows a reason in place of its chip when it cannot be used, and `n/a` is
only one of them: `n/a` is the engine site that never resolved, `session` is a running multiplayer
session, `needs key` is a binding that has to be made first, `needs row` is another row in the same
group that decides this one, and `held back` is one of the three shipped codes that resolve, run and
are deliberately not offered. There is one more, `see why`: the entity spawner works out its own
reason for the whole group and writes it as a sentence a few rows up, and the word on the row points
at that sentence rather than repeating it, because it is longer than a chip and it changes with the
world. The sentence behind the other kinds is written once, on the row directly under the first row
that gives that reason. The
pointer is the game's own cursor.

**On a client of a multiplayer session four taken rows read the host's value instead.** The host
decides the draw distance, the fog thickness, whether the fog follows the draw distance and
lightsaber dismemberment for everybody, and a client runs the host's values for the session without
its own ini changing. Those rows are taken like every other row a session takes, greyed and not
pressable, but their chip reads `host 1.50x`, `host 0.50x`, `host OFF` or `host ON` rather than
`session`, because the host's value is what that machine is running and the number in its own ini
is not. A setting the host did not name reads `session` as before. The two switches under the draw
distance, the frame rate rows and the game's own codes have no host value; they are this machine's
own and read `session`. On the host itself, and outside a session, nothing changes. The values come
from the record the multiplayer files for the session (`common/host_settings_note`), and the panel
asks for it only while its session note says this machine is a client. The folded heading over
those rows counts the host's switches as well: with the host's fog not following the draw distance,
**Engine** reads one switch fewer on the client than the client's own file has on, because the
word on a heading says what is running under it.

**When the panel turns something down it says so**, in a band directly above the footer, in
amber. A number outside its ends, a key the panel needs for itself, a settings file that could not
be written: each of those used to leave the row showing exactly what it shows when the change
worked and the value happened to be the same, so there was no way to tell a refusal from a no-op
by looking. The band holds one sentence, the last one, and it is **not there at all** when there
is nothing to say: it costs no height, and the rows sit where they always sat.

It never goes away on a clock, and that is deliberate. While it stands it costs one band of
height: it takes the last row that fits, or makes the panel taller by its own height, and no row
above it moves. A band that expired by itself would turn the place it held back into a row, or
into the footer, while the pointer stood still, and a click aimed at the band would land on
whatever took its place. It goes on the next click, key or pad press, which is a moment the
player is already acting. The entity spawner's own refusal, such as "Refused: 16 are alive, the
most at once", stays a row in its group, because it describes a state of the world rather than
an answer to a keystroke; it is amber now too, and every other note stays grey.

One sentence in the band is not a refusal: after the chat's key is bound it says `Saved: in a
session it works within a second`. It is put up as a confirmation and drawn in the ordinary text
colour, not in amber, because in the colour of a refusal a sentence that says the key was saved
reads as one that says it was not. Not in green either: green in this panel says a switch is on.
It goes the same way a refusal goes, on the next action.

**The keyboard's row and the pointer's row are two pictures.** Both are filled, which is what says
"this row is current"; the keyboard's carries a thin accent frame around it as well, which is what
says which hand put it there. Moving the mouse still clears the selection, so the two never both
exist; what this answers is the question a player has after touching the mouse and then reaching
for Return. The frame is round the whole row rather than a bar down its left edge, because that
bar is already the heading's, in the same place and the same colour.

Along the bottom is a band that says what the keys do, `[Up/Down] move`,
`[Left/Right] fold, set, tab` (a heading folds, a number changes, a row of words walks
them, and anything else is the tab),
`[Return] act` and `[Esc] close`, and on its right either how
much the open tab holds (`12 groups - 79 rows`) or, while a multiplayer session runs,
`session - host` or `session - client`. That is the whole of what the panel can say about a
session: the note the multiplayer publishes carries whether one runs and who hosts it, and no
count of the players, so the band names none. Nothing of it reaches the log. On a panel too
narrow to hold the whole line it drops the grey words first and the tally second, and never
makes the panel wider: the panel is as wide as its longest row needs, and that is the
measurement everything else here is fitted into. `Esc closes` used to stand on the right of
the title band and has gone from there, because one sentence in two places is the pair that
drifts apart.

While a key row waits for its key (its chip reads `...`), the keys on that line stand down and the
band says `press a key` instead, with the tally beside it where it fits. Every key the caps name
would be taken as the binding in that moment, Escape included, so `[Esc] close` would have been
wrong for exactly as long as it stood there. A refused key leaves the old binding and says why in
the band above.

## How it draws, and why there is no window

It is drawn with the engine's own filled quads and its built in font, from the moment just before
the scene is closed, so it composites with the finished picture the way the game's own letterbox
bars do. It needs no window, cannot take the focus, and works in a full screen mode.

Painting from the shared per-frame hook was tried first and was wrong: that hook runs its callbacks
*after* the function it sits on, and that function ends by closing the scene and flipping the page.
The panel was drawn into a buffer that had already been shown, so it appeared a frame late and, as
the engine does not clear between frames, it stayed there and smeared when the pointer moved.

The other two routes were rejected on their own terms. A layered window over the game is a second
window with one flat alpha, and full screen is where it fails. Hooking Direct3D and drawing there
the way ReShade does would mean hooking the graphics wrapper this project ships and taking on a C++
user interface library, for nothing the engine's own renderer does not already give.

| Site | Address in retail | What it is |
|---|---|---|
| filled shape | `0x00419660` | four coordinates, a packed ARGB, and a flag: draw now or queue |
| built in font | `0x0046B754` | the slot the engine loaded at startup, read as a cell |
| font select | `0x0046B13B` | refuses below 0 and at or above 16 |
| alignment | `0x0046B23C` | three modes, writing 1, 2 and 4 into one field |
| glyph scale | `0x0046B293` | `+0x28`, telling it from the position scale |
| position scale | `0x0046B2BA` | `+0x38` |
| text colour | `0x0046B179` | packed ARGB |
| text | `0x0046B3C0` | a string at a position |
| character metrics | `0x0046B2FC` | width and height, in real screen pixels |
| string width | `0x0046B37A` | |
| screen size | `0x00439476` | the one place that loads both halves back to back |
| the cursor | `0x0045FD01` | the pointer's texture and the sprite drawer, both read out of it |
| the scene closes | `0x0046C32D` | the call redirected to paint from |
| window messages | `0x0043F603` | where the game's own console is opened, on backspace |
| the player is suspended | `0x00450FD8` | the engine's own predicate; the cell it loads the player record from is read out of it, and every cheat that reads the player goes through that cell |

The filled shape is not named in any reconstruction. What identifies it is its call graph: exactly
six call sites reach it and five lie inside the fade and letterbox module, which draws exactly five
filled shapes. One of those sites cleans 24 bytes after the call, which is where the six arguments
and the calling convention come from.

**The fills are drawn with our own vertices, through the routine's own three calls.** The routine
writes every vertex with `rhw = 0` and, on a 16-bit depth buffer, `z = 1.0`, the far plane. NVIDIA
and AMD draw that; an Intel UHD laptop drew the panel's text and pointer (the font layer and a
textured sprite) with none of its fills, and the movie player's post-movie curtain vanished with
them, which showed as the character dropping in after a movie. `common/screen_fill.c` reads the
three calls the routine's immediate arm makes (the render state word, no texture, a triangle fan
of transformed vertices) out of its body, checks the bytes around each first, and makes the same
calls with `z = 0` and `rhw = 1`, the values Direct3D defines for a transformed vertex. If the arm
ever fails to read, the routine itself is called as before and the log says so.

**Text is not drawn in pixels by default.** The font layer keeps a position scale and a glyph scale,
and the layer below it multiplies every glyph by the display over 640 by 480 before it draws *or
measures*. Both scales, the alignment and the font itself are set before every string **and before
every measurement**, because the engine puts none of them back and anything else in the frame will
have changed them. Measuring with one set of them and drawing with another was this feature's most
expensive defect: the panel was sized from a number that did not describe the text in it.

## What the panel takes, and what it does not

While it is open, two separate things are held, and it needs both.

The player's phases are stopped, which is how this engine stops a character taking orders: it has no
input switch, it simply does not run the phases in a menu, a dialogue or a cutscene, which is also
what the mouse look in `enhanced_input` already watches for.

**That alone was never a pause, and this file used to claim it was.** The player stopped and the
world did not: NPCs kept walking, movers kept moving and timers kept running behind the panel. A
player notices that opening the overlay mid fight. So the simulation is now held as well, on the
engine's own flag. `sys_frame` gates its own substep loop on it and the retail pause menu sets the
same one, so nothing here is invented and nothing had to be hooked; `render_frameEnd` runs below
that gate. The picture keeps being drawn.

**Except in a multiplayer session, where the hold is not taken at all.** The world a session stands
in belongs to everyone standing in it, and one player opening the panel may not stop it for the
rest. It also made a feature look broken: the entity spawner hands its wish to the multiplayer,
which reads it in a substep, and a held simulation runs none, so nothing whatever happened until the
panel was closed again. The panel asks `session_lock` on the way in, the answer is the same sticky
reading of the session note that the locked rows use, and the player is still held by the input
freeze while the world carries on around them.

**Sound and music keep playing behind the panel, deliberately.** The retail pause menu also
silences audio by broadcasting task command 8 on the way in and 9 on the way out. That pair is not
borrowed, because the pause broadcast only marks a task paused when its handler returns 0 and
iMUSE's returns 2, so the mark is never set and the matching resume never fires `ImResume`.
Copying it would risk leaving the music stopped with nothing to start it again, and audio that keeps
going is a smaller wrong than silence that does not come back.

Window messages are answered here and not passed on, which covers the pause and the menu keys.
**Alt combinations are handed back untouched**, so Alt+F4 and Alt+Tab still work: a modal panel that
can trap somebody in a full screen game would be worse than anything it fixes.

The panel closes itself if it is asked to paint into a frame the player is not seeing. The front
end and a movie look like that from here. Otherwise a level ending could leave the game held with
nothing on screen.

**A controller drives the panel too** (`pad_input.c`, `pad_panel.c`). The pad is read through XInput
once a frame from the same hook that draws the panel, the call `controller_input` makes on a thread
of its own; two readers of one pad are fine, the API keeps no state per caller, and the two DLLs
share nothing but the settings file, from which the one key read out of the other's section is which
slot the pad is in. An empty slot is the expensive case of that call, so it is asked again only
every two seconds. Pressing the opening button (`PadOpenButtons`, View unless set otherwise; two
names make a chord, which fires on the frame its last button goes down) opens or closes the panel,
and hides or shows it while the free camera flies, the open key's own three steps. The game's own
joystick reading sees the same press and runs whatever its controls screen has on the button, which
is the player's to clear; a half second hold was tried first and gave the game the whole half
second, and both stick clicks in its place turned out to be on the game's list as well. Open, the
left stick moves the system cursor, which is the panel's pointer, at a speed in screen widths a
second on both axes, a mouse on a stick, so the hover, the click and the drag all follow the stick
and nothing in the model knows a pad exists (holding the stick still sideways, and then pinning the
pointer to the label column once the pad was touched, were both tried against the sideways drift the
right stick puts into the cursor through controller_input, and each cost the mouse more than it gave
the pad); the D-pad puts the cursor on the centre of the next row's label, scrolling the list by one
when that row is off the screen, and switches the tab sideways, so a press is a step from row to row
with the hover as the mark. The triggers move the slider under the pointer, or the one under the
value row it belongs to, half the track a second fully in, written at the drag's own throttle and
once more on leaving the row. A presses where the pointer is and, held, drags a slider, since the
drag polls the pad's button beside the mouse's; B takes the Escape key's steps, cancelling a typed
value, hiding the panel under the camera, or closing it; the right stick scrolls as the wheel does,
twelve rows a second fully over with the fraction carried between frames, and while it is over the
pointer and the hover are hidden, for a third of a second past its release, since the sideways
motion controller_input fakes from it walked the hover across the rows as they scrolled and a hidden
pointer cannot hover (a D-pad step shows it again); the bumpers page. The search box and typed
values still want a keyboard, and with the panel closed and the camera off the pad is read and
dropped.

**The cursor stays on the panel while the panel is open** (`panel_cage.c`). The panel's pointer is
the system cursor, and a mouse pushed past the panel's edge, or the sideways motion the right stick
fakes through `controller_input`, put it on the game's picture, where nothing takes a click and the
eye has to go looking for it. So once a frame, after the paint, while the panel is shown and the
game has the focus, a cursor found outside the panel's rectangle on the desktop is put on the
nearest point inside it and the frame's pointer reads that point. It is a warp on every move, the
same shape as the engine's own confinement of the pointer to its play area, and not `ClipCursor`:
that was the first cut, and `enhanced_resolution`'s focus guard holds a `ClipCursor` of its own
round the whole window and puts it back the moment the cage it reads differs from the window's
rectangle, so the panel's cage was undone every frame, and the two DLLs share nothing and may not.
Nothing is held: the moment the panel is hidden or closed, or the focus goes, the cursor is free.

**Typing reaches the search box only after a click has landed on it, not the moment the panel
opens.** The box used to take every character while the panel was up, which meant the key that
opened the panel was also typed into it: Windows queues a `WM_CHAR` right behind the `WM_KEYDOWN`
that opens the panel, and with nowhere else for it to go, the open key showed up as the first
character of every search. A click inside the field is what starts focus now, and any click outside
it, or opening the panel fresh, ends it; the field's border and caret are only drawn while focused,
so the box never looks ready to type into before it is.

## The ten cheats this project adds

In the order the panel lists them: **Unlimited ammunition**, **Unlimited health**, **Invincible
NPCs**, **One-shot NPCs (your damage)**, **Giant player**, **Tiny player**, **No clip**, **Super
run**, **Jump boost** and **Free camera**. They need fewer engine sites than that, because
several pairs are two answers to one question and share a single detour.

**No fog was a ninth and now heads the fog rows, under the picture.** It is still the same code in
`cheats_no_fog.c` and still writes the same `NoFog` key; only the row moved. A player looking for
it is looking at the fog, and the fog thickness and fog follow rows below it are the rest of that
answer: this one removes the fog, the second says how thick it is, and the third says what it is
measured against. Split across two groups they read as unrelated.

**Free camera is drawn by its own group**, directly under the cheats: its teleport key, the cheat
itself, the "Animations freeze while paused" and "World runs while flying" switches and the "how
to fly" fold, in that order, because read top to bottom they are the steps.
The cheat is still `CHEATS_OWN_FREECAM` in `cheats_openphantom.c`; the cheats group counts to one
short of it and the jump-boost scale takes its numeric slot, which puts the scale directly after
jump boost's own toggle.

**Lightsaber dismemberment has a heading of its own**, directly under the free camera's. It
writes `[dismemberment] Mode`, 2 or 0, and never reaches into `dismemberment.dll`; that DLL
re-reads the key about once a second. It lived with the settings for a while, on the argument that
its choice survives the session and the cheats' do not, then among the cheats because a player
looking for it looks there first, and a heading that says the word is where that player looks
first of all. The key also takes 1, which corrects which limb the engine's own seven authored
severings take without adding any; nobody wants that on purpose, so the row writes 2 or 0 and a
reader who has set 1 by hand sees the row lit and keeps their setting until they press it.

A cheat whose site did not resolve is shown greyed, never hidden, and cannot be switched.
That is deliberate: a row that ticks and does nothing is worse than a row that says plainly it
is not available on this executable. The five that read the player record (the two sizes, no clip,
jump boost and the free camera) are not installed at all when the cell the engine reads the player
from could not be found, and the jump boost's two sites are each required to load the player from
that same cell before they are hooked.

The panel's own three sites, the drawing, the instant it paints and the hook that opens it, are
found before any cheat places a detour, so a build the panel cannot open on is left without a
single hook in it.

### Unlimited ammunition and unlimited health

The first two are one detour on one short function each, and both work by **declining**, never by
topping a value up.

* **Unlimited ammunition** sits in front of the ammunition spend at `0x00459FD4`
  (`ammo[weaponId] -= n`, base `+0x10`). While it is on, the subtraction does not happen.
* **Unlimited health** sits in front of the damage application at `0x00459ECE`
  (`health -= amount`, health at `+0x00`). While it is on, the subtraction does not happen, and the
  health bar does not flash either, which is right: nothing hurt the player.

Refilling a counter every frame would have fought the pickup code, flashed the bar on frames where
nothing happened, and written a value into the save. Declining does none of that, and switching a
cheat off leaves a state the game could have reached by itself.

The ammunition pattern reaches three bytes past the load that fetches the counter, because the
function that **gives** ammunition is byte for byte identical up to there and differs only in `add`
against `sub`. Stopping earlier matched both, and detouring the wrong one would have made every
pickup a no-op while the cheat was on.

**Not covered**, because it is a different mechanism: anything that *sets* health instead of
subtracting from it. A scripted death and the console's own `kill me now` both go elsewhere.

* **No fog**, in `cheats_no_fog.c`, is a different shape from the other two because there is
  nothing to decline: fog is not spent through any function, it is state the renderer reads
  straight out of the loaded level's own record every frame. This reuses byte evidence
  `view_distance_fix`'s `fog_regime.c` already proved in full, the same `[g_level]` world
  pointer, resolved and cross-checked the same way, with no second derivation, though the two
  DLLs never touch each other's memory: this one resolves its own copy of the site independently,
  the same isolation every feature DLL here keeps. While the cheat is on, a per-frame hook
  (`common/frame_hook.h`, the same one `fog_regime.c` uses for its own easing) pushes the level's
  fog band (`world+0x218`/`0x21C`, both world-unit floats) out past anything the world walk's own
  draw-distance cull can still be showing. The per-frame push survives a level change; it does
  not last only until the next one.

  **The first version cleared `world+0x210` bit 0 instead, the level's "has fog" flag, and field
  testing found that breaks the renderer**: every moving actor drew as a flat, unlit silhouette,
  and setting the bit back did not undo it. Retail never toggles that bit at runtime at all, it is
  set once at level load and held fixed for the level's life, so a runtime flip exercises a
  combination of engine state nothing in 1999 ever produced. `fog_regime.c` never touches that bit
  either, only the band. This now does the same and leaves the flag. See the header comment
  in `cheats_no_fog.c` for the full account, kept in and not quietly fixed for
  the same reason the graphics detail / red highlight mislabelling is kept in
  `cheats_original_actions.c`'s own history.

  **Turning the cheat back off restores the band the level was actually authored with**, captured
  the first frame this file ever saw that level's record, before writing to it. An even earlier
  version declined to restore anything here, the same rule the other two cheats follow, and field
  testing found that the wrong call for fog specifically: ammunition and health decline a
  SUBTRACTION, so their own "off" is just the game's other systems carrying on from wherever they
  already were, but nothing else in the engine ever moves this band, so nothing else was ever going
  to hand it back. The remembered band survives the record being freed and reallocated on a level
  change the same way `fog_regime.c`'s own `is_the_same_level` does: by checking the record still
  holds what this file itself last wrote, not only that the pointer looks the same.

### Invincible NPCs and one-shot NPCs

Two opposite answers to the same fifteen bytes, so they share one detour instead of taking a
signature each. `enemy_receiveDamage` changes an NPC's health in exactly one place, at
`0x004338EC`, and that place is five back-to-back three-byte instructions with no `rel32` and
nothing environment-dependent in it:

```
004338EC  8B 55 08     mov edx,[ebp+8]      victim (character*)
004338EF  8B 42 38     mov eax,[edx+0x38]   health
004338F2  2B 45 EC     sub eax,[ebp-0x14]   minus the damage this call computed
004338F5  8B 4D 08     mov ecx,[ebp+8]
004338F8  89 41 38     mov [ecx+0x38],eax   health -= damage, written back
```

Traced forward to the end of the function, nothing later reads EAX, ECX or EDX left over from
this block, and the first flag-testing instruction after it sets its own flags. That is what
lets the hook decide whether the block runs **at all**, with nothing to preserve about how it
executed. The `+0x38` health field is the one `dismemberment.c` already established from
retail's own death gate at `0x0043707D`.

* **Invincible NPCs** skips the block, so health is untouched.
* **One-shot NPCs (your damage)** writes health straight to zero. The death gate this function
  feeds tests for that. It fires **only for damage that came from the player**, so NPCs
  fighting each other are unaffected, and **only against an enemy**: the victim's body class,
  the one word the engine sides its actors by, is read at the hit, and a victim of the player's
  own class (the party), an escort (class 9, the queen and her guards) or a civilian (class 3)
  takes the ordinary hit. A sabre swing through the queen used to kill her outright with the
  cheat on. The class census behind those three numbers is in `cheats_npc_damage.c`.

Invincible wins if both are somehow on at once: refusing the hit outright is more obviously
correct than a hit that is at the same time "took no damage" and "died".

**One-shot is not indistinguishable from ordinary lethal damage**, and the source used to claim
it was. It is indistinguishable to the *gate*, which only asks whether health reached zero. It
is not indistinguishable to a script gating its own death on a health **band**: the scrapyard
machine in Mos Espa waits for health at or below 900 of 999 with the player nearby and then runs
its own explosion. Ordinary damage walks health down through that band and the script fires; one
store of zero steps over the band entirely and it never does. `cheats_npc_damage.c` records what
that costs in full.

### Giant player and tiny player

Also one detour for two rows, on `rdThing_Draw` at `0x0040FE70`, which is every drawn object's
own render call. The hook acts only when the incoming thing is the player's, established by
walking the player record to its actor and comparing that actor's `rdThing*`. Particle sprites
reach the same function through `emitter_drawParticles` and are never touched.

**The scale trick is already in the retail game.** A few instructions past this prologue, gated
behind a cheat-flag slot and a hardcoded four-character model-name match, retail applies a flat
3.0x scale to this exact incoming matrix through a small "compose a diagonal scale into this
transform" utility. This calls that utility, with no reimplementation, and finds its address
out of the call, with no independent signature of its own. Giant player uses retail's
own 3.0. Tiny player uses 0.35, which has no retail precedent in that direction and is simply a
first guess at small but still visible and playable.

The player's thing is chased off the player-record global on every call, never cached. The
incoming matrix is a full rebuild of the player's position and orientation for this frame, every
frame, so scaling it is inherently transient and switching either cheat off needs no un-write:
the next call simply stops scaling.

**Field-tested, and the caveat is kept, not quietly fixed.** `matrix` is the caller's own
working buffer, not something owned by this call, and `bapobj_drawAll` reads it again right
after the call returns for something that has nothing to do with rendering. So scaling it in
place also scales the force-push ability's reach and power. A local-copy version that left the
caller's numbers alone was written and worked, and was reverted: combat is not meaningfully
usable at either scale anyway, so the extra copy bought correctness nothing was asking for.

### No clip

Everything the player can be stopped by, except the floor. `cheats_noclip.c` owns it, across five
small hooks and one per-frame tick.

**This is not the noclip this project removed.** That one detoured `0x0044C36D`, which turns out
not to be a collision routine at all: it is phase 9 of the player's own locomotion phase table.
Suppressing it suppressed a whole phase of a state machine, only while the dispatch happened to be
in that phase, and the floor went with it. Everything recorded against it, falling through
modelled floors most of all, followed from the site, not from the idea.

**Five things can stop the player, and each needed its own site.** Four of the five were found by
measuring the running game, not by reading it. They are listed here because the set is not
recoverable from any one of them.

| what stops you | site | what it is |
|---|---|---|
| walls, on the ground | `0x0040C1AE` | the universal wall raycast |
| walls, in the air | `0x0040C870` | its stationary sibling, which the airborne tick uses instead |
| air-block fences and low ceilings | `0x0044C59D` | `Plr_AirMoveGate`, the veto on a move made in the air |
| edges catching you as you pass | `0x0044C78E` | phase 8 of mode Fall, the ledge grab |
| people | `0x004131EB` | `bapobj_cylinderPush`, bodies being solid to each other |

That the set is complete is checkable: every write of zero into the player's moved flag was
enumerated in the image, eleven sites in all. Four are the ones above, three are irrelevant
(death, the tripod turret, non-player code) and the rest were already covered.

**The floor is a different function and is never touched.** `bapmap_probeFloor` is not on this
list and nothing here goes near it. The player keeps standing on ground throughout.

**The air-block fences are not a rare case.** The third hook matters: 73,360 faces across the
eleven levels carry that flag, 22 per cent of every face in the game, and another 29,968 carry the
low-ceiling one. Without that hook clipping works on the ground and then stops working the moment
the player leaves it.

**The ledge grab was the subtlest.** It is not a collision test at all, so no wall probe can reach
it: it looks half a unit ahead for an edge and puts the player on it. A glide keeps the player in
mode Fall the whole time they are clipping, so every edge they pass is a candidate, and the walls
that appeared not to work were the ones with a grabbable lip. They were not being blocked, they
were being caught.

**People are not geometry.** A character in a doorway is a cylinder, not a polygon, so no wall
probe could ever see one. NPCs stay solid to each other; only the player stops being part of the
crowd. An NPC walking into the player can still shove them, left alone deliberately because
suppressing it means reaching into everyone else's collision loop to hide one body from it.

**The glide is what stops the player falling out of the world.** Beyond a wall there is often no
floor at all, because geometry is only modelled where the player was meant to go, so a working
floor probe correctly reports none and gravity does the rest. Height is therefore held for exactly
as long as there is nothing to stand on, and released the moment there is, so ordinary movement
over real floor is not touched at all. "Nothing to stand on" means nothing within a sane drop,
not nothing whatsoever, because a lower storey far below is not somewhere to be set down.

**NPCs and the AI stay solid to the world.** They walk the same wall raycast for their locomotion,
line of sight and path checks, so every hook answers only for the player, identified by the
address of a field in the one player record and never by a position value.

**Buttons and push blocks still work**, because those probes ask the same function a different
question, looking for a face to act on, not one to be stopped by, and are excluded by mask.

**Free camera and this are mutually exclusive.** Both write the player's position from the same
per-frame site in the same frame: free camera freezes the simulation and teleports the player to
the camera on the way out, and this holds the player's height every frame. Switching either on
turns the other off, and this one also declines to act while the camera is flying, since the
toggle rule can be bypassed by a saved state or a level change and the check costs a comparison.
Free camera is the one that wins, because it is the one you cannot leave without its own hotkey.

**Four of the five hooks are optional.** Only the ground wall probe is required; if any of the
others stops resolving the cheat loses that one behaviour and says so in the log; it does not
disappear. An earlier build made one of them required and a signature that matched two
functions instead of one took the whole cheat down with it.

**A doors-only variant was built, tested and dropped.** It identified a door leaf by the mover
owning the polygon, which worked and was proven against the shipped levels. It is described at the
head of `cheats_noclip.c` with what bringing it back would need.

### Super run

One float, no hook. The player's speed is not written by the run key: each stand tick decides
which clip plays and hands two speed caps to `Plr_RampSpeedCaps` (`0x0044D05C`), which walks
the live caps toward them by 0.25 a tick, and the integrator approaches the forward cap and
multiplies by the frame's dt and the turn penalty. The caps are pushed as immediates, 3.5 world
units a second for a run at `0x0044CE01` and 2.0 for a walk at `0x0044CF1D`; the two lightsaber
lunges reach the same routine with a table value. Only the run push is touched: while the cheat
is on its immediate holds 3.5 times the speed row's number, the engine's own ramp carries the
speed up over a few ticks, and off undoes the write and the ramp brings it back down.

The speed is the row directly under the toggle, `Super run speed (1.1 to 4.0x)`, typed or dragged
on the track beneath it, the same shape as the draw distance. A drag writes `SuperRunScale` on a
hundredths grid and, while the cheat is on, rewrites the run cap at once, so the speed follows
the hand; the row and its track are unavailable with the toggle, since a number nothing is hooked
to would be a lie. Its `Default` is 2.0, what the key falls back to with nothing written.

What a faster run does and does not change: the run clip plays at its authored rate, so above
about 1.5x the feet visibly slide, the same gait note enhanced_input's pad stick makes; the turn
penalty still applies; the walk, the lunges and the swim are as shipped; the wall probe is swept
per substep, so at the 4x ceiling, 0.44 units a step, nothing is passed through. The pattern
carries the 3.5 itself, matched once in the retail image, and the site has to read 3.5 at install
or the row is unavailable.

| Site | Address in retail | What it is |
|---|---|---|
| the run cap push | `0x0044CE01`, operand at `+1` | `push 3.5f` before the call of `Plr_RampSpeedCaps`; the imm32 is written through the journal and put back |

### Jump boost

Two hooks, on the Jump mode entry and the Jedi Jump mode entry, because different characters
route through different ones. Either alone still helps whichever characters use it, so unlike
free camera below a partial resolve here is kept: it is a real cheat for part of the cast rather
than half a feature that does nothing.

Each hook calls the original **first and unconditionally**. This is a boost, not a
reimplementation. The jump happens as retail built it, guard check and all, and
only once it has decided to jump and written its own vertical velocity does the cheat scale what
is now sitting at `+0xB4`, whichever path the original took, the fallback constant or the
per-character table value. The multiplier is a number you can type on the cheat's own row,
with a track of its own on the line under it, the same shape super run and the draw distance
have. Its `Default` is 1.3, what `install_jump_boost()` seeds the scale with; unlike every
other number this panel edits that one is held in memory and no key carries it, so it reads
1.3 again on every start whatever it was left at.

**A higher jump is a longer fall.** While the cheat is on it also suppresses three things
retail's own ground-contact code does to a long fall, none of which is a cheat of its own and
none of which has a row:

* the fixed ten-point landing damage every hard landing already risks, taken through this
  module's own damage hook and not around it, so Unlimited health still wins if both are on;
* the outright force-kill, which retail applies unconditionally with no health check anywhere in
  the path, either after two seconds airborne or past a second fall-distance ceiling. This one
  was found only after a field report of a boosted jump ending in a death screen and a reload;
* retail's dramatic-fall camera, which pitches down to watch the player from above and, because
  nothing in its landing path ever expected a fall this big to be survived, never lets go
  afterwards. Found the same way, after the deaths stopped.

All three fire from the same "this fall just became significant" transition and stop mattering
the instant jump boost goes off. `cheats_fall_consequences.c` owns them and carries the full
mechanism. The same grace is granted for one landing by the free camera teleport below, because
arriving at a camera that was flying is a fall the player did not choose to take.

### Free camera

Two engine sites and a hold on the simulation. It holds the world still through `sim_pause`, by
no means of its own, and writes the camera pose **after** the engine has composed it, so it never
fights the original for the fields.

Both sites, the pause flag and the camera object pointer must all resolve. A partial resolve is
not offered as half a feature here: a camera that could roam but never stopped the world moving
underneath it is not this thing, and neither is a pause with nothing to look through.

**Flying it from a pad.** The camera reads the frame the panel's pad reader took: the left stick
flies along the view with its deflection as the speed, so a half push glides, the right stick looks
on both axes at `PadLookSpeed`, the triggers climb and dive, the bumpers step the speed by the
wheel's own ratio on the press and go on stepping while held, A ends the flight bringing the player
to the camera (the bound key's meaning) and B ends it leaving them where they were (F4's). The right
stick's sideways half also reaches the camera as mouse motion, faked by `controller_input` for the
game's own camera, so while the stick is over the mouse's sideways counts are dropped, or the camera
turned twice as fast under a pad as under a mouse.

**Flying it.** `W`/`S` forward and back, `A`/`D` strafe, `E`/`Q` up and down, the mouse to look,
the wheel to change speed. Speed moves by a constant ratio per notch, not a constant amount, which is Blender's fly-mode feel: even control at both ends, where a fixed addition would
be enormous down low and glacial up high.

**Leaving it.** The bound key ends the flight and **brings the player to the camera**, which is
usually what the camera was being flown for, so it is the action worth putting on a key of the
player's own choosing. `F4` is fixed and means the opposite: put it back, leave without moving.
A fallback that always means the same thing is worth more than one more thing to configure.
Alt+F4 still closes the game, because Alt is not read here. With no key bound the cheat refuses
to switch on at all.

**Holding the animations.** The pause skips the substeps and nothing else, and the puppet tracks
are not stepped by the substeps: bapobj's draw pass advances every object's four tracks by the
frame delta on each frame it draws them. So behind the panel and under the camera every walk
cycle ran on the spot and every idle swayed. The engine's own pause menu stops that through a
draw latch of its own, `g_objDrawPaused`, written by the one-line pair `bapobj_drawPause` and
`bapobj_drawResume` (`0x00411019`, `0x0041100A`), which the draw pass tests before it steps a
track and which also holds the interpolation alpha at its last value; `sim_pause` resolves the
pair as one pattern, reads the cell out of both stores, and writes it beside the pause flag
whenever a pause is in force, with the same capture and restore. The "Animations freeze while
paused" row in the Free camera group is the switch, off as shipped so the pause keeps the look
it has always had, and kept as `[dev_overlay] PauseFreezesAnimation`. It and "World runs while
flying" are one or the other: switching either on switches the other off, since a world that
runs is not paused and there is nothing for the freeze to hold. An object the engine flags to
animate while paused still does, as it does behind the pause menu. Played 2026-09-15: with the
freeze on, nobody walks on the spot under the camera; with the world running, the crowd carries
on while the camera flies and the player stands.

**Letting the world run.** The flight holds the simulation, and a held world is the wrong thing
for watching a fight or a crowd from where the camera can go, so the group's third row, "World
runs while flying", lets it carry on under the camera: `freecam_world.c` asks `sim_pause` to let
the simulation run under the holders it keeps (`sim_pause_let_run`, which leaves the holders'
own accounting alone, so the flight's end and the panel's close still put the right value back)
and takes the player's input through the same holders the panel uses (`input_freeze_hold`), so
the flight keys steer the camera alone and the player stands where they were left while
everyone else moves. It is a setting, kept as `[dev_overlay] FreeCameraWorldRuns` and off as
shipped, applied on every frame of a flight so it takes mid flight too, and released with the
flight.

**Hiding the panel.** While the camera is flying the panel cannot be closed, because it is what
holds the game still under the camera (see below). The dev menu's open key and Escape hide it
instead, so the picture is the camera's alone, and the same keys bring it back; it comes back by
itself when the flight ends. Hidden is only not drawn: the freeze, the pause and the keys the panel
swallows are all still there, so hiding is safe where closing is not.

**One guard.** Mouse look measures the pointer's movement between frames
against an anchor it re-centres each frame. Anything else that also moves the pointer, a cursor
cage or another overlay, turns that difference into a constant that is not hand movement and that
arrives again on the next frame, and the frame after. A steady vertical bias drives pitch onto
its clamp and pins it there: the camera stares at the floor, `W` flies into it, and no hand
movement lifts it. Two guards close that off. A jump larger than a quarter of the screen in one
frame is not a hand, so it contributes no rotation and simply re-syncs the anchor; and the anchor
follows where the pointer actually **landed**, not where it was sent, because a cage can
refuse the position asked for and a stale anchor then measures the same refusal for ever.
Reported from a tester's machine and confirmed fixed there. It was never reproduced here, which
is the point: the other writer it collides with is not something every machine has.

### Level selection: the skip and the level a new game starts at

A group of its own, directly under the cheats, holding the two rows about which level is
played: "Skip to next level (debug)", which was the tail of the cheats, and "New game starts
at", whose chip shows the choice as the number and the level's file stem, `6 espa`, and whose
press opens a list of the eleven levels in the game's order, the chosen one marked, closing on a
pick, the same shape as the window group's size list. Picking level one writes the setting
as `0`, the game's own new game. It exists for modding: a save game carries the level it was
made in, so a modded level file cannot be entered from a save of the unmodded game, and the
shipped saves are all of that game. A level has to be entered as itself, from a new game, and
this is how a new game gets there.

The engine has the mechanism already. `level <n>` on the command line sets the front end's
"start at" index, and New Game loads that row, plays its intro movie, shows its title and stamps
the checkpoint as reaching it in play would. What it cannot do is survive the front end: the
campaign loop zeroes the index at the end of every level, before the front end comes back, so a
value set from inside the game is gone by the time New Game reads it, and the panel is not drawn
in the front end. So `start_level.c` sits on `campaign_loadLevel` (`0x0043F70A`, the same site
dialogue_anim_fix detours, chained) and redirects the one load that is a new game: the campaign
index reads 0, the level outcome is below 4 (4 to 10 are the fail variants, so a restart of
level one after a death is not a new game), the restore flag is clear (a save being loaded copies
its own flow block over the live one first, and that block carries the flag set), and the path
is the table's own first row. Then the campaign index is written to the chosen row and the
original is called with that row's path, so the movie, the title and the checkpoint after the
load all read the new index. The three cells and the table come out of three data sites in
`campaign_run`, each operand read back and the two readings of a cell made to agree:
`0x0043EBD6` (the restart), `0x0043EC59` (the New Game load) and `0x0043ED6B` (the level loop).

What the player has on arrival is what a new campaign gives plus what the level's own script
hands out, not the kit the shipped saves carry from the levels before. Kept as `[dev_overlay]
NewGameStartsAt`, off as shipped. Played 2026-09-15: Mos Espa picked from the list, quit to
the front end, New Game, and the level opened with its own movie and title.

### Entity spawner

A group of its own, directly under Level selection, called "Entity spawner" in the panel. In the
source, on the wire and in the log it is still the NPC spawner: its files are `npc_spawn*`, its log
lines begin `npc spawner:`, so a field log compares against every older one.

Eight rows and a fold: a note counting the spawns alive against the cap; "Place with the mouse",
with the entity it would place on its chip; the two keys of that mode, "Key: place with the mouse"
and "Key: turn to face you"; "Entity to spawn", which opens the list; "Spawned entities", which
carries the four behaviours on the row itself, Stand, Follow, Attack and Help, the chosen one
filled; a note under it reading "Attack fights you; Help fights for you"; the remove; and "About
spawned entities".

**The mouse is the only way in.** No row puts a copy a few steps ahead of the player: a copy goes
down where the pointer puts it and nowhere else, on a spot the player can see and the mode has
probed.

The behaviours were a list of four rows that opened, each with a sentence saying what it does.
Four words fit beside the name with room to spare, so the choice is shown whole and there is
nothing to open. Two of the four sentences said what their own word says, Follow and Attack. The
other two did not: Attack and Help are both fighting and what differs is who is fought, and Help
follows as well, which its word does not say. The note under the row holds 48 characters and no
more, so it carries the first of those, with an object on each verb: written "Help follows and
fights", the second verb read as fighting the player too, which is the one thing the line exists
to tell apart. The other two, that Help follows and that Stand turns to face you, are a line in
"About spawned entities", which has the room. When a spawn or a wish is refused, a row under the
two keys says why, "Refused: 16 are alive, the most at once", in a single player game as in a
session, until the next spawn or a new world. The rows read unavailable with no level loaded.

**Why a row is unavailable.** Over the refusal, whenever anything in the group is out of reach,
stands a row that says what is in the way: "Why: the session runs no copies", "Why: no level, or no
player in it", "Why: this level offers nothing to spawn", "Why: nothing is chosen to place", "Why:
the camera cells did not resolve", "Why: the spawner cannot raise a copy here". One rule decides
it (`spawn_reason.c`): the placement mode asks it before it starts, every row of the group asks it
before it offers itself, and the row above and the log print its sentence. There is no second
spelling of the question anywhere, which is what let the mode and the rows disagree before. Once a
world the log also prints
the numbers behind it, whether the group is usable or not: whether a session is running, whether the
multiplayer runs the copies in it, whether its record was read, its cap, this machine's world slot
and the epoch.

**In a multiplayer session.** The group is the one a running session leaves alone, as long as the
multiplayer runs the NPC copies in it, which it does in every co-op session over a socket. Both the
host and a client can spawn and place; what each asks for becomes a wish and the host grants it. A
session that does not run the copies is the only one that takes the group, and the row that says
why reads "Why: the session runs no copies".

Each copy built from a grant writes a line naming the player who ordered it, the key it was built
under, the file and the place, for the first eight of a world; after that one line says they are no
longer named and the count at the end of the world carries them. Without it a wish that worked and
a wish that never left looked the same in a log.

**What is offered.** The list is shelved under headings: From this level, Figures, Creatures and
droids, Vehicles, Pickups, Guns, each sorted by the name a person reads. A file is offered when the
data say it is safe to raise, four gates in this order (`entity_offer.c`):

* no clip at all: never. The spawn plays clip 0, the play refuses it on a file with no tracks and
  leaves the animation slot at -1, and the script opcodes then write in front of the track array.
  Eighteen files of the archive have no clip, and no retail level places one of them.
* `inviso.baf`: never, the engine's own script anchor with no body.
* a node named `head` or `chest`: a figure, as before.
* a class the eleven retail levels place the file under: 1 to 3 a living thing, 10 to 27 a pickup
  (the player's pickup range), 4 with the nodes `turret` and `target` the one gun, `tripod.baf`.
  Class 0 (props, ships, quest items) and class 8 (the tank) are not proven for a copy and are left
  out.

The classes live in the levels, which the panel never reads, so they are carried as generated data,
`entity_class_data.h`, one row per archive file with its clip count and node facts, written by
`tools/census_entity_classes.py` from `big.lab` and the retail levels (`--check` compares the
committed table with the data). The unit test runs the rule over every row: 303 files, 204 offered,
99 refused, not one of the clipless files among the offered. At run time the archive is read as
before and held against the table, and a line says what the offer came to and how far the two
agree.

A pickup is raised as its own class, so the player takes it up as the level's own, and it runs the
still script whatever the behaviour row says, which then reads `n/a`, as it does for the gun. Both
are functions of the file name, so no behaviour number is added and every machine of a session
builds the same copy. In a session a pickup the host takes is gone for everyone: its life ends on
the host, and every client gives up its copy. One a client takes is taken on that client only, and
the others still see it.

Names come from a table in `entity_names.c`, which is a courtesy and never a filter: a file it does
not name shows its stem. The level's own kinds show how many the level placed, `x31`.

**Placing with the mouse.** "Place with the mouse", or its key, hides the panel and frees the
pointer; the player stays held. In a single player game the world stays held as it is under the
panel, and after each copy placed it runs for two of its substeps, counted by the panel's module
node, so the copy stands where it was put: the engine's spawn writes only the actor's position,
the body's stays at zero until the first substep hands it on, and a world held before two substeps
have passed would draw the copy somewhere between the world's origin and its place. In a session the
world runs, as it does under the panel there. The entity is drawn where it would stand, solid, with
a render handle of its own and nothing spawned for it (`spawn_ghost.c`), and four corners around it
say whether it may: green where a left click places it, red where it will not, with the reason in a
line under the pointer. The ray under the pointer is turned round out of the camera's own numbers,
read at the end of the scene from the cells the world pass projects with (`world_camera.c`,
`world_pick.c`); where it strikes, the floor is looked for just above the strike and the entity
stands on it. A body is not a point: four short rays go out from the place at a body's height, as
long as the entity's collision radius, and a wall they strike pushes the place away from it, so a
copy set at the foot of a wall stands beside it rather than half in it. Refused: nothing struck, no
floor within reach, a moving platform, too tight between two walls, no headroom, a copy or the
player already there, the cap reached.

* left click places, again and again, the entity staying at the pointer; a second click within
  150 ms is the same click;
* the wheel turns it, 15 degrees a notch, 1 with Shift, 90 with Ctrl snapped to the nearest
  quarter; until it is turned it faces the player, the tripod with its back to the player;
* the middle button or the face key turns it back to face the player;
* every copy in view gets thin corners, white for this player's and grey for another player's;
  the one under the pointer, the first the ray enters before what it struck, gets bright corners
  and its name, and a right click removes it, in a single player game and on the host of a
  session, where every copy may be removed. A copy behind a wall is never under the pointer. A
  ridden tripod is skipped with a line. A client cannot remove a single copy yet: that would be a
  new wish on the wire, and the line under the pointer says so;
* Escape, or the mode's key, brings the panel back; the key that opens the panel closes it. A new
  world, a closed panel, a lost player or the player's death end the mode, and the free camera
  takes the mouse from it while it flies.

Who has the pointer, the wheel and the pause at any moment, the game, the panel, the free camera or
the placement mode, is one answer, `input_owner.c`, which every part asks.

The rest of this section is the spawner underneath, as it was built.

A level's actors are placements, one record each in a directory the world record
carries at `+0x20C` (count at `+0x204`), and `spawn_actor` (`0x00437250`, cdecl: the record, its
index, a script index or -1 for the record's own) is the one routine that turns a record into a live
actor. The activation scan calls it for every placement the player comes near (`0x0043722E`) and the
level scripts call it for their own spawns; the group calls it too. The routine is found from its
own opening and from that call, and the two have to agree; the world pointer is read out of the
opening's two operands, which have to agree as well.

What is spawned is a copy. The engine keeps one live actor per record and writes its death
bookkeeping back into it, so a second actor on the level's own record would take over its live word
and its spawn state. The chosen placement is copied into a record of the group's own, from a ring of
128, one per actor the engine's pool holds, that takes a record again only when no actor names it
any more, with the position moved, the yaw turned to face the player, the starting mode set to 0,
the class set to 2, the reveal count zeroed, no removal by distance and a route of one node, the
spot it stands on (`npc_spawn_record.c`); the copy goes in under an index of its own, 256 + k, past
every placement of every level, which the engine reads back in two places only, the enemy block of
a savegame and a tripod gun's mount. The live words the engine keeps in those records are what the
cap counts, sixteen alive at once against the engine's pool of 128 for the whole level. A copy
stands where the mouse puts it. The list is by model, one row per actor file; of a kind's placements
the plainest is the source, one the
activation scan spawns and the level did not name.

**A copy runs a script of this project's own, never its source's.** A placement's script is the
level's business: a story stand-in's opens his cutscene (a spawned Obi-Wan started Mos Espa's
opening scene over again and hung the level), a boss's sends him off to the marks it names (a
spawned Darth Maul ran its source's script for a day, 2026-09-16, and walked off to where the duel's
Maul stands), a companion's follows the player. The scripts a copy can run are written in the AI
language of the project's level editor (`scripts/stand.bais`, `scripts/follow.bais`,
`scripts/attack.bais`, `scripts/shoot.bais`, `scripts/droideka.bais`, `scripts/still.bais`,
`scripts/ally.bais`, `scripts/allyshot.bais`), compiled by the editor's compiler into the engine's
own bytecode record by `tools/compile_spawn_scripts.py`, and carried in `spawn_script_data.h`; the
patch builds without the editor. Stand plays the stand clip and turns to face the player; Follow
walks after them, stops within a unit and a half facing them, and goes again once they are three
units off (two thresholds, since one made a follower stutter on the line while the player walked
slowly); Attack is
two records chosen by the model. A model with a swing clip gets the hunter the editor proved in the
Federation ship, the Tatooine duel's own chase and swing calls with the model's run and two swing
clips, which runs at the player, swings from its reach and swings again while they stay close; a
model with a fire clip and no swing (`scripts/shoot.bais`) walks to five units and fires the
record's first weapon (attack kind 8, the fire op's remap through the placement's weapon slots at
`+0x8C`), so a bazooka droid fires its rockets and a guard his bolts as the level's own do; an
archive creature's record and an empty slot get the blaster bolt, shot kind 1. The twin muzzles are
the one weapon the fire op tests for by the kind it is called with, before the remap, and the level
scripts choose it, not the records: every destroyer, tripod and hovering droid placement in the
retail levels carries empty weapon slots and a script that names kind 2 outright (read out of all 22
levels). So a copy of those three models, `TWIN_MUZZLES` in `spawn_scripts.c`, names kind 2 and
fires both barrels from the nodes the model marks by usage, as the level's own do. The destroyer
droid alone has a record of its own under Attack, `scripts/droideka.bais`, the shape of the game's
"dest" script: it rolls at the player on its roll clip, unfolds within two and a half units, widens
its cylinder and raises the shield (`extra_func` 2, the fxshield the engine hangs on the body),
fires the twin muzzles while they stay within seven units, refunds three hit points per hit while
the shield is up until it is down to 25, and folds and rolls again when they leave. Help is the same
two records turned the other way (`scripts/ally.bais`, `scripts/allyshot.bais`): a helper follows
the player as Follow does until the nearest class 2 actor, an enemy or another spawned copy, comes
within ten units, then a swinger runs at it and swings, chasing with no arrive width so it pushes
until the bodies touch and swinging from the model's reach plus 0.3, and a shooter stands and fires
at it from twelve; both come back when none is left in range. A helper is raised as class 1, the
player's own side, so the other helpers are not its enemies (the target kinds 2 and 4 are the
nearest class 2) and the enemies' fire is aimed at the player; the pair pass lets class 1 touch
class 2, and the log shows a helper's swings taking three or six points a hit (played 2026-09-16).
Its clips are the one model's own, so that record carries no placeholders. The tripod gun is the one
copy not raised as class 2: the Use key mounts an object of class 4 ahead of the player
(`Plr_GroundActions`), so a spawned tripod is class 4 and can be taken over like the level's own,
which puts its AI in standby while it is ridden. It takes a record of its own whatever the row says,
`scripts/still.bais`, which neither turns nor moves, since it is a thing to mount and fire and one
that turned or fired on its own got in the way of that; and it alone spawns facing away from the
player, its back to them, since it is mounted from behind and fired forward. The archive list
carries it by name, since it has no head or chest node to count as a creature. An archive model the
levels arm gets the shot kind they arm it with (`MODEL_WEAPONS` in `spawn_scripts.c`, read out of
all 22 levels: the droid fighter's 13, the Tusken gunner's 17, the bazooka man's 8 and so on), the
rest the blaster. The reload is the record's fire interval, which the engine draws a random part of
after each shot and floors at half a second; a bolt keeps it, as the droid fighter's own script
fires every tick it can, and only the explosive kinds (rocket, tank shell, grenade, thermal
detonator, energy ball, fireball) are raised to four seconds, since the bazooka man's placement
carries a short interval and paces its rockets in its own script, which the copy does not run. A
shooter turns to the player as the fire op does itself, and walks again past six. So a boss or an
archive creature fights without the level's script and the marks in it. Two numbers follow the model
as the clips do: the chase speed is the duel's 5.05 for a model with a run clip and Follow's 3.0 for
one without, which would slide; a model with no walk clip either, a turret of a droid, gets no walk
at all in Follow and in the shooter, so it turns in place, and the shooter then fires from twelve
units in place of six. A flyer, a record whose move mode is 2 (a hover) or 4 to 6 (pitching toward
where it goes; 3 is a walker with no collision, which the levels use for trees, power-ups and the
droid fighter), moves on its hover clip as the gunboat's own script moves it and needs no walk clip;
a level's flyer keeps its placement's move mode, an archive one gets the mode the retail levels
place that model with as a creature (`FLYERS` in `spawn_scripts.c`, read out of all 22 levels; the
droid fighter is placed flying only as a class 0 ship, and walks), and either spawns 1.8 units above
the player's feet, about head height. A flyer's height is tracked toward its move destination's z
(`move_trackZ`), and `move_to`'s destination 3 is the player's feet, so a flyer sent at the player
comes down to the floor, as the level's own gunboat does when its script sends it at them; a copy's
scripts send it to destination 0 instead, the placement's authored position, which for a copy is the
spawner's own ring record, and the spawner keeps that record 1.8 units over the player's feet every
frame from the scene end hook. Two models with a walk clip are held the same way by name,
`baronbaz.baf`, the kneeling bazooka man, whose level script kneels him, fires and gets him up, and
`drdfitr.baf`, the droid fighter, which deploys where it lands; both looked wrong walking after the
player. The list is `HOLDERS` in `spawn_scripts.c`. The reach is 0.7 units for a model with a sabre
node, 0.65 with a weapon mount, 0.6 with neither, hands and teeth. All of them poll the death each
cycle, play the model's own death clip, lie eight seconds and remove the body, the shape the
editor's hunter proved in the Federation ship, because the engine's own death path does not finish
for a body no script claims. `spawn_scripts.c` lays the record out as the loader would (the entry
count at `+0`, the state labels at `+4`, the pool pointer at `+0xC`, the entries from `+0x20` and
the pool after them) and writes the model's own clips into the pool slots the header marks: the
stand is clip 0 in every file, the walk is found by name (`walk`, `wlk`, `run`, never a walk back)
and the death by the clip's usage mark. After the spawn the copy's script pointer (`+0x28`) is
pointed at that record and its instruction pointer (`+0x2C`) cleared, before the copy's first tick.
A copy is class 2 because the contact handler hands a hit to `enemy_receiveDamage` for class 2 and
for no other, so a townsman or a story stand-in could not otherwise be struck, with the two
exceptions above, the tripod at class 4 and a helper at class 1; the story people keep the hit
points their level gave them, so Jar Jar and Obi-Wan take everything and the Jawas do not. A source
with no hit points gets ten. Giving a copy another placement's script, the level's
everyday townsfolk script with its clips translated by name, was tried before this and taken out:
the borrowed script greets the player in the townsman's voice and follows them about.

Placements of class 1 to 3 are offered, the engine's own sorting at `+0x34`: 0 is props, ships and
pods, 1 the story's principals, 2 and 3 the crowds, guards, creatures and droids, 4 a tripod gun and
13 upward the power-ups. A hosted placement (flag `0x2000`, parked on the player's body) and the
script anchor `inviso.baf` are left off, and the retail assert's bound of 256 placements is the most
that is read.

**The archive.** The list runs on past the level's own files into every creature in `big.lab` the
level did not load, marked "from the archive". `actor_catalog.c` reads the archive once, the way the
engine's own loader lays it out (a 16 byte head, 16 byte entries, a name table), and for each `.baf`
keeps the name, the clip count at `+0xC8` of the file header, whether it has a `head` or `chest`
node, which tells a creature from a ship, a pickup or a door, and whether it carries a `sabre` node
or the `weapon` mount; the tripod, which has neither node, is carried by name. Nothing is loaded
until one is first raised. Then `actor_loader.c` asks the engine's own resource layer for the file
the way the hero code does, `res_Alloc` with the tag the actor loader is registered under
(`0x42414653`, the bytes `53 46 41 42` the hero code pushes) and the file name, found from the two
calls in `player_spawnHero` and `player_despawn`, which have to name the same player block; a loaded
file is kept until the level changes and given back through `res_Free`, as the hero's is. Before the
spawner touches it the loader checks that what came back is an actor: the model and the clip table
at `+0xE0` and `+0xE4` must point inside the block whose size sits at `+0xA8`, and the model's node
count must be sane; the wrong tag once handed back a file's raw bytes, and a body bound to those
took the level with it. The spawn routine binds the model by the record's index into the level's
model table, so a foreign record names slot 0 and the loader's file is lent to that slot for the
length of the one spawn call and put back after; the record itself is the plainest of the level's
own placements copied for its everyday numbers, with the mode the retail levels fly that model with,
the weapon they arm it with, a reload just over the engine's floor and the file's stem for a name
(`npc_foreign.c`).

The point ahead is the player's position plus (-sin, cos) of the heading at `+0x2A0`, the engine's
own forward. It may be inside a wall or another actor, and the engine's push layer sorts that out on
the first tick as it does for a placement authored too close. A spawned actor is the level's from
then on: it is culled with the corpses when the pool fills, and it is gone with the level.

| Site | Address in retail | What it is |
|---|---|---|
| `spawn_actor` | `0x00437250` | its opening, through the second model table load; called, never patched |
| the activation scan's call | `0x0043722E` | `call spawn_actor` after its three pushes; the target has to be the routine above |
| `enemy_delete` | `0x00437850` | its opening, and the reveal assert (line `0xF8B`) that has to lie inside its 422 bytes; called with reason 1, a script's remove, for each live spawn when the remove row is pressed |
| `g_level` | `0x008A0060` | the two operands in the opening, which have to agree |
| the hero file's load | `0x00447ECF` | `player_spawnHero`'s `res_Alloc` call; the tag, the name table and the call are read out of it |
| the hero file's release | `0x00448262` | `player_despawn`'s `res_Free` call; it has to name the same player block |

Played 2026-09-16, on the rig: a Tusken, a sithgoon and Maul on Attack in Theed and Mos Espa, run up
to touching and swinging, each dying on its own clip; the bazooka man and the droid fighter holding
their ground and firing their own weapons; the hover cannon at head height following and firing; the
destroyer rolling, unfolding behind its shield and firing both barrels; a tripod spawned back to the
player, mounted, fired and dismounted; Watto, a Coruscant thug, a Wookiee, Maul, a droid and
Palpatine from the archive on the Tatooine sand; and Tuskens on Attack against a Maul on Help, with
the log showing his swings taking three and six points a hit until one fell.

#### Spawned entities in a savegame

A save keeps every living copy where it stands, facing as it faces, with its health, and a load
brings it back. Three heads are hulled for that and nothing else. `enemy_saveBlock` takes every copy
out of the enemy pool's chain for the length of the call and puts it back exactly where it was
(`npc_spawn_list.c`), because the enemy block writes each actor's index and a load would read the
level's directory with 256 + k. The copies go into the panel's own block instead, written by an
engine module node of its own, `OPNpcCopies`, which a hull on `sys_startup` installs right after the
engine builds its own (`npc_spawn_node.c`); the block is read back when the load hands it over and
the copies are raised at the first frame after the load has finished, never in the middle of it.
`save_saveGame` refuses a save while the player sits on a spawned tripod gun, with the engine's own
message box ("Not saved / Leave the spawned tripod first"), since a load could not give the mounted
gun back; it also refuses one when the pool cannot be walked to keep the copies out. Dying copies
are not saved. Without the enemy block's hull or the module node no copy is raised at all, and
without the refusal no tripod gun. The original game loads such a save, steps over the panel's block
by its length, and only the copies are missing. A save written before this carries its copies under
their source's index and still loads them as one more actor of that placement, running the
placement's own script; that cannot be told apart afterwards and is not repaired.

#### Spawned entities in a multiplayer session

While a multiplayer session runs the copies, over the LAN or through the relay, on the host and on
a client alike, the panel builds nothing of its own accord. A click of the placement mode asks the
host's multiplayer for a copy, and the host's cap decides (`[multiplayer] NpcCopiesMax`); every copy
is built on every machine, under the key the host handed out, and the count row shows the host's
cap. A refusal is a line in the log with its reason: the cap, a full actor pool, no level, a host
with no panel that could build it, a machine that cannot hold a copy still, or too many wishes too
fast. The group shows it too, on a row under the count ("Refused: the host's cap is reached"), until
the next wish. Every epoch, a level's end or the session's, the log also carries one line of what
the panel's copies came to.

"Remove your spawned entities" removes the copies this player asked for, wherever they stand. On
the host a second row, "Remove every player's spawned entities", removes all of them. A copy
somebody rides is not removed while they sit on it. A player who leaves hands their copies to the
host. A load on the host asks for each of the save's copies again, and each comes back where the
save had it; a client's load raises none, since its copies belong to the host's world. When the
session ends or the world changes, a client's copies of the old world are removed as soon as
nothing holds the pool, and the host's stay where they are.

On the loopback, or beside a multiplayer older than the copies, the group stays locked for the
length of the session, since a copy nobody hands a key out for cannot be shown to anybody else.

## The eleven original toggle codes

They are not reimplemented. The engine keeps eleven `int32` of state and its own console flips a row
with `^ 1`; this reads and writes the same array the same way, so a row switched from the panel is
indistinguishable from one that was typed. Every effect stays the engine's own and nothing
downstream is patched.

Both tables are found through the one piece of code that touches them together, the console's
comparison loop, and read out of its operands. The name table has exactly **one** reference in the
whole code section. That site was chosen over the tidier looking ones nearby.

The console also prints a confirmation line from a parallel table of message ids. That is left out
on purpose: it exists to confirm a code somebody typed blind, and a panel that shows the state has
nothing to confirm.

**Each row says what the code does, then the code**, the way the one-shot group below already
does: `Disable cheats (turntables)`, `Letterbox view (beyond cinema)`, `Slow motion mode (slowmo)`,
`Auto-fire/attack (perfection)`, `Force push turns red (but i feel so good)`, `60 fps frame rate
(60fps)`, `Wire frame view (perf)`, `First person view (naughty naughty)`, `Overhead view (from
above)`, `Weapon 3 more powerful (happy)` and `Debug mode (oldcode)`. The image's table holds only
the codes, and a row that read "perfection" told nobody it was auto-fire. The descriptions are
matched by the code's text, not by its position, so a table read out of a build with a different
set still gets a row for every code and an unknown one is written on its own.

## The sixteen one-shot codes, in `cheats_original_actions.c`

Kill self, full health, all-weapons-full-ammo, the four play-as-character swaps, two ways to lower
the difficulty, one to raise it, a graphics detail level cycler, a red icon highlight toggle, and
the "Tech Bonus!" message: thirteen of the sixteen codes the retail console understands that are
not one of the eleven toggles above. Each is a row with a `RUN` chip instead of an `ON` / `OFF`
one, because none of them are a state; typing `kill me now` does not leave anything switched on
that a second look could find.

**Three of the sixteen are held back as `n/a` on purpose, not because any failed to resolve:**

* **Wavering graphics** (`drop a beat`) resolves cleanly, the flag and both apply calls all read as
  valid addresses, and runs without crashing, but confirmed against the running game, not
  assumed, it has no visible effect. What it flips is read inside two of the engine's own dense
  per-vertex model transform routines, deep enough that pinning down what it actually renders as
  would take real additional work. "Resolves and runs" is not the same claim as "does something a
  player asked for", so it is offered as unavailable and not as a row that ticks and, as far as
  this project can currently show, does nothing.
* **View credits** (`gurshick`) also resolves cleanly and writes without crashing, but field testing
  found triggering it from this panel, mid level, misbehaves badly enough to be worth not offering
  until it is diagnosed. Retail's own path to this code is the console, which pumps its
  own frame loop with the player never suspended, not this panel's path, so whatever the credits
  sequence expects to be true when it starts may simply not be, here.
* **Debug mode** (the game's own debug/fps toggle) resolves, flag and code text both, and runs.
  It draws its frame rate readout through the same text layer this panel draws through, and
  running it from here breaks the panel, field confirmed by switching it on and watching what
  happened to the overlay afterwards. The row stays visible and greyed, with the code in its
  label, so the cheat is still discoverable to anyone who wants to type it into the game's own
  console, where it works.

All three resolutions are left in `cheats_original_actions.c`, not deleted, one line from
being restored if any of them is ever fully understood.

**Most of these print the same on-screen confirmation retail's own console prints**, through the
same message function tech bonus needs to do anything at all (`FUN_0043dc61`): kill self, full
health, all-weapons-full-ammo, both lower-difficulty codes, increase difficulty, the graphics detail
cycler and the red highlight toggle all show one now. This was originally left out everywhere except
tech bonus, on the reasoning `cheats_original.c` gives for the eleven toggles, "a panel that shows
the state has nothing to confirm", and that reasoning does not hold for a fire-once effect with no
OTHER visible feedback: difficulty changes nothing on screen a player can look at, so without the
message a working press and a silently-failing one looked identical. View credits and the four
play-as codes print no message in retail either, so none is added here; that absence is retail's
own, not an omission. The graphics detail cycler's message id is not a fixed number: retail computes
it from the level just cycled TO (`DAT_004ac538 + 0x37`), read fresh after every press so the
message always names the level actually landed on.

**The graphics detail row is the one exception to "every action shows `RUN`".** Its chip shows the
level itself, `1` to `4`, read live off `DAT_004ac538` on every rebuild and not cached from the
press that set it, the same cell the retail message above reads, so the row and the message can
never disagree. It starts showing whatever level the game is already on, and each press updates it
to the level just cycled to, which is the only one of the sixteen where a live number is more useful
than a generic button: the confirmation message answers "did something happen", the chip answers
"what is it right now."

### The four play-as codes are queued, not run: the only actions that work this way

Field testing found these worked intermittently: same button, same character, sometimes nothing.
The swap has its own precondition inside the retail function itself, a pointer in the player's state
block that reads as "no active controller" whenever the player is suspended, which is the engine's
own idle state, and it is what `input_freeze.c` deliberately holds the player in for as long as this
panel is open, on every frame, so the game holds still. Pressing the row while the panel is open can
therefore land on exactly the state the swap silently declines to run in, with nothing shown for it
either way.

So a press does not call the swap. It records which character was asked for, the row shows `QUEUED`
in place of `RUN`, and the title bar, which says nothing otherwise, reads `Close applies the queued
swap`, and then `cheats_original_actions_apply_pending()` runs it once, from `overlay_input.c`,
right after the panel closes and the player has been un-suspended again. Only the last press before
closing takes effect; the four are mutually exclusive characters anyway, so replacing a pending one
instead of queuing several is the honest behaviour. Every other action in this file still runs the
instant its row is pressed; this is the one exception, and it exists because of a real, confirmed
collision between two of this project's own subsystems, not a general pattern the other fifteen
needed too.

Two of these, the graphics detail cycler and the red highlight toggle, were **field-corrected**
after their first ship: they were named from a fan-made cheat sheet before either callee was
actually read, on the guess that whichever mystery codes were left over must be whichever
screenshot rows were left over. That guess was wrong twice over in one go: the two screenshot rows
it leaned on turned out to already belong to the eleven toggles above, under different codes
entirely, and both labels have since been corrected to what their callees actually do, read
straight out of the decompiled functions, not guessed again. See the header comment in
`cheats_original_actions.c` for the full account; it is left in and not quietly fixed, because a
project whose whole discipline is "byte evidence over assumption" should say so when it assumed
anyway and got caught.

**One anchor, not sixteen signatures.** All sixteen live inside one retail function,
`gameplay_open_cheat_console` at `0x0042fc90`, decompiled in full. After the
eleven-entry loop above, it just chains `strcmpi` tests against the typed text, each followed by
whatever that code does. Rather than sixteen independent byte patterns, this resolves ONE signature
for that function's own prologue and reads every site as a fixed byte offset from it, sound for the
same reason the toggle table's own `OFFSET_NAME_TABLE` is: a recompile that relocates the whole
function moves everything inside it by the same amount, and every address is read out of the match,
never assumed. `cheats_original_actions.c` carries the full offset table and the byte evidence for
the anchor itself.

**Three of the sixteen are shown by a code text this project never had to know in advance.** Debug
mode, the forced power colour and the stronger third weapon all compare against strings under five
characters, too short for the image's own string analysis to have catalogued them the way
`cheats_original.c`'s `MAX_NAME_LENGTH` note already flags happening the other way round. Rather
than guess the words, the panel reads them live out of the image at the same offset the comparison
itself uses, the same "never a hardcoded address" rule as everything else here.

### The gate, what the counter really does, and why the gate matches retail's own `< 10` anyway

Full health and all-weapons-full-ammo both raise `DAT_00872efc`, capped by the console's own `< 10`
guard so the two effects stop giving anything past a point. The only other place in the whole image
that reads this cell is the function computing the player's EFFECTIVE difficulty for the run:

```
local_14 = DAT_00872fa0 - local_10;
if (local_14 < 0) local_14 = 0;
if (0 < DAT_00872efc) local_14 = 10;      /* ANY nonzero value, not >= 10 */
return local_14;
```

The FIRST use of either code sets this counter to 2 or 5, already `0 < DAT_00872efc`, already
pinning the hardest row from that point on, and there is no gate that can prevent that while the
effect still does anything: the pin is a cost of the code, not a defect in how it is offered. An
earlier version of this gate read that as reason to allow only the very first press. It was correct
about the bytes and the wrong gate anyway; it did not stop the pin, it just took away the handful
of further uses retail itself always allows after that same, already-unavoidable cost, which is
less than typing the same code into the console by hand gets you. So the gate matches retail's own
`< 10` exactly: `cheats_original_actions_is_available()` reads this cell fresh on every paint and
answers unavailable once it stops being under ten, the same point retail's own effect stops giving
anything, for either code, since they share the one counter. A row that greys out here greyed out
because that shared budget ran out, not because a resolve failed; the panel does not need to say
which.

## A group's rows as a table

Three groups do not describe their rows in a `switch` any more. The picture, the fog and
**Dismemberment** are tables of entries, one entry per row, each naming what it is and handing
in the two or three functions its kind needs; `overlay_kit.c` turns a table into rows, into a
press, into a committed number and into a drag. Adding a row there is one entry in the table
and nothing else, except the group's row count and, if it does not go at the end, the slot
numbers `session_lock.c` addresses.

A number entry draws TWO rows, the value and the track under it, the way these groups always
drew one, so an entry put in above another moves that one's slot by two. That is the piece
worth knowing: a slot that moved hands a player a row a session takes away, or takes away one
it should leave, and nothing in the build notices.
`unittests/overlay_session.c` notices, by name.

Each entry also carries its minimum, its maximum, its step, a coarse step and what a
`Default` puts back. They sit on the entry rather than in the drawing so that the sideways
keys and the Default are written once, in `overlay_number.c`, rather than once per group.
The two groups that are not tables, the cheats and the controls, state the same numbers in
the same shape, so every track in the panel is driven by one arithmetic.

The standard is a **function** and not a number, which is the one thing here worth reading
twice. A number could not say "this row has none": an entry that simply left it out would
read as zero, and zero is a value on some of these tracks, so a Default nobody wrote would
quietly put a row at the bottom of its band. It also lets the one row whose standard is not
a constant answer for itself. The field of view shows a base plus an offset, this panel
writes the offset, and the value with nothing written is the base, which comes out of the
settings file every time; `FOV_ROW_MIN_DEFAULT` sits right beside it and is the low END of
that row's track, named for the default `variable_fov`'s own slider uses for it. Wiring the
two together sets the narrowest picture the row allows and calls it the default.

## The picture rows

The picture group, under the heading **Engine**: the draw distance with its slider, the number in
force and its two gates, the field of view, and the subtitle size. They were the front half of
Utilities until the settings there outnumbered everything else, and every one of them answers the
same question a player arrives with, how the engine draws the world. Three DLLs own the keys,
`view_distance_fix`, `variable_fov` and `enhanced_resolution`, and none of them is called from
here; each re-reads its keys about once a second. The fog is a group of its own and is drawn under
the same heading, after these rows. The group keeps its inner name, the picture, because its slots
and its row ids are written against it.

## The draw distance row

The first row under **Engine** edits `[view_distance_fix] ViewRangeScale`, the draw
distance, typed in the same way as the jump-boost scale. Its label carries the accepted range,
`1.0 to 2.5`, so it is learned from the row and not from a refused number.

**It has a slider on the line directly beneath it**, on its own line so the handle never covers the
number it sets, which is the same shape the field of view and mouse speed rows use. The track spans
`VIEW_RANGE_MIN` to `VIEW_RANGE_MAX`, both compile-time constants here, not settings, so
unlike the field of view there is no way for a reader to set the two ends equal and nothing to guard
against dividing by zero. A drag rounds to a hundredth, because the row's own formatter shows two
decimals and a value with more than that would leave the number and the handle disagreeing about
what had been set. A fiftieth was tried first and is wrong: the grid has to contain both ends of
every row using it, and fog thickness starts at `0.25`, which a fiftieth rounds up to `0.26`, so
the documented minimum could not be reached. That was caught in a log, not by a test.

**It writes the ini and never calls `view_distance_fix`.** Feature DLLs here never depend on
each other at run time. Any one of them can be deleted from the `mods` folder without breaking the
rest. The ini is a channel both already have and neither owns, `view_distance_fix` re-reads the
key once a second and adopts it, and the setting survives a restart for free because it is written
where the setting already lived. The cost is a fraction of a second between committing the row and
the world changing.

**The row shows the setting, not necessarily what the game is drawing at.** The cell watchdog lowers
the scale on its own when a dense scene fills its buffers, so in a heavy area the row can
honestly read `2.50x` while the game is really drawing at `1.00x`. The note directly under the row
says so.

## The note under the draw distance

The row under that slider is not a control. It reads `in force: 1.00x` and cannot be
clicked into, because nothing here can change it: it reports the draw distance the game is actually
running, which is not always the one typed above it. The frame governor lowers that when a scene
costs too much frame time and the cell watchdog lowers it when the draw table or the vertex cache is
close to overflowing, and on a heavy level the watchdog can hold it at `1.00x` for the whole level.

**It exists because the row above it was reported as doing nothing.** On Coruscant the number could
be typed, committed and written to the ini, and the world would not change, because the watchdog had
already taken the scale and nothing on screen said so. The log said so, and nobody reads the log
while playing.

**It reads the ini and never calls `view_distance_fix`**, the same way round as every other row
here, except that the direction is reversed: that DLL publishes what it is running as
`[view_distance_fix] EffectiveViewRange` and this only ever reads it. Editing that key does nothing,
the next frame overwrites it, and a machine without `view_distance_fix` installed reads
`in force: not reported`, not a number this would otherwise have to invent.

On a client of a multiplayer session whose host set the draw distance, `view_distance_fix` does not
write that key, because the ini is the player's own and the host's value holds only for the
session. It files what it applies in a record instead (`host_taken_view_distance_fix`), the host's
value after this machine's frame governor and cell watchdog, and the note reads it from there for
as long as that record says the host's value is in force. The row above it reads `host 1.50x`, and
the note can read less than that on a machine that cannot hold it.

## The two switches under the draw distance

`Draw distance follows the frame rate` is `[view_distance_fix] FrameBackoff`. It is the frame
governor alone: the draw distance drops when a scene costs more frame time than the distance is
worth and comes back when the scene gets cheaper. That moves the fog with it, so the world visibly
opens and closes as the frame rate wanders, which some people want to stop.

`Keep the draw distance (costs frame rate)` is `[view_distance_fix] StrictViewRange`, and it is the
bigger hammer. It declines the governor, the level-opening window, the scripted-camera raise **and
the cell watchdog**, so the number typed two rows above is the number in force on every frame.

**The watchdog is the part to understand before leaving this on.** It is not automation for taste,
it is a memory-corruption guard. The cell table ends where the bucket list heads begin, its
limit is checked once at the entry to the gather and not again, and an overflow was traced to an
access violation in the draw function with a list head the renderer had read as a pointer. The
vertex cache fails more quietly and more permanently: a vertex skipped once never gets a slot again
until the level is reloaded, which is the stretched geometry people report after raising the
distance.

With this on both of those happen instead of being avoided. The watchdog keeps running on a copy of
the scale, so it keeps measuring, keeps its own ceiling current for the moment the switch goes back
off, and keeps writing its warnings to `engine_fixes.log`. What it cannot do is act.

At `1.00x` there is nothing here to be afraid of. The risk is entirely in holding a raised scale on
a level that cannot afford it, which is a reasonable thing to want while looking at something and a
poor thing to leave on while playing.

The row is named for the frame rate because that is the cost a reader will actually meet: the
governor is the only one of the four that acts in ordinary play at `1.00x`. The watchdog's cost is
real but conditional, and it is written here and in the ini, where a label has no room for it.

**The two switches are mutually exclusive, and the second one wins.** While strict is on the
frame-rate row is greyed: it reads off and it cannot be clicked. The game is genuinely in that
state, because strict declines the governor outright. A row still reading ON over a governor that
is not acting would be a lie in the one place a reader looks to find out what is happening.

What it deliberately does **not** do is write `FrameBackoff`. Someone who had the governor on, turns
strict on to look at something and turns it off again gets their governor back, instead of
discovering that a setting they never touched has been changed for them. So the row reports the
state the game is in and the file keeps the state the reader asked for.

## The field of view row

A row that reaches a setting owned by another DLL, so the panel can change it without leaving
the game to find the screen it normally lives on. The control scheme's switches and the mouse
speed were beside it until they became a group of their own, described further down.

`Field of view` is `[variable_fov] ExtraDegrees`, it is the only row here that shows one number and
writes another, and it is the only one with a **slider**. That key is an offset from a base that
depends on the canvas, the aspect mode and the engine's own projection, none of which exist in this
DLL, so an offset is a number this row could display and nobody could read. `variable_fov` therefore
publishes `BaseFov`, and the width of the picture is `BaseFov` plus `ExtraDegrees`.

**The base is published, not the current width, and the first version got that wrong.** The
width moves every time the offset does, which is every frame of a drag, so working the base out as
"width minus offset" pairs a width written a moment ago with an offset written just now. The base
drifts by the size of the last drag step, every step is then measured from a wrong origin, and the
value runs away past both ends of the slider. The base moves only when the resolution or the aspect
mode changes, so publishing that half has nothing to go stale.

**Each track is a row of its own, directly under the value it drives.** The first version put it in
the gap between the name and the chip, which made it a short target within a few pixels of both,
and at the panel's smaller sizes it read as though it were striking the name through. A line costs
one row and buys a track running nearly the width of the panel. The chip above still shows the
number and still opens for typing, so there are two ways to set either one.

**Two rates, on purpose.** Writing a key rewrites the whole settings file, so a drag applies at
thirty a second, not at the frame rate; unthrottled it would be several megabytes a second of
file traffic for one handle and the stutter would get blamed on the setting instead of the
dragging. The handle itself is drawn from the pointer at the full frame rate, because drawing it
from the file would move it in thirty steps against a hand moving in sixty. On the other side
`variable_fov` polls every frame but asks the file system for its last write time before parsing
anything, so it notices within a frame without reading ninety kilobytes sixty times a second.

**One write path, and one place the two numbers are written down** (`overlay_slider.c`). The
mouse's drag, the click that grabs a track and the pad's triggers were five writers in two files,
two of them throttled with a copy each of the same two intervals. They are one hold now: which
track, where the hand has it, what the file last got, and when. Two hands can reach a track, the
pointer and the triggers, and a hand commands only the hold it has, so the triggers looking at
whatever is under the pointer cannot end a mouse drag whose hand has wandered off its row.

**The number beside a track follows the hand, not the file.** While a track is held, the value on
the row above it is worked out from the same fraction the handle is drawn at, through the row's
own arithmetic run backwards (`overlay_kit_value_at`). It used to come out of the file, which is
written four times a second, so the number and the handle under it were up to a quarter of a
second apart while somebody dragged.

**The number stands at the end of the track as well.** The track now ends a little short of
the panel's edge and the number reads in the gap, so an eye that is on a handle does not
travel a row and the width of the panel to see what it is setting. It is the same reading as
the chip above it, from the same fraction through the same inversion, so the two cannot
disagree in either state: from the file with no hand on the track, from the hand's own
fraction while there is one (`overlay_slider_number_on`).

**And a `Default` after it**, drawn as a button, which puts the row back to the standard it
named. A track that named none has no button. Pressing the track row runs it, from the
keyboard with Return or with a click on the button, because a track is dragged rather than
pressed and there is nothing else a press on that row could mean. A click that lands on the
row but on neither target does nothing, so a missed handle cannot put a setting back.

On a panel too narrow to hold all three the Default goes first and the number second, and
the track never falls under about six capitals wide: a track that cannot be set is worse
than a button that is not drawn (`overlay_number_fit`).

**Left and Right change a number.** On a value row or on the track under it they move one
step, with Control held the row's larger step, clamped at both ends; the same keys still
fold a heading, still walk a row of words, and still change the tab on everything else. The
rule is one function that the keyboard and the pad's D-pad both ask (`overlay_keys_sideways`),
because the pad used to change the tab with Left and Right unconditionally and two halves of
one rule written in two files is the pair that comes apart. The pad has no modifier to hold,
so a D-pad press is always the fine step and the triggers remain its way to cross a track
quickly. A press reaches the file through the same hold every other hand writes through, and
unlike a drag it writes at once: there is no key release to write the last press of a burst
on, and every press is a value somebody asked for.

Pressing a sideways key while a number is being typed into ends the typing and steps the
value, rather than being swallowed: clicking a number is the most likely way of reaching
one, and the keys did nothing at all in exactly that state.

**One row has a number and no track, on purpose:** `Dev menu size`. Its shipped value is
`auto`, which is outside any band; its ends depend on the size of the picture; and a pull on
it scales the panel the track is drawn in, so the track would move under the hand pulling
it. It stays a number that is typed, and the sideways keys leave it to the tab.

Thirty a second is the field of view's rate alone. Every other slider writes four times a second
and once more on release, since a Steam Deck fell to seven frames a second dragging any of them:
under Wine the profile layer parses and rewrites the whole file on every write, and every other
DLL's next read parses it again. The field of view keeps the full rate on purpose, because its
whole effect is the picture zooming under the hand, and at four a second that zoom is a series of
steps, which is worse than the frame cost.

**It is also the only row here that can be unavailable.** Every other row edits a settings file and
works with the DLL that reads it deleted from `mods\`. This one needs a published width, so with
`variable_fov` absent it greys out; a number it invented would be wrong on some canvas. Its range comes from that DLL's own `SliderMinFovDegrees` and `SliderMaxFovDegrees`, so
widening the in-game slider widens this row with it.

**The field of view and the control switches all needed the owning DLL to start reading its own
settings back.** Both of those screens pushed outward only: they applied a change and then wrote
the file, and nothing ever read it. A row here would have done nothing until the next launch.
`variable_fov` and `enhanced_input` now re-read these keys once a second, the way
`view_distance_fix` already did, so a row takes effect within the second. **A key is only re-read
if it is named in that poll.** One added without it writes the file, nothing reads it back, and the
row looks dead until the next launch. `CameraFollow` and `AirControl` are both in it.

**Naming a key in that poll is not quite enough, and the second half cost a released build.**
The poll reads each key and compares it against the value it last saw, and it reads all of
them before it acts on any of them. So when one setting switches another off as a dependency,
as free look does to these two, the value it last saw for the other key is one that
no longer exists in the file or in the running game. Every later edit to that key then compares
equal to it and is decided not to be a change, and the row goes dead until the game is
restarted. It now re-reads what is live on any pass where it acted. A row whose key another
row can write needs that, or it works once and never again.

**And a group of its own decides whether any of this appears in the game's own menus.** Under
Under **Menus**, `Show extra menu options (restart the game)` writes
`[variable_fov] MenuSlider` and `[enhanced_input] MenuWidgets` together. Both ship off, so the
video options and controls screens look as they did in 1999, and all four settings live in this
panel instead. It reads on only when both keys are on, since a half state can only be reached by
editing the file by hand and reporting that as on would claim a screen is changed when it is half
changed. Under it a fold, `What this adds`, opens into seven lines that say what the switch puts
where, that it takes a restart, and that nothing here needs it, the same shape as the free
camera's "how to fly" row.

It says "on restart" on the row itself because it means it: both screens are patched by repointing
the engine's own widget table once while the game starts, and this project has no path that puts
such a table back. A switch that appears to do nothing is worse than one that explains itself.

**The mouse speed slider is gated with the rest, and that was the one real decision.** Mouse
look ships on, so that slider was its only adjustment inside the game. That is why the row above
exists. Leaving one widget behind on a screen meant to look untouched would have been the worse
answer: either the screen is the one the game shipped or it is not.

## The fog rows

No fog at its head, then two rows that are `[view_distance_fix]` keys the fog reads while the
game runs, so each takes effect within about a second and neither needs a restart.

`Fog thickness` carries a slider on the line beneath it, on the same terms as the draw distance
above: `FOG_BAND_MIN` to `FOG_BAND_MAX`, rounded to a hundredth on a drag.

`Fog thickness` is `FogBandScale`, and it is the only setting in the whole fog path that can bring
the band NEARER. Every other term decides where the fog has to be so that it is solid before the
geometry stops; this one says how much sooner than that a player wants it, which is taste. Both
ends of the band move together, so a level's authored proportions survive.

`Fog follows the draw distance` is `AuthoredFogBand`, inverted: the key asks whether the band is
left alone, the row asks whether it moves, because moving is the behaviour a player is looking for
after raising the draw distance and finding the world stops short of the fog. It decides what the
thickness above is a share OF, never whether it applies: the thickness is the last term in both
modes, so following the draw distance cannot push the fog back out to it.

`FogFollowFov` had a row here for one evening and lost it. The fog's end floor ships at the full
draw distance and that setting's correction is the same distance times the cosine of half the
picture, which is never larger, so the floor overrode it and the switch could not change anything.
It is still in the ini for somebody who lowers the floor and plays very wide.

Which half of the engine draws the fog is deliberately NOT here. `FogImplementation` is read once at
startup, because the two implementations differ in device state the engine only programs at a level
load, and switching while a level is up leaves nothing fogged at all.

## The key that opens this menu

The second row under **Menus** binds `[dev_overlay] OpenKey`. Click it, press a key, and
it is bound in the running panel and written to the ini, so the next start already has it.

**The default accepts three keys**: F6, and the key directly below Escape, which is the backtick on
a British or American layout and the caret on a German one. That last key has been the way in since
this panel existed and is kept, but by itself it cannot cover a layout where the key below Escape is
neither of those. F6 is in the same place on every keyboard and the retail game binds nothing to it.

**F5 would have been the obvious choice and is taken.** The retail default key table binds it to
`CONTROL_FN_09`, read by the player's own weapons and force handler. The game reads its keys as
DirectInput scancodes and never sees the window messages this panel hooks, so a shared key would do
both things at once.

**Eight keys are refused**, all of which would lock a player out: Escape and Return and the four
arrows, which drive the panel itself, and Alt and F4 together, so that Alt+F4 stays a way to quit
on a panel that does not read modifiers. Keys the game uses are allowed, and both things then
happen. The key that opens the multiplayer chat is refused as well, for the reason given under the
next heading.

## The key that opens the chat

The row under **Multiplayer**, directly below **Controls**, binds `[multiplayer] ChatKey`, the
key that opens the chat in a multiplayer session. Click it (or press Return on it), and while its
chip reads `...` the footer says `press a key`; then press the key. The key belongs to
`multiplayer.dll`, so this row writes the file and nothing else, as the window keys and the
subtitle size do: the multiplayer reads the key when a session is set up and again once a second,
so a running session takes a new key within a second, and the band under the rows says so after
every binding, as a confirmation in the ordinary text colour and not in the amber of a refusal.
The row works with the multiplayer absent and in a session alike; it is not one of the rows a
session takes away, because the chat's key is a matter for the machine it is pressed on.

The search finds it by `chat` or `key` as well. Its row id is 1344, the first of the multiplayer
group's own block; nothing but the key capture remembers a row by its id, and only for as long as
one capture lasts.

**It writes the key's NAME, `T` or `F9`, and never its number.** The multiplayer reads its keys by
name, the way it reads `ScoreboardKey`: one letter, one digit, or F1 to F12. The other key rows here
write a virtual key code, and a `ChatKey=84` would be no key at all to the multiplayer, which would
fall back to T while this row showed the key the player chose. For the same reason the row reads
the file with the multiplayer's grammar and not with the one `OpenKey` uses, which takes a bare `5`
for key code 5 rather than the 5 key. The default is T, in the code and in the shipped file.

**What it shows is the key the chat opens on.** A name the multiplayer cannot read, or one it
refuses, shows the key the multiplayer falls back to: T, or U while `ScoreboardKey` is T.

**Refused, with a sentence in the band, and the file left alone:**

* every key without such a name: Escape, Return, Backspace, Tab, Space, the arrows, Shift, Ctrl,
  Alt, the number pad, the key below Escape, F13 and above;
* the keys the multiplayer refuses too: M, which opens its menu; F4, half of Alt+F4 and the free
  camera's way out; F6, this panel's default key; F7, F8, F11 and F12, which the engine's graphics
  keys take before any hook sees them; and F10, which arrives as a system key;
* the key `[multiplayer] ScoreboardKey` names;
* every key this panel already uses: the key that opens it, the two placement keys of the entity
  spawner and the free camera's teleport key.

The row that binds the key opening this panel refuses the chat's key in turn, so the two can never
be one key: the chat asks the panel first, and the panel would take that key every time.

Keys the game itself reads are allowed, as they are for `OpenKey`, but the digits are a poor choice:
1 to 6 also switch weapons, so the chat would open on every change. A key bound by hand in the file
to one this panel uses cannot be refused here; the multiplayer then opens the chat on it anyway.
Nor are the placement keys and the free camera's key checked against the chat's when they are
bound after it.

## The subtitle size row

The last two rows of the picture group under **Engine**, and they edit
`[enhanced_resolution] SubtitleScale`:
how big the subtitles are, as a multiple of the size they have at 640x480. Typed in like the rows
above it, with the band in the label, and **dragged on the track beneath it**.

**A track is right here, unlike the panel's own size.** That one was tried and taken back out,
because dragging it moved the panel being dragged. This changes text somewhere else on the screen.
That is what a slider is for: bring up a line of dialogue and drag until it reads well.

**It goes through the ini, like the rows beside it.** The setting belongs to
`enhanced_resolution.dll`, feature DLLs here never depend on each other at run time, and either can
be deleted from `mods\` without breaking the other. That DLL re-reads the key once a second, so a
drag shows up on the next subtitle drawn.

**`0` is not shown and cannot be dragged to.** It is that DLL's spelling for "leave the engine's own
shrinking size alone", it sits outside the band this row offers, and there is no honest place to put
a handle for it. The row reports the default instead, and anyone who wants the engine's behaviour
back sets `0` in the file, where the comment explains it.

## The dev menu size row

The first row under **Menus** edits `[dev_overlay] DevMenuSize`, which is how much
bigger than its authored size this menu is drawn. Typed in like the other value rows, with the
accepted range, `0.33 to 4.0`, in the label.

**It is named for the menu and not for the panel**, because the menu is the thing a player
already has a name for, the one on its own title band, and the panel is an implementation
detail of it. It read `Cheatmenu size` while the title band still read `Cheatmenu`, and went on
reading it after the band did not. The key it writes keeps its older name, `DevMenuSize`,
because a renamed key would silently drop every size already set in a player's file.

**The menu is a fixed number of pixels, which is the problem this solves.** It reads the same on a
1080 display as on a 4K one, which is deliberate: what it cannot know is how big those pixels
physically are. The engine has the resolution and not the screen, so on a high density laptop
panel the size that is comfortable on a desktop monitor comes out too small to read. There is no
number this can default to that is right everywhere, so it is a row and not a constant.

**It takes effect on the next frame, and it moves itself while it is being used.** Committing a
value redraws the menu at the new size immediately, including the row that was just typed into.
This row is near the bottom because a control that moves while you are working it is easier to
find again at the end of a group than in the middle of one. Only the two key bindings sit below
it, and each is used once.

**The value is owned by `dev_menu_size_row.c`, not by the drawing.** The row is asked for the
current scale on every frame and answers from memory, reading the ini only on the first ask of a
session. Writing goes the other way, straight to the ini, so the setting survives a restart in the
same place the drawing would have looked for it anyway. Nothing here calls into another mod, so
this row works whatever else is or is not in the `mods` folder.

## The panel had a row limit it could reach

Every group on the open tab is built into one array each time the panel redraws, headings included.
With every group open, the "how to fly" fold open and a display offering a full size list, the
OpenPhantom tab wanted more rows than the array held, and the row that did not fit was dropped by a
bounds test that logged nothing. There is no scrolling, so a player had no way to tell a missing
row from a feature that was never written.

The array now holds 160, and `overlay_row_ids.h` asserts that against the parts the number is made
of instead of restating it, so a group that grows past the array stops the build; the third fold's
lines did that at 128, which is the assert doing its job. The parts that only
the display knows, the size list among them, cannot be asserted, so an overflow also writes one
warning naming the first row it dropped.

## A key row could not be rebound while the size list was open

Slots in the Window group shift down by the length of the size list while that list is open, and
`source_row` and the activation path both take the shift out before they look at a slot. The
binding path did not. With the list open, the release-key row arrived as slot 22, not 6, so
the check for "is this a key row" said no, the binding was refused, and nothing was written or
logged. Closing the list first and then pressing the row always worked, so this survived testing.

## Configuration: `[dev_overlay]`

| Key | Default | Meaning |
|---|---|---|
| `Enabled` | `1` | `0` installs nothing and the log says so. |
| `OpenKey` | `0` | The key that opens the panel. `0` accepts F6 and whichever key sits below Escape: the caret on a German layout, the backtick on a British one. Takes a name (`F8`, `numpad +`, `backtick`, `A`) or a virtual key code. Written by the panel's own key-binding row, and typed by hand when you cannot open it. |
| `TextAlign` | `1` | Which of the font layer's three modes starts a string where it is put. `0` centres it on its position; `1` and `2` are the other two. |
| `DevMenuSize` | `0` | How much bigger than its authored size to draw the menu, clamped to `0.33` and `4.0`. Written by the dev menu size row above, so it is normally set in game rather than here. |
| `SuperRunScale` | `2.0` | What the run cap is multiplied by while the Super run cheat is on, `1.1` to `4.0`. Written by the Super run speed row and its slider, so it is normally set from the panel. |
| `NoFog` | `0` | Whether the panel's "No fog" row starts on. Written by that row every time it is flipped, so it records the last choice and is not edited here. |
| `NewGameStartsAt` | `0` | The level a new game starts at, `1` to `11` in the game's order, `0` for its own first. Only a new game's first load is redirected. Written by the Level selection group's "New game starts at" list. |
| `PauseFreezesAnimation` | `0` | Whether a pause, the panel open or the free camera flying, also holds the animations on the engine's own draw latch. Written by the "Animations freeze while paused" row. |
| `PadEnabled` | `1` | Whether a controller drives the panel and the free camera, read through XInput once a frame while either is up. `0` leaves the pad to the game and to `controller_input` alone. |
| `PadOpenButtons` | `View` | The button that opens or closes the panel on its press, from `A B X Y LB RB LS RS View Menu Up Down Left Right`; two names make a chord. The game's own joystick reading sees the same press and runs whatever its controls screen has on that button, so clear it there. |
| `PadDeadzone` | `0.24` | The radial deadzone on both sticks, `0` to under `1`; the rest of the travel is rescaled so the first hair past it is a hair. |
| `PadPointerSpeed` | `0.6` | How fast the left stick moves the panel's pointer with the stick fully over, in screen widths a second, up to `5`. |
| `PadLookSpeed` | `120` | How fast the right stick turns the free camera with the stick fully over, in degrees a second, up to `720`. |
| `FreeCameraWorldRuns` | `0` | Whether the world keeps moving under the free camera while it flies, with the player's own input held. Written by the "World runs while flying" row. One or the other of this and the freeze. |
| `SpawnPlaceKey` | `0` | The key that turns the entity spawner's placement mode on and off, and opens the panel straight into it when the panel is shut. `0` binds none. Takes a name or a virtual key code, as `OpenKey` does; written by the "Key: place with the mouse" row. Escape, Return, the arrows, Alt, F4, Shift, Ctrl, the panel's own key, the free camera's key and the other placement key are refused. |
| `SpawnFaceKey` | `0` | The key that turns the entity being placed back to face the player. `0` binds none; the middle mouse button does the same. Written by the "Key: turn to face you" row. |
| `TwoSidedSwap` | `1` | Whether a borrowed body, a model put on from the Appearance group or one a far player wears, is drawn two sided: the engine's backface drop is lowered for the one call that draws that body and put straight back. `0` keeps the drop, and where the asset never marked a limb shell two sided, its open side shows nothing. Read once, when the first model is put on. |

## Limitations

* The pointer arrives in the window's client pixels and is mapped into the picture the engine draws.
  Those are the same size in every mode this project ships, so the mapping is usually the identity.
* A cheat or action whose site did not resolve is shown as `n/a`, not hidden, so a panel on
  an unsupported executable says what is missing instead of looking empty.
* The panel selects a font, sets an alignment, two scales and a colour, and puts none of them back.
  Nothing else draws text between the panel and the end of the frame, so nothing is affected today.
* The call the paint is redirected from cannot chain behind another DLL that redirected it first.
  Nothing else in this project targets it.
* The entity spawner's placement mode does not ask whether the ground is walkable, only whether a
  floor carries the place: a copy set on a ledge or a roof stands there and may not walk off it.
  Its wall rays go out a unit over the floor, so an obstacle lower than that does not push the
  place. Under a ceiling lower than about 1.15 the rays start inside it, all four strike at once and
  the answer is "too tight between walls" rather than "no headroom". The four rays follow the
  pointer's heading, so a diagonal wall or a pillar between two of them can overlap the body by up
  to about 29 % of its radius. A push over the edge of a narrow ledge looks for the floor again up
  to 3.5 below, and can stand the copy a level lower; the click's line says how far it was moved,
  not how far it dropped. The ghost is drawn in its rest pose. A key bound to the mode is refused
  when it is one the panel, the free camera or the mode itself reads, but the free camera's key
  bound later is not checked against the mode's, and neither is the key that frees the mouse.

## What was tested

The entity spawner's placement mode is covered by programs of its own: `spawn_place` (the mode's
state, the clicks, the turn, the settle, the right click's decision, the pointer's reach, where the
pointer puts the entity and the push off a wall), `world_pick` (the projection both ways, the
reading of the camera, the ray against a body and the nearest body), `spawn_ghost` (the render
handle's life), `input_owner` (who has the pointer, the wheel and the pause), `spawn_keys`,
`spawn_marks`, and `spawn_mode`, which runs the mode's own frame against a stand in for the engine:
the held world and its settle, the death, the refusal row, the right click and the order of two
clicks in one frame. `entity_offer`, `npc_census` and `spawn_scripts_archive` hold the offer and the
clipless gate on each of its three roads. The mode itself has run in the game once, in a co-op
session, where it placed entities, refused clicks, turned entities with the wheel and drew the
ghost; nothing else these programs cover has been checked against the game.

`overlay_model` covers the half that can be checked without the game: the search, the folding,
both groups on the Original tab, the tab switch, the bounds of the search box, the queued-swap
sentinel (nothing reads as pending before `resolve()` has run), the cheats, the free camera, the
three folds and every index that does not exist. `overlay_groups` walks the settings groups by
position, every row in the order it is drawn with its caption and whether it is offered. The two
share the sources, the stubs and the row arithmetic and were one program until the tenth group
put it over the size limit; the checks live in `unittests/overlay_model.c` and
`unittests/overlay_groups.c`.

The chat's key row has a program of its own, `chat_key_row`, linked beside the multiplayer's own
reader, `mp_board_key_code`: the two readers answer alike for every printable string of one to
three characters, every name the row can write reads back as its own key, every key the row takes
is written and read back out of the file, and every refused key leaves the file as it was, the
saved one standing as a confirmation and every refusal as a refusal. `overlay_groups` binds it once
more through the panel itself, under its **Multiplayer** heading, which is what holds the key
capture's arm for the group's own block of ids; `overlay_session` holds that the row stays free and
can be pressed in a session; `overlay_legend` holds the `press a key` line and `overlay_notice` the
two kinds of sentence. The row has not run against the game under its new heading, and neither
have the footer's prompt or the colour of the confirmation.

The host's values on a client are held by `overlay_session`, against the real records: the chip of
every taken row on a host, on a client whose host named all four settings, on one whose host named
the draw distance alone, and on a host with a client's records still filed; the note under the draw
distance out of `view_distance_fix`'s record; and no row left carrying a host's value once the
session ends. `overlay_kit` holds the hook that puts the words on a row, `overlay_width` measures
the widest of them beside their rows, and the golden master prints a pass as a client. None of it
has run against the game.

Every row in the panel has been opened, drawn and switched against the running game, and the
layout has been through several rounds of correction against screenshots. All of this
project's cheats are accepted in game, in the v0.4.1 build, which was played through by hand.

**Field-tested against the running game, several rounds:** kill self, full health,
all-weapons-full-ammo (including the shared gate greying both out together once spent, and now the
retail confirmation message on each), both difficulty codes (message confirmed), the graphics
detail cycler (message and live chip number both confirmed), and the "Tech Bonus!" message all
confirmed working. The four play-as codes are confirmed working through the queue. The red icon
highlight resolves and runs but which icon it affects is still unconfirmed. Wavering graphics,
view credits and debug mode are deliberately `n/a`, see above; the last two were added after field
testing found each misbehaved when triggered from this panel. **No fog has had two field
rounds, and each found a real bug.** The first version cleared the level's fog flag directly,
which broke the renderer (every moving actor drawn as a flat, unlit silhouette, not recoverable by
toggling the cheat back off); rewritten to push the fog band out instead of touching the flag. The
second version fixed that but declined to restore the band on the way back off, so fog could be
turned off but not back on short of a level reload; rewritten again to remember and restore the
authored band. Both directions are confirmed working now.

**Free camera has had several field rounds.** Pausing the simulation and driving the camera object
directly through a chained detour on `updateCam` both confirmed working; the WASD-along-view-
direction formula was field-tested wrong once (a sign error in the yaw-to-world-axis conversion,
found by comparing against the engine's own render-eye builder and its built-in debug free-cam,
not guessed a second time) and is now confirmed correct; the mouse axes were field-tested
inverted and corrected (yaw's flip stuck, pitch's did not, it was already right and got reverted
back). The line to look for:
```
[dev_overlay] free camera: pausing through sim_pause, camera object pointer at 008A011C, update chained at 00418544; WASD moves along the view, mouse looks, E/Q move vertically
```
Its absence, or a "did not resolve" warning next to it naming which of the two sites failed, means
free camera declined entirely and is shown in the panel as unavailable. An earlier attempt at this
feature, noclip, letting the player walk through walls and fly, was removed after free camera
replaced it outright; see `cheats_openphantom.h`'s own header comment for why.

**The bound key teleports, F4 does not.** The key set in the panel ends the flight and brings the
player to wherever the camera is. That is usually what the camera is being flown for. F4 ends the
flight and leaves the player where they were, and is fixed, not bindable, because a
fallback that always means the same thing is worth more than one more thing to configure. Both are
read while the panel holds the simulation, so the move is written into a still-frozen world and
the player's own physics resumes from the new place.

Three things this needed that were each found in the field, not predicted:

* The position that moves is `pPlayer+0x118` (`pos`), not `+0x124` (`desiredPos`). Phase 12 copies
  `desiredPos` into `pos` only when `bMovedThisFrame` is set, and a standing player clears that
  every substep, so a write to `+0x124` is discarded. The first attempt did that and
  looked like it did nothing. Traced through j0nny's decomp of `Plr_CommitPose`.
* The panel has to close itself on the way out. It holds the simulation just as the camera does, so
  without that the move does not resolve until the panel is closed by hand, which read, again, as
  the key doing nothing.
* The panel is locked open while the camera is flying: neither Escape nor the open key will close
  it, because closing it mid-flight leaves the camera stranded with no cursor to recover it. The
  only ways out are the bound key and F4. The fly-controls fold now says so. Since 2026-09-14 the
  two keys hide and show the panel instead of doing nothing; a person pressing them mid-flight
  wanted the panel out of the picture, not the flight ended.

Tested in game on Windows: teleport across a room, teleport onto a ledge, F4 from mid-air, and the
panel refusing to close while flying.

**A fall the engine cannot finish is handed back to it whole.** Jump boost suppresses five
consequences of falling so that a higher jump is not punished for coming down harder: the damage,
the airborne-too-long death, the fall-distance death and the two camera latches. None of those was
ever meant for a fall off the edge of the world, and applying them there did not make the player
immortal, it made the fall endless. Field report:

> if i fall like how i would fall off a ledge to my death normally and the died screen shows up IF
> we do that through super jump or fly cam then the audio goes really loud and the death screen
> takes a lot longer to show and when you load game from the death screen it doesnt actually load
> the game its still loud audio and your still off the ledge

All of it is one cause. `floor_probe.c` asks the engine's own `bapmap_probeFloor` whether there is
anywhere to land, once, at the moment a fall first becomes significant, and the answer gates every
one of the five suppressions, not only the death. A boosted jump has ground under it and
keeps its full immunity however long it takes to come down; a fall with nothing under it behaves
as it does with no cheat installed at all, because that is the behaviour known to end
properly. An unresolved probe answers "floor", so a build that cannot ask the question keeps the
immunity it had and does not quietly start killing people.

A time limit was tried first and was the wrong shape: it cannot tell a high jump from a void fall,
so any value that spares the jump also lets the void fall run long enough to break.

**The teleport writes both copies of the position.** The player carries `pos` at `+0x118` and
`desiredPos` at `+0x124`, and `Plr_CommitPose` (`0x0044C06B`) opens by copying the second over the
first on any frame the player is moving. Writing `pos` alone survived only while the player happened
to be standing still: the moment the movement phase ran it recomputed `desiredPos` from the new
position with its own ground resolution applied and committed that back, so the player arrived at
the right x and y planted on the floor. Writing `desiredPos` alone was tried even earlier and left
them where they started, which is the same fault from the other side. Both are written, and the
vertical velocity at `+0xB4` is zeroed so the fall starts from rest, without whatever the player
had when the flight began.

**The teleport will not drop the player further than the engine can cope with.** A drop onto a real
floor still breaks it if it is high enough, so the drop is capped at 80 world units and a teleport
past that ends the flight without moving anybody, as F4 does.

**Eighty is measured, and it was briefly raised to 350 with a crash to show for it.** The reasoning
for raising it was that the fall grace above suppresses the damage and both deaths for ten seconds,
and that ten seconds of falling at 40 units/s^2 clamped to 40 units/s covers 380 units. That
arithmetic is right and it answers the wrong question: the grace decides whether the player survives
the landing, not whether the engine can run the fall. A session logged teleports at 30.0, 51.5 and
72.3 which were fine, then one at 110.8 over ground at roughly 25 to 30, a drop of about 85 units,
which took the game down. **Do not raise it again without a session that survives the higher
number.**

Eighty also matches what the engine's own thresholds suggest: `FUN_0044F162` compares accumulated
fall distance at `player+0x360` against 3.5 (minimum distance for damage), 6.0 (the fall-death test)
and 8.0 (the distance at which a fall becomes significant, which arms the 2.0 second airborne
death). Eight units is already a serious fall here, so the cap is ten times that.

**The drop is measured against the player, not probed under the camera.** The first version asked
the engine's floor probe what was beneath the camera. That probe is scoped to the cell its point
sits in, so a camera flown above the level is in no cell and it reports "no floor" for solid ground.
A field session logged twelve refusals and every one was that false void, with the only teleports
getting through within a few units of standing height, which is to say the feature did not work at
all. The player is always inside the world, so the measurement is the camera's height above the
player plus the player's own height above their floor, and the probe is asked only at the player,
where its cell lookup succeeds.

Those three constants live at `0x004a875c`, `0x004a86dc` and `0x004a86f4`, read off the `FCOMP`
instructions that reference them. The addresses are the shipped executable's own: the neighbouring
constant at `0x004a86a4` is embedded as literal bytes in both jump-entry patterns in
`cheats_jump_boost.c`, and those patterns resolve on the `WMAIN.EXE` jump boost was field tested
against.

Tested in game on Windows: a boosted jump onto ground (immune as before), jump boost off a ledge
into the void (dies promptly, death screen loads correctly, audio normal), and, after the
measurement was corrected, drops from real height that land properly alongside a refusal past the
cap. The log lines to look for:
```
[dev_overlay] the floor probe resolved: falls and teleports can both be asked about
[dev_overlay] the free camera teleport key dropped the player at 38.2 127.1 101.0
[dev_overlay] the teleport was refused and the camera returned instead: it is 104 units of drop, past the 80 this engine can finish a fall from
```


## The Controls group

Five switches and the mouse speed, all of them `[enhanced_input]` keys, drawn under Enhanced
resolution. They were Utilities rows until a player asked for one switch that turned the pad's
scheme on, and a switch that sets four rows wants the four rows under it, under a heading that
says what they are together.

`Mouse speed` is `[enhanced_input] MouseDegreesPerCount`, named after the controls screen's own
caption, and it has a track too. Unlike the field of view it needs nothing published, because both
of its ends are fixed and are the same band the controls screen's own slider spanned. It is here
because that screen no longer offers it.

Last in the group, a fold, `What these do`, opens into a line or two on each row above it, in the
order they are drawn, the same shape as the free camera's "how to fly" row. It sits last so that
its lines are the tail of the group and a slot stays its id whether or not it is open, so the
mouse speed keeps a fixed id while it is being typed into.

`Enhanced controller mode` sits at the top and is the one a pad player wants: one click writes
`Strafe`, `FreeLook`, `CameraFollow` and `AirControl` on, and one click writes them off. Four rows
carrying the game's own names give no hint that a pad wants all of them, and that is the whole
reason the row exists. It has no key of its own: it reads ON when the four under it do, so
any of them can still be switched off alone, and the row then reads OFF, which is the truth. Each
key is written through its own row, so the rules below, which one switches which on, hold here
too.

`Free look` and `Strafe` are `[enhanced_input] FreeLook` and `Strafe`, the two check boxes on the
game's own controls screen. They carry that screen's own captions and not a description this
panel invented, so a reader who has seen it recognises these rows. **Either can be refused**, and
the row cannot tell in advance: strafe needs mouse look and the keyboard axis reader, because the
engine's `turnWheel` is the only turn channel and driving it sideways would clear the mouse with
it; free look needs a follow camera that `enhanced_input` recognises. When one is declined it says
why in the log and the row reads back off on its next rebuild, which is the honest outcome.

**Two rows below those are the pad's, and both are built on free look.** `Camera follows you`
writes `[enhanced_input] CameraFollow` and `Steer a jump in the air` writes `AirControl`. Each
**writes `FreeLook=1` with itself**, because both are built on free look turning the body to
face where it travels: the camera drifts at the body's heading, and the jump is steered by an
angle measured against the camera. Without free look neither has anything to work with. The
dependency is one way, so switching either off leaves free look alone, and switching free look
off takes both down with it.

**The row writes both keys and does not call the feature**, and not for tidiness. Free look
refuses while the player phases are stopped, the state the game is in while this panel is open,
so a row that asked it directly would be refused every time it was clicked. Written to the file
instead, the once-a-second re-read applies them in its own order once play resumes, with the
refusal handling it already has.

`Camera follows you` is **unavailable, not hidden, while `Strafe` is off**, because the walk
never leaves the heading then and there is nothing to follow.

`Steer a jump in the air` is available while **either** of them is on, and that difference is worth
the sentence. **The shipped game steers a jump on its own**: the Jump and Fall descriptors both
carry the ordinary steer phase, so the turn input has always turned the body in the air. What takes
it away is free look, which handles the substep outside Stand so that the mouse stays on the camera.
So this row is not an addition to the game, it is what gives back what our own scheme removed, and
it is offered wherever that scheme is on. With free look on and the sideways walk off it still
steers, with fewer directions, because a lone forward key is a turn toward the camera. With both
off it is greyed out, because the engine is already doing the job.

## The Window group

Ten rows, and the group is a good deal more stateful than most, so the rules it follows are
worth writing down.

**The five shape rows are one choice, not five switches.** Pressing the marked one does nothing. An
earlier build treated a second press as "turn this off" and dropped the player to the engine's own
shape, which looks like the window feature having stopped working. They were still DRAWN as five
switches for a long while after that, with a chip each, and read as five settings of which one was
on; they carry a mark now and no chip, and so do the sizes under them.

**Fullscreen is the exception, and is a switch.** The engine's own shape covers most of a screen, so
turning it on reads as going fullscreen and the next thing anyone does is press it again to come
back. It remembers the mode it replaced and gives it back. That memory is kept in this DLL rather
than written to the settings file: it is a memory of a gesture, not a setting, and a key in the file
that nothing else reads would only leave a reader wondering what it was for.

**The size list opens with `auto`, and that row is the whole reason it is a list of its own.** The
entries below it are the display's own modes, asked of Windows and not of the engine, so every
one of them is a real size. `auto` is not: it writes zero to both axes. The rest of the feature
already reads that as "take the size the game is rendering", and it is what a fresh install has.
Without a row for it, choosing any size was a one-way door out of the state the panel started in,
because nothing else anywhere writes those numbers back. The row is marked by the ABSENCE of a size,
which is the same test the row above it uses to decide it says `auto`, so the two cannot disagree.

**Choosing a shape also writes the device.** `WindowedPresent` had a row of its own and should not
have: it has exactly one correct value for each shape above it, and one broken one. A window without
it means an exclusive device owning the screen, so asking the engine for a smaller picture sets the
real display resolution smaller, which was measured leaving a 4K desktop at 800x600. Fullscreen
without it is not fullscreen. A row whose only two settings are the right answer and a trap is not a
choice, so it is written wherever the shape is written and there is no row for it.

**Everything greys until the device is windowed.** The gate is what the device actually IS, which is
not the same question as what the settings file says: that file is read once, when the engine builds
its device, so after a press the two disagree until the game is restarted. Reading it once, before
anything in the group can be pressed, makes the rows follow the device, not the file.
Without that, switching fullscreen off un-greyed rows the device still could not carry.

The two key bindings grey with everything else. Greying a binding row does not disable the key it
names: Alt and Enter is the way out of fullscreen without opening this panel at all, and having the
panel's own appearance take that away would be a trap, not a tidy-up. What is given up while
they are grey is the ability to rebind them.

**The size is a list, not two typed numbers.** It unfolds into the sizes the display reports, one
row each, and closes when one is chosen: the same shape the free camera's "how to fly" row uses, so
the group's row count grows only while it is open. It was two text boxes first, and a typed number
can be one no display offers. That matters more than it sounds, because the size chosen here is also
written as the resolution to render at, the engine opens whatever it finds at startup, and a size it
cannot open stops the game before it draws anything. Catching that afterwards and explaining it is
worse than not being able to say it.

The list comes from Windows, not from the engine. The engine has one and
`enhanced_resolution` owns it, but that is a different DLL and feature DLLs here do not depend on
each other. Windows answers the same question from the same driver, and the DLL that actually
writes the resolution checks its own list before it does, so a size offered here that the engine
somehow does not know is refused there with a line saying so, and never reaches the settings
file.

Sizes under 640x480 are dropped. That is not tidiness: below roughly that much client area the
engine's warp to client (320,240) lands outside the window, the pointer is clamped short of it, the
echo test never matches, and a constant delta accumulates for as long as the window stays small.
## The Frame rate group

Six rows: the switch, the fraction and the number under it, and three lines saying what the
three of them are for.
The note is three rows because the panel is about forty-five characters wide and a longer label is
cut off, not wrapped; the continuations are indented past the line they finish, the same
shape the free camera's how-to-fly lines use.

It names what goes wrong and not only that something does. Somebody reading that row has come
to it because the game looks bad while the numbers look fine, and the last line is the half that
tells them they are in the right place.

**Match the screen** writes `MatchDisplayRefresh`, and it is the row worth pressing. The frame limit
decides how fast frames are produced, and the display shows them at its own rate, so a limit that
does not divide into the refresh rate leaves the display repeating some frames and not others, on
a pattern that shifts. Everything in motion judders slightly while the frame counter reads
perfectly steady. The choppy platforms once blamed on this were the camera target pair collapsing
while riding, repaired in `framerate_fix`; the cap was never the cause.

**Fraction of the screen's rate** writes `RefreshDivisor`. `auto` lets framerate_fix step the cap
down to a half, a third or a quarter of the refresh when the machine cannot hold the rate above,
and back when it can; a digit pins one. The chip shows the fraction and the rate it makes on this
screen. A fraction the screen cannot go down to is not refused: `framerate_fix` steps it back up
until the rate clears 30 a second and applies that, so on a 60 Hz screen `1/4` runs at a half, and
the chip reads `1/4 as 1/2 = 30`. On a synchronised display, which the installer's wrapper
configuration provides from 2.0.0, only the refresh and its fractions are even.

Measured, because it cost the time to measure it: a limit of 100 on a 144 Hz screen leaves 44
refreshes a second showing a repeat, and a limit of 60 on a 90 Hz Steam Deck OLED leaves 30. Both
looked like a fault in the interpolation, and both were chased as one.

**Frame rate limit** is `TargetFps`, typed. Zero means no limit at all, which reads as `none`. A
number outside 0 to 1000 is refused, not clamped: somebody typing 1440 for a 144 Hz screen
has made a mistake, and quietly handing them 1000 hides it.

While the row above is on, this one reads `n/a` and greys. The setting still holds the number and
`framerate_fix` still keeps it as the fallback for a screen that will not report a rate, but
nothing is using it, and a number somebody can change and watch do nothing is worse than a number
they cannot reach. Greyed, not hidden, because a row that disappears takes the answer to
"where did I set that" with it.

All three rows write the settings file, the same as every other row that reaches out of this DLL.
`framerate_fix` owns the cap and re-reads all three keys about once a second, so a change here takes
hold within that second, in game, without a restart. Nothing here calls into that DLL, because
feature DLLs in this tree do not depend on each other at run time; the refresh rate on the switch
is asked of Windows directly.

