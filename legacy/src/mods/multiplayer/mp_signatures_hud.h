/* mp_signatures_hud.h: the fourth site table, everything a mod needs to put a panel into the
 * frame the engine is already drawing.
 *
 * Seven entry points and two anchors: a filled rectangle, one string, and the five pieces of font
 * state that decide which font the string is in, what colour it is, where it starts and how big
 * it comes out. The anchors carry no code this file calls; their operands name the built in
 * font's slot and the two cells holding the screen size.
 *
 * Why this feature resolves them again. Another mod in this tree draws its own panel through the
 * same nine sites. A feature DLL may not call another feature DLL, so the entry points are found
 * here as well. That is not duplicated behaviour: a byte pattern is data, and two readers of the
 * same engine function are two readers, not a dependency.
 *
 * None of them is declared a detour target, and that is a decision rather than an oversight. The
 * two stage rule buys a site whose head another module has branched over, and it pays for it with
 * the honesty test on the prologue bytes. Four of these seven functions open with the same four
 * byte frame and then an absolute operand that has to be wildcarded, so their prologues would be
 * mostly wildcard and the test would rest on the tail alone. Nothing in this tree writes a branch
 * over a font setter: they are called, not hooked. A one stage unique match is the stronger check
 * here, and every one of the nine matches exactly once in all six shipped images, the recompile
 * included, fifty four unique matches with no exception. The recompile does move seven of the
 * nine by about 0x60, and the patterns still find them because not one of them requires an
 * absolute address as a byte.
 *
 * Three routines in the same module were read and deliberately not used. The string measure at
 * 0x0046B37A is not the width of a string: per character it takes the glyph's rectangle in the
 * 256 wide atlas plus one, not the advance at glyph+0x14 that the drawing pass itself steps by,
 * so a space, which is zero wide with an advance of five, measures as one; and it does not stop
 * at a newline. The range setter at 0x0046B662 is not a depth range, its two words are word wrap
 * margins the heads up display and the frame rate readout never set and would inherit. And the
 * font creator is not called: the pool is sixteen slots of 0x48 bytes and the menu screens take
 * three each, so the built in slot is used and never created.
 *
 * The enumeration is mp_hud_site_t in mp_signatures.h, beside the other three, which also says
 * why a new subject gets a table of its own rather than more rows in the first one.
 */
#ifndef MULTIPLAYER_MP_SIGNATURES_HUD_H
#define MULTIPLAYER_MP_SIGNATURES_HUD_H

#include "mp_signatures.h"

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

/* Every one of these is cdecl. The engine cleans the arguments at the call site of each of them,
 * which is what says so; the letterbox call that hands the rectangle six arguments and then adds
 * 0x18 to the stack is the clearest of the six. */
typedef void(__cdecl *mp_hud_quad_fn)(float x0, float y0, float x1, float y1,
                                      uint32_t argb, int32_t immediate);
typedef void(__cdecl *mp_hud_text_fn)(const char *text, float x, float y);
typedef void(__cdecl *mp_hud_int_fn)(int32_t value);
typedef void(__cdecl *mp_hud_colour_fn)(uint32_t argb);
typedef void(__cdecl *mp_hud_scale_fn)(float sx, float sy);

/* What a drawing caller needs, in one place, so that no other file in this feature holds a cast
 * from an address to a function pointer.
 *
 * The two cells are read on every frame rather than once. The font slot holds -1 until the engine
 * builds its font module and goes back to -1 when the module is torn down, and a select with -1
 * puts the current font at NULL, after which every setter and the drawer itself return without
 * doing anything and without saying so. The screen size is not known until a display mode has
 * been chosen. */
typedef struct mp_hud_surface {
    mp_hud_quad_fn   quad;
    mp_hud_text_fn   text;
    mp_hud_int_fn    select;
    mp_hud_colour_fn colour;
    mp_hud_int_fn    align;
    mp_hud_scale_fn  glyph_scale;
    mp_hud_scale_fn  pos_scale;

    const volatile int32_t *font_slot;
    const volatile float   *screen_w;
    const volatile float   *screen_h;
} mp_hud_surface_t;

/* Resolves the nine sites and reads the three cells out of the two anchors. Returns how many of
 * the nine resolved. Safe to call again; the second call redoes the work and answers the same. */
size_t mp_signatures_hud_resolve(void);

/* The surface, or NULL when anything it is made of is missing. All or nothing on purpose: a panel
 * that could fill rectangles but not select a font would draw a solid block over the game. */
const mp_hud_surface_t *mp_signatures_hud_surface(void);

/* The first site or cell that did not resolve, or NULL when everything did. This is what a report
 * prints when the panel never drew, so that "it is not there" comes with a reason. */
const char *mp_signatures_hud_missing(void);

#endif /* MULTIPLAYER_MP_SIGNATURES_HUD_H */
