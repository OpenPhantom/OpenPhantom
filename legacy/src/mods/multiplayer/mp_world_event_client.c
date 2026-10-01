/* mp_world_event_client.c: the moments of the world on a client: read with the block, taken
 * once, and performed on their replica by the module that owns the kind. See mp_world_event.h.
 */
#include "mp_world_event.h"

#include "mp_enemy_sync.h"
#include "mp_script_sound_rule.h"
#include "mp_wire.h"
#include "mp_world_event_internal.h"
#include "mp_world_event_rule.h"

#include "common/logging.h"

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <string.h>

typedef struct queued {
    mp_world_event_t event;
    uint8_t          waited;        /* substeps it has waited here for its replica */
} queued_t;

typedef struct staged_part {
    mp_world_event_head_t head;
    mp_world_event_t      event[MP_WORLD_EVENT_PART_MAX];
    uint32_t              count;
    uint32_t              skipped;
} staged_part_t;

typedef struct client_state {
    staged_part_t           staged;
    queued_t                queue[MP_WORLD_EVENT_RING];
    uint32_t                queued;
    mp_world_event_memory_t memory;
    uint16_t                music_state;      /* as the host last said */
    uint16_t                music_sequence;
    uint32_t                arrived;
    uint32_t                again;
    uint32_t                past;
    uint32_t                skipped;
    uint32_t                fired;
    uint32_t                fired_kind[MP_WORLD_EVENT_KINDS];
    uint32_t                at_place;
    uint32_t                waited;
    uint32_t                waited_fired;
    uint32_t                unreplicated;     /* no replica came inside the window */
    uint32_t                no_player;
    uint32_t                refused;          /* the player could not perform it */
    uint32_t                torn;
    uint32_t                queue_full;

    mp_world_event_player_fn_t player[MP_WORLD_EVENT_KINDS];
    uint32_t                   players_refused;
} client_state_t;

static client_state_t events;

bool mp_world_event_stage(const uint8_t *block, size_t bytes, size_t *at)
{
    staged_part_t *s = &events.staged;
    uint32_t       i;

    s->count   = 0u;
    s->skipped = 0u;
    memset(&s->head, 0, sizeof s->head);
    if (block == NULL || at == NULL || *at > bytes ||
        bytes - *at < MP_WORLD_EVENT_HEAD_BYTES) {
        ++events.torn;
        return false;
    }
    mp_world_event_get_head(block + *at, &s->head);
    *at += MP_WORLD_EVENT_HEAD_BYTES;
    if (s->head.count > MP_WORLD_EVENT_PART_MAX) {
        ++events.torn;
        return false;
    }
    for (i = 0; i < s->head.count; ++i) {
        mp_world_event_t event;
        size_t           read = 0;

        switch (mp_world_event_get(block + *at, bytes - *at, &event, &read)) {
        case MP_WORLD_EVENT_READ_OK:
            s->event[s->count++] = event;
            break;
        case MP_WORLD_EVENT_READ_SKIPPED:
            ++s->skipped;
            break;
        case MP_WORLD_EVENT_READ_TORN:
        default:
            ++events.torn;
            return false;
        }
        *at += read;
    }
    return true;
}

static void enqueue(const mp_world_event_t *event)
{
    if (events.queued >= MP_WORLD_EVENT_RING) {
        /* The oldest goes: it is the one nearest the end of its window anyway. */
        memmove(&events.queue[0], &events.queue[1],
                (MP_WORLD_EVENT_RING - 1u) * sizeof events.queue[0]);
        --events.queued;
        ++events.queue_full;
    }
    events.queue[events.queued].event  = *event;
    events.queue[events.queued].waited = 0u;
    ++events.queued;
}

void mp_world_event_take_staged(uint32_t tick)
{
    const staged_part_t *s = &events.staged;
    uint32_t             i;

    events.music_state    = s->head.music_state;
    events.music_sequence = s->head.music_sequence;
    events.skipped += s->skipped;
    if (s->head.count != 0u) {
        mp_world_event_memory_block(&events.memory, tick);
    }
    for (i = 0; i < s->count; ++i) {
        const mp_world_event_t *event = &s->event[i];

        ++events.arrived;
        switch (mp_world_event_memory_note(&events.memory, event->sequence)) {
        case MP_WORLD_EVENT_SEEN_AGAIN:
            ++events.again;
            break;
        case MP_WORLD_EVENT_PAST_MEMORY:
            ++events.past;
            break;
        case MP_WORLD_EVENT_FIRST_TIME:
        default:
            if (event->age >= MP_WORLD_EVENT_WINDOW) {
                ++events.past;
            } else {
                enqueue(event);
            }
            break;
        }
    }
    events.staged.count = 0u;
}

/* The replica of the key's life the event names, when that life is the one this side holds. */
static uintptr_t replica_of(const mp_world_event_t *event)
{
    uint8_t life = 0;

    if (!event->at_actor || !mp_enemy_sync_generation(event->key, &life) || life != event->life) {
        return 0u;
    }
    return mp_enemy_sync_replica_for(event->key);
}

/* One queued event: true when it leaves the queue. */
static bool perform(queued_t *q)
{
    const mp_world_event_t    *event  = &q->event;
    mp_world_event_player_fn_t player = events.player[event->kind];
    uintptr_t                  replica;

    if (player == NULL) {
        ++events.no_player;
        return true;
    }
    replica = replica_of(event);
    switch (mp_world_event_where(event, replica != 0u, (uint32_t)event->age + q->waited)) {
    case MP_WORLD_EVENT_AT_REPLICA:
    case MP_WORLD_EVENT_AT_PLACE:
        if (!player(event, replica)) {
            ++events.refused;
            return true;
        }
        ++events.fired;
        ++events.fired_kind[event->kind];
        events.at_place += replica == 0u ? 1u : 0u;
        events.waited_fired += q->waited != 0u ? 1u : 0u;
        return true;
    case MP_WORLD_EVENT_WAIT:
        events.waited += q->waited == 0u ? 1u : 0u;
        ++q->waited;
        return false;
    case MP_WORLD_EVENT_DROP:
    default:
        ++events.unreplicated;
        return true;
    }
}

uint32_t mp_world_event_flush(void)
{
    uint32_t before = events.fired;
    uint32_t kept   = 0;
    uint32_t i;

    for (i = 0; i < events.queued; ++i) {
        if (!perform(&events.queue[i])) {
            events.queue[kept++] = events.queue[i];
        }
    }
    events.queued = kept;
    return events.fired - before;
}

bool mp_world_event_set_player(uint8_t kind, mp_world_event_player_fn_t player)
{
    if (!mp_world_event_kind_known(kind) || player == NULL) {
        return false;
    }
    if (events.player[kind] != NULL && events.player[kind] != player) {
        ++events.players_refused;
        log_warning("a second player for the world event kind %s was refused: one module owns "
                    "each kind", mp_world_event_kind_name(kind));
        return false;
    }
    events.player[kind] = player;
    return true;
}

/* Asked from the mirror's side: the key whose replica here is this actor, and whether the host has
 * described a life of it. The mirror names placements and copies alike, so one question answers
 * for both, and a copy this side raised on its own under the key is not the replica the overlay
 * built for the host's grant. The search runs only on a client, and a client runs a script only
 * for an actor it does not park. */
static bool described_replica(uintptr_t actor)
{
    uint8_t  life = 0;
    uint32_t key;

    for (key = 0; actor != 0u && key < MP_WIRE_KEY_COUNT; ++key) {
        if (mp_enemy_sync_replica_for(key) == actor) {
            return mp_enemy_sync_generation(key, &life);
        }
    }
    return false;
}

bool mp_world_event_output_is_the_hosts(uintptr_t actor, bool client_of_started)
{
    return mp_script_sound_output_is_the_hosts(client_of_started,
                                               client_of_started && described_replica(actor));
}

void mp_world_event_music_heard(uint16_t *state, uint16_t *sequence)
{
    if (state != NULL) {
        *state = events.music_state;
    }
    if (sequence != NULL) {
        *sequence = events.music_sequence;
    }
}

void mp_world_event_client_reset(void)
{
    events.staged.count = 0u;
    events.queued       = 0u;
    mp_world_event_memory_reset(&events.memory);
    events.music_state    = MP_WORLD_EVENT_MUSIC_NONE;
    events.music_sequence = MP_WORLD_EVENT_MUSIC_NONE;
}

void mp_world_event_client_report(void)
{
    log_info("the world events (client): %u arrived, %u fired (emitter %u, clang %u, limb %u, "
             "sound %u, blast %u, zap %u; %u at a fixed point), %u seen again, %u past the window, "
             "%u unknown kind skipped, %u waited for a replica (%u of them fired), %u without a "
             "replica, %u with no player here, %u the player could not perform, %u torn part(s), "
             "%u dropped from a full queue; music %u and %u as the host last said",
             (unsigned)events.arrived, (unsigned)events.fired,
             (unsigned)events.fired_kind[MP_WORLD_EVENT_SCRIPT_EMITTER],
             (unsigned)events.fired_kind[MP_WORLD_EVENT_NPC_CLANG],
             (unsigned)events.fired_kind[MP_WORLD_EVENT_LIMB_FLY],
             (unsigned)events.fired_kind[MP_WORLD_EVENT_SCRIPT_SOUND],
             (unsigned)events.fired_kind[MP_WORLD_EVENT_EXPLODE_AT],
             (unsigned)events.fired_kind[MP_WORLD_EVENT_ZAP_ARCS], (unsigned)events.at_place,
             (unsigned)events.again, (unsigned)events.past, (unsigned)events.skipped,
             (unsigned)events.waited, (unsigned)events.waited_fired,
             (unsigned)events.unreplicated, (unsigned)events.no_player, (unsigned)events.refused,
             (unsigned)events.torn, (unsigned)events.queue_full, (unsigned)events.music_state,
             (unsigned)events.music_sequence);
    if (events.players_refused != 0u) {
        log_warning("the world events: %u second player(s) for a kind were refused",
                    (unsigned)events.players_refused);
    }
}
