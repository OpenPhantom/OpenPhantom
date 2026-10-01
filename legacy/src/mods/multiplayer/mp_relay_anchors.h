/* mp_relay_anchors.h: the two certificates the relay key fetch trusts, and nothing else.
 *
 * Layer 0, data. ISRG Root YE and ISRG Root X2, both ECDSA P-384, the roots Let's Encrypt signs the
 * relay's name under. The fetch proves the relay's certificate chain against these two and never
 * against the system's store, because a store any program on the PC may add to is not a promise
 * about the relay. Each is pinned by the SHA-256 the relay's repository records for it.
 */
#ifndef MULTIPLAYER_MP_RELAY_ANCHORS_H
#define MULTIPLAYER_MP_RELAY_ANCHORS_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#define MP_RELAY_ANCHOR_YE_BYTES 477u
#define MP_RELAY_ANCHOR_X2_BYTES 543u

extern const uint8_t MP_RELAY_ANCHOR_YE[MP_RELAY_ANCHOR_YE_BYTES];
extern const uint8_t MP_RELAY_ANCHOR_X2[MP_RELAY_ANCHOR_X2_BYTES];
extern const uint8_t MP_RELAY_ANCHOR_YE_SHA256[32];
extern const uint8_t MP_RELAY_ANCHOR_X2_SHA256[32];

/* Whether both certificates still hash to their pinned values. A build whose bytes changed trusts
 * nothing: the fetch refuses before it connects. */
bool mp_relay_anchors_intact(void);

#endif /* MULTIPLAYER_MP_RELAY_ANCHORS_H */
