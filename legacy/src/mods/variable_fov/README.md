# variable_fov

**Produces:** `variable_fov.dll` -> `mods\`

An adjustable, aspect-correct field of view, with a real slider in the engine's own video options
screen, of the engine's own `SW_SLIDER` class, drawn by the engine's own toolkit. Nothing here
draws a pixel itself.

## Supported executables

Retail `WMAIN.EXE`, MD5 `7c5af8428c19b17cca09ae3a49bd10ef` (EN and DE), and the Fix Pack build,
which differs from retail by one import-name byte. On `obi.exe` (the Edit Tool's recompile) the
patterns do not resolve and the feature disables itself with a log line.

## Configuration: `[variable_fov]`

| Key | Default | Range | Meaning |
|---|---|---|---|
| `Enabled` | `1` | | |
| `AspectMode` | `0` | 0-2 | 0 Hor+, 1 Vert-, 2 Stretch (touch nothing) |
| `BaseVerticalDegrees` | `46.826` | 30-60 | the byte-native 4:3 vertical |
| `ExtraDegrees` | `0.0` | -120-120 | the offset on top of what the aspect mode computes; the slider writes this back. **May be negative**, see below |
| `Menu3dMode` | `0` | 0-2 | 0 fill width, 1 keep 4:3, 2 native 640 px |
| `MenuSlider` | `1` | | add the slider to Options -> Video |
| `SliderMinFovDegrees` | `60` | 30-120 | the leftmost notch, as an **absolute** horizontal angle |
| `SliderMaxFovDegrees` | `120` | min+2-170 | the rightmost notch, likewise absolute |
| `Language` | *(empty)* | en\|de\|fr\|it\|es | caption language; empty follows Windows |

### The slider is an absolute angle, and that is a change

It used to select an **offset** on top of whatever the aspect mode computed, so notch 0 was "the
unmodified view". That reads well at 4:3, where the authored horizontal really is 60 degrees, and badly
anywhere else: on a 16:9 frame the Hor+ correction already puts the horizontal near 75 degrees, and an
offset that can only add cannot come back down. There was no way to ask for 60.

Now notch *n* means an absolute `SliderMinFovDegrees + n times 2` degrees, the number on the slider is
the number in the caption. The offset is still what is stored and applied; it is simply computed
from the angle picked, which is why the same notch is the same **angle** at every resolution and
the offset behind it differs.

Two consequences worth naming. `ExtraDegrees` **can now be negative**, and it has to be for this to
work at all. And choosing less than the aspect mode's own answer costs vertical view: at 16:9 an
absolute 60 degrees horizontal gives about 36 degrees vertical against the authored 46.8 degrees, i.e. narrower than the
original game. That is a legitimate choice and it is not the default, the default range simply
starts there.

`SliderMaxDegrees` is **no longer read**. It was renamed rather than reinterpreted: a tuned `40`
read as an absolute angle would have meant 40 degrees of view. A file still carrying it is told once in
the log.

## Engine locations

| Site | Retail VA | What |
|---|---|---|
| `rdCamera_BuildProjection` | `0x475FFA` | detoured, 6-byte prologue |
| the 3-D menu focal operand | `0x45C431` | operand repointed at our own cell |
| `options_video` | `0x4410F2` | detoured, 9-byte prologue |
| the widget-table `push imm32` | `0x4410F2 + 0x1A` | repointed at our copy of the array |
| `render_frameEnd` | `0x46C139` | detoured via `common/frame_hook.c`, for live preview |

`rdCamera+0x38` (`fovDeg`) is written immediately before the engine's own rebuild, so the focal
length and the six frustum planes can never fall out of sync with it.

## What this engine cannot do

**A separate vertical field of view is structurally impossible.** One focal length serves both
axes: `bapvrt_frameSetup` copies `cam+0x3C` into `g_projScale` once per frame and computes
`s = g_projScale / y` for *both*. `cam+0x40` only moves clip edges, and `cam+0x44` belongs to the
orthographic arm, which is dead code in this game. The vertical is therefore *displayed* in the
caption, not controlled.

Two earlier claims to the contrary are byte-refuted; the refutation is in `variable_fov.c`.

## Where the slider sits

Slider at (0, 376, 250, 50) with its caption at (0, 426, 250, 50), **stacked, bar over words,
flush to the left edge** of the 640x480 canvas, on the free strip along the bottom of the screen.

The pair was side by side first (caption at 20,392 with the gauge to its right) and that was
rejected on the picture rather than on the numbers: the caption sat in the middle of an otherwise
empty strip with the bar floating beside it, and the two read as two unrelated things. Stacked and
hard left they read as one control, and the eye reaches the bar before the words naming it, the
order the authored screen already uses for gamma.

**Three earlier placements were wrong, and the third was wrong for a reason that invalidated the
reasoning behind all of them.** The first sat at (25, 250) and overlapped check box 3, a plain
collision. The second, (50, 330), collided with nothing and looked detached from the column above
it. The third put the pair on a `popup.bmp` plate and reasoned about cropping that plate to size.

**A widget rectangle is neither a crop nor a scale.** The blit every widget in this toolkit ends in
takes canvas, bitmap, x and y, no width and no height, and clips only against a hard-coded
640x480. `popup.bmp` is 300x200 and draws 300x200 wherever it is put. The only free space on this
screen is 108 pixels tall, and **no plate in the shared bitmap table fits it**: popup is 200 tall
and rusure is 148. So the pair is drawn without one.

Where the space is: the authored rectangles occupy the left column down to y=305 and the right down
to y=350, and the background `dialog.bmp` is opaque over columns 23-621, rows 10-371 (plus a
one-pixel hairline at column 299 reaching row 446). The BACK button's artwork plate occupies
columns 486-591, rows 399-465.

**BACK's plate and BACK's rectangle are not the same thing, and the hit test uses the
rectangle.** The plate starts at x=486; the widget rectangle starts at **x=484**. The hit test
walks the widget table from index 0 and stops at the first widget containing the point, with the
authored widgets ahead of every appended one, so a slider reaching x=484 would hand its two
rightmost columns to BACK and dragging to maximum could leave the screen instead of setting the
value. The usable strip is therefore **x 0..483, y 372..479**, and the slider ends at 483.

The strip is 108 rows and the stacked pair needs 100, so the margin is four rows top and bottom.
That is tight by choice: the alternative was a comfortable margin and a caption somewhere it does
not belong. Which of the two arrangements to use was settled by rendering both over the real
extracted background and looking at them, not by arithmetic on rectangles.

The compile-time checks in `fov_menu.c` assert the **drawn** footprint, a slider occupies exactly
`slgauge.bmp`'s 250x50 from its own (x, y), whatever width the rectangle claims, because the
previous ones checked a width the engine discards against a plate that was never drawn where it was
asked for.

**This screen gets no borrowed panel** either, unlike anything the controls screen might want. The
audio screen's panel art sits on the **right**, where this screen already has its mode list at
(300, 50, 300, 100) and its apply button at (300, 150, 300, 50).

## Known limitations

* **Up/Down do not reach the slider.** `options_video`'s arrow navigation is a hand-written graph
  over the authored ids and everything else falls into `default: break`. Tab, the mouse and Escape
  all work. Moving that switch means rewriting a jump table for a convenience, so it is stated
  rather than patched around.
* The clamp is 5-170 degrees, deliberately below the engine's own 179: `bapdraw_drawWorld` computes
  `tan((fovDeg + 3)/2)`, which goes negative from 177 and collects **nothing**.
* Changing `ExtraDegrees` in the ini while the game runs does nothing until the next canvas
  rebuild. The slider does not have that problem: it calls the rebuild itself.

## Fallback behaviour

If the per-frame hook cannot be installed, live preview is unavailable and the log says so. The
slider's final value is still read, applied and saved when the screen closes, the same apply path
is used for both.

If the camera hook fails, the slider is **not** added at all: a slider that drives nothing is
worse than no slider.

If the screen's bitmap-name table cannot be read or an index would fall past its end, the slider is
**not** added either. The engine's own bitmap lookup checks only that an index is not negative, so
an index one past that table is an unchecked read that ends in a file loader, refusing is the only
honest answer.

## Testing status

Built and linked, `/W4 /WX` clean. `unittests/fov_math.c`, 43 checks, all passing, including two
cross-checks against retail data: a 4:3 canvas at the authored vertical must yield exactly 60.000 degrees,
and a 640x480 canvas at 60 degrees must yield a focal length of 554.256, the hard-coded 3-D menu constant.
Offline verification of every pattern passes on both retail builds. **Not accepted in
game.**

To check in game: open Options -> Video, confirm the slider is under the check boxes, that dragging
it changes the view live, that the caption names both angles, and that leaving the screen writes
`ExtraDegrees` into `engine_fixes.ini`.
