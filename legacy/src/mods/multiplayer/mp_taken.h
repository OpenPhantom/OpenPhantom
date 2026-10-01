/* mp_taken.h: which pickups are already gone, on the wire.
 *
 * The bug this exists for happens on every single join and costs nothing to reproduce: the host
 * has been playing for ten minutes and has taken every medipack on the way. The client arrives,
 * loads the same level, and its OWN activation scan builds every one of them again, because a
 * placement the host buried is buried on the host's machine and nowhere else. The client walks
 * the same corridor and heals itself on medipacks that were used up before it connected.
 *
 * It is not caught by anything already here. The pickup relay only sends a claim for a pickup it
 * can see the host owns, and a pickup the host has buried is in no list the client can consult,
 * so the client takes it locally, counts it as its own, and the host never hears about it. The
 * enemy presence bitmap does not cover it either: that is the set of actors ALIVE this substep,
 * rebuilt from the actor walk, and a buried pickup is not an actor.
 *
 * Why it is absolute and repeated rather than an event. An event says "this one just went"; it
 * has to arrive, and a client that was not connected when it was sent has missed it for good.
 * This note says "these are the ones that are gone", complete, once a second, so a client that
 * joins in the tenth minute is told the whole truth in its first note and a dropped packet costs
 * nothing but a second.
 *
 * The size is why it can be. The eleven shipped levels author between 4 and 26 pickups, 177 in
 * all, so the whole list fits in 30 bytes at worst, which is under the channel's eager threshold
 * of 32: it rides every packet until it is acknowledged rather than waiting its turn. A 256 bit
 * bitmap would have been 35 bytes and always above it, which is why the list is sparse.
 *
 * A pickup, for that count, is what `Plr_PickUp` at 0x00448894 treats as "walk into it and it is
 * yours": the classes 0x0A to 0x1B, the force cell at 0x0A, health at 0x0D to 0x10, ammunition
 * and weapons at 0x11 to 0x1B. On a take it sets bit 3 on the body and deletes it, which is why a
 * player's body may never carry a class in that band. Gone, in the engine's own terms, is spawn
 * state 2 at +0xC8 of the placement: `enemy_delete` at 0x00437850 writes it on a removal, the
 * engine's own savegame keeps exactly two fields per placement, that state and the flag word, and
 * the activation scan skips any placement whose state is not zero. Writing it is enough to stop a
 * pickup being built; it does not remove one already standing, which is the claim path's case.
 *
 * SIZE NOTE: under 120 lines, no seam.
 */
#ifndef MULTIPLAYER_MP_TAKEN_H
#define MULTIPLAYER_MP_TAKEN_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

/* The next free tag when this was written, counted over the tags in use (0x81 to 0x96 were all
 * taken) rather than read off a comment: the comment that named the next free tag had already
 * been overtaken once by the time this was written. */
#define MP_TAKEN_TAG 0x97u

/* The most pickups one note describes. The largest shipped level authors 26; the headroom is for
 * a level somebody makes, and the note stays under the eager threshold up to 27. */
#define MP_TAKEN_MAX 64u

/* tag, level (2), count, then one index per entry. */
#define MP_TAKEN_HEADER_BYTES 4u
#define MP_TAKEN_BYTES_FOR(n) (MP_TAKEN_HEADER_BYTES + (size_t)(n))
#define MP_TAKEN_MAX_BYTES    MP_TAKEN_BYTES_FOR(MP_TAKEN_MAX)

typedef struct mp_taken {
    uint16_t level;              /* the level identity, so a note cannot cross a level load */
    uint8_t  count;
    uint8_t  index[MP_TAKEN_MAX];
} mp_taken_t;

/* Puts one index in, keeping the list sorted and free of duplicates. False when the list is full,
 * which the caller counts: a level with more pickups than fit is one whose later ones would
 * silently never travel. Sorted because two hosts describing the same set must produce the same
 * bytes, which is what lets a receiver skip an unchanged note without comparing sets. */
bool mp_taken_add(mp_taken_t *taken, uint8_t index);

/* The note. The ENCODER refuses a count past the maximum, or a buffer too small for it, rather
 * than truncating: a truncated list of what is GONE reads as a list of what is left, which is the
 * wrong answer rather than a short one.
 *
 * What fills the list cannot make that promise, only keep the damage visible: a level with more
 * pickups than fit stops adding at the cap and counts it, so the shortfall is a number in the
 * report rather than a silence. At four to twenty six per shipped level against a cap of sixty
 * four, that is headroom rather than a live risk. */
size_t mp_taken_encode(const mp_taken_t *taken, uint8_t *buffer, size_t capacity);

/* Recognised by tag AND by exact length, the way the roster is: a note whose length does not
 * match the count it declares is a torn one, and accepting it would seat garbage indices. */
bool mp_taken_is(const uint8_t *buffer, size_t bytes);
bool mp_taken_decode(const uint8_t *buffer, size_t bytes, mp_taken_t *out);

/* Whether two lists say the same thing, so a receiver can skip work on a repeat and a sender can
 * skip a send. Compares the level as well: the same set in a different level is a different
 * statement. */
bool mp_taken_equal(const mp_taken_t *a, const mp_taken_t *b);

#endif /* MULTIPLAYER_MP_TAKEN_H */
