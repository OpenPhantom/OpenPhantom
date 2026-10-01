/* mp_bridge_far.h: what this machine holds for each far player, one far bank each.
 *
 * Every far body is sampled out of its own history on its own timeline. The first build had one
 * far player and one interpolator, owned by the bridge and started over on anybody's arrival, and
 * the host drained its first peer alone: a second client's every state stayed in its ring, and its
 * join cost the first client's body its timeline.
 *
 * A bank's history is keyed on the connection in front of it. It starts over when that changes,
 * which a new player behind the same peer index always does, a restart from the same address
 * included, and never because somebody else arrived.
 *
 * Beside the history each bank keeps the world slot it shows, the last pose resolved for that
 * player, the hero the player's last appearance named, and the look that appearance asked for:
 * the actor the body is built from and the model worn over it. A listen host seats every bank on
 * the slot of its own number; a client seats its first bank on the one far player it shows. A
 * rule that asks where a far player stands, a body that asks whether anybody is behind it, and
 * an appearance that names a slot all find their bank here.
 */
#ifndef MULTIPLAYER_MP_BRIDGE_FAR_H
#define MULTIPLAYER_MP_BRIDGE_FAR_H

#include "mp_interp.h"

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

/* Where a far player is, and which world slot it holds. The interpolator resolves that pose every
 * substep for the puppet, and a rule that has to know where a far player is standing needs the
 * same answer without going through the puppet or the engine.
 *
 * `alive` and `dead` are the far machine's own words about its player, carried in the sample the
 * pose was blended from; they are not the negation of each other, because a body can be neither
 * yet. `world` is the world that sample was sent from and `state_tick` its tick, the earlier of two
 * a blend lay between. */
typedef struct mp_bridge_far_pose {
    uint8_t  slot;          /* the world slot the far body stands for */
    float    position[3];
    float    heading;       /* degrees, the unit the hero block carries */
    bool     alive;
    bool     dead;
    uint8_t  health;
    uint8_t  world;         /* the sender's world, as the body carried it */
    uint32_t state_tick;
} mp_bridge_far_pose_t;

/* ================================ Only a pose of this world ====================================
 *
 * Every body names the world its sender stood in, and a far pose of another world is not a place in
 * this one: the last pose of the level before, or the first of the next one a far player already
 * stands in. Coordinates of one level read in another are a floor nobody stands on, and a seat
 * search that was handed them put two clients on one point far from their host. So a pose is kept
 * whatever its world, and every reader is answered only with a pose of the world this machine
 * entered: one rule in one place, asked by name so the log can say who was refused. */

/* The world this machine stands in: the generation of the setup note its running level began
 * under, told at every level begin of a session. Before the first one nothing is judged, because no
 * far pose is resolved outside a level of a session. */
void    mp_bridge_far_enter_world(uint8_t generation);
uint8_t mp_bridge_far_world(void);

/* The same rule for a world a body names, the wire's four bits: whether it is the world this
 * machine stands in, true for every world before this machine entered one. The host's greeting of
 * a player entering its world asks it of the state that player sends. */
bool mp_bridge_far_is_this_world(uint8_t world);

/* Who asks for a far pose, for the count of the ones refused as another world's. */
typedef enum mp_bridge_far_reader {
    MP_FAR_READER_ARRIVAL,      /* a client's offset beside its host */
    MP_FAR_READER_REENTRY,      /* the re-entry's anchors and the seat's bodies */
    MP_FAR_READER_RANGE_GATE,   /* where a far body wakes the host's enemies and keeps them */
    MP_FAR_READER_SCENE,        /* the host's scene: the place it gathers around, and who sits */
    MP_FAR_READER_OTHER,        /* a body that stands, the world's anchor, the copies, a reply */
    MP_FAR_READERS
} mp_bridge_far_reader_t;

/* Every bank's history fresh and every pose, hero and placement forgotten, at every reset of the
 * whole session. The seats stay: they belong to the bind, not to a peer. */
void mp_bridge_far_reset(void);

/* The run's choice between a measured and a held lag, for every history now and every one started
 * later: a fresh interpolator holds its lag, and without this the regulation would be on for the
 * first peer only. */
void mp_bridge_far_set_auto_lag(bool enabled);

/* The connection whose history bank `bank` holds, 0 for nobody and for an index that is no far
 * bank. */
uint64_t mp_bridge_far_connection(size_t bank);

/* Bank `bank` holds `connection_id` from now on, 0 for nobody, and its history starts over; the
 * pose, the hero and the placement of the player who was there go with it. */
void mp_bridge_far_start_over(size_t bank, uint64_t connection_id);

/* A far player's history, or NULL for an index that is no far bank. */
mp_interp_t *mp_bridge_far_interp(size_t bank);

/* One state of the player in bank `bank` taken into its history, for the report. */
void mp_bridge_far_note_state(size_t bank);

/* One moment of the player in bank `bank` queued for its puppet, and how many over the run, for
 * the report: the line a client's acceptance of the host's relay reads. */
void     mp_bridge_far_note_moment(size_t bank);
uint32_t mp_bridge_far_moments(size_t bank);

/* On a listen host, which passes every client's moments on to the others: one moment passed on
 * to the player of bank `bank` on the host's substep `tick`. Counted per connection, and said
 * with the bank's other lines at the report and as the player leaves, so the report shows which
 * player the host passed nothing to, and whether the last copy went after the player had gone.
 * Only a relaying machine says these lines; the bridge tells this module which it is. */
void mp_bridge_far_set_relaying(bool relaying);
void mp_bridge_far_note_passed_on(size_t bank, uint32_t tick);

/* How many were passed on to the player of bank `bank` since its connection began, and the host's
 * substep of the last one into `last_tick` when it is not NULL. */
uint32_t mp_bridge_far_passed_on(size_t bank, uint32_t *last_tick);

/* Which world slot bank `bank` shows. Seating replaces whatever the bank showed, and a slot is
 * shown by one bank at most. False for a bank that shows nobody. */
void mp_bridge_far_unseat_all(void);
void mp_bridge_far_seat(size_t bank, uint8_t slot);
bool mp_bridge_far_slot_of(size_t bank, uint8_t *slot);

/* The bank that shows world slot `slot`, or 0 for a slot no bank shows. */
size_t mp_bridge_far_bank_of_slot(uint8_t slot);

/* A client's seat for a slot the host's world carries, as seen from this side's own slot. A slot
 * already seated keeps its bank. A new one takes the bank of its own number when it lies above
 * this side's slot and the bank one up when it lies below, so a client of a listen host shows the
 * host in bank 1 and the others where the host shows them; a slot that does not fit takes the
 * lowest free bank, and one that finds none, or this side's own, is answered 0. A bank seated
 * anew starts its history over, because it may have shown somebody else. */
size_t mp_bridge_far_seat_slot(uint8_t slot, uint8_t my_slot);

/* Bank `bank` shows nobody any more: its seat, its history, its pose and its hero go. */
void mp_bridge_far_unseat(size_t bank);

/* The pose the interpolator resolved for bank `bank`'s player this substep, kept for the readers
 * that are not the puppet. True when it is a pose of this world, which is the one a puppet may be
 * placed on; the first such also marks the bank placed, which keeps its window running on every
 * later substep. A pose of another world is kept and counted, and marks nothing. */
bool mp_bridge_far_note_pose(size_t bank, const mp_bridge_far_pose_t *pose);

/* The pose kept for bank `bank`, when it is one of this world. False before the first, after the
 * player left, and while the one kept is another world's, which is counted for `reader`. */
bool mp_bridge_far_pose(size_t bank, mp_bridge_far_reader_t reader, mp_bridge_far_pose_t *out);

/* Whether the player behind bank `bank` stands in another world: the bank shows a slot and the pose
 * kept for it is another world's. On a host that is a client still on its way into the host's
 * world; on a client, a player who has not followed yet or one this side has not caught up with.
 * The slot is written when there is one; the pose is not handed out. Counted for `reader`. */
bool mp_bridge_far_in_another_world(size_t bank, mp_bridge_far_reader_t reader, uint8_t *slot);

/* How many times bank `bank`'s history began for a new player: a connection on a host, a seat on a
 * client. A pose from a history begun after some moment is newer than that moment, whatever tick it
 * carries. */
uint32_t mp_bridge_far_starts(size_t bank);

/* Whether the player a pose was resolved for stands: alive and not dead, in the far machine's own
 * words, which are not each other's negation (a body can be neither yet). The one predicate for
 * every rule that asks, the return beside a far player and the copies that go with one. */
bool mp_bridge_far_pose_stands(const mp_bridge_far_pose_t *pose);
bool mp_bridge_far_placed(size_t bank);

/* Whether a far player stands behind bank `bank` right now: the bank shows a slot and a pose of
 * this world has been resolved for its player. A peer that has joined and sends nothing yet, a
 * client still in its lobby, and a player still in another world are not one. */
bool mp_bridge_far_occupied(size_t bank);

/* The line of the poses of another world: resolved and not placed on a puppet, refused to each
 * reader, far players gathered for a scene on their way here, and blends held at a change of
 * world. */
void mp_bridge_far_report_worlds(void);

/* The hero the last appearance event for bank `bank`'s player named, which is what the engine
 * actually has once a character has been swapped in; the repeated roster carries the lobby's
 * choice and would rebuild the body against the wrong number without this. */
void mp_bridge_far_note_hero(size_t bank, uint8_t hero);
bool mp_bridge_far_hero(size_t bank, uint8_t *hero);

/* The look bank `bank`'s player asked for, as their appearance said it: `kind` and `name`, worn as
 * hero `hero`. Decides the actor the body is built from (mp_skin_body_actor, against the actor
 * kept from the appearance before) and keeps the model worn over it. True when the model
 * changed. Forgotten with the hero. */
bool mp_bridge_far_note_look(size_t bank, uint8_t kind, const char *name, uint8_t hero);

/* What that decided. The actor is "" for the hero's own shipped asset and for a bank that has
 * heard no appearance; the model reader answers false when none is worn and copies the name into
 * `out` when `out` has room. */
const char *mp_bridge_far_actor(size_t bank);
bool mp_bridge_far_model(size_t bank, char *out, size_t bytes);

/* The list the report's states line carries, on both roles: each bank that ever held a player, as
 * the world slot it showed last, with the states taken, how often a new player started its
 * history and how often its timeline had to resync since. It used to print the bank's index as
 * the slot, which is the same number on a listen host and a different one on a client, and it
 * printed nothing on a client at all. Answers the length written; always terminated. */
size_t mp_bridge_far_states_text(char *out, size_t bytes);

/* For every bank a player stands behind, its buffer, its timeline and its blade, the bank and its
 * slot behind the colon of each line. The same three are printed for a bank as its player leaves,
 * and as a session is reset, because the history goes with them and a report after that would
 * describe a nought. */
void mp_bridge_far_describe_banks(void);

/* How many banks were described as their player left, over the run. */
uint32_t mp_bridge_far_departures(void);

#endif /* MULTIPLAYER_MP_BRIDGE_FAR_H */
