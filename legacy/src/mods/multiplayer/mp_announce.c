/* mp_announce.c: the announce codec. See mp_announce.h. */
#include "mp_announce.h"

#include "mp_wire.h"

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <string.h>

/* The name a session gets when nobody typed one. It is the host's own default and not a blank,
 * because a blank row in a browser is a row nobody clicks. */
#define DEFAULT_NAME "Phantom Menace"

static bool printable(char c)
{
    return c >= 0x20 && c <= 0x7E;
}

bool mp_announce_name_is_sound(const char *name)
{
    size_t i;

    if (name == NULL || name[0] == '\0') {
        return false;
    }
    for (i = 0; i < MP_ANNOUNCE_NAME_MAX; ++i) {
        if (name[i] == '\0') {
            return true;
        }
        if (!printable(name[i])) {
            return false;
        }
    }
    return false;   /* no terminator inside the field */
}

void mp_announce_name_clean(const char *from, char out[MP_ANNOUNCE_NAME_MAX])
{
    size_t at = 0;
    size_t lead;
    size_t i;

    memset(out, 0, MP_ANNOUNCE_NAME_MAX);
    if (from != NULL) {
        for (i = 0; from[i] != '\0' && at + 1u < MP_ANNOUNCE_NAME_MAX; ++i) {
            out[at++] = printable(from[i]) ? from[i] : '?';
        }
    }

    /* Trailing blanks first, then leading ones, because a name typed into an edit field collects
     * both and neither is visible to the person who typed it. */
    while (at > 0u && out[at - 1u] == ' ') {
        out[--at] = '\0';
    }
    lead = 0;
    while (lead < at && out[lead] == ' ') {
        ++lead;
    }
    if (lead > 0u) {
        memmove(out, out + lead, at - lead);
        memset(out + (at - lead), 0, lead);
        at -= lead;
    }
    if (at == 0u) {
        memcpy(out, DEFAULT_NAME, sizeof DEFAULT_NAME);
    }
}

size_t mp_announce_encode(const mp_announce_t *announce, uint8_t *buffer, size_t capacity)
{
    mp_wire_writer_t w;
    size_t           i;

    if (announce == NULL || buffer == NULL || capacity < MP_ANNOUNCE_BYTES) {
        return 0u;
    }
    if (!mp_announce_name_is_sound(announce->name) || announce->game_port == 0u ||
        announce->slots == 0u || announce->players > announce->slots) {
        return 0u;
    }

    mp_wire_writer_init(&w, buffer, capacity);
    mp_wire_put_u32(&w, MP_ANNOUNCE_MAGIC);
    mp_wire_put_u8(&w, MP_ANNOUNCE_VERSION);
    mp_wire_put_u8(&w, announce->wire);
    mp_wire_put_u32(&w, announce->fingerprint);
    mp_wire_put_u16(&w, announce->game_port);
    mp_wire_put_u8(&w, announce->players);
    mp_wire_put_u8(&w, announce->slots);
    mp_wire_put_u8(&w, announce->flags);
    for (i = 0; i < MP_ANNOUNCE_NAME_MAX; ++i) {
        mp_wire_put_u8(&w, (uint8_t)announce->name[i]);
    }
    return w.overflowed ? 0u : w.at;
}

bool mp_announce_is_announce(const uint8_t *buffer, size_t bytes)
{
    uint32_t magic;

    if (buffer == NULL || bytes != MP_ANNOUNCE_BYTES) {
        return false;
    }
    magic = (uint32_t)buffer[0] | ((uint32_t)buffer[1] << 8) | ((uint32_t)buffer[2] << 16) |
            ((uint32_t)buffer[3] << 24);
    return magic == MP_ANNOUNCE_MAGIC;
}

bool mp_announce_decode(const uint8_t *buffer, size_t bytes, mp_announce_t *out)
{
    mp_wire_reader_t r;
    uint32_t         magic = 0;
    size_t           i;

    if (out == NULL || !mp_announce_is_announce(buffer, bytes)) {
        return false;
    }
    memset(out, 0, sizeof *out);

    mp_wire_reader_init(&r, buffer, bytes);
    mp_wire_get_u32(&r, &magic);
    mp_wire_get_u8(&r, &out->version);
    mp_wire_get_u8(&r, &out->wire);
    mp_wire_get_u32(&r, &out->fingerprint);
    mp_wire_get_u16(&r, &out->game_port);
    mp_wire_get_u8(&r, &out->players);
    mp_wire_get_u8(&r, &out->slots);
    mp_wire_get_u8(&r, &out->flags);
    for (i = 0; i < MP_ANNOUNCE_NAME_MAX; ++i) {
        uint8_t byte = 0;

        mp_wire_get_u8(&r, &byte);
        out->name[i] = (char)byte;
    }
    if (r.overran) {
        return false;
    }

    /* The version is refused because a later announce may lay its fields out differently, and
     * everything read above would then be read from the wrong offsets. An unknown WIRE version and
     * an unknown FLAG bit are deliberately kept: both describe a sender this build cannot join,
     * and being able to say that in the list is the reason the field exists. */
    if (out->version != MP_ANNOUNCE_VERSION) {
        return false;
    }
    if (out->game_port == 0u || out->slots == 0u || out->players > out->slots) {
        return false;
    }
    return mp_announce_name_is_sound(out->name);
}

bool mp_announce_joinable(const mp_announce_t *announce, uint8_t wire, uint32_t fingerprint)
{
    if (announce == NULL) {
        return false;
    }
    if ((announce->flags & MP_ANNOUNCE_F_LOCKED) != 0u) {
        return false;
    }
    return announce->wire == wire && announce->fingerprint == fingerprint;
}

bool mp_announce_full(const mp_announce_t *announce)
{
    return announce != NULL && announce->players >= announce->slots;
}
