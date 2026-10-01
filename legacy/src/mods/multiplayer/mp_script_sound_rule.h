/* mp_script_sound_rule.h: what the host does with a sound an actor's script plays, and what a
 * client does with what the host said about it.
 *
 * Layer 1, pure. No engine, no address. The call site, the engine's records and the calls into the
 * engine are mp_script_sound.c and mp_actor_loop.c; every decision they make is one of these.
 *
 * An actor's script plays a sound with one opcode, and the one call it makes is the engine's
 * "play sound call N at the actor". A client's replica runs no script, so without the host none of
 * it is heard there: the fall of a dead droid, the burst of a destroyed one, the steam a wreck
 * keeps hissing, the music a fight turns on. What the call is decides how it travels, and the
 * record's flag word says what it is, in the order the engine itself asks it: a music state, then
 * a music sequence, then a loop, and everything else is a sound played once. Five records carry
 * the loop bit beside a music bit, and the engine plays them as music.
 *
 *   ONCE     a moment: a world event at the actor, heard by a peer whose player stands within the
 *            record's own reach and a margin. Scripts call many of these in every substep a state
 *            lasts, so a call in the substep after the last one is HELD and makes no new event;
 *            one after a pause is an EDGE and does. A held call goes out again once a second, so
 *            a player who walks into earshot hears it, and the receiver's own engine refuses a
 *            duplicate as it does in single player.
 *
 *   LOOP     state: one loop per actor's life, carried in the level's state until a command stops
 *            it, the actor is removed or its life ends. A client starts it on the replica and
 *            starts it again when the engine's own distance cut ended it and the listener came
 *            back, which is what the script does on the host by calling again.
 *
 *   MUSIC    state per player: the host keeps, for each peer and each of the two music channels,
 *            the call it last claimed for that peer, and says it in the head of every enemy block.
 *            The receiver calls it in every substep the host holds it, and the engine's own latch
 *            and gate decide there, against that machine's own player. The host plays its own
 *            music only when the music is meant for its player, by the same function.
 *
 * Whom music is meant for: the player the actor's script last asked for, while the answer is
 * fresh; otherwise the player that hurt it lately; otherwise everybody, when its script has never
 * asked for a player at all; otherwise the nearest player. And in every case every player the
 * actor would have been woken by, which is the interest rule's own near class and no new radius.
 */
#ifndef MULTIPLAYER_MP_SCRIPT_SOUND_RULE_H
#define MULTIPLAYER_MP_SCRIPT_SOUND_RULE_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

/* The flag word of a sound call record, as far as it is read here. The engine tests the two music
 * bits before anything else and returns out of the play; only then does the loop bit mean a
 * channel the actor keeps. */
#define MP_SCRIPT_SOUND_FLAG_MUSIC_STATE    0x00000008u
#define MP_SCRIPT_SOUND_FLAG_LOOP           0x00000010u
#define MP_SCRIPT_SOUND_FLAG_MUSIC_SEQUENCE 0x00000400u

typedef enum mp_script_sound_class {
    MP_SCRIPT_SOUND_ONCE = 0,
    MP_SCRIPT_SOUND_LOOP,
    MP_SCRIPT_SOUND_MUSIC_STATE,
    MP_SCRIPT_SOUND_MUSIC_SEQUENCE,
    MP_SCRIPT_SOUND_CLASSES
} mp_script_sound_class_t;

mp_script_sound_class_t mp_script_sound_classify(uint32_t flags);

/* ==============================================================================================
 * Who handles a call at the script's sound site.
 * ============================================================================================ */

typedef enum mp_script_sound_route {
    MP_SCRIPT_SOUND_TO_ENGINE = 0,   /* no session, or an actor whose life is this machine's own */
    MP_SCRIPT_SOUND_BY_HOST,         /* the host of a started session */
    MP_SCRIPT_SOUND_WITHHELD         /* a client, an actor whose life the host describes */
} mp_script_sound_route_t;

/* `hosting` is the host of a started session, whether or not a peer is there yet: a loop its
 * script starts before the first census is still the level's state a peer is told when it comes;
 * `client_of_started` a client of a started session; `hosts_life` that the actor's key and life
 * are the ones the host described to this side. */
mp_script_sound_route_t mp_script_sound_route(bool hosting, bool client_of_started,
                                              bool hosts_life);

/* Whether a script output of one actor is the host's to make, the rule behind the one predicate
 * every script output of a client asks (mp_world_event_output_is_the_hosts): its sounds, its lines,
 * its emitters and the director's commands 8, 9, 17 and 19. An actor whose life the host described
 * plays none of its own, including after it was let go, until it is removed; an actor the host
 * never described plays them as it always did. `hosts_life` is that fact for the actor. */
bool mp_script_sound_output_is_the_hosts(bool client_of_started, bool hosts_life);

/* Whether the host plays a call it handles itself: everything but music, and music only when it
 * is meant for the host's own player. */
bool mp_script_sound_host_plays(mp_script_sound_class_t cls, bool meant_for_this_player);

/* ==============================================================================================
 * A sound played once: the edge, and the hold behind it.
 * ============================================================================================ */

/* How many substeps a held call waits before it goes out again: a second. */
#define MP_SCRIPT_SOUND_HELD_REPEAT 32u

/* How many actor and call pairs are remembered at once. Only a pair called in the last substep is
 * worth keeping, because any other one is an edge again. */
#define MP_SCRIPT_SOUND_EDGES 64u

typedef enum mp_script_sound_edge {
    MP_SCRIPT_SOUND_EDGE = 0,   /* after a pause: a new event */
    MP_SCRIPT_SOUND_AGAIN,      /* held for a second, or held and never out yet: an event */
    MP_SCRIPT_SOUND_HELD        /* the same call in the next substep: no event */
} mp_script_sound_edge_t;

typedef struct mp_script_sound_edge_row {
    bool      used;
    bool      out;          /* an event of this row went out */
    uintptr_t actor;
    uint16_t  call;
    uint32_t  last_call;    /* the substep of the last call */
    uint32_t  last_out;     /* the substep the last event went out */
} mp_script_sound_edge_row_t;

typedef struct mp_script_sound_edges {
    mp_script_sound_edge_row_t row[MP_SCRIPT_SOUND_EDGES];
    uint32_t                   full;   /* calls no row was free for, taken as edges */
} mp_script_sound_edges_t;

/* One call of `call` by `actor` in substep `now`: what it is, and the row that keeps it, or -1
 * when the table had no row to spare. The caller says whether an event went out, and only then is
 * the hold measured from it, so an event the world events refused is tried again. */
mp_script_sound_edge_t mp_script_sound_edge_note(mp_script_sound_edges_t *edges, uintptr_t actor,
                                                 uint16_t call, uint32_t now, int32_t *row);
void mp_script_sound_edge_out(mp_script_sound_edges_t *edges, int32_t row, uint32_t now);

/* ==============================================================================================
 * Music: whom it is meant for, and how long a claim holds.
 * ============================================================================================ */

/* The players, by bank: 0 is the host's own, 1 to 3 the far ones. */
#define MP_SCRIPT_MUSIC_BANKS   4u
#define MP_SCRIPT_MUSIC_NO_BANK 0xFFu

/* How many substeps a claim is said after the last call that made it, the world events' window:
 * a script that calls its music once still reaches a peer whose next few blocks are lost, and a
 * script that stops calling stops being said a third of a second later. */
#define MP_SCRIPT_MUSIC_HOLD 10u

typedef struct mp_script_music_ask {
    bool     answered;         /* the actor's script asked for a player and has an answer */
    bool     answered_player;  /* that answer was a player, not an ally or nobody */
    uint8_t  answered_bank;
    uint32_t answered_age;     /* substeps */
    uint32_t fresh;            /* how old an answer may be and still count */
    bool     attacked;         /* a player hurt the actor lately */
    uint8_t  attacker_bank;
    uint8_t  listeners;        /* bank bits of the players music can be claimed for */
    uint8_t  nearest;          /* the nearest player's bank, or MP_SCRIPT_MUSIC_NO_BANK */
    uint8_t  awake_for;        /* bank bits: the actor would be woken by that player */
} mp_script_music_ask_t;

typedef enum mp_script_music_why {
    MP_SCRIPT_MUSIC_FOR_THE_ANSWER = 0,
    MP_SCRIPT_MUSIC_FOR_THE_ATTACKER,
    MP_SCRIPT_MUSIC_FOR_EVERYBODY,
    MP_SCRIPT_MUSIC_FOR_THE_NEAREST,
    MP_SCRIPT_MUSIC_REASONS
} mp_script_music_why_t;

/* The bank bits the music is meant for, never outside `listeners`. */
uint8_t mp_script_music_meant(const mp_script_music_ask_t *ask, mp_script_music_why_t *why);

/* The nearest of `count` players to `at`, by the engine's own distance, or MP_SCRIPT_MUSIC_NO_BANK
 * when none is placed. The first of two at the same distance wins. */
uint8_t mp_script_music_nearest(const float at[3], const float positions[][3],
                                const bool placed[], size_t count);

/* What a claim of `call` made in substep `made` says in substep `now`: the call while it holds,
 * and 0 for none. */
uint16_t mp_script_music_held(bool claimed, uint16_t call, uint32_t made, uint32_t now);

/* ==============================================================================================
 * A loop on a client's replica.
 * ============================================================================================ */

typedef enum mp_actor_loop_step {
    MP_ACTOR_LOOP_NOTHING = 0,
    MP_ACTOR_LOOP_KEEP,      /* the wanted call plays */
    MP_ACTOR_LOOP_START,     /* wanted and not playing: start it, or start it again */
    MP_ACTOR_LOOP_REPLACE,   /* another call plays: stop it first, then start */
    MP_ACTOR_LOOP_STOP,      /* playing and no longer wanted */
    MP_ACTOR_LOOP_FORGET     /* no longer wanted, and nothing plays any more */
} mp_actor_loop_step_t;

/* `wanted` and `want` are the host's; `started` and `playing_call` what this side started on the
 * replica; `alive` whether the replica's handle still names a channel. At most one loop per
 * replica, whatever the host asks in between. */
mp_actor_loop_step_t mp_actor_loop_step(bool wanted, uint16_t want, bool started,
                                        uint16_t playing_call, bool alive);

#endif /* MULTIPLAYER_MP_SCRIPT_SOUND_RULE_H */
