# enhanced_resolution

**Produces:** `enhanced_resolution.dll` -> `mods\`

Modern resolutions, listed in the options screen as if they had always been there, and the window,
the mouse pointer and the input devices kept in step with them.

> The game runs **16-bit**: the mode list accepts nothing else, and the 2-D layer writes two-byte
> pixels straight into the back buffer. Which resolutions actually exist is therefore up to your
> DirectDraw layer (dgVoodoo2, DDrawCompat). `LogModeTable=1` prints the complete table the layer
> reported, so you can see it. `ModeBitDepth` is documented in `engine_fixes.ini` and should be
> left at 16; see `mode_depth.h` for how far 32 was taken and where it stops.

## Supported executables

Retail `WMAIN.EXE` (EN/DE) and the Fix Pack build. On `obi.exe` the mode-list patterns do not
resolve. What does resolve there, because those functions survived the recompile unchanged and
sit 0x60 lower, is the two cursor patterns (`0x46A0B5`, `0x46A0F5`), the two input-focus patterns
(`0x48D6B9`, `0x48D16F`), `graphics_setMode` (`0x46BC25`) and `stdDisplay_getModeSize`
(`0x4937FA`). So the window fit works on `obi.exe` too, minus the pre-call size lookup, which needs
the mode table that the aspect gate anchors.

## Configuration: `[enhanced_resolution]`

| Key | Default | Meaning |
|---|---|---|
| `Enabled` | `1` | |
| `FilterModeEnumeration` | `1` | keep the engine's 64 mode records for modes it can use: a mode that is not 16 bit RGB, or a resolution a usable record already holds, is answered without taking one. The engine cancels the enumeration when those 64 are gone, so on a driver reporting three bit depths two thirds of them were spent on modes the options screen can never show |
| `WidescreenModes` | `1` | lift the 4:3 lock in the mode list |
| `MaxMenuModes` | `63` | cap for the engine's 64-slot label array; 4-63 |
| `ForceWidth` / `ForceHeight` | `0` | 0 = leave `obi.ini` alone |
| `LogModeTable` | `0` | dump the raw DirectDraw table on the first enumeration. A diagnostic, so it is off in a release |
| `LogMenuArt` | `0` | report every distinct menu picture once, with the blit path it takes, and count the textured quads. A measurement rather than a feature, and off in a release. See **What a menu is actually made of** below |
| `MenuKeepsResolution` | `1` | stop menus switching to 640x480 |
| `SubtitleScale` | `1.0` | how big the subtitles are, as a multiple of the size they have at 640x480. `1.0` is exactly the authored size and proportions at any resolution, `0.5` to `3.0` either side of it, and `0` leaves the engine's own shrinking behaviour alone. Settable from the developer menu and applied within a second; only `0` needs a relaunch. See **Subtitles that scale** below |
| `FitWindowToMode` | **`0`** | **last resort.** Move and size the window to match the display mode. Only for a setup with **no graphics wrapper at all**, where the window really can end up smaller than the mode. It costs the engine's window its position at screen (0,0), which its own pointer handling assumes. |
| `WindowMode` | **`0`** | the shape of the window: `0` authentic, `1` borderless at the size of the monitor, `2` a caption and a border at a chosen size, `3` the same with a resize grip and a maximise button, `4` no frame at a chosen size. `0` changes nothing. Modes `2` to `4` take their size from the two keys below; `1` takes the monitor. On its own this changes the window and not the device, so Alt Tab still costs a reset; `WindowedPresent` below is what changes that |
| `WindowedPresent` | **`0`** | build the device windowed instead of letting it own the display. One byte: the cooperative flags the engine hands DirectDraw, `DDSCL_FULLSCREEN\|DDSCL_EXCLUSIVE` to `DDSCL_NORMAL`, keeping `DDSCL_FPUSETUP`. This makes Alt Tab free, and the sized window modes need it too. Writes `DdrawWriteToGDI` in `dxwrapper.ini` to match, which is the only key this patch ever writes in that file |
| `WindowedWidth` | `0` | the client width for modes `2` to `4`; `0` means the resolution being rendered. Clamped to the monitor. The developer menu offers this as a list of the sizes the display reports, and the chosen one also becomes the resolution the game renders at from the next start |
| `WindowedHeight` | `0` | the same for the height. Each axis falls back on its own |
| `WindowedFill` | `1` | make the whole picture fill the window when the window is not at the screen's origin. Only does anything with `WindowedPresent=1`. See **Why a moved window showed part of the picture** below |
| `PointerReleaseKey` | `0x91` | a virtual key that hands the mouse pointer back to Windows, so a framed window's title bar and buttons can be reached. Scroll Lock by default. `0` switches it off |
| `FullscreenToggleKey` | `0x0D` | the key that, held with Alt, swaps between a window and the whole monitor. Enter by default, so Alt+Enter. Alt is required and is not configurable. `0` switches it off |
| `KeepCursorInWindow` | `1` | re-centre the mouse pointer in the window's client area instead of at screen (320,240) |
| `ClipPointerToWindow` | `1` | hold the pointer inside the client area while the game window is in front, and let go the instant it is not |
| `ReacquireInputOnFocus` | `1` | send the engine the input resume it authored and never sends, so the keyboard and mouse still work after an Alt-Tab |
| `WidenMenuCursorArea` | `1` | let the **drawn menu cursor** move over the whole 640x480 menu canvas instead of the 607x447 island the engine clamps it to. Does **not** move or rescale any menu, the engine already centres those itself. It is widened to the **canvas** and deliberately not to the display mode: the pause screens repair themselves through damage rectangles clipped to that same hard-coded 640x480, so a cursor outside it is drawn and never erased, which was reported from a 3840x2160 session as the cursor's blue glow smearing across the border. Widening to the canvas is exactly the region that repaints, so it keeps the engine's guarantee |
| `ClampMenuSpritesToIsland` | `1` | the erase-side companion of `MenuKeepsResolution`: clamp the menu toolkit's sprite draws to the 640x480 island, gated on the engine's own widget-pass flag so the HUD and the frozen pause backdrop pass through untouched. Closes the reported blue stamp the hovered button's halo left on the island's border (drawn against the screen, repaired against the canvas). Bit-identical for every sprite that fits the island, and a sprite drawn with a partial fill is passed through untouched |
| `MenuScale` | `0` | how many times its authored size to draw the 640x480 menu canvas at. `0` is automatic: the ratio comes from a converted artwork set when one is mounted, and from the display's own resolution when one is not. A number sets it by hand, up to the 4095/640 ceiling the run length format imposes. Declines when `WidenMenuCursorArea` is `0`, since a scaled menu inside the shipped cursor cage has buttons the pointer cannot reach. It is no longer half a feature without converted artwork: menu bitmaps are replicated to the canvas as they load. See **Menus at any resolution** below |
| `MenuArtDirectory` | `menu_hd` | a folder of converted artwork to mount ahead of the game's own archives, or empty for none. Optional now rather than required: a picture that is already the size the canvas wants passes straight through the replication, so a converted set is a one time cost paid instead of a per load one. It was undocumented here until the replication made it optional |

## Engine locations

| Site | Retail VA | What |
|---|---|---|
| the 4:3 gate in `graphics_buildModeList` | `0x46C6D9` | two bytes, `EB 32` -> the accept label |
| `graphics_enumModes` | `0x46C932` | detoured, 9-byte prologue |
| `graphics_setResolution` | `0x46BE3D` | detoured, 6-byte prologue, **only** when `ForceWidth`/`ForceHeight` are set |
| `graphics_setMode` | `0x46BC85` | detoured, 6-byte prologue, **only** when `FitWindowToMode=1`. The choke point every valid mode change passes through |
| `stdWin95_setDisplayMode` | `0x498C86` | detoured, 6-byte prologue, **only** when `WindowMode` is not `0`. The engine's own style-and-size switch; the correction runs before **and** after the original. Masked signature, anchored on the `push 0x10CF0000`, unique in all three shipped builds |
| `graphics_setMode` (second hook) | `0x46BC85` | detoured a second time, **only** when `WindowMode` is not `0`, so the shape is reasserted before and after every device rebuild. `stdWin95_setDisplayMode` fires once a session and by then the first device already exists |
| `stdDisplay_ddSetMode` cooperative flags | `0x492C43 + 0x12` | one byte, `0x11` to `0x08`, **only** when `WindowedPresent=1`. Read back before writing, so a second install declines |
| `sys_main` | `0x43E5E0` | **read, never patched**, and only for the address inside it. The call at `+12` is `sys_shutdown`; its own prologue is one hundreds of functions share, so the address is read out of the call rather than matched for. Resolves in both `WMAIN.EXE` builds, absent from `TPM.EXE` |
| `sys_shutdown` | read from the call above | **called**, when the window's close box is used. The engine's own teardown, the same one `sys_main` runs on a normal exit |
| the game's window procedure | subclassed | `SetWindowLongA(GWL_WNDPROC)`, chained with `CallWindowProcA`. Answers `WM_CLOSE`, which the engine swallows by design, and only when the window has a close box to click |
| `dxwrapper.dll` import of `user32!GetClientRect` | import table | replaced, **only** when `WindowedFill=1` and `WindowedPresent=1`. Scoped to that one module, so this patch's own calls still get the truth |
| the game's own import of `user32!SetCursor` | import table | replaced, **only** when `PointerReleaseKey` is set. Substitutes an arrow for the `NULL` the engine passes, while the pointer is released |
| `stdDisplay_getModeSize` | `0x49385A` | **called, never patched**, reads back the size that is really set |
| `swmenu_enterMenuMode` | `0x45F7AC` | operand repointed at a cell holding `0x7FFFFFFF` |
| `swmenu_modeIsUsable` | `0x45F686` | the same, and both must read the SAME cell |
| `g_aRawMode`, `g_numRawModes`, `mem_free` | gate -0xA7 / -0x12C / -0xF4 | read backwards from the gate, each opcode-checked |
| `control_recentreMouse` | `0x46A115` | detoured, 7-byte prologue, the pointer anchor |
| `control_captureMouse` | `0x46A155` | detoured, 8-byte prologue, the re-anchor on activation, and the operand its `call` carries yields the engine's window handle |
| `stdControl_setFocus` | `0x48D719` | **called, never patched**. Acquire/Unacquire on the DirectInput keyboard and mouse |
| `stdControl_resync` | `0x48D1CF` | **called, never patched**, drains both device buffers, releasing everything held |
| `swrle_windowProc`, the cursor clamp | `0x460C04`..`0x460C7C` | four immediates rewritten from `0x25F`/`0x1BF` to canvas width minus 33 and canvas height minus 33, **only** when `WidenMenuCursorArea=1`. The block's two origin operands are read back as proof the block is the right one and are **never written**; an earlier version repointed them at zero cells to make the clamp screen relative, which is the fault above. One write step, so there is no partial state. 121-byte masked signature; in `obi.exe` it resolves at `0x460BA4` |
| the DirectDraw enumeration callback | `0x4928FC` | detoured, 6-byte prologue, **only** when `FilterModeEnumeration=1`. Address free: the mode counter, the 64 cap, the 0x54 stride and the table base are all read out of the matched operands and checked before use |
| `swmenu_render`, the widget-pass bracket | `0x45DC6F`..`0x45DCB9` | **read, never patched**: address-free masked pattern over `inc g_tickCounter / mov [flag],1 / cmp [parent],1`; the flag cell is read out of the `C7 05` operand and cross-checked against the closing `mov [flag],0` at +0x41. The gate for the island clamp. In `obi.exe` it resolves at `0x45DC0F`, with the flag cell at `0x008BFB40` instead of `0x008BFBA0` |
| `texture_drawSprite` | `0x0042963B` | detoured, 9-byte prologue, **only** when `ClampMenuSpritesToIsland=1`; chains with `hud_ratio_scaling`'s detour on the same function in either load order |
| `swrle_blit`, the canvas clip | `0x004616CC` | two immediates at `+0x30` and `+0x37`, `640`/`480` -> `640N`/`480N`; the function reads the destination surface size and discards it |
| the menu origin block | matched **twice** | `0x0045D69D` and `0x0045D7CB`; three operands each repointed at cells holding `640N` and `480N`, which also gives `g_menuScale` its multiplier without touching its non-popping `fst` |
| `swmenu_open` | `0x0045D9F5` | detoured, 8 byte prologue; scales each menu's widget rectangles once, which the draw and the hit test both read |

## Why the gate alone is not enough

`graphics_setResolution` never tested the aspect in the first place, a widescreen resolution in
`obi.ini` already worked. The lock only ever hid modes from the **menu**. And the menu label array
holds exactly 64 entries with **no bounds check**; the next live datum behind it is the menu
descriptor. A modern driver can enumerate more 16-bit modes than that, so the enumerator is wrapped
and capped, and the dropped labels are handed back to the engine's own allocator.

## The menu canvas follows the display

Changing resolution in the game's own options screen lays the menus out again at the new size,
artwork included, instead of abandoning the canvas when it no longer fits. That check still exists
and is still a memory safety measure; it is the fallback rather than the answer.

It applies only when the ratio came from the display, which is when there is no converted artwork on
disk. A converted set is a fixed size and the canvas stays welded to it.

Written again from the new ratio: the two canvas immediates in `swrle_blit`, the three cells the
origin operands read, the menu origin, `g_menuScale` and `g_menuTextScale`, the list box row floor
and text insets, the drawn cursor's size, every tracked screen's widget rectangles, and the cursor
cage, sprite island and loading bar. Following on their own: the animated previews, the 3-D widgets,
and every picture widget's rectangle, which `swpic_draw` fills in from the bitmap on every draw.

**Four cells the engine derives, not one.** The menu origin on both axes, `g_menuScale` and
`g_menuTextScale` all come out of one block that runs at startup and on the mode-change message and
nowhere else. On a live change it runs before the canvas has moved, so all four belong to the
resolution just left. Writing only the origin was not enough, and the symptom was unmistakable:
`g_menuScale` is glyph size, base font size and where the 3-D widgets sit, so 4K down to a small
mode left the text scaled for 4K and every screen off its background.

**Rectangles are scaled from the size the screen was authored at**, which the shadow keeps.
Dividing back does not recover it: the engine rewrites a list box's own height to a whole number of
rows plus six, so the number that comes back was never the authored one. Measured in
`unittests/menu_refit.c`, a box authored 100 tall walks to 83 over five changes and loses a row off
the resolution list itself.

List boxes on the open screen are sent `SWMSG_RESET` again, which is the engine's own message and
the only place row height, row count and box height are derived. Artwork is dropped through the
engine's own `swmenu_freeBitmaps`, which it calls itself on the outgoing screen when one screen is
pushed over another; the resource layer counts references, so dropping every tracked screen at once
is safe.

Field confirmed at 2560x1600 refitted to 2048x1536: a widget authored 500x480 came back 1600x1536
and the animated preview authored 232x100 came back 742x320, each the authored size through the new
ratio.

## A windowed game fits the monitor once its frame is counted

The window is placed by growing the wanted client rectangle by the frame, but the client was only
ever clamped against the raw monitor size. On a 3840x2160 screen a 3840x2160 client came back as a
3862x2216 window at -11,0, with most of its caption above the top of the screen, and the same clamp
decided the resolution written into `obi.ini`. The file said 3840x2160 while the window could only
show 3818x2104, so the picture and the window disagreed by the width of the frame.

`window_mode_fit_frame` shrinks the client until the whole window fits and centres on the outer
rectangle rather than the client. Both the placement and the render size go through it. It is a
separate step from `window_mode_client_rect` because that one answers what was asked for, which is
the same answer whatever frame the mode has, and this one needs a measurement only a live window can
give. Borderless modes measure a frame of nothing and are unchanged. They can still show the
screen's own resolution when a framed one cannot.

## `WindowMode`, and what the window actually is today

The engine already switches window styles and it already picks a size. `stdWin95_setDisplayMode`
at `0x498C86` holds both style words, `0x10CF0000` with a caption and `0x10000000` frameless, and
in both arms calls `SetWindowPos(hwnd, NULL, 0, 0, w + borderW, h + borderH, SWP_NOZORDER |
SWP_NOMOVE)`.

The retail path calls it once, from `main_openGraphics 0x43F4D4` at `0x43F542`, as
`setDisplayMode(1, 0x800, 0x800)`. The bytes at `0x43F536` are `68 00 08 00 00 / 68 00 08 00 00 /
6A 01 / E8 3F 97 05 00`. Two thousand and forty eight is not a resolution; it is a number that in
1999 no display could exceed. The border deltas are added in that arm too, although the style it
selects has no border, and they are `2 * SM_CXFRAME` and `SM_CYMENU + 2 * SM_CXFRAME`, computed
once in WinMain.

So the window the game runs in is frameless, about 2054 by 2077, anchored at the top left. It
covers the screen by being larger than it, and on a display wider than about 2048 it stops covering
it at all. Every description of this engine, including this file until recently, quotes the
CREATION shape instead: `WS_POPUP` at `SM_CXSCREEN` by `SM_CYSCREEN`. That is true for the first
moments of the process and not afterwards.

`WindowMode=1` is therefore a correction rather than an addition: the same frameless style, at the
size of the monitor the window is on. `WindowMode=2` is the new one, and it uses a style word of
ours rather than the engine's `0x10CF0000`, because that word sets `WS_THICKFRAME` and
`WS_MAXIMIZEBOX`. The window procedure at `0x49905E` handles neither `WM_SIZE` nor `WM_PAINT`, and
the class style word at `0x498F74` is a literal zero so there is no `CS_HREDRAW`, so a window the
player could drag-resize would change nothing about what is rendered and repaint nothing while it
happened.

The detour calls the original first and corrects on top of it. That way nothing the original does
is skipped, including its write to `stdWin95_bWindowed`, which is the same shape `window_fit.c`
uses on the mode change beside it.

**`WindowMode` changes the window. `WindowedPresent` changes the device.** They are separate keys
and either works without the other. With `WindowedPresent=0` the cooperative level stays `0x811`,
the primary keeps `DDSCAPS_COMPLEX | DDSCAPS_FLIP`, the frame still reaches the screen through
`Flip`, and Alt Tab still costs a reset and a re-upload of every texture, so a borderless window is
a borderless window over a device that owns the screen.

## `WindowedPresent`, and what it needs from the wrapper

One byte, at `0x492C43 + 0x12`. The engine assembles its cooperative flags as `mov [ebp-0x414],0x11`
followed by `or ah,8`; writing `0x08` over the `0x11` makes the same two instructions produce
`DDSCL_NORMAL | DDSCL_FPUSETUP`. Everything else is the engine's own: it still sets its display
mode, still creates the same flipping surfaces, still presents with `Flip`.

**It needs `EnableWindowMode = 0` in `dxwrapper.ini`, which is how that file ships.** That key does
not mean what its name suggests: it arms the wrapper's own window management, which strips the
caption and border off the game's window and recentres it to a size of its own.

**It also needs `DdrawWriteToGDI = 1`, and this patch writes that itself.** The name describes
something it does not do here: the blit it is named for needs an emulated surface and a game not
using Direct3D, and this game fails the second test from its first scene. What it decides for us is
how big the surfaces the wrapper hands the engine are. With it off they take the desktop's size
whatever resolution the game renders at, which is the wrong size for every windowed arrangement and
the right one for none, so it is not really a second choice to make. It is written whenever
`WindowedPresent` changes, only when the two disagree, and never when that file is absent.

That was measured before it was understood. On a 3840x2160 desktop, rendering at 1600x900 gave
desktop sized surfaces and a black window, and a 1600x900 window over a 3840x2160 render put the
picture in one corner at 1600/3840 of the window. Both were the surface sizing rather than the
device, so an earlier build refused `WindowMode=2` whenever this was on. That refusal is gone.

An earlier version replaced the whole device build instead, with no display mode, no flip chain, a
clipper and a scaled blit, which is the arrangement the DirectX 6 and 7 samples use. It is written
up as refuted in `windowed_device.h`: this engine reads its own front buffer back when a pause page
opens and when the loading screen is built, so a primary that is not the game's own mode corrupts
both.

## Menus at any resolution

The engine's menu blitter copies one source pixel to one destination pixel. It has no scale term,
so a picture only fills a bigger canvas if the file is bigger. Artwork was converted on disk for
that, and the menus were welded to the resolution it was converted for. Below that resolution the
entire scale stood down, because a canvas larger than the back buffer writes past the end of it.

Menu bitmaps are now replicated to the canvas as they load, which removes the weld. Measured on
2026-09-08 at 1920x1080 with no converted artwork present: `640x480` arrived as `1920x1080`,
`500x480` as `1500x1080`, `620x207` as `1860x621`, each exactly half the size the same file has in a
set converted for 3840x2160. A ratio of 3.0 by 2.25 against 6.0 by 4.5 should give half.

**Where.** The BBMP resource handler loads a bitmap in three steps: read the file, convert it to 16
bits, compress it into a run length stream. Between the second and third the pixels are raw and
about to be discarded, which is the one moment a larger copy costs nothing. The CALL is redirected
rather than the compressor detoured, because that function has exactly two callers and the other one
is the save game thumbnail, which is reallocated at a fixed 160x120 on every row change and copied
into at that size. Redirecting one call site cannot reach it.

**Whole pixels only, not a quality preference.** A 16 bit pixel of exactly zero is transparent
to this engine, and the zeros are not only the black an artist drew: the conversion to
16 bits truncates, so in 565 anything under 8 red, 4 green and 8 blue collapses onto zero. A
smoothing filter fails in both directions, and the arithmetic was traced rather than assumed.
Blending a channel value of 1 against an adjacent transparent 0 at half weight gives 128, and the
vertical pass then gives 0: a dark but opaque pixel becomes transparent, punching holes in dark
artwork. Interpolating the other way haloes every transparent edge. Replication cannot do either,
because every pixel drawn is one that was already in the picture.

**Upward only.** Below a ratio of 1 the interval for a destination pixel is shorter than one source
pixel and may contain none, so pixels are dropped: a one pixel border comes out dashed rather than
thinner. At 3840 to 1920, half the source columns are never sampled. The replication refuses to
shrink.

**The ratio now comes from the display when there is no artwork**, which inverts what this file used
to argue. The old doctrine was that reading the ratio from the pictures is what stops the layout and
the pictures disagreeing. That was true and it could not survive a resolution that changes: artwork
cannot follow one and a display always can.

**What it does not do yet.** The menus are correct at the resolution the game started at. A picture
is loaded once and held for as long as its screen is open, so it does not follow a resolution
changed while the game runs. The cache that would have to be dropped is the engine's own, and the
engine drops it itself on every screen change through `swmenu_freeBitmaps`.

## Why a moved window showed part of the picture

`WindowedFill`. With `WindowedPresent` on, the graphics wrapper copies the engine's surface into its
own back buffer before presenting, and before it copies, it clips. It takes the surface rectangle,
which begins at the origin and is the size the engine renders, and intersects it with the window's
client rectangle expressed in **desktop** coordinates. Two rectangles in two different coordinate
spaces, intersected as though they were in one, so the result is only right when the client area
happens to sit at the desktop origin.

Three cases come out of that, and only the middle one is wrong:

| the client area | what arrives |
|---|---|
| contains the surface rectangle | nothing is clipped, the whole picture |
| overlaps it in part | a fragment, stretched over the whole window |
| misses it entirely | the whole surface, scaled to the window |

The third case is the interesting one. When the intersection comes out empty the wrapper leaves both
rectangles null and asks for a whole surface to whole back buffer copy, and the present then scales
that to the client area. That is the behaviour we want, and it is already written: it is what the
code does when its own clip fails.

So the correction makes the clip fail. `user32!GetClientRect` is replaced in the wrapper's own import
table, and only there, and it answers with an empty rectangle in exactly the case above that is
broken. Our own code keeps the truth from the same function, which matters: `focus_guard` confines
the pointer with it and `window_fit` measures with it. It lies in one case out of three because the
other two already put the whole picture on the screen, so correcting them would be a change with no
purpose and some risk.

## Why the pointer could not leave a window with a frame

Two things hold it, and only one of them is ours.

`focus_guard`'s `ClipCursor` is ours, and it stands down while the pointer is released. Tying it to
the window mode instead was tried and was wrong: it left the pointer loose for the whole session, and
the clip is what stops it escaping during fast mouse movement.

The engine's is not a clip at all. `control_recentreMouse` warps the pointer back to the middle of
the client area on every mouse message, which is how a 1999 engine reads a relative mouse: it moves
the pointer to a known place, and the distance the next message arrives from is the delta. So the
pointer can never be walked out, because every movement towards the edge is answered by a warp back
to the middle. The engine also holds `SetCapture` while it is the active application, so a pointer
that did escape would still send its clicks to the game rather than to the button under it.

`PointerReleaseKey` suppresses the warp, drops the capture, and substitutes an arrow for the `NULL`
the engine passes to `SetCursor`. That last part is needed because the engine answers `WM_SETCURSOR`
for the whole window and not only for the picture, so a released pointer is invisible over the title
bar too, where it has to be seen. The capture is dropped every frame rather than once, because the
engine takes it back whenever it handles an activation and coming back from minimised is an
activation.

While released, the engine is sent the same "nothing moved" its own early-out returns, so no delta
accumulates and the view does not lurch when the pointer is handed back.

## Why the close box did nothing

The engine answers `WM_CLOSE` with zero and does nothing else. It never reaches `DefWindowProc` and
never reaches the shutdown. That is deliberate: the window the engine gives itself is `WS_POPUP`
with no close box at all, so the only thing that could ever send it a close was Alt+F4, and
swallowing it stopped a stray keypress ending a level.

Giving the window a frame gave it a button the engine has never had a handler for. So the window
procedure is wrapped, the message is answered, and the engine's own `sys_shutdown` runs a frame
later. Not a synthesised quit: it is the same function `sys_main` calls when the game exits normally,
and the settings file is unaffected either way because this game writes its settings when they change
rather than on the way out.

Two details that are not incidental. It runs a frame later rather than inside the window procedure,
because freeing the world from inside a message dispatched from a frame that is still running is a
crash on exit waiting to happen. And the process ends inside the teardown rather than returning from
it, because the engine only calls that function from `sys_main` with nothing above it but `WinMain`,
whereas this reaches it from inside a frame; returning would carry on running a level that no longer
exists. What is given up by leaving there is the C runtime's exit handlers, after the game's own
shutdown has already run in full.

There is a cleaner path in principle, which is to make the front end return its own quit answer so
the game unwinds to `sys_main` by itself. It was looked at and is not available: that answer is a
local in the front end's stack frame, not a global anything can set.

## What a menu is actually made of

Measured with `LogMenuArt=1` on 2026-09-08, walking eight screens: the front end, the options pages,
a level, a pause page and Load Game. The result contradicted what this file assumed, twice.

**Almost nothing is a picture widget.** Two distinct shapes appeared in eight screens, and both were
already known about: the 232x100 animated previews and the 160x120 save thumbnails. Against those,
4805 widgets drew no picture at all. So the menu's backgrounds, plates and buttons do not reach the
screen through `swpic_draw`, and the draw time upscaler in `menu_preview.c` cannot see them.

**The furniture is textured quads.** 1208 sprite draws across the same eight screens, about 150 a
screen, through `texture_drawSprite`. That function takes float destination edges, so its quad
stretches whatever texture it is handed and needs no larger source to fill a larger canvas. The
largest quad measured was 216x107, which is button sized: nothing approaching a full screen bitmap
was drawn on any path watched.

**So the artwork conversion is not buying geometry.** The menus already scale. What the conversion
buys is what happens to a small texture when a quad stretches it, and the answer today is the
rasterizer's own bilinear filter, the smoothing filter `convert_menu.py` refuses to use, and for a
stated reason: after the engine converts a bitmap to 16 bit, a pixel that is exactly zero is a
SKIP, so a smoothing filter invents near black where black was transparent and new exact zeros
where there were none, haloing every button and punching holes in dark artwork. The converter
replicates whole pixels instead, which cannot invent a colour that was not already there.

That reframes the standing problem. It is not that the menus cannot follow a resolution; it is that
stretching their textures through the wrong filter looks bad, so `MenuScale` ships at 0.
A runtime upscaler using the converter's own rule would have the same output with no conversion step
and no resolution lock, and `menu_preview.c` already does exactly that for the four previews.

Two things this has NOT established, and they are the next measurements rather than conclusions.
What draws the front end's backdrop, which is thought to be a 3-D room rather than a bitmap and was
not seen on either path. And whether converting the artwork changes the sprite sizes, which would
confirm the quads are drawing the converted textures; that needs the same census run against an
installation that has some.

## Why `FitWindowToMode` is off by default

Everything else in this DLL patches the game and leaves the presentation alone, so it behaves the
same whichever wrapper renders afterwards. Moving the game's window is the one thing here that
argues with that wrapper over the same object, and it has cost twice:

1. it broke the engine's own pointer confinement (the section below), so `cursor_anchor.c` and
   `focus_guard.c` exist;
2. it produced a **2 fps** field report. The mode went down to 640x480 with the window, came back up
   to 2560x1440 without it, and the wrapper then downscaled a 1440p image into a 640x480 window on
   every frame. Invisible under the previous wrapper, which ran exclusive fullscreen.

So it is a last resort for a setup with no wrapper at all. Turn it on only if the window really is
smaller than the display mode, and expect the pointer features below to become load-bearing when you
do, the log says which of the two states it is in.

## When it is on: it follows the mode in BOTH directions, from `graphics_setMode`

The 2 fps defect was a **wrong hook site**, not a wrong correction. The fit used to be driven from
`graphics_setResolution 0x46BE3D`, which is **not** the choke point: eight `call rel32` sites reach
`graphics_setMode 0x46BC85` and only one of them (`0x46BF67`) is inside `graphics_setResolution`.
Both mode changes a player actually triggers live in one function, `swmenu_enterMenuMode 0x45F772`,
and it is asymmetric:

```
0045F7A7  call graphics_getWidth
0045F7AC  cmp  eax,[max_menu_Width]
0045F7C8  call graphics_setResolution(640,480)   ENTERING a menu: the mode goes DOWN
0045F7F4  call graphics_setMode(savedIndex)      LEAVING  a menu: the mode goes UP
```

plus the options screen's own apply, which calls `graphics_setMode` directly twice (`0x44162C`,
`0x44163C`). A fit driven from `graphics_setResolution` therefore **heard the mode go down and never
heard it come back up**. The field log shows exactly that.

Three details the new site needs, and all three are handled:

* **The size of the requested mode is read from `g_aRawMode[index]` *before* the call**, using the
  same stride `0x54` and the same `+8` / `+0xC` field offsets `graphics_setMode` itself uses. It has
  to be: the device is rebuilt inside `stdDisplay_setMode` and is created with whatever size the
  window has at that moment.
* **The return value is not the signal.** `0x46BCB6` returns 1 for a mode that was *already* set and
  changes nothing; a failed change leaves the previous mode standing. So the size is **read back**
  afterwards with `stdDisplay_getModeSize` and compared against the last size the window was fitted
  to. That covers all three exits and makes the hook idempotent on a retry.
* **The window is re-checked for a few frames**, because whatever renders for the game owns the same
  window and moves it during its own device rebuild.

## Why moving the window lets the mouse pointer escape

`control_recentreMouse 0x46A115` is handed the `lParam` of a `WM_MOUSEMOVE`, which Windows fills
with **client** coordinates. It compares that against (320, 240) and, when it does not match, warps
the pointer with `SetCursorPos(320, 240)`, which takes **screen** coordinates. That is the engine's
entire mouse confinement: warp back to the middle on every movement. It also never converts between
the two spaces, and it cannot: the import table has no `ClientToScreen`, no `GetClientRect`, no
`ClipCursor` and no `ShowCursor`.

The two spaces agree for exactly one window position, the screen origin. That is where the engine
puts its own window (`CreateWindowExA` at `0x499019` passes X = 0, Y = 0 and style `0x80000000`,
`WS_POPUP`) and where it leaves it, because both of its own `SetWindowPos` calls carry
`SWP_NOMOVE`. **`window_fit.c` is the only code that moves that window, and only when
`FitWindowToMode=1`**, so
this DLL makes the assumption false, and with the key at its new default of `0` it does not make
it false at all. On a second monitor whose origin is at -1920,0 the warp target lands on the
*other* display, the "already centred" test can never be true, and the pointer comes to rest
outside the game window. Two field logs of the same build, one with `monitor at 0,0` and one with
`monitor at -1920,0`, differ in exactly that and in nothing else.

`cursor_anchor.c` converts the engine's own (320, 240) into screen coordinates with
`ClientToScreen` before warping, and leaves the comparison in client space where it belongs. It is
the identity on a window at the screen origin. It adds no `ClipCursor` and no `SetCapture` of its
own; there is nothing global to hold and nothing to release on exit, and it refuses to warp at
all while the foreground window belongs to another process, so Alt-Tab always frees the pointer even
where a graphics wrapper filters `WM_ACTIVATEAPP` before the engine sees it.

The pointer is not meant to be usable in the menus either: the front end draws its own cursor from
the accumulated deltas and the window procedure answers `WM_SETCURSOR` with `SetCursor(NULL)`
unconditionally (`0x4990D3`). So there is no screen on which the OS pointer should be released, and
the anchor makes no exception for one.

## Why the coordinate repair is not enough on its own

A warp only happens when a `WM_MOUSEMOVE` arrives, and one only arrives while the pointer is over
the window, or while a mouse button is down, which is the only case in which a window that holds
the capture still gets mouse input once the pointer is over another thread's window. Cross the edge
with no button held and the loop stops feeding itself: no message, no warp, pointer gone. The
warp target makes it easy, too, because it is client (320, 240), 320 pixels from the left edge,
with a second monitor beginning one pixel further left.

And the capture the engine takes on `WM_ACTIVATEAPP` is not reaching it. In the field log the
graphics wrapper writes `WndProc::Handler Warning: filtering WM_ACTIVATEAPP: 0` and `: 1`, so
`control_captureMouse 0x46A155` and `control_releaseMouse 0x46A17E` never run. The only `SetCapture`
that survives is the one-off at the end of the engine's input startup.

`focus_guard.c` therefore holds the pointer with `ClipCursor` while the game window is the
**foreground window**, and drops the rectangle the instant it is not. The signal is polled once per
frame from the frame hook rather than taken from a message, because a message can be filtered and
`GetForegroundWindow` cannot. The test is against the *window*, not the process, so an error box
the engine puts up releases the pointer too.

## Why Alt-Tab breaks the input, and what part of that is ours

`stdControl_openMouse 0x48DA7C` sets cooperative level 6, `DISCL_NONEXCLUSIVE | DISCL_FOREGROUND`
(`0x48DAEA push 6`). `stdControl_openKeyboard 0x48D9E8` sets none at all: it creates the device,
sets the data format and the buffer size, and there is no `call [edx+0x34]` in it. Windows
unacquires a foreground device by itself when the window goes to the background.

The only function that acquires is `stdControl_setFocus 0x48D719`, and none of its five callers can
be reached by a focus change:

* `0x48D195` / `0x48D1B6`, `stdControl_open` / `stdControl_close`, driven by module messages 3 and 4;
* `0x464A8E` / `0x464AA1`, the Control module's cases for messages **0x12** and **0x13**, which are
  `setFocus(0)` and `setFocus(1)` + `stdControl_resync`. Every one of the 24 `module_broadcast`
  `0x46F3C3` and 14 `module_broadcastDt` `0x46F4A9` call sites pushes its message id as an
  immediate, and the complete set is `{3,4,5,6,7,0x10,0x16,0x17,0x18,0x19}` and
  `{8,9,0x0C,0x0D,0x0E,0x11,0x15}`. **0x12 and 0x13 are sent by nobody.**
* `0x48D656`, inside `0x48D629`, which handles `WM_ACTIVATE` and would call `setFocus`. The
  little-endian dword `29 D6 48 00` **does not occur anywhere in the 829,952-byte image** and no
  `call`/`jmp rel32` targets it, so that handler is never registered and `WM_ACTIVATE` reaches
  nothing.

So the engine authored a suspend/resume pair for its input and shipped without a sender. After a
focus loss the devices are unacquired and nothing in the retail build ever acquires them again;
`stdControl_bAcquired [0x8619C8]` stays 1 while every `GetDeviceState` fails. `ReacquireInputOnFocus`
sends the two messages that are missing, by calling exactly the leaves the engine's own case `0x13`
calls, in the same order.

**What is not ours.** The engine does not pause when it loses focus and never did: the message pump
`0x498BCA` uses `PeekMessageA` with `PM_NOREMOVE` and returns when the queue is empty, and the frame
limiter around it spins on `Sleep(0)` (`0x475BC9`). It also never changes the display mode on a
focus change, `graphics_shutdownMode 0x46C4E8` has one caller, `0x43F5CD`, on the shutdown path.
The device loss and reset an Alt-Tab causes belong entirely to the DirectDraw-to-Direct3D9 wrapper:
in the field log it creates the device with `Windowed: 0`, i.e. exclusive fullscreen, and each
`Resetting device!` takes about half a second. If Alt-Tab is to cost nothing at all, that is a
wrapper setting, not a patch here.

## Timing

`graphics_buildModeList` runs during **graphics** startup. Everything here that has to influence
the list, the 4:3 gate above all, must therefore be patched before that, so the loader
triggers at the host's entry point and not at `DirectInputCreateA`. When the gate is lifted after
the list has been built, the patch reports success and has no effect: the options screen then
offers only the 4:3 modes the DirectDraw layer happens to report. On one machine that was exactly
one entry, 800x600.

`hook_enum_modes` logs every mode it hands to the options screen, unconditionally. That line is
what tells the two cases apart, and it is not behind a verbose flag for that reason.

## Subtitles that scale

Subtitles shrink as the resolution rises: at 640x480 they read correctly, at 4K they are a fifth of
that on screen. It is not a font size problem. The engine lays the whole thing out in a **640x480
box of fixed pixels** and centres it, so everything inside, the 18 pixel row pitch, the 450
baseline, the 580 wrap, stays the same number of pixels however big the display is.

**The change is the mapping, not one constant inside the box.** The box reaches the screen through
two things only: the centring, which asks how big the screen is, and the position scale, which
divides by the same. Tell both that the screen is `k` times smaller than it is and the whole box
lands `k` times bigger with every proportion inside it untouched. That matters beyond tidiness: the
row pitch, the baseline, the wrap and the two nudges live in the one part of that function the
decompilation admits it never reconstructed, so this way none of them has to be understood.

`k` is fitted by **height**, `k = H/480`. By width it would be `W/640`, which on 16:9 makes a 4:3
box taller than the display and pushes the baseline off the bottom. By height the box comes out
1.333xH wide, narrower than the screen, so it pillarboxes as the layout expects. At 640x480 with a
scale of 1 every number is the one the engine already had.

**Ten writes, all or nothing.** Three grow the box, two put it back where it belongs, two keep the
line breaks with it, two let it hang off the top edge once it is taller than the screen, and one
detour moves the backdrop quad to match. Each of those was found by a screenshot of what breaks
without it:

* glyphs alone, and the rows stayed 18 pixels apart while the letters grew, so three lines landed on
  top of each other
* the wrap left behind, and lines broke after two or three words inside a box four times wider than
  they were using, because `font3d_measureChar` hands the glyph scale to the measurement while the
  limit is a bare constant that does not move
* the two offset clamps left in, and at any scale above the fit the box wants its top above the
  screen, the engine floors the offset at zero instead, and a 450 baseline lands in a 390 tall space
  below the bottom edge
* the backdrop left out, and the text scaled while the panel behind it stayed the old size, because
  that quad is built in device pixels and handed straight to the drawing, never touching the
  position scale

The backdrop transform reduces to something simpler than the layout it matches: `x` scales about the
horizontal centre and `y` about the bottom edge, where the text is anchored, and it is the identity
at `k = 1`. Only two calls reach that function and both are the subtitle's own bars.

**Nothing else the font layer draws is affected.** The same layer draws every menu string and HUD
readout; all ten writes are inside the dialogue's own drawing or reached only from it.

## Known limitations

* `MenuKeepsResolution=1` has a visible price: the front end, the pause screens and the loading
  screen become a 640x480 island in the middle of the picture (14.8 % of the area at 1080p), and the
  drawn menu cursor stays inside that box. What it buys is no full D3D9 device rebuild on every menu
  open and close; six of those in 22 seconds appeared in one user log, and the graphics wrapper
  hung inside exactly that rebuild.
* There is a **third** `SetCursorPos(320, 240)` at the end of the engine's input startup, inside a
  large function that is not detoured for one warp that happens once. Input startup runs after
  graphics startup, i.e. after the window has already been moved, so on a secondary monitor the
  pointer can sit outside the window from launch until the hand touches the mouse, the first
  movement then anchors it. A transient, not a standing defect.
* The anchor cannot help while the game is not the foreground application: that is deliberate, and
  it makes Alt-Tab work.
* The confinement is released by a **per-frame** poll, so there is a window of up to one frame
  between the foreground going away and the rectangle being dropped. Windows also drops a clip of
  its own accord when the foreground window changes, but that is not relied on here.
* If the game is killed rather than closed, `DLL_PROCESS_DETACH` does not run and the clip rectangle
  is left to the operating system to reset. This is the one release path this DLL cannot own.
* The monitor rule **changed**. It used to be "the smallest monitor that can still show the mode",
  which moved a 640x480 window from a 2560x1440 primary onto a 1920x1080 secondary and left it
  there; that is what put the window at -1920,0 in the field log. It is now, in order: the monitor
  the window is **already** on whenever that one can show the mode; then an exact size match; then
  the smallest that still fits. If nothing can show the mode the window is resized where it stands
  rather than teleported. `window_fit_choose_monitor()` is pure and `unittests/window_fit.c`
  enumerates the rule, including the regression above.
* The two menu-bolt patterns contain an absolute `.data` address. Under forced ASLR they stop
  resolving and the patch disables itself with a log line. Address-free variants were measured and
  rejected, 6 and 10 hits respectively.
* If a chosen mode fits no connected monitor, the wrapper has to scale. It once hung there. The log
  warns; it does not stop you.

## Fallback behaviour

The window fit has five named branches and the log says which one was taken, including the branch
where it does nothing, because a feature that is silently absent reads exactly like one that is
silently broken:

| what failed | what happens |
|---|---|
| `FitWindowToMode=0` | nothing is hooked and the window is never touched. **Logged as an `info` line, not silence.** |
| `graphics_setMode` does not resolve | the window is not fitted at all and stays wherever it is. The rest of the DLL is unaffected; nothing else uses that site. Warned. |
| the detour cannot be installed | the same, as an error. |
| `g_aRawMode` / `g_numRawModes` did not resolve | the window can only be fitted **after** a mode change and not before it, so whatever renders may still read the old window size while rebuilding its device. Warned once. |
| `stdDisplay_getModeSize` does not resolve | the fit runs from the *requested* mode only, so a mode change the engine **rejects** would leave the window on a size that was never set. Warned. |
| the frame hook cannot be installed | the fit acts once at the mode change and does not follow up while the renderer rebuilds its device. Warned. |

The pointer anchor and the focus guard now also log **whether they are load-bearing**: with
`FitWindowToMode=0` nothing moves the window, so both are insurance and the log says so in those
words. Their behaviour is unchanged either way, and their defaults stay `1`, at window origin
(0,0) the anchor computes the identical warp and the clip rectangle is the whole desktop, so they
cost nothing there and they make an unusual setup survivable.

The pointer anchor has four named branches and the log says which one was taken:

| what failed | what happens |
|---|---|
| `control_recentreMouse` does not resolve or cannot be detoured | no anchor at all; the pointer keeps the engine's behaviour and can leave the window. Warned. |
| the engine's window handle does not resolve | the anchor uses `GetActiveWindow()` instead, which inside the window procedure is the same window whenever the game is the active application. Warned. |
| `control_captureMouse` does not resolve or cannot be detoured | the pointer is still kept inside the window, but after an Alt-Tab back it is put there by the first mouse movement instead of immediately. Warned. |
| the window cannot be asked for its client origin at run time | that one warp falls through to the engine's own, once, with a single warning line, never one per message. |

The focus guard has three named branches and the log says which one was taken:

| what failed | what happens |
|---|---|
| the frame hook cannot be installed | **neither** the confinement nor the re-acquire is armed. A confinement nobody can observe losing the foreground would be held for the rest of the session, which is worse than the defect it fixes, and the re-acquire has the same single source of truth. Warned. |
| `stdControl_setFocus` does not resolve | the confinement still works; the input devices are not re-acquired, i.e. the engine's own behaviour. Warned. |
| `stdControl_resync` does not resolve | the devices are still re-acquired; what queued up while the game was away is not drained, so a key held across the switch can arrive as a fresh press. Warned. |
| `ClipCursor` refuses | the confinement is dropped and the pointer keeps the engine's own behaviour. Warned once, not once per frame. |

## Testing status

**Played, including at 3840x2160.** The mode reaches the game's own options screen, the game runs
in it, and the menus hold the resolution rather than dropping to 640x480 and rebuilding the device.
That last part is the one worth watching, because the 640x480 island it leaves is the visible cost
of it: about 15 per cent of the picture at 1080p and under 4 per cent at 2160p, so the higher the
resolution the more obvious it is.

The window work is newer than that session and has been played, with a result for each mode.

**Played on 2026-09-08**, on a 3840x2160 desktop with `WindowedPresent=1`: all four window shapes,
dragging the window across the middle of the screen, dragging the edges of the resizable one,
maximise and restore, minimise and restore, the pointer release, Alt+Enter, and the close box
ending the game. Around 100 frames a second at 10.8 ms in a 1280x960 window over a 1024x768 render.

What that session also found, and what the code now says instead of what it said then: the surfaces
were never the device's fault, the pointer could not leave the window because the ENGINE re-centres
it on every mouse message rather than because of any clip of ours, and the close box did nothing
because the engine answers `WM_CLOSE` with zero on purpose.

**Not tested:** a second monitor, a display whose scaling is not 100 per cent, and any machine
without the graphics wrapper installed. The last one is the interesting gap: `WindowedFill` has
nothing to correct there and switches itself off with a line saying so, but the shapes themselves
have never been run against real DirectDraw.

**`WindowMode=1` with `WindowedPresent=1` works and is the configuration to use.** Played on a
3840x2160 desktop at that render resolution: correct picture, Alt Tab genuinely free, the graphics
wrapper reporting zero device recreates and staying windowed throughout, and roughly 100 fps against
45 for the exclusive device. Movies play inside the window with `MovieSurface=child`.

**`WindowMode=2` does not work with `WindowedPresent=1` and is refused**, for the structural reason
given in the section above. That refusal is new and has not itself been observed in a log.

**`WindowMode=1` with `WindowedPresent=0` has not been played.** It applies the engine's own style
word and changes only the geometry, so the risk is low, but it is the combination a player gets by
turning on one key and not the other and nobody has run it.

**`WindowedPresent` on a machine with `EnableWindowMode=1` in `dxwrapper.ini` has not been played**
either, and it is known to be wrong: that setting arms the wrapper's own window management, which
strips the frame and recentres the window. The shipped `dxwrapper.ini` has it at 0.

Untested and worth naming: two monitors, Wine and the Steam Deck, a resolution change during play,
and a display with scaling set to anything other than 100 per cent. The per-apply log line prints
the monitor rectangle, `GetSystemMetrics` and the window rectangle read back precisely so that the
last of those can be told apart from a fault when somebody does run it. The running window is about 2054 by 2077 rather than the
desktop-sized popup this module's headers used to describe, and that size is read from the retail
image; it has not been measured in a running game. The pointer confinement log line was
rewritten because of it, and that half has been seen: a launch since carries the new wording, which
describes the running window size rather than claiming the client edge is the desktop edge.

The offline verification below still stands and is what the individual patches rest on.

`/W4 /WX` clean with zero compiler warnings; the DLL links and all six wired unit tests pass,
including the `focus_guard` test that enumerates all 64 input combinations of the release rule and
walks a whole session of Alt-Tabs, a Win key and a minimise asserting that no frame ever ends with
the pointer confined and the foreground gone.

`unittests/window_fit.c` is new and covers the monitor rule, 17 checks, including the -1920,0
regression itself. It is wired into `CMakeLists.txt` and runs with the rest of the suite
under `/W4 /WX` against the built `engine_fixes_common.lib` and all 17 pass.

Offline signature verification passes on both retail builds. `SIG_SET_MODE_ENTRY` resolves
uniquely in **all three** builds including the recompiled `obi.exe` (`0x46BC25` there), and
so does `SIG_MODE_SIZE_ACCESSOR` (`0x4937FA` there), its two absolute `.data` operands are
wildcarded for exactly that reason. `obi.exe` still reports its expected 35 problems, unchanged.

## The menu cursor cage, and why the order of two writes matters

The engine already centres its menus. `g_menuOriginX [0x6CFD58] = (W-640)/2` and
`g_menuOriginY [0x6CFD5C] = (H-480)/2` are written at startup and on every mode change, and both
are **added** by `swwidget_draw` and by `swwidget_hitTest`, 18 references and 16 references, so
what you see and what you can click cannot drift apart. **None of that is touched.**

What is wrong is a different number: inside the window procedure the engine clamps the cursor it
*draws for its own menus* to `[originX, originX+0x25F] by [originY, originY+0x1BF]`, 607x447, i.e.
640 and 480 less the 32-pixel cursor quad. At 1920x1080 that is an island in the middle of the
screen the pointer cannot be moved out of. The cursor coordinates are absolute screen coordinates
while the hit test adds the origin to the **widget**, so widening the clamp needs no coordinate
work at all: a cursor outside the island simply hits nothing, exactly as it does today.

**The install is one write and there is no partial state.** Only the four clamp immediates are
written, to canvas width minus 33 and canvas height minus 33. The block's two origin operands are
read back
first, as proof that the block is the one the listing describes, and are never written.

An earlier version wrote them too, repointing them at zero cells so the clamp became screen
relative rather than canvas relative. That is the fault described above: it let the cursor leave
the region the pause screens can repaint, and it was reported from a 3840x2160 session as the
cursor's blue glow smearing across the border. Removing it removed the smear, the two-step ordering
and the rollback path that ordering needed, all at once. The cage is now computed absolutely from
the canvas size, so writing it twice writes the same four numbers.

**The signature contains the immediates it patches**, which identifies the block as the 640x480
cage in the first place. It therefore cannot resolve a second time: the addresses are cached
at install and the site is never re-resolved. If an earlier generation of this DLL is already in the
process and has already widened the cage, the resolve fails, and the log says *that*, rather than
claiming the engine was not recognised.

The refresh on a resolution change is a once-per-frame poll of the mode size, **not** a detour on
the engine's mode-change broadcast: the loading screen calls `swmenu_setSuppressModeSwitch`, and
`enterMenuMode` then returns *before* that broadcast. The clamp is recomputed absolutely from the
current width and height, never by adding a delta, so repeating it cannot drift.

At 640x480 the computed clamp is bit-identical to the constants the engine ships with. That is
asserted by a unit test and is the reason this may default to on.

**Testing status:** the arithmetic is unit-tested. The patch itself has **not** been
run in the game.
