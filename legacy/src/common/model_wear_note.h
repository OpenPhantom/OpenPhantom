/* model_wear_note.h: which far body should wear which borrowed model, and what came of it.
 *
 * A far player who wears a model over their hero is drawn on this machine by two feature DLLs that
 * may not call each other. The multiplayer owns the far bodies: it builds them, takes them down and
 * runs their tick inside its bank windows. The developer overlay owns the model swap: the rebind of
 * a render handle, the translation of the hero's clips onto the borrowed rig by node name, and the
 * hooks that make both hold every frame. So the multiplayer says what it wants in one record
 * and the overlay answers in another, each with exactly one writer.
 *
 * The records are STATE, not a queue. Each bank stands on its own and a repetition is harmless:
 * the overlay reads the wish every scene end and answers the body serial it acted on, and the
 * multiplayer takes only an answer for the serial the bank has now.
 *
 * THE BODY SERIAL is what tells one body from the next. The object address does not: the engine's
 * object list hands back the lowest free slot, and a rebuilt far body sat at the same address three
 * times in one field run. The multiplayer counts a serial per bank at every successful spawn and
 * every successful take down.
 *
 * THE ECHO is what the body wears AFTER the answer: the model for WORN, the model it still wears
 * for a refusal because it wears another one, and nothing for every other refusal. The
 * multiplayer rebuilds a body whose echo is not what it wants, so an echo of the refused name
 * would leave a body in the old model for good.
 *
 * THE WEAPON of a WORN answer says whether that body carries its player's own weapon at the hand
 * of the rig it is wearing. It is what tells a body that can strike apart from one whose swing
 * reaches for a node the borrowed rig does not carry, and the multiplayer withholds the contact,
 * the swept blade and, in a deathmatch, the wish itself from a body that has none.
 *
 * The one write into the other DLL's memory. For a WORN answer the overlay writes exactly the six
 * node index words at +0x40..+0x54 of the bank block the wish names, at the end of a scene,
 * after it has checked that no bank window is open, for the current serial only, and only after
 * the rebind succeeded. Nothing else crosses: the block belongs to the multiplayer.
 */
#ifndef COMMON_MODEL_WEAR_NOTE_H
#define COMMON_MODEL_WEAR_NOTE_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

/* How many far bodies the records describe. The multiplayer holds its own count against this at
 * compile time, and a record for more banks must still fit the shared note's payload. */
#define MODEL_WEAR_BANKS    3u
#define MODEL_WEAR_NAME_MAX 32u

#define MODEL_WEAR_WANT_NOTE_NAME "model_wear_want"
#define MODEL_WEAR_DONE_NOTE_NAME "model_wear_done"

/* The shape of the two records below. A reader answers false for a version it does not know, so
 * a machine running one DLL of this pair and one of another shows far players as their heroes
 * instead of acting on a record it cannot read. The two DLLs ship as one mod and change over
 * together. Version 2 named the weapon flag of an answer and the ready bit beside it. */
#define MODEL_WEAR_NOTE_VERSION 2u

/* What a far bank asks for. Bank b of the multiplayer is entry b - 1. */
typedef struct model_wear_want_bank {
    uint32_t serial;                      /* the bank's body serial, 0 = never built */
    uint32_t object;                      /* the body's bapObj for every standing bank, 0 = none */
    uint32_t block;                       /* the bank's persistent player block */
    uint8_t  slot;                        /* the hero slot the body was actually built on */
    uint8_t  reserved[3];
    char     model[MODEL_WEAR_NAME_MAX];  /* the model wanted over it, empty = none */
} model_wear_want_bank_t;

typedef struct model_wear_want_record {
    uint16_t               version;
    uint16_t               reserved;
    model_wear_want_bank_t bank[MODEL_WEAR_BANKS];
} model_wear_want_record_t;

/* The overlay's answer for one bank. */
#define MODEL_WEAR_STATE_NONE    0u
#define MODEL_WEAR_STATE_WORN    1u
#define MODEL_WEAR_STATE_REFUSED 2u

#define MODEL_WEAR_REASON_NONE         0u
#define MODEL_WEAR_REASON_NO_ROW       1u   /* the name is no row of the model roster */
#define MODEL_WEAR_REASON_NO_ASSET     2u   /* the asset or its model would not load */
#define MODEL_WEAR_REASON_FIT          3u   /* too few of the hero's node names on the rig */
#define MODEL_WEAR_REASON_NOT_ABLE     4u   /* the swap's sites, guard or translation are missing */
#define MODEL_WEAR_REASON_BLADE        5u   /* a Jedi body, and the blade guard has not been seen */
#define MODEL_WEAR_REASON_NOT_FRESH    6u   /* the body does not wear its own actor's model */
#define MODEL_WEAR_REASON_WEARS_OTHER  7u   /* the body wears another model; the echo names it */
#define MODEL_WEAR_REASON_NO_ROOM      8u   /* no body or pair left in the translation's tables */
#define MODEL_WEAR_REASON_BIND_FAILED  9u   /* the rebind failed and the body is its hero again */
#define MODEL_WEAR_REASON_NO_HAND      10u  /* the rig has no node a push could leave from */
#define MODEL_WEAR_REASON_BAD_BLOCK    11u  /* the block the wish names is not one to write */
#define MODEL_WEAR_REASON_BROKEN       12u  /* the rebind AND the way back failed: rebuild it */
#define MODEL_WEAR_REASON_MAX          MODEL_WEAR_REASON_BROKEN

typedef struct model_wear_done_bank {
    uint32_t serial;                      /* the body serial this answers */
    uint8_t  state;                       /* MODEL_WEAR_STATE_* */
    uint8_t  reason;                      /* MODEL_WEAR_REASON_*, NONE unless refused */
    uint8_t  weapon;                      /* 1 when the WORN body carries its player's weapon */
    uint8_t  reserved;
    float    scale;                       /* the worn asset's own scale for WORN, else 0 */
    char     model[MODEL_WEAR_NAME_MAX];  /* the echo: what the body wears after the answer */
} model_wear_done_bank_t;

/* The overlay is loaded and listening, it is able to put a model on a far body, and the places a
 * borrowed weapon is drawn and shot from have resolved.
 *
 * The last bit is a READING and not a hook. The detours that draw and measure a borrowed weapon
 * go in at the first far dressing, so a bit that waited for them would never come on a machine
 * whose own player never swapped a model, and the deathmatch that waits for the bit would never
 * ask for one. Resolving is a walk over untouched bytes and may happen at load. */
#define MODEL_WEAR_READY_LISTENING 0x01u
#define MODEL_WEAR_READY_ABLE      0x02u
#define MODEL_WEAR_READY_WEAPON    0x04u

typedef struct model_wear_done_record {
    uint16_t               version;
    uint8_t                ready;         /* MODEL_WEAR_READY_* */
    uint8_t                reserved;
    model_wear_done_bank_t bank[MODEL_WEAR_BANKS];
} model_wear_done_record_t;

/* An asset name either record may carry: empty, or the engine's 8.3 file name rule (at most eight
 * letters, digits or underscores, then optionally a dot and at most three), terminated inside the
 * field and zero after the terminator, so two records that say the same thing are the same
 * bytes. */
bool model_wear_name_is_sound(const char name[MODEL_WEAR_NAME_MAX]);

/* Whether a record is one the other side may act on: sound names, a block for every body, a known
 * state and reason, a finite scale inside 0.1..10 for WORN and zero otherwise, an echo only where
 * the state gives one, a weapon flag of 0 or 1 that is 0 for every state but WORN, and zero in
 * every reserved byte. The version is the reader's to check. */
bool model_wear_want_is_sound(const model_wear_want_record_t *record);
bool model_wear_done_is_sound(const model_wear_done_record_t *record);

/* Files a record under its name with the version filled in. False when the record is not sound or
 * the channel refused; nothing was published then. */
bool model_wear_publish_want(const model_wear_want_record_t *record);
bool model_wear_publish_done(const model_wear_done_record_t *record);

/* Reads a record back. False when nobody published one, when it is torn, of another version or not
 * sound; `out` is then left alone. `serial`, when not NULL, receives the publication count. */
bool model_wear_read_want(model_wear_want_record_t *out, uint32_t *serial);
bool model_wear_read_done(model_wear_done_record_t *out, uint32_t *serial);

#endif /* COMMON_MODEL_WEAR_NOTE_H */
