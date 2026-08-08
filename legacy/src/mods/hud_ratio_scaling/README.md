# hud_ratio_scaling

**Produces:** `hud_ratio_scaling.dll` -> `mods\` ,  replaces the earlier `text_aspect_fix.dll`

One size knob for the whole HUD, and square glyphs everywhere the engine draws text.
Delete `text_aspect_fix.dll` from `mods\` when you install this one; they hook the same function.

## Supported executables

Retail `WMAIN.EXE` (EN/DE) and the Fix Pack build: all six sites resolve on all four of them. On
`obi.exe`, which is a recompile, two of the six move and the parts that need them disable
themselves with a log line.

## Configuration: `[hud_ratio_scaling]`

| Key | Default | Range | Meaning |
|---|---|---|---|
| `Enabled` | `1` | | |
| `SquareText` | `1` | | make every glyph square **outside** the HUD; identity at 4:3 |
| `SquareHud` | `1` | | derive the bar widths from the height; identity at 4:3 |
| `HudScale` | `1.0` | 0.25-4.0 | one multiplier for bars, icon, both numbers and the HUD digits |

**`SquareText` and `SquareHud` are independent.** They share one detour, on
`font3d_setGlyphScale`, but that detour is now installed whenever *either* of them is in use (or
`HudScale` is not 1.0). Before, it was installed only for `SquareText`, so the combination
`SquareText=0, SquareHud=1` would have moved the bars while leaving the digits at the size the
engine picked.

**`SquareHud=0` with `HudScale=1.0` is the original, bit for bit, at every resolution.** In that
configuration none of the HUD's three hooks is installed at all.

## What the HUD actually does

Four blocks, all in framebuffer pixels, all rebuilt from the live screen size on every frame:

| Block | Rectangle |
|---|---|
| health bar | x `1 .. 0.2*W`, y `0.93333*H .. H-1` |
| force bar | the same x, y `0.93333*H-0.06667*H-1 .. H-2-0.06667*H` (Jedi only) |
| weapon icon | x `0.2*W+1 .. +0.2*H`, y `0.93333*H-1 .. H-1` |
| escort bar | centred, `0.2*W` wide, y `0 .. 0.06667*H` |

The force bar's edges are each built as "the matching health edge, lifted by one bar height and by
one more pixel", so its bottom is `H-2-0.06667*H` and not `H-1-0.06667*H`. That single pixel is the
whole tolerance budget of the recogniser that finds it.

Three consequences, and they are what this DLL exists for.

**The bars stretch, the icon does not.** A bar is `0.2*W` wide and `0.06667*H` tall, so its shape
follows the aspect ratio, 4:1 at 4:3, 5.33:1 at 16:9. The weapon icon takes its *width* from the
height and therefore keeps its shape. `SquareHud` derives the bar widths from the height as well.

**The weapon icon is not exempt.** Its width comes from the height but its **left edge** comes from
the width, so the two terms need different multipliers. Exempting the block entirely, which is
what this DLL used to do, leaves a hole between the bar and the icon exactly as wide as the bar
lost: 97 px at 1920x1080.

**Nothing grows relative to the screen.** Every extent is a fixed fraction, so the HUD occupies the
same share of the picture at 480p and at 4K. On a large screen that reads as small. `HudScale`
multiplies every extent about the edge its block is anchored to, the bars and the icon about the
bottom-left, the escort bar about the top centre, so nothing walks off screen.

### The two multipliers

```
s   = HudScale
mW  = (SquareHud ? (4*H)/(3*W) : 1) * s      every extent the engine took from the WIDTH
mH  = s                                      every extent the engine took from the HEIGHT
```

`4H/3W` is exactly 1.0 at 4:3 because `4H` and `3W` are then the same integer. On a 5:4 screen
(720x576, 1280x1024) it is 1.0667, so there "square" makes the bars **wider** than authored, not
narrower. Nothing leaves the screen at either of those two modes.

### The digits

Inside the HUD the glyph pair becomes `(sx*mW, sy*mH)`. The renderer draws at `(sx*W/640,
sy*H/480)`, so that is what makes the digits grow by `H/480`, the factor the squared bars grow by
, instead of by `W/640`. At 1920x1080 the HUD's authored `(0.8, 0.8)` becomes `(0.6, 0.8)` and the
drawn glyph is `1.80 x 1.80` px: square, and 2.25 times its 480p size, like the bars.

Outside the HUD the older rule is unchanged: `sy = sx * 3W/4H`. It raises the vertical rather than
lowering the horizontal because the swift menus already pass a hand-corrected horizontal, and
lowering that would make menu text smaller than it has ever been.

## Engine locations

| Site | Retail VA | What |
|---|---|---|
| `font3d_setGlyphScale` | `0x46B293` | detoured, 10-byte prologue |
| `graphics_currentModeSize` | `0x49385A` | called, never patched |
| `status_drawHud` | `0x459230` | detoured; it is the gate for the three below |
| `texture_drawSprite` | `0x42963B` | detoured, acts only inside the gate |
| `font3d_draw` | `0x46B3C0` | detoured, acts only inside the gate |
| `graphics_setMode` | `0x46BC85` | detoured, epilogue only, the live re-layout |

**`font3d_draw` is `0x46B3C0`, not `0x479370`.** At `0x46B3C0` the coordinates are framebuffer
pixels, the same space the sprite rectangles are in. `0x479370` is the string renderer one level
further down, and by the time coordinates reach it they have been divided by `W` and `H`; they are
canvas fractions in `[0,1]`. This DLL used to hook `0x479370` and gate on `y <= H/2`, a comparison
between a number below 1 and a number of at least 32, which is true on every call that has ever
been made. That hook installed successfully and never once executed its body.

The gate matters. The sprite blitter draws far more than the HUD and the font layer draws every
subtitle and menu string, so the three inner hooks check "is `status_drawHud` on the stack" first
and pass everything else straight through. Inside the gate a rectangle is only changed when it
matches one of the four formulas above; anything else is left alone.

## Known limitations

* **The HUD is recognised by its geometry.** A build whose layout formulas differ would simply not
  match, and the HUD would be drawn as authored. That is the safe direction. The log now names each
  of the four blocks once per display mode, so what did and did not match is readable.
* **The classifier sees rectangles it does not own.** Any other DLL that chains onto
  `status_drawHud` blits inside the same window, and its rectangles reach the classifier with the
  gate open. They fall out as "not a HUD block" and are forwarded untouched. That is why the four
  tests pin every edge of a block and why the +/-2 px tolerance must not be widened.
* **Text another DLL draws inside the gate, on the bottom half of the screen, would be moved with
  the HUD numbers.** Nothing in this tree does that today.
* `graphics_setMode` is not the only writer of the display size, the shutdown path stores `-1.0f`
  into the same globals from four call chains that never enter it. The hook therefore sees every
  *valid* size change and not every write, so the layout path validates the size it is handed
  (64 ... 16384 px) instead of trusting the hook to have latched it.
* **`HudScale` can only be re-read live when the HUD transform was already active at start-up.**
  With `SquareHud=0` and `HudScale=1.0` none of the three HUD hooks is installed, so there is
  nothing for a new value to act on and the mode hook says so rather than pretending otherwise.
* The engine's `+1/+1` drop shadow and its `-2 px` ammo baseline nudge are absolute pixels in the
  original. Under the transform they become `+mW/+mH` and `-2*mH`. That is a change, about one
  pixel at 480p and two at 1080p, and it is the intended one.
* The level-intro crawl passes `(640/W, 480/H)` and is the identity under the `SquareText`
  correction by construction; it is the one place the original compensates by hand.
* **Whether the HUD textures survive a mid-session mode change was never established.** The
  shutdown path tears the surfaces down before they are rebuilt, and whether the texture layer
  restores them behind the handles the HUD holds was not read. If it does not, a live resolution
  change leaves a broken HUD for reasons that have nothing to do with scaling.

## Fallback

If `graphics_setMode` cannot be hooked, the layout **still follows the resolution frame by frame**:
nothing is cached, by the engine or by this DLL. `status_drawHud` rebuilds all four rectangles from
the live screen size on every frame and the font layer re-reads the display size on every string,
so a resolution change needs no notification to take effect. What is lost is the fresh log line
after a mode change and the live re-read of `HudScale`, which then needs a restart. The warning in
the log says exactly that.

If `font3d_draw` cannot be hooked, the four rectangles still move and the two numbers do not; the
summary log line reports which of the two happened rather than claiming both.

If `font3d_setGlyphScale` cannot be hooked, the rectangles still move and the digits keep the
engine's size.

## Testing status

* **Compiled and linked** with the configured 32-bit MSVC toolset, `/W4 /WX`, zero warnings.
* **Unit tested offline**, `unittests/hud_layout.c`, 56 checks, all passing, over all 26 display
  modes the game offers. They cover: every one of the four blocks classified correctly and
  uniquely at every mode; both numbers landing on the exact centre of their own block at every
  mode; and the two identities, measured as a **bit-exact deviation of 0.0**, `SquareHud=0` with
  `HudScale=1.0` at every mode, and both `SquareHud` values at every 4:3 mode.
* **Every byte pattern verified offline** against all four retail `WMAIN.EXE` with
  All six patterns resolve uniquely on each.
* **NOT accepted in game.** No screen has been looked at. Nothing here is a statement about how it
  looks or behaves while playing.
