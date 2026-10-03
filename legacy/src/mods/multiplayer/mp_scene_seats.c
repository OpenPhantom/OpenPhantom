/* mp_scene_seats.c: the place the host is brought to for a scene a far player set off. See
 * mp_scene_host.h and mp_scene_host_internal.h.
 *
 * In the substep after the door, outside the engine, where the seat search's probes are asked
 * everywhere else too. The decisions are mp_scene_room's; what is here reads the players and the
 * scene and asks the one seat search.
 *
 * The place of the host is where the far player the script meant stands, on the floor under it,
 * searched without the host's own body and without that player's, so that neither takes it from
 * the host. While that player is in the air or in the water it is read again every substep, for a
 * second, and the scene's hold waits. A host who stands at the place found is not moved. Nobody
 * else is given a place: a far player goes on playing where he is.
 */
#include "mp_scene_host_internal.h"

#include "mp_bank.h"
#include "mp_body.h"
#include "mp_bridge_far.h"
#include "mp_scene_bind.h"
#include "mp_scene_room.h"
#include "mp_seat.h"
#include "mp_target.h"

#include "common/logging.h"
#include "common/text.h"

#include <math.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <string.h>

typedef struct seats_state {
    bool                  place_pending;   /* the place is read again every substep */
    uint32_t              place_since;     /* the substep the first reading was made in */
    bool                  place_said;      /* the line of the wait is written */
    mp_scene_place_read_t place_read;      /* the last reading */
    float                 trigger_at[3];   /* where the player the script meant stands */
} seats_state_t;

static seats_state_t ss;

/* ==============================================================================================
 * Small readings.
 * ============================================================================================ */

static uint8_t slot_of_bank(uint8_t bank)
{
    uint8_t slot = MP_SCENE_TRIGGER_UNKNOWN;

    return mp_body_bank_slot(bank, &slot) ? slot : (uint8_t)MP_SCENE_TRIGGER_UNKNOWN;
}

static void slot_text(char *out, size_t size, uint8_t slot)
{
    if (slot == MP_SCENE_TRIGGER_UNKNOWN) {
        (void)text_format(out, size, "unknown");
    } else {
        (void)text_format(out, size, "%u", (unsigned)slot);
    }
}

static const char *kind_text(mp_scene_kind_t kind)
{
    return kind == MP_SCENE_KIND_LOCK ? "a lock" : "the hero as an actor";
}

/* ==============================================================================================
 * The place of the host.
 * ============================================================================================ */

/* What the host does, as the scene's line says it after "the host". */
typedef enum host_way {
    HOST_TAKES_THE_PLACE = 0,
    HOST_BESIDE_THE_ACTOR
} host_way_t;

/* Whether the host stands where the scene is to be played already, asked once the place is found
 * and never before: a host beside a far player who stands where nobody can be put is not at a
 * place. Then he is not moved, and the hold falls on the next look. */
static bool the_host_is_there(const float place[3])
{
    mp_scene_host_shared_t *sh = mp_scene_host_shared();
    float                   host[3];
    char                    slot[16];

    if (!mp_target_player_position(0u, host) ||
        !mp_scene_host_is_there(sh->flow.kind == MP_SCENE_KIND_HERO, host, place,
                                ss.trigger_at)) {
        return false;
    }
    ++sh->n.already_there;
    if (mp_scene_host_line_allowed()) {
        slot_text(slot, sizeof slot, sh->trigger_slot);
        log_info("a scene of a far player's is the host's: scene %u at %.2f %.2f %.2f, where "
                 "world slot %s stood (%s), the host stands there already at %.2f %.2f %.2f and "
                 "is not moved, and nobody else is moved or held", (unsigned)sh->flow.serial,
                 (double)ss.trigger_at[0], (double)ss.trigger_at[1], (double)ss.trigger_at[2],
                 slot, kind_text(sh->flow.kind), (double)host[0], (double)host[1],
                 (double)host[2]);
    }
    return true;
}

/* The host takes `place`, facing `heading`. */
static void the_host_goes_to(const float place[3], float heading, host_way_t way)
{
    mp_scene_host_shared_t *sh = mp_scene_host_shared();
    char                    slot[16];

    ss.place_pending = false;
    memcpy(sh->anchor, place, sizeof sh->anchor);
    sh->heading      = heading;
    sh->anchor_known = true;
    ++sh->seat_counts.searches;
    if (the_host_is_there(place)) {
        return;
    }
    mp_scene_own_start(place, heading);
    if (!mp_scene_host_line_allowed()) {
        return;
    }
    slot_text(slot, sizeof slot, sh->trigger_slot);
    if (way == HOST_TAKES_THE_PLACE) {
        log_info("a scene of a far player's is the host's: scene %u at %.2f %.2f %.2f, where "
                 "world slot %s stood (%s), the host takes that place at %.2f %.2f %.2f facing "
                 "%.2f, and nobody else is moved or held", (unsigned)sh->flow.serial,
                 (double)ss.trigger_at[0], (double)ss.trigger_at[1], (double)ss.trigger_at[2],
                 slot, kind_text(sh->flow.kind), (double)place[0], (double)place[1],
                 (double)place[2], (double)heading);
        return;
    }
    log_info("a scene of a far player's is the host's: scene %u at %.2f %.2f %.2f, where world "
             "slot %s stood (%s), the host takes a place beside the scene's actor at %.2f %.2f "
             "%.2f, the place of that player could not be stood on (%s), and nobody else is "
             "moved or held", (unsigned)sh->flow.serial, (double)ss.trigger_at[0],
             (double)ss.trigger_at[1], (double)ss.trigger_at[2], slot, kind_text(sh->flow.kind),
             (double)place[0], (double)place[1], (double)place[2],
             mp_scene_place_text(ss.place_read));
}

/* The host has no place: he stays where he stands, and the hold falls on the next look. A hero
 * scene then runs where he stands; a lock is no scene of his, and the host's scene lets him go of
 * it in this substep. */
static void the_host_has_no_place(bool beside_asked, mp_scene_place_read_t beside_read)
{
    mp_scene_host_shared_t *sh = mp_scene_host_shared();
    char                    slot[16];

    ss.place_pending = false;
    sh->no_place     = true;
    ++sh->n.no_place;
    ++sh->seat_counts.searches;
    ++sh->seat_counts.found_nothing;
    if (!mp_scene_host_line_allowed()) {
        return;
    }
    slot_text(slot, sizeof slot, sh->trigger_slot);
    if (beside_asked) {
        log_warning("the host has no place for scene %u: neither the place of world slot %s (%s) "
                    "nor the scene's actor (%s) could be stood on, so the host stays where he "
                    "stands", (unsigned)sh->flow.serial, slot, mp_scene_place_text(ss.place_read),
                    mp_scene_place_text(beside_read));
    } else {
        log_warning("the host has no place for scene %u: the place of world slot %s could not be "
                    "stood on (%s), and %s, so the host stays where he stands",
                    (unsigned)sh->flow.serial, slot, mp_scene_place_text(ss.place_read),
                    ss.place_read == MP_SCENE_PLACE_READ_MOVER
                        ? "a mover is the ride of whoever stands on it"
                        : "only a scene with the hero as an actor is played beside its actor");
    }
}

/* The bodies the place is searched against: every far player's but the one the script meant, and
 * not the host's, so that neither takes the place from the host. */
static size_t bodies_for_the_place(mp_seat_body_t bodies[MP_BANK_FAR_MAX])
{
    mp_scene_host_shared_t *sh    = mp_scene_host_shared();
    size_t                  count = 0u;
    size_t                  bank;

    for (bank = 1u; bank <= MP_BANK_FAR_MAX; ++bank) {
        mp_bridge_far_pose_t pose;

        if (bank == sh->bank || !mp_bridge_far_pose(bank, MP_FAR_READER_SCENE, &pose)) {
            continue;
        }
        memset(&bodies[count], 0, sizeof bodies[count]);
        bodies[count].known  = true;
        bodies[count].stands = mp_bridge_far_pose_stands(&pose);
        memcpy(bodies[count].position, pose.position, sizeof pose.position);
        ++count;
    }
    return count;
}

/* One search, on the point itself first when `on_it`, otherwise around it, in the order of the
 * host's own slot, as the reading the place rule takes. */
static mp_scene_place_read_t search_from(const float at[3], bool on_it, float place[3])
{
    mp_scene_host_shared_t *sh = mp_scene_host_shared();
    mp_seat_body_t          bodies[MP_BANK_FAR_MAX];
    mp_seat_search_t        search;
    mp_seat_floor_t         floor = MP_SEAT_FLOOR_OK;
    size_t                  count = bodies_for_the_place(bodies);

    memset(&search, 0, sizeof search);
    mp_seat_rule_slot_order(slot_of_bank(0u), search.order);
    switch (mp_seat_probe_ordered(at, !on_it, &search, bodies, count, &sh->seat_counts, place,
                                  &floor)) {
    case MP_SEAT_FOUND:
        return MP_SCENE_PLACE_READ_FOUND;
    case MP_SEAT_NONE_FREE:
        return MP_SCENE_PLACE_READ_NONE_FREE;
    case MP_SEAT_ANCHOR_MOVING:
        return floor == MP_SEAT_FLOOR_MOVER ? MP_SCENE_PLACE_READ_MOVER
                                            : MP_SCENE_PLACE_READ_MOVING;
    case MP_SEAT_NO_LEVEL:
    case MP_SEAT_NO_PROBES:
    case MP_SEAT_NOBODY_STANDING:
    default:
        return MP_SCENE_PLACE_READ_NO_PROBES;
    }
}

/* The place of the far player the script meant: his body here, which is what its script's test
 * measured, in the same substep, and the way his pose faces. */
static mp_scene_place_read_t read_the_place(float place[3], float *heading)
{
    mp_scene_host_shared_t *sh = mp_scene_host_shared();
    mp_bridge_far_pose_t    pose;

    *heading = mp_bridge_far_pose(sh->bank, MP_FAR_READER_SCENE, &pose) ? pose.heading : 0.0f;
    if (!mp_target_player_position(sh->bank, ss.trigger_at)) {
        return MP_SCENE_PLACE_READ_UNREAD;
    }
    memcpy(sh->anchor, ss.trigger_at, sizeof sh->anchor);
    sh->anchor_known = true;
    return search_from(ss.trigger_at, true, place);
}

/* Beside the actor whose script set the scene off: a seat in the ring around where it stood as
 * the door opened, from where the hero walks his way as he would from the place. The position is
 * the one read at the door, because the actor may be gone by now. */
static void try_beside_the_actor(void)
{
    mp_scene_host_shared_t *sh       = mp_scene_host_shared();
    float                   place[3] = { 0.0f, 0.0f, 0.0f };
    mp_scene_place_read_t   beside   = MP_SCENE_PLACE_READ_UNREAD;

    if (sh->actor_at_known) {
        beside = search_from(sh->actor_at, false, place);
        if (beside == MP_SCENE_PLACE_READ_FOUND) {
            ++sh->n.beside_actor;
            the_host_goes_to(place, mp_scene_facing(place, sh->actor_at), HOST_BESIDE_THE_ACTOR);
            return;
        }
    }
    the_host_has_no_place(true, beside);
}

/* One reading of the place and what the rule makes of it. */
static void try_the_place(void)
{
    mp_scene_host_shared_t *sh      = mp_scene_host_shared();
    float                   place[3] = { 0.0f, 0.0f, 0.0f };
    float                   heading = 0.0f;
    char                    slot[16];

    ss.place_read = read_the_place(place, &heading);
    switch (mp_scene_place_rule(ss.place_read, sh->flow.kind == MP_SCENE_KIND_HERO,
                                sh->substep - ss.place_since)) {
    case MP_SCENE_PLACE_TAKE:
        the_host_goes_to(place, heading, HOST_TAKES_THE_PLACE);
        return;
    case MP_SCENE_PLACE_READ_AGAIN:
        ss.place_pending = true;
        if (!ss.place_said) {
            ss.place_said = true;
            ++sh->n.place_waits;
            slot_text(slot, sizeof slot, sh->trigger_slot);
            if (mp_scene_host_line_allowed()) {
                log_info("the place of scene %u is waited for: world slot %s stands where nobody "
                         "can be put (%s), read again for up to %u substep(s)",
                         (unsigned)sh->flow.serial, slot, mp_scene_place_text(ss.place_read),
                         (unsigned)MP_SCENE_PLACE_WAIT_SUBSTEPS);
            }
        }
        return;
    case MP_SCENE_PLACE_BESIDE_ACTOR:
        try_beside_the_actor();
        return;
    case MP_SCENE_PLACE_NOBODY:
    default:
        the_host_has_no_place(false, MP_SCENE_PLACE_READ_UNREAD);
        return;
    }
}

/* ==============================================================================================
 * The substeps.
 * ============================================================================================ */

void mp_scene_seats_forget(void)
{
    mp_scene_host_shared_t *sh = mp_scene_host_shared();

    sh->anchor_known = false;
    sh->no_place     = false;
    memset(&ss, 0, sizeof ss);
}

/* A warp: the engine sends the host to its target, and no far player follows it. */
static void say_the_warp(void)
{
    mp_scene_host_shared_t *sh = mp_scene_host_shared();

    if (mp_scene_host_line_allowed()) {
        log_info("a script warped this host to %.2f %.2f %.2f as hero %d; no far player is "
                 "moved for it", (double)sh->anchor[0], (double)sh->anchor[1],
                 (double)sh->anchor[2], (int)sh->warp_hero);
    }
}

void mp_scene_seats_start(void)
{
    mp_scene_host_shared_t *sh = mp_scene_host_shared();

    /* The way to the place of the scene before this one is not this scene's: a scene that sets no
     * place of its own would otherwise bring the host back to the old one. */
    mp_scene_own_forget(MP_SCENE_OWN_END_NEXT);
    mp_scene_seats_forget();
    ++sh->n.began[sh->flow.kind];
    sh->trigger_slot = slot_of_bank(sh->bank);
    if (sh->flow.kind == MP_SCENE_KIND_WARP) {
        memcpy(sh->anchor, sh->warp_at, sizeof sh->anchor);
        sh->heading      = sh->warp_heading;
        sh->anchor_known = true;
        say_the_warp();
        return;
    }
    if (!mp_scene_bind_player_stands() && sh->flow.holds && mp_scene_host_line_allowed()) {
        log_warning("the host is dead as scene %u begins, so its hold waits for the host to stand "
                    "again, and the host's re-entry takes the place of the scene",
                    (unsigned)sh->flow.serial);
    }
    if (sh->bank != 0u && sh->flow.holds) {
        ++sh->n.held;
        ss.place_since = sh->substep;
        try_the_place();
        return;
    }
    /* A scene of the host's own: he stays, and the scene is played where he stands. */
    sh->anchor_known = mp_scene_bind_own_pose(sh->anchor, &sh->heading);
    memcpy(ss.trigger_at, sh->anchor, sizeof ss.trigger_at);
}

/* The place is read again only while the hold stands: a hold that fell meanwhile, every far player
 * gone or the wait over, has nobody left to place. */
void mp_scene_seats_step(void)
{
    const mp_scene_host_flow_t *flow = &mp_scene_host_shared()->flow;

    if (!ss.place_pending || mp_scene_host_shared()->substep == ss.place_since) {
        return;   /* none to read, or read in this substep already by the start */
    }
    if (flow->phase != MP_SCENE_PHASE_GATHERING || !flow->holds) {
        ss.place_pending = false;
        return;
    }
    try_the_place();
}

bool mp_scene_seats_place_pending(void)
{
    return ss.place_pending;
}
