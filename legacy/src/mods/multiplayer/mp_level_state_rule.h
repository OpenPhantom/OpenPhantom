/* mp_level_state_rule.h: what a host's scripts have switched on its level, as a note and as the
 * decisions a client makes about it.
 *
 * Layer 1, pure. No address and no game in the process: the reading of the engine and the calls
 * into it are in mp_level_state_bind.c, and mp_level_state.c runs the two.
 *
 * A client parks every actor the host lists, a parked actor runs no script, and so nothing a
 * script switches on the level happens on the client: the steam of a gas room, the sparks of a
 * leaking droid, a light going out, the green fog. What a script switches there is STATE with one
 * writer. The host reads it out of its own memory, not out of the calls that made it, and says it
 * whole: on every change, at most once in four substeps with the newest winning, and once a
 * second as a repeat, so a player who arrives late or loads a savegame is told the level as it
 * stands. A client holds the newest note and applies it at the start of a substep.
 *
 * What travels, and why each part has the shape it has:
 *
 *   EMITTER PLACEMENTS, two bits each: never spawned, on, off, or spawned and gone. The engine
 *   keeps the pool slot a placement spawned into and never clears it when that emitter ends, and
 *   three shipped placements carry a template with a lifetime and end by themselves. A slot that
 *   no longer holds the placement's own emitter (a free slot, or one somebody else took) is
 *   "gone": the host says so and a client does nothing for it, rather than switching a stranger's
 *   emitter off or raising a cloud that ended on the host long ago. On and off are compared by
 *   what is SEEN: never spawned and off are both nothing on screen.
 *
 *   LEVEL LIGHTS, one bit each, the whole first pool. Only loading, a savegame and a script write
 *   the active word of these records.
 *
 *   The DIRECTOR'S FOG, as a state in the shape the engine saves it in: the start the ramp works
 *   from, the end, the ramp's target, its length and what is left of it, the colour a ramp set and
 *   whether the level's own fog was put back. Not the device's cells: how fog reaches the device
 *   differs by machine (another fix scales the level's band), and the script's values replayed
 *   through the director give each machine what its own single player would. A ramp's length is
 *   simulation time: the engine counts it down by the length of its substep, 1/32 s or 1/64 s
 *   under the frame rate cheat, so the number a script passes is seconds, and what is left travels
 *   in whole seconds, the only unit the director replays.
 *   The fog is said once: each change that took effect rides the journal below, and the state
 *   heals a client that meets a level's first note or lost entries.
 *
 *   SOUND PLACEMENTS do not travel. The one script that switches one in the eleven levels names a
 *   placement that is authored active, and the engine's own switch returns at once for an active
 *   one, so there is never anything to match. The bit they had reserved carries the loops now.
 *
 * Four more parts: the LOOPS an actor's script keeps playing, one sound call per key and life; the
 * FOG each viewer sees for itself, by the actor whose script makes it, that script, whether it
 * runs, whether it has ended and where that actor stands; the ESCORT's health bar and whether it
 * is shown; and a JOURNAL of the last sixteen changes that took effect, each with a number of its
 * own. The journal is a window and not "what changed since the last note", because this note is a
 * state the channel replaces while it waits and a client keeps only the newest: a change that
 * rode only a replaced note would never arrive. A client plays the entries past the last number it
 * played and takes the rest from the state. The eighth bit is reserved, and a note that sets it is
 * refused like any bit this build does not know.
 */
#ifndef MULTIPLAYER_MP_LEVEL_STATE_RULE_H
#define MULTIPLAYER_MP_LEVEL_STATE_RULE_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

/* The tag, and a header no other message on the reliable channel has. */
#define MP_LEVEL_STATE_TAG          0xA3u
#define MP_LEVEL_STATE_HEADER_BYTES 12u   /* tag, tick, level, generation, parts */

/* Once a second as a repeat, and a change at most once in four substeps. */
#define MP_LEVEL_STATE_REPEAT_TICKS 32u
#define MP_LEVEL_STATE_CHANGE_TICKS 4u

/* What one note can describe. A byte counts the placements; the lights stop at 1023, well past the
 * 403 of the largest shipped level. */
#define MP_LEVEL_STATE_MAX_EMITTERS 255u
#define MP_LEVEL_STATE_MAX_LIGHTS   1023u

/* The director's fog: flags, the start the ramp works from, the end, the ramp's target and length
 * (each a float's bits), the whole seconds left of it, and the colour. */
#define MP_LEVEL_STATE_FOG_BYTES 22u
#define MP_LEVEL_FOG_HAS_START   0x01u   /* a start took effect since the level or a restore */
#define MP_LEVEL_FOG_HAS_END     0x02u   /* and an end */
#define MP_LEVEL_FOG_RAMP        0x04u   /* a ramp's target and length are set */
#define MP_LEVEL_FOG_KEEP_WORLD  0x08u   /* the ramp puts the level's own fog back at its end */
#define MP_LEVEL_FOG_COLOUR      0x10u   /* a ramp set the colour and switched the fog bit on */
#define MP_LEVEL_FOG_RESTORED    0x20u   /* a ramp ran out and put the level's own fog back */
#define MP_LEVEL_FOG_FLAGS       0x3Fu

#define MP_LEVEL_STATE_PART_EMITTERS    0x01u
#define MP_LEVEL_STATE_PART_LIGHTS      0x02u
#define MP_LEVEL_STATE_PART_ACTOR_LOOPS 0x04u
#define MP_LEVEL_STATE_PART_FOG         0x08u
#define MP_LEVEL_STATE_PART_FOG_VIEWERS 0x10u
#define MP_LEVEL_STATE_PART_ESCORT      0x20u
#define MP_LEVEL_STATE_PART_JOURNAL     0x40u
#define MP_LEVEL_STATE_PART_RESERVED    0x80u   /* a note that sets it is refused */

/* The loops, each a key, its life and the sound call it keeps playing. */
#define MP_LEVEL_STATE_LOOPS_MAX   16u
#define MP_LEVEL_STATE_LOOP_BYTES  5u

/* The fog a viewer sees for itself: the key and life of the actor whose script makes it, the
 * index of that script in the level, whether it runs and whether it has ended, and where that
 * actor stands in the enemy record's own quantisation. The script travels because a script can
 * be handed to an actor when it is woken, so the placement does not say which one runs; each side
 * then reads the fog out of its own copy of that script. Three scripts of the shipped levels set
 * fog at all. */
#define MP_LEVEL_STATE_FOG_VIEWERS_MAX   4u
#define MP_LEVEL_STATE_FOG_VIEWER_BYTES  12u
#define MP_LEVEL_STATE_FOG_VIEWER_ACTIVE 0x01u
#define MP_LEVEL_STATE_FOG_VIEWER_ENDED  0x02u

/* The escort's bar: its health, 0 to 100 as the engine clamps it, and whether it is shown. */
#define MP_LEVEL_STATE_ESCORT_BYTES  2u
#define MP_LEVEL_STATE_ESCORT_HEALTH 100u
#define MP_LEVEL_STATE_ESCORT_SHOWN  0x01u

/* The journal: the newest number, a count, and the entries oldest first. */
#define MP_LEVEL_STATE_JOURNAL_MAX   16u
#define MP_LEVEL_STATE_JOURNAL_BYTES 10u   /* sequence, kind, a, b, c */

#define MP_LEVEL_STATE_MAX_BYTES                                                                  \
    (MP_LEVEL_STATE_HEADER_BYTES + 1u + (2u * MP_LEVEL_STATE_MAX_EMITTERS + 7u) / 8u + 2u +      \
     (MP_LEVEL_STATE_MAX_LIGHTS + 7u) / 8u + MP_LEVEL_STATE_FOG_BYTES +                          \
     1u + MP_LEVEL_STATE_LOOPS_MAX * MP_LEVEL_STATE_LOOP_BYTES + 1u +                             \
     MP_LEVEL_STATE_FOG_VIEWERS_MAX * MP_LEVEL_STATE_FOG_VIEWER_BYTES +                            \
     MP_LEVEL_STATE_ESCORT_BYTES + 3u + MP_LEVEL_STATE_JOURNAL_MAX * MP_LEVEL_STATE_JOURNAL_BYTES)

/* One emitter placement as a side sees it. */
typedef enum mp_level_emitter {
    MP_LEVEL_EMITTER_NEVER = 0,   /* no slot: never spawned, or its spawn found nothing */
    MP_LEVEL_EMITTER_ON,
    MP_LEVEL_EMITTER_OFF,         /* its own slot, switched off */
    MP_LEVEL_EMITTER_GONE         /* a slot that is no longer its own emitter */
} mp_level_emitter_t;

/* The director's fog in the fields the engine saves for it. The floats travel as their bits: they
 * are a script's own values, handed back to the director as they came. */
typedef struct mp_level_fog_state {
    uint8_t  flags;       /* MP_LEVEL_FOG_ */
    uint32_t cur;         /* the start a ramp works from: the last start set, or the target of
                           * the last ramp once it ran out */
    uint32_t end;         /* the last end set */
    uint32_t target;      /* the start the ramp walks to */
    uint32_t span;        /* the ramp's length in seconds */
    uint16_t left;        /* the whole seconds of it still to run, rounded up; 0 when none runs */
    uint8_t  colour[3];   /* red, green, blue, as the last ramp that set a colour gave them */
} mp_level_fog_state_t;

typedef struct mp_level_loop {
    uint16_t key;
    uint8_t  life;
    uint16_t call;       /* the level's sound call */
} mp_level_loop_t;

typedef struct mp_level_fog_viewer {
    uint16_t key;
    uint8_t  life;
    uint16_t script;     /* the script's index in the level */
    uint8_t  flags;      /* MP_LEVEL_STATE_FOG_VIEWER_ */
    uint16_t place[3];
} mp_level_fog_viewer_t;

/* What one journal entry says, by kind. */
typedef enum mp_level_journal_kind {
    MP_LEVEL_JOURNAL_NONE = 0,
    MP_LEVEL_JOURNAL_EMITTER,   /* a: 1 on, 0 off; b: the placement */
    MP_LEVEL_JOURNAL_LIGHT,     /* a: 1 on, 0 off; b: the light */
    MP_LEVEL_JOURNAL_FOG,       /* a: the command; b: its frames; c: its float's bits */
    MP_LEVEL_JOURNAL_CRAWL,     /* b: the text the line crawls */
    MP_LEVEL_JOURNAL_ESCORT,    /* a: the health */
    MP_LEVEL_JOURNAL_KINDS
} mp_level_journal_kind_t;

typedef struct mp_level_journal_entry {
    uint16_t sequence;   /* never 0 */
    uint8_t  kind;
    uint8_t  a;
    uint16_t b;
    uint32_t c;
} mp_level_journal_entry_t;

typedef struct mp_level_state_note {
    uint32_t       tick;         /* the host's substep */
    uint16_t       level;        /* the same two bytes the digest and the enemy block carry */
    uint32_t       generation;   /* bank transitions the host has seen */
    uint8_t        parts;
    uint16_t       emitters;
    uint8_t        emitter[MP_LEVEL_STATE_MAX_EMITTERS];   /* mp_level_emitter_t each */
    uint16_t       lights;
    uint8_t        light[(MP_LEVEL_STATE_MAX_LIGHTS + 7u) / 8u];
    mp_level_fog_state_t  fog;
    uint8_t               loops;
    mp_level_loop_t       loop[MP_LEVEL_STATE_LOOPS_MAX];
    uint8_t               fog_viewers;
    mp_level_fog_viewer_t fog_viewer[MP_LEVEL_STATE_FOG_VIEWERS_MAX];
    uint8_t               escort_health;
    uint8_t               escort_flags;
    uint16_t                 journal_newest;   /* the host's newest number, 0 before any */
    uint8_t                  journal_count;
    mp_level_journal_entry_t journal[MP_LEVEL_STATE_JOURNAL_MAX];   /* oldest first */
} mp_level_state_note_t;

/* ==============================================================================================
 * The codec. A note is read whole or refused whole.
 * ============================================================================================ */

void   mp_level_state_note_init(mp_level_state_note_t *note, uint32_t tick, uint16_t level,
                                uint32_t generation);
bool   mp_level_state_light_on(const mp_level_state_note_t *note, size_t index);
void   mp_level_state_set_light(mp_level_state_note_t *note, size_t index, bool on);
size_t mp_level_state_bytes(const mp_level_state_note_t *note);
size_t mp_level_state_encode(const mp_level_state_note_t *note, uint8_t *buffer, size_t capacity);
bool   mp_level_state_is_note(const uint8_t *buffer, size_t bytes);
bool   mp_level_state_decode(const uint8_t *buffer, size_t bytes, mp_level_state_note_t *out);

/* Whether two encoded notes say the same, the tick aside: a change is a difference here. */
bool mp_level_state_same(const uint8_t *a, size_t a_bytes, const uint8_t *b, size_t b_bytes);

/* ==============================================================================================
 * The emitter placements.
 * ============================================================================================ */

#define MP_LEVEL_EMITTER_NAME_BYTES 16u

/* Whether a pool slot still holds the placement's own emitter: in use, spawned under the
 * placement's template name and at the placement's own position, which the allocator copies in
 * unchanged. */
bool mp_level_state_emitter_owned(uint32_t type, const char *name, const char *template_name,
                                  const float *position, const float *place_position);

/* What a side sees of one placement. `live_index` below zero is no slot; `owned` is the answer
 * above, false as well for a slot that did not read or lies past the pool. */
mp_level_emitter_t mp_level_state_emitter_seen(int32_t live_index, bool owned, bool disabled);

typedef enum mp_level_act {
    MP_LEVEL_ACT_NONE = 0,
    MP_LEVEL_ACT_ON,          /* switch the placement on here, spawning it if it never was */
    MP_LEVEL_ACT_OFF,         /* switch it off here */
    MP_LEVEL_ACT_NOT_OWNED    /* it differs, and its slot here is not its own: left alone */
} mp_level_act_t;

/* What a client does for one placement. Nothing for one the host calls gone. */
mp_level_act_t mp_level_state_emitter_act(mp_level_emitter_t host, mp_level_emitter_t here);

/* ==============================================================================================
 * The director's fog.
 * ============================================================================================ */

/* The four commands that set fog, and the only ones a client may replay through the director
 * with a stand-in for the actor: none of their arms reads the actor. The director's class table
 * answers it (mp_director_rule.h). */
bool mp_level_state_fog_command(int32_t command);

/* The two of them that ramp over a number of seconds: 11 to pale green and 12 back to black. */
bool mp_level_state_fog_is_ramp(int32_t command);

/* Whether a ramp's length fits a journal entry, which carries it in sixteen bits. The engine
 * takes an int and ignores anything below one. */
bool mp_level_state_fog_fits(int32_t a1);

/* ==============================================================================================
 * When a host sends, and what a client takes.
 * ============================================================================================ */

typedef enum mp_level_send {
    MP_LEVEL_SEND_NONE = 0,
    MP_LEVEL_SEND_CHANGE,   /* something differs from the last note sent */
    MP_LEVEL_SEND_REPEAT,   /* a second has passed */
    MP_LEVEL_SEND_HELD      /* something differs and the throttle holds it back */
} mp_level_send_t;

/* `sent_before` false is a level with no note yet, and its first one goes at once. */
mp_level_send_t mp_level_state_due(bool changed, bool sent_before, uint32_t since_sent,
                                   bool change_before, uint32_t since_change);

typedef enum mp_level_order {
    MP_LEVEL_ORDER_TAKE = 0,
    MP_LEVEL_ORDER_OTHER_LEVEL,
    MP_LEVEL_ORDER_OLD_GENERATION,
    MP_LEVEL_ORDER_OLD_TICK
} mp_level_order_t;

/* What a client has taken before in this run, for the order below. */
typedef struct mp_level_taken {
    bool     any;          /* a note has been taken since the last reset */
    uint16_t level;
    uint32_t generation;
    uint32_t tick;
    uint32_t newest_generation;
} mp_level_taken_t;

/* Whether a client takes a note in the level it has open. A note about another level is refused,
 * so is one of a generation older than the newest taken, and inside one level and generation a
 * tick that is not after the last one taken. The tick is compared nowhere else: a host's substep
 * count starts over with a session. `first` says the note is the first of its level and
 * generation here. */
mp_level_order_t mp_level_state_order(const mp_level_taken_t *taken, uint16_t here_level,
                                      uint16_t level, uint32_t generation, uint32_t tick,
                                      bool *first);

#endif /* MULTIPLAYER_MP_LEVEL_STATE_RULE_H */
