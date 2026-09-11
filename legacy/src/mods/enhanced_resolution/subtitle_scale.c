/* subtitle_scale.c: see subtitle_scale.h. */
#include "subtitle_scale.h"

#include "common/detour.h"
#include "common/frame_hook.h"
#include "common/ini.h"
#include "common/logging.h"
#include "common/memory.h"
#include "common/patch.h"
#include "common/signature.h"

#include <stdint.h>

#define RESOLUTION_SECTION "enhanced_resolution"
#define SUBTITLE_SCALE_KEY "SubtitleScale"

/* The screen the dialogue layout is authored on, and the units every constant inside it is measured
 * in: rows 18 apart, the group anchored at 450, the wrap at 580, the box itself 640 by 480. */
#define AUTHORED_WIDTH  640.0f
#define AUTHORED_HEIGHT 480.0f

/* The engine's live display size. These are the integers its own two getters return, and the floats
 * the layout divides by are converted from the same pair. */
#define SCREEN_WIDTH_INT  0x006D6314u
#define SCREEN_HEIGHT_INT 0x006D632Cu

/* --- the posScale pair inside DLG_DrawLine, at 0x00431545 -------------------------------------- *
 *   D9 05 D0 83 4A 00     fld  dword [1.0]
 *   D8 35 38 A4 86 00     fdiv dword [screenHeight]      the Y scale, pushed first
 *   51 D9 1C 24           push ecx / fstp [esp]
 *   D9 05 D0 83 4A 00     fld  dword [1.0]
 *   D8 35 40 A4 86 00     fdiv dword [screenWidth]       the X scale
 *   51 D9 1C 24           push ecx / fstp [esp]
 *
 * Twelve bytes are already unique and thirty two are taken, through both halves, so the pattern
 * cannot match a shape the engine repeats elsewhere with a different pair of cells. */
static const uint8_t SIG_POS_SCALE[] = {
    0xD9, 0x05, 0xD0, 0x83, 0x4A, 0x00, 0xD8, 0x35, 0x38, 0xA4, 0x86, 0x00,
    0x51, 0xD9, 0x1C, 0x24,
    0xD9, 0x05, 0xD0, 0x83, 0x4A, 0x00, 0xD8, 0x35, 0x40, 0xA4, 0x86, 0x00,
    0x51, 0xD9, 0x1C, 0x24
};
#define POS_SCALE_HEIGHT_OPERAND 8u
#define POS_SCALE_WIDTH_OPERAND  24u

/* --- the glyph scale, sixty five bytes later --------------------------------------------------- *
 *   D9 05 E4 83 4A 00     fld  dword [640.0]
 *   D8 35 40 A4 86 00     fdiv dword [screenWidth]
 *   D9 5D FC              fstp dword [ebp-4]             handed to both axes
 *
 * Nine bytes are unique; fifteen are taken, through the store, because the store is what proves
 * this is the glyph scale rather than another division of the same constant. */
static const uint8_t SIG_GLYPH_SCALE[] = {
    0xD9, 0x05, 0xE4, 0x83, 0x4A, 0x00,
    0xD8, 0x35, 0x40, 0xA4, 0x86, 0x00,
    0xD9, 0x5D, 0xFC
};
#define GLYPH_SCALE_WIDTH_OPERAND 8u

/* --- the two centring calls, at 0x0043177A and 0x0043179F ------------------------------------- *
 *   E8 <rel>   call screenWidth()    then (eax - 640) / 2, floored at zero
 *   E8 <rel>   call screenHeight()   then  eax - 480,      floored at zero
 *
 * Located from the glyph scale rather than by a pattern of their own. A bare call is five bytes of
 * which four are an offset, so there is nothing in one to match on; the distance from a site that
 * IS unique is the honest anchor, and the opcode is checked before either is touched. */
#define CENTRE_WIDTH_CALL_FROM_GLYPH  0x1F4u   /* 0x0043177A - 0x00431586 */
#define CENTRE_HEIGHT_CALL_FROM_GLYPH 0x219u   /* 0x0043179F - 0x00431586 */

/* --- the line wrap, compared twice against 580.0 at 0x004A83E8 ------------------------------- *
 *   D8 1D E8 83 4A 00     fcomp dword [580.0]     once before the loop and once inside it
 *
 * This has to move with the BOX, and finding out why cost a screenshot. font3d_measureChar hands
 * the glyph scale to the measurement, so the width it answers follows that scale; the wrap limit
 * is a bare constant and does not. Growing the box therefore made every character measure larger
 * against an unchanged limit, and lines broke after two or three words inside a box four times
 * wider than they were using.
 *
 * The factor is k, the same one everything else here uses, so at 640x480 the limit is the 580 the
 * engine shipped and the authored screen is unchanged. Both comparisons read the one constant, so
 * both are repointed at the one cell. */
#define WRAP_FIRST_OPERAND_FROM_GLYPH  0x5Du   /* 0x004315E3 - 0x00431586 */
#define WRAP_SECOND_OPERAND_FROM_GLYPH 0xAAu   /* 0x00431630 - 0x00431586 */
#define AUTHORED_WRAP 580.0f

/* --- the two offset clamps, at 0x00431793 and 0x004317B6 -------------------------------------- *
 *   7D 0A                 jge over `mov [offset], 0`
 *
 * BOTH have to go; this is the wall that made a scale above the fit produce an empty box.
 * The engine floors each centring offset at zero, which is right for a box that is always at
 * most the size of the screen. Ours can be larger: the moment the box is taller than the
 * display, its top belongs ABOVE the top edge, and a floor of zero pins it there instead and
 * carries the baseline off the bottom. At 1.23x the vertical offset wants to be -90 and was
 * being read as 0, which put a 450 baseline into a 390 tall space.
 *
 * A jge becomes a jmp, so the zeroing is skipped rather than removed, one byte each and no
 * instruction lengths change. At a scale of 1 or below both offsets are positive anyway and
 * these two writes change nothing at all. */
#define CLAMP_X_FROM_GLYPH 0x20Du   /* 0x00431793 - 0x00431586 */
#define CLAMP_Y_FROM_GLYPH 0x230u   /* 0x004317B6 - 0x00431586 */
#define JGE_REL8 0x7Du
#define JMP_REL8 0xEBu

/* --- dialog_drawBar 0x00430AC2 ---------------------------------------------------------------- *
 *   55 8B EC              push ebp / mov ebp,esp
 *   81 EC B0 00 00 00     sub esp,0xB0
 *   57                    push edi
 *
 * Twelve bytes are unique, twenty are taken, and the nine byte prologue is an instruction
 * boundary holding nothing relative.
 *
 * The backdrop is NOT drawn through the font layer, so nine writes made the text right and left
 * the panel behind it the old size. The quad is built in device pixels by the caller and
 * handed straight here, so posScale never touches it. Only two calls reach this function and both
 * are the subtitle's own bars, so a detour on it cannot affect anything else.
 *
 * The transform falls out to something simpler than the layout it is matching. The caller builds
 * x from (W-640)/2 + boxX and y from (H-480) + boxY, so scaling the box by k is
 *
 *     x' = W/2 + k*(x - W/2)      scaled about the horizontal CENTRE
 *     y' = H   + k*(y - H)        scaled about the BOTTOM edge
 *
 * The bottom edge is where the text is anchored, and the transform is the identity at k = 1. */
static const uint8_t SIG_DRAW_BAR[] = {
    0x55, 0x8B, 0xEC, 0x81, 0xEC, 0xB0, 0x00, 0x00, 0x00, 0x57, 0x68, 0x00,
    0x00, 0x00, 0x3F, 0x8B, 0x45, 0x18, 0x50, 0xE8
};
#define DRAW_BAR_PROLOGUE 9u

typedef void(__cdecl *draw_bar_fn_t)(float x0, float y0, float x1, float y1, uint32_t argb);

/* ==============================================================================================
 * The change is the MAPPING, not one constant inside the box
 *
 * Everything the layout does is in box pixels, and the box reaches the screen through two things
 * only: the centring, which asks how big the screen is, and posScale, which divides by the same.
 * Tell both that the screen is k times SMALLER than it is, and the whole box lands k times bigger
 * with every proportion inside it untouched. The row pitch, the baseline, the wrap and the two
 * nudges never move, which is the point of doing it this way: those are the part the decompilation
 * never reconstructed, and this does not have to know what any of them are.
 *
 *     told width   W/k         so the centring becomes (W/k - 640) / 2
 *     told height  H/k                                  H/k - 480
 *     posScale     (k/W, k/H)  which is 1 / (W/k) and 1 / (H/k), the same two numbers
 *     glyph scale  640k/W      which is 640 / (W/k), the same number again
 *
 * So three divisors and two getters all read one pair of values, and that pair is the only thing
 * that has to be right.
 *
 * k is fitted BY HEIGHT, k = H/480, and only that fit makes a 4:3 box behave on a 16:9 screen. By
 * width it would be W/640, which at 16:9 makes the box taller than the screen and pushes the
 * baseline off the bottom. By height the box comes out 1.333*H wide, narrower than the screen, so
 * it pillarboxes exactly as the layout expects.
 *
 * At 640x480 with a scale of 1, k is 1 and every number above is the one the engine already had, so
 * the shipped look is reproduced rather than approximated.
 * ============================================================================================ */

/* Read by the game's own instructions once the operands point here, so a new value takes effect on
 * the next subtitle drawn with nothing patched again. Never allowed to reach zero: they are
 * divisors. */
static float told_width  = AUTHORED_WIDTH;
static float told_height = AUTHORED_HEIGHT;
static float told_wrap   = AUTHORED_WRAP;

static struct {
    detour_t  bar;
    bool      installed;        /* the ten writes are in place */
    bool      abandoned;        /* one of them was refused, the rest were put back, and it is not
                                 * tried again: a site that was wrong once is wrong every time */
    bool      resolved;         /* the sites are known, which happens long before the display is */
    uintptr_t pos_site;
    uintptr_t glyph_site;
    uintptr_t bar_site;
    float   scale;              /* the player's multiplier, on top of fitting the height */
    int32_t told_w;             /* what the two replacement getters answer */
    int32_t told_h;
    int32_t seen_w;             /* the display those numbers were computed from */
    int32_t seen_h;
} state;

/* The replacements for the engine's own two getters, reached ONLY from DLG_DrawLine's two call
 * sites: the call targets are rewritten rather than the getters themselves, so everything else in
 * the game that asks how big the screen is still gets the truth. */
static int32_t __cdecl told_screen_width(void)
{
    return state.told_w;
}

static int32_t __cdecl told_screen_height(void)
{
    return state.told_h;
}

static float box_scale(int32_t height);

/* The two backdrop quads, moved to match the box the text is now drawn in. */
static void __cdecl hook_draw_bar(float x0, float y0, float x1, float y1, uint32_t argb)
{
    draw_bar_fn_t original = (draw_bar_fn_t)state.bar.original;
    float         k        = box_scale(state.seen_h);
    float         half_w   = (float)state.seen_w * 0.5f;
    float         bottom   = (float)state.seen_h;

    if (!state.installed || !(k > 0.0f) || state.seen_h <= 0) {
        original(x0, y0, x1, y1, argb);      /* nothing known yet, so nothing is moved */
        return;
    }
    original(half_w + k * (x0 - half_w), bottom + k * (y0 - bottom),
             half_w + k * (x1 - half_w), bottom + k * (y1 - bottom), argb);
}

static bool read_screen(int32_t *out_w, int32_t *out_h)
{
    if (!memory_is_readable_range(SCREEN_WIDTH_INT, sizeof(int32_t)) ||
        !memory_is_readable_range(SCREEN_HEIGHT_INT, sizeof(int32_t))) {
        return false;
    }
    *out_w = *(const int32_t *)(uintptr_t)SCREEN_WIDTH_INT;
    *out_h = *(const int32_t *)(uintptr_t)SCREEN_HEIGHT_INT;
    return (*out_w > 0) && (*out_h > 0);
}

static float clamp_scale(float scale)
{
    if (!(scale > 0.0f)) {              /* negated, so a value that is not a number lands here */
        return SUBTITLE_SCALE_ENGINE;
    }
    if (scale < SUBTITLE_SCALE_MIN) {
        return SUBTITLE_SCALE_MIN;
    }
    if (scale > SUBTITLE_SCALE_MAX) {
        return SUBTITLE_SCALE_MAX;
    }
    return scale;
}

static float box_scale(int32_t height)
{
    return ((float)height / AUTHORED_HEIGHT) * state.scale;
}

/* Recomputes the pair from the live display. Answers false when the display cannot be read, and
 * then the previous numbers stand rather than being replaced by something invented. */
static bool recompute(void)
{
    int32_t w = 0;
    int32_t h = 0;
    float   k;

    if (!read_screen(&w, &h)) {
        return false;
    }
    k = box_scale(h);
    if (!(k > 0.0f)) {
        return false;
    }

    told_width   = (float)w / k;
    told_height  = (float)h / k;
    told_wrap    = AUTHORED_WRAP * k;
    state.told_w = (int32_t)(told_width + 0.5f);
    state.told_h = (int32_t)(told_height + 0.5f);
    state.seen_w = w;
    state.seen_h = h;
    return true;
}

/* ALL TEN OR NONE. Three make the box bigger, two put it back where it belongs, two keep the line
 * breaks where the box is, two let it hang off the top edge once it is taller than the screen, and
 * the last moves the backdrop quad to match. Any subset is a box of the wrong size, in the wrong
 * place, wrapping at the wrong column, with its text under the bottom of the display, or with the
 * panel behind it still the old size, and every one of those has now been photographed. */
static bool install_patches(void);

/* Once a second, which is far more often than anybody drags. It exists for two reasons: the
 * developer menu's row writes the key and this is what notices, and a resolution change moves every
 * number above. */
#define POLL_FRAMES 60u

static void on_frame(void)
{
    static uint32_t frames;
    float           wanted;
    int32_t         w = 0;
    int32_t         h = 0;

    if (++frames < POLL_FRAMES) {
        return;
    }
    frames = 0;

    wanted = clamp_scale(ini_read_float(RESOLUTION_SECTION, SUBTITLE_SCALE_KEY, 1.0f));
    if (wanted == SUBTITLE_SCALE_ENGINE) {
        /* Zero asks for the engine's own box back, the one value this cannot honour while the
         * game runs: the operands and the two calls would have to be put back, which is
         * a code write rather than a number. The last good scale stands until the next launch. */
        wanted = state.scale;
    }
    (void)read_screen(&w, &h);

    if (wanted == state.scale && w == state.seen_w && h == state.seen_h) {
        return;
    }
    state.scale = wanted;
    if (!recompute()) {
        return;              /* no display yet, so the old numbers stand and nothing is said */
    }
    if (state.resolved && !state.installed) {
        (void)install_patches();     /* the display has arrived; it says so itself */
        return;
    }
    log_info("subtitles: the layout is told %dx%d for a real %dx%d, so the authored 640x480 "
             "box is drawn %.2f times its own size",
             (int)state.told_w, (int)state.told_h, (int)w, (int)h, (double)box_scale(h));
}

/* Rewrites one call's target. The instruction stays a call and its length does not change; only
 * where it goes does, and only at this one site. */
/* The two wrap comparisons, each checked for its opcode before it is touched: both are the same
 * six byte `fcomp dword [imm32]`, so the operand sits two bytes in. */
/* The nine reversible writes are journaled as they go, so a refusal part way through puts the
 * earlier ones back and the engine draws its own box, the way the header promises. The detour is
 * the tenth and last, because a detour cannot be taken out again. */
typedef struct written_bytes {
    uintptr_t at;
    uint8_t   size;
    uint8_t   before[4];
} written_bytes_t;

static written_bytes_t journal[9];
static size_t          journal_count;

static bool write_journaled(uintptr_t at, const void *bytes, size_t size)
{
    written_bytes_t *entry;

    if (journal_count >= sizeof journal / sizeof journal[0] || size > sizeof entry->before) {
        return false;
    }
    entry = &journal[journal_count];
    if (!memory_read(at, entry->before, size)) {
        return false;
    }
    entry->at   = at;
    entry->size = (uint8_t)size;
    if (patch_write_bytes(at, bytes, size) != PATCH_RESULT_OK) {
        return false;
    }
    ++journal_count;
    return true;
}

static void undo_journal(void)
{
    while (journal_count > 0u) {
        const written_bytes_t *entry = &journal[--journal_count];

        (void)patch_write_bytes(entry->at, entry->before, entry->size);
    }
}

static bool write_pointer_journaled(uintptr_t at, const void *pointer)
{
    uint32_t value = (uint32_t)(uintptr_t)pointer;

    return write_journaled(at, &value, sizeof value);
}

static bool retarget_operand(uintptr_t operand_site, const void *cell)
{
    if (!memory_is_readable_range(operand_site - 2u, 6u) ||
        *(const uint8_t *)(operand_site - 2u) != 0xD8u ||
        *(const uint8_t *)(operand_site - 1u) != 0x1Du) {
        log_warning("the wrap comparison at %08X is not the one expected, so the subtitle box is "
                    "left alone", (unsigned)(operand_site - 2u));
        return false;
    }
    return write_pointer_journaled(operand_site, cell);
}

/* Turns one `jge` into a `jmp`, having checked it is the conditional this expects. */
static bool unclamp(uintptr_t site)
{
    if (!memory_is_readable_range(site, 2u) || *(const uint8_t *)site != JGE_REL8) {
        log_warning("the offset clamp at %08X is not the branch expected, so the subtitle box is "
                    "left alone", (unsigned)site);
        return false;
    }
    {
        uint8_t jmp = JMP_REL8;

        return write_journaled(site, &jmp, sizeof jmp);
    }
}

static bool retarget_call(uintptr_t site, const void *destination)
{
    int32_t rel;

    if (!memory_is_readable_range(site, 5u) || *(const uint8_t *)site != 0xE8u) {
        log_warning("the site at %08X is not a call, so the subtitle box is left alone",
                    (unsigned)site);
        return false;
    }
    rel = (int32_t)((uintptr_t)destination - (site + 5u));
    return write_journaled(site + 1u, &rel, sizeof rel);
}

static bool install_patches(void)
{
    if (state.abandoned) {
        return false;
    }
    journal_count = 0;
    if (!write_pointer_journaled(state.pos_site + POS_SCALE_HEIGHT_OPERAND, &told_height) ||
        !write_pointer_journaled(state.pos_site + POS_SCALE_WIDTH_OPERAND, &told_width) ||
        !write_pointer_journaled(state.glyph_site + GLYPH_SCALE_WIDTH_OPERAND, &told_width) ||
        !retarget_operand(state.glyph_site + WRAP_FIRST_OPERAND_FROM_GLYPH, &told_wrap) ||
        !retarget_operand(state.glyph_site + WRAP_SECOND_OPERAND_FROM_GLYPH, &told_wrap) ||
        !retarget_call(state.glyph_site + CENTRE_WIDTH_CALL_FROM_GLYPH,
                       (const void *)told_screen_width) ||
        !retarget_call(state.glyph_site + CENTRE_HEIGHT_CALL_FROM_GLYPH,
                       (const void *)told_screen_height) ||
        !unclamp(state.glyph_site + CLAMP_X_FROM_GLYPH) ||
        !unclamp(state.glyph_site + CLAMP_Y_FROM_GLYPH) ||
        !detour_install(&state.bar, state.bar_site, (const void *)hook_draw_bar,
                        DRAW_BAR_PROLOGUE)) {
        undo_journal();
        state.abandoned = true;
        log_warning("one of the subtitle box's ten sites was refused, so the ones already "
                    "written were put back and the engine draws its own box this session");
        return false;
    }

    state.installed = true;
    log_info("the subtitle box now scales with the display. The engine drew a 640x480 box of fixed "
             "pixels and centred it, holding the text at a constant PIXEL size, so it shrank as "
             "the resolution rose. The layout is now told the screen is %dx%d rather than %dx%d "
             "and its two scales divide by that same pair, so the box, its row pitch, its "
             "baseline and its wrap all grow together. Fitted by HEIGHT, so a 4:3 box pillarboxes "
             "on a wide screen instead of running off the bottom. Nothing else the font layer "
             "draws is affected",
             (int)state.told_w, (int)state.told_h, (int)state.seen_w, (int)state.seen_h);
    return true;
}

void subtitle_scale_install(void)
{
    uintptr_t pos_site;
    uintptr_t glyph_site;
    uintptr_t bar_site;

    if (state.installed) {
        return;
    }

    state.scale = clamp_scale(ini_read_float(RESOLUTION_SECTION, SUBTITLE_SCALE_KEY, 1.0f));
    if (state.scale == SUBTITLE_SCALE_ENGINE) {
        log_info("SubtitleScale=0, so the subtitles keep the engine's own behaviour: a 640x480 box "
                 "of fixed PIXELS centred on the screen, so the text shrinks as the "
                 "display grows");
        return;
    }

    pos_site   = signature_find_unique(SIG_POS_SCALE, NULL, sizeof SIG_POS_SCALE);
    glyph_site = signature_find_unique(SIG_GLYPH_SCALE, NULL, sizeof SIG_GLYPH_SCALE);
    bar_site   = signature_find_unique(SIG_DRAW_BAR, NULL, sizeof SIG_DRAW_BAR);
    if (pos_site == 0 || glyph_site == 0 || bar_site == 0) {
        log_warning("the subtitle layout was not found (%s, %s, %s), so it keeps the engine's own "
                    "size",
                    (pos_site != 0) ? "position scale found" : "position scale NOT found",
                    (glyph_site != 0) ? "glyph scale found" : "glyph scale NOT found",
                    (bar_site != 0) ? "backdrop found" : "backdrop NOT found");
        return;
    }
    state.pos_site   = pos_site;
    state.glyph_site = glyph_site;
    state.bar_site   = bar_site;
    state.resolved   = true;

    /* NOTHING is written until the display is known, on the order the game starts in rather than
     * on caution. This runs from the loader, before any mode has been set, so the two size
     * cells are still zero and the numbers every one of the five writes depends on cannot be
     * computed yet. Patching anyway with the authored 640x480 in them would draw the box across
     * the whole screen for as long as it took the first poll to correct it.
     *
     * The first attempt did the writes here and refused when the read failed, so it reported that
     * the display could not be read and then did nothing at all for the session. */
    if (recompute() && !install_patches()) {
        return;
    }
    if (!state.installed) {
        log_info("the subtitle box is ready to scale and is waiting for a display mode: this runs "
                 "before the game has set one, so the size it has to divide by does not exist yet. "
                 "It takes effect within a second of the first mode being set");
    }

    if (!frame_hook_add(on_frame)) {
        log_warning("no per-frame hook, so SubtitleScale and the display size are read once at "
                    "startup and a change to either waits for the next launch");
    }
}
