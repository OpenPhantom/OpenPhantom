/* mp_scene_bind.c: the engine a scene's gathering reads and calls. See the header. */
#include "mp_scene_bind.h"

#include "mp_bank.h"
#include "mp_cells.h"
#include "mp_cutscene.h"
#include "mp_scratch.h"
#include "mp_signatures.h"
#include "mp_signatures_scene.h"
#include "mp_world_anchor.h"

#include "common/logging.h"
#include "common/memory.h"
#include "common/patch.h"
#include "common/signature.h"

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <string.h>

_Static_assert(MP_SCENE_QUEST_WINDOW_BYTES == MP_SCRATCH_HERO_BYTES,
               "the gathering reads the same hero window the campaign mirror keeps apart");

/* fxfade_startTintOpaque (kind, seconds, hold, r, g, b), cdecl: the call in the player task pushes
 * six dwords and clears 0x18. The three colour bytes are pushed as dwords and read as bytes. Kind 2
 * ramps to the colour and kind 1 back from it; a hold of 1 keeps the colour once the ramp is done,
 * until the next tint starts. */
typedef void(__cdecl *tint_fn_t)(int32_t kind, float seconds, int32_t hold, int32_t red,
                                 int32_t green, int32_t blue);

#define TINT_BACK_IN    1
#define TINT_OUT_TO     2
#define TINT_HOLD       1
#define TINT_LET_GO     0

/* The player record's mode descriptor, the pointer every phase of the player is run from. */
#define PR_MODE_DESCRIPTOR 0x60u

typedef struct bind_state {
    bool      installed;
    bool      fade_bound;
    bool      modes_bound;
    tint_fn_t tint;
    uintptr_t tint_done;       /* g_tintExpired */
    uintptr_t parkable[3];     /* stand, sabre, Panaka */
    uintptr_t death;
    uintptr_t pr_cell;
    uintptr_t story;
    uintptr_t swap_return;
} bind_state_t;

static bind_state_t bind;

/* ==============================================================================================
 * Installation.
 * ============================================================================================ */

/* The fade and its cell, out of the player task's respawn arms, and the player record they name
 * held against the cell's own rows. */
static bool bind_the_fade(void)
{
    uintptr_t site = signature_find_unique(SIG_SCENE_TINT_RESPAWN, MSK_SCENE_TINT_RESPAWN,
                                           sizeof SIG_SCENE_TINT_RESPAWN);
    uintptr_t fade = 0u;
    uint32_t  done = 0u;
    uint32_t  pr   = 0u;

    if (site == 0u || !patch_read_call_target(site + SCENE_TINT_CALL, &fade) ||
        !memory_read_u32(site + SCENE_TINT_DONE_OPERAND, &done) || done == 0u ||
        !memory_read_u32(site + SCENE_TINT_PR_OPERAND, &pr) || (uintptr_t)pr != bind.pr_cell) {
        return false;
    }
    bind.tint      = (tint_fn_t)fade;
    bind.tint_done = (uintptr_t)done;
    return true;
}

/* The three modes the grab parks a player from, and its stand compared with the stand the cells
 * already know: two readings of one descriptor that have to agree. */
static bool bind_the_modes(void)
{
    uintptr_t site = signature_find_unique(SIG_SCENE_SUSPEND_MODES, MSK_SCENE_SUSPEND_MODES,
                                           sizeof SIG_SCENE_SUSPEND_MODES);
    uint32_t  stand  = 0u;
    uint32_t  sabre  = 0u;
    uint32_t  panaka = 0u;
    uint32_t  pr     = 0u;

    bind.death = mp_cells_address(MP_CELL_MODE_DEATH_DESC);
    if (site == 0u || bind.death == 0u ||
        !memory_read_u32(site + SCENE_MODES_STAND_OPERAND, &stand) ||
        !memory_read_u32(site + SCENE_MODES_SABRE_OPERAND, &sabre) ||
        !memory_read_u32(site + SCENE_MODES_PANAKA_OPERAND, &panaka) ||
        !memory_read_u32(site + SCENE_MODES_PR_OPERAND, &pr) || (uintptr_t)pr != bind.pr_cell ||
        (uintptr_t)stand != mp_cells_address(MP_CELL_MODE_STAND_DESC) || stand == 0u ||
        sabre == 0u || panaka == 0u) {
        return false;
    }
    bind.parkable[0] = (uintptr_t)stand;
    bind.parkable[1] = (uintptr_t)sabre;
    bind.parkable[2] = (uintptr_t)panaka;
    return true;
}

/* The story cells, and the hero swap's call to the respawn, proved by naming the respawn this
 * feature resolved. */
static void bind_the_rest(void)
{
    uintptr_t swap  = mp_signatures_address(MP_SITE_HERO_SWAP);
    uintptr_t named = 0u;

    bind.story = mp_cells_address(MP_CELL_STORY_FLAGS);
    if (swap != 0u &&
        patch_read_call_target(swap + SCENE_HERO_SWAP_RESPAWN_RETURN - 5u, &named) &&
        named == mp_signatures_address(MP_SITE_PLAYER_RESPAWN_AT)) {
        bind.swap_return = swap + SCENE_HERO_SWAP_RESPAWN_RETURN;
    }
}

bool mp_scene_bind_install(void)
{
    if (bind.installed) {
        return mp_scene_bind_ready();
    }
    bind.installed   = true;
    bind.pr_cell     = mp_cells_address(MP_CELL_PR);
    bind.fade_bound  = bind.pr_cell != 0u && bind_the_fade();
    bind.modes_bound = bind.pr_cell != 0u && bind_the_modes();
    bind_the_rest();
    if (!mp_scene_bind_ready()) {
        log_warning("the scene gathering is not bound: %s%s%s, so a scene of the host's runs where "
                    "each player stands, as before",
                    bind.fade_bound ? "" : "the fade in the player's respawn did not resolve",
                    (!bind.fade_bound && !bind.modes_bound) ? " and " : "",
                    bind.modes_bound ? ""
                                     : "the modes the grab parks a player from did not resolve");
        return false;
    }
    log_info("the scene gathering is bound: the fade at %08X ends in the cell at %08X, a player is "
             "moved only out of the three modes the engine parks for a scene (%08X %08X %08X), and "
             "a scene of the host's gathers every player before it runs",
             (unsigned)(uintptr_t)bind.tint, (unsigned)bind.tint_done,
             (unsigned)bind.parkable[0], (unsigned)bind.parkable[1], (unsigned)bind.parkable[2]);
    return true;
}

bool mp_scene_bind_ready(void)
{
    return bind.fade_bound && bind.modes_bound;
}

/* ==============================================================================================
 * The fade.
 * ============================================================================================ */

void mp_scene_bind_fade_out(float seconds)
{
    if (bind.fade_bound) {
        bind.tint(TINT_OUT_TO, seconds, TINT_HOLD, 0, 0, 0);
    }
}

void mp_scene_bind_fade_in(float seconds)
{
    if (bind.fade_bound) {
        bind.tint(TINT_BACK_IN, seconds, TINT_LET_GO, 0, 0, 0);
    }
}

bool mp_scene_bind_fade_done(void)
{
    uint32_t done = 0u;

    return bind.fade_bound && memory_try_read_u32(bind.tint_done, &done) && done == 1u;
}

/* ==============================================================================================
 * The player.
 * ============================================================================================ */

/* The player record, but only with bank 0 in place: inside a bank window it is a far body's. */
static uintptr_t own_record(void)
{
    uint32_t record = 0u;

    if (bind.pr_cell == 0u || mp_bank_active() != 0u ||
        !memory_try_read_u32(bind.pr_cell, &record)) {
        return 0u;
    }
    return (uintptr_t)record;
}

static mp_scene_mode_t mode_of(uintptr_t record)
{
    uint32_t descriptor = 0u;
    size_t   i;

    if (!bind.modes_bound || record == 0u ||
        !memory_try_read_u32(record + PR_MODE_DESCRIPTOR, &descriptor)) {
        return MP_SCENE_MODE_UNREAD;
    }
    if ((uintptr_t)descriptor == bind.death) {
        return MP_SCENE_MODE_DEATH;
    }
    for (i = 0u; i < 3u; ++i) {
        if ((uintptr_t)descriptor == bind.parkable[i]) {
            return MP_SCENE_MODE_PARKABLE;
        }
    }
    return MP_SCENE_MODE_OTHER;
}

mp_scene_move_t mp_scene_bind_may_move(void)
{
    uintptr_t record = own_record();
    uint32_t  module = 0u;
    uint32_t  body   = 0u;

    if (record == 0u) {
        return MP_SCENE_MOVE_NO_BODY;
    }
    /* Both are used whether they read or not, and a read that faulted part way may have left
     * bytes behind, so a refusal puts the default back. */
    if (!memory_try_read_u32(record + MP_HERO_BLOCK_MODULE_STATE, &module)) {
        module = 0u;
    }
    if (!memory_try_read_u32(record + MP_HERO_BLOCK_OBJECT, &body)) {
        body = 0u;
    }
    return mp_scene_may_move(body != 0u, module == MP_HERO_MODULE_RUNNING,
                             mp_scene_bind_player_stands(), mode_of(record));
}

bool mp_scene_bind_player_stands(void)
{
    bool known  = false;
    bool stands = mp_world_anchor_player_stands(&known);

    return known ? stands : mode_of(own_record()) != MP_SCENE_MODE_DEATH;
}

bool mp_scene_bind_module_state(uint32_t *state)
{
    uintptr_t record = own_record();

    return record != 0u && state != NULL &&
           memory_try_read_u32(record + MP_HERO_BLOCK_MODULE_STATE, state);
}

bool mp_scene_bind_running(void)
{
    uintptr_t record = own_record();
    uint32_t  module = 1u;
    uint32_t  saved  = 0u;
    uint32_t  body   = 0u;

    /* The defaults above stand for a refused read, and a read that faulted part way may have left
     * bytes behind, so each refusal puts its default back. */
    if (record != 0u) {
        if (!memory_try_read_u32(record + MP_HERO_BLOCK_MODULE_STATE, &module)) {
            module = 1u;
        }
        if (!memory_try_read_u32(record + MP_HERO_BLOCK_SAVED_MODULE_STATE, &saved)) {
            saved = 0u;
        }
        if (!memory_try_read_u32(record + MP_HERO_BLOCK_OBJECT, &body)) {
            body = 0u;
        }
    }
    return mp_scene_running(mp_cutscene_lock_level(), module, saved, body != 0u);
}

bool mp_scene_bind_own_pose(float position[3], float *heading)
{
    uintptr_t record = own_record();

    return record != 0u && position != NULL && heading != NULL &&
           memory_try_read(record + MP_HERO_BLOCK_POS, position, 3u * sizeof(float)) &&
           memory_try_read(record + MP_HERO_BLOCK_HEADING, heading, sizeof *heading);
}

bool mp_scene_bind_quest_window(uint8_t out[MP_SCENE_QUEST_WINDOW_BYTES])
{
    return bind.story != 0u && out != NULL &&
           memory_try_read(bind.story + MP_SCRATCH_HERO_FIRST, out, MP_SCENE_QUEST_WINDOW_BYTES);
}

uintptr_t mp_scene_bind_swap_return(void)
{
    return bind.swap_return;
}
