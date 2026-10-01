/* mp_world_digest.c: the map's digest, and what a comparison of two of them says.
 *
 * The seam this file sits on was drawn in mp_world.c long before it was cut: everything here is
 * arithmetic over a byte buffer and a handful of numbers, and none of it needs a map, an address
 * or a game in the process. Kept beside the code that reaches into the engine, a pure codec is only
 * pure by assertion. Here it is pure by position.
 *
 * The one piece of judgement in the file is the pose comparison. A free runner has no ends, so
 * two poses either side of the wrap are a hair apart rather than a whole travel; every other type
 * is clamped between nothing and its travel by its own case, so taking the short way round there
 * would report agreement that has not been earned.
 */
#include "mp_world.h"

#include "mp_wire.h"

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <string.h>

/* ==============================================================================================
 * The digest codec and the comparison. Pure: everything below this line and above the engine
 * section is arithmetic over a byte buffer and a handful of numbers.
 * ============================================================================================ */

void mp_world_digest_init(mp_world_digest_t *digest, uint32_t tick)
{
    if (digest == NULL) {
        return;
    }
    memset(digest, 0, sizeof *digest);
    digest->tick = tick;
}

void mp_world_digest_set_level(mp_world_digest_t *digest, uint16_t level)
{
    if (digest != NULL) {
        digest->level = level;
    }
}

/* The dwell reaches the wire as milliseconds in two bytes, so it runs out at 65.5 seconds. The
 * field it comes from is the seconds a mover has spent in its current phase, and what reads it is
 * a comparison against the mover's authored hold; no shipped mover holds anywhere near that long,
 * and a value that saturates describes a phase that is over rather than one that is not. */
#define MP_WORLD_DWELL_MAX_MS 65535.0f

static uint16_t dwell_to_wire(float seconds)
{
    float milliseconds;

    if (seconds != seconds || seconds <= 0.0f) {
        return 0u;
    }
    milliseconds = seconds * 1000.0f;
    if (milliseconds > MP_WORLD_DWELL_MAX_MS) {
        return (uint16_t)MP_WORLD_DWELL_MAX_MS;
    }
    return (uint16_t)(milliseconds + 0.5f);
}

static float clamp_fraction(float fraction)
{
    if (fraction != fraction || fraction < 0.0f) {
        return 0.0f;
    }
    return fraction > 1.0f ? 1.0f : fraction;
}

void mp_world_entry_put(mp_wire_writer_t *writer, const mp_world_entry_t *entry)
{
    uint8_t status;

    if (writer == NULL || entry == NULL) {
        return;
    }
    /* Three bits of type, three of direction, one of whether the engine still ticks this mover.
     * The eighth is unspent and stays zero, so a receiver of a later version can tell a set one
     * from a torn byte. */
    status = (uint8_t)((entry->type & 0x07u) | ((entry->dir & 0x07u) << 3));
    if (entry->active != 0u) {
        status = (uint8_t)(status | 0x40u);
    }
    mp_wire_put_u16(writer, entry->id);
    mp_wire_put_u8(writer, status);
    mp_wire_put_u16(writer, (uint16_t)(clamp_fraction(entry->pose) * 65535.0f + 0.5f));
    mp_wire_put_u16(writer, dwell_to_wire(entry->dwell));
}

void mp_world_entry_get(mp_wire_reader_t *reader, mp_world_entry_t *entry)
{
    uint8_t  status = 0;
    uint16_t pose = 0;
    uint16_t dwell = 0;

    if (reader == NULL || entry == NULL) {
        return;
    }
    mp_wire_get_u16(reader, &entry->id);
    mp_wire_get_u8(reader, &status);
    mp_wire_get_u16(reader, &pose);
    mp_wire_get_u16(reader, &dwell);
    entry->type   = (uint8_t)(status & 0x07u);
    entry->dir    = (uint8_t)((status >> 3) & 0x07u);
    entry->active = (uint8_t)((status & 0x40u) != 0u ? 1u : 0u);
    entry->pose   = (float)pose / 65535.0f;
    entry->dwell  = (float)dwell / 1000.0f;
}

mp_world_add_t mp_world_digest_add(mp_world_digest_t *digest, uint32_t id, uint32_t type,
                                   uint32_t dir, uint32_t active, float pose, float length,
                                   float dwell)
{
    mp_world_entry_t *entry;

    if (digest == NULL || id > 0xFFFFu || type >= MP_WORLD_MOVER_TYPES || dir > MOVER_DIR_MAX) {
        return MP_WORLD_ADD_REFUSED;
    }
    /* Written as a positive test so that a length which is not a number is refused too. The
     * engine's own wrap loop subtracts the length from the pose until the pose is no larger, so
     * a mover with no length is one the engine itself cannot integrate. */
    if (!(length > 0.0f) || pose != pose) {
        return MP_WORLD_ADD_REFUSED;
    }
    if (digest->count >= MP_WORLD_DIGEST_MAX_ENTRIES) {
        return MP_WORLD_ADD_FULL;
    }
    entry         = &digest->entry[digest->count];
    entry->id     = (uint16_t)id;
    entry->type   = (uint8_t)type;
    entry->dir    = (uint8_t)dir;
    entry->active = (uint8_t)(active != 0u ? 1u : 0u);
    entry->pose   = clamp_fraction(pose / length);
    entry->dwell  = dwell;
    ++digest->count;
    return MP_WORLD_ADD_OK;
}

size_t mp_world_digest_encode(const mp_world_digest_t *digest, uint8_t *buffer, size_t capacity)
{
    mp_wire_writer_t writer;
    size_t           index;

    if (digest == NULL || buffer == NULL || digest->count > MP_WORLD_DIGEST_MAX_ENTRIES) {
        return 0u;
    }
    mp_wire_writer_init(&writer, buffer, capacity);
    mp_wire_put_u8(&writer, MP_WORLD_DIGEST_TAG);
    mp_wire_put_u32(&writer, digest->tick);
    mp_wire_put_u16(&writer, digest->level);
    mp_wire_put_u8(&writer, digest->count);
    for (index = 0; index < digest->count; ++index) {
        const mp_world_entry_t *entry = &digest->entry[index];

        if (entry->type >= MP_WORLD_MOVER_TYPES || entry->dir > MOVER_DIR_MAX ||
            entry->pose != entry->pose) {
            return 0u;
        }
        mp_world_entry_put(&writer, entry);
    }
    return writer.overflowed ? 0u : writer.at;
}

bool mp_world_is_digest(const uint8_t *buffer, size_t bytes)
{
    size_t body;

    if (buffer == NULL || bytes < MP_WORLD_DIGEST_HEADER_BYTES ||
        buffer[0] != MP_WORLD_DIGEST_TAG) {
        return false;
    }
    body = bytes - MP_WORLD_DIGEST_HEADER_BYTES;
    if ((body % MP_WORLD_DIGEST_ENTRY_BYTES) != 0u) {
        return false;
    }
    /* The count byte and the length say the same thing twice, and a message where they disagree
     * is a truncated one that would otherwise be read as a shorter, valid digest. */
    return (body / MP_WORLD_DIGEST_ENTRY_BYTES) == (size_t)buffer[7] &&
           buffer[7] <= MP_WORLD_DIGEST_MAX_ENTRIES;
}

bool mp_world_digest_decode(const uint8_t *buffer, size_t bytes, mp_world_digest_t *out)
{
    mp_wire_reader_t reader;
    uint8_t          tag = 0;
    uint8_t          count = 0;
    size_t           index;

    if (out == NULL || !mp_world_is_digest(buffer, bytes)) {
        return false;
    }
    memset(out, 0, sizeof *out);
    mp_wire_reader_init(&reader, buffer, bytes);
    mp_wire_get_u8(&reader, &tag);
    mp_wire_get_u32(&reader, &out->tick);
    mp_wire_get_u16(&reader, &out->level);
    mp_wire_get_u8(&reader, &count);
    for (index = 0; index < count; ++index) {
        mp_world_entry_get(&reader, &out->entry[index]);
    }
    if (reader.overran) {
        return false;
    }
    out->count = count;
    return true;
}

float mp_world_pose_delta(uint32_t type, float local_pose, float wire_pose)
{
    float delta;

    if (local_pose != local_pose || wire_pose != wire_pose) {
        return 1.0f;   /* one side has no number, which is the largest disagreement there is */
    }
    delta = local_pose - wire_pose;
    if (delta < 0.0f) {
        delta = -delta;
    }
    if (type == MP_WORLD_TYPE_ALWAYS_ON && delta > 0.5f) {
        delta = 1.0f - delta;
    }
    return delta;
}

size_t mp_world_bucket(float delta)
{
    static const float EDGE[MP_WORLD_BUCKETS - 1u] = {
        1.0f / 256.0f, 1.0f / 64.0f, 1.0f / 16.0f, 1.0f / 4.0f
    };
    size_t index;

    for (index = 0; index < MP_WORLD_BUCKETS - 1u; ++index) {
        if (delta < EDGE[index]) {
            return index;
        }
    }
    return MP_WORLD_BUCKETS - 1u;
}

void mp_world_note(mp_world_stats_t *stats, const mp_world_entry_t *wire,
                   const mp_world_entry_t *local)
{
    mp_world_type_stats_t *row;
    float                  delta;
    uint32_t               milli;

    if (stats == NULL || wire == NULL || local == NULL || wire->type >= MP_WORLD_MOVER_TYPES) {
        return;
    }
    /* Filed under the type the sender named, so a row stays one kind of mover even when the two
     * sides disagree about which kind it is; that disagreement is its own counter. */
    row = &stats->type[wire->type];
    ++row->compared;
    if (wire->type != local->type) {
        ++row->type_mismatch;
    }
    if (wire->dir != local->dir) {
        ++row->dir_mismatch;
    }
    delta = mp_world_pose_delta(wire->type, local->pose, wire->pose);
    ++row->bucket[mp_world_bucket(delta)];
    milli = (uint32_t)(delta * 1000.0f + 0.5f);
    if (milli > row->worst_milli) {
        row->worst_milli = milli;
    }
}

