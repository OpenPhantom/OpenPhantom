/* overlay_sites.c: every engine entry point the panel draws through, and nothing else.
 *
 * ==============================================================================================
 * THE SITES, AND HOW EACH ONE WAS ESTABLISHED
 *
 * All addresses are from retail WMAIN.EXE, 829,952 bytes, ImageBase 0x400000.
 *
 * THE FILLED SHAPE. 0x00419660 takes four screen coordinates, a packed ARGB and a layer, and is
 * what the game draws its own letterbox bars and its screen tint with:
 *
 *   00419660  81 EC 84 00 00 00        sub esp,0x84        ; no frame pointer, an optimised leaf
 *   00419666  D9 84 24 90 00 00 00     fld dword [esp+0x90]
 *   0041966D  D8 25 B0 81 4A 00        fsub [004A81B0]     ; that cell is -0.9999, an outward snap
 *
 * It is not named in the reconstruction and is marked there as unproven, so the evidence used here
 * is the call graph instead: exactly six call sites reach it, and five of them lie between
 * 0x00439465 and 0x004396C3, which is inside the fade and letterbox module and is precisely the
 * five filled shapes that module draws. One of those call sites cleans its own arguments:
 *
 *   00439465  E8 F6 01 FE FF           call 0x00419660
 *   0043946A  83 C4 18                 add esp,0x18        ; 24 bytes, so cdecl with six arguments
 *
 * That the engine passes it `(u32)alpha << 24` for the letterbox fade is where the alpha comes
 * from: this is a blended shape, not a solid one, which is the whole reason the panel can be read
 * through.
 *
 * THE TEXT. The font layer sits at 0x0046AFA0 to 0x0046B7B0 and keeps sixteen font slots. Three
 * calls are made into it, select, colour and draw, and one cell is read rather than called. The
 * selector proves its own identity from its first bytes:
 *
 *   0046B13B  55 8B EC 83 7D 08 00 7C 15
 *             83 7D 08 10 7D 0F                refuses below 0 and at or above 16
 *
 * Sixteen is the slot count, which is the same number the module's own pool carries. The built in
 * font is loaded by the engine at startup, so nothing here loads or frees a font and there is no
 * asset to ship.
 *
 * ==============================================================================================
 * WHAT WENT WRONG THE FIRST TIME, BECAUSE IT COST A TEST ROUND
 *
 * Every pattern here was first written with its mask inverted. The shared matcher treats a NON ZERO
 * mask byte as "must match" and a zero as a wildcard, and these were written the other way round,
 * which made almost every byte a wildcard. Nothing failed loudly: the patterns matched dozens of
 * places, `signature_find_unique` refused them all for not being unique, and the log said only that
 * they "did not resolve", which reads exactly like an unsupported executable.
 *
 * Two of them were also simply not unique once that was fixed, and one of those is the interesting
 * one: see the note beside the ammunition pattern in cheats_openphantom.c.
 *
 * ==============================================================================================
 * WHY THE PANEL IS DRAWN AND NOT BLITTED
 *
 * Every shape and every string goes through the engine on the frame it appears on. Nothing is
 * cached, no surface is held and no state is left changed behind us: the colour is set before each
 * string and the font slot before each run. A panel that cached a rendered image would have to
 * invalidate it whenever a cheat is switched, a group folds or the search changes, which is every
 * interesting moment, and would still have to be re-blitted every frame.
 * ============================================================================================ */
#include "overlay_sites.h"


#include "common/logging.h"
#include "common/memory.h"
#include "common/patch.h"
#include "common/signature.h"

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

/* --- 0x00419660, the filled shape. The pattern is the entry plus the first load and the multiply
 * against the screen scale, whose operand is masked. */
static const uint8_t SIG_DRAW_QUAD[] = {
    0x81, 0xEC, 0x84, 0x00, 0x00, 0x00,              /* sub esp,0x84             */
    0xD9, 0x84, 0x24, 0x90, 0x00, 0x00, 0x00,        /* fld dword [esp+0x90]     */
    0xD8, 0x25, 0x00, 0x00, 0x00, 0x00               /* fsub the pixel snap      */
};
static const uint8_t MSK_DRAW_QUAD[] = {
    0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF,
    0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF,
    0xFF, 0xFF, 0x00, 0x00, 0x00, 0x00
};
_Static_assert(sizeof(SIG_DRAW_QUAD) == sizeof(MSK_DRAW_QUAD),
               "the draw quad pattern and its mask are different lengths");

/* --- 0x0046B754, the built in font's slot. The whole function is `mov eax,[cell]; ret`, which is
 * far too common a shape to find on its own: that pattern alone matches twenty five places.
 *
 * So this is not used to CALL anything. The pattern starts five bytes earlier, in the tail of the
 * function before it, which is what makes it unique, and the cell's address is read out of the
 * operand and then read directly. That is better than a call would have been anyway: the slot is
 * fetched fresh wherever it is needed instead of once at install time.
 *
 *   0046B74F  83 C4 10 5D C3           the previous function ending
 *   0046B754  55 8B EC                 push ebp; mov ebp,esp
 *   0046B757  A1 EC 77 4B 00           mov eax,[004B77EC], the slot
 *   0046B75C  5D C3                    pop ebp; ret                                              */
static const uint8_t SIG_SYS_FONT[] = {
    0x83, 0xC4, 0x10,                                /* add esp,0x10             */
    0x5D, 0xC3,                                      /* pop ebp; ret             */
    0x55, 0x8B, 0xEC,                                /* push ebp; mov ebp,esp    */
    0xA1, 0x00, 0x00, 0x00, 0x00,                    /* mov eax,[the slot]       */
    0x5D, 0xC3                                       /* pop ebp; ret             */
};
static const uint8_t MSK_SYS_FONT[] = {
    0xFF, 0xFF, 0xFF,
    0xFF, 0xFF,
    0xFF, 0xFF, 0xFF,
    0xFF, 0x00, 0x00, 0x00, 0x00,
    0xFF, 0xFF
};
_Static_assert(sizeof(SIG_SYS_FONT) == sizeof(MSK_SYS_FONT),
               "the sys font pattern and its mask are different lengths");
#define OFFSET_SYS_FONT_CELL 9u

/* --- 0x0046B13B. The bounds test against the sixteen slots is what names it. */
static const uint8_t SIG_SELECT[] = {
    0x55, 0x8B, 0xEC,                                /* push ebp; mov ebp,esp    */
    0x83, 0x7D, 0x08, 0x00,                          /* cmp [ebp+8],0            */
    0x7C, 0x00,                                      /* jl  out                  */
    0x83, 0x7D, 0x08, 0x10,                          /* cmp [ebp+8],0x10         */
    0x7D, 0x00                                       /* jge out                  */
};
static const uint8_t MSK_SELECT[] = {
    0xFF, 0xFF, 0xFF,
    0xFF, 0xFF, 0xFF, 0xFF,
    0xFF, 0x00,
    0xFF, 0xFF, 0xFF, 0xFF,
    0xFF, 0x00
};
_Static_assert(sizeof(SIG_SELECT) == sizeof(MSK_SELECT),
               "the select pattern and its mask are different lengths");

/* --- 0x0046B179, the colour. It tests the module's own pool pointer before writing, and the head
 * of that is a shape five other functions share, so the pattern runs on into the loop
 * that writes all four corner colours, which nothing else does:
 *
 *   0046B188  C7 45 FC 00 00 00 00     the index, cleared
 *   0046B18F  EB 09                    into the test
 *   0046B191  8B 45 FC 83 C0 01 89 45 FC   ++index
 *   0046B19A  83 7D FC 04              cmp index,4     <- four corners, and that is the tell */
static const uint8_t SIG_SET_COLOUR[] = {
    0x55, 0x8B, 0xEC,                                /* push ebp; mov ebp,esp    */
    0x51,                                            /* push ecx                 */
    0x83, 0x3D, 0x00, 0x00, 0x00, 0x00, 0x00,        /* cmp [the pool],0         */
    0x75, 0x02,                                      /* jnz +2                   */
    0xEB, 0x00,                                      /* jmp out                  */
    0xC7, 0x45, 0xFC, 0x00, 0x00, 0x00, 0x00,        /* index = 0                */
    0xEB, 0x09,                                      /* jmp to the test          */
    0x8B, 0x45, 0xFC,                                /* mov eax,index            */
    0x83, 0xC0, 0x01,                                /* add eax,1                */
    0x89, 0x45, 0xFC,                                /* mov index,eax            */
    0x83, 0x7D, 0xFC, 0x04                           /* cmp index,4              */
};
static const uint8_t MSK_SET_COLOUR[] = {
    0xFF, 0xFF, 0xFF,
    0xFF,
    0xFF, 0xFF, 0x00, 0x00, 0x00, 0x00, 0xFF,
    0xFF, 0xFF,
    0xFF, 0x00,
    0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF,
    0xFF, 0xFF,
    0xFF, 0xFF, 0xFF,
    0xFF, 0xFF, 0xFF,
    0xFF, 0xFF, 0xFF,
    0xFF, 0xFF, 0xFF, 0xFF
};
_Static_assert(sizeof(SIG_SET_COLOUR) == sizeof(MSK_SET_COLOUR),
               "the set colour pattern and its mask are different lengths");

/* --- 0x0046B3C0, the drawer. Its head is not distinctive on its own, so the pattern reaches into
 * the body; the 0x80C byte frame it builds carries most of that weight by itself. */
static const uint8_t SIG_DRAW_TEXT[] = {
    0x55, 0x8B, 0xEC,                                /* push ebp; mov ebp,esp    */
    0x81, 0xEC, 0x0C, 0x08, 0x00, 0x00,              /* sub esp,0x80C            */
    0x57,                                            /* push edi                 */
    0xC6, 0x85, 0xF4, 0xF7, 0xFF, 0xFF               /* mov byte [ebp-0x80C],..  */
};
static const uint8_t MSK_DRAW_TEXT[] = {
    0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF,
    0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF
};
_Static_assert(sizeof(SIG_DRAW_TEXT) == sizeof(MSK_DRAW_TEXT),
               "the draw text pattern and its mask are different lengths");

/* --- 0x0046B293 and 0x0046B2BA, the two scales. Text is not drawn in pixels: the font layer keeps
 * a position scale and a glyph scale, and the engine sets both before every string of its own,
 * 1/width and 1/height for the first and 640/width and 480/height for the second. Setting neither
 * draws at whatever the last user of the font left behind, which is why the panel first came out
 * enormous and off the screen.
 *
 * The two setters are identical for their first twenty four bytes, both being the same guard around
 * the same pool. They part at the field they write, +0x28 against +0x38, so each pattern has to
 * reach that far or it matches both and takes whichever comes first. */
static const uint8_t SIG_GLYPH_SCALE[] = {
    0x55, 0x8B, 0xEC,
    0x83, 0x3D, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x75, 0x02,
    0xEB, 0x17,
    0xA1, 0x00, 0x00, 0x00, 0x00,
    0x8B, 0x4D, 0x08,
    0x89, 0x48, 0x28
};
static const uint8_t MSK_GLYPH_SCALE[] = {
    0xFF, 0xFF, 0xFF,
    0xFF, 0xFF, 0x00, 0x00, 0x00, 0x00, 0xFF,
    0xFF, 0xFF,
    0xFF, 0xFF,
    0xFF, 0x00, 0x00, 0x00, 0x00,
    0xFF, 0xFF, 0xFF,
    0xFF, 0xFF, 0xFF
};
_Static_assert(sizeof(SIG_GLYPH_SCALE) == sizeof(MSK_GLYPH_SCALE),
               "the glyph scale pattern and its mask are different lengths");

static const uint8_t SIG_POS_SCALE[] = {
    0x55, 0x8B, 0xEC,
    0x83, 0x3D, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x75, 0x02,
    0xEB, 0x17,
    0xA1, 0x00, 0x00, 0x00, 0x00,
    0x8B, 0x4D, 0x08,
    0x89, 0x48, 0x38
};
static const uint8_t MSK_POS_SCALE[] = {
    0xFF, 0xFF, 0xFF,
    0xFF, 0xFF, 0x00, 0x00, 0x00, 0x00, 0xFF,
    0xFF, 0xFF,
    0xFF, 0xFF,
    0xFF, 0x00, 0x00, 0x00, 0x00,
    0xFF, 0xFF, 0xFF,
    0xFF, 0xFF, 0xFF
};
_Static_assert(sizeof(SIG_POS_SCALE) == sizeof(MSK_POS_SCALE),
               "the position scale pattern and its mask are different lengths");

/* --- 0x00439476, the one place that loads both halves of the screen size back to back. */
static const uint8_t SIG_SCREEN_SIZE[] = {
    0xA1, 0x00, 0x00, 0x00, 0x00,
    0x50,
    0x8B, 0x0D, 0x00, 0x00, 0x00, 0x00,
    0x51,
    0xD9, 0x05, 0x00, 0x00, 0x00, 0x00
};
static const uint8_t MSK_SCREEN_SIZE[] = {
    0xFF, 0x00, 0x00, 0x00, 0x00,
    0xFF,
    0xFF, 0xFF, 0x00, 0x00, 0x00, 0x00,
    0xFF,
    0xFF, 0xFF, 0x00, 0x00, 0x00, 0x00
};
_Static_assert(sizeof(SIG_SCREEN_SIZE) == sizeof(MSK_SCREEN_SIZE),
               "the screen size pattern and its mask are different lengths");
#define OFFSET_SCREEN_HEIGHT 1u
#define OFFSET_SCREEN_WIDTH  8u

/* --- 0x0046B23C, the alignment. The font layer remembers it like it remembers a colour, and
 * nothing puts it back, so a panel that set neither drew with whatever the last user asked for. */
static const uint8_t SIG_SET_ALIGN[] = {
    0x55, 0x8B, 0xEC, 0x51,
    0x83, 0x3D, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x75, 0x02,
    0xEB, 0x44,
    0x8B, 0x45, 0x08,
    0x89, 0x45, 0xFC,
    0x83, 0x7D, 0xFC, 0x00,
    0x74, 0x2C
};
static const uint8_t MSK_SET_ALIGN[] = {
    0xFF, 0xFF, 0xFF, 0xFF,
    0xFF, 0xFF, 0x00, 0x00, 0x00, 0x00, 0xFF,
    0xFF, 0xFF,
    0xFF, 0xFF,
    0xFF, 0xFF, 0xFF,
    0xFF, 0xFF, 0xFF,
    0xFF, 0xFF, 0xFF, 0xFF,
    0xFF, 0xFF
};
_Static_assert(sizeof(SIG_SET_ALIGN) == sizeof(MSK_SET_ALIGN),
               "the set align pattern and its mask are different lengths");

/* --- 0x0046B2FC and 0x0046B37A, the font's own metrics. Every band is a multiple of the height of
 * a capital, asked for once per painted frame, because no fixed set of numbers suits a font whose
 * size follows the display. */
static const uint8_t SIG_MEASURE_CHAR[] = {
    0x55, 0x8B, 0xEC, 0x83, 0xEC, 0x0C,
    0x83, 0x3D, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x75, 0x04,
    0x33, 0xC0,
    0xEB, 0x67,
    0xA1, 0x00, 0x00, 0x00, 0x00,
    0x8B, 0x48, 0x2C
};
static const uint8_t MSK_MEASURE_CHAR[] = {
    0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF,
    0xFF, 0xFF, 0x00, 0x00, 0x00, 0x00, 0xFF,
    0xFF, 0xFF,
    0xFF, 0xFF,
    0xFF, 0xFF,
    0xFF, 0x00, 0x00, 0x00, 0x00,
    0xFF, 0xFF, 0xFF
};
_Static_assert(sizeof(SIG_MEASURE_CHAR) == sizeof(MSK_MEASURE_CHAR),
               "the measure char pattern and its mask are different lengths");

static const uint8_t SIG_MEASURE_STRING[] = {
    0x55, 0x8B, 0xEC, 0x83, 0xEC, 0x08,
    0xC7, 0x45, 0xF8, 0x00, 0x00, 0x00, 0x00,
    0x8B, 0x45, 0x08,
    0x0F, 0xBE, 0x08,
    0x85, 0xC9,
    0x74, 0x28
};
static const uint8_t MSK_MEASURE_STRING[] = {
    0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF,
    0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF,
    0xFF, 0xFF, 0xFF,
    0xFF, 0xFF, 0xFF,
    0xFF, 0xFF,
    0xFF, 0xFF
};
_Static_assert(sizeof(SIG_MEASURE_STRING) == sizeof(MSK_MEASURE_STRING),
               "the measure string pattern and its mask are different lengths");

/* --- 0x0045FD01, the game's own pointer. Its four lines carry both the texture the pointer is made
 * of and the sprite drawer that puts it on screen, so the panel shows the arrow the menus show
 * rather than something built out of rectangles. The reconstruction marks that cell unproven and
 * reads it as a two dimensional draw context rather than a texture; what is certain is that the
 * drawer is handed it and draws the pointer with it, which is all this needs. The function
 * itself is NOT called: it would read
 * the position out of the engine's cursor cells, and the panel has its own. */
static const uint8_t SIG_DRAW_CURSOR[] = {
    0x55, 0x8B, 0xEC, 0x83, 0xEC, 0x10,
    0x8D, 0x45, 0xF8, 0x50,
    0x8D, 0x4D, 0xFC, 0x51,
    0xE8, 0x00, 0x00, 0x00, 0x00,
    0x83, 0xC4, 0x08,
    0x68, 0x00, 0x00, 0x80, 0x3F,
    0x68, 0xFF, 0xFF, 0xFF, 0xF0,
    /* The two operands this file reads are at the far end, so the pattern has to reach them.
     * Verifying thirty two bytes and then reading at eighty is trusting bytes nobody looked at,
     * and one of the executables here is a recompile in which most of the code differs. */
    0x8B, 0x55, 0xF8, 0x83, 0xC2, 0x20, 0x89, 0x55, 0xF4, 0xDB, 0x45, 0xF4, 0x51, 0xD9, 0x1C,
    0x24, 0xDB, 0x45, 0xF8, 0x51, 0xD9, 0x1C, 0x24,
    0x8B, 0x45, 0xFC, 0x83, 0xC0, 0x20, 0x89, 0x45, 0xF0, 0xDB, 0x45, 0xF0, 0x51, 0xD9, 0x1C,
    0x24, 0xDB, 0x45, 0xFC, 0x51, 0xD9, 0x1C, 0x24,
    0x8B, 0x0D, 0x00, 0x00, 0x00, 0x00,              /* mov ecx,[the cursor texture] */
    0x51,
    0xE8, 0x00, 0x00, 0x00, 0x00,                    /* call the sprite drawer       */
    0x83, 0xC4, 0x1C                                 /* seven arguments, cdecl       */
};
static const uint8_t MSK_DRAW_CURSOR[] = {
    0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF,
    0xFF, 0xFF, 0xFF, 0xFF,
    0xFF, 0xFF, 0xFF, 0xFF,
    0xFF, 0x00, 0x00, 0x00, 0x00,
    0xFF, 0xFF, 0xFF,
    0xFF, 0xFF, 0xFF, 0xFF, 0xFF,
    0xFF, 0xFF, 0xFF, 0xFF, 0xFF,
    0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF,
    0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF,
    0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF,
    0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF,
    0xFF, 0xFF, 0x00, 0x00, 0x00, 0x00,
    0xFF,
    0xFF, 0x00, 0x00, 0x00, 0x00,
    0xFF, 0xFF, 0xFF
};
_Static_assert(sizeof(SIG_DRAW_CURSOR) == sizeof(MSK_DRAW_CURSOR),
               "the draw cursor pattern and its mask are different lengths");
#define OFFSET_CURSOR_TEXTURE 0x50u
#define OFFSET_CURSOR_CALL    0x55u

overlay_draw_state_t draw_state;

static bool resolve_one(const uint8_t *bytes, const uint8_t *mask, size_t size,
                        const char *what, uintptr_t *out)
{
    uintptr_t site = signature_find_unique(bytes, mask, size);

    if (site == 0) {
        log_warning("%s did not resolve, so the overlay cannot draw and stays closed", what);
        return false;
    }
    *out = site;
    return true;
}

bool overlay_draw_resolve(void)
{
    uintptr_t quad = 0;
    uintptr_t sys_font = 0;
    uintptr_t select = 0;
    uintptr_t colour = 0;
    uintptr_t text = 0;
    uintptr_t glyph = 0;
    uintptr_t pos = 0;
    uintptr_t screen = 0;
    uintptr_t align = 0;
    uintptr_t mchar = 0;
    uintptr_t mstring = 0;
    uintptr_t cursor = 0;
    uintptr_t sprite = 0;
    uint32_t  texture_cell = 0;
    uint32_t  font_cell = 0;
    uint32_t  width_cell = 0;
    uint32_t  height_cell = 0;

    if (draw_state.resolved) {
        return true;
    }

    if (!resolve_one(SIG_DRAW_QUAD, MSK_DRAW_QUAD, sizeof SIG_DRAW_QUAD,
                     "the filled shape drawer", &quad) ||
        !resolve_one(SIG_SYS_FONT, MSK_SYS_FONT, sizeof SIG_SYS_FONT,
                     "the built in font", &sys_font) ||
        !resolve_one(SIG_SELECT, MSK_SELECT, sizeof SIG_SELECT,
                     "the font selector", &select) ||
        !resolve_one(SIG_SET_COLOUR, MSK_SET_COLOUR, sizeof SIG_SET_COLOUR,
                     "the text colour setter", &colour) ||
        !resolve_one(SIG_DRAW_TEXT, MSK_DRAW_TEXT, sizeof SIG_DRAW_TEXT,
                     "the text drawer", &text) ||
        !resolve_one(SIG_GLYPH_SCALE, MSK_GLYPH_SCALE, sizeof SIG_GLYPH_SCALE,
                     "the glyph scale setter", &glyph) ||
        !resolve_one(SIG_POS_SCALE, MSK_POS_SCALE, sizeof SIG_POS_SCALE,
                     "the position scale setter", &pos) ||
        !resolve_one(SIG_SCREEN_SIZE, MSK_SCREEN_SIZE, sizeof SIG_SCREEN_SIZE,
                     "the screen size", &screen) ||
        !resolve_one(SIG_SET_ALIGN, MSK_SET_ALIGN, sizeof SIG_SET_ALIGN,
                     "the text alignment", &align) ||
        !resolve_one(SIG_MEASURE_CHAR, MSK_MEASURE_CHAR, sizeof SIG_MEASURE_CHAR,
                     "the character metrics", &mchar) ||
        !resolve_one(SIG_MEASURE_STRING, MSK_MEASURE_STRING, sizeof SIG_MEASURE_STRING,
                     "the string width", &mstring)) {
        return false;
    }
    draw_state.set_align = (set_align_fn_t)align;

    /* The pointer is a nicety, not a dependency: a build whose cursor could not be found still gets
     * a panel, with the plain cross. So this one resolves on its own and never fails the
     * rest. */
    cursor = signature_find_unique(SIG_DRAW_CURSOR, MSK_DRAW_CURSOR, sizeof SIG_DRAW_CURSOR);
    if (cursor != 0 &&
        memory_read_u32(cursor + OFFSET_CURSOR_TEXTURE, &texture_cell) &&
        memory_is_inside_image(texture_cell, sizeof(void *)) &&
        patch_read_call_target(cursor + OFFSET_CURSOR_CALL, &sprite)) {
        draw_state.cursor_texture = (void *const volatile *)(uintptr_t)texture_cell;
        draw_state.draw_sprite = (draw_sprite_fn_t)sprite;
        log_info("the panel uses the game's own mouse pointer, texture at %08X drawn through %08X",
                 (unsigned)texture_cell, (unsigned)sprite);
    } else {
        log_warning("the game's own mouse pointer was not found, so the panel draws a plain cross");
    }
    draw_state.measure_char = (measure_char_fn_t)mchar;
    draw_state.measure_string = (measure_string_fn_t)mstring;

    if (!memory_read_u32(screen + OFFSET_SCREEN_WIDTH, &width_cell) ||
        !memory_read_u32(screen + OFFSET_SCREEN_HEIGHT, &height_cell) ||
        !memory_is_inside_image(width_cell, sizeof(float)) ||
        !memory_is_inside_image(height_cell, sizeof(float))) {
        log_warning("the screen size cells are not both inside the image, refused");
        return false;
    }
    draw_state.screen_w = (const volatile float *)(uintptr_t)width_cell;
    draw_state.screen_h = (const volatile float *)(uintptr_t)height_cell;
    draw_state.glyph_scale = (scale_fn_t)glyph;
    draw_state.pos_scale = (scale_fn_t)pos;

    if (!memory_read_u32(sys_font + OFFSET_SYS_FONT_CELL, &font_cell) ||
        !memory_is_inside_image(font_cell, sizeof(int32_t))) {
        log_warning("the built in font's cell at %08X is not an address inside the image, refused",
                    (unsigned)(sys_font + OFFSET_SYS_FONT_CELL));
        return false;
    }

    draw_state.quad = (draw_quad_fn_t)quad;
    draw_state.select = (select_fn_t)select;
    draw_state.colour = (set_colour_fn_t)colour;
    draw_state.text = (draw_text_fn_t)text;
    draw_state.font_slot = (const volatile int32_t *)(uintptr_t)font_cell;

    /* The engine loads this font during its own startup, and install runs at the entry point, which
     * is earlier. So the cell may still be empty right now and that is not a failure: it is read
     * again on every paint, and the panel cannot be opened before the game is running anyway. */
    draw_state.resolved = true;
    log_info("the overlay draws with the engine's own renderer: shapes at %08X, text at %08X, the "
             "built in font's slot in %08X. Nothing is blitted and no surface is held.",
             (unsigned)quad, (unsigned)text, (unsigned)font_cell);
    return true;
}

/* ============================================================================================ */
/* The layout. One function decides where a row sits, and both the paint and the pointer use it, so
 * the two can never disagree. */

/* The height of a capital in whatever font the engine actually has. Everything the panel is made of
 * is a multiple of this, so it is asked once per painted frame rather than assumed. */
