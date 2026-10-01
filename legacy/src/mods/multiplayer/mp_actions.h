/* mp_actions.h: what the local player does that the far side must perform, caught at the engine.
 *
 * Four kinds of moment are caught: a shot leaving the player's weapon, at the shot spawner the
 * body module already hulls; a force push starting, at the engine's own starter; a weapon change
 * starting, at the engine's weapon selector, because the slot the body's state carries is the one
 * the change commits a draw clip's marker later; and a sabre action beginning or ending, at the
 * four hulls of the sabre module. Each becomes an event in a queue the bridge drains once per
 * substep onto the reliable channel. Only the local player's moments count: while a bank is
 * swapped in, the same functions are being run for the second body or for the puppet, and those
 * are the far player's moments arriving, not this player's leaving.
 *
 * Every event is stamped with the tick the bridge sets at the start of each substep, so the far
 * side performs it when its replay reaches that tick; a shot's muzzle is turned into the body's
 * own frame at the moment of the catch, so the far side can put it at its puppet's weapon.
 *
 * A fifth kind is not caught but sampled, because it happens in no one function this module could
 * hull: what the local player looks like. It is described at the bottom of this file.
 */
#ifndef MULTIPLAYER_MP_ACTIONS_H
#define MULTIPLAYER_MP_ACTIONS_H

#include "mp_events.h"

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

/* Registers with the body module for shots, hulls the push starter and the weapon selector, and
 * installs the four sabre hulls. Idempotent. A site that did not resolve leaves that moment
 * uncaught and says so; the answer is true only when every hull stands, and a partial
 * installation is logged as partial. */
bool mp_actions_install(void);

/* The sender tick every event caught from now on carries. The bridge sets it once per substep,
 * right after it advances its own counter and before the engine tick in which the hulls run: the
 * shot hull inside the player's fire aux in the first phase of the tick, the push hull inside the
 * input phase, both between the bridge's pre tick and post tick halves. So an event caught in
 * substep S carries S, and the state the bridge sends after that tick carries the same substep's
 * fire overlay, with no off by one between the two. */
void mp_actions_set_tick(uint32_t tick);

/* A moment of the local player's body caught somewhere else, a sound of it or its shield
 * (mp_player_sound.h), queued with the others and stamped with the same tick, so it goes out in
 * the order the engine made it. The caller has decided that it is this player's. */
void mp_actions_note_moment(const mp_event_t *moment);

/* ======================= The appearance, which is sampled and not caught =====================
 *
 * The other four moments are caught at an engine function the local player runs. An appearance
 * change has no such function: a hero change, a character change and a model change end in three
 * different places, and one of them happens inside another feature DLL, which this one may not
 * call. So this moment is SAMPLED instead, out of two answers that arrive by themselves.
 *
 * `world_slot` is this machine's own body slot and the event has to carry it: without it an
 * arriving change says only that somebody looks different, and a receiver has to guess which body
 * it means. `worn` is filled with the name that went out, for the roster, which carries the same
 * answer as repeated state so that a player who joins later does not depend on an edge that fired
 * before they were there. Pass NULL and 0 for it when the caller does not want the name.
 * `kind` receives what that name is, MP_SKIN_CHARACTER or MP_SKIN_MODEL, out of the same call,
 * so the table cannot carry a name from one sample and a kind from another; NULL is allowed.
 *
 * True when a change was queued. False is the ordinary answer: nothing changed. */
bool mp_actions_note_appearance(uint8_t world_slot, char *worn, size_t bytes, uint8_t *kind);

/* A factor as the wire carries it: hundredths, or MP_EVENT_SCALE_NONE for a body at its own size
 * and for anything outside the bounds. Pure, and exposed because the rounding is the one place
 * where "the same size" and "a size worth sending" are decided. */
uint16_t mp_actions_scale_hundredths(float scale);

/* What the last sample said this machine's own body is drawn at, in those hundredths. Zero before
 * anything has been sampled, which is a body at its own size. */
uint16_t mp_actions_own_scale(void);

/* Appearance changes queued, and names the codec would not carry. A refused name is not a dropped
 * packet, it is a far side that goes on seeing the old body, so it is counted apart. */
uint32_t mp_actions_appearances(void);
uint32_t mp_actions_appearances_refused(void);

/* The next uncollected event, oldest first: peek leaves it, pop takes it. */
bool mp_actions_peek(mp_event_t *out);
bool mp_actions_pop(mp_event_t *out);

/* Forget everything uncollected, on every arrival of a peer: what the player did before the far
 * side was there is not the far side's to perform. */
void mp_actions_clear(void);

/* A peer arrived while others were already here. Nothing queued is thrown away, because the
 * others are owed it; only the appearance is owed again, because the newcomer has not been told
 * what this player looks like. */
void mp_actions_owe_appearance(void);

uint32_t mp_actions_caught(void);
uint32_t mp_actions_dropped(void);

/* Shots that could not be placed in the body's frame because the object behind the hero block
 * did not read, and were therefore not sent. */
uint32_t mp_actions_unplaced(void);

#endif /* MULTIPLAYER_MP_ACTIONS_H */
