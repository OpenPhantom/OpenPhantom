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
* **OpenPhantom** lists the three this project adds.

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

While it is open the player's phases are stopped. That is how this engine holds still: it has no
input switch, it simply does not run the phases in a menu, a dialogue or a cutscene, which is also
what the mouse look in `enhanced_input` already watches for. So one field does the whole job and no
other DLL had to change.

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

## Configuration: `[dev_overlay]`

| Key | Default | Meaning |
|---|---|---|
| `Enabled` | `1` | |
| `OpenKey` | `0` | The virtual key that opens the panel. `0` accepts whichever key sits below Escape: the caret on a German layout, the backtick on a British one. |
| `TextAlign` | `1` | Which of the font layer's three modes starts a string where it is put. `0` centres it on its position; `1` and `2` are the other two. |

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
