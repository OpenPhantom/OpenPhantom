/* mp_announce.h: what a host says about itself to a room full of machines that have not asked.
 *
 * Layer 1, pure. Bytes in, a record out, and every refusal is a test.
 *
 * ================================ Why this is not a session message ===========================
 *
 * The reliable channel carries messages between two machines that have already agreed to talk. An
 * announce is the opposite of that: connectionless, addressed to everybody, and listened for by a
 * player who is sitting in the main menu with no session, no level and no peer. It therefore gets
 * its own datagram on its own port, which also means it can never disturb a connection that is
 * running.
 *
 * ============================== Why a browser may run in the menu =============================
 *
 * An announce authenticates nothing and establishes nothing; it answers "who is there", and the
 * listener needs no fingerprint of its own to hear one. The fingerprint it carries is the sender's
 * statement folded into one number: the game data and the build of the multiplayer, which stand
 * from the moment its transport is armed, so a host announces it from its lobby on. A listener can
 * make its own statement in the menu before any transport, and holding the two against each other
 * lets a browser mark a session as unjoinable rather than sending the player into a refused join.
 *
 * ==================================== What is not in it =======================================
 *
 * THE ADDRESS. It is the datagram's source address and the receiver already has it. A field for it
 * would be the sender's claim about itself, and behind a NAT that claim is wrong. What IS carried
 * is the GAME port, because the announce leaves from the announce port and a join goes to the game
 * port, and those are two different numbers.
 */
#ifndef MULTIPLAYER_MP_ANNOUNCE_H
#define MULTIPLAYER_MP_ANNOUNCE_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

/* The port the announce is broadcast to and listened for on. Deliberately not the game port: a
 * host binds the game port for the session and this must work before and beside that. */
#define MP_ANNOUNCE_PORT 27961u

/* "OBIA", so that a stray datagram from something else on this port is refused on its first four
 * bytes rather than parsed. */
#define MP_ANNOUNCE_MAGIC 0x4149424Fu

/* The announce's own version, which is not the wire version. The two change for different reasons:
 * the wire version changes when the SIMULATION's messages change, and a browser must still be able
 * to read the announce of a host it cannot join, or it could not tell the player why. */
#define MP_ANNOUNCE_VERSION 1u

/* The session name, as shown in the browser's first column. Long enough for a sentence a person
 * would actually type and short enough that the whole announce stays far inside one datagram. */
#define MP_ANNOUNCE_NAME_MAX 24u

/* magic 4, version 1, wire 1, fingerprint 4, game port 2, players 1, slots 1, flags 1, name. */
#define MP_ANNOUNCE_BYTES (15u + MP_ANNOUNCE_NAME_MAX)

/* The flag byte. The mode is a bit rather than a number because the feature has two modes, team
 * deathmatch and co-op; a third would become a two-bit field and a new announce version. */
#define MP_ANNOUNCE_F_PASSWORD 0x01u   /* the session asks for one before it lets anybody in */
#define MP_ANNOUNCE_F_TDM      0x02u   /* clear means co-op, which is the default mode */
#define MP_ANNOUNCE_F_MODDED   0x04u   /* something beyond this feature is loaded and it
                                        * changes play */
#define MP_ANNOUNCE_F_LOCKED   0x08u   /* in progress and not taking anybody else */
#define MP_ANNOUNCE_F_KNOWN    0x0Fu   /* every bit above; anything else is a newer sender */

typedef struct mp_announce {
    uint8_t  version;            /* DECODE ONLY: the sender's announce version. The encoder stamps
                                  *  its own, because that is a fact about the sender rather than
                                  *  something a caller gets to choose */
    uint8_t  wire;               /* MP_WIRE_VERSION of the sender */
    uint32_t fingerprint;        /* the sender's statement as one number, 0 before it has one */
    uint16_t game_port;          /* where a join goes, which is not where this came from */
    uint8_t  players;            /* how many are in it now, the host included */
    uint8_t  slots;              /* how many it holds */
    uint8_t  flags;
    char     name[MP_ANNOUNCE_NAME_MAX];
} mp_announce_t;

/* Printable ASCII, at least one character, NUL terminated inside the field: the same rule the
 * roster applies to a player's name, for the same two reasons, the engine's font has no glyph for
 * anything else, and it ends up in a log. */
bool mp_announce_name_is_sound(const char *name);

/* Copies a name in, replacing what is not printable with '?', cutting at the field, trimming the
 * blanks off both ends, and putting a default in an empty one, so that the test above always
 * passes afterwards. */
void mp_announce_name_clean(const char *from, char out[MP_ANNOUNCE_NAME_MAX]);

/* Encodes to exactly MP_ANNOUNCE_BYTES, or 0 when the buffer is too small, the name is not sound,
 * the game port is zero, or the session claims more players than it has slots. */
size_t mp_announce_encode(const mp_announce_t *announce, uint8_t *buffer, size_t capacity);

/* True for a buffer that is an announce by magic and length. Cheap, so a listener can drop a
 * stray datagram without decoding it. */
bool mp_announce_is_announce(const uint8_t *buffer, size_t bytes);

/* Decodes. Refuses a bad magic, a length that is not exact, a version this build does not know, a
 * zero game port, a name that is not sound, and a player count past the slots. On false nothing of
 * `out` is to be trusted.
 *
 * It does NOT refuse an unknown flag bit or an unknown wire version: both mean a sender that is
 * newer than this build, and the whole point of showing it in the list is to be able to say so. */
bool mp_announce_decode(const uint8_t *buffer, size_t bytes, mp_announce_t *out);

/* Whether a listener on this wire version and fingerprint could actually join this session. The
 * browser shows the answer rather than acting on it: a player who sees WHY a server is greyed out
 * files a useful report, and one who is silently refused files "multiplayer is broken". */
bool mp_announce_joinable(const mp_announce_t *announce, uint8_t wire, uint32_t fingerprint);

/* Whether the session is full. Separate from joinable because a full server is a server to wait
 * for and a mismatched one is a server to give up on. */
bool mp_announce_full(const mp_announce_t *announce);

#endif /* MULTIPLAYER_MP_ANNOUNCE_H */
