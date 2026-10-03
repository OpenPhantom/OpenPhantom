/* mp_scene_bind.c: the engine the host's scene reads and calls. See the header. */
#include "mp_scene_bind.h"

#include "mp_bank.h"
#include "mp_cells.h"
#include "mp_cutscene.h"
#include "mp_respawn.h"
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
               "the scene reads the same hero window the campaign mirror keeps apart");

/* fxfade_startTintOpaque (kind, seconds, hold, r, g, b), cdecl: the call in the player task pushes
 * six dwords and clears 0x18. The three colour bytes are pushed as dwords and read as bytes. Kind 2
 * ramps to the colour and kind 1 back from it; a hold of 1 keeps the colour once the ramp is done,
 * until the next tint starts. */
typedef void(__cdecl *tint_fn_t)(int32_t kind, float seconds, int32_t hold, int32_t red,
                                 int32_t green, int32_t blue);

/* control_setState (mode), cdecl, the one writer of the input mode. */
typedef void(__cdecl *set_mode_fn_t)(int32_t mode);

/* A line each time the input mode is given back, this many in a process. */
#define MODE_LINES_MAX 32u

#define TINT_BACK_IN    1
#define TINT_OUT_TO     2
#define TINT_HOLD       1
#define TINT_LET_GO     0

/* The player record's mode descriptor, the pointer every phase of the player is run from. */
#define PR_MODE_DESCRIPTOR 0x60u

/* The mode table, fourteen descriptors and a nought behind them, in the order the engine keeps:
 * stand at 0, sabre at 3, Panaka at 4, death at 12 and the tripod gun at 13. */
#define MODE_TABLE_STAND   0u
#define MODE_TABLE_SABRE   3u
#define MODE_TABLE_PANAKA  4u
#define MODE_TABLE_DEATH   12u
#define MODE_TABLE_GUN     13u
#define MODE_TABLE_ENTRIES 15u

typedef struct bind_state {
    bool      installed;
    bool      fade_bound;
    bool      modes_bound;
    tint_fn_t tint;
    uintptr_t tint_done;       /* g_tintExpired */
    uintptr_t parkable[3];     /* stand, sabre, Panaka */
    uintptr_t death;
    bool      gun_bound;
    uintptr_t gun;
    uintptr_t table;
    uintptr_t status_cell;
    uintptr_t menu_cell;       /* the menu on show, or nought */
    bool      mode_bound;
    set_mode_fn_t set_mode;    /* control_setState */
    uintptr_t mode_cell;       /* the input mode it writes */
    bool      menu_seen;       /* a menu was seen open, and its close not looked at yet */
    uint32_t  modes_given_back;
    uint32_t  mode_lines;
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

/* The tripod gun's mode, out of the table both the save and the restore name, the table proved by
 * the four modes this file knows already standing where the table puts them and by the nought
 * that ends it. Without it a gun cannot be told from any other mode, and the hard way is off.
 *
 * In the retail image the table is g_plrModeTable at 0x004B54B0, named by player_save 0x004479D2
 * and player_restore 0x00447AB1: stand 0x004B5260 at 0, sabre 0x004B52C8 at 3, Panaka 0x004B52E8
 * at 4, death 0x004B5448 at 12, the tripod gun 0x004B5480 at 13, and a nought at 14. The gun's
 * descriptor is set only by Plr_EnterTripodGun 0x00450457; its way out restores the camera and
 * the turret, which a respawn would leave as they are. */
static bool bind_the_gun(void)
{
    uintptr_t save    = signature_find_unique(SIG_SCENE_MODE_TABLE_SAVE, MSK_SCENE_MODE_TABLE_SAVE,
                                              sizeof SIG_SCENE_MODE_TABLE_SAVE);
    uintptr_t restore = signature_find_unique(SIG_SCENE_MODE_TABLE_RESTORE,
                                              MSK_SCENE_MODE_TABLE_RESTORE,
                                              sizeof SIG_SCENE_MODE_TABLE_RESTORE);
    uint32_t  named   = 0u;
    uint32_t  again   = 0u;
    uint32_t  entry[MODE_TABLE_ENTRIES];

    if (!bind.modes_bound || save == 0u || restore == 0u ||
        !memory_read_u32(save + SCENE_MODE_TABLE_SAVE_OPERAND, &named) ||
        !memory_read_u32(restore + SCENE_MODE_TABLE_RESTORE_OPERAND, &again) || named == 0u ||
        named != again || !memory_try_read((uintptr_t)named, entry, sizeof entry)) {
        return false;
    }
    if ((uintptr_t)entry[MODE_TABLE_STAND] != bind.parkable[0] ||
        (uintptr_t)entry[MODE_TABLE_SABRE] != bind.parkable[1] ||
        (uintptr_t)entry[MODE_TABLE_PANAKA] != bind.parkable[2] ||
        (uintptr_t)entry[MODE_TABLE_DEATH] != bind.death || entry[MODE_TABLE_GUN] == 0u ||
        entry[MODE_TABLE_ENTRIES - 1u] != 0u) {
        return false;
    }
    bind.table = (uintptr_t)named;
    bind.gun   = (uintptr_t)entry[MODE_TABLE_GUN];
    return true;
}

/* The input mode and its setter, out of the menu's own two calls: the open asks the mode's
 * getter for the mode it remembers, the close hands that mode back through the setter, and both
 * of those load the one cell first thing. The two loads have to name the same cell, and it has
 * to lie in the image. The setter is the call that put the mode back, so it is the engine's own
 * way to set it again. */
static bool bind_the_input_mode(void)
{
    uintptr_t open     = mp_signatures_address(MP_SITE_SWMENU_OPEN);
    uintptr_t close    = mp_signatures_address(MP_SITE_SWMENU_CLOSE);
    uintptr_t get      = 0u;
    uintptr_t set      = 0u;
    uint8_t   get_load = 0u;
    uint8_t   set_load = 0u;
    uint32_t  got      = 0u;
    uint32_t  cell     = 0u;

    if (open == 0u || close == 0u ||
        !patch_read_call_target(open + SCENE_MENU_GET_MODE_CALL, &get) ||
        !patch_read_call_target(close + SCENE_MENU_SET_MODE_CALL, &set) ||
        !memory_read_u8(get + SCENE_GET_MODE_LOAD, &get_load) ||
        !memory_read_u8(set + SCENE_SET_MODE_LOAD, &set_load) ||
        get_load != SCENE_LOAD_EAX_OPCODE || set_load != SCENE_LOAD_EAX_OPCODE ||
        !memory_read_u32(get + SCENE_GET_MODE_LOAD + 1u, &got) ||
        !memory_read_u32(set + SCENE_SET_MODE_LOAD + 1u, &cell) || cell == 0u || got != cell ||
        !memory_is_inside_image((uintptr_t)cell, sizeof(int32_t))) {
        return false;
    }
    bind.set_mode  = (set_mode_fn_t)set;
    bind.mode_cell = (uintptr_t)cell;
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
    bind.gun_bound   = bind_the_gun();
    bind.status_cell = mp_cells_address(MP_CELL_PLR_STATUS_POINTER);
    bind.menu_cell   = mp_cells_address(MP_CELL_CURRENT_MENU);
    bind.mode_bound  = bind.menu_cell != 0u && bind_the_input_mode();
    if (bind.mode_bound) {
        log_info("the input mode after a menu is bound: the mode's cell at %08X and its setter "
                 "at %08X, both named by the menu's own calls", (unsigned)bind.mode_cell,
                 (unsigned)(uintptr_t)bind.set_mode);
    } else {
        log_warning("the input mode after a menu is not bound: the menu on show or the menu's own "
                    "calls to the mode's getter and setter did not resolve to one cell, so a menu "
                    "that closes over a lock which fell while it was open can leave the player "
                    "standing");
    }
    bind_the_rest();
    if (!bind.gun_bound) {
        log_warning("the hard way of a scene is off: the tripod gun's mode did not resolve out of "
                    "the mode table, so a player in a mode the teleport may not move is waited "
                    "for and never brought to its seat by the engine's respawn");
    } else {
        log_info("the hard way of a scene is bound: the tripod gun's mode is %08X, the 14th of "
                 "the mode table at %08X that the save and the restore both name, its stand, "
                 "sabre, Panaka and death where the grab's compares found them",
                 (unsigned)bind.gun, (unsigned)bind.table);
    }
    if (!mp_scene_bind_ready()) {
        log_warning("the host's way to a scene is not bound: %s%s%s, so no scene a far player "
                    "sets off becomes the host's, and nobody is moved by a teleport",
                    bind.fade_bound ? "" : "the fade in the player's respawn did not resolve",
                    (!bind.fade_bound && !bind.modes_bound) ? " and " : "",
                    bind.modes_bound ? ""
                                     : "the modes the grab parks a player from did not resolve");
        return false;
    }
    log_info("the host's way to a scene is bound: the fade at %08X ends in the cell at %08X, a "
             "player is moved only out of the three modes the engine parks for a scene (%08X %08X "
             "%08X), and the host is brought to a scene a far player sets off before it runs",
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

bool mp_scene_bind_fade_sites(uintptr_t *start_routine, uintptr_t *done_cell)
{
    if (!bind.fade_bound || start_routine == NULL || done_cell == NULL) {
        return false;
    }
    *start_routine = (uintptr_t)bind.tint;
    *done_cell     = bind.tint_done;
    return true;
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
    if (bind.gun_bound && (uintptr_t)descriptor == bind.gun) {
        return MP_SCENE_MODE_GUN;
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

bool mp_scene_bind_mode_is_gun(void)
{
    return mode_of(own_record()) == MP_SCENE_MODE_GUN;
}

/* The health in the active status record, the record of this machine's own hero only while no
 * bank window is open. */
static bool own_health(int32_t *health)
{
    uint32_t record = 0u;

    return bind.status_cell != 0u && mp_bank_active() == 0u &&
           memory_try_read_u32(bind.status_cell, &record) && record != 0u &&
           memory_try_read((uintptr_t)record, health, sizeof *health);
}

bool mp_scene_bind_hard_way_open(void)
{
    uintptr_t       record = own_record();
    uint32_t        module = 0u;
    uint32_t        body   = 0u;
    int32_t         health = 0;
    mp_scene_mode_t mode;

    if (!bind.gun_bound || record == 0u || !mp_respawn_installed() || mp_respawn_pending()) {
        return false;
    }
    mode = mode_of(record);
    return memory_try_read_u32(record + MP_HERO_BLOCK_MODULE_STATE, &module) &&
           module == MP_HERO_MODULE_RUNNING &&
           memory_try_read_u32(record + MP_HERO_BLOCK_OBJECT, &body) && body != 0u &&
           mode != MP_SCENE_MODE_UNREAD && mode != MP_SCENE_MODE_DEATH &&
           mode != MP_SCENE_MODE_GUN && own_health(&health) && health > 0;
}

bool mp_scene_bind_module_state(uint32_t *state)
{
    uintptr_t record = own_record();

    return record != 0u && state != NULL &&
           memory_try_read_u32(record + MP_HERO_BLOCK_MODULE_STATE, state);
}

/* The module, its store and the body. The defaults stand for a refused read, and a read that
 * faulted part way may have left bytes behind, so each refusal puts its default back. */
static void read_the_module(uint32_t *module, uint32_t *saved, uint32_t *body)
{
    uintptr_t record = own_record();

    *module = 1u;
    *saved  = 0u;
    *body   = 0u;
    if (record == 0u) {
        return;
    }
    if (!memory_try_read_u32(record + MP_HERO_BLOCK_MODULE_STATE, module)) {
        *module = 1u;
    }
    if (!memory_try_read_u32(record + MP_HERO_BLOCK_SAVED_MODULE_STATE, saved)) {
        *saved = 0u;
    }
    if (!memory_try_read_u32(record + MP_HERO_BLOCK_OBJECT, body)) {
        *body = 0u;
    }
}

void mp_scene_bind_module_cells(uint32_t *module, uint32_t *store, bool *has_body)
{
    uint32_t state;
    uint32_t saved;
    uint32_t body;

    read_the_module(&state, &saved, &body);
    if (module != NULL) {
        *module = state;
    }
    if (store != NULL) {
        *store = saved;
    }
    if (has_body != NULL) {
        *has_body = body != 0u;
    }
}

bool mp_scene_bind_running(void)
{
    uint32_t module;
    uint32_t saved;
    uint32_t body;

    read_the_module(&module, &saved, &body);
    return mp_scene_running(mp_cutscene_lock_level(), module, saved, body != 0u);
}

/* The same rule asked with no lock, which leaves its module half. */
bool mp_scene_bind_parked(void)
{
    uint32_t module;
    uint32_t saved;
    uint32_t body;

    read_the_module(&module, &saved, &body);
    return mp_scene_running(0, module, saved, body != 0u);
}

bool mp_scene_bind_menu_open(void)
{
    uint32_t menu = 0u;

    return bind.menu_cell != 0u && memory_try_read_u32(bind.menu_cell, &menu) && menu != 0u;
}

bool mp_scene_bind_menu_known(void)
{
    return bind.menu_cell != 0u;
}

bool mp_scene_bind_gun_known(void)
{
    return bind.gun_bound;
}

void mp_scene_bind_after_a_menu(void)
{
    int32_t mode = -1;

    if (!bind.mode_bound) {
        return;
    }
    if (!memory_try_read(bind.mode_cell, &mode, sizeof mode)) {
        mode = -1;
    }
    if (!mp_scene_menu_left_the_input_held(&bind.menu_seen, mp_scene_bind_menu_open(),
                                           mp_cutscene_lock_level(), mode)) {
        return;
    }
    bind.set_mode(MP_SCENE_INPUT_MODE_PLAY);
    ++bind.modes_given_back;
    if (bind.mode_lines < MODE_LINES_MAX) {
        ++bind.mode_lines;
        log_info("a menu of the engine closed over a lock that fell while it was open, and put "
                 "back the input mode %d it had found with the lock at nought; the mode is given "
                 "back as %d, so the player is not left standing", (int)mode,
                 (int)MP_SCENE_INPUT_MODE_PLAY);
    }
}

int32_t mp_scene_bind_input_mode(void)
{
    int32_t mode = -1;

    if (!bind.mode_bound || !memory_try_read(bind.mode_cell, &mode, sizeof mode)) {
        return -1;
    }
    return mode;
}

/* control_setState 0x004658C1 loads the mode's cell and compares it with its argument first
 * thing, and leaves at once when they are equal:
 *
 *   004658C1  55 8B EC 51           push ebp; mov ebp,esp; push ecx
 *   004658C5  A1 5C 5D 6D 00        mov eax, [0x006D5D5C]
 *   004658CA  3B 45 08 / 75 02      cmp eax, [ebp+8]; jne on
 *   004658CF  EB 4A                 jmp out
 *
 * Otherwise it closes the input device, drops every binding and builds the set of the new mode,
 * so it is called only where the mode has to change. */
bool mp_scene_bind_set_play(void)
{
    if (!bind.mode_bound) {
        return false;
    }
    bind.set_mode(MP_SCENE_INPUT_MODE_PLAY);
    return true;
}

void mp_scene_bind_report(void)
{
    log_info("  the input after a menu: %u time(s) a menu of the engine closed over a lock that "
             "fell while it was open, and the input mode was given back%s",
             (unsigned)bind.modes_given_back, bind.mode_bound ? "" : " (not bound)");
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
