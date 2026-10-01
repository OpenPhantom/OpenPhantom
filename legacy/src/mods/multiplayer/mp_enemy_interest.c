/* mp_enemy_interest.c: the world the enemy block's interest rule reads. See the header. */
#include "mp_enemy_interest.h"

#include "mp_bank.h"
#include "mp_body.h"
#include "mp_bridge_far.h"
#include "mp_cells.h"
#include "mp_enemy_interest_rule.h"
#include "mp_enemy_sync.h"
#include "mp_interp.h"
#include "mp_placements.h"
#include "mp_range_gate.h"
#include "mp_session.h"
#include "mp_wire.h"

#include "common/ini.h"
#include "common/logging.h"
#include "common/memory.h"

#include <windows.h>

#include <math.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

/* The enemy actor, by the four fields read here. The placement pointer is the one the entity loop
 * reads the keep radius through; the position is what that loop measures; the target is the body
 * every shot, swing and parry of the actor goes at, which the target resolver also writes when it
 * hands a far player over; the tracked shot is its last bolt while that bolt lives, cleared by the
 * bolt itself when it ends. */
#define ACTOR_PLACEMENT    0x10u
#define ACTOR_POS          0xD0u
#define ACTOR_TRACKED_SHOT 0x1ECu
#define ACTOR_TARGET       0x200u

/* Where view_distance_fix keeps its scale, and the range it holds the scale to. */
#define VIEW_DISTANCE_DLL     "view_distance_fix.dll"
#define VIEW_DISTANCE_SECTION "view_distance_fix"
#define NPC_RANGE_SCALE_MIN   1.0f
#define NPC_RANGE_SCALE_MAX   2.0f

/* What was read for one view, for the report. */
typedef struct view_seen {
    uint32_t from_puppet;
    uint32_t no_position;
    uint32_t gate_off;       /* of those, with the range gate not measuring at all */
    uint32_t newest_seen;
    float    newest_most;    /* how far the newest state stood ahead of the puppet */
    double   newest_sum;
} view_seen_t;

/* One placement's radii as first read in a level, to tell a script's change from the record. */
typedef struct radii_seen {
    uintptr_t record;
    float     wake;
    float     keep;
    bool      changed;
} radii_seen_t;

typedef struct interest_state {
    bool         scale_known;
    float        scale;
    const char  *scale_why;
    view_seen_t  views[MP_ENEMY_SYNC_VIEWS];
    radii_seen_t radii[MP_ENEMY_SYNC_MAX_PLACEMENTS];
    uintptr_t    world;         /* the level the radii were read in */
    uint32_t     unreadable;    /* live keys whose actor or record did not read */
} interest_state_t;

static interest_state_t interest;

/* The wake radius scale the activation site uses. Worked out at the first read of a session
 * rather than at arming, because the range gate that can see the call is installed after the
 * enemies are made one world. */
static void settle_scale(void)
{
    float scale;

    if (interest.scale_known) {
        return;
    }
    interest.scale_known = true;
    interest.scale       = 1.0f;
    if (GetModuleHandleA(VIEW_DISTANCE_DLL) == NULL) {
        interest.scale_why = "view_distance_fix is not loaded";
    } else {
        scale = ini_read_float(VIEW_DISTANCE_SECTION, "NpcRangeScale", 1.0f);
        if (!(scale >= NPC_RANGE_SCALE_MIN)) {
            scale = NPC_RANGE_SCALE_MIN;
        }
        if (scale > NPC_RANGE_SCALE_MAX) {
            scale = NPC_RANGE_SCALE_MAX;
        }
        if (scale <= NPC_RANGE_SCALE_MIN) {
            interest.scale_why = "view_distance_fix's NpcRangeScale is 1";
        } else if (!mp_range_gate_wake_redirected()) {
            interest.scale_why = "view_distance_fix's NpcRangeScale is set, and the activation "
                                 "scan's call is not redirected, so nothing scales it";
        } else {
            interest.scale     = scale;
            interest.scale_why = "view_distance_fix's NpcRangeScale, and the activation scan's "
                                 "call is redirected";
        }
    }
    log_info("the enemies each peer is sent are chosen by the interest rule, which measures the "
             "wake radius scaled by %.2f: %s", (double)interest.scale, interest.scale_why);
}

static bool viewer(size_t view, mp_enemy_viewer_t *out)
{
    view_seen_t   *seen;
    mp_wire_body_t newest;
    uint8_t        slot;
    size_t         bank;

    if (view >= MP_ENEMY_SYNC_VIEWS || out == NULL) {
        return false;
    }
    seen      = &interest.views[view];
    slot      = mp_session_slot_of_peer(view);
    bank      = mp_bridge_far_bank_of_slot(slot);
    out->slot = slot;
    if (bank == 0u || !mp_range_gate_player(bank, out->position)) {
        ++seen->no_position;
        seen->gate_off += mp_range_gate_measuring() ? 0u : 1u;
        return false;
    }
    out->placed = true;
    ++seen->from_puppet;
    if (mp_body_exists_at(bank)) {
        (void)mp_bank_read_at(bank, MP_HERO_BLOCK_OBJECT, &out->body, sizeof out->body);
    }
    /* How far the player's own newest state stands from the puppet the gate measured: what the
     * choice of the puppet costs, measured rather than assumed. */
    if (mp_interp_newest(mp_bridge_far_interp(bank), &newest, NULL)) {
        float dx = newest.position[0] - out->position[0];
        float dy = newest.position[1] - out->position[1];
        float dz = newest.position[2] - out->position[2];
        float d  = sqrtf(dx * dx + dy * dy + dz * dz);

        ++seen->newest_seen;
        seen->newest_sum += (double)d;
        if (d > seen->newest_most) {
            seen->newest_most = d;
        }
    }
    return true;
}

/* The radii of one placement as this level began with them, and whether a script moved them. */
static void note_radii(size_t key, uintptr_t record, const mp_placement_reach_t *reach)
{
    radii_seen_t *seen = &interest.radii[key];
    uintptr_t     world = 0;
    uint32_t      count = 0;
    uint32_t      table = 0;

    if (mp_placements_table(&world, &count, &table) && world != interest.world) {
        memset(interest.radii, 0, sizeof interest.radii);
        interest.world = world;
    }
    if (seen->record != record) {
        seen->record  = record;
        seen->wake    = reach->wake;
        seen->keep    = reach->keep;
        seen->changed = false;
    } else if (seen->wake != reach->wake || seen->keep != reach->keep) {
        seen->changed = true;
    }
}

static bool subject(size_t key, uintptr_t actor, mp_enemy_subject_t *out)
{
    uint32_t             placement = 0;
    uint32_t             shot      = 0;
    mp_placement_reach_t reach;

    if (out == NULL) {
        return false;
    }
    settle_scale();
    if (actor == 0u || !memory_try_read(actor + ACTOR_POS, out->position, sizeof out->position) ||
        !memory_try_read(actor + ACTOR_TARGET, &out->target, sizeof out->target) ||
        !memory_try_read(actor + ACTOR_TRACKED_SHOT, &shot, sizeof shot)) {
        ++interest.unreadable;
        return false;
    }
    out->shooting = shot != 0u;
    /* A copy an editor spawned has no placement of the level, and is ranked in the middle. */
    if (mp_wire_key_is_copy((uint32_t)key)) {
        out->read = true;
        return true;
    }
    if (key >= MP_ENEMY_SYNC_MAX_PLACEMENTS ||
        !memory_try_read(actor + ACTOR_PLACEMENT, &placement, sizeof placement) ||
        !mp_placements_reach_at((uintptr_t)placement, &reach)) {
        ++interest.unreadable;
        return false;
    }
    note_radii(key, (uintptr_t)placement, &reach);
    out->has_placement = true;
    memcpy(out->placement, reach.position, sizeof out->placement);
    out->keep = reach.keep;
    out->wake = mp_enemy_interest_wake(reach.wake, reach.keep, interest.scale);
    out->read = true;
    return true;
}

static int compare_floats(const void *a, const void *b)
{
    float lhs = *(const float *)a;
    float rhs = *(const float *)b;

    return (lhs > rhs) - (lhs < rhs);
}

/* The smallest, the middle and the largest of `count` values, sorted in place. */
static void spread(float *values, size_t count, float out[3])
{
    qsort(values, count, sizeof values[0], &compare_floats);
    out[0] = values[0];
    out[1] = values[count / 2u];
    out[2] = values[count - 1u];
}

static void report_radii(void)
{
    float    wake[MP_ENEMY_SYNC_MAX_PLACEMENTS];
    float    keep[MP_ENEMY_SYNC_MAX_PLACEMENTS];
    float    wakes[3];
    float    keeps[3];
    size_t   count   = 0;
    uint32_t no_test = 0;
    uint32_t changed = 0;
    size_t   i;

    for (i = 0; i < MP_ENEMY_SYNC_MAX_PLACEMENTS; ++i) {
        const radii_seen_t *seen = &interest.radii[i];

        if (seen->record == 0u) {
            continue;
        }
        wake[count] = seen->wake;
        keep[count] = seen->keep;
        ++count;
        no_test += seen->wake == 0.0f ? 1u : 0u;
        changed += seen->changed ? 1u : 0u;
    }
    if (count == 0u) {
        return;
    }
    spread(wake, count, wakes);
    spread(keep, count, keeps);
    log_info("the placements' own radii: wake min %.1f, median %.1f, most %.1f; keep min %.1f, "
             "median %.1f, most %.1f; %u with no distance test, %u changed by a script since the "
             "level began, over %u placement(s) read; woken at %.2f times the authored radius "
             "(%s); %u live key(s) whose actor or record did not read",
             (double)wakes[0], (double)wakes[1], (double)wakes[2], (double)keeps[0],
             (double)keeps[1], (double)keeps[2], (unsigned)no_test, (unsigned)changed,
             (unsigned)count, (double)interest.scale,
             interest.scale_why != NULL ? interest.scale_why : "not asked yet",
             (unsigned)interest.unreadable);
}

static void report(void)
{
    size_t view;

    for (view = 0; view < MP_ENEMY_SYNC_VIEWS; ++view) {
        const view_seen_t *seen = &interest.views[view];

        if (seen->from_puppet + seen->no_position == 0u) {
            continue;   /* never asked: no peer on that seat, or this side is a client */
        }
        log_info("where the host measures each player: peer %u (slot %u) read %u time(s) from "
                 "the puppet, %u time(s) with no position (%u of them with the range gate not "
                 "measuring); the newest state was ahead of the puppet by %.2f u at the most, "
                 "%.2f on average",
                 (unsigned)view, (unsigned)mp_session_slot_of_peer(view),
                 (unsigned)seen->from_puppet, (unsigned)seen->no_position,
                 (unsigned)seen->gate_off, (double)seen->newest_most,
                 seen->newest_seen != 0u ? seen->newest_sum / (double)seen->newest_seen : 0.0);
    }
    report_radii();
}

/* The view a hit on the player of world slot `slot` belongs to: that slot's peer, by the one slot
 * rule the hit relay addressed the hit by and `viewer` above reads the other way round. */
static bool view_of_slot(uint8_t slot, size_t *view)
{
    return mp_session_peer_of_slot(slot, view) && *view < MP_ENEMY_SYNC_VIEWS;
}

static const mp_enemy_sync_interest_t SOURCE = { &viewer, &subject, &report, &view_of_slot };

void mp_enemy_interest_set_armed(bool armed)
{
    interest.scale_known = false;
    mp_enemy_sync_set_interest(armed ? &SOURCE : NULL);
}
