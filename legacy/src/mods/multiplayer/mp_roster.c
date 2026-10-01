/* mp_roster.c: the roster codec. */
#include "mp_roster.h"

#include "mp_wire.h"

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <string.h>

/* The backslash is not a name character here, and the reason is two layers down.
 *
 * Every string this feature shows reaches the engine's text drawer, which runs an escape pass over
 * it before anything is measured or drawn: a backslash followed by n becomes a real newline. A
 * player who names himself that way therefore draws as TWO lines in a list whose rows are one line
 * each, and the table under him shifts by one for everybody who can see him.
 *
 * It is NOT a memory defect, and the source that said so is wrong. The reconstruction of the
 * text drawer used to describe the lone-backslash branch of that pass as a write one byte before
 * its 2048 byte stack buffer. The branch is `mov edx,[ebp-8] / sub edx,1 / mov [ebp-8],edx` at
 * 0x0046B486 and stores nothing; the loop head adds the one back before anything is written.
 * Corrected there on 2026-09-07.
 *
 * It is filtered HERE rather than at each place a name is drawn, because there is one of these and
 * there are several of those, and the property worth having is that no name CAN break a row. A
 * backslash has no legitimate use in a player name; a tab and a newline are already out, since
 * neither is printable. */
static bool printable(char c)
{
    return c >= 0x20 && c <= 0x7E && c != '\\';
}

bool mp_roster_name_is_sound(const char *name)
{
    size_t i;

    if (name == NULL || name[0] == '\0') {
        return false;
    }
    for (i = 0; i < MP_ROSTER_NAME_MAX; ++i) {
        if (name[i] == '\0') {
            return true;
        }
        if (!printable(name[i])) {
            return false;
        }
    }
    return false;   /* no terminator inside the field */
}

void mp_roster_name_clean(const char *from, char out[MP_ROSTER_NAME_MAX])
{
    size_t at = 0;
    size_t i;

    memset(out, 0, MP_ROSTER_NAME_MAX);
    if (from != NULL) {
        for (i = 0; from[i] != '\0' && at < MP_ROSTER_NAME_MAX - 1u; ++i) {
            out[at++] = printable(from[i]) ? from[i] : '?';
        }
    }
    /* Leading and trailing blanks are not a name, and an empty one gets the default. */
    while (at > 0u && out[at - 1u] == ' ') {
        out[--at] = '\0';
    }
    if (at > 0u && out[0] == ' ') {
        size_t lead = 0;

        while (lead < at && out[lead] == ' ') {
            ++lead;
        }
        memmove(out, out + lead, at - lead);
        memset(out + (at - lead), 0, lead);
        at -= lead;
    }
    if (at == 0u) {
        memcpy(out, "Player", 7u);
    }
}

/* The asset name rule, which is the engine's own resource gate and not this file's choice: at most
 * one dot, at most eight characters before it, at most three after it, and every other character
 * out of a fixed alphabet of the letters, the digits and the underscore. A name outside that can
 * never name a resource, because the gate refuses it before any file is opened, so carrying one
 * would only postpone the refusal to a layer with nobody left to tell.
 *
 * The alphabet has no hyphen. Names with one exist in this game's data, but they are clip names
 * inside a .baf and a clip name never reaches the resource gate.
 *
 * The appearance event applies the same rule to the same 32 byte field, and the two are separate
 * copies on purpose: the roster codec and the event codec are pure modules that link independently
 * of each other, and a call from one to the other would make every build of either drag in the
 * other. The obligation the copy creates is that they must agree, and both unit tests drive the
 * same table of border cases so that a change to one and not the other fails.
 *
 * An EMPTY field is sound here and is not in the event. A roster line for a player who has not
 * changed appearance has nothing to say about it, and saying nothing is the honest encoding of
 * that; an appearance event, by contrast, exists only to name something. */
static bool asset_char_is_allowed(char c)
{
    static const char ALPHABET[] = MP_EVENT_ASSET_ALPHABET;
    size_t            i;

    for (i = 0; i < sizeof ALPHABET - 1u; ++i) {
        if (ALPHABET[i] == c) {
            return true;
        }
    }
    return false;
}

bool mp_roster_asset_is_sound(const char *asset)
{
    size_t at;
    size_t dot_at = 0;
    bool   dotted = false;
    bool   terminated = false;

    if (asset == NULL) {
        return false;
    }
    for (at = 0; at < MP_EVENT_ASSET_MAX; ++at) {
        char c = asset[at];

        if (c == '\0') {
            terminated = true;
            break;
        }
        if (c == '.') {
            if (dotted || at > MP_EVENT_ASSET_STEM_MAX) {
                return false;   /* a second dot, or a stem the engine's gate would refuse */
            }
            dotted = true;
            dot_at = at;
        } else if (!asset_char_is_allowed(c)) {
            return false;
        }
    }
    if (!terminated) {
        return false;
    }
    if (dotted && at - dot_at - 1u > MP_EVENT_ASSET_EXTENSION_MAX) {
        return false;   /* an extension longer than three */
    }
    for (++at; at < MP_EVENT_ASSET_MAX; ++at) {
        if (asset[at] != '\0') {
            return false;   /* padding that carries something is not this build's encoding */
        }
    }
    return true;
}

/* A name that does not fit is emptied rather than cut. Cutting an asset name does not shorten it,
 * it renames it: the result either names a different actor or names nothing, and both are worse
 * answers than saying the appearance is not known. A player name is cut because a shortened name
 * is still that player; an asset name is not that kind of string. */
void mp_roster_asset_clean(const char *from, char out[MP_EVENT_ASSET_MAX])
{
    size_t i;

    memset(out, 0, MP_EVENT_ASSET_MAX);
    if (from == NULL) {
        return;
    }
    for (i = 0; i < MP_EVENT_ASSET_MAX && from[i] != '\0'; ++i) {
        out[i] = from[i];
    }
    if (i == MP_EVENT_ASSET_MAX || !mp_roster_asset_is_sound(out)) {
        memset(out, 0, MP_EVENT_ASSET_MAX);   /* too long, or not a name this engine could load */
    }
}

static void put_entry(mp_wire_writer_t *w, const mp_roster_entry_t *entry)
{
    size_t i;

    mp_wire_put_u8(w, entry->slot);
    mp_wire_put_u8(w, entry->team);
    mp_wire_put_u8(w, entry->ready);
    mp_wire_put_u8(w, entry->hero);
    mp_wire_put_u16(w, entry->rtt_ms);
    for (i = 0; i < MP_ROSTER_NAME_MAX; ++i) {
        mp_wire_put_u8(w, (uint8_t)entry->name[i]);
    }
    for (i = 0; i < MP_EVENT_ASSET_MAX; ++i) {
        mp_wire_put_u8(w, (uint8_t)entry->asset[i]);
    }
    mp_wire_put_u16(w, entry->scale);
    mp_wire_put_u8(w, entry->asset_kind);
}

/* A kind this build knows, and none but the first for an asset that is not known. */
static bool kind_is_sound(const mp_roster_entry_t *entry)
{
    return entry->asset_kind <= MP_SKIN_KIND_MAX &&
           (entry->asset[0] != '\0' || entry->asset_kind == MP_SKIN_CHARACTER);
}

size_t mp_roster_encode(const mp_roster_t *roster, uint8_t *buffer, size_t capacity)
{
    mp_wire_writer_t w;
    size_t           i;

    if (roster == NULL || buffer == NULL || roster->count > MP_ROSTER_MAX_ENTRIES ||
        capacity < MP_ROSTER_BYTES_FOR(roster->count)) {
        return 0u;
    }
    for (i = 0; i < roster->count; ++i) {
        if (!mp_roster_name_is_sound(roster->entry[i].name) || roster->entry[i].ready > 1u ||
            !mp_wire_scale_is_sound(roster->entry[i].scale) ||
            !mp_roster_asset_is_sound(roster->entry[i].asset) ||
            !kind_is_sound(&roster->entry[i])) {
            return 0u;
        }
    }
    mp_wire_writer_init(&w, buffer, capacity);
    mp_wire_put_u8(&w, MP_ROSTER_TAG);
    mp_wire_put_u8(&w, roster->count);
    for (i = 0; i < roster->count; ++i) {
        put_entry(&w, &roster->entry[i]);
    }
    return w.overflowed ? 0u : w.at;
}

bool mp_roster_is_roster(const uint8_t *buffer, size_t bytes)
{
    /* The count is read out of the message before anything that depends on it, so this is one
     * exact length rather than a range: a message one byte short of what its own count promises
     * is refused as flatly as one with the wrong tag. */
    return buffer != NULL && bytes >= 2u && buffer[0] == MP_ROSTER_TAG &&
           buffer[1] <= MP_ROSTER_MAX_ENTRIES && bytes == MP_ROSTER_BYTES_FOR(buffer[1]);
}

bool mp_roster_decode(const uint8_t *buffer, size_t bytes, mp_roster_t *out)
{
    mp_wire_reader_t r;
    uint8_t          tag = 0;
    size_t           i;

    if (out == NULL || !mp_roster_is_roster(buffer, bytes)) {
        return false;
    }
    memset(out, 0, sizeof *out);
    mp_wire_reader_init(&r, buffer, bytes);
    mp_wire_get_u8(&r, &tag);
    mp_wire_get_u8(&r, &out->count);
    if (out->count > MP_ROSTER_MAX_ENTRIES) {
        return false;
    }
    for (i = 0; i < out->count; ++i) {
        mp_roster_entry_t *entry = &out->entry[i];
        size_t             k;

        mp_wire_get_u8(&r, &entry->slot);
        mp_wire_get_u8(&r, &entry->team);
        mp_wire_get_u8(&r, &entry->ready);
        mp_wire_get_u8(&r, &entry->hero);
        mp_wire_get_u16(&r, &entry->rtt_ms);
        for (k = 0; k < MP_ROSTER_NAME_MAX; ++k) {
            uint8_t byte = 0;

            mp_wire_get_u8(&r, &byte);
            entry->name[k] = (char)byte;
        }
        for (k = 0; k < MP_EVENT_ASSET_MAX; ++k) {
            uint8_t byte = 0;

            mp_wire_get_u8(&r, &byte);
            entry->asset[k] = (char)byte;
        }
        mp_wire_get_u16(&r, &entry->scale);
        mp_wire_get_u8(&r, &entry->asset_kind);
        if (entry->ready > 1u || !mp_roster_name_is_sound(entry->name) ||
            !mp_wire_scale_is_sound(entry->scale) ||
            !mp_roster_asset_is_sound(entry->asset) || !kind_is_sound(entry)) {
            return false;
        }
    }
    return !r.overran;
}

/* The comparison both public forms share, with the one field that separates them as a flag. */
static bool entries_differ(const mp_roster_t *a, const mp_roster_t *b, bool with_round_trip)
{
    size_t i;

    if (a == NULL || b == NULL || a->count != b->count) {
        return true;
    }
    for (i = 0; i < a->count; ++i) {
        if (a->entry[i].slot != b->entry[i].slot || a->entry[i].team != b->entry[i].team ||
            a->entry[i].ready != b->entry[i].ready || a->entry[i].hero != b->entry[i].hero ||
            strncmp(a->entry[i].name, b->entry[i].name, MP_ROSTER_NAME_MAX) != 0 ||
            a->entry[i].asset_kind != b->entry[i].asset_kind ||
            a->entry[i].scale != b->entry[i].scale ||
            strncmp(a->entry[i].asset, b->entry[i].asset, MP_EVENT_ASSET_MAX) != 0) {
            return true;
        }
        if (with_round_trip && a->entry[i].rtt_ms != b->entry[i].rtt_ms) {
            return true;
        }
    }
    return false;
}

bool mp_roster_equal(const mp_roster_t *a, const mp_roster_t *b)
{
    return !entries_differ(a, b, true);
}

bool mp_roster_differs_to_a_player(const mp_roster_t *a, const mp_roster_t *b)
{
    return entries_differ(a, b, false);
}
