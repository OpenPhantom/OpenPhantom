/* mp_script_sound_client.c: the scripts' sounds on a client: the sounds the host said, played on
 * the replicas; the music it holds, asked of the engine in every substep; the loops it wants,
 * through mp_actor_loop; and command 17 of this side's own scripts. See mp_script_sound.h.
 */
#include "mp_script_sound.h"

#include "mp_actor_loop.h"
#include "mp_director_rule.h"
#include "mp_enemy_bind.h"
#include "mp_enemy_sync.h"
#include "mp_enemy_wire.h"
#include "mp_level_state_bind.h"
#include "mp_script_sound_internal.h"
#include "mp_script_sound_rule.h"
#include "mp_session_now.h"
#include "mp_signatures_script_sound.h"
#include "mp_sound.h"
#include "mp_world_door.h"
#include "mp_world_event.h"
#include "mp_world_event_rule.h"

#include "common/logging.h"
#include "common/memory.h"

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <string.h>

/* The director's command that frees the channel an actor's handle names, the one way the engine
 * stops an actor's loop. Its class says so; the install checks the two agree. */
#define DIRECTOR_COMMAND_SOUND_STOP 17

/* A sound call record begins with the name of its sound, and a sound reference carries the same
 * name four bytes in, which is the key the engine finds a reference by. */
#define SOUND_CALL_NAME_BYTES 0x18u
#define SOUND_REF_NAME        0x04u
#define SOUND_REF_NAME_BYTES  0x34u

typedef struct client_sound_state {
    /* The engine's habit for a sound played once: one cell every such channel writes its index
     * into and its end into, which nobody reads. Never a cell on the stack, which the channel
     * would write into long after the frame is gone. */
    int32_t  once_cell;
    float    place[3];        /* a fixed point, copied by the engine inside the call */
    uint32_t record[16];      /* the record played at a fixed point, with a fixed place */
    float    music_place[3];  /* the music branch reads a position and uses none of it */
    uint16_t music_last[2];

    uint32_t at_replica;
    uint32_t at_place;
    uint32_t started;
    uint32_t refused;
    uint32_t not_once;        /* the host's call is not a sound played once in this level */
    uint32_t no_place;
    uint32_t unreadable;
    uint32_t music_substeps[2];
    uint32_t music_changes[2];
    uint32_t music_not_here;  /* the host's music call is not music in this level */
    uint32_t stop_withheld;
    uint32_t stop_let_through;
    uint32_t stop_by_host;
    uint32_t stops_failed;    /* no director to stop a loop with */
} client_sound_state_t;

static client_sound_state_t cs;

/* ==============================================================================================
 * A sound played once, as the host said it.
 * ============================================================================================ */

/* At the replica with the engine's own call, the channel following the replica as it follows the
 * actor on the host; with no replica, at the place the host named, the record copied with a fixed
 * place so the engine keeps the point and never reads this module's cell again. */
static bool play_once(const mp_world_event_t *event, uintptr_t replica)
{
    const mp_script_sound_engine_t *engine = mp_script_sound_engine();
    size_t                          i;

    if (engine->play_call == NULL || !mp_script_sound_record(event->a, cs.record)) {
        ++cs.unreadable;
        return false;
    }
    if (mp_script_sound_classify(mp_script_sound_record_flags(cs.record)) !=
        MP_SCRIPT_SOUND_ONCE) {
        ++cs.not_once;
        return false;
    }
    cs.once_cell = -1;
    if (replica != 0u) {
        if (!memory_try_readable(replica + MP_ACTOR_SOUND_POS, sizeof cs.place)) {
            ++cs.unreadable;
            return false;
        }
        engine->play_call(event->a, &cs.once_cell,
                          (const float *)(replica + MP_ACTOR_SOUND_POS));
        ++cs.at_replica;
    } else {
        if (!event->has_place || engine->play == NULL) {
            ++cs.no_place;
            return false;
        }
        for (i = 0; i < 3u; ++i) {
            cs.place[i] = mp_enemy_wire_get_position(event->place[i]);
        }
        cs.record[MP_SOUND_RECORD_FLAGS / sizeof(uint32_t)] |= MP_SOUND_FLAG_STATIC_POS;
        (void)engine->play(cs.record, &cs.once_cell, cs.place);
        ++cs.at_place;
    }
    if (cs.once_cell >= 0) {
        ++cs.started;
    } else {
        ++cs.refused;
    }
    return true;
}

/* ==============================================================================================
 * Music, in every substep the host holds it.
 * ============================================================================================ */

static void play_the_music(void)
{
    static const mp_script_sound_class_t CLASS[2] = { MP_SCRIPT_SOUND_MUSIC_STATE,
                                                      MP_SCRIPT_SOUND_MUSIC_SEQUENCE };
    const mp_script_sound_engine_t *engine = mp_script_sound_engine();
    uint16_t                        call[2] = { 0u, 0u };
    uint32_t                        record[16];
    size_t                          channel;

    mp_world_event_music_heard(&call[0], &call[1]);
    for (channel = 0; channel < 2u; ++channel) {
        if (call[channel] == 0u) {
            cs.music_last[channel] = 0u;
            continue;
        }
        if (call[channel] != cs.music_last[channel]) {
            ++cs.music_changes[channel];
            cs.music_last[channel] = call[channel];
        }
        if (engine->play_call == NULL || !mp_script_sound_record(call[channel], record) ||
            mp_script_sound_classify(mp_script_sound_record_flags(record)) != CLASS[channel]) {
            ++cs.music_not_here;
            continue;
        }
        /* The latch makes a repeated call nothing; a sequence behind a gate is asked again until
         * the gate opens, against this machine's own player. No handle: music writes none. */
        engine->play_call(call[channel], NULL, cs.music_place);
        ++cs.music_substeps[channel];
    }
}

/* ==============================================================================================
 * The loops' engine, and command 17.
 * ============================================================================================ */

static void engine_play(uint16_t call, int32_t *handle, const float *position)
{
    const mp_script_sound_engine_t *engine = mp_script_sound_engine();

    if (engine->play_call != NULL) {
        engine->play_call(call, handle, position);
    }
}

/* The engine's own stop, command 17 on the replica past this feature's hull. */
static bool engine_stop(uintptr_t actor)
{
    if (!mp_level_state_bind_call_director(actor, DIRECTOR_COMMAND_SOUND_STOP, 0, 0)) {
        ++cs.stops_failed;
        return false;
    }
    return true;
}

/* A loop whose replica went: when the channel still names the replica's cell as its owner, the
 * engine's pin keeps it at the last place the replica stood, and the owner is let go, so the end
 * of the loop writes into nobody's cell. */
static bool engine_leave(int32_t channel, const int32_t *handle, const float position[3])
{
    const mp_script_sound_engine_t *engine = mp_script_sound_engine();
    const uint32_t                  none   = 0u;
    uint32_t                        ref    = 0u;
    uint32_t                        owner  = 0u;
    uintptr_t                       slot;

    if (engine->pin == NULL || engine->bank == 0u || channel < 0 ||
        channel >= MP_SCRIPT_SOUND_CHANNELS) {
        return false;
    }
    slot = engine->bank + (uint32_t)channel * MP_SCRIPT_SOUND_CHANNEL_STRIDE;
    if (!memory_try_read(slot + MP_SCRIPT_SOUND_CHANNEL_REF, &ref, sizeof ref) || ref == 0u ||
        !memory_try_read(slot + MP_SCRIPT_SOUND_CHANNEL_OWNER, &owner, sizeof owner) ||
        owner != (uint32_t)(uintptr_t)handle) {
        return false;
    }
    engine->pin((uint32_t)channel, position);
    return memory_try_write(slot + MP_SCRIPT_SOUND_CHANNEL_OWNER, &none, sizeof none);
}

static bool engine_stands(uint32_t key, uintptr_t actor, uint8_t life)
{
    return mp_enemy_slot_is_actor(mp_enemy_sync_slot(key, actor, life));
}

/* What a channel a replica's cell names is. The engine writes the channel's number into the cell
 * and the cell into the channel as its owner, and starts it with the sound reference its record's
 * name finds, by an exact comparison of names. So the channel is the cell's while its owner is the
 * cell, and it plays the sound of a call when its reference carries that call's name. */
static mp_actor_loop_found_t engine_found(int32_t channel, const int32_t *handle, uint16_t call)
{
    const mp_script_sound_engine_t *engine = mp_script_sound_engine();
    uint32_t                        record[16];
    uint32_t                        ref   = 0u;
    uint32_t                        owner = 0u;
    char                            wanted[SOUND_CALL_NAME_BYTES + 1u];
    char                            playing[SOUND_REF_NAME_BYTES];
    uintptr_t                       slot;

    if (engine->bank == 0u || channel < 0 || channel >= MP_SCRIPT_SOUND_CHANNELS) {
        return MP_ACTOR_LOOP_FOUND_NONE;
    }
    slot = engine->bank + (uint32_t)channel * MP_SCRIPT_SOUND_CHANNEL_STRIDE;
    if (!memory_try_read(slot + MP_SCRIPT_SOUND_CHANNEL_REF, &ref, sizeof ref) || ref == 0u ||
        !memory_try_read(slot + MP_SCRIPT_SOUND_CHANNEL_OWNER, &owner, sizeof owner) ||
        owner != (uint32_t)(uintptr_t)handle) {
        return MP_ACTOR_LOOP_FOUND_NONE;
    }
    if (!mp_script_sound_record(call, record) ||
        !memory_try_read((uintptr_t)ref + SOUND_REF_NAME, playing, sizeof playing)) {
        return MP_ACTOR_LOOP_FOUND_OTHER;   /* the cell's, and not known to be the wanted sound */
    }
    memcpy(wanted, record, SOUND_CALL_NAME_BYTES);
    wanted[SOUND_CALL_NAME_BYTES] = '\0';
    playing[sizeof playing - 1u]  = '\0';
    return strcmp(wanted, playing) == 0 ? MP_ACTOR_LOOP_FOUND_SAME : MP_ACTOR_LOOP_FOUND_OTHER;
}

static const mp_actor_loop_engine_t LOOP_ENGINE = { &engine_play, &engine_stop, &engine_leave,
                                                    &engine_stands, &engine_found };

/* Command 17 of a script. On the host it ends the loop on record and the engine frees the host's
 * channel as it always did. On a client it is withheld for an actor whose life the host describes,
 * whose loop is the host's to end. */
static bool hand_sound_stop(void *actor, int32_t command, int32_t a1, int32_t a2)
{
    bool runs = false;
    bool client;

    (void)command;
    (void)a1;
    (void)a2;
    client = mp_session_now_client_of_a_started_session(&runs, NULL);
    if (runs && !client) {
        mp_actor_loop_stopped((uintptr_t)actor);
        ++cs.stop_by_host;
        return false;
    }
    if (mp_world_event_output_is_the_hosts((uintptr_t)actor, client)) {
        ++cs.stop_withheld;
        return true;
    }
    ++cs.stop_let_through;
    return false;
}

/* ==============================================================================================
 * The install, the substep and the report.
 * ============================================================================================ */

void mp_script_sound_client_install(void)
{
    (void)mp_world_event_set_player(MP_WORLD_EVENT_SCRIPT_SOUND, &play_once);
    mp_actor_loop_set_engine(&LOOP_ENGINE);
    if (mp_director_class_of(DIRECTOR_COMMAND_SOUND_STOP) != MP_DIRECTOR_SOUND_STOP) {
        log_warning("the director's command %d is not the sound stop in its class table, so "
                    "command 17 of a script is left to the engine on every side",
                    (int)DIRECTOR_COMMAND_SOUND_STOP);
        return;
    }
    (void)mp_world_door_hand(MP_DIRECTOR_SOUND_STOP, &hand_sound_stop, "the actors' loops");
}

void mp_script_sound_flush(void)
{
    if (mp_script_sound_engine()->play_call == NULL) {
        return;
    }
    play_the_music();
    mp_actor_loop_flush();
}

void mp_script_sound_client_report(void)
{
    log_info("the scripts' sounds (client): from the host %u played at a replica and %u at a fixed "
             "point, %u started, %u refused by the engine's start gate (distance, a duplicate or "
             "no channel), %u not a sound played once here, %u without a place, %u unreadable; "
             "music as the host holds it: state %u substep(s) (%u change(s)), sequence %u "
             "substep(s) (%u change(s)), %u not music here; command 17 of a script: %u on the "
             "host, %u withheld here, %u let through here, %u stop(s) with no director",
             (unsigned)cs.at_replica, (unsigned)cs.at_place, (unsigned)cs.started,
             (unsigned)cs.refused, (unsigned)cs.not_once, (unsigned)cs.no_place,
             (unsigned)cs.unreadable, (unsigned)cs.music_substeps[0],
             (unsigned)cs.music_changes[0], (unsigned)cs.music_substeps[1],
             (unsigned)cs.music_changes[1], (unsigned)cs.music_not_here,
             (unsigned)cs.stop_by_host, (unsigned)cs.stop_withheld,
             (unsigned)cs.stop_let_through, (unsigned)cs.stops_failed);
}
