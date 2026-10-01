/* mp_settings_ini.c: the intention on disk. See mp_settings_ini.h. */
#include "mp_settings_ini.h"

#include "mp_roster.h"
#include "mp_rules.h"
#include "multiplayer.h"

#include "common/ini.h"
#include "common/text.h"

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

/* The mode is written as a WORD, the same two the ini's GameMode key already understands, so a
 * player reading the file sees "coop" and not "1". */
static const char *mode_word(mp_settings_mode_t mode)
{
    return mode == MP_SETTINGS_MODE_TDM ? "tdm" : "coop";
}

/* A word for a game this build does not offer is read as co-op, and the next save writes coop
 * back over it. The two words are still told apart above, because the day the deathmatch comes
 * back the file should not have to learn its own word again. */
static mp_settings_mode_t mode_of(const char *word, mp_settings_mode_t fallback)
{
    mp_settings_mode_t mode = fallback;

    if (_stricmp(word, "tdm") == 0) {
        mode = MP_SETTINGS_MODE_TDM;
    } else if (_stricmp(word, "coop") == 0) {
        mode = MP_SETTINGS_MODE_COOP;
    }
    return mp_settings_mode_offered((int32_t)mode) ? mode : MP_SETTINGS_MODE_COOP;
}

/* One whole number out of the file, or nothing.
 *
 * It goes through the string read rather than through the integer read on purpose. The profile
 * API's integer read cannot tell a key that is absent from a key whose value is empty: the first
 * answers the default and the second answers zero. Zero is a legal setting for four of the seven
 * rules and means "no limit", so a key somebody blanked out by hand would quietly turn a limit off
 * instead of leaving it as it was. False here leaves the caller's value exactly where it was. */
static bool read_number(const char *key, int32_t *value)
{
    char text[32];

    if (!ini_read_string(MULTIPLAYER_SECTION, key, "", text, sizeof text)) {
        return false;
    }
    return mp_settings_parse_int(text, value);
}

/* The seven rules. Every one of them is read into a local first and only then put into the rule
 * set, and the whole set is clamped once at the end rather than field by field, so a file with one
 * bad number costs that number and nothing else. Nothing in here ever calls mp_rules_default: a
 * default belongs at the start of the load, and putting one in the middle is how a load comes to
 * throw away everything it had already read. */
static void load_rules(mp_rules_t *rules)
{
    int32_t number = 0;

    if (read_number("ScoreLimit", &number)) {
        rules->score_limit = (uint16_t)(number < 0 ? 0 : number > 0xFFFF ? 0xFFFF : number);
    }
    /* The unit is in the key name because a bare TimeLimit reads as minutes. */
    if (read_number("TimeLimitSeconds", &number)) {
        rules->time_limit_s = (uint16_t)(number < 0 ? 0 : number > 0xFFFF ? 0xFFFF : number);
    }
    if (read_number("Teams", &number)) {
        rules->flags = number != 0 ? (uint8_t)(rules->flags | MP_RULES_F_TEAMS)
                                   : (uint8_t)(rules->flags & ~(uint8_t)MP_RULES_F_TEAMS);
    }
    if (read_number("FriendlyFire", &number)) {
        rules->flags = number != 0 ? (uint8_t)(rules->flags | MP_RULES_F_FRIENDLY_FIRE)
                                   : (uint8_t)(rules->flags & ~(uint8_t)MP_RULES_F_FRIENDLY_FIRE);
    }
    if (read_number("BetrayalPenalty", &number)) {
        rules->team_penalty = (int8_t)(number < -128 ? -128 : number > 127 ? 127 : number);
    }
    if (read_number("SuicidePenalty", &number)) {
        rules->suicide_penalty = (int8_t)(number < -128 ? -128 : number > 127 ? 127 : number);
    }
    /* The key names its unit, because tenths of a second is not what a reader would assume and
     * this file has no way to carry a comment. */
    if (read_number("RespawnTenths", &number)) {
        rules->respawn_tenths = (uint8_t)(number < 0 ? 0 : number > 0xFF ? 0xFF : number);
    }
    (void)mp_rules_clamp(rules);
}

void mp_settings_ini_load(mp_settings_t *settings)
{
    char     text[64];
    uint32_t index;

    if (settings == NULL) {
        return;
    }
    mp_settings_default(settings);

    /* The role is not read back. It is what this session picked, and mp_settings_default
     * has already put it at off, which is what is installed before anybody picks. */
    if (ini_read_string(MULTIPLAYER_SECTION, "MenuServer", "", text, sizeof text)) {
        (void)mp_settings_parse_endpoint(text, settings->address, sizeof settings->address,
                                         &settings->port);
    }
    if (ini_read_string(MULTIPLAYER_SECTION, "PlayerName", "", text, sizeof text)) {
        mp_roster_name_clean(text, settings->name);
    }
    for (index = 0; index < MP_SETTINGS_SERVERS_MAX; ++index) {
        char     key[16];
        char     address[MP_SETTINGS_ADDRESS_MAX];
        uint16_t port = settings->port;

        text_format(key, sizeof key, "Server%u", (unsigned)index);
        if (!ini_read_string(MULTIPLAYER_SECTION, key, "", text, sizeof text) || text[0] == '\0') {
            continue;
        }
        if (mp_settings_parse_endpoint(text, address, sizeof address, &port)) {
            /* Read oldest first so that the head of the file ends up at the head of the list. */
            (void)mp_settings_remember(settings, address, port);
        }
    }

    if (ini_read_string(MULTIPLAYER_SECTION, "SessionName", "", text, sizeof text) &&
        text[0] != '\0') {
        char cleaned[MP_SETTINGS_SESSION_NAME_MAX];

        /* A name of only blanks is no name, so the default stands. It is put back FIELD BY FIELD:
         * calling mp_settings_default here would throw away the role, the address, the server
         * list and the player name, all of which were already read above. */
        mp_settings_clean_text(text, cleaned, sizeof cleaned);
        if (cleaned[0] != '\0') {
            memcpy(settings->session_name, cleaned, sizeof cleaned);
        }
    }
    /* The passwords are not read back: they are not kept in the file at all, see the save. */
    if (ini_read_string(MULTIPLAYER_SECTION, "MenuMode", "", text, sizeof text)) {
        settings->mode = mode_of(text, settings->mode);
    }
    /* Against the ceiling of the mode just read, not against the larger of the two: a co-op session
     * of sixteen is a number the game has no levels for; four is the co-op ceiling. */
    settings->slots = mp_settings_clamp_slots_for(ini_read_int(MULTIPLAYER_SECTION, "Slots",
                                                              (int32_t)settings->slots),
                                                  settings->mode);
    settings->announce = ini_read_bool(MULTIPLAYER_SECTION, "Announce", settings->announce);
    /* The network is a word, like the mode. Anything but "public" is the LAN, which never
     * reaches the relay. */
    if (ini_read_string(MULTIPLAYER_SECTION, "MenuNet", "", text, sizeof text)) {
        settings->net = _stricmp(text, "public") == 0 ? MP_SETTINGS_NET_PUBLIC
                                                      : MP_SETTINGS_NET_LAN;
    }
    settings->list_public = ini_read_bool(MULTIPLAYER_SECTION, "ListPublic",
                                          settings->list_public);
    load_rules(&settings->rules);
}

void mp_settings_ini_save_name(const char *name)
{
    if (name == NULL || name[0] == '\0') {
        return;
    }
    (void)ini_write_string(MULTIPLAYER_SECTION, "PlayerName", name);
}

void mp_settings_ini_save(const mp_settings_t *settings)
{
    char     text[MP_SETTINGS_ENDPOINT_MAX];
    uint32_t index;

    if (settings == NULL) {
        return;
    }
    (void)ini_write_string(MULTIPLAYER_SECTION, "PlayerName", settings->name);
    if (mp_settings_format_endpoint(settings->address, settings->port, text, sizeof text)) {
        (void)ini_write_string(MULTIPLAYER_SECTION, "MenuServer", text);
    }
    /* Written newest last, so a load that remembers each in turn puts the newest back at the head.
     * A slot past the end of the list is REMOVED rather than blanked. */
    for (index = 0; index < MP_SETTINGS_SERVERS_MAX; ++index) {
        char   key[16];
        size_t from = settings->servers - 1u - index;

        text_format(key, sizeof key, "Server%u", (unsigned)index);
        if (index < settings->servers &&
            mp_settings_server_text(settings, from, text, sizeof text)) {
            (void)ini_write_string(MULTIPLAYER_SECTION, key, text);
        } else {
            (void)ini_write_string(MULTIPLAYER_SECTION, key, NULL);
        }
    }

    (void)ini_write_string(MULTIPLAYER_SECTION, "SessionName", settings->session_name);
    /* The passwords live in memory for the life of the game and are asked for again after a
     * restart. The file lies in the game folder, and whoever reads it would read them in the
     * clear; a copy an older build wrote is removed here. */
    (void)ini_write_string(MULTIPLAYER_SECTION, "SessionPassword", NULL);
    (void)ini_write_string(MULTIPLAYER_SECTION, "JoinPassword", NULL);
    (void)ini_write_string(MULTIPLAYER_SECTION, "MenuMode", mode_word(settings->mode));
    (void)ini_write_int(MULTIPLAYER_SECTION, "Slots", (int32_t)settings->slots);
    (void)ini_write_int(MULTIPLAYER_SECTION, "Announce", settings->announce ? 1 : 0);
    (void)ini_write_string(MULTIPLAYER_SECTION, "MenuNet",
                           settings->net == MP_SETTINGS_NET_PUBLIC ? "public" : "lan");
    (void)ini_write_int(MULTIPLAYER_SECTION, "ListPublic", settings->list_public ? 1 : 0);

    (void)ini_write_int(MULTIPLAYER_SECTION, "ScoreLimit", (int32_t)settings->rules.score_limit);
    (void)ini_write_int(MULTIPLAYER_SECTION, "TimeLimitSeconds",
                        (int32_t)settings->rules.time_limit_s);
    (void)ini_write_int(MULTIPLAYER_SECTION, "Teams",
                        (settings->rules.flags & MP_RULES_F_TEAMS) != 0u ? 1 : 0);
    (void)ini_write_int(MULTIPLAYER_SECTION, "FriendlyFire",
                        (settings->rules.flags & MP_RULES_F_FRIENDLY_FIRE) != 0u ? 1 : 0);
    (void)ini_write_int(MULTIPLAYER_SECTION, "BetrayalPenalty",
                        (int32_t)settings->rules.team_penalty);
    (void)ini_write_int(MULTIPLAYER_SECTION, "SuicidePenalty",
                        (int32_t)settings->rules.suicide_penalty);
    (void)ini_write_int(MULTIPLAYER_SECTION, "RespawnTenths",
                        (int32_t)settings->rules.respawn_tenths);
}
