/* mp_lobby_note_rule.h: which reliable notes a client in its lobby takes, and which it drops.
 *
 * Layer 1, pure. A client in its lobby has no level. Before a session starts that is harmless,
 * because the host sends nothing but the lobby's own notes. A client that joins a session whose
 * host already plays is sent everything a level says as well: mover events, removals, bolts and
 * the level's state. A mover event with no level waits and then fires into whatever level
 * this side loads next, and a removal names an actor of a level that is not here. So the lobby
 * takes only what a lobby is for and drops the rest, counted by what it was.
 *
 * What a lobby takes: the roster, the host's setup, the content fingerprint, the savegame's three
 * notes and the shared story, which is state and is written by nothing until a level runs. The
 * one-byte slot note carries no tag and is read before any of this.
 *
 * The repeated states of a level come back by themselves once the level runs; what the host says
 * only once, the movers, the push blocks, the appearance and the campaign, it says again when the
 * player enters its world. So dropping here loses nothing a level needs.
 *
 * Every tag of the wire has a row, and the band ends at the newest tag. The unit test walks the
 * band and fails on a tag inside it that has no row or two, and it holds MP_LOBBY_NOTE_BAND_LAST
 * below at the newest tag it knows, the host's settings. A tag past the band is UNKNOWN to the rule
 * and dropped in a lobby, so a new tag raises the band beside its row, and the test's newest tag
 * with it.
 */
#ifndef MULTIPLAYER_MP_LOBBY_NOTE_RULE_H
#define MULTIPLAYER_MP_LOBBY_NOTE_RULE_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

/* What a note is to a client in its lobby. Every class but LOBBY is dropped there. */
typedef enum mp_lobby_note_class {
    MP_LOBBY_NOTE_UNKNOWN = 0,      /* a first byte no row names, or no note at all */
    MP_LOBBY_NOTE_LOBBY,            /* taken: what a lobby is for */
    MP_LOBBY_NOTE_MOVER,            /* a mover event, the map's digest and the map's state */
    MP_LOBBY_NOTE_REMOVAL,          /* an actor the host removed */
    MP_LOBBY_NOTE_BOLT,             /* a bolt one of the host's actors fired */
    MP_LOBBY_NOTE_LEVEL_STATE,      /* what the host's scripts switched on its level */
    MP_LOBBY_NOTE_LEVEL_OTHER,      /* anything else a running level says */
    MP_LOBBY_NOTE_NEVER_TO_CLIENT,  /* a player's word to its host, which no host sends on */
    MP_LOBBY_NOTE_CLASSES
} mp_lobby_note_class_t;

/* The first and the last tag the table knows. The band below the first holds event kinds carried
 * inside a message, which are no tags of their own. The last is the highest tag on the wire, and
 * the unit test holds it there; a new tag raises it here, beside its row, or the rule answers
 * UNKNOWN for the new tag and a lobby drops it. */
#define MP_LOBBY_NOTE_BAND_FIRST 0x81u
#define MP_LOBBY_NOTE_BAND_LAST  0xABu

/* The class of one reliable note, by its first byte. */
mp_lobby_note_class_t mp_lobby_note_class(const uint8_t *note, size_t bytes);

/* Whether a client in its lobby takes a note of this class. */
bool mp_lobby_note_taken_in_lobby(mp_lobby_note_class_t note_class);

#endif /* MULTIPLAYER_MP_LOBBY_NOTE_RULE_H */
