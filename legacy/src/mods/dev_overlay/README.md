# dev_overlay

A panel over the running game, opened with **the key below Escape**. It holds the cheats today. The
name is what the thing is, not what is in it: the diagnostics and developer tools that come later
are groups inside this same panel, and a shipped DLL cannot be renamed without breaking every
`engine_fixes.ini` that mentions it.

## What it looks like

Two tabs under a heading that reads `Cheatmenu`.

* **Original** lists the eleven codes the shipped game already understands.
* **OpenPhantom** lists the two this project adds.

Everything starts folded. The search box filters by name and opens a group that has matches, and
clearing it puts the fold back the way you left it. A row shows its state as `ON` or `OFF` in a
chip, or as `n/a` with no chip when the engine site behind it never resolved. The pointer is the
game's own cursor.

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

## The two cheats this project adds

Both are one detour on one short function, and both work by **declining** rather than by topping a
value up.

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

## The eleven original codes

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

## Configuration: `[dev_overlay]`

| Key | Default | Meaning |
|---|---|---|
| `Enabled` | `1` | |
| `OpenKey` | `0` | The virtual key that opens the panel. `0` accepts whichever key sits below Escape: the caret on a German layout, the backtick on a British one. |
| `TextAlign` | `1` | Which of the font layer's three modes starts a string where it is put. `0` centres it on its position; `1` and `2` are the other two. |

## Limitations

* The pointer arrives in the window's client pixels and is mapped into the picture the engine draws.
  Those are the same size in every mode this project ships, so the mapping is usually the identity.
* Only the eleven toggles are offered from the game's own set. Its seventeen one shot codes are a
  separate effect each, two of them raise a hidden counter that pins the difficulty at its hardest
  row for the rest of the run, and none of that is worth guessing at.
* A cheat whose site did not resolve is shown as `n/a` rather than hidden, so a panel on an
  unsupported executable says what is missing instead of looking empty.
* The panel selects a font, sets an alignment, two scales and a colour, and puts none of them back.
  Nothing else draws text between the panel and the end of the frame, so nothing is affected today.
* The call the paint is redirected from cannot chain behind another DLL that redirected it first.
  Nothing else in this project targets it.

## What was tested

`overlay_model` covers the half that can be checked without the game: the search, the folding, the
tab switch, the bounds of the search box and every index that does not exist. 33 checks.

Everything else needs the game. The panel has been opened, drawn, and its cheats switched; the
layout has been through several rounds of correction against screenshots.
