/* mp_savefile.c: the chunk, the request, and the assembly. See the header. */
#include "mp_savefile.h"

#include "mp_wire.h"

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <string.h>

/* FNV-1a, seed 2166136261, prime 16777619, over the raw bytes: the same function two higher layers
 * use for the bank and the content fingerprint, repeated here because this layer may include
 * neither. The unit test pins the empty input to the offset basis and "a" to 0xE40C292C, so a
 * change to any copy would show. */
uint32_t mp_savefile_digest(const void *bytes, size_t size)
{
    const uint8_t *at  = (const uint8_t *)bytes;
    uint32_t       sum = MP_SAVEFILE_FNV_SEED;
    size_t         index;

    for (index = 0; index < size; ++index) {
        sum ^= at[index];
        sum *= 16777619u;
    }
    return sum;
}

uint16_t mp_savefile_chunk_count(uint32_t total)
{
    if (total == 0u || total > MP_SAVEFILE_MAX_BYTES) {
        return 0u;
    }
    return (uint16_t)((total + MP_SAVEFILE_CHUNK_PAYLOAD - 1u) / MP_SAVEFILE_CHUNK_PAYLOAD);
}

/* How many bytes chunk `index` of a file of `total` bytes carries. */
static uint16_t chunk_bytes(uint32_t total, uint16_t index, uint16_t count)
{
    uint32_t start = (uint32_t)index * MP_SAVEFILE_CHUNK_PAYLOAD;

    if (index + 1u < count) {
        return (uint16_t)MP_SAVEFILE_CHUNK_PAYLOAD;
    }
    return (uint16_t)(total - start);
}

/* ==============================================================================================
 * The chunk.
 * ============================================================================================ */

size_t mp_savefile_chunk_encode(uint32_t file_id, const uint8_t *file, uint32_t total,
                                uint16_t index, uint8_t *out, size_t capacity)
{
    mp_wire_writer_t w;
    uint16_t         count = mp_savefile_chunk_count(total);
    uint16_t         bytes;
    size_t           i;

    if (file == NULL || out == NULL || count == 0u || index >= count) {
        return 0;
    }
    bytes = chunk_bytes(total, index, count);
    if (capacity < MP_SAVEFILE_CHUNK_HEAD + (size_t)bytes) {
        return 0;
    }
    mp_wire_writer_init(&w, out, capacity);
    mp_wire_put_u8(&w, (uint8_t)MP_SAVEFILE_CHUNK_TAG);
    mp_wire_put_u32(&w, file_id);
    mp_wire_put_u32(&w, total);
    mp_wire_put_u16(&w, index);
    mp_wire_put_u16(&w, count);
    mp_wire_put_u16(&w, bytes);
    for (i = 0; i < bytes; ++i) {
        mp_wire_put_u8(&w, file[(size_t)index * MP_SAVEFILE_CHUNK_PAYLOAD + i]);
    }
    return w.overflowed ? 0u : w.at;
}

bool mp_savefile_is_chunk(const uint8_t *note, size_t bytes)
{
    return note != NULL && bytes >= MP_SAVEFILE_CHUNK_HEAD && bytes <= MP_SAVEFILE_CHUNK_BYTES &&
           note[0] == (uint8_t)MP_SAVEFILE_CHUNK_TAG;
}

bool mp_savefile_chunk_decode(const uint8_t *note, size_t bytes, mp_savefile_chunk_t *out)
{
    mp_wire_reader_t r;
    uint8_t          tag = 0;

    if (out == NULL || !mp_savefile_is_chunk(note, bytes)) {
        return false;
    }
    memset(out, 0, sizeof *out);
    mp_wire_reader_init(&r, note, bytes);
    mp_wire_get_u8(&r, &tag);
    mp_wire_get_u32(&r, &out->file_id);
    mp_wire_get_u32(&r, &out->total);
    mp_wire_get_u16(&r, &out->index);
    mp_wire_get_u16(&r, &out->count);
    mp_wire_get_u16(&r, &out->bytes);
    if (r.overran) {
        return false;
    }
    /* A stranger wrote the head, so every field is held to what the others imply. A chunk whose
     * count or length disagrees with its total would place bytes where they do not belong. */
    if (out->count == 0u || out->count != mp_savefile_chunk_count(out->total) ||
        out->index >= out->count ||
        out->bytes != chunk_bytes(out->total, out->index, out->count) ||
        bytes != MP_SAVEFILE_CHUNK_HEAD + (size_t)out->bytes) {
        return false;
    }
    out->payload = note + MP_SAVEFILE_CHUNK_HEAD;
    return true;
}

/* ==============================================================================================
 * The request.
 * ============================================================================================ */

bool mp_savefile_is_request(const uint8_t *note, size_t bytes)
{
    return note != NULL && bytes == MP_SAVEFILE_REQUEST_BYTES &&
           note[0] == (uint8_t)MP_SAVEFILE_REQUEST_TAG;
}

/* ==============================================================================================
 * The mask: one bit per chunk, read the same way by the side that holds them and the side that
 * sends them. Both ends of a transfer now speak in these, so a disagreement about who has what
 * cannot survive one acknowledgement.
 * ============================================================================================ */

bool mp_savefile_mask_has(const uint8_t *mask, uint16_t index)
{
    return mask != NULL && (mask[index / 8u] & (uint8_t)(1u << (index % 8u))) != 0u;
}

void mp_savefile_mask_set(uint8_t *mask, uint16_t index)
{
    if (mask != NULL) {
        mask[index / 8u] |= (uint8_t)(1u << (index % 8u));
    }
}

uint16_t mp_savefile_mask_count(const uint8_t *mask, uint16_t count)
{
    uint16_t index;
    uint16_t held = 0;

    for (index = 0; index < count; ++index) {
        if (mp_savefile_mask_has(mask, index)) {
            ++held;
        }
    }
    return held;
}

uint16_t mp_savefile_mask_first_missing(const uint8_t *mask, uint16_t count)
{
    uint16_t index;

    for (index = 0; index < count; ++index) {
        if (!mp_savefile_mask_has(mask, index)) {
            return index;
        }
    }
    return count;
}

/* ==============================================================================================
 * The acknowledgement.
 * ============================================================================================ */

size_t mp_savefile_ack_encode(uint32_t file_id, uint16_t count, const uint8_t *mask, uint8_t *out,
                              size_t capacity)
{
    mp_wire_writer_t w;
    size_t           mask_bytes;
    size_t           i;

    if (out == NULL || mask == NULL || count == 0u || count > MP_SAVEFILE_MAX_CHUNKS) {
        return 0;
    }
    mask_bytes = MP_SAVEFILE_MASK_BYTES_FOR(count);
    if (capacity < MP_SAVEFILE_ACK_HEAD + mask_bytes) {
        return 0;
    }
    mp_wire_writer_init(&w, out, capacity);
    mp_wire_put_u8(&w, (uint8_t)MP_SAVEFILE_ACK_TAG);
    mp_wire_put_u32(&w, file_id);
    mp_wire_put_u16(&w, count);
    for (i = 0; i < mask_bytes; ++i) {
        mp_wire_put_u8(&w, mask[i]);
    }
    return w.overflowed ? 0u : w.at;
}

bool mp_savefile_is_ack(const uint8_t *note, size_t bytes)
{
    return note != NULL && bytes >= MP_SAVEFILE_ACK_HEAD + 1u &&
           bytes <= MP_SAVEFILE_ACK_BYTES && note[0] == (uint8_t)MP_SAVEFILE_ACK_TAG;
}

bool mp_savefile_ack_decode(const uint8_t *note, size_t bytes, uint32_t *file_id, uint16_t *count,
                            const uint8_t **mask)
{
    mp_wire_reader_t r;
    uint8_t          tag = 0;

    if (file_id == NULL || count == NULL || mask == NULL || !mp_savefile_is_ack(note, bytes)) {
        return false;
    }
    mp_wire_reader_init(&r, note, bytes);
    mp_wire_get_u8(&r, &tag);
    mp_wire_get_u32(&r, file_id);
    mp_wire_get_u16(&r, count);
    if (r.overran) {
        return false;
    }
    /* A stranger wrote the head. The declared count decides how many mask bytes must follow, to
     * the byte, so a note cannot claim more chunks than it carries bits for and be read off the
     * end of itself. */
    if (*count == 0u || *count > MP_SAVEFILE_MAX_CHUNKS ||
        bytes != MP_SAVEFILE_ACK_HEAD + MP_SAVEFILE_MASK_BYTES_FOR(*count)) {
        return false;
    }
    *mask = note + MP_SAVEFILE_ACK_HEAD;
    return true;
}

/* ==============================================================================================
 * The assembly.
 * ============================================================================================ */

static bool has_chunk(const mp_savefile_assembly_t *assembly, uint16_t index)
{
    return mp_savefile_mask_has(assembly->have, index);
}

static void note_chunk(mp_savefile_assembly_t *assembly, uint16_t index)
{
    mp_savefile_mask_set(assembly->have, index);
}

/* Everything about the file that is open, and none of the counters: the counters are the
 * assembly's history and survive the file. */
static void forget_the_file(mp_savefile_assembly_t *assembly)
{
    assembly->open     = false;
    assembly->complete = false;
    assembly->file_id  = 0u;
    assembly->total    = 0u;
    assembly->count    = 0u;
    assembly->received = 0u;
    memset(assembly->have, 0, sizeof assembly->have);
}

void mp_savefile_assembly_reset(mp_savefile_assembly_t *assembly)
{
    if (assembly != NULL) {
        memset(assembly, 0, sizeof *assembly);
    }
}

bool mp_savefile_assembly_open(mp_savefile_assembly_t *assembly, uint32_t file_id, uint32_t total)
{
    uint16_t count = mp_savefile_chunk_count(total);

    if (assembly == NULL || count == 0u) {
        return false;
    }
    if (assembly->open && assembly->file_id == file_id && assembly->total == total) {
        return true;   /* the same file, said again: nothing gathered is thrown away */
    }
    forget_the_file(assembly);
    assembly->open    = true;
    assembly->file_id = file_id;
    assembly->total   = total;
    assembly->count   = count;
    return true;
}

bool mp_savefile_assembly_take(mp_savefile_assembly_t *assembly, const mp_savefile_chunk_t *chunk)
{
    if (assembly == NULL || chunk == NULL || chunk->payload == NULL) {
        return false;
    }
    if (!assembly->open || chunk->file_id != assembly->file_id ||
        chunk->total != assembly->total || chunk->count != assembly->count ||
        chunk->index >= assembly->count ||
        chunk->bytes != chunk_bytes(assembly->total, chunk->index, assembly->count)) {
        ++assembly->chunks_refused;
        return false;
    }
    if (has_chunk(assembly, chunk->index)) {
        /* Not a refusal: a repeat is what a restarted request produces, and the tail of a
         * stream that completed the file a moment ago is the same thing. */
        ++assembly->chunks_repeated;
        return true;
    }
    memcpy(assembly->bytes + (size_t)chunk->index * MP_SAVEFILE_CHUNK_PAYLOAD, chunk->payload,
           chunk->bytes);
    note_chunk(assembly, chunk->index);
    ++assembly->received;
    ++assembly->chunks_taken;

    if (assembly->received == assembly->count) {
        /* The name is the proof. A file that does not hash to its own name is not this file, and
         * a restore chain that would load it as a half world is exactly why it is dropped here. */
        if (mp_savefile_digest(assembly->bytes, assembly->total) == assembly->file_id) {
            assembly->complete = true;
        } else {
            uint32_t file_id = assembly->file_id;
            uint32_t total   = assembly->total;

            ++assembly->files_rejected;
            forget_the_file(assembly);
            (void)mp_savefile_assembly_open(assembly, file_id, total);   /* and ask again */
        }
    }
    return true;
}

uint32_t mp_savefile_assembly_percent(const mp_savefile_assembly_t *assembly)
{
    if (assembly == NULL || !assembly->open || assembly->count == 0u) {
        return 0u;
    }
    if (assembly->complete) {
        return 100u;
    }
    return (uint32_t)assembly->received * 100u / assembly->count;
}

/* A clock that went backwards reads as a long wait and fills the bucket, which is what a new
 * session on a new clock should start with anyway. */
uint32_t mp_savefile_pace_fill(mp_savefile_pace_t *pace, uint32_t now_ms, uint32_t per_second,
                               uint32_t burst)
{
    uint64_t full = (uint64_t)burst * 1000u;
    uint64_t held;

    if (pace == NULL) {
        return 0u;
    }
    held = pace->started
               ? pace->milli + (uint64_t)(uint32_t)(now_ms - pace->last_ms) * per_second
               : full;
    pace->milli   = (uint32_t)(held < full ? held : full);
    pace->last_ms = now_ms;
    pace->started = true;
    return pace->milli / 1000u;
}

void mp_savefile_pace_spend(mp_savefile_pace_t *pace, uint32_t slices)
{
    uint64_t spent = (uint64_t)slices * 1000u;

    if (pace != NULL) {
        pace->milli = spent >= pace->milli ? 0u : (uint32_t)(pace->milli - spent);
    }
}
