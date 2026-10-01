/* mp_bridge_appearance.h: what a far player looks like, from an edge and from a state.
 *
 * Two sources say what a far player wears. The edge is the appearance event the player's own
 * machine sends the moment it changes, and it names the world slot it belongs to. The state is the
 * roster the authority repeats once a second, which is how a player who joined later learns what
 * everybody already wears. Both end at the far body of the bank that shows that slot; a slot no
 * bank shows, or this side's own, is dropped and counted rather than painted onto somebody else.
 *
 * Left the drain when the far bodies became three: the one appearance it handled became one per
 * far bank, and the path is a subject of its own with counters of its own.
 */
#ifndef MULTIPLAYER_MP_BRIDGE_APPEARANCE_H
#define MULTIPLAYER_MP_BRIDGE_APPEARANCE_H

#include "mp_events.h"

#include <stddef.h>
#include <stdint.h>

/* Whether the hero a far player's state says and the hero their body here was built for may be
 * held against each other yet. Pure, and the reason it exists is in the body of the function. */
typedef enum mp_appearance_verdict {
    MP_APPEARANCE_WAIT,        /* too early to tell: the wire is still behind the rebuild */
    MP_APPEARANCE_AGREED,      /* the two name the same hero */
    MP_APPEARANCE_DISAGREES    /* and after the grace they still do not */
} mp_appearance_verdict_t;

mp_appearance_verdict_t mp_appearance_hero_verdict(int32_t built_for, int32_t far_hero,
                                                   uint32_t samples_since_build,
                                                   uint32_t grace_samples);

/* This side's own appearance went out as an event, and `worn` with its `kind` is what the table
 * this side publishes carries from now on. An edge alone loses everybody who was not there. */
void mp_bridge_appearance_note_own(const char *worn, uint8_t kind);

/* An appearance event off the channel from the peer at `peer_index`, judged against this side's
 * own world slot. */
void mp_bridge_appearance_take_event(uint8_t my_slot, size_t peer_index, const mp_event_t *event);

/* The roster that has just arrived, read for every far player a bank here shows. */
void mp_bridge_appearance_take_roster(void);

/* What the path did over the run, as its own line. */
void mp_bridge_appearance_report(void);

#endif /* MULTIPLAYER_MP_BRIDGE_APPEARANCE_H */
