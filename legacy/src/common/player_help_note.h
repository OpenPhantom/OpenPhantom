/* player_help_note.h: two buttons of the developer menu, asked of the multiplayer session and
 * answered by it.
 *
 * The developer overlay draws two buttons under its Multiplayer heading, "Repair lock" and
 * "Teleport to host". What they do is the multiplayer's to do: it knows what holds a player's
 * controls and camera, where the host stands and how a living player is moved. The two are
 * feature DLLs and may not call each other, so a button files an ASK in one record and the panel
 * reads the ANSWER out of a second one. Each record has exactly one writer: the overlay writes
 * the ask, the multiplayer writes the answer.
 *
 * Both records are STATE, not a queue. The ask holds the last press and nothing before it; the
 * answer holds what came of one press, and whether anybody listens. Reading either of them twice
 * is harmless, because only a serial the reader has not acted on yet starts anything. A press
 * that the next press overwrites before the reader looked is lost, and that is what pressing
 * again means.
 *
 * THE SERIAL counts presses over the life of the process. It is never 0 for a press and it never
 * starts over. The overlay files an empty ask, kind NONE and serial 0, when it loads, so the
 * reader finds the note at its first look and does not ask the operating system on every frame
 * for a name nobody filed.
 *
 * THE MARK is what keeps an old ask from ever firing. The notes live as long as the process, so
 * a press made in single player, or in the session before this one, is still on file when the
 * next session begins. The reader therefore takes the serial it finds at the moment it arms as
 * its mark and acts only on a press made after the mark, which is what
 * player_help_serial_after() answers. No ask note at all at that moment is a mark of 0, and the
 * first press carries 1.
 *
 * THE ANSWER names the serial and the kind it answers, so the panel can tell an answer to its
 * last press from one to an older press. OPEN says the ask was taken and is still being worked
 * on: a teleport waits for a place beside the host and for the fade. DONE, NOTHING and REFUSED
 * end it. NOTHING is the press that found nothing to do, and it is no failure: nothing held the
 * player, or the player stands beside the host already. `reason` says why for NOTHING and for
 * REFUSED. `released` is the multiplayer's own bit set of what a repair gave back; the panel
 * reads no meaning into it and only writes it to its log.
 *
 * An OPEN answer has to be ended by its writer on every way out, the end of the session
 * included. The panel greys a button for as long as the answer to its last press stands OPEN,
 * and an answer left that way would grey it into the next session.
 *
 * READY is how the panel learns that somebody listens. The multiplayer sets the bits when a
 * session arms its reader, and it publishes 0 at the one exit of the session. So `ready` 0, like
 * no answer note at all, means that nobody listens: there is no session, or the multiplayer in
 * this process does not know these records. The panel offers a button only under its own bit,
 * so no button can be pressed into a void. The two CAN bits say what the listener is able to do
 * on this executable and in this session.
 *
 * THE OVERLAY HOLDS flag of an ask says that the overlay itself still holds the player at the
 * moment the ask is filed. A press closes the panel first, because an open panel keeps the
 * player's module on its idle state, and the listener cannot tell that from a scene holding it.
 * What can still hold afterwards is the free camera in flight, which keeps the panel up under
 * it. The listener then must neither move that player nor hand the module back.
 */
#ifndef COMMON_PLAYER_HELP_NOTE_H
#define COMMON_PLAYER_HELP_NOTE_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#define PLAYER_HELP_ASK_NOTE_NAME    "player_help_ask"      /* written by dev_overlay */
#define PLAYER_HELP_ANSWER_NOTE_NAME "player_help_answer"   /* written by multiplayer */

/* The shape of the two records below. A publisher fills it in and a record of any other version
 * is neither published nor read, so a machine running one DLL of this pair and one from another
 * build shows the buttons greyed instead of acting on a record it cannot read. */
#define PLAYER_HELP_NOTE_VERSION 1u

/* Until the first read that found the other side's note, a reader that asks every frame asks the
 * operating system at most this often: a name nobody has filed costs a failed mapping lookup
 * each time. Once a read has found the note the mapping stays open and a read is a copy. */
#define PLAYER_HELP_NOTE_RETRY_MS 1000u

enum {
    PLAYER_HELP_KIND_NONE     = 0,   /* the empty ask filed at load, and an answer to nothing */
    PLAYER_HELP_KIND_REPAIR   = 1,   /* give controls and camera back to this player */
    PLAYER_HELP_KIND_TELEPORT = 2    /* put this player beside the host */
};

/* The overlay still holds the player when the ask is filed: the free camera flies. */
#define PLAYER_HELP_ASK_F_OVERLAY_HOLDS 0x01u

typedef struct player_help_ask {      /* 8 bytes, no padding */
    uint16_t version;
    uint8_t  kind;      /* the last press; NONE in the empty note filed at load */
    uint8_t  flags;     /* PLAYER_HELP_ASK_F_*, 0 in the empty note */
    uint32_t serial;    /* counts presses over the life of the process, never 0 for a press,
                         * never starts over */
} player_help_ask_t;

#define PLAYER_HELP_READY_LISTENING    0x01u   /* a session's reader is armed and looks */
#define PLAYER_HELP_READY_CAN_REPAIR   0x02u   /* and a repair would be carried out */
#define PLAYER_HELP_READY_CAN_TELEPORT 0x04u   /* and so would a teleport to the host */

enum {
    PLAYER_HELP_OUTCOME_NONE = 0,   /* nothing answered yet */
    PLAYER_HELP_OUTCOME_OPEN,       /* taken, and still being worked on */
    PLAYER_HELP_OUTCOME_DONE,       /* carried out */
    PLAYER_HELP_OUTCOME_NOTHING,    /* looked at, and there was nothing to do */
    PLAYER_HELP_OUTCOME_REFUSED     /* not carried out, `reason` says why */
};

enum {
    PLAYER_HELP_REASON_NONE = 0,
    PLAYER_HELP_REASON_NO_LEVEL,          /* no level of a started session runs here */
    PLAYER_HELP_REASON_DEAD,              /* this player is dead; the re-entry brings them back */
    PLAYER_HELP_REASON_AT_A_GUN,          /* this player mans a gun emplacement */
    PLAYER_HELP_REASON_BUSY,              /* a re-entry, a move or another teleport is under way */
    PLAYER_HELP_REASON_IS_HOST,           /* this machine is the host */
    PLAYER_HELP_REASON_HOST_ELSEWHERE,    /* the host stands in another level */
    PLAYER_HELP_REASON_HOST_HAS_NO_BODY,  /* no pose of the host has arrived in this level yet */
    PLAYER_HELP_REASON_HOST_DEAD,         /* the host is dead */
    PLAYER_HELP_REASON_NEAR_ALREADY,      /* this player stands beside the host already */
    PLAYER_HELP_REASON_NO_SEAT,           /* no free place beside the host was found */
    PLAYER_HELP_REASON_OVERLAY_HOLDS,     /* the ask carried PLAYER_HELP_ASK_F_OVERLAY_HOLDS */
    PLAYER_HELP_REASON_NOT_BOUND,         /* the engine sites it needs did not resolve */
    PLAYER_HELP_REASON_GAVE_UP,           /* the wait for the body or for the place ran out */
    PLAYER_HELP_REASON_MENU_OPEN,         /* a menu of the game is open and was left as it is */
    PLAYER_HELP_REASON_CONVERSATION,      /* a conversation's answer list is open */
    PLAYER_HELP_REASON_WORLD_CHANGED,     /* the level changed under the ask */
    PLAYER_HELP_REASON_COUNT
};

typedef struct player_help_answer {   /* 12 bytes, no padding */
    uint16_t version;
    uint8_t  ready;     /* PLAYER_HELP_READY_* bits; 0 = nobody listens (no session) */
    uint8_t  outcome;   /* PLAYER_HELP_OUTCOME_* for `serial` */
    uint32_t serial;    /* the ask this answers; 0 = none yet */
    uint8_t  kind;      /* the kind of that ask */
    uint8_t  reason;    /* PLAYER_HELP_REASON_*, why REFUSED or NOTHING */
    uint16_t released;  /* repair: a bit set of what was given back, opaque to the panel */
} player_help_answer_t;

/* Files a record under its name. The caller fills in every field, the version included. False
 * when the record is not sound or the channel refused; nothing was published then.
 *
 * Sound is: this version; a kind, an outcome and a reason this build knows; and no flag and no
 * ready bit it does not know. An ask is held to one thing more, because its reader acts on it:
 * the empty ask is kind NONE with serial 0 and no flag, and a press is any other kind with a
 * serial that is not 0. */
bool player_help_ask_publish(const player_help_ask_t *ask);
bool player_help_answer_publish(const player_help_answer_t *answer);

/* Reads a record back. False when nobody published one, when the read lost its race against a
 * write, and when what is filed is of another size, of another version or not sound; `out` is
 * left alone then. */
bool player_help_ask_read(player_help_ask_t *out);
bool player_help_answer_read(player_help_answer_t *out);

/* Whether `serial` is a press made after `mark`, safe across the wrap of the counter. */
bool player_help_serial_after(uint32_t serial, uint32_t mark);

#endif /* COMMON_PLAYER_HELP_NOTE_H */
