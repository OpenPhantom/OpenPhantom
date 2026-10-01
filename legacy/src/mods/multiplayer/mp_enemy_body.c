/* mp_enemy_body.c: how an enemy's body ends, as the host has it. See the header. */
#include "mp_enemy_body.h"

#include "mp_enemy_body_rule.h"
#include "mp_enemy_pose.h"
#include "mp_enemy_wire.h"
#include "mp_wire.h"

#include "common/logging.h"
#include "common/memory.h"

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <string.h>

/* The body: its flag word, whose bit 0 the renderer draws by and whose bit 1 gives a ground
 * shadow; its class, which is what makes it collide and a target; the shooter class the spawn gave
 * it; and its drawn thing, whose alpha and dissolve the fade and dissolve arms ramp. */
#define B_FLAGS          0x00u
#define B_CLASS          0x04u
#define B_SHOOTER_CLASS  0x08u
#define B_THING          0x9Cu
#define THING_ALPHA      0x150u
#define THING_DISSOLVE   0x158u

/* What the host counted of one key's base clip. */
typedef struct host_starts {
    bool     known;
    uint32_t body;
    int32_t  slot;
    uint32_t keyframe;
    float    head;
    uint8_t  count;
} host_starts_t;

/* What a client last acted on for one key: the host's field, for the life and the body it acted
 * on. */
typedef struct replica_acted {
    bool     known;
    uint8_t  generation;
    uint32_t body;
    uint32_t field;
} replica_acted_t;

typedef struct body_state {
    host_starts_t   starts[MP_WIRE_KEY_COUNT];
    replica_acted_t acted[MP_WIRE_KEY_COUNT];

    /* The host. */
    uint32_t reads;
    uint32_t unread;           /* no flag word or class to read */
    uint32_t no_thing;         /* no drawn thing: alpha and dissolve read as the engine's start */
    uint32_t clip_starts;
    uint32_t clip_restarts;    /* of them, the same keyframe again */

    /* A client. */
    uint32_t records;
    uint32_t no_body;          /* the host read no body */
    uint32_t first;
    uint32_t written[MP_ENEMY_BODY_PARTS];
    uint32_t already;
    uint32_t left;
    uint32_t laid;             /* the class taken away as the host's was */
    uint32_t stood;            /* the class put back as the host's was */
    uint32_t refused;          /* a write that did not take, or a replica that did not read */
    uint32_t apart;            /* parts still apart from the host's after the write */
} body_state_t;

static body_state_t bodies;

/* The five values as the body holds them. False when the flag word or the class does not read;
 * a thing that does not read leaves the alpha and the dissolve at the engine's own start. */
static bool read_body(uint32_t body, mp_enemy_body_state_t *out, int32_t *shooter_class,
                      bool *thing_read)
{
    uint32_t flags = 0;
    int32_t  cls = 0;
    uint32_t thing = 0;
    float    alpha = 1.0f;
    float    dissolve = 0.0f;

    memset(out, 0, sizeof *out);
    if (body == 0u || !memory_try_read_u32((uintptr_t)body + B_FLAGS, &flags) ||
        !memory_try_read((uintptr_t)body + B_CLASS, &cls, sizeof cls)) {
        return false;
    }
    if (shooter_class != NULL &&
        !memory_try_read((uintptr_t)body + B_SHOOTER_CLASS, shooter_class,
                         sizeof *shooter_class)) {
        return false;
    }
    *thing_read = memory_try_read_u32((uintptr_t)body + B_THING, &thing) && thing != 0u &&
                  memory_try_read((uintptr_t)thing + THING_ALPHA, &alpha, sizeof alpha) &&
                  memory_try_read((uintptr_t)thing + THING_DISSOLVE, &dissolve, sizeof dissolve);
    out->has      = true;
    out->drawn    = (flags & 0x01u) != 0u;
    out->shadow   = (flags & 0x02u) != 0u;
    out->solid    = cls != 0;
    out->alpha    = *thing_read ? mp_enemy_body_quantise(alpha, MP_ENEMY_BODY_OPAQUE)
                                : MP_ENEMY_BODY_OPAQUE;
    out->dissolve = *thing_read ? mp_enemy_body_quantise(dissolve, MP_ENEMY_BODY_WHOLE)
                                : MP_ENEMY_BODY_WHOLE;
    return true;
}

/* ==============================================================================================
 * The host.
 * ============================================================================================ */

/* The starts of the key's base clip, from the track the census sees each substep. A body the key
 * had not been seen with starts from the count it has, so that a new body is not a restart; its
 * first record is the client's first write anyway. */
static uint8_t count_starts(uint32_t key, uint32_t body)
{
    host_starts_t        *m = &bodies.starts[key];
    mp_enemy_pose_track_t track;

    if (!mp_enemy_pose_base_track(body, &track)) {
        return m->count;
    }
    if (m->known && m->body == body &&
        mp_enemy_body_started(m->slot, m->keyframe, m->head, track.slot, track.keyframe,
                              track.head, track.loops)) {
        m->count = (uint8_t)((m->count + 1u) & MP_ENEMY_BODY_STARTS_MASK);
        ++bodies.clip_starts;
        bodies.clip_restarts += (track.keyframe == m->keyframe) ? 1u : 0u;
    }
    m->known    = true;
    m->body     = body;
    m->slot     = track.slot;
    m->keyframe = track.keyframe;
    m->head     = track.head;
    return m->count;
}

bool mp_enemy_body_peek(uint32_t body, mp_enemy_body_state_t *out)
{
    bool thing_read = false;

    return out != NULL && read_body(body, out, NULL, &thing_read);
}

void mp_enemy_body_read(uintptr_t actor, uint32_t body, uint32_t key, mp_enemy_record_t *record)
{
    mp_enemy_body_state_t state;
    bool                  thing_read = false;

    (void)actor;
    if (record == NULL) {
        return;
    }
    record->value[MP_ENEMY_F_BODY] = 0u;
    ++bodies.reads;
    if (!read_body(body, &state, NULL, &thing_read)) {
        ++bodies.unread;
        return;
    }
    bodies.no_thing += thing_read ? 0u : 1u;
    state.starts = (key < MP_WIRE_KEY_COUNT) ? count_starts(key, body) : 0u;
    record->value[MP_ENEMY_F_BODY] = mp_enemy_body_pack(&state);
}

/* ==============================================================================================
 * A client.
 * ============================================================================================ */

static bool write_parts(uint32_t body, const mp_enemy_body_state_t *host, uint32_t parts,
                        int32_t shooter_class)
{
    bool     ok = true;
    uint32_t thing = 0;

    if ((parts & (MP_ENEMY_BODY_PART_DRAWN | MP_ENEMY_BODY_PART_SHADOW)) != 0u) {
        uint32_t flags = 0;

        ok = memory_try_read_u32((uintptr_t)body + B_FLAGS, &flags);
        flags = mp_enemy_body_flags_with(flags, host, parts);
        ok = ok && memory_try_write((uintptr_t)body + B_FLAGS, &flags, sizeof flags);
    }
    if ((parts & MP_ENEMY_BODY_PART_SOLID) != 0u) {
        int32_t cls = mp_enemy_body_class_for(host->solid, shooter_class);

        ok = memory_try_write((uintptr_t)body + B_CLASS, &cls, sizeof cls) && ok;
    }
    if ((parts & (MP_ENEMY_BODY_PART_ALPHA | MP_ENEMY_BODY_PART_DISSOLVE)) != 0u) {
        float alpha    = mp_enemy_body_level(host->alpha);
        float dissolve = mp_enemy_body_level(host->dissolve);

        ok = memory_try_read_u32((uintptr_t)body + B_THING, &thing) && thing != 0u && ok;
        if ((parts & MP_ENEMY_BODY_PART_ALPHA) != 0u) {
            ok = thing != 0u &&
                 memory_try_write((uintptr_t)thing + THING_ALPHA, &alpha, sizeof alpha) && ok;
        }
        if ((parts & MP_ENEMY_BODY_PART_DISSOLVE) != 0u) {
            ok = thing != 0u &&
                 memory_try_write((uintptr_t)thing + THING_DISSOLVE, &dissolve,
                                  sizeof dissolve) && ok;
        }
    }
    return ok;
}

static void count_written(const mp_enemy_body_state_t *host, uint32_t parts)
{
    uint32_t part;

    for (part = 0; part < MP_ENEMY_BODY_PARTS; ++part) {
        bodies.written[part] += ((parts >> part) & 1u) != 0u ? 1u : 0u;
    }
    if ((parts & MP_ENEMY_BODY_PART_SOLID) != 0u) {
        if (host->solid) {
            ++bodies.stood;
        } else {
            ++bodies.laid;
        }
    }
}

void mp_enemy_body_apply(uintptr_t actor, uint32_t body, uint32_t key,
                         const mp_enemy_record_t *record, const mp_enemy_record_t *previous)
{
    mp_enemy_body_state_t   host;
    mp_enemy_body_state_t   acted;
    mp_enemy_body_state_t   local;
    mp_enemy_body_state_t   after;
    mp_enemy_body_verdict_t verdict;
    replica_acted_t        *m;
    int32_t                 shooter_class = 0;
    bool                    thing_read = false;
    bool                    first;
    uint8_t                 generation;

    (void)actor;
    if (record == NULL || key >= MP_WIRE_KEY_COUNT) {
        return;
    }
    ++bodies.records;
    mp_enemy_body_unpack(record->value[MP_ENEMY_F_BODY], &host);
    if (!host.has) {
        ++bodies.no_body;
        return;
    }
    if (!read_body(body, &local, &shooter_class, &thing_read)) {
        ++bodies.refused;
        return;
    }
    generation = (uint8_t)(record->value[MP_ENEMY_F_GENERATION] & 0xFFu);
    m          = &bodies.acted[key];
    first      = previous == NULL || !m->known || m->generation != generation || m->body != body;
    mp_enemy_body_unpack(m->field, &acted);
    bodies.first += first ? 1u : 0u;

    verdict = mp_enemy_body_decide(&host, first ? NULL : &acted, &local);
    if (verdict.write != 0u) {
        if (!write_parts(body, &host, verdict.write, shooter_class)) {
            ++bodies.refused;
        }
        count_written(&host, verdict.write);
    }
    bodies.already += verdict.already != 0u ? 1u : 0u;
    bodies.left += verdict.left != 0u ? 1u : 0u;

    /* The write proves itself: every part it decided on has the host's value now. A class of 0
     * is what a solid host gets on a body whose shooter class is 0, which is a body this side
     * cannot make collide, and it is counted here with the rest. */
    if (read_body(body, &after, NULL, &thing_read) &&
        (mp_enemy_body_differ(&host, &after) & (verdict.write | verdict.already)) != 0u) {
        ++bodies.apart;
    }
    m->known      = true;
    m->generation = generation;
    m->body       = body;
    m->field      = record->value[MP_ENEMY_F_BODY];
}

void mp_enemy_body_forget(uint32_t key)
{
    if (key < MP_WIRE_KEY_COUNT) {
        bodies.acted[key].known = false;
    }
}

void mp_enemy_body_reset(void)
{
    memset(bodies.starts, 0, sizeof bodies.starts);
    memset(bodies.acted, 0, sizeof bodies.acted);
}

void mp_enemy_body_report(bool described, bool applied)
{
    if (described || bodies.reads != 0u) {
        log_info("the body as the host reads it: %u read(s), %u with no body to read, %u with no "
                 "drawn thing to read the alpha and the dissolve from, %u start(s) of a base clip "
                 "counted, %u of them the clip already playing begun again",
                 (unsigned)bodies.reads, (unsigned)bodies.unread, (unsigned)bodies.no_thing,
                 (unsigned)bodies.clip_starts, (unsigned)bodies.clip_restarts);
    }
    if (applied || bodies.records != 0u) {
        log_info("the body the host describes: %u record(s) carried it, %u with no body read on "
                 "the host; %u first write(s) of a life on a body; values written on a change of "
                 "the host's: %u drawn, %u solid, %u shadow, %u alpha, %u dissolve; %u record(s) "
                 "with a change the body already had, %u with a value of its own left alone while "
                 "the host's stood still; %u laid down as the host's lost its class, %u stood up "
                 "as the host's got it back; %u write(s) refused, %u apart after the write (must "
                 "be 0)",
                 (unsigned)bodies.records, (unsigned)bodies.no_body, (unsigned)bodies.first,
                 (unsigned)bodies.written[0], (unsigned)bodies.written[1],
                 (unsigned)bodies.written[2], (unsigned)bodies.written[3],
                 (unsigned)bodies.written[4], (unsigned)bodies.already, (unsigned)bodies.left,
                 (unsigned)bodies.laid, (unsigned)bodies.stood, (unsigned)bodies.refused,
                 (unsigned)bodies.apart);
    }
}
