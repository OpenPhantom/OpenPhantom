/* mp_relay_link_int.h: what the two halves of the relay link share. Not a public header.
 *
 * mp_relay_link.c holds the attempts: the Hello, the request, the answer and every refusal of
 * one. mp_relay_link_leg.c holds a leg that stands: its refresh rounds, the sealed traffic on it
 * and the goodbye. An attempt ends in a leg; a leg that is gone begins an attempt.
 */
#ifndef MULTIPLAYER_MP_RELAY_LINK_INT_H
#define MULTIPLAYER_MP_RELAY_LINK_INT_H

#include "mp_relay_link.h"

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

/* Whether `now` has reached `at` on a millisecond clock that wraps. */
bool mp_relay_link_reached(uint32_t now, uint32_t at);

/* A new attempt that replaces the leg in use, which carries on until the answer opens. */
void mp_relay_link_renew(mp_relay_link_t *link, uint32_t now);

/* The refresh interval the Registered or Joined named, clamped. */
uint32_t mp_relay_link_keep_interval(const mp_relay_link_t *link);

/* The READY phase of the tick: a new leg when one is due, otherwise the refresh round. */
size_t mp_relay_link_tick_leg(mp_relay_link_t *link, uint32_t now, uint8_t *out, size_t capacity);

/* An open Nack that arrived while a refresh round is out. */
void mp_relay_link_keep_refused(mp_relay_link_t *link, uint32_t now, uint8_t reason);

/* A datagram of a sealed type: checked against the leg's index before it is opened. */
bool mp_relay_link_take_sealed(mp_relay_link_t *link, uint32_t now, const uint8_t *datagram,
                               size_t bytes, uint8_t *game, size_t game_capacity,
                               mp_relay_link_event_t *event);

#endif /* MULTIPLAYER_MP_RELAY_LINK_INT_H */
