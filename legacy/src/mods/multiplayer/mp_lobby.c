/* mp_lobby.c: two notes, see the header. */
#include "mp_lobby.h"

#include "mp_wire.h"

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <string.h>

/* How many levels the game's own table holds: eleven, 0..10. */
#define LEVEL_TABLE_ENTRIES 11u

/* ==============================================================================================
 * What a player says about themselves.
 * ============================================================================================ */

size_t mp_lobby_encode(const mp_lobby_t *lobby, uint8_t *buffer, size_t capacity)
{
    if (lobby == NULL || buffer == NULL || capacity < MP_LOBBY_BYTES) {
        return 0;
    }
    if (lobby->team > MP_LOBBY_TEAM_MAX || lobby->hero > MP_LOBBY_HERO_MAX) {
        return 0;
    }
    buffer[0] = (uint8_t)MP_LOBBY_TAG;
    buffer[1] = lobby->team;
    buffer[2] = lobby->ready != 0u ? 1u : 0u;
    buffer[3] = lobby->hero;
    return MP_LOBBY_BYTES;
}

bool mp_lobby_is_lobby(const uint8_t *buffer, size_t bytes)
{
    return buffer != NULL && bytes == MP_LOBBY_BYTES && buffer[0] == (uint8_t)MP_LOBBY_TAG;
}

bool mp_lobby_decode(const uint8_t *buffer, size_t bytes, mp_lobby_t *out)
{
    if (out == NULL || !mp_lobby_is_lobby(buffer, bytes)) {
        return false;
    }
    if (buffer[1] > MP_LOBBY_TEAM_MAX || buffer[2] > 1u || buffer[3] > MP_LOBBY_HERO_MAX) {
        return false;
    }
    out->team  = buffer[1];
    out->ready = buffer[2];
    out->hero  = buffer[3];
    return true;
}

/* ==============================================================================================
 * What the host says about the session.
 * ============================================================================================ */

/* Printable ASCII, NUL terminated inside the field: the rule every string on this wire keeps,
 * because the engine's font has no glyph for anything else and it all ends up in a log. */
static bool field_is_sound(const char *field, size_t size)
{
    size_t i;

    for (i = 0; i < size; ++i) {
        if (field[i] == '\0') {
            return true;
        }
        if (field[i] < 0x20 || field[i] > 0x7E) {
            return false;
        }
    }
    return false;   /* no terminator inside the field */
}

void mp_lobby_clean_field(const char *from, char *out, size_t out_size)
{
    size_t at = 0;
    size_t i;

    if (out == NULL || out_size == 0u) {
        return;
    }
    memset(out, 0, out_size);
    if (from == NULL) {
        return;
    }
    for (i = 0; from[i] != '\0' && at + 1u < out_size; ++i) {
        char c = from[i];

        if (c < 0x20 || c > 0x7E) {
            /* A byte with no glyph is dropped rather than turned into a question mark, because
             * these are file paths, and a `?` in a path is a path that names nothing. */
            continue;
        }
        if (c == ' ' && at == 0u) {
            continue;   /* no leading blanks */
        }
        out[at++] = c;
    }
    while (at > 0u && out[at - 1u] == ' ') {
        out[--at] = '\0';   /* and none trailing */
    }
}

size_t mp_lobby_setup_encode(const mp_lobby_setup_t *setup, uint8_t *buffer, size_t capacity)
{
    mp_wire_writer_t w;
    size_t           i;

    if (setup == NULL || buffer == NULL || capacity < MP_LOBBY_SETUP_BYTES) {
        return 0;
    }
    if (setup->mode != MP_LOBBY_MODE_COOP && setup->mode != MP_LOBBY_MODE_TDM) {
        return 0;
    }
    if ((setup->flags & ~(uint8_t)MP_LOBBY_F_KNOWN) != 0u) {
        return 0;
    }
    if (setup->level_index != (uint8_t)MP_LOBBY_LEVEL_CUSTOM &&
        setup->level_index >= LEVEL_TABLE_ENTRIES) {
        return 0;
    }
    if (setup->level[0] == '\0' || !field_is_sound(setup->level, MP_LOBBY_LEVEL_MAX) ||
        !field_is_sound(setup->title, MP_LOBBY_TITLE_MAX)) {
        return 0;
    }
    if (!mp_rules_valid(&setup->rules) || setup->host_difficulty > MP_LOBBY_DIFFICULTY_MAX) {
        return 0;   /* a rule nobody can play is not sent, so nobody has to decide what it means */
    }
    mp_wire_writer_init(&w, buffer, capacity);
    mp_wire_put_u8(&w, (uint8_t)MP_LOBBY_SETUP_TAG);
    mp_wire_put_u8(&w, setup->mode);
    mp_wire_put_u8(&w, setup->flags);
    mp_wire_put_u8(&w, setup->level_index);
    for (i = 0; i < MP_LOBBY_LEVEL_MAX; ++i) {
        mp_wire_put_u8(&w, (uint8_t)setup->level[i]);
    }
    for (i = 0; i < MP_LOBBY_TITLE_MAX; ++i) {
        mp_wire_put_u8(&w, (uint8_t)setup->title[i]);
    }
    if (!mp_rules_put(&w, &setup->rules)) {
        return 0;
    }
    mp_wire_put_u8(&w, setup->generation);
    mp_wire_put_u32(&w, setup->save_id);
    mp_wire_put_u32(&w, setup->save_bytes);
    mp_wire_put_u8(&w, setup->host_difficulty);
    return w.overflowed ? 0u : w.at;
}

bool mp_lobby_is_setup(const uint8_t *buffer, size_t bytes)
{
    return buffer != NULL && bytes == MP_LOBBY_SETUP_BYTES &&
           buffer[0] == (uint8_t)MP_LOBBY_SETUP_TAG;
}

bool mp_lobby_setup_decode(const uint8_t *buffer, size_t bytes, mp_lobby_setup_t *out)
{
    mp_wire_reader_t r;
    uint8_t          tag = 0;
    size_t           i;

    if (out == NULL || !mp_lobby_is_setup(buffer, bytes)) {
        return false;
    }
    memset(out, 0, sizeof *out);
    mp_wire_reader_init(&r, buffer, bytes);
    mp_wire_get_u8(&r, &tag);
    mp_wire_get_u8(&r, &out->mode);
    mp_wire_get_u8(&r, &out->flags);
    mp_wire_get_u8(&r, &out->level_index);
    for (i = 0; i < MP_LOBBY_LEVEL_MAX; ++i) {
        uint8_t byte = 0;

        mp_wire_get_u8(&r, &byte);
        out->level[i] = (char)byte;
    }
    for (i = 0; i < MP_LOBBY_TITLE_MAX; ++i) {
        uint8_t byte = 0;

        mp_wire_get_u8(&r, &byte);
        out->title[i] = (char)byte;
    }
    /* The rule set holds itself to the same range in this direction as in the other, which is the
     * whole reason its two halves live in one file. */
    if (!mp_rules_get(&r, &out->rules)) {
        return false;
    }
    mp_wire_get_u8(&r, &out->generation);
    /* The savegame's name and size are free values: any digest is a digest, and a size the
     * transfer refuses is refused THERE, with a line that names it, not here in silence. */
    mp_wire_get_u32(&r, &out->save_id);
    mp_wire_get_u32(&r, &out->save_bytes);
    mp_wire_get_u8(&r, &out->host_difficulty);
    if (r.overran || out->host_difficulty > MP_LOBBY_DIFFICULTY_MAX) {
        return false;
    }
    /* A stranger wrote all of this, so it is held to exactly what the encoder promises. An
     * unknown flag bit is refused rather than ignored: the bits decide whether a LEVEL LOADS, and
     * a client that guessed at one would load the wrong thing or nothing. */
    if (out->mode != MP_LOBBY_MODE_COOP && out->mode != MP_LOBBY_MODE_TDM) {
        return false;
    }
    if ((out->flags & ~(uint8_t)MP_LOBBY_F_KNOWN) != 0u) {
        return false;
    }
    if (out->level_index != (uint8_t)MP_LOBBY_LEVEL_CUSTOM &&
        out->level_index >= LEVEL_TABLE_ENTRIES) {
        return false;
    }
    if (out->level[0] == '\0' || !field_is_sound(out->level, MP_LOBBY_LEVEL_MAX) ||
        !field_is_sound(out->title, MP_LOBBY_TITLE_MAX)) {
        return false;
    }
    return true;
}

mp_lobby_join_t mp_lobby_join_status(bool content_mismatch, bool denied, bool host_left,
                                     bool gave_up, bool connected)
{
    if (content_mismatch) {
        return MP_LOBBY_JOIN_CONTENT;
    }
    if (denied) {
        return MP_LOBBY_JOIN_DENIED;
    }
    if (host_left) {
        return MP_LOBBY_JOIN_HOST_LEFT;   /* a goodbye is an answer, not silence */
    }
    if (gave_up) {
        return MP_LOBBY_JOIN_GAVE_UP;
    }
    return connected ? MP_LOBBY_JOIN_CONNECTED : MP_LOBBY_JOIN_ASKING;
}

mp_lobby_password_t mp_lobby_password_question(bool heard, bool wants)
{
    if (!heard) {
        return MP_LOBBY_PASSWORD_WAIT;   /* nobody said anything, so nobody is asked anything */
    }
    return wants ? MP_LOBBY_PASSWORD_ASK : MP_LOBBY_PASSWORD_NONE;
}

mp_lobby_over_t mp_lobby_session_over(bool started, bool level_running, bool is_client,
                                      bool ended_flag, bool far_side_present,
                                      bool far_side_given_up, bool content_mismatch,
                                      bool sent_away)
{
    if (!started || !level_running) {
        return MP_LOBBY_OVER_NO;
    }
    if (is_client) {
        /* The client's own doing, and it comes first: it disconnected itself because the two
         * builds disagree about the content, so the absence that follows is not a lost host.
         * Judged as one it put the wrong sentence on the screen. */
        if (content_mismatch) {
            return MP_LOBBY_OVER_CONTENT;
        }
        /* The host sent this side away with a reason: the session is over for this machine, and
         * shown as a lost host the player would look for the fault in the wire instead. */
        if (sent_away && !far_side_present) {
            return MP_LOBBY_OVER_BEHIND;
        }
        /* The host's own word first. It arrives within a second of the host deciding, and it is
         * the only evidence for a host that walked back to its title screen with the socket still
         * open and answering. */
        if (ended_flag) {
            return MP_LOBBY_OVER_HOST_ENDED;
        }
        /* And otherwise silence, but only once the session itself has stopped trying. A peer that
         * is merely between levels is gone for half a minute and comes back; giving up is the
         * session's judgement, not this rule's, and this rule waits for it. */
        if (!far_side_present && far_side_given_up) {
            return MP_LOBBY_OVER_HOST_LOST;
        }
        return MP_LOBBY_OVER_NO;
    }
    /* The host has nobody to be told by, so it has only the one question. It is asked the same
     * way: not "is anyone missing" but "is anyone left", so a session of four that loses one
     * keeps playing. */
    if (!far_side_present && far_side_given_up) {
        return MP_LOBBY_OVER_ALL_LEFT;
    }
    return MP_LOBBY_OVER_NO;
}

size_t mp_lobby_content_encode(uint32_t fingerprint, uint8_t *buffer, size_t capacity)
{
    mp_wire_writer_t w;

    if (buffer == NULL || capacity < MP_LOBBY_CONTENT_BYTES || fingerprint == 0u) {
        return 0;
    }
    mp_wire_writer_init(&w, buffer, capacity);
    mp_wire_put_u8(&w, (uint8_t)MP_LOBBY_CONTENT_TAG);
    mp_wire_put_u32(&w, fingerprint);
    return w.overflowed ? 0u : w.at;
}

bool mp_lobby_is_content(const uint8_t *buffer, size_t bytes)
{
    return buffer != NULL && bytes == MP_LOBBY_CONTENT_BYTES &&
           buffer[0] == (uint8_t)MP_LOBBY_CONTENT_TAG;
}

bool mp_lobby_content_decode(const uint8_t *buffer, size_t bytes, uint32_t *out)
{
    mp_wire_reader_t r;
    uint8_t          tag = 0;
    uint32_t         fingerprint = 0;

    if (out == NULL || !mp_lobby_is_content(buffer, bytes)) {
        return false;
    }
    mp_wire_reader_init(&r, buffer, bytes);
    mp_wire_get_u8(&r, &tag);
    mp_wire_get_u32(&r, &fingerprint);
    if (r.overran || fingerprint == 0u) {
        return false;
    }
    *out = fingerprint;
    return true;
}

bool mp_lobby_setup_equal(const mp_lobby_setup_t *a, const mp_lobby_setup_t *b)
{
    if (a == NULL || b == NULL) {
        return false;
    }
    return a->mode == b->mode && a->flags == b->flags && a->level_index == b->level_index &&
           a->generation == b->generation && mp_rules_equal(&a->rules, &b->rules) &&
           a->save_id == b->save_id && a->save_bytes == b->save_bytes &&
           a->host_difficulty == b->host_difficulty &&
           strncmp(a->level, b->level, MP_LOBBY_LEVEL_MAX) == 0 &&
           strncmp(a->title, b->title, MP_LOBBY_TITLE_MAX) == 0;
}

bool mp_lobby_generation_is_new(uint8_t acted_on, uint8_t heard)
{
    /* Inequality, never order. The byte wraps, and the wrap from 255 to 0 is a world change like
     * any other; a greater-than would let that one pass unnoticed and then refuse every change
     * after it for the rest of the session. */
    return acted_on != heard;
}

bool mp_lobby_start_may_be_taken(bool this_player_ready, bool start_offered)
{
    return start_offered && this_player_ready;
}

bool mp_lobby_may_damage(uint8_t mode, uint8_t attacker_team, uint8_t victim_team,
                         bool friendly_fire)
{
    if (mode != MP_LOBBY_MODE_COOP && mode != MP_LOBBY_MODE_TDM) {
        return false;   /* an unknown game is not a licence to hurt anybody */
    }
    if (friendly_fire) {
        return true;
    }
    if (mode == MP_LOBBY_MODE_COOP) {
        return false;
    }
    if (attacker_team == MP_LOBBY_TEAM_NONE || victim_team == MP_LOBBY_TEAM_NONE) {
        return true;
    }
    return attacker_team != victim_team;
}
