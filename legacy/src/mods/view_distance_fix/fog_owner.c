/* fog_owner.c: whose fog the device shows, and the band push. See the header for why. */
#include "fog_owner.h"

#include "common/logging.h"
#include "common/memory.h"
#include "common/patch.h"
#include "common/signature.h"

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <string.h>

/* The world record's packed fog colour, 0xRRGGBB, from the level header at load. */
#define WORLD_FOG_COLOUR 0x214u
#define RGB_MASK         0x00FFFFFFu

/* Bit 6 of the render flags word is FOGENABLE: the full commit issues render state 0x1C from it
 * (0x00489B0A..0x00489B2A), and the level's fog apply and the engine's ramp clear and set exactly
 * this bit around every band they write. */
#define RENDER_FLAG_FOG 0x40u

/* How long a band the effects set without their colour is left alone. */
#define GRACE_SUBSTEPS 1u

/* --- 0x0041F1A8  inside baplight_applyLevelFog: its four calls into std3D ------------------- *
 *   0041F1A8  E8 <rel32>            call std3D_setFogColor 0x00487A30 (r, g, b)
 *   0041F1AD  83 C4 0C              add  esp,0xC
 *   0041F1B0  8B 45 08              mov  eax,[ebp+8]
 *   0041F1B3  8B 88 1C 02 00 00     mov  ecx,[eax+0x21C]           the record's fog end
 *   0041F1B9  51                    push ecx
 *   0041F1BA  8B 55 08              mov  edx,[ebp+8]
 *   0041F1BD  8B 82 18 02 00 00     mov  eax,[edx+0x218]           the record's fog start
 *   0041F1C3  50                    push eax
 *   0041F1C4  E8 <rel32>            call std3D_setFogRange 0x00487AC0
 *   0041F1C9  83 C4 08              add  esp,8
 *   0041F1CC  8B 4D 08              mov  ecx,[ebp+8]
 *   0041F1CF  8B 91 10 02 00 00     mov  edx,[ecx+0x210]           the record's flags, bit 0 fog
 *   0041F1D5  83 E2 01 / 85 D2      and edx,1 / test edx,edx
 *   0041F1DA  74 36                 je   the fog-off branch
 *   0041F1DC  E8 <rel32>            call std3D_getRenderFlags 0x00487A10
 *   0041F1E1  24 BF                 and  al,0xBF                    FOGENABLE off
 *   0041F1E3  50                    push eax
 *   0041F1E4  E8 <rel32>            call std3D_setRenderFlags 0x00487A20
 *   0041F1E9  83 C4 04              add  esp,4
 * 0x5E bytes into the function, behind the six the detour takes, so reading it needs no patch
 * site of its own. Masked: the four calls, whose targets are read and then proved below. */
#define APPLY_BODY_OFFSET 0x5Eu
static const uint8_t SIG_APPLY_BODY[] = {
    0xE8, 0x00, 0x00, 0x00, 0x00, 0x83, 0xC4, 0x0C, 0x8B, 0x45, 0x08, 0x8B, 0x88, 0x1C, 0x02,
    0x00, 0x00, 0x51, 0x8B, 0x55, 0x08, 0x8B, 0x82, 0x18, 0x02, 0x00, 0x00, 0x50, 0xE8, 0x00,
    0x00, 0x00, 0x00, 0x83, 0xC4, 0x08, 0x8B, 0x4D, 0x08, 0x8B, 0x91, 0x10, 0x02, 0x00, 0x00,
    0x83, 0xE2, 0x01, 0x85, 0xD2, 0x74, 0x36, 0xE8, 0x00, 0x00, 0x00, 0x00, 0x24, 0xBF, 0x50,
    0xE8, 0x00, 0x00, 0x00, 0x00, 0x83, 0xC4, 0x04
};
static const uint8_t MSK_APPLY_BODY[] = {
    0xFF, 0x00, 0x00, 0x00, 0x00, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF,
    0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0x00,
    0x00, 0x00, 0x00, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF,
    0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0x00, 0x00, 0x00, 0x00, 0xFF, 0xFF, 0xFF,
    0xFF, 0x00, 0x00, 0x00, 0x00, 0xFF, 0xFF, 0xFF
};
_Static_assert(sizeof SIG_APPLY_BODY == sizeof MSK_APPLY_BODY,
               "the fog apply body pattern and its mask are different lengths");
#define BODY_CALL_COLOUR 0x00u
#define BODY_CALL_RANGE  0x1Cu
#define BODY_CALL_GET    0x34u
#define BODY_CALL_SET    0x3Cu

/* --- 0x00487A30  std3D_setFogColor: three arguments into the device's three colour cells ----- *
 *   8B 44 24 04 / 8B 4C 24 08 / 8B 54 24 0C     the red, green and blue arguments
 *   A3 <red> / 89 0D <green> / 89 15 <blue>     0x006FC988, 0x00854E50, 0x006FC980
 *   C3
 * The only writer of those cells in the whole image, called from four places: the level's fog
 * apply, the effects' fog ramp, the savegame's fog restore and the loading screen. The same
 * pattern the multiplayer DLL proves; it is unique in the image. Masked: the cells. */
static const uint8_t SIG_FOG_COLOUR[] = {
    0x8B, 0x44, 0x24, 0x04, 0x8B, 0x4C, 0x24, 0x08, 0x8B, 0x54, 0x24, 0x0C, 0xA3, 0x00, 0x00,
    0x00, 0x00, 0x89, 0x0D, 0x00, 0x00, 0x00, 0x00, 0x89, 0x15, 0x00, 0x00, 0x00, 0x00, 0xC3
};
static const uint8_t MSK_FOG_COLOUR[] = {
    0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0x00, 0x00,
    0x00, 0x00, 0xFF, 0xFF, 0x00, 0x00, 0x00, 0x00, 0xFF, 0xFF, 0x00, 0x00, 0x00, 0x00, 0xFF
};
_Static_assert(sizeof SIG_FOG_COLOUR == sizeof MSK_FOG_COLOUR,
               "the fog colour pattern and its mask are different lengths");
#define COLOUR_RED   0x0Du
#define COLOUR_GREEN 0x13u
#define COLOUR_BLUE  0x19u

/* --- 0x00487AC0  std3D_setFogRange: the band into two cells, and nothing to the device ------ *
 *   8B 44 24 04 / 8B 4C 24 08                   start, end
 *   A3 <start> / 89 0D <end> / C3               0x00866FA8, 0x00866FA4
 * The cells reach the device on the next commit, which every std3D_setRenderFlags runs, and on
 * the per face switch when a face turns the fog bit back on. Three functions of the image have
 * this shape, so it is checked where the call leads rather than searched for. Masked: the cells. */
static const uint8_t SIG_FOG_RANGE[] = {
    0x8B, 0x44, 0x24, 0x04, 0x8B, 0x4C, 0x24, 0x08, 0xA3, 0x00, 0x00, 0x00, 0x00, 0x89, 0x0D,
    0x00, 0x00, 0x00, 0x00, 0xC3
};
static const uint8_t MSK_FOG_RANGE[] = {
    0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0x00, 0x00, 0x00, 0x00, 0xFF, 0xFF,
    0x00, 0x00, 0x00, 0x00, 0xFF
};
_Static_assert(sizeof SIG_FOG_RANGE == sizeof MSK_FOG_RANGE,
               "the fog range pattern and its mask are different lengths");
#define RANGE_START 0x09u
#define RANGE_END   0x0Fu

/* --- 0x00487A10  std3D_getRenderFlags, and 0x00487A20  std3D_setRenderFlags ------------------ *
 *   A1 <flags> / C3                             mov eax,[0x00855960]; ret
 *   8B 44 24 04 / A3 <flags> / E9 <rel32>       store the argument, jump into the full commit
 *                                               at 0x00489837, which ends in its own ret
 * Both must name the same flags word. Masked: the word and the jump. */
static const uint8_t SIG_GET_FLAGS[] = { 0xA1, 0x00, 0x00, 0x00, 0x00, 0xC3 };
static const uint8_t MSK_GET_FLAGS[] = { 0xFF, 0x00, 0x00, 0x00, 0x00, 0xFF };
static const uint8_t SIG_SET_FLAGS[] = { 0x8B, 0x44, 0x24, 0x04, 0xA3, 0x00, 0x00, 0x00, 0x00,
                                         0xE9 };
static const uint8_t MSK_SET_FLAGS[] = { 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0x00, 0x00, 0x00, 0x00,
                                         0xFF };
_Static_assert(sizeof SIG_GET_FLAGS == sizeof MSK_GET_FLAGS &&
                   sizeof SIG_SET_FLAGS == sizeof MSK_SET_FLAGS,
               "the render flag patterns and their masks are different lengths");
#define GET_FLAGS_CELL 0x01u
#define SET_FLAGS_CELL 0x05u

/* --- 0x004757DB  the tail of the substep loop, where the engine counts its substeps ---------- *
 *   A1 <counter> / 83 C0 01 / A3 <counter>      g_tickCounter 0x004B8860, plus one
 *   D9 05 <sim time> / D8 05 <dt> / D9 1D <sim time>
 * The count moves after the substep's tasks, the scripts among them, have run. The Swift menu
 * advances it once a frame as well, which only shortens a grace while a menu is up. Every absolute
 * operand masked; one match in the image. framerate_fix and effect_clock read the same cell out of
 * the same increment, each in its own DLL. */
static const uint8_t SIG_SUBSTEP_COUNTER[] = {
    0xA1, 0x00, 0x00, 0x00, 0x00, 0x83, 0xC0, 0x01, 0xA3, 0x00, 0x00, 0x00, 0x00,
    0xD9, 0x05, 0x00, 0x00, 0x00, 0x00, 0xD8, 0x05, 0x00, 0x00, 0x00, 0x00,
    0xD9, 0x1D, 0x00, 0x00, 0x00, 0x00
};
static const uint8_t MSK_SUBSTEP_COUNTER[] = {
    0xFF, 0x00, 0x00, 0x00, 0x00, 0xFF, 0xFF, 0xFF, 0xFF, 0x00, 0x00, 0x00, 0x00,
    0xFF, 0xFF, 0x00, 0x00, 0x00, 0x00, 0xFF, 0xFF, 0x00, 0x00, 0x00, 0x00,
    0xFF, 0xFF, 0x00, 0x00, 0x00, 0x00
};
_Static_assert(sizeof SIG_SUBSTEP_COUNTER == sizeof MSK_SUBSTEP_COUNTER,
               "the substep counter pattern and its mask are different lengths");
#define SUBSTEP_COUNTER_CELL 0x01u

typedef struct fog_owner_counts {
    uint32_t frames;           /* frames with the effects' colour on the device */
    uint32_t held;             /* band pushes held back for the effects */
    uint32_t graces;           /* bands they set without their colour, honoured for a substep */
    uint32_t made;
    uint32_t over;             /* pushes made over a band the effects had written */
    uint32_t colour_changed;   /* the colour differed across one of this module's pushes */
} fog_owner_counts_t;

typedef struct fog_owner_state {
    bool                attempted;
    bool                colour_bound;
    bool                cells_bound;
    bool                writes_bound;
    const volatile uint32_t *colour[3];
    const volatile float    *band_start;
    const volatile float    *band_end;
    const volatile uint32_t *substep;
    fog_device_writes_t writes;

    bool                level_open;
    bool                effects;          /* the last frame looked found the effects' colour */
    uint32_t            episode_frames;
    fog_owner_grace_t   grace;
    fog_owner_counts_t  n;
} fog_owner_state_t;

static fog_owner_state_t owner;

/* ==============================================================================================
 * Pure
 * ============================================================================================ */
static bool same_band(const fog_regime_band_t *a, const fog_regime_band_t *b)
{
    return a->start == b->start && a->end == b->end;
}

bool fog_owner_effects_hold(uint32_t device_rgb, uint32_t level_rgb)
{
    return (device_rgb & RGB_MASK) != (level_rgb & RGB_MASK);
}

fog_holder_t fog_owner_judge(const fog_owner_view_t *view, fog_owner_grace_t *grace,
                             bool *grace_began)
{
    if (grace_began != NULL) {
        *grace_began = false;
    }
    if (view->colour_read && fog_owner_effects_hold(view->device_rgb, view->level_rgb)) {
        grace->running = false;
        return FOG_HELD_BY_EFFECTS;
    }
    if (!view->cells_read || same_band(&view->cells, &view->pushed)) {
        grace->running = false;
        return FOG_HELD_BY_LEVEL;
    }
    /* A band that is neither ours nor the record's. Without a substep clock there is no grace to
     * give, and the band goes over it at once, as it did before. */
    if (!view->substep_read) {
        return FOG_HELD_BY_LEVEL;
    }
    if (!grace->running || !same_band(&grace->band, &view->cells)) {
        grace->running = true;
        grace->band    = view->cells;
        grace->since   = view->substep;
        if (grace_began != NULL) {
            *grace_began = true;
        }
        return FOG_HELD_FOR_A_SUBSTEP;
    }
    return view->substep - grace->since < GRACE_SUBSTEPS ? FOG_HELD_FOR_A_SUBSTEP
                                                          : FOG_HELD_BY_LEVEL;
}

bool fog_owner_band_settled(const fog_regime_band_t *current, const fog_regime_band_t *written,
                            bool pixel_fog, fog_holder_t holder, bool device_shows_current)
{
    return same_band(current, written) &&
           (!pixel_fog || holder != FOG_HELD_BY_LEVEL || device_shows_current);
}

void fog_owner_push_band(const fog_device_writes_t *writes, const fog_regime_band_t *band)
{
    uint32_t flags;
    uint32_t start_bits;
    uint32_t end_bits;

    memcpy(&start_bits, &band->start, sizeof start_bits);
    memcpy(&end_bits, &band->end, sizeof end_bits);
    flags = writes->get_render_flags();
    writes->set_render_flags(flags & ~RENDER_FLAG_FOG);
    writes->set_fog_range(start_bits, end_bits);
    writes->set_render_flags(flags);
}

/* ==============================================================================================
 * Binding, once
 * ============================================================================================ */

/* The bytes at `address` against a masked pattern. Install time, so the asking read. */
static bool holds(uintptr_t address, const uint8_t *sig, const uint8_t *mask, size_t size)
{
    uint8_t seen[sizeof SIG_APPLY_BODY];
    size_t  i;

    if (address == 0u || size > sizeof seen || !memory_read(address, seen, size)) {
        return false;
    }
    for (i = 0; i < size; ++i) {
        if (mask[i] != 0u && seen[i] != sig[i]) {
            return false;
        }
    }
    return true;
}

/* A cell named by the operand at `operand`, inside the image. */
static bool cell_at(uintptr_t operand, uintptr_t *cell)
{
    uint32_t value = 0;

    if (!memory_read_u32(operand, &value) || !memory_is_inside_image(value, sizeof(uint32_t))) {
        return false;
    }
    *cell = (uintptr_t)value;
    return true;
}

/* The function a call of the body leads to, proved by its own bytes. */
static uintptr_t callee(uintptr_t body, uintptr_t call, const uint8_t *sig, const uint8_t *mask,
                        size_t size)
{
    uintptr_t target = 0;

    if (!patch_read_call_target(body + call, &target) || !holds(target, sig, mask, size)) {
        return 0u;
    }
    return target;
}

static void bind_substep_counter(void)
{
    uintptr_t site = signature_find_unique(SIG_SUBSTEP_COUNTER, MSK_SUBSTEP_COUNTER,
                                           sizeof SIG_SUBSTEP_COUNTER);
    uintptr_t cell = 0;

    if (site == 0u || !cell_at(site + SUBSTEP_COUNTER_CELL, &cell)) {
        log_warning("the engine's substep counter is NOT found (%s), so a band the effects set "
                    "without their colour gets no substep of grace and this module's band goes "
                    "over it in the next frame",
                    site == 0u ? "the increment did not resolve"
                               : "its operand is not in the image");
        return;
    }
    owner.substep = (const volatile uint32_t *)cell;
    log_info("the fog's substep of grace counts the engine's substeps at %08X, read out of the "
             "increment at %08X", (unsigned)cell, (unsigned)site);
}

static const char *bind_colour(uintptr_t body)
{
    uintptr_t setter = callee(body, BODY_CALL_COLOUR, SIG_FOG_COLOUR, MSK_FOG_COLOUR,
                              sizeof SIG_FOG_COLOUR);
    uintptr_t cells[3] = { 0u, 0u, 0u };

    if (setter == 0u) {
        return "the colour call does not lead to std3D_setFogColor";
    }
    if (!cell_at(setter + COLOUR_RED, &cells[0]) || !cell_at(setter + COLOUR_GREEN, &cells[1]) ||
        !cell_at(setter + COLOUR_BLUE, &cells[2]) || cells[0] == cells[1] ||
        cells[1] == cells[2] || cells[0] == cells[2]) {
        return "the colour cells are not three cells of the image";
    }
    owner.colour[0]    = (const volatile uint32_t *)cells[0];
    owner.colour[1]    = (const volatile uint32_t *)cells[1];
    owner.colour[2]    = (const volatile uint32_t *)cells[2];
    owner.colour_bound = true;
    return NULL;
}

static const char *bind_writes(uintptr_t body, uintptr_t *range, uintptr_t *get, uintptr_t *set)
{
    uintptr_t start = 0;
    uintptr_t end = 0;
    uintptr_t get_cell = 0;
    uintptr_t set_cell = 0;

    *range = callee(body, BODY_CALL_RANGE, SIG_FOG_RANGE, MSK_FOG_RANGE, sizeof SIG_FOG_RANGE);
    if (*range == 0u || !cell_at(*range + RANGE_START, &start) ||
        !cell_at(*range + RANGE_END, &end) || start == end) {
        return "the band call does not lead to std3D_setFogRange";
    }
    owner.band_start  = (const volatile float *)start;
    owner.band_end    = (const volatile float *)end;
    owner.cells_bound = true;

    *get = callee(body, BODY_CALL_GET, SIG_GET_FLAGS, MSK_GET_FLAGS, sizeof SIG_GET_FLAGS);
    *set = callee(body, BODY_CALL_SET, SIG_SET_FLAGS, MSK_SET_FLAGS, sizeof SIG_SET_FLAGS);
    if (*get == 0u || *set == 0u || !cell_at(*get + GET_FLAGS_CELL, &get_cell) ||
        !cell_at(*set + SET_FLAGS_CELL, &set_cell) || get_cell != set_cell) {
        return "the two flag calls do not lead to one render flags word";
    }
    owner.writes.set_fog_range    = (fog_set_range_fn_t)*range;
    owner.writes.get_render_flags = (fog_get_render_flags_fn_t)*get;
    owner.writes.set_render_flags = (fog_set_render_flags_fn_t)*set;
    owner.writes_bound = true;
    return NULL;
}

void fog_owner_bind(uintptr_t apply_level_fog)
{
    uintptr_t   body = apply_level_fog + APPLY_BODY_OFFSET;
    uintptr_t   range = 0;
    uintptr_t   get = 0;
    uintptr_t   set = 0;
    const char *colour_why;
    const char *writes_why;

    if (owner.attempted) {
        return;
    }
    owner.attempted = true;
    bind_substep_counter();

    if (apply_level_fog == 0u || !holds(body, SIG_APPLY_BODY, MSK_APPLY_BODY,
                                        sizeof SIG_APPLY_BODY)) {
        colour_why = "applyLevelFog's body is not the one this is written against";
        writes_why = colour_why;
    } else {
        colour_why = bind_colour(body);
        writes_why = bind_writes(body, &range, &get, &set);
    }
    if (colour_why != NULL) {
        /* Without the owner nothing else here is used either, so the push is the old one. */
        owner.cells_bound  = false;
        owner.writes_bound = false;
        log_warning("the fog's device colour is NOT read (%s), so the effects' fog cannot be told "
                    "from this level's: the band goes to the device through applyLevelFog as it "
                    "always did, and a green room loses its colour in the first frame the band "
                    "moves", colour_why);
        return;
    }
    if (writes_why != NULL) {
        log_warning("the fog's device writes are NOT bound (%s), so the band goes to the device "
                    "through applyLevelFog and only while the device colour is this level's own",
                    writes_why);
        return;
    }
    log_info("the fog's device writes are bound: std3D_setFogRange at %08X, std3D_getRenderFlags "
             "at %08X and std3D_setRenderFlags at %08X, read out of applyLevelFog's own calls at "
             "%08X, %08X and %08X; the device colour is read at %08X, %08X and %08X",
             (unsigned)range, (unsigned)get, (unsigned)set, (unsigned)(body + BODY_CALL_RANGE),
             (unsigned)(body + BODY_CALL_GET), (unsigned)(body + BODY_CALL_SET),
             (unsigned)(uintptr_t)owner.colour[0], (unsigned)(uintptr_t)owner.colour[1],
             (unsigned)(uintptr_t)owner.colour[2]);
}

/* ==============================================================================================
 * Every frame
 * ============================================================================================ */
static bool device_colour(uint32_t *rgb)
{
    if (!owner.colour_bound) {
        return false;
    }
    *rgb = ((*owner.colour[0] & 0xFFu) << 16) | ((*owner.colour[1] & 0xFFu) << 8) |
           (*owner.colour[2] & 0xFFu);
    return true;
}

static void look(const void *level, const fog_regime_band_t *pushed, fog_owner_view_t *view)
{
    uint32_t level_colour = 0;

    memset(view, 0, sizeof *view);
    view->pushed      = *pushed;
    view->colour_read = device_colour(&view->device_rgb) &&
                        memory_try_read_u32((uintptr_t)level + WORLD_FOG_COLOUR, &level_colour);
    view->level_rgb   = level_colour & RGB_MASK;
    if (owner.cells_bound) {
        view->cells_read  = true;
        view->cells.start = *owner.band_start;
        view->cells.end   = *owner.band_end;
    }
    if (owner.substep != NULL) {
        view->substep_read = true;
        view->substep      = *owner.substep;
    }
}

static fog_holder_t judge_now(const void *level, const fog_regime_band_t *pushed,
                              fog_owner_view_t *view)
{
    bool         began = false;
    fog_holder_t holder;

    look(level, pushed, view);
    holder = fog_owner_judge(view, &owner.grace, &began);
    if (began) {
        ++owner.n.graces;
    }
    return holder;
}

fog_holder_t fog_owner_observe(const void *level, const fog_regime_band_t *pushed,
                               const fog_regime_band_t *current)
{
    fog_owner_view_t view;
    fog_holder_t     holder = judge_now(level, pushed, &view);
    bool             effects = holder == FOG_HELD_BY_EFFECTS;

    if (effects) {
        ++owner.n.frames;
        ++owner.episode_frames;
    }
    if (effects == owner.effects) {
        return holder;
    }
    owner.effects = effects;
    if (effects) {
        log_info("the fog belongs to the effects now: the device colour %06X is not this level's "
                 "%06X, so the band they set (start %.2f end %.2f) is left on the device",
                 (unsigned)view.device_rgb, (unsigned)view.level_rgb, (double)view.cells.start,
                 (double)view.cells.end);
    } else {
        log_info("the fog is this level's again after %u frame(s): the engine applied it, and "
                 "the band %.2f..%.2f is this module's to push", (unsigned)owner.episode_frames,
                 (double)current->start, (double)current->end);
        owner.episode_frames = 0u;
    }
    return holder;
}

bool fog_owner_device_shows(const fog_regime_band_t *band, const fog_regime_band_t *pushed)
{
    fog_regime_band_t cells;

    if (!owner.cells_bound) {
        return same_band(band, pushed);
    }
    cells.start = *owner.band_start;
    cells.end   = *owner.band_end;
    return same_band(band, &cells);
}

bool fog_owner_push(void *level, const fog_regime_band_t *band, const fog_regime_band_t *pushed,
                    fog_apply_fn_t apply)
{
    fog_owner_view_t view;
    bool             over;
    uint32_t         after = 0;

    if (judge_now(level, pushed, &view) != FOG_HELD_BY_LEVEL) {
        ++owner.n.held;
        return false;
    }
    over = view.cells_read && !same_band(&view.cells, pushed);
    if (owner.writes_bound) {
        fog_owner_push_band(&owner.writes, band);
    } else if (apply != NULL) {
        apply(level);
    } else {
        return false;
    }
    ++owner.n.made;
    owner.n.over += over ? 1u : 0u;
    if (view.colour_read && device_colour(&after) && after != view.device_rgb) {
        ++owner.n.colour_changed;
    }
    return true;
}

void fog_owner_level_begins(void)
{
    memset(&owner.n, 0, sizeof owner.n);
    memset(&owner.grace, 0, sizeof owner.grace);
    owner.effects        = false;
    owner.episode_frames = 0u;
    owner.level_open     = true;
}

void fog_owner_level_ends(void)
{
    if (!owner.level_open) {
        return;
    }
    owner.level_open = false;
    log_info("  the fog's owner (view_distance_fix): %u frame(s) with the effects' fog, %u band "
             "push(es) held back for them, %u band(s) they set without their colour honoured for "
             "a substep, %u band push(es) made, %u of them over a band the effects had written; "
             "the colour differed across this module's own push %u time(s) (must be 0)",
             (unsigned)owner.n.frames, (unsigned)owner.n.held, (unsigned)owner.n.graces,
             (unsigned)owner.n.made, (unsigned)owner.n.over, (unsigned)owner.n.colour_changed);
}
