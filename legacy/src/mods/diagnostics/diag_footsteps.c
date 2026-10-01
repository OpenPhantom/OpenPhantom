/* diag_footsteps.c: see diag_footsteps.h. */
#include "diag_footsteps.h"

#include "diag_install.h"
#include "diag_log.h"

#include "common/logging.h"
#include "common/memory.h"
#include "common/signature.h"

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

/* --- footstep_tick 0x00437AC0 ----------------------------------------------------------------- *
 *   55 8B EC 83 EC 08            push ebp / mov ebp,esp / sub esp,8
 *   8B 45 10                     mov eax,[ebp+0x10]         the thing
 *   A3 <g_footThing>             mov [g_footThing],eax
 *   8B 4D 10                     mov ecx,[ebp+0x10]
 *   8B 91 E4 00 00 00            mov edx,[ecx+0xE4]         thing->pFloorPoly
 *   89 15 <g_floorPoly>          mov [g_floorPoly],edx
 *   83 3D <g_floorPoly> 00       cmp dword [g_floorPoly],0
 *   74 2F                        jz  +0x2F                  airborne: nothing to step on
 *
 * Then, at +0x2D, the material: `xor ecx,ecx / mov cx,[eax+0x3E] / sar ecx,12 / and ecx,15`, the
 * top nibble of the polygon's 16-bit surface word. And at +0xEF the stamp this exists to watch:
 *
 *   00437BB8  83 7D F8 0C   cmp [ebp-8],12        material below 12: no stamp
 *   00437BBE  83 7D F8 0E   cmp [ebp-8],14        above 14: no stamp
 *   00437BC6  8B 15 <g_footThing>
 *   00437BCC  A1 <g_gameTime>                     wall seconds since the process started
 *   00437BD1  89 82 08 01 00 00   mov [edx+0x108],eax   the wet stamp
 *
 * Three absolute cells sit inside the head, so they are masked and the pattern is the code
 * around them. The prologue is nine bytes to the first store and holds no relative operand. The
 * clock cell is read out of the `mov eax,[imm32]` at +0x10C, past the pattern, so its opcode is
 * checked before the operand is believed. */
static const uint8_t SIG_FOOTSTEP_TICK[] = {
    0x55, 0x8B, 0xEC, 0x83, 0xEC, 0x08, 0x8B, 0x45, 0x10, 0xA3, 0x00, 0x00, 0x00, 0x00,
    0x8B, 0x4D, 0x10, 0x8B, 0x91, 0xE4, 0x00, 0x00, 0x00, 0x89, 0x15, 0x00, 0x00, 0x00, 0x00,
    0x83, 0x3D, 0x00, 0x00, 0x00, 0x00, 0x00, 0x74, 0x2F
};
static const uint8_t MSK_FOOTSTEP_TICK[] = {
    0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0x00, 0x00, 0x00, 0x00,
    0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0x00, 0x00, 0x00, 0x00,
    0xFF, 0xFF, 0x00, 0x00, 0x00, 0x00, 0xFF, 0xFF, 0xFF
};
_Static_assert(sizeof SIG_FOOTSTEP_TICK == sizeof MSK_FOOTSTEP_TICK,
               "the footstep tick pattern and its mask are different lengths");
#define FOOTSTEP_TICK_PROLOGUE      9u
#define OFFSET_CLOCK_LOAD           0x10Cu   /* the A1 opcode */
#define OFFSET_CLOCK_OPERAND        0x10Du

#define THING_POSITION              0x18u    /* vec3, world position */
#define THING_FLOOR_POLY            0xE4u
#define THING_LAST_WET              0x108u   /* float, wall seconds at the last wet footstep */
#define POLY_SURFACE_WORD           0x3Eu    /* uint16, material in the top nibble */
#define WET_WINDOW_SECONDS          8.0f     /* [0x4A84BC] in the print pass */

#define MATERIAL_WATER              12
#define MATERIAL_WATER_PLANE        14

/* The engine's material table, top nibble of the surface word. 15 has no name in the image. */
static const char *const MATERIAL_NAMES[16] = {
    "none", "concrete", "metal", "grating", "crete", "marble", "dirt", "gravel",
    "sand", "junk", "grass", "mud", "water", "swamp", "water plane", "unnamed 15"
};

/* Bounded, since a walk across a swamp is a stamp on every tick. A spell is reported when it
 * begins and every second it continues; a print spell when it begins. */
#define FOOTSTEP_LINES              400
#define TICKS_PER_REPEAT            32       /* the simulation runs 32 substeps a second */

enum {
    SITE_FOOTSTEP_TICK,
    SITE_COUNT
};

static signature_t sites[SITE_COUNT] = {
    SIGNATURE_ENTRY_DETOUR_MASKED("footstep_tick", SIG_FOOTSTEP_TICK, MSK_FOOTSTEP_TICK,
                                  FOOTSTEP_TICK_PROLOGUE)
};

typedef void (__cdecl *footstep_tick_fn_t)(int loco_state, int gender, void *thing);

static struct {
    detour_t             detour;
    const volatile float *clock;
    bool                 armed;
    unsigned             lines;
    uintptr_t            last_thing;
    int                  last_material;
    bool                 last_in_spell;   /* a wet print spell was in force on the last tick */
    unsigned             ticks_in_wet;
} foot;

static bool may_report(void)
{
    if (foot.lines >= FOOTSTEP_LINES) {
        if (foot.lines == FOOTSTEP_LINES) {
            ++foot.lines;
            diag_log_write("foot line budget of %d reached, the rest of the session is not "
                           "reported", FOOTSTEP_LINES);
        }
        return false;
    }
    ++foot.lines;
    return true;
}

static void report_tick(uintptr_t thing)
{
    uint32_t poly = 0;
    uint16_t surface = 0;
    float    position[3] = { 0.0f, 0.0f, 0.0f };
    float    last_wet = 0.0f;
    float    now;
    float    age;
    int      material;
    bool     wet;
    bool     in_spell;

    /* Every read here is the per-tick kind: the pointer came from the engine a moment ago and a
     * fault is caught rather than asked about. */
    if (!memory_try_read_u32(thing + THING_FLOOR_POLY, &poly) || poly == 0) {
        return;                                     /* airborne, the tick stamped nothing */
    }
    (void)memory_try_read(thing + THING_POSITION, position, sizeof position);
    (void)memory_try_read(thing + THING_LAST_WET, &last_wet, sizeof last_wet);
    if (!memory_try_read(poly + POLY_SURFACE_WORD, &surface, sizeof surface)) {
        if (may_report()) {
            diag_log_write("foot thing %08X at (%.1f %.1f %.1f): floor polygon %08X is not "
                           "readable, so the material the tick read is unknown",
                           (unsigned)thing, (double)position[0], (double)position[1],
                           (double)position[2], (unsigned)poly);
        }
        return;
    }
    material = (surface >> 12) & 0xF;
    wet      = material >= MATERIAL_WATER && material <= MATERIAL_WATER_PLANE;
    now      = *foot.clock;
    age      = now - last_wet;
    in_spell = !wet && age < WET_WINDOW_SECONDS;

    if (thing != foot.last_thing) {
        foot.last_material = -1;
        foot.last_in_spell = false;
        foot.ticks_in_wet  = 0;
        foot.last_thing    = thing;
    }

    if (wet) {
        bool first = (material != foot.last_material);

        ++foot.ticks_in_wet;
        if ((first || foot.ticks_in_wet % TICKS_PER_REPEAT == 0) && may_report()) {
            diag_log_write("foot WET STAMP: thing %08X at (%.1f %.1f %.1f) on polygon %08X, "
                           "surface %04X, material %d (%s)%s",
                           (unsigned)thing, (double)position[0], (double)position[1],
                           (double)position[2], (unsigned)poly, (unsigned)surface, material,
                           MATERIAL_NAMES[material], first ? "" : ", still");
        }
    } else {
        foot.ticks_in_wet = 0;
        if (in_spell && !foot.last_in_spell && may_report()) {
            diag_log_write("foot wet prints begin: thing %08X on material %d (%s) at "
                           "(%.1f %.1f %.1f), polygon %08X surface %04X, the stamp is %.2f s old "
                           "(clock %.2f, stamp %.2f)",
                           (unsigned)thing, material, MATERIAL_NAMES[material],
                           (double)position[0], (double)position[1], (double)position[2],
                           (unsigned)poly, (unsigned)surface, (double)age, (double)now,
                           (double)last_wet);
        }
    }
    foot.last_material = material;
    foot.last_in_spell = in_spell;
}

static void __cdecl hook_footstep_tick(int loco_state, int gender, void *thing)
{
    ((footstep_tick_fn_t)foot.detour.original)(loco_state, gender, thing);
    if (foot.armed && thing != NULL) {
        report_tick((uintptr_t)thing);
    }
}

int diag_footsteps_install(int level)
{
    uintptr_t site;
    uint8_t   opcode = 0;
    uint32_t  cell   = 0;

    if (level <= 0) {
        return 0;
    }
    signature_resolve_table(sites, SITE_COUNT);
    site = sites[SITE_FOOTSTEP_TICK].address;
    if (site == 0) {
        return 0;                       /* the table already said which site did not resolve */
    }

    if (!memory_read_u8(site + OFFSET_CLOCK_LOAD, &opcode) || opcode != 0xA1 ||
        !memory_read_image_cell(site + OFFSET_CLOCK_OPERAND, sizeof(float), &cell)) {
        log_warning("footsteps: the wall clock load at %08X is not the `mov eax,[imm32]` "
                    "expected, so the footstep observer is not installed",
                    (unsigned)(site + OFFSET_CLOCK_LOAD));
        return 0;
    }
    foot.clock         = (const volatile float *)(uintptr_t)cell;
    foot.last_material = -1;

    if (!diag_install_observer(sites, SITE_FOOTSTEP_TICK, &foot.detour,
                               (const void *)hook_footstep_tick, FOOTSTEP_TICK_PROLOGUE,
                               "every footstep tick, for the wet stamp and the wet print spell")) {
        return 0;
    }
    foot.armed = true;
    diag_log_write("foot observer armed: wall clock at %08X, a stamp is material 12 to 14 under "
                   "the foot, a print spell is %.0f s from the stamp on any other material",
                   (unsigned)cell, (double)WET_WINDOW_SECONDS);
    return 1;
}
