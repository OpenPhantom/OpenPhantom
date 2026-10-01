/* mp_world_anchor.c: who wakes and keeps a session's enemies. See the header. */
#include "mp_world_anchor.h"

#include "mp_armed.h"
#include "mp_bank.h"
#include "mp_body.h"
#include "mp_bridge_drain.h"
#include "mp_bridge_far.h"
#include "mp_cells.h"
#include "mp_range_gate.h"
#include "mp_session_now.h"
#include "mp_signatures.h"
#include "mp_signatures_enemy.h"
#include "mp_stopwatch.h"
#include "mp_task.h"
#include "mp_world_anchor_rule.h"

#include "common/detour.h"
#include "common/logging.h"
#include "common/memory.h"
#include "common/patch.h"

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <string.h>

/* The engine's task table: 64 records of 0x2C bytes, a record in use while its function word at
 * +0x14 is not 0. */
#define TASK_SLOTS    64u
#define TASK_STRIDE   0x2Cu
#define TASK_FUNCTION 0x14u

/* The engine's live player test: the local player's body object, or 0 while the death mode is
 * up. cdecl, no argument, the answer in eax. */
typedef uint32_t(__cdecl *live_player_fn_t)(void);

/* The activation scan: cdecl, nothing in and nothing out. */
typedef void(__cdecl *activation_scan_fn_t)(void);

/* The first few times a far body stands in for the dead host are each worth a line, because they
 * are the direct evidence the world goes on for the others; after that the report counts. */
#define STAND_IN_LOG_CAP 8u

/* The rule reads one candidate for every far bank and no more. */
_Static_assert(MP_WORLD_ANCHOR_CANDIDATES == MP_BANK_FAR_MAX, "a candidate for every far bank");

typedef enum anchor_call {
    ANCHOR_CALL_SCAN = 0,
    ANCHOR_CALL_REMOVAL,
    ANCHOR_CALL_COUNT
} anchor_call_t;

typedef struct world_anchor_state {
    bool             anchored;                   /* both calls point at the anchor */
    uintptr_t        calls[ANCHOR_CALL_COUNT];   /* the `E8` of each redirected call */
    live_player_fn_t live_player;                /* where both calls went before */

    detour_t             scan_hull;
    activation_scan_fn_t scan_original;
    bool                 scan_held;              /* the scan's hull is on */

    bool     widened_once;                       /* a call found this side a host that widens */
    bool     standing_in;                        /* the last answer was a far body */
    uint32_t stood_in[ANCHOR_CALL_COUNT];        /* a far body answered for the dead host */
    uint32_t nobody;                             /* the host dead and nobody standing: 0 */
    uint32_t stand_ins_logged;

    uint32_t scans_withheld;                     /* held on a client of a started session */
    uint32_t scans_run_in_session;               /* run on a client with a transport up */
} world_anchor_state_t;

static world_anchor_state_t anchor;

/* ==============================================================================================
 * The anchor: the two calls that ask for the living player.
 * ============================================================================================ */

/* Every far bank as the rule reads it. The pose and the word "stands" are the ones the re-entry
 * rules are fed from, so the player the world is woken by is standing in the same sense as the
 * player a dead one comes back beside. The body is read live out of the bank's block, because a
 * respawn gives the far player a new object and a handle kept from the spawn would name the old
 * one. */
static void read_candidates(mp_world_anchor_candidate_t out[MP_BANK_FAR_MAX])
{
    size_t bank;

    for (bank = 1u; bank <= MP_BANK_FAR_MAX; ++bank) {
        mp_world_anchor_candidate_t *candidate = &out[bank - 1u];
        mp_bridge_far_pose_t         pose;
        uint32_t                     body = 0u;

        memset(candidate, 0, sizeof *candidate);
        if (!mp_bridge_far_pose(bank, MP_FAR_READER_OTHER, &pose)) {
            continue;
        }
        if (mp_body_exists_at(bank) &&
            mp_bank_read_at(bank, MP_HERO_BLOCK_HACTOR, &body, sizeof body)) {
            candidate->body = body;
        }
        candidate->stands = mp_bridge_far_pose_stands(&pose);
        memcpy(candidate->position, pose.position, sizeof candidate->position);
    }
}

static void note_stand_in(size_t chosen)
{
    if (anchor.standing_in) {
        return;
    }
    anchor.standing_in = true;
    if (anchor.stand_ins_logged < STAND_IN_LOG_CAP) {
        ++anchor.stand_ins_logged;
        log_info("this player is dead, so the host's world is woken and kept by the far player "
                 "on bank %u until this one stands again", (unsigned)(chosen + 1u));
    }
}

/* The engine's own answer first, always, then the rule. A far body may only stand in while the
 * range gate widens and bank 0 is in place: inside a bank window the player record is a far
 * body's, and its answer is that body's life rather than this player's. */
static uint32_t answer_for(anchor_call_t call)
{
    mp_world_anchor_candidate_t candidates[MP_BANK_FAR_MAX];
    mp_world_anchor_answer_t    answer;
    uint32_t                    local  = anchor.live_player();
    bool                        widens = mp_range_gate_widens() && mp_bank_active() == 0u;
    float                       died_at[3];
    bool                        died_known;
    size_t                      chosen = 0u;

    anchor.widened_once = anchor.widened_once || widens;
    read_candidates(candidates);
    died_known = mp_cells_hero_position(died_at);
    answer     = mp_world_anchor_rule_pick(widens, local, candidates, MP_BANK_FAR_MAX,
                                           died_known ? died_at : NULL, &chosen);
    if (answer == MP_WORLD_ANCHOR_FAR) {
        ++anchor.stood_in[call];
        note_stand_in(chosen);
        return candidates[chosen].body;
    }
    anchor.standing_in = false;
    if (answer == MP_WORLD_ANCHOR_NOBODY) {
        ++anchor.nobody;
    }
    return local;
}

/* engine: void *player_getActorIfAlive(void) */
static uint32_t __cdecl hook_scan_anchor(void)
{
    return answer_for(ANCHOR_CALL_SCAN);
}

/* engine: void *player_getActorIfAlive(void) */
static uint32_t __cdecl hook_removal_anchor(void)
{
    return answer_for(ANCHOR_CALL_REMOVAL);
}

/* The live player test has more callers than these two, and the burst's head names it too. The
 * two operands are rewritten only when all three calls name one function: a call that names
 * another is a build or a module this was not measured against. The target is stored before the
 * first operand moves, and the first is put back if the second will not move, because a scan that
 * wakes for a far body while the removal still reads the dead host's nothing would keep what it
 * woke for good. */
static bool anchor_the_two_calls(void)
{
    uintptr_t scan    = mp_signatures_address(MP_SITE_ENEMY_ACTIVATION_SCAN);
    uintptr_t tick    = mp_signatures_address(MP_SITE_ENEMY_TICK_ANCHOR_CALL);
    uintptr_t shatter = mp_signatures_address(MP_SITE_SHOT_SHATTER_THING);
    uintptr_t named[3] = { 0u, 0u, 0u };

    if (anchor.anchored) {
        return true;
    }
    if (scan == 0u || tick == 0u || shatter == 0u) {
        log_warning("the world's anchor is not bound: the activation scan, the entity loop's live "
                    "player test or the burst did not resolve, so a host whose player is dead "
                    "wakes and removes nothing for the others");
        return false;
    }
    anchor.calls[ANCHOR_CALL_SCAN]    = scan + ENEMY_SCAN_ANCHOR_CALL;
    anchor.calls[ANCHOR_CALL_REMOVAL] = tick + ENEMY_TICK_ANCHOR_CALL;
    if (!patch_read_call_target(anchor.calls[ANCHOR_CALL_SCAN], &named[0]) ||
        !patch_read_call_target(anchor.calls[ANCHOR_CALL_REMOVAL], &named[1]) ||
        !patch_read_call_target(shatter + SHOT_SHATTER_THING_ANCHOR_CALL, &named[2]) ||
        named[0] != named[1] || named[0] != named[2]) {
        log_warning("the world's anchor is not bound: the three calls of the live player test at "
                    "%08X, %08X and %08X name %08X, %08X and %08X, not one function, so a host "
                    "whose player is dead wakes and removes nothing for the others",
                    (unsigned)anchor.calls[ANCHOR_CALL_SCAN],
                    (unsigned)anchor.calls[ANCHOR_CALL_REMOVAL],
                    (unsigned)(shatter + SHOT_SHATTER_THING_ANCHOR_CALL), (unsigned)named[0],
                    (unsigned)named[1], (unsigned)named[2]);
        return false;
    }
    /* The target is an address in the host image that the three calls agree on; it is called as
     * the function it is. */
    anchor.live_player = (live_player_fn_t)named[0];
    if (patch_redirect_call(anchor.calls[ANCHOR_CALL_SCAN], (const void *)&hook_scan_anchor) !=
        PATCH_RESULT_OK) {
        log_warning("the world's anchor is not bound: the scan's call at %08X did not move",
                    (unsigned)anchor.calls[ANCHOR_CALL_SCAN]);
        return false;
    }
    if (patch_redirect_call(anchor.calls[ANCHOR_CALL_REMOVAL],
                            (const void *)&hook_removal_anchor) != PATCH_RESULT_OK) {
        /* Back to the address all three calls named, which is where the first one pointed. */
        (void)patch_redirect_call(anchor.calls[ANCHOR_CALL_SCAN], (const void *)named[0]);
        log_warning("the world's anchor is not bound: the entity loop's call at %08X did not "
                    "move, and the scan's was put back",
                    (unsigned)anchor.calls[ANCHOR_CALL_REMOVAL]);
        return false;
    }
    anchor.anchored = true;
    log_info("the activation scan and the removal pass ask the world's anchor for the living "
             "player (calls at %08X and %08X, which name %08X as the burst's does): on a host "
             "whose player is dead the standing far player nearest to where he died answers, and "
             "nobody standing answers nothing, as the engine does",
             (unsigned)anchor.calls[ANCHOR_CALL_SCAN], (unsigned)anchor.calls[ANCHOR_CALL_REMOVAL],
             (unsigned)named[0]);
    return true;
}

/* ==============================================================================================
 * The scan a client holds.
 * ============================================================================================ */

/* engine: void enemy_activationScan(void) */
static void __cdecl hook_activation_scan(void)
{
    /* The start of the engine's tasks ahead of this side's. The enemy task, the lowest slot, calls
     * the scan once right after its first three calls, on a host and on a client alike, and the
     * stopwatch ends the stage where this side's task enters the substep. Marked before the
     * client's early return, so both sides measure the same span. */
    if (mp_armed_transport()) {
        mp_stopwatch_mark(MP_WATCH_ENGINE_TASKS);
    }
    if (mp_session_now_client_of_a_started_session(NULL, NULL)) {
        ++anchor.scans_withheld;
        return;
    }
    /* The witness to the predicate above: a client with a transport up whose scan ran anyway. A
     * setup note that has not arrived yet, or a session already marked ended, would show here. */
    if (mp_armed_transport() && mp_bridge_drain_is_client()) {
        ++anchor.scans_run_in_session;
    }
    /* Not reachable while the install and the enemy tick share the engine's thread; a null call
     * would take the process down, and one scan skipped is one frame late. */
    if (anchor.scan_original == NULL) {
        return;
    }
    anchor.scan_original();
}

static bool hold_the_scan(void)
{
    uintptr_t scan = mp_signatures_address(MP_SITE_ENEMY_ACTIVATION_SCAN);

    if (anchor.scan_held) {
        return true;
    }
    if (scan == 0u) {
        log_warning("the activation scan did not resolve, so a client of a session goes on waking "
                    "enemies of its own that the host never lists");
        return false;
    }
    if (!detour_install(&anchor.scan_hull, scan, (const void *)&hook_activation_scan,
                        mp_signatures_prologue(MP_SITE_ENEMY_ACTIVATION_SCAN))) {
        log_warning("the activation scan at %08X did not take a hull, so a client of a session "
                    "goes on waking enemies of its own that the host never lists",
                    (unsigned)scan);
        return false;
    }
    anchor.scan_original = (activation_scan_fn_t)anchor.scan_hull.original;
    anchor.scan_held     = true;
    log_info("the activation scan at %08X is held on a client of a started session: every enemy "
             "there is built from what the host lists, and the scan runs again once the session "
             "is over", (unsigned)scan);
    return true;
}

/* ============================================================================================ */

bool mp_world_anchor_player_stands(bool *known)
{
    bool read = anchor.live_player != NULL && mp_bank_active() == 0u;

    if (known != NULL) {
        *known = read;
    }
    return read && anchor.live_player() != 0u;
}

bool mp_world_anchor_install(void)
{
    bool anchored = anchor_the_two_calls();
    bool held     = hold_the_scan();

    return anchored || held;
}

/* Which slot runs which task, read at a report while the stopwatch is armed. Its first stage runs
 * from the scan in the enemies' task to this side's task, the engine runs its slots lowest first,
 * and what the stage holds besides the enemies is every slot between the two; so the order is
 * read out of the table rather than assumed. -1 for a task the table does not hold. */
static void report_task_slots(void)
{
    uintptr_t table      = mp_cells_address(MP_CELL_TASK_ARRAY);
    uintptr_t enemy      = mp_signatures_address(MP_SITE_ENEMY_TICK_ALL);
    uintptr_t own        = (uintptr_t)&mp_task_tick;
    int32_t   enemy_slot = -1;
    int32_t   own_slot   = -1;
    uint32_t  used       = 0u;
    uint32_t  slot;

    for (slot = 0; table != 0u && slot < TASK_SLOTS; ++slot) {
        uint32_t function = 0u;

        if (!memory_try_read(table + slot * TASK_STRIDE + TASK_FUNCTION, &function,
                             sizeof function) ||
            function == 0u) {
            continue;
        }
        ++used;
        if (enemy != 0u && function == (uint32_t)enemy) {
            enemy_slot = (int32_t)slot;
        }
        if (function == (uint32_t)own) {
            own_slot = (int32_t)slot;
        }
    }
    log_info("  the engine's task slots at this report: the enemies' task in slot %d, this side's "
             "task in slot %d (-1 for one the table does not hold), %u of %u slot(s) in use; the "
             "stopwatch's first stage runs from the enemies' activation scan to this side's task",
             (int)enemy_slot, (int)own_slot, (unsigned)used, (unsigned)TASK_SLOTS);
}

void mp_world_anchor_report(void)
{
    if (mp_stopwatch_armed()) {
        report_task_slots();
    }
    if (anchor.anchored && (anchor.widened_once || anchor.nobody != 0u)) {
        log_info("  the world's anchor while this player was dead: %u activation scan(s) and %u "
                 "removal pass(es) ran on a far player's body, %u with nobody standing (the "
                 "engine's own answer, 0)",
                 (unsigned)anchor.stood_in[ANCHOR_CALL_SCAN],
                 (unsigned)anchor.stood_in[ANCHOR_CALL_REMOVAL], (unsigned)anchor.nobody);
    }
    if (anchor.scan_held && (mp_bridge_drain_is_client() || anchor.scans_withheld != 0u ||
                             anchor.scans_run_in_session != 0u)) {
        log_info("  the activation scan: %u substep(s) withheld because the host owns the world, "
                 "%u run here in a session",
                 (unsigned)anchor.scans_withheld, (unsigned)anchor.scans_run_in_session);
    }
}
