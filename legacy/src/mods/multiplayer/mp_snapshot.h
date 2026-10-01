/* mp_snapshot.h: the world state the host sends, delta encoded against what the client has.
 *
 * Layer 1, pure logic. A snapshot is one server tick and up to eight body slots, each slot a body
 * record (mp_wire_body) or empty. The host builds one every tick; the client applies it and
 * interpolates. What crosses the wire is the difference from the last snapshot the client
 * acknowledged, which is the Quake-3 model and the reason to use it is not bandwidth, it is loss:
 * the same encoding produces a full update, a partial update and a repeat of a lost change with one
 * mechanism, so a lost snapshot is healed by the next one rather than retransmitted.
 *
 * A snapshot is quantised the moment it is built, on both sides, so the client's own prediction and
 * the host's authority agree to the bit and transmission rounding never becomes a standing
 * correction. The quantisation lives in mp_wire; this module only decides which bodies changed.
 *
 * The slot index is the body id. The host assigns player 0 to slot 0 and so on, so a receiver knows
 * which body a slot is without a lookup. Eight slots leave room past four players for a few shared
 * world objects.
 */
#ifndef MULTIPLAYER_MP_SNAPSHOT_H
#define MULTIPLAYER_MP_SNAPSHOT_H

#include "mp_wire.h"

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#define MP_SNAPSHOT_MAX_BODIES 16u

typedef struct mp_snapshot {
    uint32_t       tick;                          /* the server tick this state is for */
    uint16_t       present_mask;                  /* bit i set means slot i holds a body; two
                                                   * bytes because sixteen slots do not fit one */
    mp_wire_body_t body[MP_SNAPSHOT_MAX_BODIES];
} mp_snapshot_t;

/* Clears a snapshot to no bodies at tick zero. A snapshot must be cleared before it is filled,
 * because the change detection compares whole records and an uninitialised slot would read as a
 * spurious difference. */
void mp_snapshot_clear(mp_snapshot_t *snapshot);

/* Sets or clears one body slot. The index is the body id, 0 based. */
void mp_snapshot_set_body(mp_snapshot_t *snapshot, size_t slot, const mp_wire_body_t *body);
void mp_snapshot_clear_body(mp_snapshot_t *snapshot, size_t slot);

bool mp_snapshot_has_body(const mp_snapshot_t *snapshot, size_t slot);

/* Encodes `current` as a delta against `baseline`, or in full when `baseline` is NULL. Each slot
 * costs one state byte, plus a body record only when it is present and changed. The encoded tick
 * and baseline tick let the decoder refuse a delta it cannot apply. False when the buffer is too
 * small; nothing is fragmented. */
bool mp_snapshot_encode(const mp_snapshot_t *current, const mp_snapshot_t *baseline,
                        uint8_t *buffer, size_t capacity, size_t *bytes);

/* Decodes into `out`, applying against `baseline`. `baseline` must be the snapshot whose tick the
 * encoded packet names, or NULL when the packet was encoded in full. Returns false when the
 * baselines do not match, a slot claims to be unchanged but the baseline has no body there, or the
 * bytes run out; on false `out` is left cleared rather than half applied. */
bool mp_snapshot_decode(const uint8_t *buffer, size_t bytes, const mp_snapshot_t *baseline,
                        mp_snapshot_t *out);

/* The baseline tick a packet was encoded against, read from its header without decoding the rest,
 * so a receiver can find the right baseline before it applies the delta. False when the bytes are
 * too short to hold a header. */
bool mp_snapshot_baseline_tick(const uint8_t *buffer, size_t bytes, uint32_t *baseline_tick);

#endif /* MULTIPLAYER_MP_SNAPSHOT_H */
