/* npc_spawn_block.c: see npc_spawn_block.h. */
#include "npc_spawn_block.h"

#include <math.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <string.h>

#define FLAG_ARCHIVE 0x01u

_Static_assert(NPC_SPAWN_FILE_MAX == 12u, "the block lays a name out in twelve bytes");

_Static_assert(NPC_SPAWN_BLOCK_MAX_BYTES == 7172u, "the size the header names");
_Static_assert(NPC_SPAWN_BLOCK_MAX_BYTES <= 128u * 1024u - 84888u,
               "a full block beside the largest shipped savegame fits the multiplayer's transfer");

static void put_u16(uint8_t *out, uint32_t value)
{
    out[0] = (uint8_t)(value & 0xFFu);
    out[1] = (uint8_t)((value >> 8) & 0xFFu);
}

static void put_u32(uint8_t *out, uint32_t value)
{
    put_u16(out, value & 0xFFFFu);
    put_u16(out + 2, value >> 16);
}

static void put_f32(uint8_t *out, float value)
{
    uint32_t bits;

    memcpy(&bits, &value, sizeof bits);
    put_u32(out, bits);
}

static uint32_t get_u16(const uint8_t *in)
{
    return (uint32_t)in[0] | ((uint32_t)in[1] << 8);
}

static uint32_t get_u32(const uint8_t *in)
{
    return get_u16(in) | (get_u16(in + 2) << 16);
}

static float get_f32(const uint8_t *in)
{
    uint32_t bits = get_u32(in);
    float    value;

    memcpy(&value, &bits, sizeof value);
    return value;
}

static bool finite3(const float *v)
{
    return isfinite(v[0]) && isfinite(v[1]) && isfinite(v[2]);
}

/* A file name the archive could hold: 1 to 12 printable characters, no space, NUL after. */
static bool file_is_valid(const char *file)
{
    size_t i;

    for (i = 0; i < NPC_SPAWN_FILE_MAX && file[i] != '\0'; ++i) {
        if (file[i] <= ' ' || file[i] > '~') {
            return false;
        }
    }
    return i > 0 && file[i] == '\0';
}

bool npc_spawn_block_copy_is_valid(const npc_spawn_saved_t *copy)
{
    const npc_spawn_desc_t *d;

    if (copy == NULL) {
        return false;
    }
    d = &copy->desc;
    return file_is_valid(d->file) && (d->archive || d->source != NPC_SPAWN_NO_SOURCE) &&
           d->behaviour < NPC_SPAWN_BEHAVIOURS &&
           finite3(d->position) && isfinite(d->facing) && finite3(copy->position) &&
           isfinite(copy->yaw);
}

size_t npc_spawn_block_bytes(uint32_t count)
{
    return NPC_SPAWN_BLOCK_LENGTH_BYTES +
           (size_t)count * (NPC_SPAWN_BLOCK_ENTRY_HEAD + NPC_SPAWN_BLOCK_COPY_BYTES);
}

static void put_copy(uint8_t *out, const npc_spawn_saved_t *copy)
{
    const npc_spawn_desc_t *d = &copy->desc;

    out[0] = NPC_SPAWN_BLOCK_TYPE_COPY;
    out[1] = NPC_SPAWN_BLOCK_COPY_VERSION;
    put_u16(out + 2, NPC_SPAWN_BLOCK_COPY_BYTES);
    out += NPC_SPAWN_BLOCK_ENTRY_HEAD;

    out[0] = d->source;
    out[1] = d->behaviour;
    out[2] = d->archive ? FLAG_ARCHIVE : 0u;
    out[3] = 0u;
    memset(out + 4, 0, NPC_SPAWN_FILE_MAX);
    memcpy(out + 4, d->file, strlen(d->file));
    put_f32(out + 16, d->position[0]);
    put_f32(out + 20, d->position[1]);
    put_f32(out + 24, d->position[2]);
    put_f32(out + 28, d->facing);
    put_f32(out + 32, copy->position[0]);
    put_f32(out + 36, copy->position[1]);
    put_f32(out + 40, copy->position[2]);
    put_f32(out + 44, copy->yaw);
    put_u32(out + 48, (uint32_t)copy->health);
}

bool npc_spawn_block_encode(const npc_spawn_saved_t *copies, uint32_t count, uint8_t *out,
                            size_t capacity, size_t *bytes)
{
    size_t   need = npc_spawn_block_bytes(count);
    uint32_t i;

    if (out == NULL || bytes == NULL || (copies == NULL && count != 0u) ||
        count > NPC_SPAWN_BLOCK_COPIES_MAX || capacity < need) {
        return false;
    }
    for (i = 0; i < count; ++i) {
        if (!npc_spawn_block_copy_is_valid(&copies[i])) {
            return false;
        }
    }
    put_u32(out, (uint32_t)(need - NPC_SPAWN_BLOCK_LENGTH_BYTES));
    for (i = 0; i < count; ++i) {
        put_copy(out + NPC_SPAWN_BLOCK_LENGTH_BYTES +
                     (size_t)i * (NPC_SPAWN_BLOCK_ENTRY_HEAD + NPC_SPAWN_BLOCK_COPY_BYTES),
                 &copies[i]);
    }
    *bytes = need;
    return true;
}

/* One copy's 52 bytes. False for a copy no build could raise; the entry is still stepped over. */
static bool get_copy(const uint8_t *in, npc_spawn_saved_t *copy)
{
    npc_spawn_desc_t *d = &copy->desc;
    size_t            i;

    memset(copy, 0, sizeof *copy);
    if (((uint32_t)in[2] & ~FLAG_ARCHIVE) != 0u || in[3] != 0u) {
        return false;
    }
    d->source    = in[0];
    d->behaviour = in[1];
    d->archive   = (in[2] & FLAG_ARCHIVE) != 0u;
    memcpy(d->file, in + 4, NPC_SPAWN_FILE_MAX);
    d->file[NPC_SPAWN_FILE_MAX] = '\0';
    /* Behind the name only NUL: a name with bytes after its end is not one this build wrote. */
    for (i = strlen(d->file); i < NPC_SPAWN_FILE_MAX; ++i) {
        if (in[4 + i] != 0u) {
            return false;
        }
    }
    d->position[0]    = get_f32(in + 16);
    d->position[1]    = get_f32(in + 20);
    d->position[2]    = get_f32(in + 24);
    d->facing         = get_f32(in + 28);
    copy->position[0] = get_f32(in + 32);
    copy->position[1] = get_f32(in + 36);
    copy->position[2] = get_f32(in + 40);
    copy->yaw         = get_f32(in + 44);
    copy->health      = (int32_t)get_u32(in + 48);
    return npc_spawn_block_copy_is_valid(copy);
}

bool npc_spawn_block_decode(const uint8_t *in, size_t bytes, npc_spawn_saved_t *out, uint32_t max,
                            npc_spawn_block_read_t *read)
{
    npc_spawn_block_read_t found;
    size_t                 at = NPC_SPAWN_BLOCK_LENGTH_BYTES;

    memset(&found, 0, sizeof found);
    if (read != NULL) {
        *read = found;
    }
    if (in == NULL || (out == NULL && max != 0u) || bytes < NPC_SPAWN_BLOCK_LENGTH_BYTES ||
        get_u32(in) != bytes - NPC_SPAWN_BLOCK_LENGTH_BYTES) {
        return false;
    }
    /* Checked whole before anything is taken: the caller's array is filled only from a block
     * that reads to its end. */
    while (at < bytes) {
        size_t length;

        if (bytes - at < NPC_SPAWN_BLOCK_ENTRY_HEAD) {
            return false;
        }
        length = get_u16(in + at + 2);
        if (bytes - at - NPC_SPAWN_BLOCK_ENTRY_HEAD < length) {
            return false;
        }
        at += NPC_SPAWN_BLOCK_ENTRY_HEAD + length;
    }
    for (at = NPC_SPAWN_BLOCK_LENGTH_BYTES; at < bytes;) {
        uint32_t          type    = in[at];
        uint32_t          version = in[at + 1];
        size_t            length  = get_u16(in + at + 2);
        const uint8_t    *entry   = in + at + NPC_SPAWN_BLOCK_ENTRY_HEAD;
        npc_spawn_saved_t copy;

        at += NPC_SPAWN_BLOCK_ENTRY_HEAD + length;
        if (type != NPC_SPAWN_BLOCK_TYPE_COPY || version != NPC_SPAWN_BLOCK_COPY_VERSION) {
            ++found.unknown;
            continue;
        }
        /* A version keeps its length; a copy of another length is damaged, not newer. */
        if (length != NPC_SPAWN_BLOCK_COPY_BYTES || !get_copy(entry, &copy)) {
            ++found.invalid;
            continue;
        }
        if (found.copies >= max) {
            ++found.over;
            continue;
        }
        out[found.copies++] = copy;
    }
    if (read != NULL) {
        *read = found;
    }
    return true;
}
