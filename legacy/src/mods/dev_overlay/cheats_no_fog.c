/* cheats_no_fog.c: see cheats_no_fog.h.
 *
 * ==============================================================================================
 * THE SITE, REUSED FROM fog_regime.c RATHER THAN RE-DERIVED
 *
 * view_distance_fix's fog_regime.c already proved, in full, that the live world pointer, g_level,
 * can be read out of bapdraw_setFrameState's own operands with a cross check that costs nothing:
 * the pointer is embedded twice, once in a `cmp` and once in a `mov`, and a build where the two
 * disagree is not the function this is written against. It also proved the world record's fog
 * band, `world+0x218` (start) and `world+0x21C` (end), both world-unit floats the per-vertex fog
 * ramp reads every frame.
 *
 * Retail WMAIN.EXE, 829,952 bytes, ImageBase 0x400000, at 0x00401DFE:
 *
 *   83 3D <g_level> 00      cmp dword ptr [g_level],0
 *   0F 84 <rel32>           je   -> no world this frame
 *   8B 15 <g_level>         mov edx,[g_level]
 *   8B 82 10 02 00 00       mov eax,[edx+0x210]      the world's render-flag word
 *   83 E0 01                and eax,1                bit 0 = "this world has fog"
 *
 * This is a SEPARATE DLL from view_distance_fix, so the pattern, the mask and the cross check are
 * carried here again rather than shared at run time - the two never touch each other's memory, the
 * same isolation every other feature DLL in this project keeps. What is not repeated is the
 * research: this project's own rule is byte evidence over assumption, and byte evidence already
 * proven correct in one file does not need proving a second time in another, only citing.
 *
 * ==============================================================================================
 * FIRST VERSION CLEARED THE FLAG BIT. FIELD TESTING FOUND THAT BREAKS THE RENDERER.
 *
 * The first version of this cheat cleared world+0x210 bit 0 every frame - "this world has fog",
 * off. Field testing found that leaves every moving actor (the player, fish, foliage) drawn as a
 * flat, unlit silhouette, and the damage does not undo when the bit is set back: something reads
 * that bit once and latches a rendering path this project has not identified, not something a
 * flipped bit alone reverses. Retail never toggles this bit at runtime at all - it is set once,
 * at level load, from the level file header, and held fixed for the level's whole life - so a
 * runtime flip exercises a combination of engine state nothing in 1999 ever produced.
 *
 * fog_regime.c never touches that bit either. What it changes, continuously, every frame, in a
 * feature that has shipped without this failure, is the band itself: world+0x218 and world+0x21C.
 * So this file does the same rather than the flag: while the cheat is on, it pushes the band's
 * start and end out past anything the world walk's own draw-distance cull can still be showing
 * (0x00404F33 clamps that cull to [2,64] world units, per fog_regime.c's own clamp_cut), so the
 * per-vertex ramp never has anything left in view to fog. The flag stays exactly as the level
 * authored it, which is the one thing field testing showed matters.
 *
 * ==============================================================================================
 * WHY A PER-FRAME FORCE, NOT ONE WRITE - AND WHY IT REMEMBERS THE BAND RATHER THAN DECLINING
 *
 * Ammunition and health are each spent through one function this project can decline. Fog is not
 * spent through anything: it is two floats the renderer reads directly out of the level record
 * every frame, and the record is freed and replaced whole on every level load, so a single write at
 * the moment the cheat is switched on would last exactly until the next level change undoes it.
 * Holding the band out every frame, through the same common/frame_hook.h tick fog_regime.c already
 * uses for its own easing, is what makes the cheat survive a level change rather than needing to be
 * pressed again after every one.
 *
 * A first version of THIS file also declined to restore anything on the way back off, the same
 * "never invent a value" rule the ammunition and health cheats follow. Field testing found that
 * the wrong call here: those two cheats decline a SUBTRACTION, so "off" is simply "stop declining"
 * and the game's own systems carry on from wherever they already were. Fog has no such systems to
 * hand back to mid-level - nothing else in the engine ever restores this band, because nothing else
 * ever moves it - so declining to restore left the player able to turn fog off but never back on
 * again short of a level reload, which is not a toggle.
 *
 * So this remembers the band it found BEFORE ever touching it, once per level - the same
 * `is_the_same_level` / `remember_level` shape fog_regime.c already uses to survive the record
 * being freed and reallocated, simplified down to what a plain on/off needs: is this still the
 * record we last wrote to, checked by BOTH the pointer and the two floats still holding exactly
 * what we last put there. A level that fails either check is treated as new, and whatever band it
 * is holding right now - before this file does anything to it - becomes the value "off" restores.
 * ============================================================================================== */
#include "cheats_no_fog.h"

#include "common/frame_hook.h"
#include "common/logging.h"
#include "common/memory.h"
#include "common/patch.h"
#include "common/signature.h"

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

static const uint8_t SIG_LEVEL_POINTER[] = {
    0x83, 0x3D, 0x00, 0x00, 0x00, 0x00, 0x00,          /* cmp dword ptr [g_level],0        */
    0x0F, 0x84, 0x00, 0x00, 0x00, 0x00,                /* je   -> no world this frame      */
    0x8B, 0x15, 0x00, 0x00, 0x00, 0x00,                /* mov edx,[g_level]                */
    0x8B, 0x82, 0x10, 0x02, 0x00, 0x00,                /* mov eax,[edx+0x210]  render flags */
    0x83, 0xE0, 0x01                                   /* and eax,1            fog bit      */
};
static const uint8_t MSK_LEVEL_POINTER[] = {
    0xFF, 0xFF, 0x00, 0x00, 0x00, 0x00, 0xFF,
    0xFF, 0xFF, 0x00, 0x00, 0x00, 0x00,
    0xFF, 0xFF, 0x00, 0x00, 0x00, 0x00,
    0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF,
    0xFF, 0xFF, 0xFF
};
_Static_assert(sizeof(SIG_LEVEL_POINTER) == sizeof(MSK_LEVEL_POINTER),
               "the level pointer pattern and its mask are different lengths");
#define OFFSET_LEVEL_POINTER_CMP 0x02u   /* operand of `cmp dword ptr [g_level],0` */
#define OFFSET_LEVEL_POINTER_MOV 0x0Fu   /* operand of `mov edx,[g_level]`, must be the same */
#define WORLD_RENDER_FLAGS       0x210u  /* the world record's render-flag word, READ ONLY here */
#define WORLD_FOG_BIT            0x001u  /* bit 0 = this level authored fog at all */
#define WORLD_FOG_START          0x218u  /* float, world units - never written past this level's */
#define WORLD_FOG_END            0x21Cu  /* float, world units - own authored band without asking */
#define WORLD_PROBE_SIZE         (WORLD_FOG_END + sizeof(float))

/* Comfortably past the world walk's own draw-distance clamp of [2,64] world units (0x00404F33,
 * documented in fog_regime.c's clamp_cut) - nothing the renderer still has in view at these depths,
 * so the per-vertex ramp never finds anything left to fog. Not an astronomical number: the ramp's
 * own math divides by the band width, and keeping this within a few thousand units leaves that
 * comfortably inside float32's precision. */
#define NO_FOG_START 4000.0f
#define NO_FOG_END   5000.0f

typedef struct no_fog_state {
    bool             installed;
    bool             on;
    void *volatile  *level_pointer;   /* [g_level], the same site fog_regime.c reads */

    /* The level currently remembered, the band it was authored with (captured once, before this
     * file ever wrote to it), and the band this file itself last wrote - which is what tells the
     * next frame whether the record still holds ours or has been freed and reallocated under us. */
    void            *remembered_level;
    bool             have_authored;
    float            authored_start;
    float            authored_end;
    float            written_start;
    float            written_end;
} no_fog_state_t;

static no_fog_state_t st;

/* ============================================================================================ */
static void forget_level(void)
{
    st.remembered_level = NULL;
    st.have_authored = false;
}

static void tick(void)
{
    void    *level;
    uint32_t flags = 0;
    float    band[2];
    float    target_start;
    float    target_end;
    bool     is_the_same_record;

    if (st.level_pointer == NULL) {
        return;
    }
    level = *st.level_pointer;
    if (level == NULL || !memory_is_readable_range((uintptr_t)level, WORLD_PROBE_SIZE)) {
        forget_level();
        return;
    }
    if (!memory_read_u32((uintptr_t)level + WORLD_RENDER_FLAGS, &flags) ||
        (flags & WORLD_FOG_BIT) == 0) {
        forget_level();                /* this level authored no fog: nothing to remember or push */
        return;
    }
    if (!memory_read((uintptr_t)level + WORLD_FOG_START, band, sizeof band)) {
        forget_level();
        return;
    }

    /* Is this still the record we last touched? The pointer alone is not an identity - the record
     * is freed and reallocated on every level load, and an allocator hands out the same address
     * more often than not - so a level that no longer holds exactly what we last wrote has been
     * through a load since, whatever its address says. */
    is_the_same_record = st.have_authored && level == st.remembered_level &&
                         band[0] == st.written_start && band[1] == st.written_end;
    if (!is_the_same_record) {
        st.remembered_level = level;
        st.authored_start   = band[0];
        st.authored_end     = band[1];
        st.have_authored     = true;
    }

    target_start = st.on ? NO_FOG_START : st.authored_start;
    target_end   = st.on ? NO_FOG_END   : st.authored_end;

    if (band[0] == target_start && band[1] == target_end) {
        return;                        /* already there: no write, no cache line touched */
    }
    (void)patch_write_f32((uintptr_t)level + WORLD_FOG_START, target_start);
    (void)patch_write_f32((uintptr_t)level + WORLD_FOG_END, target_end);
    st.written_start = target_start;
    st.written_end   = target_end;
}

/* ============================================================================================ */
bool cheats_no_fog_install(void)
{
    uintptr_t site;
    uint32_t  from_cmp = 0;
    uint32_t  from_mov = 0;

    if (st.installed) {
        return st.level_pointer != NULL;
    }
    st.installed = true;

    site = signature_find_unique(SIG_LEVEL_POINTER, MSK_LEVEL_POINTER, sizeof SIG_LEVEL_POINTER);
    if (site == 0) {
        log_warning("no fog: the world pointer did not resolve, that cheat stays unavailable");
        return false;
    }
    if (!memory_read_u32(site + OFFSET_LEVEL_POINTER_CMP, &from_cmp) ||
        !memory_read_u32(site + OFFSET_LEVEL_POINTER_MOV, &from_mov) ||
        from_cmp != from_mov || !memory_is_inside_image(from_cmp, sizeof(void *))) {
        log_warning("no fog: the two world-pointer operands at %08X disagree (%08X vs %08X), that "
                    "cheat stays unavailable", (unsigned)site, (unsigned)from_cmp,
                    (unsigned)from_mov);
        return false;
    }
    if (!frame_hook_add(tick)) {
        log_warning("no fog: the per-frame hook could not be armed, that cheat stays unavailable "
                    "rather than a single write that would not survive the next level");
        return false;
    }

    st.level_pointer = (void *volatile *)(uintptr_t)from_cmp;
    log_info("no fog: world pointer at %08X, pushing world+0x218/0x21C out to %.0f/%.0f every "
             "frame while on and restoring the level's own band while off. The fog flag at "
             "world+0x210 is never touched.",
             (unsigned)from_cmp, (double)NO_FOG_START, (double)NO_FOG_END);
    return true;
}

bool cheats_no_fog_is_available(void)
{
    return st.level_pointer != NULL;
}

bool cheats_no_fog_is_on(void)
{
    return st.on;
}

bool cheats_no_fog_toggle(void)
{
    if (!cheats_no_fog_is_available()) {
        return false;
    }
    st.on = !st.on;
    return st.on;
}
