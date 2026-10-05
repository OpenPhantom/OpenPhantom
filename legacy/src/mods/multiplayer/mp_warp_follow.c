/* mp_warp_follow.c: a client's player follows the host when a script warps him. See the header.
 *
 * One thing about the code is worth having in front of a maintainer: the count of warps heard is
 * taken up on every look, in every phase, so that a warp heard while the teleport of the last
 * one is under way is not lost. It replaces the target at once and leaves the phase alone; the
 * end of that teleport then finds the target changed and goes back to the wait instead of
 * calling the warp followed.
 */
#include "mp_warp_follow.h"

#include "mp_bridge_far.h"
#include "mp_level_state_warp.h"
#include "mp_teleport_host.h"
#include "mp_wallclock.h"
#include "mp_warp_follow_rule.h"

#include "common/logging.h"
#include "common/player_help_note.h"

#include <math.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <string.h>

typedef enum follow_phase {
    FOLLOW_IDLE = 0,
    FOLLOW_WAITING,   /* a warp was heard and the teleport is not pressed yet */
    FOLLOW_MOVING     /* the teleport pressed for it is under way */
} follow_phase_t;

typedef struct follow_counts {
    uint32_t taken_up;      /* warps this side began to follow */
    uint32_t replaced;      /* of those, the ones a newer warp overtook */
    uint32_t pressed;       /* presses of the teleport's door */
    uint32_t landed;        /* warps followed to a place beside the host */
    uint32_t beside;        /* warps that left this player beside the host already */
    uint32_t tried_again;   /* teleports that ended without a landing and were waited for again */
    uint32_t given_up;      /* waits that ran out or met a door that does not open */
    uint32_t left;          /* ended by the player's repair or with the session */
    uint32_t longest;       /* substeps from a warp heard to its landing, the most */
} follow_counts_t;

typedef struct follow_state {
    follow_phase_t  phase;
    uint32_t        handled;       /* the count of warps heard that was taken up last */
    uint32_t        warp;          /* and the count the teleport under way was pressed for */
    uint8_t         world;         /* the world the warp was heard in */
    uint32_t        began;         /* the substep count as it was heard */
    uint32_t        began_ms;
    bool            landed;        /* the host's pose has read near the target since */
    int32_t         hero;
    float           target[3];
    uint8_t         door_reason;   /* the door's last refusal while it waited */
    follow_counts_t n;
} follow_state_t;

static follow_state_t follow;

static float distance(const float a[3], const float b[3])
{
    float dx = a[0] - b[0];
    float dy = a[1] - b[1];
    float dz = a[2] - b[2];

    return sqrtf(dx * dx + dy * dy + dz * dz);
}

/* A warp heard since the last look, taken up in whatever phase this is. */
static void take_up_a_new_warp(uint32_t substeps)
{
    uint32_t count = 0u;
    float    at[3] = { 0.0f, 0.0f, 0.0f };
    int32_t  hero  = 0;

    if (!mp_level_state_warp_heard(&count, at, &hero) || count == follow.handled) {
        return;
    }
    follow.n.replaced += follow.phase != FOLLOW_IDLE ? 1u : 0u;
    ++follow.n.taken_up;
    follow.handled     = count;
    follow.world       = mp_bridge_far_world();
    follow.began       = substeps;
    follow.began_ms    = mp_wallclock_ms();
    follow.landed      = false;
    follow.hero        = hero;
    follow.door_reason = PLAYER_HELP_REASON_NONE;
    memcpy(follow.target, at, sizeof follow.target);
    if (follow.phase == FOLLOW_IDLE) {
        follow.phase = FOLLOW_WAITING;
    }
    log_info("this player follows the host's warp to %.2f %.2f %.2f: the teleport to the host is "
             "pressed once he stands there, within %u substep(s)%s", (double)at[0], (double)at[1],
             (double)at[2], (unsigned)MP_WARP_FOLLOW_SUBSTEPS,
             follow.phase == FOLLOW_MOVING ? ", after the teleport that is under way" : "");
}

static void give_up(const char *why, uint32_t substeps)
{
    ++follow.n.given_up;
    follow.phase = FOLLOW_IDLE;
    log_info("the host's warp to %.2f %.2f %.2f is not followed, %s, %u substep(s) and %u ms "
             "after it was heard; the developer menu's teleport to the host still brings this "
             "player to him", (double)follow.target[0], (double)follow.target[1],
             (double)follow.target[2], why, (unsigned)(substeps - follow.began),
             (unsigned)(mp_wallclock_ms() - follow.began_ms));
}

/* Why a wait ended without a press, in the line's words. */
static const char *why_given_up(const mp_warp_follow_look_t *look)
{
    if (look->door.outcome == (uint8_t)PLAYER_HELP_OUTCOME_REFUSED &&
        !mp_warp_follow_passes(look->door.reason)) {
        return mp_player_help_reason_text(look->door.reason);
    }
    if (look->door.outcome == (uint8_t)PLAYER_HELP_OUTCOME_NOTHING) {
        return mp_player_help_reason_text(look->door.reason);
    }
    return follow.door_reason != (uint8_t)PLAYER_HELP_REASON_NONE
               ? mp_player_help_reason_text(follow.door_reason)
               : "the wait ran out";
}

bool mp_warp_follow_due(uint32_t substeps)
{
    mp_warp_follow_look_t look;
    float                 host_at[3] = { 0.0f, 0.0f, 0.0f };
    bool                  host_known = false;

    take_up_a_new_warp(substeps);
    if (follow.phase != FOLLOW_WAITING) {
        return false;
    }
    if (mp_bridge_far_world() != follow.world) {
        give_up("the level changed under it", substeps);
        return false;
    }
    memset(&look, 0, sizeof look);
    look.waited   = substeps - follow.began;
    look.door     = mp_teleport_host_would(host_at, &host_known);
    follow.landed = mp_warp_follow_landed(follow.landed, host_known,
                                          host_known ? distance(host_at, follow.target) : 0.0f);
    look.landed   = follow.landed;
    switch (mp_warp_follow_step(&look)) {
    case MP_WARP_FOLLOW_PRESS:
        return true;
    case MP_WARP_FOLLOW_BESIDE:
        ++follow.n.beside;
        follow.phase = FOLLOW_IDLE;
        log_info("the host's warp to %.2f %.2f %.2f needs no move: this player stands beside "
                 "the host already, %u substep(s) after it was heard", (double)follow.target[0],
                 (double)follow.target[1], (double)follow.target[2], (unsigned)look.waited);
        return false;
    case MP_WARP_FOLLOW_GIVE_UP:
        give_up(why_given_up(&look), substeps);
        return false;
    case MP_WARP_FOLLOW_WAIT:
    default:
        if (look.door.outcome == (uint8_t)PLAYER_HELP_OUTCOME_REFUSED) {
            follow.door_reason = look.door.reason;
        }
        return false;
    }
}

void mp_warp_follow_pressed(mp_player_help_verdict_t verdict, uint32_t substeps)
{
    if (follow.phase != FOLLOW_WAITING) {
        return;
    }
    ++follow.n.pressed;
    if (verdict.outcome != (uint8_t)PLAYER_HELP_OUTCOME_OPEN) {
        /* The door answered the look and the press differently on one frame: waited for again. */
        follow.door_reason = verdict.reason;
        return;
    }
    follow.phase = FOLLOW_MOVING;
    follow.warp  = follow.handled;
    log_info("the teleport to the host is pressed for his warp, %u substep(s) after it was "
             "heard: the host %s", (unsigned)(substeps - follow.began),
             follow.landed ? "stands at its target" : "was not seen at its target and is taken "
                                                      "as landed");
}

void mp_warp_follow_ended(mp_player_help_verdict_t ended, uint32_t substeps)
{
    uint32_t took = substeps - follow.began;

    if (follow.phase != FOLLOW_MOVING) {
        return;
    }
    if (follow.warp != follow.handled) {
        /* A newer warp was heard meanwhile: wherever this teleport ended, that one is next. */
        follow.phase = FOLLOW_WAITING;
        return;
    }
    if (ended.outcome == (uint8_t)PLAYER_HELP_OUTCOME_DONE) {
        ++follow.n.landed;
        follow.n.longest = took > follow.n.longest ? took : follow.n.longest;
        follow.phase     = FOLLOW_IDLE;
        log_info("this player followed the host's warp and stands beside him, %u substep(s) and "
                 "%u ms after it was heard", (unsigned)took,
                 (unsigned)(mp_wallclock_ms() - follow.began_ms));
        return;
    }
    if (!mp_warp_follow_tries_again(ended)) {
        give_up(mp_player_help_reason_text(ended.reason), substeps);
        return;
    }
    ++follow.n.tried_again;
    follow.door_reason = ended.reason;
    follow.phase       = FOLLOW_WAITING;
}

void mp_warp_follow_left(const char *why)
{
    if (follow.phase == FOLLOW_IDLE) {
        return;
    }
    ++follow.n.left;
    follow.phase = FOLLOW_IDLE;
    log_info("the host's warp to %.2f %.2f %.2f is not followed any further: %s",
             (double)follow.target[0], (double)follow.target[1], (double)follow.target[2],
             why != NULL ? why : "ended from outside");
}

void mp_warp_follow_report(void)
{
    const follow_counts_t *n = &follow.n;

    log_info("  the host's warps followed (client): %u taken up, %u of them overtaken by a newer "
             "one; the teleport pressed %u time(s); %u landed beside the host, %u stood beside "
             "him already, %u given up, %u ended by a repair or the session's end; %u "
             "teleport(s) ended without a landing and were waited for again; the longest took %u "
             "substep(s)%s",
             (unsigned)n->taken_up, (unsigned)n->replaced, (unsigned)n->pressed,
             (unsigned)n->landed, (unsigned)n->beside, (unsigned)n->given_up, (unsigned)n->left,
             (unsigned)n->tried_again, (unsigned)n->longest,
             follow.phase == FOLLOW_WAITING  ? "; one is waited for"
             : follow.phase == FOLLOW_MOVING ? "; one is under way"
                                             : "");
}
