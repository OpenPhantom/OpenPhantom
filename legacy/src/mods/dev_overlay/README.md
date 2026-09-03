# dev_overlay

A panel over the running game, opened with **the key below Escape**. It holds the cheats today. The
name is what the thing is, not what is in it: the diagnostics and developer tools that come later
are groups inside this same panel, and a shipped DLL cannot be renamed without breaking every
`engine_fixes.ini` that mentions it.

## What it looks like

Two tabs under a heading that reads `Cheatmenu`.

* **Original** holds two groups: the eleven codes the shipped console can switch on and off, and
  the sixteen it can only run once - typed, in retail, one backspace and one line of text at a
  time. Here they are both just rows in the same tab.
* **OpenPhantom** holds two groups as well, split the same way and for the same reason the
  Original tab is: **Cheats** are the things that change the game, **Utilities** are the settings
  that configure this patch. It began as one group with a settings row appended, and the settings
  outgrew the cheats, so a reader had to scroll past invincibility to reach the draw distance.

Everything starts folded. The search box filters by name and opens a group that has matches, and
clearing it puts the fold back the way you left it. A switchable row shows its state as `ON` or
`OFF` in a chip; a row that only runs once shows `RUN` instead, in the same shape but never green -
green would say "this is on right now", which a fire-once row never is. Either kind shows `n/a`
with no chip when the engine site behind it never resolved, or - for the two rows this can pin the
difficulty, see below - when running it now would be unsafe rather than merely unresolved. The
pointer is the game's own cursor.

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
| glyph scale | `0x0046B293` | `+0x28`, which is what tells it from the position scale |
| position scale | `0x0046B2BA` | `+0x38` |
| text colour | `0x0046B179` | packed ARGB |
| text | `0x0046B3C0` | a string at a position |
| character metrics | `0x0046B2FC` | width and height, in real screen pixels |
| string width | `0x0046B37A` | |
| screen size | `0x00439476` | the one place that loads both halves back to back |
| the cursor | `0x0045FD01` | the pointer's texture and the sprite drawer, both read out of it |
| the scene closes | `0x0046C32D` | the call redirected to paint from |
| window messages | `0x0043F603` | where the game's own console is opened, on backspace |

The filled shape is not named in any reconstruction. What identifies it is its call graph: exactly
six call sites reach it and five lie inside the fade and letterbox module, which draws exactly five
filled shapes. One of those sites cleans 24 bytes after the call, which is where the six arguments
and the calling convention come from.

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
world did not: NPCs kept walking, movers kept moving and timers kept running behind the panel, which
is exactly what a player notices opening the overlay mid fight. So the simulation is now held as
well, on the engine's own flag. `sys_frame` gates its own substep loop on it and the retail pause
menu sets the same one, so nothing here is invented and nothing had to be hooked; `render_frameEnd`
runs below that gate, which is why the picture keeps being drawn.

**Sound and music keep playing behind the panel, deliberately.** The retail pause menu also
broadcasts task command 8 on the way in and 9 on the way out, which is what silences audio. That
pair is not borrowed, because the pause broadcast only marks a task paused when its handler returns
0 and iMUSE's returns 2, so the mark is never set and the matching resume never fires `ImResume`.
Copying it would risk leaving the music stopped with nothing to start it again, and audio that keeps
going is a smaller wrong than silence that does not come back.

Window messages are answered here rather than passed on, which covers the pause and the menu keys.
**Alt combinations are handed back untouched**, so Alt+F4 and Alt+Tab still work: a modal panel that
can trap somebody in a full screen game would be worse than anything it fixes.

The panel closes itself if it is asked to paint into a frame the player is not seeing, which is what
the front end and a movie look like from here. Otherwise a level ending could leave the game held
with nothing on screen.

**Typing reaches the search box only after a click has landed on it, not the moment the panel
opens.** The box used to take every character while the panel was up, which meant the key that
opened the panel was also typed into it: Windows queues a `WM_CHAR` right behind the `WM_KEYDOWN`
that opens the panel, and with nowhere else for it to go, the open key showed up as the first
character of every search. A click inside the field is what starts focus now, and any click outside
it, or opening the panel fresh, ends it; the field's border and caret are only drawn while focused,
so the box never looks ready to type into before it is.

## The three cheats this project adds

The first two are one detour on one short function each, and both work by **declining** rather than
by topping a value up.

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

**Not covered**, because it is a different mechanism: anything that *sets* health rather than
subtracting from it. A scripted death and the console's own `kill me now` both go elsewhere.

* **No fog**, in `cheats_no_fog.c`, is a different shape from the other two because there is
  nothing to decline: fog is not spent through any function, it is state the renderer reads
  straight out of the loaded level's own record every frame. This reuses byte evidence
  `view_distance_fix`'s `fog_regime.c` already proved in full - the same `[g_level]` world
  pointer, resolved and cross-checked the same way - rather than re-deriving it, though the two
  DLLs never touch each other's memory: this one resolves its own copy of the site independently,
  the same isolation every feature DLL here keeps. While the cheat is on, a per-frame hook
  (`common/frame_hook.h`, the same one `fog_regime.c` uses for its own easing) pushes the level's
  fog band (`world+0x218`/`0x21C`, both world-unit floats) out past anything the world walk's own
  draw-distance cull can still be showing, which is what makes it survive a level change rather
  than lasting only until the next one.

  **The first version cleared `world+0x210` bit 0 instead - the level's "has fog" flag - and field
  testing found that breaks the renderer**: every moving actor drew as a flat, unlit silhouette,
  and setting the bit back did not undo it. Retail never toggles that bit at runtime at all, it is
  set once at level load and held fixed for the level's life, so a runtime flip exercises a
  combination of engine state nothing in 1999 ever produced. `fog_regime.c` never touches that bit
  either - only the band - which is why this now does the same rather than the flag. See the
  header comment in `cheats_no_fog.c` for the full account, kept in rather than quietly fixed for
  the same reason the graphics detail / red highlight mislabelling is kept in `cheats_original_
  actions.c`'s own history.

  **Turning the cheat back off restores the band the level was actually authored with**, captured
  the first frame this file ever saw that level's record, before writing to it. An even earlier
  version declined to restore anything here, the same rule the other two cheats follow - and field
  testing found that the wrong call for fog specifically: ammunition and health decline a
  SUBTRACTION, so their own "off" is just the game's other systems carrying on from wherever they
  already were, but nothing else in the engine ever moves this band, so nothing else was ever going
  to hand it back. The remembered band survives the record being freed and reallocated on a level
  change the same way `fog_regime.c`'s own `is_the_same_level` does: by checking the record still
  holds exactly what this file itself last wrote, not just that the pointer looks the same.

## The eleven original toggle codes

They are not reimplemented. The engine keeps eleven `int32` of state and its own console flips a row
with `^ 1`; this reads and writes the same array the same way, so a row switched from the panel is
indistinguishable from one that was typed. Every effect stays the engine's own and nothing
downstream is patched.

Both tables are found through the one piece of code that touches them together, the console's
comparison loop, and read out of its operands. The name table has exactly **one** reference in the
whole code section, which is why that site was chosen over the tidier looking ones nearby.

The console also prints a confirmation line from a parallel table of message ids. That is left out
on purpose: it exists to confirm a code somebody typed blind, and a panel that shows the state has
nothing to confirm.

## The sixteen one-shot codes, in `cheats_original_actions.c`

Kill self, full health, all-weapons-full-ammo, the four play-as-character swaps, two ways to lower
the difficulty, one to raise it, a debug/fps toggle, a graphics detail level cycler, a red icon
highlight toggle, and the "Tech Bonus!" message: fourteen of the sixteen codes the retail console
understands that are not one of the eleven toggles above. Each is a row with a `RUN` chip rather
than an `ON` / `OFF` one, because none of them are a state - typing `kill me now` does not leave
anything switched on that a second look could find.

**Two of the sixteen are held back as `n/a` on purpose, not because either failed to resolve:**

* **Wavering graphics** (`drop a beat`) resolves cleanly - the flag and both apply calls all read as
  valid addresses - and runs without crashing, but confirmed against the running game rather than
  assumed, it has no visible effect. What it flips is read inside two of the engine's own dense
  per-vertex model transform routines, deep enough that pinning down what it actually renders as
  would take real additional work. "Resolves and runs" is not the same claim as "does something a
  player asked for", so it is offered as unavailable rather than as a row that ticks and, as far as
  this project can currently show, does nothing.
* **View credits** (`gurshick`) also resolves cleanly and writes without crashing, but field testing
  found triggering it from this panel, mid level, misbehaves badly enough to be worth not offering
  rather than diagnosing on the spot. Retail's own path to this code is the console, which pumps its
  own frame loop with the player never suspended - not this panel's path - so whatever the credits
  sequence expects to be true when it starts may simply not be, here.

Both resolutions are left in `cheats_original_actions.c` rather than deleted, one line from being
restored if either one is ever fully understood.

**Most of these print the same on-screen confirmation retail's own console prints**, through the
same message function tech bonus needs to do anything at all (`FUN_0043dc61`) - kill self, full
health, all-weapons-full-ammo, both lower-difficulty codes, increase difficulty, the graphics detail
cycler and the red highlight toggle all show one now. This was originally left out everywhere except
tech bonus, on the reasoning `cheats_original.c` gives for the eleven toggles - "a panel that shows
the state has nothing to confirm" - and that reasoning does not hold for a fire-once effect with no
OTHER visible feedback: difficulty changes nothing on screen a player can look at, so without the
message a working press and a silently-failing one looked identical. View credits and the four
play-as codes print no message in retail either, so none is added here - that absence is retail's
own, not an omission. The graphics detail cycler's message id is not a fixed number: retail computes
it from the level just cycled TO (`DAT_004ac538 + 0x37`), read fresh after every press so the
message always names the level actually landed on.

**The graphics detail row is the one exception to "every action shows `RUN`".** Its chip shows the
level itself, `1` to `4`, read live off `DAT_004ac538` on every rebuild rather than cached from the
press that set it - the same cell the retail message above reads, so the row and the message can
never disagree. It starts showing whatever level the game is already on, and each press updates it
to the level just cycled to, which is the only one of the sixteen where a live number is more useful
than a generic button: the confirmation message answers "did something happen", the chip answers
"what is it right now."

### The four play-as codes are queued, not run - the only actions that work this way

Field testing found these worked intermittently: same button, same character, sometimes nothing.
The swap has its own precondition inside the retail function itself, a pointer in the player's state
block that reads as "no active controller" whenever the player is suspended - which is the engine's
own idle state, and it is what `input_freeze.c` deliberately holds the player in for as long as this
panel is open, on every frame, so the game holds still. Pressing the row while the panel is open can
therefore land on exactly the state the swap silently declines to run in, with nothing shown for it
either way.

So a press does not call the swap. It records which character was asked for - the row shows `QUEUED`
in place of `RUN`, and the title bar swaps its usual `Esc closes` hint for `Close applies the queued
swap` - and `cheats_original_actions_apply_pending()` runs it once, from `overlay_input.c`, right
after the panel closes and the player has been un-suspended again. Only the last press before closing
takes effect; the four are mutually exclusive characters anyway, so replacing a pending one rather
than queuing several is the honest behaviour. Every other action in this file still runs the instant
its row is pressed - this is the one exception, and it exists because of a real, confirmed collision
between two of this project's own subsystems, not a general pattern the other fifteen needed too.

Two of these, the graphics detail cycler and the red highlight toggle, were **field-corrected**
after their first ship: they were named from a fan-made cheat sheet before either callee was
actually read, on the guess that whichever mystery codes were left over must be whichever
screenshot rows were left over. That guess was wrong twice over in one go - the two screenshot rows
it leaned on turned out to already belong to the eleven toggles above, under different codes
entirely - and both labels have since been corrected to what their callees actually do, read
straight out of the decompiled functions rather than guessed again. See the header comment in
`cheats_original_actions.c` for the full account; it is left in rather than quietly fixed, because a
project whose whole discipline is "byte evidence over assumption" should say so when it assumed
anyway and got caught.

**One anchor, not sixteen signatures.** All sixteen live inside one retail function,
`gameplay_open_cheat_console` at `0x0042fc90`, decompiled in full rather than guessed at. After the
eleven-entry loop above, it just chains `strcmpi` tests against the typed text, each followed by
whatever that code does. Rather than sixteen independent byte patterns, this resolves ONE signature
for that function's own prologue and reads every site as a fixed byte offset from it - sound for the
same reason the toggle table's own `OFFSET_NAME_TABLE` is: a recompile that relocates the whole
function moves everything inside it by the same amount, and every address is read out of the match,
never assumed. `cheats_original_actions.c` carries the full offset table and the byte evidence for
the anchor itself.

**Three of the sixteen are shown by a code text this project never had to know in advance.** Debug
mode, the forced power colour and the stronger third weapon all compare against strings under five
characters, too short for the image's own string analysis to have catalogued them the way
`cheats_original.c`'s `MAX_NAME_LENGTH` note already flags happening the other way round. Rather than
guess the words, the panel reads them live out of the image at the same offset the comparison itself
uses, the same "never a hardcoded address" rule as everything else here.

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

The FIRST use of either code sets this counter to 2 or 5 - already `0 < DAT_00872efc`, already
pinning the hardest row from that point on, and there is no gate that can prevent that while the
effect still does anything: the pin is a cost of the code, not a defect in how it is offered. An
earlier version of this gate read that as reason to allow only the very first press. It was correct
about the bytes and the wrong gate anyway - it did not stop the pin, it just took away the handful
of further uses retail itself always allows after that same, already-unavoidable cost, which is
less than typing the same code into the console by hand gets you. So the gate matches retail's own
`< 10` exactly: `cheats_original_actions_is_available()` reads this cell fresh on every paint and
answers unavailable once it stops being under ten - the same point retail's own effect stops giving
anything, for either code, since they share the one counter. A row that greys out here greyed out
because that shared budget ran out, not because a resolve failed; the panel does not need to say
which.

## The draw distance row

The first row under **Utilities** edits `[view_distance_fix] ViewRangeScale`, the draw
distance, typed
in the same way as the jump-boost scale. Its label carries the accepted range, `1.0 to 2.5`, so it
is learned from the row rather than by having a number refused.

**It writes the ini rather than calling `view_distance_fix`.** Feature DLLs here never depend on
each other at run time, which is what lets any one of them be deleted from the `mods` folder without breaking
the rest. The ini is a channel both already have and neither owns, `view_distance_fix` re-reads the
key once a second and adopts it, and the setting survives a restart for free because it is written
where the setting already lived. The cost is a fraction of a second between committing the row and
the world changing.

**The row shows the setting, not necessarily what the game is drawing at.** The cell watchdog lowers
the scale on its own when a dense scene fills its buffers, so in a heavy area the row can honestly
read `2.50x` while the game is really drawing at `1.00x`. Reporting the live value would mean asking
`view_distance_fix` for it, which is the dependency above. The watchdog announces itself in
`engine_fixes.log` when it brakes, which is where that answer lives instead.

## The fog rows

Two rows under **Utilities**, both `[view_distance_fix]` keys the fog reads while the game runs, so
each takes effect within about a second and neither needs a restart.

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

The last row under **Utilities** binds `[dev_overlay] OpenKey`. Click it, press a key, and it is
bound in the running panel and written to the ini, so the next start already has it.

**The default accepts three keys**: F6, and the key directly below Escape, which is the backtick on
a British or American layout and the caret on a German one. That last key has been the way in since
this panel existed and is kept, but by itself it cannot cover a layout where the key below Escape is
neither of those. F6 is in the same place on every keyboard and the retail game binds nothing to it.

**F5 would have been the obvious choice and is taken.** The retail default key table binds it to
`CONTROL_FN_09`, read by the player's own weapons and force handler. The game reads its keys as
DirectInput scancodes and never sees the window messages this panel hooks, so a shared key would do
both things at once rather than one of them.

**Seven keys are refused**, all of which would lock a player out: Escape and Return and the four
arrows, which drive the panel itself, and F4, so that Alt+F4 stays a way to quit. Keys the game uses
are allowed, and both things then happen.

## The dev menu size row

The second to last row under **Utilities** edits `[dev_overlay] DevMenuSize`, which is how much bigger
than its authored size this menu is drawn. Typed in like the two rows above it, with the accepted
range, `0.33 to 4.0`, in the label.

**It is named for the dev menu rather than for the panel**, because the menu is the thing a player
already has a name for and the panel is an implementation detail of it.

**The menu is a fixed number of pixels, which is the problem this solves.** It reads the same on a
1080 display as on a 4K one, which is deliberate: what it cannot know is how big those pixels
physically are. The engine has the resolution and not the screen, so on a high density laptop
panel the size that is comfortable on a desktop monitor comes out too small to read. There is no
number this can default to that is right everywhere, which is why it is a row and not a constant.

**It takes effect on the next frame, and it moves itself while it is being used.** Committing a
value redraws the menu at the new size immediately, including the row that was just typed into,
which is why this row is near the bottom: a control that moves while you are working it is easier
to find again at the end of a group than in the middle of one. Only the key binding sits below it,
and that one is used once.

**The value is owned by `dev_menu_size_row.c`, not by the drawing.** The row is asked for the
current scale on every frame and answers from memory, reading the ini only on the first ask of a
session. Writing goes the other way, straight to the ini, so the setting survives a restart in the
same place the drawing would have looked for it anyway. Nothing here calls into another mod, so
this row works whatever else is or is not in the `mods` folder.

## Configuration: `[dev_overlay]`

| Key | Default | Meaning |
|---|---|---|
| `Enabled` | `1` | |
| `OpenKey` | `0` | The key that opens the panel. `0` accepts F6 and whichever key sits below Escape: the caret on a German layout, the backtick on a British one. Takes a name (`F8`, `numpad +`, `backtick`, `A`) or a virtual key code. Written by the panel's own key-binding row, and typed by hand when you cannot open it. |
| `TextAlign` | `1` | Which of the font layer's three modes starts a string where it is put. `0` centres it on its position; `1` and `2` are the other two. |
| `DevMenuSize` | `0` | How much bigger than its authored size to draw the menu, clamped to `0.33` and `4.0`. Written by the dev menu size row above, so it is normally set in game rather than here. |
| `NoFog` | `0` | Whether the panel's "No fog" row starts on. Written by that row every time it is flipped, so it records the last choice rather than being edited here. |

## Limitations

* The pointer arrives in the window's client pixels and is mapped into the picture the engine draws.
  Those are the same size in every mode this project ships, so the mapping is usually the identity.
* A cheat or action whose site did not resolve is shown as `n/a` rather than hidden, so a panel on an
  unsupported executable says what is missing instead of looking empty.
* The panel selects a font, sets an alignment, two scales and a colour, and puts none of them back.
  Nothing else draws text between the panel and the end of the frame, so nothing is affected today.
* The call the paint is redirected from cannot chain behind another DLL that redirected it first.
  Nothing else in this project targets it.

## What was tested

`overlay_model` covers the half that can be checked without the game: the search, the folding, both
groups on the Original tab, the tab switch, the bounds of the search box, the queued-swap sentinel
(nothing reads as pending before `resolve()` has run) and every index that does not exist. 42 checks.

The eleven toggles and unlimited ammunition / unlimited health have been opened, drawn, and switched
against the running game; the layout has been through several rounds of correction against
screenshots.

**Field-tested against the running game, several rounds:** kill self, full health,
all-weapons-full-ammo (including the shared gate greying both out together once spent, and now the
retail confirmation message on each), both difficulty codes (message confirmed), the debug/fps
toggle, the graphics detail cycler (message and live chip number both confirmed), and the "Tech
Bonus!" message all confirmed working. The four play-as codes are confirmed working through the
queue. The red icon highlight resolves and runs but which icon it affects is still unconfirmed.
Wavering graphics and view credits are both deliberately `n/a` - see above, the latter added after
field testing found it misbehaved when triggered from this panel. **No fog has had two field
rounds, and each found a real bug.** The first version cleared the level's fog flag directly,
which broke the renderer (every moving actor drawn as a flat, unlit silhouette, not recoverable by
toggling the cheat back off) - rewritten to push the fog band out instead of touching the flag. The
second version fixed that but declined to restore the band on the way back off, so fog could be
turned off but not back on short of a level reload - rewritten again to remember and restore the
authored band. Confirmed off now works cleanly; back-on has not yet had its own field round.

**Free camera has had several field rounds.** Pausing the simulation and driving the camera object
directly through a chained detour on `updateCam` both confirmed working; the WASD-along-view-
direction formula was field-tested wrong once (a sign error in the yaw-to-world-axis conversion,
found by comparing against the engine's own render-eye builder and its built-in debug free-cam
rather than guessed a second time) and is now confirmed correct; the mouse axes were field-tested
inverted and corrected (yaw's flip stuck, pitch's did not - it was already right and got reverted
back). The line to look for:
```
[dev_overlay] free camera: pause flag at 006CCFD8, camera object pointer at 008A011C, update chained at 00418544 - WASD moves along the view, mouse looks, E/Q move vertically
```
Its absence, or a "did not resolve" warning next to it naming which of the two sites failed, means
free camera declined entirely and is shown in the panel as unavailable. An earlier attempt at this
feature, noclip - letting the player walk through walls and fly - was removed after free camera
replaced it outright; see `cheats_openphantom.h`'s own header comment for why.

**The bound key teleports, F4 does not.** The key set in the panel ends the flight and brings the
player to wherever the camera is, which is what the camera is usually being flown for. F4 ends the
flight and leaves the player exactly where they were, and is fixed rather than bindable because a
fallback that always means the same thing is worth more than one more thing to configure. Both are
read while the panel holds the simulation, so the move is written into a still-frozen world and
the player's own physics resumes from the new place.

Three things this needed that were each found in the field, not predicted:

* The position that moves is `pPlayer+0x118` (`pos`), not `+0x124` (`desiredPos`). Phase 12 copies
  `desiredPos` into `pos` only when `bMovedThisFrame` is set, and a standing player clears that
  every substep, so a write to `+0x124` is simply discarded. The first attempt did exactly that and
  looked like it did nothing. Traced through j0nny's decomp of `Plr_CommitPose`.
* The panel has to close itself on the way out. It holds the simulation just as the camera does, so
  without that the move does not resolve until the panel is closed by hand - which read, again, as
  the key doing nothing.
* The panel is locked open while the camera is flying: neither Escape nor the open key will close
  it, because closing it mid-flight leaves the camera stranded with no cursor to recover it. The
  only ways out are the bound key and F4, which is what the fly-controls fold now says.

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
one of the five suppressions rather than only the death. A boosted jump has ground under it and
keeps its full immunity however long it takes to come down; a fall with nothing under it behaves
exactly as it does with no cheat installed at all, because that is the behaviour known to end
properly. An unresolved probe answers "floor", so a build that cannot ask the question keeps the
immunity it had rather than quietly starting to kill people.

A time limit was tried first and was the wrong shape: it cannot tell a high jump from a void fall,
so any value that spares the jump also lets the void fall run long enough to break.

**The teleport will not drop the player further than the engine can cope with.** A drop onto a real
floor still broke it if it was high enough, so the drop is capped at 80 world units and a teleport
past that ends the flight without moving anybody, exactly as F4 does. The number is read off the
engine's own thresholds rather than picked: `FUN_0044F162` compares accumulated fall distance at
`player+0x360` against 3.5 (minimum distance for damage), 6.0 (the fall-death test) and 8.0 (the
distance at which a fall becomes significant, which arms the 2.0 second airborne death). Eight
units is already a serious fall to this engine, so the cap is ten times that: generous for dropping
somebody in from height, and still short of the falls that broke it.

Those three constants live at `0x004a875c`, `0x004a86dc` and `0x004a86f4` in the shipped
`WMAIN.EXE`. Note that the data addresses quoted throughout these comments come from j0nny's
`obiold.exe` and do not map to the same places in the shipped executable - in `WMAIN.EXE` that
range is inside `.rsrc`. They were read by finding the `FCOMP` instructions that reference them.

Tested in game on Windows: a boosted jump onto ground (immune as before), jump boost off a ledge
into the void (dies promptly, death screen loads correctly, audio normal), a free camera teleport
into the void (refused, camera returns), and a teleport from very high onto a real floor (refused
the same way). The log lines to look for:
```
[dev_overlay] the floor probe resolved: falls and teleports can both be asked about
[dev_overlay] the teleport was refused and the camera returned instead: the floor under it is 214 units down, past the 80 the engine can finish a fall from
```
