/* What the menu produces, driven with no game, no file and no screen.
 *
 * SIZE NOTE: over 600 lines. One module's checks, and the module is the whole of what the menu
 * decides: the address, the list, the host's side, the lobby's rows and the transport a choice
 * needs.
 *
 * The awkward parts are all here rather than in the screen: an address is malformed for most of
 * the time a player is typing it, a list has to forget rather than grow, and the same server
 * chosen twice is one server that moved to the front. What the screen does with the answer is
 * drawing; what counts as an answer is this.
 *
 * The address parser is written by hand, so the checks are written against the things the library
 * parsers get wrong: a leading sign, a space, a part above 255, a fifth part, and rubbish after
 * the last one.
 */
#include "unittest.h"

#include "mp_rules.h"
#include "mp_settings.h"

#include "common/text.h"

#include <string.h>

static void check_defaults(void)
{
    mp_settings_t s;

    ut_section("what a fresh installation means");
    mp_settings_default(&s);
    ut_check(s.role == MP_SETTINGS_ROLE_OFF, "off, so nothing happens to somebody who never asked");
    ut_check(strcmp(s.address, "127.0.0.1") == 0 && s.port == 27960u,
             "the loopback and the default port, which is what a first two window run wants");
    ut_check(s.servers == 0u, "and no remembered servers");
}

static void check_address(void)
{
    ut_section("what counts as an address");

    ut_check(mp_settings_valid_address("0.0.0.0"), "all zeroes is an address");
    ut_check(mp_settings_valid_address("255.255.255.255"), "and so is the broadcast");
    ut_check(mp_settings_valid_address("192.168.1.42"), "and an ordinary one");

    ut_check(!mp_settings_valid_address("192.168.1"), "three parts is not an address");
    ut_check(!mp_settings_valid_address("192.168.1.42.1"), "nor is five");
    ut_check(!mp_settings_valid_address("192.168.1."), "nor a trailing dot, which is what the "
             "field holds while the last part is being typed");
    ut_check(!mp_settings_valid_address("256.1.1.1"), "a part above 255 is not one");
    ut_check(!mp_settings_valid_address("0001.1.1.1"), "nor is one padded past three digits");
    ut_check(!mp_settings_valid_address("+1.2.3.4"), "a sign is not a digit, whatever atoi thinks");
    ut_check(!mp_settings_valid_address(" 1.2.3.4"), "and neither is a space");
    ut_check(!mp_settings_valid_address("1.2.3.4 "), "a trailing space is rubbish too");
    ut_check(!mp_settings_valid_address("1.2.3.4rubbish"), "and so is anything else after it");
    ut_check(!mp_settings_valid_address(""), "nothing is not an address");
    ut_check(!mp_settings_valid_address(NULL), "and neither is no string at all");
}

static void check_endpoint(void)
{
    char     address[MP_SETTINGS_ADDRESS_MAX];
    char     text[MP_SETTINGS_ENDPOINT_MAX];
    uint16_t port;

    ut_section("reading an address with or without its port");

    port = 27960u;
    ut_check(mp_settings_parse_endpoint("10.0.0.7", address, sizeof address, &port) &&
             strcmp(address, "10.0.0.7") == 0 && port == 27960u,
             "an address alone leaves the port the caller seeded");

    ut_check(mp_settings_parse_endpoint("10.0.0.7:1234", address, sizeof address, &port) &&
             strcmp(address, "10.0.0.7") == 0 && port == 1234u,
             "and one with a port takes it");

    ut_check(!mp_settings_parse_endpoint("10.0.0.7:0", address, sizeof address, &port),
             "port zero is refused: a socket reads it as any, a player as unset");
    ut_check(!mp_settings_parse_endpoint("10.0.0.7:65536", address, sizeof address, &port),
             "and one past the field it ends up in");
    ut_check(!mp_settings_parse_endpoint("10.0.0.7:", address, sizeof address, &port),
             "a colon with nothing after it is not a port");
    ut_check(port == 1234u && strcmp(address, "10.0.0.7") == 0,
             "and none of those refusals touched what was already there");

    ut_section("writing it back for the transport");

    ut_check(mp_settings_format_endpoint("192.168.1.42", 27960u, text, sizeof text) &&
             strcmp(text, "192.168.1.42:27960") == 0, "the two go back together with a colon");
    ut_check(mp_settings_format_endpoint("1.2.3.4", 1u, text, sizeof text) &&
             strcmp(text, "1.2.3.4:1") == 0, "a one digit port is not padded");
    ut_check(!mp_settings_format_endpoint("1.2.3.4", 0u, text, sizeof text),
             "a port of zero is not formatted, because it is not one");
    ut_check(!mp_settings_format_endpoint("1.2.3", 80u, text, sizeof text),
             "and neither is half an address");
    {
        char tiny[8];

        ut_check(!mp_settings_format_endpoint("255.255.255.255", 65535u, tiny, sizeof tiny),
                 "a buffer that cannot hold the answer gets nothing rather than a piece of it");
    }
}

static void check_list(void)
{
    mp_settings_t s;
    char          text[MP_SETTINGS_ENDPOINT_MAX];
    uint32_t      index;

    ut_section("the remembered servers");
    mp_settings_default(&s);

    ut_check(mp_settings_remember(&s, "1.1.1.1", 100u) && s.servers == 1u,
             "the first one is remembered");
    ut_check(mp_settings_remember(&s, "2.2.2.2", 200u) && s.servers == 2u, "and the second");
    ut_check(strcmp(s.server[0].address, "2.2.2.2") == 0,
             "the newest is at the head, which is what a player reaches for");

    ut_check(mp_settings_remember(&s, "1.1.1.1", 100u) && s.servers == 2u,
             "the same one again is not a second entry");
    ut_check(strcmp(s.server[0].address, "1.1.1.1") == 0 &&
             strcmp(s.server[1].address, "2.2.2.2") == 0,
             "it moved to the head instead");

    ut_check(mp_settings_remember(&s, "1.1.1.1", 999u) && s.servers == 3u,
             "the same address on another port IS another server");

    ut_check(!mp_settings_remember(&s, "1.2.3", 100u) && s.servers == 3u,
             "half an address is not remembered");
    ut_check(!mp_settings_remember(&s, "1.2.3.4", 0u) && s.servers == 3u,
             "and neither is a port of zero");

    ut_section("a full list forgets its oldest");

    mp_settings_default(&s);
    for (index = 0; index < MP_SETTINGS_SERVERS_MAX; ++index) {
        char address[MP_SETTINGS_ADDRESS_MAX];

        text_format(address, sizeof address, "10.0.0.%u", (unsigned)index);
        ut_check(mp_settings_remember(&s, address, 27960u), "every slot takes one");
    }
    ut_checkf(s.servers == MP_SETTINGS_SERVERS_MAX, "the list is full at %u",
              (unsigned)s.servers);
    ut_check(strcmp(s.server[MP_SETTINGS_SERVERS_MAX - 1u].address, "10.0.0.0") == 0,
             "and the first one entered is at the tail");

    ut_check(mp_settings_remember(&s, "8.8.8.8", 27960u) &&
             s.servers == MP_SETTINGS_SERVERS_MAX,
             "one more does not grow the list");
    ut_check(strcmp(s.server[0].address, "8.8.8.8") == 0,
             "the new one is at the head");
    for (index = 0; index < MP_SETTINGS_SERVERS_MAX; ++index) {
        ut_check(strcmp(s.server[index].address, "10.0.0.0") != 0,
                 "and the oldest is gone rather than shuffled somewhere");
    }

    ut_section("forgetting one closes the gap behind it");

    ut_check(mp_settings_forget(&s, 0u) && s.servers == MP_SETTINGS_SERVERS_MAX - 1u,
             "the head can go");
    ut_check(strcmp(s.server[0].address, "10.0.0.7") == 0, "and the next moves up into it");
    ut_check(!mp_settings_forget(&s, s.servers), "an index past the end names nothing");
    ut_check(!mp_settings_forget(&s, 99u), "and neither does a wild one");

    ut_section("a row for the list box");

    mp_settings_default(&s);
    (void)mp_settings_remember(&s, "192.168.1.42", 27960u);
    ut_check(mp_settings_server_text(&s, 0u, text, sizeof text) &&
             strcmp(text, "192.168.1.42:27960") == 0, "reads as one string");
    ut_check(!mp_settings_server_text(&s, 1u, text, sizeof text),
             "and an empty slot has no row rather than an empty one");
}

static void check_the_host_side(void)
{
    mp_settings_t settings;
    char          out[MP_SETTINGS_PASSWORD_MAX];

    ut_section("the host's side has defaults a player can host with at once");
    mp_settings_default(&settings);
    ut_check(settings.mode == MP_SETTINGS_MODE_COOP, "co-op by default");
    ut_check(settings.slots == MP_SETTINGS_SLOTS_DEFAULT, "four slots by default");
    ut_check(settings.session_name[0] != '\0', "a session name, so the announce is never blank");
    ut_check(settings.password[0] == '\0', "and no password, so the door starts open");
    ut_check(settings.announce, "announcing on the LAN, or the browser would never find it");

    ut_check(mp_settings_valid_mode(1) && mp_settings_valid_mode(2) && !mp_settings_valid_mode(3) &&
                 !mp_settings_valid_mode(0),
             "only the two modes are modes");

    ut_section("typed text is cleaned, and empty is allowed");
    mp_settings_clean_text("  jedi 1999  ", out, sizeof out);
    ut_check(strcmp(out, "jedi 1999") == 0, "blanks at both ends go, blanks inside stay");
    mp_settings_clean_text("a\001b\177c", out, sizeof out);
    ut_check(strcmp(out, "abc") == 0, "bytes with no glyph are dropped, not replaced");
    mp_settings_clean_text("", out, sizeof out);
    ut_check(out[0] == '\0', "an empty password is no password, and that is allowed");
    mp_settings_clean_text("0123456789ABCDEFGHIJKLMNOP", out, sizeof out);
    ut_check(strlen(out) == MP_SETTINGS_PASSWORD_MAX - 1u, "a long one is cut to its field");
}

/* The ceiling is not a transport limit. A co-op session holds four because a campaign level was
 * authored for one player, and a deathmatch holds four as well, because four is what one machine
 * can show: a fifth player would be in nobody else's world. The mode-blind clamp that used to
 * stand beside these is gone; it was the reason a co-op host could set sixteen. */
static void check_the_ceiling(void)
{
    ut_section("how many may sit in a session depends on the game");

    ut_check(mp_settings_slots_max(MP_SETTINGS_MODE_COOP) == MP_SETTINGS_SLOTS_COOP_MAX,
             "co-op is four");
    ut_check(mp_settings_slots_max(MP_SETTINGS_MODE_TDM) == 4u,
             "a deathmatch is four, the bodies one machine can show, and not the sixteen it wants");
    ut_check(MP_SETTINGS_SLOTS_MAX == 4u, "and four is what every buffer and every host caps at");
    ut_check(mp_settings_slots_max((mp_settings_mode_t)99) == MP_SETTINGS_SLOTS_COOP_MAX,
             "and a mode that is neither answers the smaller of the two, which is the safe one");

    ut_check(mp_settings_clamp_slots_for(16, MP_SETTINGS_MODE_COOP) == MP_SETTINGS_SLOTS_COOP_MAX,
             "sixteen in co-op comes back as four");
    ut_check(mp_settings_clamp_slots_for(16, MP_SETTINGS_MODE_TDM) == 4u,
             "and sixteen in a deathmatch comes back as four as well, so an old ini cannot seat a "
             "player nobody else sees");
    ut_check(mp_settings_clamp_slots_for(0, MP_SETTINGS_MODE_TDM) == MP_SETTINGS_SLOTS_MIN,
             "zero is not a session in either");
    ut_check(mp_settings_clamp_slots_for(-4, MP_SETTINGS_MODE_COOP) == MP_SETTINGS_SLOTS_MIN,
             "and neither is a negative, which is what an ini can hold");
    ut_check(mp_settings_clamp_slots_for(3, MP_SETTINGS_MODE_COOP) == 3u,
             "in between passes through");

    ut_section("the seat slider has one notch per seat count");
    ut_check(mp_settings_slot_notches(MP_SETTINGS_MODE_COOP) == 3u, "co-op: two, three, four");
    ut_check(mp_settings_slot_notches(MP_SETTINGS_MODE_TDM) == 3u,
             "a deathmatch: two, three, four");
}

/* Which controls a lobby has is a rule of its own, and it is checked here rather than seen on a
 * screen. Four is the most any side shows, and both hosts show four. */
static void check_the_lobby_rows(void)
{
    mp_settings_lobby_row_t row[MP_SETTINGS_LOBBY_ROWS_MAX];
    size_t                  count;

    ut_section("a co-op host chooses a map or a savegame, sets friendly fire, and starts");
    count = mp_settings_lobby_rows(true, MP_SETTINGS_MODE_COOP, row, MP_SETTINGS_LOBBY_ROWS_MAX);
    ut_check(count == 4u, "four rows, which is the most any side has");
    ut_check(row[0] == MP_SETTINGS_ROW_MAP && row[1] == MP_SETTINGS_ROW_SAVE &&
             row[2] == MP_SETTINGS_ROW_FRIENDLY_FIRE && row[3] == MP_SETTINGS_ROW_START,
             "map, savegame, friendly fire, start");

    ut_section("a deathmatch host chooses a map, a hero and a team, and starts");
    count = mp_settings_lobby_rows(true, MP_SETTINGS_MODE_TDM, row, MP_SETTINGS_LOBBY_ROWS_MAX);
    ut_check(count == 4u, "four rows, which is the most any side has");
    ut_check(row[0] == MP_SETTINGS_ROW_MAP && row[1] == MP_SETTINGS_ROW_HERO &&
             row[2] == MP_SETTINGS_ROW_TEAM && row[3] == MP_SETTINGS_ROW_START,
             "map, hero, team, start: no savegame, because there is no campaign to carry on");

    ut_section("a co-op client chooses a hero and says it is ready");
    count = mp_settings_lobby_rows(false, MP_SETTINGS_MODE_COOP, row, MP_SETTINGS_LOBBY_ROWS_MAX);
    ut_check(count == 2u && row[0] == MP_SETTINGS_ROW_HERO && row[1] == MP_SETTINGS_ROW_READY,
             "hero and ready, and no team: every co-op player is on none");
    ut_check(row[0] != MP_SETTINGS_ROW_FRIENDLY_FIRE && row[1] != MP_SETTINGS_ROW_FRIENDLY_FIRE,
             "and no friendly fire row: the host sets that for the session and a client reads it "
             "out of the note");

    ut_section("a deathmatch client chooses a hero and a team and says it is ready");
    count = mp_settings_lobby_rows(false, MP_SETTINGS_MODE_TDM, row, MP_SETTINGS_LOBBY_ROWS_MAX);
    ut_check(count == 3u && row[0] == MP_SETTINGS_ROW_HERO && row[1] == MP_SETTINGS_ROW_TEAM &&
             row[2] == MP_SETTINGS_ROW_READY, "hero, team, ready");

    ut_section("nobody has a team in co-op and nobody has a savegame in a deathmatch");
    (void)mp_settings_lobby_rows(false, MP_SETTINGS_MODE_COOP, row, MP_SETTINGS_LOBBY_ROWS_MAX);
    ut_check(row[0] != MP_SETTINGS_ROW_TEAM && row[1] != MP_SETTINGS_ROW_TEAM,
             "a co-op client is offered no team switch");
    (void)mp_settings_lobby_rows(true, MP_SETTINGS_MODE_COOP, row, MP_SETTINGS_LOBBY_ROWS_MAX);
    ut_check(row[0] != MP_SETTINGS_ROW_HERO && row[1] != MP_SETTINGS_ROW_HERO &&
             row[2] != MP_SETTINGS_ROW_HERO,
             "and a co-op host picks no hero: the level and the savegame both prescribe one");

    ut_section("a caller with less room than there are rows is told the truth");
    count = mp_settings_lobby_rows(true, MP_SETTINGS_MODE_TDM, row, 2u);
    ut_check(count == 4u, "the answer is what the side has, not what fitted");
    ut_check(mp_settings_lobby_rows(true, MP_SETTINGS_MODE_TDM, NULL, 0u) == 4u,
             "and asking with no room at all is how a caller sizes its own array");
}

/* Who picks a hero is one rule now, because two places ask: the lobby, to decide whether to
 * offer the row, and every level begin of a session, to put the pick on. While it was written out
 * twice the pick reached exactly the first level of a session, and the next world put the player
 * back on whatever it prescribes. The checks below hold the rule and the rows against each other,
 * which is the pairing that can go wrong. */
static void check_who_chooses_a_hero(void)
{
    mp_settings_lobby_row_t row[MP_SETTINGS_LOBBY_ROWS_MAX];
    size_t                  count;
    size_t                  index;
    int                     roles;
    int                     modes;

    ut_section("who picks a hero, as one rule");

    ut_check(!mp_settings_side_chooses_hero(true, MP_SETTINGS_MODE_COOP),
             "a co-op host plays what the level or its savegame prescribes");
    ut_check(mp_settings_side_chooses_hero(false, MP_SETTINGS_MODE_COOP),
             "a co-op client plays the hero it picked");
    ut_check(mp_settings_side_chooses_hero(true, MP_SETTINGS_MODE_TDM),
             "in a deathmatch no level prescribes anything, so the host picks too");
    ut_check(mp_settings_side_chooses_hero(false, MP_SETTINGS_MODE_TDM),
             "and so does its client");

    ut_section("and the lobby's rows say the same thing");
    for (roles = 0; roles < 2; ++roles) {
        for (modes = 0; modes < 2; ++modes) {
            bool               is_host = roles != 0;
            mp_settings_mode_t mode    = modes != 0 ? MP_SETTINGS_MODE_TDM
                                                    : MP_SETTINGS_MODE_COOP;
            bool               offered = false;

            count = mp_settings_lobby_rows(is_host, mode, row, MP_SETTINGS_LOBBY_ROWS_MAX);
            for (index = 0; index < count && index < MP_SETTINGS_LOBBY_ROWS_MAX; ++index) {
                offered = offered || row[index] == MP_SETTINGS_ROW_HERO;
            }
            ut_check(offered == mp_settings_side_chooses_hero(is_host, mode),
                     "the hero row is offered exactly to the side that picks one");
        }
    }
}

/* Built and offered are two different questions, and this check keeps them apart.
 * The wire still has to understand a deathmatch, because a note from an older build carries one
 * and has to be read rather than guessed at; the menus must not be able to produce one. Holding
 * both here is what stops the next change from taking the game out of the protocol as well. */
static void check_which_games_are_offered(void)
{
    ut_section("which games this build offers, and which it still understands");

    ut_check(mp_settings_mode_offered((int32_t)MP_SETTINGS_MODE_COOP), "co-op is offered");
    ut_check(!mp_settings_mode_offered((int32_t)MP_SETTINGS_MODE_TDM),
             "the deathmatch is not: no menu of this build can choose it");
    ut_check(mp_settings_valid_mode((int32_t)MP_SETTINGS_MODE_TDM),
             "and it is still a mode number, so a note carrying one is read and not guessed at");
    ut_check(!mp_settings_mode_offered(0) && !mp_settings_mode_offered(7) &&
                 !mp_settings_mode_offered(-1),
             "a number that is no game at all is offered by nobody");
}

static void check_the_lobby_mode(void)
{
    ut_section("a client shows the host's game, not its own last one");

    ut_check(mp_settings_lobby_mode(true, MP_SETTINGS_MODE_COOP, (uint8_t)MP_SETTINGS_MODE_TDM) ==
             MP_SETTINGS_MODE_COOP, "a host plays what it chose, whatever it once heard");
    ut_check(mp_settings_lobby_mode(false, MP_SETTINGS_MODE_COOP, (uint8_t)MP_SETTINGS_MODE_TDM) ==
             MP_SETTINGS_MODE_TDM, "a client plays what the host said");
    /* This case used to be asserted the other way round, and the assertion was the defect: a
     * client that had heard nothing fell back to `own`, which is the last game this installation
     * chose while HOSTING and survives in the ini. A player joined a co-operative host by typing
     * the address, and the lobby showed a deathmatch with team rows because that machine had once
     * hosted one. Neither the field nor the test was wrong about what it held; both were wrong
     * about whose session it described. */
    ut_check(mp_settings_lobby_mode(false, MP_SETTINGS_MODE_TDM, 0u) == MP_SETTINGS_MODE_COOP,
             "a client never falls back to its own hosting choice, which is another session's");
    ut_check(mp_settings_lobby_mode(false, MP_SETTINGS_MODE_COOP, 7u) == MP_SETTINGS_MODE_COOP,
             "a mode number that is neither is not a mode, so it is ignored");
    ut_check(mp_settings_lobby_mode(false, (mp_settings_mode_t)0, 0u) == MP_SETTINGS_MODE_COOP,
             "and with nothing at all it is co-op, which is the game with no free-for-all in it");
}

static void check_the_effective_mode(void)
{
    mp_settings_t settings;

    ut_section("one answer to which game is being played");

    mp_settings_default(&settings);
    settings.role      = MP_SETTINGS_ROLE_HOST;
    settings.mode      = MP_SETTINGS_MODE_TDM;
    settings.join_mode = (uint8_t)MP_SETTINGS_MODE_COOP;
    ut_check(mp_settings_effective_mode(&settings) == MP_SETTINGS_MODE_TDM,
             "a host reads its own choice and ignores whatever it last joined");

    settings.role = MP_SETTINGS_ROLE_JOIN;
    ut_check(mp_settings_effective_mode(&settings) == MP_SETTINGS_MODE_COOP,
             "a client reads what the host announced");

    /* The field the browser fills in, left at zero because the address was typed by hand. A field
     * run had this shape: its report named a deathmatch and the session was a campaign. This
     * answer decides only what the lobby shows and which local switches turn. The join request
     * does not carry it: it names the mode that was heard, and for a typed address that is
     * none, which the host reads as no preference. */
    settings.join_mode = 0u;
    ut_check(mp_settings_effective_mode(&settings) == MP_SETTINGS_MODE_COOP,
             "and with nothing announced it is co-op, not the deathmatch left in the ini");
}

static void check_who_is_waiting(void)
{
    static const uint8_t ALL_READY[] = { 1u, 1u, 1u };
    static const uint8_t TWO_LEFT[]  = { 1u, 0u, 1u, 0u };

    ut_section("the start waits for everybody");

    ut_check(mp_settings_lobby_waiting(ALL_READY, 3u) == 0u, "nobody outstanding lets it begin");
    ut_check(mp_settings_lobby_waiting(TWO_LEFT, 4u) == 2u, "two who have not said so hold it");
    ut_check(mp_settings_lobby_waiting(TWO_LEFT, 1u) == 0u,
             "the count is the caller's: a shorter roster is not read past its end");
    ut_check(mp_settings_lobby_waiting(NULL, 4u) == 0u,
             "and no roster at all is not a room full of people who are not ready");
}

static void check_the_port(void)
{
    uint16_t port = 27960u;

    ut_section("a port typed on its own");

    ut_check(mp_settings_parse_port("1", &port) && port == 1u, "one is a port");
    ut_check(mp_settings_parse_port("65535", &port) && port == 65535u, "and so is the last one");
    ut_check(!mp_settings_parse_port("0", &port),
             "zero means any to a socket and unset to a player");
    ut_check(!mp_settings_parse_port("65536", &port), "and one past the field it ends up in");
    ut_check(!mp_settings_parse_port("123456", &port), "six digits is not a port either");
    ut_check(!mp_settings_parse_port("27960 ", &port), "a trailing space is rubbish");
    ut_check(!mp_settings_parse_port(" 27960", &port), "and so is a leading one");
    ut_check(!mp_settings_parse_port("+27960", &port),
             "a sign is not a digit, whatever atoi thinks");
    ut_check(!mp_settings_parse_port("279a60", &port), "nor is a letter in the middle");
    ut_check(!mp_settings_parse_port("", &port), "nothing is not a port");
    ut_check(port == 65535u, "and none of those refusals touched what was already there");
}

/* The rule set is part of the intention, so a fresh installation has to arrive with a playable
 * one rather than with seven zeros. */
static void check_the_rule_defaults(void)
{
    mp_settings_t settings;

    ut_section("a fresh installation proposes a round somebody can play");

    mp_settings_default(&settings);
    ut_check(mp_rules_valid(&settings.rules), "the rules are inside every range this build takes");
    ut_check(settings.rules.score_limit == MP_RULES_SCORE_LIMIT_DEFAULT &&
                 settings.rules.time_limit_s == MP_RULES_TIME_LIMIT_DEFAULT,
             "with the points and the time the rule set says are the defaults");
    ut_check(mp_rules_teams(&settings.rules) && !mp_rules_friendly_fire(&settings.rules),
             "teams on and friendly fire off");
}

/* The parser the ini half reads its numbers through, and the reason it exists.
 *
 * The profile API's own integer read cannot tell a key that is absent from a key whose value is
 * empty: the first answers the default and the second answers zero. Zero is a legal setting for
 * four of the seven rules and means "no limit", so a key somebody blanked out by hand would
 * quietly turn a limit off. Every case below that leaves the value alone is a setting that
 * survives a hand-edited file.
 */
static void check_the_number_parser(void)
{
    int32_t value = 4711;

    ut_section("a whole number written out in full, and nothing else");

    ut_check(mp_settings_parse_int("0", &value) && value == 0, "zero is a number");
    ut_check(mp_settings_parse_int("25", &value) && value == 25, "and so is a plain one");
    ut_check(mp_settings_parse_int("-1", &value) && value == -1, "a minus makes it negative");
    ut_check(mp_settings_parse_int("2147483647", &value) && value == 2147483647,
             "the largest one fits");
    ut_check(mp_settings_parse_int("-2147483647", &value) && value == -2147483647,
             "and so does its opposite");

    value = 4711;
    ut_check(!mp_settings_parse_int("", &value),
             "an EMPTY value is not a number, which is the whole reason this is not the profile "
             "API's integer read: that one answers zero, and zero means no limit");
    ut_check(!mp_settings_parse_int("-", &value), "a sign on its own is not one either");
    ut_check(!mp_settings_parse_int(" 25", &value), "a leading space is rubbish");
    ut_check(!mp_settings_parse_int("25 ", &value), "and so is a trailing one");
    ut_check(!mp_settings_parse_int("+25", &value), "a plus is not a digit, whatever atoi thinks");
    ut_check(!mp_settings_parse_int("2a5", &value), "nor is a letter in the middle");
    ut_check(!mp_settings_parse_int("2147483648", &value), "one past the field it ends up in");
    ut_check(!mp_settings_parse_int("99999999999999", &value), "and a number of no useful size");
    ut_check(!mp_settings_parse_int(NULL, &value), "nothing is not a number");
    ut_check(value == 4711, "and not one of those refusals touched what was already there");
}

/* The transport a run stood on, as the bridge would describe it. */
static mp_settings_armed_t armed_as(uint32_t role, mp_settings_net_t net, uint16_t port,
                                    uint8_t seats, const char *code)
{
    mp_settings_armed_t armed;

    memset(&armed, 0, sizeof armed);
    armed.role       = role;
    armed.net        = net;
    armed.bound_port = port;
    armed.seats      = seats;
    strncpy(armed.code, code, sizeof armed.code - 1u);
    return armed;
}

/* When the menu's choice needs the transport put up again: the other side, the other network, a
 * host on another port or with other seats, a public player in another session. None of them
 * waits for a restart. */
static void check_a_new_transport(void)
{
    const uint32_t      host = (uint32_t)MP_SETTINGS_ROLE_HOST;
    const uint32_t      join = (uint32_t)MP_SETTINGS_ROLE_JOIN;
    mp_settings_t       settings;
    mp_settings_armed_t armed;

    ut_section("the other side or another port puts the transport up again, and nothing else");
    mp_settings_default(&settings);
    settings.role = MP_SETTINGS_ROLE_HOST;
    settings.port = 27015u;
    armed = armed_as(0u, MP_SETTINGS_NET_LAN, 0u, 0u, "");
    ut_check(!mp_settings_needs_new_transport(&armed, &settings),
             "nothing armed by the menu: the first install, or the ini's, which the caller "
             "refuses");
    armed = armed_as(host, MP_SETTINGS_NET_LAN, 27015u, 0u, "");
    ut_check(!mp_settings_needs_new_transport(&armed, &settings),
             "a host again on its own port keeps its transport");
    armed.bound_port = 27016u;
    ut_check(mp_settings_needs_new_transport(&armed, &settings),
             "a host on another port puts it up again, rather than listen on the old one");
    armed = armed_as(join, MP_SETTINGS_NET_LAN, 0u, 0u, "");
    ut_check(mp_settings_needs_new_transport(&armed, &settings),
             "a client that now hosts puts it up again");
    settings.role = MP_SETTINGS_ROLE_JOIN;
    armed = armed_as(host, MP_SETTINGS_NET_LAN, 27015u, 0u, "");
    ut_check(mp_settings_needs_new_transport(&armed, &settings),
             "and so does a host that now joins");
    settings.port = 30000u;
    armed = armed_as(join, MP_SETTINGS_NET_LAN, 0u, 0u, "");
    ut_check(!mp_settings_needs_new_transport(&armed, &settings),
             "a client that joins another address keeps its transport: the lobby connects anew");
    ut_check(!mp_settings_needs_new_transport(&armed, NULL) &&
                 !mp_settings_needs_new_transport(NULL, &settings),
             "and no settings, or nothing armed, ask for nothing");

    ut_section("the network, the seats and the session");
    settings.net = MP_SETTINGS_NET_PUBLIC;
    memcpy(settings.join_code, "ABCD-EFGH", 10u);
    ut_check(mp_settings_needs_new_transport(&armed, &settings),
             "a LAN client that now joins in public puts the transport up again");
    armed = armed_as(join, MP_SETTINGS_NET_PUBLIC, 0u, 0u, "ABCD-EFGH");
    ut_check(!mp_settings_needs_new_transport(&armed, &settings),
             "a public player in the same session keeps it");
    memcpy(settings.join_code, "ABCD-EFGJ", 10u);
    ut_check(mp_settings_needs_new_transport(&armed, &settings),
             "one in another session does not: its transport joined the first");
    settings.role  = MP_SETTINGS_ROLE_HOST;
    settings.mode  = MP_SETTINGS_MODE_COOP;
    settings.slots = 4u;
    armed = armed_as(host, MP_SETTINGS_NET_PUBLIC, 0u, 3u, "");
    ut_check(!mp_settings_needs_new_transport(&armed, &settings),
             "a public host with the same seats keeps it, whatever the LAN's port field holds");
    armed.seats = 1u;
    ut_check(mp_settings_needs_new_transport(&armed, &settings),
             "with other seats it registers again");
    settings.net = MP_SETTINGS_NET_LAN;
    armed.seats  = 3u;
    ut_check(mp_settings_needs_new_transport(&armed, &settings),
             "and a public host that now hosts on the LAN puts it up again");
}

/* The one decision every install goes through: what carries the session. */
static void check_the_lan_never_reaches_the_relay(void)
{
    mp_settings_t s;
    int32_t       net;
    bool          always_lan = true;

    ut_section("the LAN never reaches the relay");
    mp_settings_default(&s);
    ut_check(s.net == MP_SETTINGS_NET_LAN && !s.list_public,
             "a fresh installation is on the LAN, and a public host would be unlisted");
    ut_check(mp_settings_transport_for(&s) == MP_SETTINGS_TRANSPORT_NONE &&
                 mp_settings_transport_for(NULL) == MP_SETTINGS_TRANSPORT_NONE,
             "with no side chosen nothing carries anything");
    s.role = MP_SETTINGS_ROLE_HOST;
    ut_check(mp_settings_transport_for(&s) == MP_SETTINGS_TRANSPORT_LAN,
             "a LAN host is direct UDP");
    s.role = MP_SETTINGS_ROLE_JOIN;
    ut_check(mp_settings_transport_for(&s) == MP_SETTINGS_TRANSPORT_LAN,
             "and so is a LAN client");
    for (net = -3; net < 9; ++net) {
        if (net == (int32_t)MP_SETTINGS_NET_PUBLIC) {
            continue;
        }
        s.net  = (mp_settings_net_t)net;
        s.role = MP_SETTINGS_ROLE_HOST;
        always_lan = always_lan && mp_settings_transport_for(&s) == MP_SETTINGS_TRANSPORT_LAN;
        s.role = MP_SETTINGS_ROLE_JOIN;
        always_lan = always_lan && mp_settings_transport_for(&s) == MP_SETTINGS_TRANSPORT_LAN;
    }
    ut_check(always_lan,
             "every value of the field but PUBLIC is the LAN, for both sides, so nothing a file "
             "leaves there puts a LAN session on the relay");
    ut_check(!mp_settings_is_public(&s) && !mp_settings_is_public(NULL),
             "and the screens ask the same question: not public");
    s.net = MP_SETTINGS_NET_PUBLIC;
    ut_check(mp_settings_is_public(&s), "public is public");
    ut_check(mp_settings_transport_for(&s) == MP_SETTINGS_TRANSPORT_RELAY,
             "a public player goes through the relay");
    s.role = MP_SETTINGS_ROLE_HOST;
    ut_check(mp_settings_transport_for(&s) == MP_SETTINGS_TRANSPORT_RELAY,
             "and so does a public host");
    s.role = MP_SETTINGS_ROLE_OFF;
    ut_check(mp_settings_transport_for(&s) == MP_SETTINGS_TRANSPORT_NONE,
             "public with no side chosen carries nothing either");

    s.mode  = MP_SETTINGS_MODE_COOP;
    s.slots = 4u;
    ut_check(mp_settings_relay_seats(&s) == 3u, "four slots ask the relay for three seats");
    s.slots = 2u;
    ut_check(mp_settings_relay_seats(&s) == 1u, "two for one");
    s.slots = 9u;
    ut_check(mp_settings_relay_seats(&s) == 3u && mp_settings_relay_seats(NULL) == 1u,
             "more slots than the game holds ask for what it holds, and nothing asks for one");
}

int main(void)
{
    check_defaults();
    check_address();
    check_endpoint();
    check_list();
    check_the_host_side();
    check_the_ceiling();
    check_the_lobby_rows();
    check_who_chooses_a_hero();
    check_which_games_are_offered();
    check_the_lobby_mode();
    check_the_effective_mode();
    check_who_is_waiting();
    check_the_port();
    check_the_rule_defaults();
    check_the_number_parser();
    check_a_new_transport();
    check_the_lan_never_reaches_the_relay();

    return ut_summary("mp_settings");
}
