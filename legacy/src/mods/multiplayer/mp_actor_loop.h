/* mp_actor_loop.h: the sound an actor's script keeps looping, as the host's state and on a
 * client's replica.
 *
 * Layer 3. The decisions are mp_script_sound_rule.h; the calls into the engine are handed in by
 * mp_script_sound.c, which owns the site the loops start at.
 *
 * On the host a loop starts where the script plays a call whose record carries the loop bit: the
 * engine hands the actor's own handle cell to the channel, and the actor keeps at most one, since
 * the script's opcode starts nothing while that cell names a channel. What is kept here is the
 * INTENT, one call per actor's life: the host's own channel may be refused for distance or cut by
 * it, and a peer standing next to the actor still has to hear it. It ends when the script's command
 * 17 frees the channel, when the census no longer reads the actor in that life (a removal, a new
 * life) and when the level goes. The engine itself frees nothing when an actor is removed, and the
 * host's channel goes on as it does in single player; only the note forgets it. A new life whose
 * script starts a loop in the slot of an old one starts its own entry, whether or not the census
 * has read it yet.
 *
 * The level's state carries the list to every client, on every change and once a second, which is
 * what brings a late player and a savegame up to date. A client starts each wanted loop on the
 * replica of that life, with the replica's own handle cell and position, and starts it again when
 * the engine's distance cut ended it and the listener is back, as the host's script would by
 * calling again. One no longer wanted is stopped with command 17 on the replica, the engine's own
 * way. A replica removed while its loop plays keeps that loop sounding where it stood, as the
 * host's does, and no longer as the owner of the channel: its slot may be the next actor's. A
 * replica whose own script started a loop before the host described it has one in its cell
 * already: the host's sound is taken over as it plays, another is stopped before the host's starts,
 * so a cell never owns two channels.
 *
 * Both sides forget everything when the enemy table resets, which every way out of a level and a
 * session takes.
 */
#ifndef MULTIPLAYER_MP_ACTOR_LOOP_H
#define MULTIPLAYER_MP_ACTOR_LOOP_H

#include "mp_level_state_rule.h"

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

/* The two fields of the engine's character the sound site reads: the cell the actor's loop
 * channel is kept in, -1 for none, and the position the channel follows. */
#define MP_ACTOR_LOOP_HANDLE 0x90u
#define MP_ACTOR_SOUND_POS   0xD0u

/* What the channel a replica's cell names is to a client about to start a loop there. */
typedef enum mp_actor_loop_found {
    MP_ACTOR_LOOP_FOUND_NONE = 0,   /* no channel, or one that is not the cell's any more */
    MP_ACTOR_LOOP_FOUND_SAME,       /* the cell's own, playing the sound of the wanted call */
    MP_ACTOR_LOOP_FOUND_OTHER       /* the cell's own, playing another sound */
} mp_actor_loop_found_t;

/* What this module asks of the engine, handed in by the module that resolved it, so that a test
 * can stand in for all of it. */
typedef struct mp_actor_loop_engine {
    /* The engine's own "play sound call N", with the handle cell and the position it follows. */
    void (*play)(uint16_t call, int32_t *handle, const float *position);
    /* Command 17 on `actor`, past this feature's own hull: the engine frees the channel the
     * actor's handle names and writes -1 into the handle. */
    bool (*stop)(uintptr_t actor);
    /* A loop whose replica was removed: when `channel` still belongs to `handle`, it goes on at
     * `position` and forgets its owner. True when it did. */
    bool (*leave)(int32_t channel, const int32_t *handle, const float position[3]);
    /* Whether `actor` is still the replica of `key` in `life`: the binding's own question. */
    bool (*stands)(uint32_t key, uintptr_t actor, uint8_t life);
    /* What `channel`, the number `handle` holds, is: still owned by `handle`, and whether it plays
     * the sound of `call`. NONE whenever the channel bank cannot be read. */
    mp_actor_loop_found_t (*found)(int32_t channel, const int32_t *handle, uint16_t call);
} mp_actor_loop_engine_t;

/* NULL takes it back, and then a client starts and stops nothing. */
void mp_actor_loop_set_engine(const mp_actor_loop_engine_t *engine);

/* ---------------------------------------------------------------------------------------------
 * The host.
 * ------------------------------------------------------------------------------------------- */

/* The script of `actor`, the enemy under `key`, started the loop `call`. A second call for the
 * same actor replaces the first: the engine starts one only while the actor holds none. */
void mp_actor_loop_started(uintptr_t actor, uint32_t key, uint16_t call);

/* The script of `actor` gave command 17. */
void mp_actor_loop_stopped(uintptr_t actor);

/* Into the host's note: every loop whose actor the census read in its life. The part is always
 * there, so a note with none says that no loop plays. */
void mp_actor_loop_write(mp_level_state_note_t *note);

/* ---------------------------------------------------------------------------------------------
 * A client.
 * ------------------------------------------------------------------------------------------- */

/* A note the level's state took in this level and generation. */
void mp_actor_loop_take(const mp_level_state_note_t *note);

/* From every substep of a client, behind the replicas the substep built. */
void mp_actor_loop_flush(void);

void mp_actor_loop_report(void);

#endif /* MULTIPLAYER_MP_ACTOR_LOOP_H */
