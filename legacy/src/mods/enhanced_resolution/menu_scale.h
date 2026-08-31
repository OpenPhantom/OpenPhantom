/* menu_scale.h: draw the 640x480 menu canvas at the size the converted artwork was made for.
 *
 * ==============================================================================================
 * What this is for
 *
 * With MenuKeepsResolution=1 the menus keep the display mode instead of dropping it to 640x480,
 * which is what stops a full Direct3D 9 device rebuild on every menu open and close. The price is
 * that the front end, the pause screens and the loading screen become a 640x480 island in the
 * middle of the picture: about 15 per cent of the area at 1080p and under 4 per cent at 2160p, so
 * the higher the resolution the smaller it looks. That price is the subject of issue #31.
 *
 * The engine draws its menu bitmaps through swrle_blit, a run-length blitter that copies one source
 * pixel to one destination pixel. There is no scale term in it, and there is no stretching blit
 * anywhere in the image: all four arms of stdDisplay_blit were disassembled and none takes a
 * destination extent. So the canvas cannot be enlarged by asking the engine to stretch it. What CAN
 * be done is to make the canvas itself bigger and put bigger artwork into it, which is what this
 * does, and why it is only half a feature on its own.
 *
 * ==============================================================================================
 * The four things that have to move together
 *
 * 1. The canvas clip. swrle_blit reads the destination surface's real width and height into two
 *    locals and then overwrites both with 640 and 480 before it clips anything. Those two
 *    immediates are the hard bound: a widget beyond canvas x=640 is dropped before the origin is
 *    even added. They become the scaled canvas.
 *
 * 2. The origin. Both places that compute it do `(g_screenW - 640.0f) / 2.0f`, reading the 640.0f
 *    and 480.0f through operands rather than immediates, so repointing those operands at cells
 *    holding the scaled canvas recentres it with no code patch at all.
 *
 * 3. Text and 3-D widget size. g_menuScale is `640.0f / g_screenW` and already drives every menu
 *    string's glyph size, the base font size and the 27 SW_3D widgets. Its numerator is read
 *    through an operand too, so repointing that at the scaled cell makes it scaled/W, so text
 *    grows with everything else.
 *
 *    This matters more than it looks. g_menuScale is written by a NON-POPPING fst whose value the
 *    very next instruction consumes to produce g_menuTextScale. Patching that instruction, or
 *    hooking after it, would corrupt the second value. Repointing the numerator leaves the
 *    arithmetic exactly as the engine authored it.
 *
 * 4. The widget rectangles. Every widget's rect is authored in 640x480 canvas units, and the origin
 *    is added at each point of use rather than being stored back. The draw path and the hit test
 *    read the SAME rect through the SAME origin, so scaling the rect scales both together, and the
 *    focus outline, the slider grab box and the list box row arithmetic follow for free. That is
 *    the reason this is done to the rect data and not at the blitter: intercepting the draw would
 *    leave every one of those input paths behind, and there is no survivable half of that.
 *
 * ==============================================================================================
 * Why this refuses to install without the cursor cage
 *
 * The drawn menu cursor is caged, by the engine, to [origin, origin+607] by [origin, origin+447].
 * WidenMenuCursorArea makes that cage follow this canvas, and it ships on. With the cage
 * SHUT and the canvas scaled, every widget outside the old 607x447 box becomes unreachable: the
 * pointer simply cannot travel to it. Nothing fails, nothing logs, the buttons are just dead.
 *
 * That is a worse outcome than not scaling at all, and it is reachable without any patch failure,
 * because WidenMenuCursorArea=0 is a documented setting a reader may choose. So this declines
 * rather than install into it.
 *
 * ==============================================================================================
 * What this does NOT do yet
 *
 * * The pause panel writes its own widget rectangles every frame, in canvas units, as part of its
 *   slide animation. Those writes land after this has scaled, so the pause panel is the one screen
 *   that will be wrong. Every other screen goes through a build that happens once.
 * * The SW_3D widgets project about a hard-coded canvas centre of 320 and 240, so with a scaled
 *   canvas they land in the wrong place. All 27 of them are on the pause screens, so this shares
 *   its fate with the point above.
 * * The drawn cursor quad stays 32 pixels, and the cage's margin stays 33.
 * * The loading bar is drawn by hand rather than as widgets, and keeps its own geometry.
 * * Nothing here upscales the artwork. The ratio is READ FROM the artwork instead, see below, so
 *   the two can never disagree about it.
 */
#ifndef MENU_SCALE_H
#define MENU_SCALE_H

#include <stdbool.h>
#include <stdint.h>

/* The authored canvas every menu is laid out in. */
#define MENU_SCALE_CANVAS_WIDTH  640
#define MENU_SCALE_CANVAS_HEIGHT 480

/* 1.0 is off and is the shipped behaviour.
 *
 * The ceiling is not a round number and is not a matter of taste: the run length encoder writes a
 * literal control word as `run & 0xfff` while advancing the output by the full run, so a canvas
 * wider than 4095 pixels corrupts the stream. 4095/640 is 6.398. */
#define MENU_SCALE_MIN_RATIO 1.0f
#define MENU_SCALE_MAX_RATIO (4095.0f / (float)MENU_SCALE_CANVAS_WIDTH)

/* Installs the scale.
 *
 * `configured_ratio` is the MenuScale setting: 0 means "follow the artwork", which is the default
 * and the useful answer; 1.0 is off; anything else is an explicit ratio, which exists for testing.
 *
 * WHY THE ARTWORK DECIDES. The engine blits menu bitmaps one source pixel to one destination pixel,
 * so the size a widget is drawn at is simply the size of its bitmap. If the layout were scaled by
 * one ratio and the artwork resampled at another, they would disagree everywhere: gaps, overlaps,
 * and a canvas the background no longer covers, which leaves stale pixels because nothing repaints
 * them. Rather than ask a reader to keep two numbers in step, this reads the ratio out of the
 * converted artwork itself and uses that. One source of truth. No converted artwork means ratio 1
 * and nothing changes.
 *
 * `cursor_cage_widens` is whether WidenMenuCursorArea is on. The cage is sized from this canvas, so
 * with it off the drawn cursor keeps the engine's 607x447 clamp while the widgets move outside it
 * and become unreachable, which is why this declines rather than install alongside it.
 *
 * Returns true only when every site resolved AND every write landed. A partial install is rolled
 * back: a menu drawn at one scale and hit tested at another is unusable in a way that looks like a
 * game bug rather than a patch failure. */
bool menu_scale_install(float configured_ratio, bool cursor_cage_widens);

/* The ratio in force, or 1.0 when this is not installed. */
float menu_scale_ratio(void);

/* The canvas the menus are drawn on, in pixels: 640x480 when this is not installed. The cursor cage
 * is sized from this, because the canvas is exactly the region the menus can repaint. */
void menu_scale_canvas(int32_t *out_width, int32_t *out_height);

#endif /* MENU_SCALE_H */
