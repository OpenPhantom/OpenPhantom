/* mp_announcer.h: the host's side of the browser: one announce a second, to the LAN.
 *
 * Layer 1. It owns a discovery socket (mp_discovery, layer 0) and an announce (mp_announce,
 * layer 1) and knows nothing of the engine or the bridge: what the bridge knows and the announce
 * needs, the wire version, the content fingerprint, the game port, how many are in, is handed in
 * by a call, and time is a parameter, so the whole cadence is driven in a test.
 *
 * The socket comes up on the first tick that has something to say and goes down with the bridge.
 * A socket that fails to open is logged once through the counters, not retried every tick: a
 * machine with no broadcast route is a machine with no broadcast route.
 */
#ifndef MULTIPLAYER_MP_ANNOUNCER_H
#define MULTIPLAYER_MP_ANNOUNCER_H

#include "mp_announce.h"
#include "mp_discovery.h"

#include <stdbool.h>
#include <stdint.h>

/* How often a host speaks: the browser's cycle, so its staleness rules read in the same unit. */
#define MP_ANNOUNCER_CYCLE_MS 1000u

typedef struct mp_announcer {
    mp_discovery_t socket;
    bool           enabled;      /* the host's choice: say so on the LAN, or keep quiet */
    bool           configured;   /* a name and a slot count were handed in */
    mp_announce_t  announce;     /* what goes out next; version is stamped by the encoder */
    uint32_t       last_ms;      /* when the last one left */
    bool           spoke;        /* at least one left, so last_ms means something */
    uint32_t       sent;
    uint32_t       failed;       /* encode refused or the send was refused */
    bool           open_failed;  /* the socket would not come up; nothing more is tried */
} mp_announcer_t;

void mp_announcer_init(mp_announcer_t *a);

/* The host's own words: the session name (cleaned to the announce's rule), how many it holds,
 * and the flags that are the host's choice (password, mode). `enabled` false keeps the socket
 * closed and every tick silent. */
void mp_announcer_configure(mp_announcer_t *a, const char *name, uint8_t slots, uint8_t flags,
                            bool enabled);

/* What the bridge knows: refreshed whenever it changes, cheap to call every tick. */
void mp_announcer_set_facts(mp_announcer_t *a, uint8_t wire, uint32_t fingerprint,
                            uint16_t game_port, uint8_t players);

/* One tick of the clock. Sends when a cycle has passed since the last one (or on the first call),
 * opening the socket on the first send. True when an announce left on this tick. */
bool mp_announcer_tick(mp_announcer_t *a, uint32_t now_ms, uint16_t announce_port);

/* Closes the socket; the configuration stays, so a later tick opens it again. */
void mp_announcer_close(mp_announcer_t *a);

#endif /* MULTIPLAYER_MP_ANNOUNCER_H */
