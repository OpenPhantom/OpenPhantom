# dev_overlay

A panel over the running game, opened with **the key below Escape**. It holds the cheats today. The
name is what the thing is, not what is in it: the diagnostics and developer tools that come later
are groups inside this same panel, and a shipped DLL cannot be renamed without breaking every
`engine_fixes.ini` that mentions it.

## What it looks like

Two tabs under a heading that reads `Cheatmenu`.

* **Original** holds two groups: the eleven codes the shipped console can switch on and off, and
  the sixteen it can only run once, typed in retail one backspace and one line of text at a
  time. Here they are both just rows in the same tab.
* **OpenPhantom** holds two groups as well, split the same way and for the same reason the
  Original tab is: **Cheats** are the things that change the game, **Utilities** are the settings
  that configure this patch. It began as one group with a settings row appended, and the settings
  outgrew the cheats, so a reader had to scroll past invincibility to reach the draw distance.

Everything starts folded. The search box filters by name and opens a group that has matches, and
clearing it puts the fold back the way you left it. A switchable row shows its state as `ON` or
`OFF` in a chip; a row that only runs once shows `RUN` instead, in the same shape but never green;
green would say "this is on right now", which a fire-once row never is. Either kind shows `n/a`
with no chip when the engine site behind it never resolved, or, for the two rows this can pin the
difficulty, see below, when running it now would be unsafe rather than merely unresolved. The
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

## The nine cheats this project adds

In the order the panel lists them: **Unlimited ammunition**, **Unlimited health**, **No fog**,
**Invincible NPCs**, **One-shot NPCs (your damage)**, **Giant player**, **Tiny player**,
**Jump boost** and **Free camera**. They need fewer engine sites than that, because several
pairs are two answers to one question and share a single detour.

A cheat whose site did not resolve is shown greyed rather than hidden, and cannot be switched.
That is deliberate: a row that ticks and does nothing is worse than a row that says plainly it
is not available on this executable.

### Unlimited ammunition, unlimited health and no fog

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
  `view_distance_fix`'s `fog_regime.c` already proved in full, the same `[g_level]` world
  pointer, resolved and cross-checked the same way, rather than re-deriving it, though the two
  DLLs never touch each other's memory: this one resolves its own copy of the site independently,
  the same isolation every feature DLL here keeps. While the cheat is on, a per-frame hook
  (`common/frame_hook.h`, the same one `fog_regime.c` uses for its own easing) pushes the level's
  fog band (`world+0x218`/`0x21C`, both world-unit floats) out past anything the world walk's own
  draw-distance cull can still be showing, which is what makes it survive a level change rather
  than lasting only until the next one.

  **The first version cleared `world+0x210` bit 0 instead, the level's "has fog" flag, and field
  testing found that breaks the renderer**: every moving actor drew as a flat, unlit silhouette,
  and setting the bit back did not undo it. Retail never toggles that bit at runtime at all, it is
  set once at level load and held fixed for the level's life, so a runtime flip exercises a
  combination of engine state nothing in 1999 ever produced. `fog_regime.c` never touches that bit
  either, only the band, which is why this now does the same rather than the flag. See the
  header comment in `cheats_no_fog.c` for the full account, kept in rather than quietly fixed for
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
  holds exactly what this file itself last wrote, not just that the pointer looks the same.

### Invincible NPCs and one-shot NPCs

Two opposite answers to the same fifteen bytes, so they share one detour rather than taking a
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
lets the hook decide whether the block runs **at all**, rather than having to preserve how it
executed. The `+0x38` health field is the one `dismemberment.c` already established from
retail's own death gate at `0x0043707D`.

* **Invincible NPCs** skips the block, so health is untouched.
* **One-shot NPCs (your damage)** writes health straight to zero, which is what the death gate
  this function feeds actually tests for. It fires **only for damage that came from the
  player**, so NPCs fighting each other are unaffected.

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
transform" utility. This calls that utility rather than reimplementing it, and finds its address
out of the call rather than by an independent signature of its own. Giant player uses retail's
own 3.0. Tiny player uses 0.35, which has no retail precedent in that direction and is simply a
first guess at small but still visible and playable.

The player's thing is chased off the player-record global on every call rather than cached. The
incoming matrix is a full rebuild of the player's position and orientation for this frame, every
frame, so scaling it is inherently transient and switching either cheat off needs no un-write:
the next call simply stops scaling.

**Field-tested, and the caveat is kept rather than quietly fixed.** `matrix` is the caller's own
working buffer, not something owned by this call, and `bapobj_drawAll` reads it again right
after the call returns for something that has nothing to do with rendering. So scaling it in
place also scales the force-push ability's reach and power. A local-copy version that left the
caller's numbers alone was written and worked, and was reverted: combat is not meaningfully
usable at either scale anyway, so the extra copy bought correctness nothing was asking for.

### Jump boost

Two hooks, on the Jump mode entry and the Jedi Jump mode entry, because different characters
route through different ones. Either alone still helps whichever characters use it, so unlike
free camera below a partial resolve here is kept: it is a real cheat for part of the cast rather
than half a feature that does nothing.

Each hook calls the original **first and unconditionally**, which is what makes this a boost and
not a reimplementation. The jump happens exactly as retail built it, guard check and all, and
only once it has decided to jump and written its own vertical velocity does the cheat scale what
is now sitting at `+0xB4`, whichever path the original took, the fallback constant or the
per-character table value. The multiplier is a number you can type on the cheat's own row.

**A higher jump is a longer fall.** While the cheat is on it also suppresses three things
retail's own ground-contact code does to a long fall, none of which is a cheat of its own and
none of which has a row:

* the fixed ten-point landing damage every hard landing already risks, taken through this
  module's own damage hook rather than around it, so Unlimited health still wins if both are on;
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

Two engine sites and a hold on the simulation. It holds the world still through `sim_pause`
rather than by any means of its own, and writes the camera pose **after** the engine has composed
it rather than fighting the original for the fields.

Both sites, the pause flag and the camera object pointer must all resolve. A partial resolve is
not offered as half a feature here: a camera that could roam but never stopped the world moving
underneath it is not this thing, and neither is a pause with nothing to look through.

**Flying it.** `W`/`S` forward and back, `A`/`D` strafe, `E`/`Q` up and down, the mouse to look,
the wheel to change speed. Speed moves by a constant ratio per notch rather than a constant
amount, which is Blender's fly-mode feel: even control at both ends, where a fixed addition would
be enormous down low and glacial up high.

**Leaving it.** The bound key ends the flight and **brings the player to the camera**, which is
usually what the camera was being flown for, so it is the action worth putting on a key of the
player's own choosing. `F4` is fixed and means the opposite: put it back, leave without moving.
A fallback that always means the same thing is worth more than one more thing to configure.
Alt+F4 still closes the game, because Alt is not read here. With no key bound the cheat refuses
to switch on at all.

**One guard worth knowing about.** Mouse look measures the pointer's movement between frames
against an anchor it re-centres each frame. Anything else that also moves the pointer, a cursor
cage or another overlay, turns that difference into a constant that is not hand movement and that
arrives again on the next frame, and the frame after. A steady vertical bias drives pitch onto
its clamp and pins it there: the camera stares at the floor, `W` flies into it, and no hand
movement lifts it. Two guards close that off. A jump larger than a quarter of the screen in one
frame is not a hand, so it contributes no rotation and simply re-syncs the anchor; and the anchor
follows where the pointer actually **landed** rather than where it was sent, because a cage can
refuse the position asked for and a stale anchor then measures the same refusal for ever.
Reported from a tester's machine and confirmed fixed there. It was never reproduced here, which
is the point: the other writer it collides with is not something every machine has.

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
than an `ON` / `OFF` one, because none of them are a state; typing `kill me now` does not leave
anything switched on that a second look could find.

**Two of the sixteen are held back as `n/a` on purpose, not because either failed to resolve:**

* **Wavering graphics** (`drop a beat`) resolves cleanly, the flag and both apply calls all read as
  valid addresses, and runs without crashing, but confirmed against the running game rather than
  assumed, it has no visible effect. What it flips is read inside two of the engine's own dense
  per-vertex model transform routines, deep enough that pinning down what it actually renders as
  would take real additional work. "Resolves and runs" is not the same claim as "does something a
  player asked for", so it is offered as unavailable rather than as a row that ticks and, as far as
  this project can currently show, does nothing.
* **View credits** (`gurshick`) also resolves cleanly and writes without crashing, but field testing
  found triggering it from this panel, mid level, misbehaves badly enough to be worth not offering
  rather than diagnosing on the spot. Retail's own path to this code is the console, which pumps its
  own frame loop with the player never suspended, not this panel's path, so whatever the credits
  sequence expects to be true when it starts may simply not be, here.

Both resolutions are left in `cheats_original_actions.c` rather than deleted, one line from being
restored if either one is ever fully understood.

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
level itself, `1` to `4`, read live off `DAT_004ac538` on every rebuild rather than cached from the
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
in place of `RUN`, and the title bar swaps its usual `Esc closes` hint for `Close applies the queued
swap`, and then `cheats_original_actions_apply_pending()` runs it once, from `overlay_input.c`, right
after the panel closes and the player has been un-suspended again. Only the last press before closing
takes effect; the four are mutually exclusive characters anyway, so replacing a pending one rather
than queuing several is the honest behaviour. Every other action in this file still runs the instant
its row is pressed; this is the one exception, and it exists because of a real, confirmed collision
between two of this project's own subsystems, not a general pattern the other fifteen needed too.

Two of these, the graphics detail cycler and the red highlight toggle, were **field-corrected**
after their first ship: they were named from a fan-made cheat sheet before either callee was
actually read, on the guess that whichever mystery codes were left over must be whichever
screenshot rows were left over. That guess was wrong twice over in one go: the two screenshot rows
it leaned on turned out to already belong to the eleven toggles above, under different codes
entirely, and both labels have since been corrected to what their callees actually do, read
straight out of the decompiled functions rather than guessed again. See the header comment in
`cheats_original_actions.c` for the full account; it is left in rather than quietly fixed, because a
project whose whole discipline is "byte evidence over assumption" should say so when it assumed
anyway and got caught.

**One anchor, not sixteen signatures.** All sixteen live inside one retail function,
`gameplay_open_cheat_console` at `0x0042fc90`, decompiled in full rather than guessed at. After the
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

## The draw distance row

The first row under **Utilities** edits `[view_distance_fix] ViewRangeScale`, the draw
distance, typed
in the same way as the jump-boost scale. Its label carries the accepted range, `1.0 to 2.5`, so it
is learned from the row rather than by having a number refused.

**It has a slider on the line directly beneath it**, on its own line so the handle never covers the
number it sets, which is the same shape the field of view and mouse speed rows use. The track spans
`VIEW_RANGE_MIN` to `VIEW_RANGE_MAX`, both compile-time constants here rather than settings, so
unlike the field of view there is no way for a reader to set the two ends equal and nothing to guard
against dividing by zero. A drag rounds to a fiftieth, because the row's own formatter shows two
decimals and a value with more than that would leave the number and the handle disagreeing about
what had been set. A fiftieth was tried first and is wrong: the grid has to contain both ends of
every row using it, and fog thickness starts at `0.25`, which a fiftieth rounds up to `0.26`, so
the documented minimum could not be reached. That was caught in a log rather than by a test.

**It writes the ini rather than calling `view_distance_fix`.** Feature DLLs here never depend on
each other at run time, which is what lets any one of them be deleted from the `mods` folder without breaking
the rest. The ini is a channel both already have and neither owns, `view_distance_fix` re-reads the
key once a second and adopts it, and the setting survives a restart for free because it is written
where the setting already lived. The cost is a fraction of a second between committing the row and
the world changing.

**The row shows the setting, not necessarily what the game is drawing at.** The cell watchdog lowers
the scale on its own when a dense scene fills its buffers, so in a heavy area the row can honestly
read `2.50x` while the game is really drawing at `1.00x`. The note directly under it is what says so.

## The note under the draw distance

The second row under **Utilities** is not a control. It reads `in force: 1.00x` and cannot be
clicked into, because nothing here can change it: it reports the draw distance the game is actually
running, which is not always the one typed above it. The frame governor lowers that when a scene
costs too much frame time and the cell watchdog lowers it when the draw table or the vertex cache is
close to overflowing, and on a heavy level the watchdog can hold it at `1.00x` for the whole level.

**It exists because the row above it was reported as doing nothing.** On Coruscant the number could
be typed, committed and written to the ini, and the world would not change, because the watchdog had
already taken the scale and nothing on screen said so. The log said so, and nobody reads the log
while playing.

**It reads the ini rather than calling `view_distance_fix`**, the same way round as every other row
here, except that the direction is reversed: that DLL publishes what it is running as
`[view_distance_fix] EffectiveViewRange` and this only ever reads it. Editing that key does nothing,
the next frame overwrites it, and a machine without `view_distance_fix` installed reads
`in force: not reported` rather than a number this would otherwise have to invent.

## The two switches under the draw distance

`Draw distance follows the frame rate` is `[view_distance_fix] FrameBackoff`. It is the frame
governor alone: the draw distance drops when a scene costs more frame time than the distance is
worth and comes back when the scene gets cheaper. That moves the fog with it, so the world visibly
opens and closes as the frame rate wanders, which some people want to stop.

`Keep the draw distance (costs frame rate)` is `[view_distance_fix] StrictViewRange`, and it is the
bigger hammer. It declines the governor, the level-opening window, the scripted-camera raise **and the cell
watchdog**, so the number typed two rows above is the number in force on every frame.

**The watchdog is the part to understand before leaving this on.** It is not automation for taste,
it is a memory-corruption guard. The cell table ends exactly where the bucket list heads begin, its
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
real but conditional, and it is written here and in the ini rather than squeezed into a label.

**The two switches are mutually exclusive, and the second one wins.** While strict is on the
frame-rate row is greyed: it reads off, it cannot be clicked, and that is the state the game is
genuinely in, because strict declines the governor outright. A row still reading ON over a governor
that is not acting would be a lie in the one place a reader looks to find out what is happening.

What it deliberately does **not** do is write `FrameBackoff`. Someone who had the governor on, turns
strict on to look at something and turns it off again gets their governor back, rather than
discovering that a setting they never touched has been changed for them. So the row reports the
state the game is in and the file keeps the state the reader asked for.

## The field of view row, and the two control switches

Three rows that reach settings owned by other DLLs, so the panel can change them without leaving
the game to find the screen they normally live on.

`Field of view` is `[variable_fov] ExtraDegrees`, it is the only row here that shows one number and
writes another, and it is the only one with a **slider**. That key is an offset from a base that
depends on the canvas, the aspect mode and the engine's own projection, none of which exist in this
DLL, so an offset is a number this row could display and nobody could read. `variable_fov` therefore
publishes `BaseFov`, and the width of the picture is `BaseFov` plus `ExtraDegrees`.

**The base is published rather than the current width, and the first version got that wrong.** The
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
thirty a second rather than at the frame rate; unthrottled it would be several megabytes a second of
file traffic for one handle and the stutter would get blamed on the setting rather than the
dragging. The handle itself is drawn from the pointer at the full frame rate, because drawing it
from the file would move it in thirty steps against a hand moving in sixty. On the other side
`variable_fov` polls every frame but asks the file system for its last write time before parsing
anything, so it notices within a frame without reading ninety kilobytes sixty times a second.

**It is also the only row here that can be unavailable.** Every other row edits a settings file and
works with the DLL that reads it deleted from `mods\`. This one needs a published width, so with
`variable_fov` absent it greys out rather than inventing a number that would be wrong on some
canvas. Its range comes from that DLL's own `SliderMinFovDegrees` and `SliderMaxFovDegrees`, so
widening the in-game slider widens this row with it.

`Free look` and `Strafe` are `[enhanced_input] FreeLook` and `Strafe`, the two check boxes on the
game's own controls screen. They carry that screen's own captions rather than a description this
panel invented, so a reader who has seen it recognises these rows. **Either can be refused**, and the row cannot
tell in advance: strafe needs mouse look and the keyboard axis reader, because the engine's
`turnWheel` is the only turn channel and driving it sideways would clear the mouse with it; free
look needs a follow camera that `enhanced_input` recognises. When one is declined it says why in the
log and the row reads back off on its next rebuild, which is the honest outcome.

**All three needed the owning DLL to start reading its own settings back.** Both of those screens
pushed outward only: they applied a change and then wrote the file, and nothing ever read it. A row
here would have done nothing until the next launch. `variable_fov` and `enhanced_input` now re-read
these keys once a second, the way `view_distance_fix` already did, so a row takes effect within the
second.

`Mouse speed` is `[enhanced_input] MouseDegreesPerCount`, named after that screen's caption as
well, and it has a track too. Unlike the
field of view it needs nothing published, because both of its ends are fixed and are the same band
the controls screen's own slider spanned. It is here because that screen no longer offers it.

**And a last row decides whether any of this appears in the game's own menus.** `Show extra menu
options (restart the game)` writes `[variable_fov] MenuSlider` and `[enhanced_input] MenuWidgets`
together. Both ship off, so the video options and controls screens look as they did in 1999, and all
four settings live here instead. It reads on only when both keys are on, since a half state can only
be reached by editing the file by hand and reporting that as on would claim a screen is changed when
it is half changed.

It says "on restart" on the row itself because it means it: both screens are patched by repointing
the engine's own widget table once while the game starts, and this project has no path that puts
such a table back. A switch that appears to do nothing is worse than one that explains itself.

**The mouse speed slider is gated with the rest, and that was the one real decision.** Mouse
look ships on, so that slider was its only adjustment inside the game, which is exactly why the row
above exists. Leaving one widget behind on a screen meant to look untouched would have been the
worse answer: either the screen is the one the game shipped or it is not.

## The fog rows

Two rows under **Utilities**, both `[view_distance_fix]` keys the fog reads while the game runs, so
each takes effect within about a second and neither needs a restart.

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

The last row but one under **Utilities** edits `[dev_overlay] DevMenuSize`, which is how much bigger
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

`overlay_model` covers the half that can be checked without the game: the search, the folding,
both groups on the Original tab, the tab switch, the bounds of the search box, the queued-swap
sentinel (nothing reads as pending before `resolve()` has run) and every index that does not
exist. The checks live in `unittests/overlay_model.c`.

Every row in the panel has been opened, drawn and switched against the running game, and the
layout has been through several rounds of correction against screenshots. All nine of this
project's cheats are accepted in game, in the 1.5.0 build, which was played through by hand.

**Field-tested against the running game, several rounds:** kill self, full health,
all-weapons-full-ammo (including the shared gate greying both out together once spent, and now the
retail confirmation message on each), both difficulty codes (message confirmed), the debug/fps
toggle, the graphics detail cycler (message and live chip number both confirmed), and the "Tech
Bonus!" message all confirmed working. The four play-as codes are confirmed working through the
queue. The red icon highlight resolves and runs but which icon it affects is still unconfirmed.
Wavering graphics and view credits are both deliberately `n/a`, see above, the latter added after
field testing found it misbehaved when triggered from this panel. **No fog has had two field
rounds, and each found a real bug.** The first version cleared the level's fog flag directly,
which broke the renderer (every moving actor drawn as a flat, unlit silhouette, not recoverable by
toggling the cheat back off); rewritten to push the fog band out instead of touching the flag. The
second version fixed that but declined to restore the band on the way back off, so fog could be
turned off but not back on short of a level reload; rewritten again to remember and restore the
authored band. Both directions are confirmed working now.

**Free camera has had several field rounds.** Pausing the simulation and driving the camera object
directly through a chained detour on `updateCam` both confirmed working; the WASD-along-view-
direction formula was field-tested wrong once (a sign error in the yaw-to-world-axis conversion,
found by comparing against the engine's own render-eye builder and its built-in debug free-cam
rather than guessed a second time) and is now confirmed correct; the mouse axes were field-tested
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
  without that the move does not resolve until the panel is closed by hand, which read, again, as
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

**The teleport writes both copies of the position.** The player carries `pos` at `+0x118` and
`desiredPos` at `+0x124`, and `Plr_CommitPose` (`0x0044C06B`) opens by copying the second over the
first on any frame the player is moving. Writing `pos` alone survived only while the player happened
to be standing still: the moment the movement phase ran it recomputed `desiredPos` from the new
position with its own ground resolution applied and committed that back, so the player arrived at
the right x and y planted on the floor. Writing `desiredPos` alone was tried even earlier and left
them where they started, which is the same fault from the other side. Both are written, and the
vertical velocity at `+0xB4` is zeroed so the fall starts from rest rather than carrying whatever
the player had when the flight began.

**The teleport will not drop the player further than the engine can cope with.** A drop onto a real
floor still breaks it if it is high enough, so the drop is capped at 80 world units and a teleport
past that ends the flight without moving anybody, exactly as F4 does.

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

Those three constants live at `0x004a875c`, `0x004a86dc` and `0x004a86f4` in the shipped
`WMAIN.EXE`. Note that the data addresses quoted throughout these comments come from j0nny's
`obiold.exe` and do not map to the same places in the shipped executable; in `WMAIN.EXE` that
range is inside `.rsrc`. They were read by finding the `FCOMP` instructions that reference them.

Tested in game on Windows: a boosted jump onto ground (immune as before), jump boost off a ledge
into the void (dies promptly, death screen loads correctly, audio normal), and, after the
measurement was corrected, drops from real height that land properly alongside a refusal past the
cap. The log lines to look for:
```
[dev_overlay] the floor probe resolved: falls and teleports can both be asked about
[dev_overlay] the free camera teleport key dropped the player at 38.2 127.1 101.0
[dev_overlay] the teleport was refused and the camera returned instead: it is 104 units of drop, past the 80 this engine can finish a fall from
```
