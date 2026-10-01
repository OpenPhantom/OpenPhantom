/* mp_signatures_hud.c: the nine byte patterns a panel is drawn through, and the three cells two
 * of them name.
 *
 * The patterns follow the rules the other three tables follow: an absolute address is never a
 * required byte, it is wildcarded and read back out of the matched operand, and a relative
 * displacement is wildcarded too. Where a pattern reaches past its own prologue it is because the
 * prologue is not distinctive: four of these functions open with the same guard against the same
 * pool pointer, and only the field they finally write tells them apart.
 */
#include "mp_signatures_hud.h"

#include "mp_signatures.h"

#include "common/logging.h"
#include "common/memory.h"
#include "common/signature.h"

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

/* ==============================================================================================
 * The patterns.
 * ============================================================================================ */

/* The filled rectangle, 0x00419660. Its entry is an optimised leaf with no frame pointer, and the
 * first thing it does is load one of its coordinates and subtract the outward pixel snap, whose
 * operand is an address and is therefore wildcarded; the cell it names, [004A81B0], holds
 * -0.9999.
 *
 * Not named in the reconstruction; the evidence is the call graph. Exactly six sites reach it and
 * five lie between 0x00439465 and 0x004396C3, the fade and letterbox module, which draws exactly
 * five filled shapes. The two calls that draw the letterbox bars give the argument order:
 *
 *     00439444  push [ebp+8]           ; argument 6
 *     00439445  push 0xFF000000        ; argument 5, the colour
 *     00439456  push height * 0.1667   ; argument 4
 *     0043945A  push [0086A440]        ; argument 3, the width
 *     00439461  push 0                 ; argument 2
 *     00439463  push 0                 ; argument 1
 *     00439465  call 0x419660
 *     0043946A  add  esp, 0x18         ; six arguments, cdecl
 *
 * so the order is (x0, y0, x1, y1) in screen pixels. The sixth argument is a flag and not a
 * layer: at 0x004197A5 a zero sends the quad to the deferred sorted queue at 0x487C50 and
 * anything else draws it there and then through 0x487260. The engine's own fade passes 1.
 *
 * The tint under a board is drawn by that same fade module, whose procedure at 0x00438CD0 has a
 * jump table at 0x00438E0F covering messages 3 to 0x18 and sends 0x15 to the letterbox draw at
 * 0x00438DAC. It listens for the same message a board listens for, and a node at the head of the
 * registry, which hears a backward broadcast last, draws over it. */
static const uint8_t SIG_MP_HUD_DRAW_QUAD[] = {
    0x81, 0xEC, 0x84, 0x00, 0x00, 0x00,              /* sub esp,0x84             */
    0xD9, 0x84, 0x24, 0x90, 0x00, 0x00, 0x00,        /* fld dword [esp+0x90]     */
    0xD8, 0x25, 0x00, 0x00, 0x00, 0x00               /* fsub the pixel snap      */
};
static const uint8_t MSK_MP_HUD_DRAW_QUAD[] = {
    0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF,
    0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF,
    0xFF, 0xFF, 0x00, 0x00, 0x00, 0x00
};
/* Called, never hulled here. The prologue is the sub esp,0x84 alone, one six byte instruction,
 * declared so the search steps over a branch another module wrote: OpenPhantom's render_guard
 * replaces this function whole on those six bytes. */
#define HUD_DRAW_QUAD_PROLOGUE 6u

/* The string drawer, 0x0046B3C0, `(const char *text, float x, float y)`. Its head is not
 * distinctive, so the pattern reaches into the body; the 0x80C byte frame it builds for its own
 * copy of the string carries most of the weight. It returns at once when the current font at
 * [006D6200] is NULL, and it refuses a string of 0x7FF characters or more without a word:
 * `cmp ecx, 0x7FF / jb / jmp out` at 0x0046B3F4.
 *
 * Its tail hands the string to the canvas multiply at 0x00479370 together with x times the font's
 * +0x38 and y times its +0x3C, the position scale pair, and that is where x becomes a fraction
 * rather than a pixel: the multiply reads the display mode's integer width and height and forms
 * penX = width * x, penY = height * y, glyphScaleX = (width / 640) * [004B7D94] and glyphScaleY =
 * (height / 480) * [004B7D98], where those two cells are written only from the font's +0x28 and
 * +0x2C, the glyph scale pair. So a caller setting the position scale to (1/screenW, 1/screenH)
 * passes pixels, and one setting the glyph scale to (640/screenW, 480/screenH) gets glyphs at the
 * size the font was drawn for. */
static const uint8_t SIG_MP_HUD_DRAW_TEXT[] = {
    0x55, 0x8B, 0xEC,                                /* push ebp; mov ebp,esp    */
    0x81, 0xEC, 0x0C, 0x08, 0x00, 0x00,              /* sub esp,0x80C            */
    0x57,                                            /* push edi                 */
    0xC6, 0x85, 0xF4, 0xF7, 0xFF, 0xFF               /* mov byte [ebp-0x80C],..  */
};

/* The font selector, 0x0046B13B, named by its bounds test against the sixteen slots the module's
 * pool holds. The two jump distances are wildcarded. What a bad slot costs: a slot below 0 or at
 * 0x10 or above, or one whose record at [eax * 0x48 + 0x6D5D88] is empty, stores 0 into the
 * current font at [006D6200], and every setter below and the drawer itself open with a test of
 * that cell and return without doing anything when it is NULL. A wrong slot is not an error, it
 * is silence. */
static const uint8_t SIG_MP_HUD_FONT_SELECT[] = {
    0x55, 0x8B, 0xEC,                                /* push ebp; mov ebp,esp    */
    0x83, 0x7D, 0x08, 0x00,                          /* cmp [ebp+8],0            */
    0x7C, 0x00,                                      /* jl  out                  */
    0x83, 0x7D, 0x08, 0x10,                          /* cmp [ebp+8],0x10         */
    0x7D, 0x00                                       /* jge out                  */
};
static const uint8_t MSK_MP_HUD_FONT_SELECT[] = {
    0xFF, 0xFF, 0xFF,
    0xFF, 0xFF, 0xFF, 0xFF,
    0xFF, 0x00,
    0xFF, 0xFF, 0xFF, 0xFF,
    0xFF, 0x00
};

/* The colour. Its guard against the current font pointer is a shape several other setters in the
 * same module share, so the pattern runs on into the loop that writes all four corner colours;
 * the comparison against four is what nothing else there does. */
static const uint8_t SIG_MP_HUD_FONT_COLOUR[] = {
    0x55, 0x8B, 0xEC,                                /* push ebp; mov ebp,esp    */
    0x51,                                            /* push ecx                 */
    0x83, 0x3D, 0x00, 0x00, 0x00, 0x00, 0x00,        /* cmp [the current font],0 */
    0x75, 0x02,                                      /* jnz +2                   */
    0xEB, 0x00,                                      /* jmp out                  */
    0xC7, 0x45, 0xFC, 0x00, 0x00, 0x00, 0x00,        /* index = 0                */
    0xEB, 0x09,                                      /* jmp to the test          */
    0x8B, 0x45, 0xFC,                                /* mov eax,index            */
    0x83, 0xC0, 0x01,                                /* add eax,1                */
    0x89, 0x45, 0xFC,                                /* mov index,eax            */
    0x83, 0x7D, 0xFC, 0x04                           /* cmp index,4              */
};
static const uint8_t MSK_MP_HUD_FONT_COLOUR[] = {
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

/* The alignment, 0x0046B23C. Three of the values it accepts are turned into three different
 * bits, and the pattern reaches the first of those comparisons; the branch distances are
 * wildcarded. It writes into font+0x1C: value 0 becomes bit 1, value 1 bit 2, value 2 bit 4, and
 * any other value writes nothing at all, so the previous alignment stands. The canvas multiply
 * separates the three bits and xors them at 0x0047939F, which is zero only when all three are
 * clear, so a font with none of them set draws nothing. Bit 1 halves the accumulated width and
 * subtracts it from x, bit 4 subtracts the whole width, and bit 2 takes neither branch: 0
 * centres, 1 starts at x, 2 ends at x. */
static const uint8_t SIG_MP_HUD_FONT_ALIGN[] = {
    0x55, 0x8B, 0xEC, 0x51,
    0x83, 0x3D, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x75, 0x02,
    0xEB, 0x44,
    0x8B, 0x45, 0x08,
    0x89, 0x45, 0xFC,
    0x83, 0x7D, 0xFC, 0x00,
    0x74, 0x2C
};
static const uint8_t MSK_MP_HUD_FONT_ALIGN[] = {
    0xFF, 0xFF, 0xFF, 0xFF,
    0xFF, 0xFF, 0x00, 0x00, 0x00, 0x00, 0xFF,
    0xFF, 0xFF,
    0xFF, 0xFF,
    0xFF, 0xFF, 0xFF,
    0xFF, 0xFF, 0xFF,
    0xFF, 0xFF, 0xFF, 0xFF,
    0xFF, 0xFF
};

/* The two scale setters are the same twenty four bytes of guard around the same pointer and part
 * only at the field they write, +0x28 against +0x38. Either pattern has to reach that far or it
 * matches both and takes whichever comes first in the image. */
static const uint8_t SIG_MP_HUD_GLYPH_SCALE[] = {
    0x55, 0x8B, 0xEC,
    0x83, 0x3D, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x75, 0x02,
    0xEB, 0x17,
    0xA1, 0x00, 0x00, 0x00, 0x00,
    0x8B, 0x4D, 0x08,
    0x89, 0x48, 0x28
};
static const uint8_t MSK_MP_HUD_GLYPH_SCALE[] = {
    0xFF, 0xFF, 0xFF,
    0xFF, 0xFF, 0x00, 0x00, 0x00, 0x00, 0xFF,
    0xFF, 0xFF,
    0xFF, 0xFF,
    0xFF, 0x00, 0x00, 0x00, 0x00,
    0xFF, 0xFF, 0xFF,
    0xFF, 0xFF, 0xFF
};

static const uint8_t SIG_MP_HUD_POS_SCALE[] = {
    0x55, 0x8B, 0xEC,
    0x83, 0x3D, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x75, 0x02,
    0xEB, 0x17,
    0xA1, 0x00, 0x00, 0x00, 0x00,
    0x8B, 0x4D, 0x08,
    0x89, 0x48, 0x38
};
static const uint8_t MSK_MP_HUD_POS_SCALE[] = {
    0xFF, 0xFF, 0xFF,
    0xFF, 0xFF, 0x00, 0x00, 0x00, 0x00, 0xFF,
    0xFF, 0xFF,
    0xFF, 0xFF,
    0xFF, 0x00, 0x00, 0x00, 0x00,
    0xFF, 0xFF, 0xFF,
    0xFF, 0xFF, 0xFF
};

/* The built in font's slot, [0x004B77EC], behind the getter at 0x0046B74F. The function that
 * returns it is three instructions long and that shape occurs all over the image, so the pattern
 * starts five bytes earlier, in the tail of the function before it, and the cell is read out of
 * the load's operand. Nothing calls the function; the cell is read directly, which is better,
 * because the slot has to be read fresh on every frame rather than once at install time: the
 * cell has five references in the whole image, four of them in the font module's own procedure
 * at 0x0046AFA0, which on its build message loads the font named "sysfont" and stores the slot,
 * and on its teardown message stores -1. So the slot is -1 before the engine's font module is
 * built and again after it is torn down. */
static const uint8_t SIG_MP_HUD_SYS_FONT[] = {
    0x83, 0xC4, 0x10,                                /* add esp,0x10             */
    0x5D, 0xC3,                                      /* pop ebp; ret             */
    0x55, 0x8B, 0xEC,                                /* push ebp; mov ebp,esp    */
    0xA1, 0x00, 0x00, 0x00, 0x00,                    /* mov eax,[the slot]       */
    0x5D, 0xC3                                       /* pop ebp; ret             */
};
static const uint8_t MSK_MP_HUD_SYS_FONT[] = {
    0xFF, 0xFF, 0xFF,
    0xFF, 0xFF,
    0xFF, 0xFF, 0xFF,
    0xFF, 0x00, 0x00, 0x00, 0x00,
    0xFF, 0xFF
};
#define OFFSET_SYS_FONT_SLOT 9u

/* The one place that loads both halves of the screen size back to back, in the fade module's own
 * second letterbox bar. Height first, then width:
 *
 *     00439476  A1 38 A4 86 00        mov eax, [0086A438]     ; the height, operand at +1
 *     0043947B  50                    push eax
 *     0043947C  8B 0D 40 A4 86 00     mov ecx, [0086A440]     ; the width, operand at +8
 *     00439482  51                    push ecx
 */
static const uint8_t SIG_MP_HUD_SCREEN_SIZE[] = {
    0xA1, 0x00, 0x00, 0x00, 0x00,                    /* mov eax,[the height]     */
    0x50,                                            /* push eax                 */
    0x8B, 0x0D, 0x00, 0x00, 0x00, 0x00,              /* mov ecx,[the width]      */
    0x51,                                            /* push ecx                 */
    0xD9, 0x05, 0x00, 0x00, 0x00, 0x00               /* fld dword [the height]   */
};
static const uint8_t MSK_MP_HUD_SCREEN_SIZE[] = {
    0xFF, 0x00, 0x00, 0x00, 0x00,
    0xFF,
    0xFF, 0xFF, 0x00, 0x00, 0x00, 0x00,
    0xFF,
    0xFF, 0xFF, 0x00, 0x00, 0x00, 0x00
};
#define OFFSET_SCREEN_HEIGHT 1u
#define OFFSET_SCREEN_WIDTH  8u

/* ==============================================================================================
 * The table.
 * ============================================================================================ */

static signature_t hud_sites[MP_HUD_SITE_COUNT] = {
    SIGNATURE_ENTRY_DETOUR_MASKED("hud_draw_quad", SIG_MP_HUD_DRAW_QUAD, MSK_MP_HUD_DRAW_QUAD,
                                  HUD_DRAW_QUAD_PROLOGUE),
    SIGNATURE_ENTRY_DETOUR("hud_draw_text", SIG_MP_HUD_DRAW_TEXT, 9u),
    SIGNATURE_ENTRY_DETOUR_MASKED("hud_font_select", SIG_MP_HUD_FONT_SELECT,
                                  MSK_MP_HUD_FONT_SELECT, 7u),
    SIGNATURE_ENTRY_DETOUR_MASKED("hud_font_colour", SIG_MP_HUD_FONT_COLOUR,
                                  MSK_MP_HUD_FONT_COLOUR, 11u),
    SIGNATURE_ENTRY_DETOUR_MASKED("hud_font_align", SIG_MP_HUD_FONT_ALIGN,
                                  MSK_MP_HUD_FONT_ALIGN, 11u),
    SIGNATURE_ENTRY_DETOUR_MASKED("hud_glyph_scale", SIG_MP_HUD_GLYPH_SCALE,
                                  MSK_MP_HUD_GLYPH_SCALE, 10u),
    SIGNATURE_ENTRY_DETOUR_MASKED("hud_pos_scale", SIG_MP_HUD_POS_SCALE, MSK_MP_HUD_POS_SCALE, 10u),
    SIGNATURE_ENTRY_MASKED("hud_sys_font", SIG_MP_HUD_SYS_FONT, MSK_MP_HUD_SYS_FONT),
    SIGNATURE_ENTRY_MASKED("hud_screen_size", SIG_MP_HUD_SCREEN_SIZE, MSK_MP_HUD_SCREEN_SIZE)
};

_Static_assert(sizeof(hud_sites) / sizeof(hud_sites[0]) == (size_t)MP_HUD_SITE_COUNT,
               "the hud site table and mp_hud_site_t differ in length");
_Static_assert(sizeof(SIG_MP_HUD_DRAW_QUAD) == sizeof(MSK_MP_HUD_DRAW_QUAD) &&
                   sizeof(SIG_MP_HUD_FONT_SELECT) == sizeof(MSK_MP_HUD_FONT_SELECT) &&
                   sizeof(SIG_MP_HUD_FONT_COLOUR) == sizeof(MSK_MP_HUD_FONT_COLOUR) &&
                   sizeof(SIG_MP_HUD_FONT_ALIGN) == sizeof(MSK_MP_HUD_FONT_ALIGN) &&
                   sizeof(SIG_MP_HUD_GLYPH_SCALE) == sizeof(MSK_MP_HUD_GLYPH_SCALE) &&
                   sizeof(SIG_MP_HUD_POS_SCALE) == sizeof(MSK_MP_HUD_POS_SCALE) &&
                   sizeof(SIG_MP_HUD_SYS_FONT) == sizeof(MSK_MP_HUD_SYS_FONT) &&
                   sizeof(SIG_MP_HUD_SCREEN_SIZE) == sizeof(MSK_MP_HUD_SCREEN_SIZE),
               "a hud pattern and its mask differ in length");

typedef struct mp_signatures_hud_state {
    mp_hud_surface_t surface;
    bool             complete;
    const char      *missing;
} mp_signatures_hud_state_t;

static mp_signatures_hud_state_t hud;

/* ==============================================================================================
 * Resolution.
 * ============================================================================================ */

/* An operand of a matched site, checked to be an address inside the host image. The shared reader
 * is used rather than a raw load because it decides which copy of the bytes to believe when
 * somebody else has written over the site. */
static bool read_cell(mp_hud_site_t site, size_t offset, const char *what, uintptr_t *out)
{
    if (!signature_read_address_operand(&hud_sites[site], offset, out) ||
        !memory_is_inside_image(*out, sizeof(uint32_t))) {
        log_warning("%s is not an address inside the image, so the board has no surface", what);
        if (hud.missing == NULL) {
            hud.missing = what;
        }
        return false;
    }
    return true;
}

size_t mp_signatures_hud_resolve(void)
{
    size_t    resolved;
    size_t    index;
    uintptr_t slot_cell   = 0u;
    uintptr_t width_cell  = 0u;
    uintptr_t height_cell = 0u;

    hud.complete = false;
    hud.missing  = NULL;

    resolved = signature_resolve_table(hud_sites, (size_t)MP_HUD_SITE_COUNT);
    log_info("%u of %u board surface site(s) resolved", (unsigned)resolved,
             (unsigned)MP_HUD_SITE_COUNT);

    for (index = 0; index < (size_t)MP_HUD_SITE_COUNT; ++index) {
        if (hud_sites[index].address == 0u) {
            hud.missing = hud_sites[index].name;
            return resolved;
        }
    }

    if (!read_cell(MP_HUD_SITE_SYS_FONT, OFFSET_SYS_FONT_SLOT, "the built in font's slot",
                   &slot_cell) ||
        !read_cell(MP_HUD_SITE_SCREEN_SIZE, OFFSET_SCREEN_WIDTH, "the screen width",
                   &width_cell) ||
        !read_cell(MP_HUD_SITE_SCREEN_SIZE, OFFSET_SCREEN_HEIGHT, "the screen height",
                   &height_cell)) {
        return resolved;
    }

    hud.surface.quad        = (mp_hud_quad_fn)hud_sites[MP_HUD_SITE_DRAW_QUAD].address;
    hud.surface.text        = (mp_hud_text_fn)hud_sites[MP_HUD_SITE_DRAW_TEXT].address;
    hud.surface.select      = (mp_hud_int_fn)hud_sites[MP_HUD_SITE_FONT_SELECT].address;
    hud.surface.colour      = (mp_hud_colour_fn)hud_sites[MP_HUD_SITE_FONT_COLOUR].address;
    hud.surface.align       = (mp_hud_int_fn)hud_sites[MP_HUD_SITE_FONT_ALIGN].address;
    hud.surface.glyph_scale = (mp_hud_scale_fn)hud_sites[MP_HUD_SITE_FONT_GLYPH_SCALE].address;
    hud.surface.pos_scale   = (mp_hud_scale_fn)hud_sites[MP_HUD_SITE_FONT_POS_SCALE].address;
    hud.surface.font_slot   = (const volatile int32_t *)slot_cell;
    hud.surface.screen_w    = (const volatile float *)width_cell;
    hud.surface.screen_h    = (const volatile float *)height_cell;
    hud.complete            = true;

    log_info("the board draws with the engine's own renderer: rectangles at %08X, text at %08X, "
             "the built in font's slot in %08X, the screen size in %08X and %08X",
             (unsigned)hud_sites[MP_HUD_SITE_DRAW_QUAD].address,
             (unsigned)hud_sites[MP_HUD_SITE_DRAW_TEXT].address,
             (unsigned)slot_cell, (unsigned)width_cell, (unsigned)height_cell);
    return resolved;
}

const mp_hud_surface_t *mp_signatures_hud_surface(void)
{
    return hud.complete ? &hud.surface : NULL;
}

const char *mp_signatures_hud_missing(void)
{
    return hud.missing;
}
