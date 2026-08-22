#include "diag_flow.h"

#include "diag_install.h"
#include "diag_log.h"
#include "diag_names.h"

#include "common/detour.h"
#include "common/logging.h"
#include "common/memory.h"
#include "common/signature.h"

#include <windows.h>
#include <mmsystem.h>

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

/* --- campaign_loadLevel 0x0043F70A ------------------------------------------------------------ *
 * The old world is freed before the new one is loaded: a failed load leaves the game with no
 * world at all. Returns 1 = loaded, 0 = not. */
static const uint8_t SIG_LEVEL_LOAD[] = {
    0x55, 0x8B, 0xEC, 0x81, 0xEC, 0x88, 0x00, 0x00, 0x00, 0x56, 0x57, 0xC7,
    0x05, 0xD8, 0xCF, 0x6C, 0x00, 0x01, 0x00, 0x00
};
#define LEVEL_LOAD_PROLOGUE 9u

/* --- Dialog_EnterInputLock 0x00430ED9 / Dialog_LeaveInputLock 0x00430F18 ---------------------- *
 * The cutscene lock is a level, not a switch: dialogue takes 1, op 0x604 takes 5, module teardown
 * takes 99. `Leave` only unwinds when its own level is at least the current one, a stuck scene
 * is almost always a `Leave` with too small a level.
 *   +0x0D : &g_cinematicLock [0x6C4D8C], the hook reads the LEVEL back after the call. */
static const uint8_t SIG_LOCK_ENTER[] = {
    0x55, 0x8B, 0xEC, 0x51, 0xC7, 0x45, 0xFC, 0x00, 0x00, 0x00, 0x00, 0x83,
    0x3D, 0x8C, 0x4D, 0x6C, 0x00, 0x00, 0x75, 0x11
};
#define LOCK_ENTER_PROLOGUE 11u
#define OFFSET_LOCK_ADDRESS 0x0Du
static const uint8_t SIG_LOCK_LEAVE[] = {
    0x55, 0x8B, 0xEC, 0x51, 0xC7, 0x45, 0xFC, 0x00, 0x00, 0x00, 0x00, 0x83,
    0x3D, 0x8C, 0x4D, 0x6C, 0x00, 0x00, 0x7E, 0x25
};
#define LOCK_LEAVE_PROLOGUE 11u

/* --- Plr_RunPhases 0x00448297 ----------------------------------------------------------------- *
 * The player mode is NOT an enum but a POINTER to a descriptor (player+0x60, the state-object
 * pattern). It becomes a number only through the NULL-terminated pointer table at [0x4B54B0], and
 * that is exactly what this hook does. It runs per SUBSTEP (32 Hz) and logs only the CHANGE.
 *   +0x27 : &pPlayer [0x4B5220] */
static const uint8_t SIG_PLAYER_RUN_PHASES[] = {
    0x55, 0x8B, 0xEC, 0x83, 0xEC, 0x08, 0xC7, 0x45, 0xF8, 0x00, 0x00, 0x00,
    0x00, 0xC7, 0x45, 0xFC, 0x00, 0x00, 0x00, 0x00, 0x8B, 0x45, 0xF8, 0x83,
    0x3C, 0x85, 0x28, 0x52, 0x4B, 0x00, 0x01, 0x0F, 0x84, 0x95, 0x00, 0x00,
    0x00, 0x8B, 0x0D, 0x20, 0x52, 0x4B, 0x00
};
#define PLAYER_RUN_PHASES_PROLOGUE 6u
#define OFFSET_PLAYER_POINTER      0x27u

/* --- A DATA SITE ONLY, no hook: player_save 0x004479F8 pushes the mode pointer table as a
 *     `push imm32`. That is where [0x4B54B0] comes from.
 *   +0x0A : &g_plrModeTable */
static const uint8_t SIG_PLAYER_MODE_TABLE[] = {
    0xA1, 0x20, 0x52, 0x4B, 0x00, 0x8B, 0x48, 0x60, 0x51, 0x68, 0xB0, 0x54,
    0x4B, 0x00, 0xE8
};
#define OFFSET_MODE_TABLE 0x0Au

/* --- Dialog_SpeakSingle 0x00430D12 ------------------------------------------------------------ *
 * "Say this line". Returns 1 = newly started, 0 = same speaker lock, only subtitle and timing
 * refreshed. */
static const uint8_t SIG_DIALOG_SPEAK[] = {
    0x55, 0x8B, 0xEC, 0x83, 0xEC, 0x0C, 0xC7, 0x45, 0xF8, 0x00, 0x00, 0x00,
    0x00, 0x8B, 0x45, 0x10, 0x50, 0xE8, 0x27, 0x04
};
#define DIALOG_SPEAK_PROLOGUE 6u

/* --- Dialog_PlayVoice 0x0043125A -------------------------------------------------------------- *
 * TWO traps that become visible exactly here: a DEBOUNCE on lineId (the same line twice in a row
 * is a no-op), and the response path (mode 1) discards the position it was given and plays at the
 * PLAYER's position. */
static const uint8_t SIG_DIALOG_VOICE[] = {
    0x55, 0x8B, 0xEC, 0x83, 0xEC, 0x30, 0x53, 0x56, 0x57, 0x83, 0x3D, 0x34,
    0x13, 0x88, 0x00, 0x00, 0x75, 0x05, 0xE9, 0x11
};
#define DIALOG_VOICE_PROLOGUE 6u

/* --- bapvrt_addDecal 0x0041C4C0 --------------------------------------------------------------- *
 * The decal pool lends out of 255 records; density = perPolyLimit x detail level, and eviction
 * takes the oldest one not drawn for >= 0.5 s. Return 0 = not stamped, and the three reasons for
 * that (GPU off, density full, no victim) are the real value of this hook.
 * No frame pointer: __cdecl with eight arguments through [esp+..]. Level 2. */
static const uint8_t SIG_ADD_DECAL[] = {
    0xA1, 0x08, 0xFE, 0x89, 0x00, 0x53, 0xD9, 0x05, 0x0C, 0x82, 0x4A, 0x00,
    0x56, 0x57, 0x33, 0xFF, 0x83, 0xCE, 0xFF, 0x85
};
#define ADD_DECAL_PROLOGUE 5u

/* --- bapvrt_drawPolyDecals 0x0041C620 -------------------------------------------------------- *
 * The decal is the polygon, drawn a second time, and this is where a stamped record either reaches
 * the device or is dropped without a word. "Stamped" therefore tells you nothing about "drawn":
 * fx_add_decal above can report 100 % success while nothing at all appears on screen.
 *
 * Two silent exits, neither of which the engine logs and neither of which retires the record:
 * the material's texture page (mat+0xB0) is NULL, or rdMaterial_selectCel refuses it. This hook
 * answers the first; the selectCel observer below answers the second.
 *
 * tLastDraw is NOT a usable discriminator here, although it looks like one. The engine stamps it
 * on a successful draw, but bapvrt_addDecal ALSO stamps it at creation with the same frame clock,
 * and the blob shadow is re-stamped every frame, so an undrawn record can carry a current
 * timestamp. Only the page pointer is read here.
 *
 * void, and that is checked at the callers rather than assumed: all four (0x0041B987, 0x0041BA17,
 * 0x0041C324, 0x0041C3B9) do `add esp,0x10` and then overwrite EAX without ever reading it. Four
 * cdecl arguments. Prologue `81 EC 94 00 00 00` is six bytes on a clean boundary. Level 3. */
static const uint8_t SIG_DRAW_POLY_DECALS[] = {
    0x81, 0xEC, 0x94, 0x00, 0x00, 0x00, 0x53, 0x55, 0x8B, 0xAC, 0x24, 0xA0,
    0x00, 0x00, 0x00, 0x33, 0xC0, 0x56, 0x57, 0x8A
};
#define DRAW_POLY_DECALS_PROLOGUE 6u

/* --- the record table, derived from the drawer's own index arithmetic ------------------------- *
 *   0041C67B  8B D0            mov edx, eax                  <- the record index
 *   0041C67D  C1 E2 04         shl edx, 4
 *   0041C680  03 D0            add edx, eax                  <- index * 17
 *   0041C682  8B 14 95 ....    mov edx,[edx*4 + 0x005BB620]  <- (index*17)*4 = index*0x44
 * So the stride is 0x44 and the operand is the table base PLUS the pPoly offset. The base is not
 * written down anywhere in this DLL; it is read out of that operand at install time. */
static const uint8_t SIG_DECAL_TABLE[] = {
    0x8B, 0xD0, 0xC1, 0xE2, 0x04, 0x03, 0xD0, 0x8B, 0x14, 0x95, 0x20, 0xB6,
    0x5B, 0x00, 0x3B, 0xD5, 0x76, 0x05, 0x8D, 0x70
};
#define OFFSET_DECAL_TABLE_OPERAND 10u

#define DECAL_RECORD_STRIDE  0x44u  /* proven by the shl/add pair above */
#define DECAL_RECORD_COUNT    255u  /* (0x005BF9D4 - 0x005BB618) / 0x44 */
#define DECAL_OFF_POLY       0x08u
#define DECAL_OFF_MATERIAL   0x0Cu
#define MATERIAL_OFF_PAGE    0xB0u
#define DECAL_CENSUS_MAX       16u  /* per-poly density is perPolyLimit * detail level = 2 * 4 */

/* --- rdMaterial_selectCel 0x0047B9BD ---------------------------------------------------------- *
 * The second silent exit. SEVEN callers, and only 0x0041C712 is inside the decal drawer, so this
 * hook has to report that one and stay quiet for the other six, otherwise every material in the
 * level would drown the log. It does NOT do that by address: the drawer's observer raises a latch
 * around its call to the original, which is exact, needs no VA range and survives a build where the
 * drawer sits somewhere else.
 *
 * The surrounding material module is barely understood: which texture formats the retail renderer
 * really produces is an open question, and even which source file this routine belonged to is
 * disputed. None of that matters here, because the hook reports the return value and nothing
 * else. It draws no conclusion from the material it observes. */
static const uint8_t SIG_SELECT_CEL[] = {
    0x55, 0x8B, 0xEC, 0x83, 0xEC, 0x0C, 0x8B, 0x45, 0x10, 0x8B, 0x4D, 0x0C,
    0x8B, 0x54, 0x81, 0x1C, 0x89, 0x55, 0xFC, 0x8B
};
#define SELECT_CEL_PROLOGUE 6u

/* --- emitter_setPlacementActive 0x0041FE24 (script op 0x212) / emitter_destroy 0x004214BF ----- */
static const uint8_t SIG_EMITTER_ACTIVE[] = {
    0x55, 0x8B, 0xEC, 0x51, 0x83, 0x7D, 0x08, 0x00, 0x7C, 0x10, 0xA1, 0x60,
    0x00, 0x8A, 0x00, 0x8B, 0x4D, 0x08, 0x3B, 0x88
};
#define EMITTER_ACTIVE_PROLOGUE 8u
static const uint8_t SIG_EMITTER_DESTROY[] = {
    0x55, 0x8B, 0xEC, 0x51, 0x83, 0x7D, 0x08, 0x00, 0x7C, 0x06, 0x83, 0x7D,
    0x08, 0x40, 0x7C, 0x07, 0x33, 0xC0, 0xE9, 0x42
};
#define EMITTER_DESTROY_PROLOGUE 8u

enum {
    SITE_LEVEL_LOAD,
    SITE_LOCK_ENTER,
    SITE_LOCK_LEAVE,
    SITE_PLAYER_RUN_PHASES,
    SITE_PLAYER_MODE_TABLE,
    SITE_DIALOG_SPEAK,
    SITE_DIALOG_VOICE,
    SITE_ADD_DECAL,
    SITE_DRAW_POLY_DECALS,
    SITE_DECAL_TABLE,
    SITE_SELECT_CEL,
    SITE_EMITTER_ACTIVE,
    SITE_EMITTER_DESTROY,
    SITE_COUNT
};

static signature_t sites[SITE_COUNT] = {
    SIGNATURE_ENTRY("level_load",        SIG_LEVEL_LOAD),
    /* cutscene_pose_sync.dll and sfx_mute.c (fmv_player.dll) both hook or read from sites near this
     * one, and depending on load order this exact site may already be patched by the time this
     * DLL's own resolver looks - a plain SIGNATURE_ENTRY searches for the PRISTINE bytes and finds
     * zero matches once that has happened, exactly the render_frameEnd lesson signature.h's own
     * header documents. The detour-aware form finds it either way. */
    SIGNATURE_ENTRY_DETOUR("lock_enter", SIG_LOCK_ENTER, LOCK_ENTER_PROLOGUE),
    SIGNATURE_ENTRY("lock_leave",        SIG_LOCK_LEAVE),
    SIGNATURE_ENTRY("player_run_phases", SIG_PLAYER_RUN_PHASES),
    SIGNATURE_ENTRY("player_mode_table", SIG_PLAYER_MODE_TABLE),
    SIGNATURE_ENTRY("dialog_speak",      SIG_DIALOG_SPEAK),
    SIGNATURE_ENTRY("dialog_play_voice", SIG_DIALOG_VOICE),
    SIGNATURE_ENTRY("fx_add_decal",      SIG_ADD_DECAL),
    SIGNATURE_ENTRY("fx_draw_decals",    SIG_DRAW_POLY_DECALS),
    SIGNATURE_ENTRY("fx_decal_table",    SIG_DECAL_TABLE),
    SIGNATURE_ENTRY("fx_select_cel",     SIG_SELECT_CEL),
    SIGNATURE_ENTRY("fx_emitter_active", SIG_EMITTER_ACTIVE),
    SIGNATURE_ENTRY("fx_emitter_destroy",SIG_EMITTER_DESTROY)
};

#define PLAYER_CURRENT_MODE 0x60   /* a POINTER to the descriptor, not an enum */
#define PLAYER_MODE_COUNT     14

typedef int32_t (__cdecl *level_load_fn_t)(const char *path);
typedef int32_t (__cdecl *lock_fn_t)(int32_t level);
typedef void    (__cdecl *run_phases_fn_t)(void);
typedef int32_t (__cdecl *dialog_speak_fn_t)(void *speaker, int32_t camera_group, int32_t line_id,
                                             const void *position);
typedef void    (__cdecl *dialog_voice_fn_t)(int32_t line_id, int32_t mode, const void *position);
typedef int32_t (__cdecl *add_decal_fn_t)(void *poly, const float *uv, void *material,
                                          float time_in, float life, float alpha, int32_t owner,
                                          int32_t per_poly_limit);
typedef void    (__cdecl *draw_decals_fn_t)(void *poly, const float *verts, int32_t clipped,
                                            void *sorted);
typedef int32_t (__cdecl *select_cel_fn_t)(void *material, void *page, int32_t index);
typedef void    (__cdecl *emitter_active_fn_t)(int32_t placement_index, int32_t on);
typedef void    (__cdecl *emitter_destroy_fn_t)(int32_t slot);

typedef struct diag_flow_state {
    bool        sites_resolved;

    detour_t    level_load;
    detour_t    lock_enter;
    detour_t    lock_leave;
    detour_t    run_phases;
    detour_t    dialog_speak;
    detour_t    dialog_voice;
    detour_t    add_decal;
    detour_t    draw_decals;
    detour_t    select_cel;
    detour_t    emitter_active;
    detour_t    emitter_destroy;

    uint8_t    *decal_table;
    bool        inside_decal_draw;   /* raised only around the decal drawer's original call */

    int32_t    *cinematic_lock;
    uint32_t   *player_pointer_slot;
    void *const *mode_table;
    const void *last_mode_descriptor;
    bool        have_last_mode;
} diag_flow_state_t;

static diag_flow_state_t flow_state;

static void resolve_sites_once(void)
{
    if (flow_state.sites_resolved) {
        return;
    }
    flow_state.sites_resolved = true;
    signature_resolve_table(sites, SITE_COUNT);
}

/* ============================================================================================ */
static int32_t __cdecl hook_level_load(const char *path)
{
    level_load_fn_t original = (level_load_fn_t)flow_state.level_load.original;
    DWORD           started = timeGetTime();
    int32_t         result;

    diag_log_write("lvl  loading \"%s\" ... (the OLD world has already been freed at this point)",
                   (path != NULL) ? path : "(null)");
    result = original(path);
    diag_log_write("lvl  loading \"%s\" -> %s after %u ms", (path != NULL) ? path : "(null)",
                   (result != 0) ? "OK" : "FAILED (the game now has NO world at all)",
                   (unsigned)(timeGetTime() - started));
    return result;
}

static int32_t __cdecl hook_lock_enter(int32_t level)
{
    lock_fn_t original = (lock_fn_t)flow_state.lock_enter.original;
    int32_t   result = original(level);

    diag_log_write("lvl  cutscene ENTER(%d) -> was free: %s, level now %d", (int)level,
                   (result != 0) ? "yes" : "no",
                   (flow_state.cinematic_lock != NULL) ? (int)*flow_state.cinematic_lock : -1);
    return result;
}

static int32_t __cdecl hook_lock_leave(int32_t level)
{
    lock_fn_t original = (lock_fn_t)flow_state.lock_leave.original;
    int32_t   result = original(level);

    diag_log_write("lvl  cutscene LEAVE(%d) -> %s, level now %d", (int)level,
                   (result != 0) ? "released" : "NOT released (own level too small)",
                   (flow_state.cinematic_lock != NULL) ? (int)*flow_state.cinematic_lock : -1);
    return result;
}

/* ============================================================================================ */
static const char *player_mode_name(const void *descriptor)
{
    int index;

    if (descriptor == NULL) {
        return "(none)";
    }
    if (flow_state.mode_table == NULL) {
        return "(table not resolved)";
    }
    for (index = 0; index < PLAYER_MODE_COUNT && flow_state.mode_table[index] != NULL; ++index) {
        if (flow_state.mode_table[index] == descriptor) {
            return (diag_player_modes[index] != NULL) ? diag_player_modes[index] : "?";
        }
    }
    return "(not in the table)";
}

static void __cdecl hook_run_phases(void)
{
    run_phases_fn_t original = (run_phases_fn_t)flow_state.run_phases.original;
    const void     *record;
    const void     *descriptor;

    original();

    if (flow_state.player_pointer_slot == NULL) {
        return;
    }
    record = (const void *)(uintptr_t)*flow_state.player_pointer_slot;
    if (record == NULL) {
        return;
    }

    descriptor = *(const void * const *)((const uint8_t *)record + PLAYER_CURRENT_MODE);
    if (flow_state.have_last_mode && descriptor == flow_state.last_mode_descriptor) {
        return;                                    /* 32 Hz -> only the change */
    }

    diag_log_write("plr  mode %s -> %s",
                   flow_state.have_last_mode ? player_mode_name(flow_state.last_mode_descriptor)
                                             : "(none)",
                   player_mode_name(descriptor));
    flow_state.last_mode_descriptor = descriptor;
    flow_state.have_last_mode = true;
}

/* ============================================================================================ */
static int32_t __cdecl hook_dialog_speak(void *speaker, int32_t camera_group, int32_t line_id,
                                         const void *position)
{
    dialog_speak_fn_t original = (dialog_speak_fn_t)flow_state.dialog_speak.original;
    int32_t           result = original(speaker, camera_group, line_id, position);

    diag_log_write("dlg  say  line %d  camera=%d  -> %s", (int)line_id, (int)camera_group,
                   (result != 0) ? "newly started"
                                 : "same speaker: only subtitle and timing refreshed");
    return result;
}

static void __cdecl hook_dialog_voice(int32_t line_id, int32_t mode, const void *position)
{
    dialog_voice_fn_t original = (dialog_voice_fn_t)flow_state.dialog_voice.original;

    original(line_id, mode, position);
    diag_log_write("dlg  voice line %d  %s%s", (int)line_id,
                   (mode == 1) ? "RESPONSE (plays at the player position, `pos` is discarded)"
                               : "bark",
                   (position != NULL) ? "" : " [no position -> 2D]");
}

/* ============================================================================================ */
static int32_t __cdecl hook_add_decal(void *poly, const float *uv, void *material, float time_in,
                                      float life, float alpha, int32_t owner,
                                      int32_t per_poly_limit)
{
    add_decal_fn_t original = (add_decal_fn_t)flow_state.add_decal.original;
    int32_t        result;

    result = original(poly, uv, material, time_in, life, alpha, owner, per_poly_limit);

    diag_log_write("fx   decal poly=%08X life=%.1f alpha=%.2f owner=%d limit=%d -> %s",
                   (unsigned)(uintptr_t)poly, (double)life, (double)alpha, (int)owner,
                   (int)per_poly_limit,
                   (result != 0) ? "stamped"
                                 : "DISCARDED (GPU off, density full or no evictable slot)");
    return result;
}

/* Read-only census of the records the drawer is about to consider for this polygon. The engine's
 * own search is a binary search over the table sorted by pPoly; this is a linear scan of the same
 * table, which cannot disturb it and does not care whether the sort is currently valid. */
static void __cdecl hook_draw_poly_decals(void *poly, const float *verts, int32_t clipped,
                                          void *sorted)
{
    draw_decals_fn_t original = (draw_decals_fn_t)flow_state.draw_decals.original;
    unsigned    found = 0, no_material = 0, no_page = 0, with_page = 0;
    uintptr_t   first_material = 0, first_page = 0;
    unsigned    i;

    if (flow_state.decal_table == NULL) {
        original(poly, verts, clipped, sorted);
        return;
    }

    for (i = 0; i < DECAL_RECORD_COUNT && found < DECAL_CENSUS_MAX; ++i) {
        uintptr_t record = (uintptr_t)flow_state.decal_table + i * DECAL_RECORD_STRIDE;
        uint32_t  record_poly = 0, material = 0, page = 0;

        if (!memory_read_u32(record + DECAL_OFF_POLY, &record_poly) ||
            record_poly != (uint32_t)(uintptr_t)poly) {
            continue;
        }
        ++found;

        if (!memory_read_u32(record + DECAL_OFF_MATERIAL, &material) || material == 0) {
            ++no_material;
            continue;
        }
        if (!memory_read_u32((uintptr_t)material + MATERIAL_OFF_PAGE, &page) || page == 0) {
            ++no_page;
        } else {
            ++with_page;
        }
        if (first_material == 0) {
            first_material = material;
            first_page     = page;
        }
    }

    flow_state.inside_decal_draw = true;
    original(poly, verts, clipped, sorted);
    flow_state.inside_decal_draw = false;

    /* No polygon address in the line on purpose: the outcome is what matters and identical
     * consecutive lines are collapsed and counted, which a per-polygon address would defeat. */
    diag_log_write("fx   decal DRAW recs=%u  page ok=%u  NO PAGE=%u  no material=%u  "
                   "mat=%08X page=%08X",
                   found, with_page, no_page, no_material,
                   (unsigned)first_material, (unsigned)first_page);
}

/* Only the decal drawer's own call site, and only when the material is refused. The other six
 * callers are every other material in the level and would bury the log. */
static int32_t __cdecl hook_select_cel(void *material, void *page, int32_t index)
{
    select_cel_fn_t original = (select_cel_fn_t)flow_state.select_cel.original;
    bool            from_decal_drawer = flow_state.inside_decal_draw;
    int32_t         result = original(material, page, index);

    if (result == 0 && from_decal_drawer) {
        diag_log_write("fx   decal REFUSED by rdMaterial_selectCel: mat=%08X page=%08X index=%d",
                       (unsigned)(uintptr_t)material, (unsigned)(uintptr_t)page, (int)index);
    }
    return result;
}

static void __cdecl hook_emitter_active(int32_t placement_index, int32_t on)
{
    emitter_active_fn_t original = (emitter_active_fn_t)flow_state.emitter_active.original;

    original(placement_index, on);
    diag_log_write("fx   emitter placement %d -> %s", (int)placement_index,
                   (on != 0) ? "ON" : "OFF");
}

static void __cdecl hook_emitter_destroy(int32_t slot)
{
    emitter_destroy_fn_t original = (emitter_destroy_fn_t)flow_state.emitter_destroy.original;

    original(slot);
    diag_log_write("fx   emitter instance %d destroyed", (int)slot);
}

/* ============================================================================================ */
int diag_level_install(int level_level)
{
    int installed = 0;

    if (level_level <= 0) {
        return 0;
    }
    resolve_sites_once();

    flow_state.cinematic_lock =
        (int32_t *)diag_derive_address(sites, SITE_LOCK_ENTER, OFFSET_LOCK_ADDRESS,
                                       "g_cinematicLock");

    installed += diag_install_observer(sites, SITE_LEVEL_LOAD, &flow_state.level_load,
                                       (const void *)hook_level_load, LEVEL_LOAD_PROLOGUE,
                                       "level loading: path, success, duration") ? 1 : 0;
    installed += diag_install_observer(sites, SITE_LOCK_ENTER, &flow_state.lock_enter,
                                       (const void *)hook_lock_enter, LOCK_ENTER_PROLOGUE,
                                       "cutscene lock ENTER (levels 1/5/99)") ? 1 : 0;
    installed += diag_install_observer(sites, SITE_LOCK_LEAVE, &flow_state.lock_leave,
                                       (const void *)hook_lock_leave, LOCK_LEAVE_PROLOGUE,
                                       "cutscene lock LEAVE") ? 1 : 0;
    return installed;
}

int diag_player_install(int player_level)
{
    if (player_level <= 0) {
        return 0;
    }
    resolve_sites_once();

    flow_state.player_pointer_slot =
        (uint32_t *)diag_derive_address(sites, SITE_PLAYER_RUN_PHASES, OFFSET_PLAYER_POINTER,
                                        "pPlayer");
    flow_state.mode_table =
        (void *const *)diag_derive_address(sites, SITE_PLAYER_MODE_TABLE, OFFSET_MODE_TABLE,
                                           "g_plrModeTable");

    if (flow_state.player_pointer_slot == NULL) {
        log_warning("  player_run_phases OFF - pPlayer could not be derived");
        return 0;
    }

    return diag_install_observer(sites, SITE_PLAYER_RUN_PHASES, &flow_state.run_phases,
                                 (const void *)hook_run_phases, PLAYER_RUN_PHASES_PROLOGUE,
                                 "mode changes of the 14-mode state machine (debounced, 32 Hz)")
           ? 1 : 0;
}

int diag_dialogue_install(int dialogue_level)
{
    int installed = 0;

    if (dialogue_level <= 0) {
        return 0;
    }
    resolve_sites_once();

    installed += diag_install_observer(sites, SITE_DIALOG_SPEAK, &flow_state.dialog_speak,
                                       (const void *)hook_dialog_speak, DIALOG_SPEAK_PROLOGUE,
                                       "a spoken line (op 0x504 / 0x500)") ? 1 : 0;
    installed += diag_install_observer(sites, SITE_DIALOG_VOICE, &flow_state.dialog_voice,
                                       (const void *)hook_dialog_voice, DIALOG_VOICE_PROLOGUE,
                                       "voice file start including the response and bark paths")
                 ? 1 : 0;
    return installed;
}

int diag_fx_install(int fx_level)
{
    int installed = 0;

    if (fx_level <= 0) {
        return 0;
    }
    resolve_sites_once();

    installed += diag_install_observer(sites, SITE_EMITTER_ACTIVE, &flow_state.emitter_active,
                                       (const void *)hook_emitter_active,
                                       EMITTER_ACTIVE_PROLOGUE,
                                       "emitter placement on/off (op 0x212)") ? 1 : 0;
    installed += diag_install_observer(sites, SITE_EMITTER_DESTROY, &flow_state.emitter_destroy,
                                       (const void *)hook_emitter_destroy,
                                       EMITTER_DESTROY_PROLOGUE,
                                       "emitter instance destroyed") ? 1 : 0;
    if (fx_level >= 2) {
        installed += diag_install_observer(sites, SITE_ADD_DECAL, &flow_state.add_decal,
                                           (const void *)hook_add_decal, ADD_DECAL_PROLOGUE,
                                           "every decal, including the reasons for discarding")
                     ? 1 : 0;
    }
    if (fx_level >= 3) {
        /* The operand carries the table base PLUS the pPoly offset; back it out to record 0. */
        void *poly_slot = diag_derive_address(sites, SITE_DECAL_TABLE,
                                              OFFSET_DECAL_TABLE_OPERAND,
                                              "the decal record table");
        if (poly_slot != NULL) {
            flow_state.decal_table = (uint8_t *)poly_slot - DECAL_OFF_POLY;
            log_info("[diagnostics] decal table %08X, %u records of 0x%02X bytes, the DRAW census "
                     "reads pPoly, pMaterial and the material's texture page, and writes nothing",
                     (unsigned)(uintptr_t)flow_state.decal_table,
                     DECAL_RECORD_COUNT, DECAL_RECORD_STRIDE);
        } else {
            log_warning("[diagnostics] the decal table did not resolve, the DRAW census will "
                        "report whether the drawer RUNS but not what it holds");
        }
        installed += diag_install_observer(sites, SITE_DRAW_POLY_DECALS, &flow_state.draw_decals,
                                           (const void *)hook_draw_poly_decals,
                                           DRAW_POLY_DECALS_PROLOGUE,
                                           "every decal DRAW, and whether the texture page is there")
                     ? 1 : 0;
        installed += diag_install_observer(sites, SITE_SELECT_CEL, &flow_state.select_cel,
                                           (const void *)hook_select_cel, SELECT_CEL_PROLOGUE,
                                           "materials the decal drawer REFUSES (that call site only)")
                     ? 1 : 0;
    }
    return installed;
}
