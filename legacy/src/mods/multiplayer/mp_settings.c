/* mp_settings.c: the menu's intention, and the string handling around it.
 *
 * Written without the C library's string parsers on purpose. `atoi` has no way to say that it
 * stopped early, and `sscanf` with "%u.%u.%u.%u" accepts leading signs, whitespace and a trailing
 * anything, so both would call "  +1.2.3.4rubbish" an address. The player types this field one
 * character at a time and the apply is the only place that may refuse, so what counts as an
 * address has to be decided here and exactly.
 */
#include "mp_settings.h"

#include <string.h>

#define DEFAULT_ADDRESS "127.0.0.1"
#define DEFAULT_PORT    27960u
#define DEFAULT_SESSION_NAME "Phantom Menace"

void mp_settings_default(mp_settings_t *settings)
{
    if (settings == NULL) {
        return;
    }
    memset(settings, 0, sizeof *settings);
    settings->role = MP_SETTINGS_ROLE_OFF;
    settings->port = (uint16_t)DEFAULT_PORT;
    memcpy(settings->address, DEFAULT_ADDRESS, sizeof DEFAULT_ADDRESS);
    memcpy(settings->name, "Player", 7u);
    memcpy(settings->session_name, DEFAULT_SESSION_NAME, sizeof DEFAULT_SESSION_NAME);
    settings->mode           = MP_SETTINGS_MODE_COOP;
    settings->slots          = (uint8_t)MP_SETTINGS_SLOTS_DEFAULT;
    settings->announce       = true;
    settings->net            = MP_SETTINGS_NET_LAN;
    settings->list_public    = false;
    mp_rules_default(&settings->rules);
}

static bool printable(char c)
{
    return c >= 0x20 && c <= 0x7E;
}

void mp_settings_clean_text(const char *from, char *out, size_t out_size)
{
    size_t at = 0;
    size_t lead = 0;
    size_t i;

    if (out == NULL || out_size == 0u) {
        return;
    }
    memset(out, 0, out_size);
    if (from == NULL) {
        return;
    }
    for (i = 0; from[i] != '\0' && at + 1u < out_size; ++i) {
        if (printable(from[i])) {
            out[at++] = from[i];
        }
    }
    while (at > 0u && out[at - 1u] == ' ') {
        out[--at] = '\0';
    }
    while (lead < at && out[lead] == ' ') {
        ++lead;
    }
    if (lead > 0u) {
        memmove(out, out + lead, at - lead);
        memset(out + (at - lead), 0, lead);
    }
}

uint8_t mp_settings_slots_max(mp_settings_mode_t mode)
{
    return mode == MP_SETTINGS_MODE_TDM ? (uint8_t)MP_SETTINGS_SLOTS_TDM_MAX
                                        : (uint8_t)MP_SETTINGS_SLOTS_COOP_MAX;
}

uint8_t mp_settings_clamp_slots_for(int32_t slots, mp_settings_mode_t mode)
{
    int32_t ceiling = (int32_t)mp_settings_slots_max(mode);

    if (slots < (int32_t)MP_SETTINGS_SLOTS_MIN) {
        return (uint8_t)MP_SETTINGS_SLOTS_MIN;
    }
    return slots > ceiling ? (uint8_t)ceiling : (uint8_t)slots;
}

uint8_t mp_settings_slot_notches(mp_settings_mode_t mode)
{
    return (uint8_t)(mp_settings_slots_max(mode) - (uint8_t)MP_SETTINGS_SLOTS_MIN + 1u);
}

bool mp_settings_valid_mode(int32_t mode)
{
    return mode == (int32_t)MP_SETTINGS_MODE_COOP || mode == (int32_t)MP_SETTINGS_MODE_TDM;
}

bool mp_settings_mode_offered(int32_t mode)
{
    return mode == (int32_t)MP_SETTINGS_MODE_COOP;
}

mp_settings_transport_t mp_settings_transport_for(const mp_settings_t *settings)
{
    if (settings == NULL ||
        (settings->role != MP_SETTINGS_ROLE_HOST && settings->role != MP_SETTINGS_ROLE_JOIN)) {
        return MP_SETTINGS_TRANSPORT_NONE;
    }
    return mp_settings_is_public(settings) ? MP_SETTINGS_TRANSPORT_RELAY
                                           : MP_SETTINGS_TRANSPORT_LAN;
}

bool mp_settings_is_public(const mp_settings_t *settings)
{
    return settings != NULL && settings->net == MP_SETTINGS_NET_PUBLIC;
}

uint8_t mp_settings_relay_seats(const mp_settings_t *settings)
{
    uint8_t slots = settings != NULL
                        ? mp_settings_clamp_slots_for((int32_t)settings->slots, settings->mode)
                        : (uint8_t)MP_SETTINGS_SLOTS_MIN;

    return (uint8_t)(slots - 1u);
}

bool mp_settings_needs_new_transport(const mp_settings_armed_t *armed,
                                     const mp_settings_t *settings)
{
    bool public_now;

    if (settings == NULL || armed == NULL || armed->role == 0u) {
        return false;
    }
    if (armed->role != (uint32_t)settings->role) {
        return true;
    }
    public_now = mp_settings_transport_for(settings) == MP_SETTINGS_TRANSPORT_RELAY;
    if (public_now != (armed->net == MP_SETTINGS_NET_PUBLIC)) {
        return true;
    }
    if (settings->role == MP_SETTINGS_ROLE_HOST) {
        return public_now ? armed->seats != mp_settings_relay_seats(settings)
                          : armed->bound_port != settings->port;
    }
    return public_now && strncmp(armed->code, settings->join_code, MP_SETTINGS_CODE_MAX) != 0;
}

/* ==============================================================================================
 * What a lobby offers.
 * ============================================================================================ */

/* Appends one row while there is room, and counts it either way, so the answer is the number of
 * rows the side has rather than the number that happened to fit. */
static void put_row(mp_settings_lobby_row_t row, mp_settings_lobby_row_t *out, size_t capacity,
                    size_t *count)
{
    if (out != NULL && *count < capacity) {
        out[*count] = row;
    }
    ++*count;
}

bool mp_settings_side_chooses_hero(bool is_host, mp_settings_mode_t mode)
{
    return !is_host || mode == MP_SETTINGS_MODE_TDM;
}

size_t mp_settings_lobby_rows(bool is_host, mp_settings_mode_t mode,
                              mp_settings_lobby_row_t *out, size_t capacity)
{
    bool   deathmatch = mode == MP_SETTINGS_MODE_TDM;
    size_t count = 0;

    if (is_host) {
        put_row(MP_SETTINGS_ROW_MAP, out, capacity, &count);
        if (!deathmatch) {
            /* A campaign level is a place a player was already in the middle of, so co-op offers
             * the savegame beside the bare map. A deathmatch has nothing to carry on from. */
            put_row(MP_SETTINGS_ROW_SAVE, out, capacity, &count);
            /* And whether the players may hurt each other, which the host sets for the session
             * the way it sets the difficulty: a client reads it off the note and has no row. A
             * deathmatch has more rule switches than one and offers none of them here yet. */
            put_row(MP_SETTINGS_ROW_FRIENDLY_FIRE, out, capacity, &count);
        }
    }
    if (mp_settings_side_chooses_hero(is_host, mode)) {
        /* In co-op the level prescribes the host's hero and the savegame carries it, so the host
         * has no hero to pick; in a deathmatch there is no level saying anything, so it does. */
        put_row(MP_SETTINGS_ROW_HERO, out, capacity, &count);
    }
    if (deathmatch) {
        put_row(MP_SETTINGS_ROW_TEAM, out, capacity, &count);
    }
    put_row(is_host ? MP_SETTINGS_ROW_START : MP_SETTINGS_ROW_READY, out, capacity, &count);
    return count;
}

mp_settings_mode_t mp_settings_lobby_mode(bool is_host, mp_settings_mode_t own, uint8_t heard)
{
    if (!is_host) {
        return mp_settings_valid_mode((int32_t)heard) ? (mp_settings_mode_t)heard
                                                      : MP_SETTINGS_MODE_COOP;
    }
    return mp_settings_valid_mode((int32_t)own) ? own : MP_SETTINGS_MODE_COOP;
}

mp_settings_mode_t mp_settings_effective_mode(const mp_settings_t *settings)
{
    return mp_settings_lobby_mode(settings->role == MP_SETTINGS_ROLE_HOST, settings->mode,
                                  settings->join_mode);
}

size_t mp_settings_lobby_waiting(const uint8_t *ready, size_t count)
{
    size_t waiting = 0;
    size_t i;

    if (ready == NULL) {
        return 0;
    }
    for (i = 0; i < count; ++i) {
        if (ready[i] == 0u) {
            ++waiting;
        }
    }
    return waiting;
}

bool mp_settings_parse_port(const char *text, uint16_t *port)
{
    uint32_t number = 0;
    size_t   digits = 0;
    size_t   at = 0;

    if (text == NULL || port == NULL) {
        return false;
    }
    while (text[at] >= '0' && text[at] <= '9') {
        if (digits == 5u) {
            return false;
        }
        number = number * 10u + (uint32_t)(text[at] - '0');
        ++digits;
        ++at;
    }
    if (digits == 0u || text[at] != '\0' || !mp_settings_valid_port(number)) {
        return false;
    }
    *port = (uint16_t)number;
    return true;
}

bool mp_settings_parse_int(const char *text, int32_t *value)
{
    uint32_t number = 0;
    size_t   digits = 0;
    size_t   at = 0;
    bool     negative = false;

    if (text == NULL || value == NULL) {
        return false;
    }
    if (text[at] == '-') {
        negative = true;
        ++at;
    }
    while (text[at] >= '0' && text[at] <= '9') {
        if (digits == 10u) {
            return false;
        }
        number = number * 10u + (uint32_t)(text[at] - '0');
        ++digits;
        ++at;
    }
    if (digits == 0u || text[at] != '\0') {
        return false;   /* an empty field, a sign on its own, or anything trailing */
    }
    /* The magnitude is refused before it is negated, because the negative side of a signed long
     * reaches one further than the positive side and a value that only fits there would otherwise
     * pass the check in one direction and not the other. */
    if (number > 2147483647u) {
        return false;
    }
    *value = negative ? -(int32_t)number : (int32_t)number;
    return true;
}

/* One decimal part of an address: at least one digit, at most three, no sign, no space, value
 * inside a byte. `*at` is left on the character that ended it. */
static bool take_part(const char *text, size_t *at, uint32_t *value)
{
    uint32_t number = 0;
    size_t   digits = 0;

    while (text[*at] >= '0' && text[*at] <= '9') {
        if (digits == 3u) {
            return false;
        }
        number = number * 10u + (uint32_t)(text[*at] - '0');
        ++digits;
        ++*at;
    }
    if (digits == 0u || number > 255u) {
        return false;
    }
    *value = number;
    return true;
}

/* Reads the four parts and leaves `*at` on whatever followed them. The caller decides whether that
 * is allowed to be a colon, a terminator or nothing at all. */
static bool take_address(const char *text, size_t *at)
{
    uint32_t part;
    size_t   index;

    for (index = 0; index < 4u; ++index) {
        if (index != 0u) {
            if (text[*at] != '.') {
                return false;
            }
            ++*at;
        }
        if (!take_part(text, at, &part)) {
            return false;
        }
    }
    return true;
}

bool mp_settings_valid_address(const char *text)
{
    size_t at = 0;

    if (text == NULL) {
        return false;
    }
    return take_address(text, &at) && text[at] == '\0';
}

bool mp_settings_valid_port(uint32_t port)
{
    return port != 0u && port <= 0xFFFFu;
}

bool mp_settings_parse_endpoint(const char *text, char *address, size_t address_size,
                                uint16_t *port)
{
    size_t   at = 0;
    size_t   end;
    uint32_t number = 0;
    size_t   digits = 0;

    if (text == NULL || address == NULL || address_size == 0u || port == NULL) {
        return false;
    }
    if (!take_address(text, &at)) {
        return false;
    }
    end = at;

    if (text[at] == ':') {
        ++at;
        while (text[at] >= '0' && text[at] <= '9') {
            if (digits == 5u) {
                return false;
            }
            number = number * 10u + (uint32_t)(text[at] - '0');
            ++digits;
            ++at;
        }
        if (digits == 0u || !mp_settings_valid_port(number)) {
            return false;
        }
    }
    if (text[at] != '\0' || end >= address_size) {
        return false;   /* trailing rubbish, or an address that will not fit where it is going */
    }

    memcpy(address, text, end);
    address[end] = '\0';
    if (digits != 0u) {
        *port = (uint16_t)number;
    }
    return true;
}

/* The port, decimal, written backwards into a small buffer and reversed. Five digits at most, so
 * there is no loop bound worth stating beyond the buffer's own. */
static size_t put_port(char *out, uint16_t port)
{
    char   digits[MP_SETTINGS_PORT_TEXT_MAX];
    size_t count = 0;
    size_t index;

    while (port != 0u && count < sizeof digits) {
        digits[count++] = (char)('0' + (port % 10u));
        port = (uint16_t)(port / 10u);
    }
    for (index = 0; index < count; ++index) {
        out[index] = digits[count - 1u - index];
    }
    return count;
}

bool mp_settings_format_endpoint(const char *address, uint16_t port, char *out, size_t out_size)
{
    size_t length;
    size_t written;

    if (out == NULL || out_size == 0u || !mp_settings_valid_address(address) ||
        !mp_settings_valid_port(port)) {
        return false;
    }
    length = strlen(address);
    if (length + 1u + 5u + 1u > out_size) {
        return false;   /* the worst case port has to fit as well, not just this one */
    }
    memcpy(out, address, length);
    out[length] = ':';
    written = put_port(out + length + 1u, port);
    out[length + 1u + written] = '\0';
    return true;
}

static bool same_server(const mp_settings_server_t *server, const char *address, uint16_t port)
{
    return server->port == port && strcmp(server->address, address) == 0;
}

bool mp_settings_remember(mp_settings_t *settings, const char *address, uint16_t port)
{
    mp_settings_server_t entry;
    size_t               index;
    size_t               found = MP_SETTINGS_SERVERS_MAX;
    size_t               keep;

    if (settings == NULL || !mp_settings_valid_address(address) ||
        !mp_settings_valid_port(port)) {
        return false;
    }
    memset(&entry, 0, sizeof entry);
    entry.port = port;
    memcpy(entry.address, address, strlen(address) + 1u);

    for (index = 0; index < settings->servers; ++index) {
        if (same_server(&settings->server[index], address, port)) {
            found = index;
            break;
        }
    }

    /* Everything above the entry being moved, or above the oldest, slides down one. The head is
     * the most recently chosen, which is what a player reaches for first. */
    keep = found < MP_SETTINGS_SERVERS_MAX ? found : MP_SETTINGS_SERVERS_MAX - 1u;
    if (keep >= settings->servers && settings->servers < MP_SETTINGS_SERVERS_MAX) {
        keep = settings->servers;
        ++settings->servers;
    }
    for (index = keep; index > 0u; --index) {
        settings->server[index] = settings->server[index - 1u];
    }
    settings->server[0] = entry;
    return true;
}

bool mp_settings_forget(mp_settings_t *settings, size_t index)
{
    size_t at;

    if (settings == NULL || index >= settings->servers) {
        return false;
    }
    for (at = index; at + 1u < settings->servers; ++at) {
        settings->server[at] = settings->server[at + 1u];
    }
    --settings->servers;
    memset(&settings->server[settings->servers], 0, sizeof settings->server[0]);
    return true;
}

bool mp_settings_server_text(const mp_settings_t *settings, size_t index, char *out,
                             size_t out_size)
{
    if (settings == NULL || index >= settings->servers) {
        return false;
    }
    return mp_settings_format_endpoint(settings->server[index].address,
                                       settings->server[index].port, out, out_size);
}
