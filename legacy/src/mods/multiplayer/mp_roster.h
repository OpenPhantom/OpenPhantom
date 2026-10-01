/* mp_roster.h: who is in the session, as one message everybody gets.
 *
 * Layer 1, pure. No engine, no socket, no clock: bytes in, a table out, and every refusal is a
 * test.
 *
 * ===================================== What a roster is =======================================
 *
 * The authority of a session, the listen host or the dedicated server, knows every peer: its
 * name from the handshake, its slot, and the round trip its channel measures. A client knows its
 * own line and nothing else. The roster is the authority telling everyone the whole table, once a
 * second and whenever it changes, on the reliable channel, so that a player list, a lobby and a
 * ping display all read from one message rather than three.
 *
 * =============================== Why the length is the count ===================================
 *
 * Every recogniser on the reliable channel tells a message apart by its tag together with its
 * length. That is what made the first version of this message pad itself to sixteen entries: a
 * length that varies looked like it would turn a test into a range.
 *
 * It does not, because the count is IN the message, at a fixed place, before anything that varies.
 * The test is `tag is ours and bytes are exactly two plus count times the entry size`, which is as
 * exact as a constant and rejects a truncated message just as flatly. A range would only be needed
 * if two messages shared a tag, and none do.
 *
 * Padding was not merely wasteful, it was a starvation risk with nothing watching it. A packet
 * reserves its payload first and seats reliable messages in what is left (1187 less the 13 byte
 * packet header less the payload), so an 866 byte roster with its 4 byte message frame needed a
 * packet whose payload was 304 bytes or less. In a lobby that is every packet; in a level
 * where the enemy block runs to 974 bytes it can be none of them for a long stretch, and the
 * counter that would have shown it counts a refused encode rather than a message that found no
 * packet. Two players now cost 116 bytes, which rides anything.
 *
 * ================================= Why the appearance is in here ===============================
 *
 * Because the appearance EVENT is an edge and a late joiner has missed it. Somebody who arrives
 * after a player changed character would otherwise see that player wearing the level's own hero
 * for the rest of the session, with no message left that could tell them otherwise. The roster is
 * repeated once a second and on every change, so it carries state, and this is state.
 *
 * The asset name obeys the same rule as the one in the appearance event: the engine's own 8.3
 * resource gate. An EMPTY asset is allowed here and means "not known", which is what every line
 * says until somebody actually changes how they look.
 *
 * ===================================== The name on the wire ===================================
 *
 * Sixteen bytes, NUL terminated inside them, printable ASCII only. A name is shown on a screen the
 * engine draws with its own font, which has no glyph for anything else, and it is written into a
 * log; a byte that is neither is refused rather than passed to either. The same rule is applied
 * where the name enters the handshake, so a roster never carries a name the session would not.
 *
 * ============================== Team, ready and hero are the lobby's ===========================
 *
 * Three bytes the lobby sets and everybody reads: which team a player is on, whether they are
 * ready, and which hero they will play. They arrive at the authority as the lobby note (0x91) and
 * leave it in this table, so a player list, a lobby and a ping display all read one message. The
 * round trip is the authority's measurement to that peer in milliseconds, clamped to a u16; the
 * authority's own line carries zero.
 */
#ifndef MULTIPLAYER_MP_ROSTER_H
#define MULTIPLAYER_MP_ROSTER_H

#include "mp_events.h"

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#define MP_ROSTER_MAX_ENTRIES 16u
#define MP_ROSTER_NAME_MAX    16u
#define MP_ROSTER_ENTRY_BYTES 57u   /* slot, team, ready, hero, rtt u16, name, asset, scale,
                                     * and the asset's kind as the last byte */
/* What a roster of `n` players weighs on the wire, and what the largest one weighs. The second
 * is what a buffer is sized for; the first is what actually goes out. */
#define MP_ROSTER_BYTES_FOR(n) (2u + (size_t)(n) * MP_ROSTER_ENTRY_BYTES)
#define MP_ROSTER_BYTES       MP_ROSTER_BYTES_FOR(MP_ROSTER_MAX_ENTRIES)

/* The tag on the reliable channel. The event tags run 0x81..0x8E and the scratchpad holds 0x8B
 * and 0x8C; this is the next free one. */
#define MP_ROSTER_TAG 0x8Fu

typedef struct mp_roster_entry {
    uint8_t  slot;      /* the world slot the body rides: 0 is the listen host, 1.. the peers */
    uint8_t  team;      /* 0 until a lobby says otherwise */
    uint8_t  ready;     /* 0 or 1 */
    uint8_t  hero;      /* which of the shipped heroes this player takes (mp_lobby.h) */
    uint16_t rtt_ms;    /* the authority's round trip to this peer; 0 for the authority itself */
    char     name[MP_ROSTER_NAME_MAX];
    char     asset[MP_EVENT_ASSET_MAX];   /* the actor this player wears; empty means not known */
    uint16_t scale;     /* how big it is drawn, in hundredths of its own size; 0 for nobody said.
                         * The appearance event carries the same number, and for the same reason
                         * the asset is in both: the event is the edge and this is the state, so a
                         * player who joins after somebody became a giant learns it here */
    uint8_t  asset_kind; /* MP_SKIN_CHARACTER or MP_SKIN_MODEL, as the appearance event says it:
                          * whether `asset` is the actor this player embodies or a model worn
                          * over their hero's own body. Zero with an empty asset, so "not
                          * known" has one encoding. A late joiner that took a model for an
                          * actor built the far body out of a file with the wrong clip table */
} mp_roster_entry_t;

typedef struct mp_roster {
    uint8_t           count;
    mp_roster_entry_t entry[MP_ROSTER_MAX_ENTRIES];
} mp_roster_t;

/* Printable ASCII, at least one character, NUL terminated inside the field. */
bool mp_roster_name_is_sound(const char *name);

/* Copies a name into a field, replacing what is not printable with '?', cutting at the field, and
 * putting "Player" in an empty one: what a caller with a name off a keyboard or an ini uses so
 * that the sound test above always passes afterwards. */
void mp_roster_name_clean(const char *from, char out[MP_ROSTER_NAME_MAX]);

/* The asset name a player wears, under the engine's own 8.3 resource rule: at most one dot, at
 * most eight characters before it, at most three after it, and every other character a letter, a
 * digit or an underscore. Empty is sound and means the appearance is not known. */
bool mp_roster_asset_is_sound(const char *asset);

/* Copies an asset name into a field, and empties it if what arrives is not sound. What a caller
 * with a name read out of the engine uses, so that the encoder below never has to refuse a whole
 * roster over one appearance: a player list that fifteen people are waiting for must not be lost
 * because one machine read a name it did not expect. */
void mp_roster_asset_clean(const char *from, char out[MP_EVENT_ASSET_MAX]);

/* Encodes to exactly MP_ROSTER_BYTES_FOR(count), or 0 when the buffer is too small, the count is
 * past the
 * maximum, or a name or an asset is not sound. Entries past the count are written as zeros. */
size_t mp_roster_encode(const mp_roster_t *roster, uint8_t *buffer, size_t capacity);

/* True for a buffer that is a roster by tag and length. */
bool mp_roster_is_roster(const uint8_t *buffer, size_t bytes);

/* Decodes, refusing a count past the maximum, a name that is not sound and an asset that is not.
 * On false nothing of `out` is to be trusted. */
bool mp_roster_decode(const uint8_t *buffer, size_t bytes, mp_roster_t *out);

/* Field for field, the round trip included. What a decoder test uses to say that what came back
 * is what went out. NOT what a sender should gate on, see the next one and read why. */
bool mp_roster_equal(const mp_roster_t *a, const mp_roster_t *b);

/* Whether two tables differ in anything a PLAYER can see or act on: who is here, their slot,
 * team, hero, ready flag, name and appearance. The round trip is deliberately left out.
 *
 * This is the predicate a sender gates on, and using the one above instead flooded the reliable
 * channel for three days. rtt_ms is a live measurement, smoothed but never still: it moved almost
 * every substep, so "has the roster changed" answered yes almost every substep, and a note meant
 * to travel once a second was offered thirty two times a second. A field run counted 41 sent
 * against 3277 the channel had no room for, and every one of those offers was competing with a
 * savegame chunk for the same slot.
 *
 * The fresh ping is not lost by leaving it out: the unchanged table repeats once a second anyway,
 * and that is the rate a ping display is worth reading at. The comment in mp_bridge_roster.c said
 * so from the first day; only the code disagreed. */
bool mp_roster_differs_to_a_player(const mp_roster_t *a, const mp_roster_t *b);

#endif /* MULTIPLAYER_MP_ROSTER_H */
