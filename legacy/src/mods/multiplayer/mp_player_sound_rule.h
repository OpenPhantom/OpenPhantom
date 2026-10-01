/* mp_player_sound_rule.h: what a player's own body makes heard, and what a far machine does with
 * it, as arithmetic.
 *
 * Layer 1, pure. The engine plays a handful of sounds for the local player's body and for nobody
 * else: the death cry and, for a burning death, the burning cry; the pickup, the key, the plunge
 * into water and the burning ground. It also puts a shield round the body for a while when the
 * player takes a shield pickup. A far machine sees none of that, because its puppet of that player
 * runs none of the functions that make them. So the machine the player is on sends each one as a
 * moment of that player's body (mp_events.h, the player sound), and every machine that shows the
 * player plays it at the puppet through the engine's own entry, with the engine's own start gate
 * deciding who is near enough to hear it.
 *
 * What is here is every decision of that in a form a test can drive: who may send, which moments a
 * death sends, which table the far side reads, which flags the engine is handed, when a moment is
 * too old to be worth playing, and how long a shield may be worn.
 */
#ifndef MULTIPLAYER_MP_PLAYER_SOUND_RULE_H
#define MULTIPLAYER_MP_PLAYER_SOUND_RULE_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

/* The engine's table of sound names is 92 pointers long; an index at or past it names nothing. */
#define MP_PLAYER_SOUND_NAMES 92u

/* The two hero voice tables have one row per hero, and there are four heroes. */
#define MP_PLAYER_SOUND_HEROES 4u

/* The death causes the engine passes to its death entry, 0 to 4. Cause 3 is fire, which bursts the
 * body and adds the burning cry; cause 4 is a death a script ordered, for which the engine plays no
 * voice at all. */
#define MP_PLAYER_SOUND_CAUSES         5u
#define MP_PLAYER_SOUND_CAUSE_BURN     3u
#define MP_PLAYER_SOUND_CAUSE_SCRIPTED 4u

/* The flags the engine's own death voice passes: 3D, and a place that is copied once rather than
 * followed. Every sound played here gets them, because every one of them is played at a body that
 * is not the listener's. The key adds the engine's own "not while the same sound still plays",
 * which is what its call in the engine passes. */
#define MP_PLAYER_SOUND_PLACED_FLAGS  0x24u
#define MP_PLAYER_SOUND_FLAG_DONT_DUP 0x200u

/* The cue the engine's two entries hand on: the death voices are started with 0, a named sound
 * through the plain entry with -1. Neither carries a music flag, so the cue is never read; it is
 * passed as the engine passes it. */
#define MP_PLAYER_SOUND_CUE_DEATH 0
#define MP_PLAYER_SOUND_CUE_NAMED (-1)

/* How old a moment may be, in substeps past the render tick of the body it belongs to, and still
 * be played: the same ten substeps the world events wait. A cry that arrives a second after the
 * death it belongs to is a cry at nothing. */
#define MP_PLAYER_SOUND_LATE_SUBSTEPS 10u

/* The engine's substep rate, which the shield's length is counted in on the far side, and the
 * seconds a far body may wear a shield past the length its moment named before it is taken off
 * without waiting for the word that it ended. */
#define MP_PLAYER_SOUND_SUBSTEPS_PER_SECOND 32u
#define MP_PLAYER_SOUND_SHIELD_SLACK_SECONDS 2u

/* Whether a sound the engine just played for the body in the player record is this machine's own
 * player's, and so worth sending: only in a session, and only while the record is the local
 * player's, whose collision class is 1. A bank window has swapped in another body, and what the
 * engine does for that body is not this player's to tell. */
bool mp_player_sound_rule_may_note(bool session, int32_t bank_class);

/* The moments one death of `cause` sends, in the order the engine plays them, written into `whats`
 * and counted in the answer: the death cry for every cause the engine knows, the scripted one
 * included so that the far side can count the death it does not voice, and the burning cry behind
 * it for fire. A cause past the five sends nothing. */
size_t mp_player_sound_rule_death_moments(int32_t cause, uint8_t whats[2]);

/* The index of a sound name out of the absolute operand an engine instruction reads it through,
 * given where the table of names begins. False for an operand that is not a whole entry of that
 * table. */
bool mp_player_sound_rule_name_index(uint32_t operand, uint32_t table, uint8_t *index);

/* The whole seconds a shield timer holds, rounded up, at least one and at most 255; 0 for a timer
 * that is not running or not a number. */
uint8_t mp_player_sound_rule_seconds(float timer);

/* Whether a moment stamped `event_tick` is too old to play once the body it belongs to is drawn at
 * `render_tick`. Without a render tick nothing is too old: the moment is due on arrival. */
bool mp_player_sound_rule_too_late(uint32_t event_tick, uint32_t render_tick, bool render_known);

/* The table a moment's sound byte is read in on the far side. */
typedef enum mp_player_sound_table {
    MP_PLAYER_SOUND_TABLE_NONE = 0,   /* a shield: nothing is played */
    MP_PLAYER_SOUND_TABLE_NAMES,      /* the byte is an index into the sound names */
    MP_PLAYER_SOUND_TABLE_DEATH,      /* the byte is a hero row of the death voices */
    MP_PLAYER_SOUND_TABLE_BURN        /* the byte is a hero row of the burning voices */
} mp_player_sound_table_t;

/* What the far side does with one moment. */
typedef struct mp_player_sound_plan {
    bool                    valid;        /* false: a byte names nothing, and nothing is done */
    mp_player_sound_table_t table;
    uint8_t                 row;          /* the sound byte, bounded by its table */
    bool                    silent;       /* a scripted death: counted, not voiced */
    uint32_t                engine_flags; /* what the engine's entry is handed */
    int32_t                 cue;
    bool                    burst;        /* the body bursts into its pieces before the cry */
    bool                    shield_on;
    bool                    shield_off;
    uint8_t                 seconds;      /* shield_on: how long it lasts */
} mp_player_sound_plan_t;

/* The plan for a moment of kind `what` with its sound byte and its flags byte, one of the
 * MP_PLAYER_SOUND_ kinds of mp_events.h. For the two cries the flags byte is the death's cause;
 * for the shield's rise the sound byte is its length in seconds. */
void mp_player_sound_rule_plan(uint8_t what, uint8_t sound, uint8_t flags,
                               mp_player_sound_plan_t *out);

/* Whether a shield a far body has worn for `substeps` has outlasted the `seconds` its moment named
 * by more than the slack. */
bool mp_player_sound_rule_shield_expired(uint32_t substeps, uint8_t seconds);

/* Whether a far player has come back: seen lying at some point since the moment that needs it, and
 * standing now. That is the far machine's re-entry, which builds its player a new body; whatever
 * the old one wore or lost goes with it. */
bool mp_player_sound_rule_came_back(bool seen_down, bool stands);

#endif /* MULTIPLAYER_MP_PLAYER_SOUND_RULE_H */
