/* The lobby's band when a join is refused for a required mod, the game data or a DLL outside this
 * release, against the real modules.
 *
 * Two sessions meet over the loopback with the host's own judge, the bridge's, and the client
 * states another build of the multiplayer, no multiplayer, other game data or DLLs outside this
 * release, or the host states none. Then the band is written as the lobby's frame writes it and
 * read back, and the rows the player list shows are asked of the same function the list asks.
 * Without a word of its own, a refusal for a mod or a DLL would leave the band saying the join is
 * still under way, and one for the game data would say only that the data differ, with no file and
 * no builds. A characters.ini one side does not have, whose fingerprint is 0, reads as missing on
 * that side and not as other data; a damage table at 0 is one that was not read and stays other
 * data.
 *
 * Nothing here reads the ini, so the host's [multiplayer] AllowMods is empty and every DLL a client
 * names is refused. The census of a test process holds no DLL outside this release, so this side's
 * row for one is held with records made by hand. A connected client's band is checked to stay its
 * ordinary line.
 *
 * SIZE NOTE: over 600 lines. Every refusal here goes through the host's real judge over one
 * loopback lobby and is read back the way the lobby reads it, so the sessions, the statements and
 * the lobby they share are written once. The seam is the records made by hand, which ask
 * mp_screens_refusal_lines alone and need no session: a program of their own would take them
 * whole, for one more build of the bridge's sources.
 */
#include "unittest.h"

#include "mp_bridge_content.h"
#include "mp_bridge_drain.h"
#include "mp_bridge_lobby.h"
#include "mp_lobby.h"
#include "mp_loopback.h"
#include "mp_menu_screens_int.h"
#include "mp_mod_allow.h"
#include "mp_mod_census.h"
#include "mp_mod_manifest_rule.h"
#include "mp_session.h"
#include "mp_text.h"
#include "mp_transport.h"
#include "mp_wire.h"

#include "common/text.h"

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <string.h>

/* Static: a session is over two megabytes now that every peer carries a bulk ring. */
static mp_loopback_t     s_net;
static mp_session_t      s_host;
static mp_session_t      s_client;
static mp_transport_t    s_host_t;
static mp_transport_t    s_client_t;
static mp_bridge_drain_t s_drain;

/* The host's statement: this process's own build of the multiplayer, out of the census, and two
 * data fingerprints. */
static mp_mod_manifest_t s_host_statement;

/* The DLLs outside this release a statement names, `listed` of them, and how many it has in all. */
typedef struct outside {
    const char *const *names;
    size_t             listed;
    size_t             count;
} outside_t;

static const outside_t NONE_OUTSIDE = { NULL, 0u, 0u };

/* A statement as a build like this one sends it: the mods, and behind them the list of DLLs
 * outside this release, judged. `outside` NULL leaves the list off, which the host refuses. */
static void state(mp_session_t *session, const mp_mod_manifest_t *manifest,
                  const outside_t *outside)
{
    uint8_t bytes[MP_MOD_STATEMENT_MAX_BYTES];
    size_t  length = mp_mod_manifest_encode(manifest, bytes, sizeof bytes);

    if (length != 0u && outside != NULL) {
        length += mp_mod_foreign_encode(outside->names, outside->listed, outside->count, true,
                                        bytes + length, sizeof bytes - length, NULL);
    }
    ut_check(length != 0u && mp_session_set_statement(session, bytes, length),
             "the statement is encoded and taken");
}

/* A client's lobby screen, open, over a fresh wire, joining a host that judges its statement,
 * with `client_outside` behind the client's mods. Answers whether the host refused it. */
static bool a_judged_lobby(uint32_t seed, const mp_mod_manifest_t *host_says,
                           const mp_mod_manifest_t *client_says, const outside_t *client_outside)
{
    uint32_t now = 0u;
    int      tick;

    mp_loopback_init(&s_net, NULL, seed);
    s_host_t   = mp_loopback_transport(&s_net, MP_LOOPBACK_ENDPOINT_A);
    s_client_t = mp_loopback_transport(&s_net, MP_LOOPBACK_ENDPOINT_B);
    mp_session_init(&s_host, MP_SESSION_HOST, &s_host_t, seed + 1u);
    mp_session_init(&s_client, MP_SESSION_CLIENT, &s_client_t, seed + 2u);
    state(&s_host, host_says, &NONE_OUTSIDE);
    mp_session_set_judge(&s_host, mp_bridge_content_judge);
    state(&s_client, client_says, client_outside);
    mp_bridge_drain_bind(&s_drain, MP_BRIDGE_UDP_CLIENT, &s_host, &s_client);
    mp_bridge_lobby_bind(&s_host, &s_client, NULL, true, &s_drain);
    mp_bridge_lobby_reset();
    mp_bridge_set_lobby(MP_LOBBY_TEAM_NONE, false);
    mp_bridge_lobby_set_open(true);
    mps.lobby_is_host  = false;
    mps.lobby_error[0] = '\0';
    mp_session_connect(&s_client, MP_LOOPBACK_ENDPOINT_A);
    for (tick = 0; tick < 400; ++tick) {
        now += 16u;
        mp_loopback_pump(&s_net, now);
        mp_session_update(&s_host, now);
        mp_session_update(&s_client, now);
        if (mp_session_is_connected(&s_client) ||
            mp_session_last_deny(&s_client) != MP_DENY_NONE) {
            break;
        }
    }
    return mp_session_last_deny(&s_client) != MP_DENY_NONE;
}

static void close_lobby(void)
{
    mp_bridge_lobby_set_open(false);
    mp_session_disconnect(&s_client);
}

/* What the band and the two rows must read for a refusal: `format` with the file's name in it,
 * and each side's value. */
static void check_what_it_says(const char *when, mp_text_id_t format, const char *file,
                               const char *host_value, const char *here_value)
{
    mp_mod_refusal_t refusal;
    char             want[128];
    char             host_row[MP_SCREEN_ROW_TEXT_MAX];
    char             here_row[MP_SCREEN_ROW_TEXT_MAX];
    char             note[256];

    mp_screens_refresh_band();
    text_format(want, sizeof want, mp_text(format), file);
    text_format(note, sizeof note, "%s: the band reads '%s' and should read '%s'", when,
                mps.lobby_band, want);
    ut_check(strcmp(mps.lobby_band, want) == 0, note);

    ut_check(mp_bridge_content_refusal(&refusal) &&
                 mp_screens_refusal_lines(&refusal, NULL, 0u, host_row, here_row,
                                          sizeof host_row),
             "the player list's rows come out of the same refusal");
    text_format(want, sizeof want, mp_text(MP_TEXT_REFUSED_HOST_ROW), host_value);
    text_format(note, sizeof note, "%s: the host's row reads '%s' and should read '%s'", when,
                host_row, want);
    ut_check(strcmp(host_row, want) == 0, note);
    text_format(want, sizeof want, mp_text(MP_TEXT_REFUSED_HERE_ROW), here_value);
    text_format(note, sizeof note, "%s: this side's row reads '%s' and should read '%s'", when,
                here_row, want);
    ut_check(strcmp(here_row, want) == 0, note);
}

/* A build as the rows show it: the local date its stamp names, the words the log uses, and no
 * release number, which a build of this tree does not carry. */
static void built(uint32_t stamp, char *out, size_t size)
{
    mp_mod_census_describe_stamp(stamp, out, size);
}

static void check_another_build(void)
{
    mp_mod_manifest_t client = s_host_statement;
    char              host_built[32];
    char              here_built[32];
    uint32_t          here_stamp = 0u;
    uint32_t          here_image = 0u;
    char              here_version[MP_MOD_REFUSAL_VERSION_MAX];

    ut_section("another build of the multiplayer: the band names it, the rows show both builds");
    client.mods[0].stamp ^= 0x00010000u;
    ut_check(a_judged_lobby(0x9100u, &s_host_statement, &client, &NONE_OUTSIDE) &&
                 mp_session_last_deny(&s_client) == MP_DENY_MODS,
             "the host refuses the join for a mod, reason 7");
    (void)mp_mod_census_build_of(MP_WIRE_MOD_MULTIPLAYER, &here_stamp, &here_image, here_version,
                                 sizeof here_version);
    built(s_host_statement.mods[0].stamp, host_built, sizeof host_built);
    built(here_stamp, here_built, sizeof here_built);
    check_what_it_says("another build", MP_TEXT_REFUSED_OTHER, "multiplayer.dll", host_built,
                       here_built);
    close_lobby();
}

static void check_missing_here(void)
{
    mp_mod_manifest_t client = s_host_statement;
    char              host_built[32];

    ut_section("no multiplayer on this side: the band says it is missing here");
    client.count = 0u;
    ut_check(a_judged_lobby(0x9200u, &s_host_statement, &client, &NONE_OUTSIDE) &&
                 mp_session_last_deny(&s_client) == MP_DENY_MODS,
             "the host refuses the join for a mod");
    built(s_host_statement.mods[0].stamp, host_built, sizeof host_built);
    check_what_it_says("missing here", MP_TEXT_REFUSED_MISSING_HERE, "multiplayer.dll",
                       host_built, mp_text(MP_TEXT_REFUSED_MISSING));
    close_lobby();
}

static void check_missing_at_the_host(void)
{
    mp_mod_manifest_t host = s_host_statement;
    char              here_built[32];
    uint32_t          here_stamp = 0u;
    uint32_t          here_image = 0u;
    char              here_version[MP_MOD_REFUSAL_VERSION_MAX];

    ut_section("no multiplayer at the host: the band says the host does not have it");
    host.count = 0u;
    ut_check(a_judged_lobby(0x9300u, &host, &s_host_statement, &NONE_OUTSIDE) &&
                 mp_session_last_deny(&s_client) == MP_DENY_MODS,
             "the host refuses the join for a mod it lacks");
    (void)mp_mod_census_build_of(MP_WIRE_MOD_MULTIPLAYER, &here_stamp, &here_image, here_version,
                                 sizeof here_version);
    built(here_stamp, here_built, sizeof here_built);
    check_what_it_says("missing at the host", MP_TEXT_REFUSED_MISSING_AT_HOST, "multiplayer.dll",
                       mp_text(MP_TEXT_REFUSED_MISSING), here_built);
    close_lobby();
}

/* The band and the host's row alone, for a refusal whose row for this side a test process cannot
 * show: that value is out of the statement this side made, and a test process makes none. */
static void check_band_and_host_row(const char *when, mp_text_id_t format, const char *file,
                                    const char *host_value)
{
    mp_mod_refusal_t refusal;
    char             want[128];
    char             host_row[MP_SCREEN_ROW_TEXT_MAX];
    char             here_row[MP_SCREEN_ROW_TEXT_MAX];

    mp_screens_refresh_band();
    text_format(want, sizeof want, mp_text(format), file);
    ut_checkf(strcmp(mps.lobby_band, want) == 0, "%s: the band reads '%s' and should read '%s'",
              when, mps.lobby_band, want);
    ut_check(mp_bridge_content_refusal(&refusal) &&
                 mp_screens_refusal_lines(&refusal, NULL, 0u, host_row, here_row,
                                          sizeof host_row),
             "the player list's rows come out of the same refusal");
    text_format(want, sizeof want, mp_text(MP_TEXT_REFUSED_HOST_ROW), host_value);
    ut_checkf(strcmp(host_row, want) == 0, "%s: the host's row reads '%s' and should read '%s'",
              when, host_row, want);
}

/* This side's value for a data file is out of the statement this side made (mp_bridge_statement),
 * which exists in a game whenever a join was judged and never in a test process, where it reads 0.
 * So these real refusals are held where they do not depend on it, and the records made by hand
 * below hold the rest. */
static void check_other_game_data(void)
{
    mp_mod_manifest_t client = s_host_statement;
    mp_mod_manifest_t host   = s_host_statement;
    mp_mod_refusal_t  refusal;
    char              host_value[16];
    char              host_row[MP_SCREEN_ROW_TEXT_MAX];
    char              here_row[MP_SCREEN_ROW_TEXT_MAX];
    char              want[MP_SCREEN_ROW_TEXT_MAX];

    ut_section("other game data: the refusal names the file and the host's fingerprint");
    client.roster ^= 0x5Au;
    ut_check(a_judged_lobby(0x9400u, &s_host_statement, &client, &NONE_OUTSIDE) &&
                 mp_session_last_deny(&s_client) == MP_DENY_CONTENT,
             "the host refuses the join for the game data, reason 3");
    ut_check(mp_bridge_content_refusal(&refusal) && refusal.sub == MP_MOD_SUB_ROSTER &&
                 strcmp(refusal.mod, "characters.ini") == 0,
             "and the refusal names characters.ini");
    text_format(host_value, sizeof host_value, "%08X", (unsigned)s_host_statement.roster);
    text_format(want, sizeof want, mp_text(MP_TEXT_REFUSED_HOST_ROW), host_value);
    ut_checkf(mp_screens_refusal_lines(&refusal, NULL, 0u, host_row, here_row, sizeof host_row) &&
                  strcmp(host_row, want) == 0,
              "the host's row reads '%s' and should read '%s'; the band and this side's row "
              "are held by hand below, since this side's value needs a statement", host_row, want);
    close_lobby();

    ut_section("no characters.ini on this side: the band says it is missing here");
    client        = s_host_statement;
    client.roster = 0u;
    ut_check(a_judged_lobby(0x9410u, &s_host_statement, &client, &NONE_OUTSIDE) &&
                 mp_session_last_deny(&s_client) == MP_DENY_CONTENT,
             "the host refuses a roster fingerprint 0 as other game data");
    check_what_it_says("characters.ini missing here", MP_TEXT_REFUSED_MISSING_HERE,
                       "characters.ini", host_value, mp_text(MP_TEXT_REFUSED_MISSING));
    close_lobby();

    ut_section("no characters.ini at the host: the band says the host does not have it");
    host.roster = 0u;
    ut_check(a_judged_lobby(0x9420u, &host, &s_host_statement, &NONE_OUTSIDE) &&
                 mp_session_last_deny(&s_client) == MP_DENY_CONTENT,
             "the host without the file refuses a joiner with it");
    check_band_and_host_row("characters.ini missing at the host",
                            MP_TEXT_REFUSED_MISSING_AT_HOST, "characters.ini",
                            mp_text(MP_TEXT_REFUSED_MISSING));
    close_lobby();

    /* A damage table at 0 is a shot table that was not read, not a missing damage.txt, so it
     * reads as other data with its fingerprint. */
    ut_section("no damage table on this side: the band says damage.txt differs, with hex");
    client        = s_host_statement;
    client.damage = 0u;
    ut_check(a_judged_lobby(0x9430u, &s_host_statement, &client, &NONE_OUTSIDE) &&
                 mp_session_last_deny(&s_client) == MP_DENY_CONTENT,
             "the host refuses a damage fingerprint 0 as other game data");
    text_format(host_value, sizeof host_value, "%08X", (unsigned)s_host_statement.damage);
    check_what_it_says("damage table 0 here", MP_TEXT_REFUSED_OTHER, "damage.txt", host_value,
                       "00000000");
    close_lobby();
}

/* One data refusal made by hand: what the band says, and the two rows. */
static void check_a_data_record(const char *file, uint8_t sub, uint32_t host_stamp,
                                uint32_t here_stamp, mp_text_id_t format, const char *host_value,
                                const char *here_value)
{
    mp_mod_refusal_t refusal;
    char             band[CAPTION_MAX];
    char             host_row[MP_SCREEN_ROW_TEXT_MAX];
    char             here_row[MP_SCREEN_ROW_TEXT_MAX];
    char             want[MP_SCREEN_ROW_TEXT_MAX];

    memset(&refusal, 0, sizeof refusal);
    refusal.reason     = MP_MOD_REFUSE_GAME_DATA;
    refusal.sub        = sub;
    refusal.host_stamp = host_stamp;
    refusal.here_stamp = here_stamp;
    memcpy(refusal.mod, file, strlen(file) + 1u);
    ut_checkf(mp_screens_refusal_lines(&refusal, band, sizeof band, host_row, here_row,
                                       sizeof host_row),
              "%s, host %08X, here %08X: the refusal is named", file, (unsigned)host_stamp,
              (unsigned)here_stamp);
    text_format(want, sizeof want, mp_text(format), file);
    ut_checkf(strcmp(band, want) == 0, "the band reads '%s' and should read '%s'", band, want);
    text_format(want, sizeof want, mp_text(MP_TEXT_REFUSED_HOST_ROW), host_value);
    ut_checkf(strcmp(host_row, want) == 0, "the host's row reads '%s' and should read '%s'",
              host_row, want);
    text_format(want, sizeof want, mp_text(MP_TEXT_REFUSED_HERE_ROW), here_value);
    ut_checkf(strcmp(here_row, want) == 0, "this side's row reads '%s' and should read '%s'",
              here_row, want);
}

/* A characters.ini a side does not have, made by hand with both values a game has;
 * a damage table at 0 is no missing file and stays other data with its hex. */
static void check_a_missing_data_file(void)
{
    ut_section("a characters.ini one side does not have, with both values a game has");
    check_a_data_record("characters.ini", MP_MOD_SUB_ROSTER, 0xE1D7140Du, 0u,
                        MP_TEXT_REFUSED_MISSING_HERE, "E1D7140D", mp_text(MP_TEXT_REFUSED_MISSING));
    check_a_data_record("characters.ini", MP_MOD_SUB_ROSTER, 0u, 0xE1D7140Du,
                        MP_TEXT_REFUSED_MISSING_AT_HOST, mp_text(MP_TEXT_REFUSED_MISSING),
                        "E1D7140D");
    check_a_data_record("damage.txt", MP_MOD_SUB_DAMAGE, 0x84273DBBu, 0u,
                        MP_TEXT_REFUSED_OTHER, "84273DBB", "00000000");
    check_a_data_record("damage.txt", MP_MOD_SUB_DAMAGE, 0u, 0x84273DBBu,
                        MP_TEXT_REFUSED_OTHER, "00000000", "84273DBB");
    check_a_data_record("characters.ini", MP_MOD_SUB_ROSTER, 0xE1D7140Du, 0x1234ABCDu,
                        MP_TEXT_REFUSED_OTHER, "E1D7140D", "1234ABCD");
}

/* Two records made by hand show the values a game has: two fingerprints of damage.txt, and a
 * released mod with its release number and the date it was built. */
static void check_the_values_a_game_has(void)
{
    mp_mod_refusal_t refusal;
    char             band[CAPTION_MAX];
    char             host_row[MP_SCREEN_ROW_TEXT_MAX];
    char             here_row[MP_SCREEN_ROW_TEXT_MAX];
    char             built[32];
    char             want[MP_SCREEN_ROW_TEXT_MAX];

    ut_section("the values a game has: two fingerprints, and a mod's release and build");
    memset(&refusal, 0, sizeof refusal);
    refusal.reason     = MP_MOD_REFUSE_GAME_DATA;
    refusal.sub        = MP_MOD_SUB_DAMAGE;
    refusal.host_stamp = 0x84273DBBu;
    refusal.here_stamp = 0x1234ABCDu;
    memcpy(refusal.mod, "damage.txt", 11u);
    ut_check(mp_screens_refusal_lines(&refusal, band, sizeof band, host_row, here_row,
                                      sizeof host_row),
             "a data file's refusal is named");
    text_format(want, sizeof want, mp_text(MP_TEXT_REFUSED_OTHER), "damage.txt");
    ut_check(strcmp(band, want) == 0, "the band names damage.txt");
    text_format(want, sizeof want, mp_text(MP_TEXT_REFUSED_HOST_ROW), "84273DBB");
    ut_checkf(strcmp(host_row, want) == 0, "the host's fingerprint as eight hex digits, no date "
              "(%s)", host_row);
    text_format(want, sizeof want, mp_text(MP_TEXT_REFUSED_HERE_ROW), "1234ABCD");
    ut_checkf(strcmp(here_row, want) == 0, "and this side's beside it (%s)", here_row);

    memset(&refusal, 0, sizeof refusal);
    refusal.reason     = MP_MOD_REFUSE_MODS;
    refusal.sub        = MP_MOD_SUB_OTHER_BUILD;
    refusal.host_stamp = 0x6AB9F2A1u;
    refusal.here_stamp = 0x6ABB2222u;
    memcpy(refusal.mod, "enhanced_resolution.dll", 24u);
    memcpy(refusal.host_version, "0.4.4", 6u);
    memcpy(refusal.here_version, "0.4.3", 6u);
    ut_check(mp_screens_refusal_lines(&refusal, band, sizeof band, host_row, here_row,
                                      sizeof host_row),
             "a released mod's refusal is named");
    mp_mod_census_describe_stamp(refusal.host_stamp, built, sizeof built);
    text_format(want, sizeof want, "0.4.4, %s", built);
    ut_checkf(strstr(host_row, want) != NULL && strstr(host_row, "id ") == NULL,
              "the host's row has its release and the local date it was built (%s)", host_row);
    mp_mod_census_describe_stamp(refusal.here_stamp, built, sizeof built);
    text_format(want, sizeof want, "0.4.3, %s", built);
    ut_checkf(strstr(here_row, want) != NULL, "and this side's row its own (%s)", here_row);

    refusal.host_stamp = 0x00000010u;
    ut_check(mp_screens_refusal_lines(&refusal, NULL, 0u, host_row, here_row, sizeof host_row) &&
                 strstr(host_row, "id 00000010") != NULL,
             "a stamp that is no plausible build date is shown as the number it is");
}

/* A refusal whose detail this build cannot read keeps the reason's word, which is all a later
 * host's unknown case should get. Built by hand, because the real judge never sends one. */
static void check_what_is_not_named(void)
{
    mp_mod_refusal_t refusal;
    char             band[64];
    char             host_row[MP_SCREEN_ROW_TEXT_MAX];
    char             here_row[MP_SCREEN_ROW_TEXT_MAX];

    ut_section("a refusal that names nothing this build knows keeps the reason's word");
    memset(&refusal, 0, sizeof refusal);
    refusal.reason = MP_MOD_REFUSE_GAME_DATA;
    memcpy(refusal.mod, "damage.txt", 11u);
    ut_check(!mp_screens_refusal_lines(&refusal, band, sizeof band, host_row, here_row,
                                       sizeof host_row),
             "game data with no sub, a deathmatch host's sending away, names no file");
    refusal.reason = MP_MOD_REFUSE_MODS;
    refusal.sub    = 9u;
    ut_check(!mp_screens_refusal_lines(&refusal, band, sizeof band, NULL, NULL, 0u),
             "a sub this build does not know is not guessed at");
    refusal.sub    = MP_MOD_SUB_OTHER_BUILD;
    refusal.mod[0] = '\0';
    ut_check(!mp_screens_refusal_lines(&refusal, band, sizeof band, NULL, NULL, 0u),
             "and a detail with no name says nothing either");
    ut_check(!mp_screens_refusal_lines(NULL, band, sizeof band, NULL, NULL, 0u),
             "no refusal at all is no sentence");
}

/* What the band says when the lobby's frame writes it, against `want`. */
static void check_the_band(const char *when, const char *want)
{
    mp_screens_refresh_band();
    ut_checkf(strcmp(mps.lobby_band, want) == 0, "%s: the band reads '%s' and should read '%s'",
              when, mps.lobby_band, want);
}

/* A DLL the host's list does not name: the band says the host refused it, by name, the host's row
 * says it is not allowed, and this side's row is left out, because the census of a test process
 * does not hold the DLL. */
static void check_a_dll_the_host_does_not_allow(void)
{
    static const char *const X[] = { "x.dll" };
    const outside_t          one = { X, 1u, 1u };
    mp_mod_refusal_t         refusal;
    char                     want[CAPTION_MAX];
    char                     host_row[MP_SCREEN_ROW_TEXT_MAX] = "";
    char                     here_row[MP_SCREEN_ROW_TEXT_MAX] = "";

    ut_section("a DLL the host's list does not name: the band names it, the host's row says so");
    ut_checkf(mp_mod_allow_list()[0] == '\0',
              "the host's [multiplayer] AllowMods is empty, since nothing here reads the ini "
              "('%s')", mp_mod_allow_list());
    ut_check(a_judged_lobby(0x9600u, &s_host_statement, &s_host_statement, &one) &&
                 mp_session_last_deny(&s_client) == MP_DENY_FOREIGN_DLL,
             "the host refuses the join for a DLL outside this release, reason 8");
    text_format(want, sizeof want, mp_text(MP_TEXT_REFUSED_NOT_ALLOWED), "x.dll");
    check_the_band("x.dll not allowed", want);
    ut_check(mp_bridge_content_refusal(&refusal) &&
                 mp_screens_refusal_lines(&refusal, NULL, 0u, host_row, here_row,
                                          sizeof host_row),
             "the player list's rows come out of the same refusal");
    text_format(want, sizeof want, mp_text(MP_TEXT_REFUSED_HOST_ROW),
                mp_text(MP_TEXT_REFUSED_NOT_ALLOWED_WORD));
    ut_checkf(strcmp(host_row, want) == 0, "the host's row reads '%s' and should read '%s'",
              host_row, want);
    ut_checkf(here_row[0] == '\0', "and this side has no row, since its census does not hold "
              "x.dll ('%s')", here_row);
    close_lobby();
}

/* A name of 31 characters, the most a request states, in each of the five languages: the band's
 * cut takes the end of the name and leaves every word in front of it. */
static void check_a_long_name_in_every_language(void)
{
    static const char *const LONG_NAME[] = { "ReShade32_addon_screenshots.dll" };
    const outside_t          one = { LONG_NAME, 1u, 1u };
    unsigned                 wrong = 0u;
    size_t                   language;

    ut_section("a name of 31 characters: the band cuts the name's end, never a word");
    ut_check(a_judged_lobby(0x9700u, &s_host_statement, &s_host_statement, &one) &&
                 mp_session_last_deny(&s_client) == MP_DENY_FOREIGN_DLL,
             "the host refuses the longest name a request states");
    for (language = 0; language < (size_t)LANGUAGE_COUNT; ++language) {
        const char *format;
        const char *slot;
        char        whole[128];
        size_t      words;
        size_t      shown;

        mp_text_set_language((language_t)language);
        format = mp_text(MP_TEXT_REFUSED_NOT_ALLOWED);
        slot   = strstr(format, "%s");
        words  = slot != NULL ? (size_t)(slot - format) : 0u;
        text_format(whole, sizeof whole, format, LONG_NAME[0]);
        mp_screens_refresh_band();
        shown = strlen(mps.lobby_band);
        if (slot == NULL || shown <= words || shown >= strlen(whole) ||
            strncmp(mps.lobby_band, whole, shown) != 0) {
            ++wrong;
            ut_checkf(false, "in %s the band '%s' is the start of '%s' with every word and part "
                      "of the name", language_tag((language_t)language), mps.lobby_band, whole);
        }
    }
    mp_text_set_language(LANGUAGE_EN);
    ut_checkf(wrong == 0u, "in all five languages the band keeps every word and cuts the name: "
              "%u do not", wrong);
    close_lobby();
}

/* More DLLs than the request names, and a request with no list: with no name this side can say,
 * the band says the word for a refused DLL. Without a word for the reason the band would read the
 * refusal as a join still under way. */
static void check_what_names_no_dll(void)
{
    const outside_t  three_unnamed = { NULL, 0u, 3u };
    mp_mod_refusal_t refusal;
    char             band[CAPTION_MAX];
    char             host_row[MP_SCREEN_ROW_TEXT_MAX];
    char             here_row[MP_SCREEN_ROW_TEXT_MAX];

    ut_section("three DLLs and none named: this side names none, and the band says the word");
    ut_check(a_judged_lobby(0x9800u, &s_host_statement, &s_host_statement, &three_unnamed) &&
                 mp_session_last_deny(&s_client) == MP_DENY_FOREIGN_DLL &&
                 mp_bridge_content_refusal(&refusal) && refusal.sub == MP_MOD_SUB_NOT_NAMED &&
                 refusal.mod[0] == '\0',
             "the host refuses more DLLs than the request names, reason 8, sub 2, and a test "
             "process, which made no statement of its own, has no name for the first of them");
    check_the_band("sub 2 with no name of this side's", mp_text(MP_TEXT_DENY_FOREIGN_DLL));
    close_lobby();

    ut_section("a statement with no list behind its mods: the band says the word, and no rows");
    ut_check(a_judged_lobby(0x9900u, &s_host_statement, &s_host_statement, NULL) &&
                 mp_session_last_deny(&s_client) == MP_DENY_FOREIGN_DLL &&
                 mp_bridge_content_refusal(&refusal) && refusal.sub == MP_MOD_SUB_NO_LIST,
             "the host refuses a statement without the list, reason 8, sub 3");
    check_the_band("sub 3", mp_text(MP_TEXT_DENY_FOREIGN_DLL));
    ut_check(!mp_screens_refusal_lines(&refusal, band, sizeof band, host_row, here_row,
                                       sizeof host_row),
             "and a request with no list has no sentence and no rows: the list keeps its line");
    close_lobby();
}

/* The records of a refused DLL made by hand, with this side's values a game has: the census holds
 * the DLL as another release of this project, as any other DLL, whose own version is not shown,
 * and with a stamp that is no date. Then more DLLs than named, with the first this side's request
 * could not name, and the cases that keep the word. */
static void check_the_records_of_a_dll(void)
{
    mp_mod_refusal_t refusal;
    char             band[CAPTION_MAX] = "";
    char             host_row[MP_SCREEN_ROW_TEXT_MAX] = "";
    char             here_row[MP_SCREEN_ROW_TEXT_MAX] = "";
    char             built[32];
    char             want[MP_SCREEN_ROW_TEXT_MAX];

    ut_section("a refused DLL this side's census holds: this side's row shows its build");
    memset(&refusal, 0, sizeof refusal);
    refusal.reason     = MP_MOD_REFUSE_FOREIGN;
    refusal.sub        = MP_MOD_SUB_NOT_ALLOWED;
    refusal.here_stamp = 0x6ABB2222u;
    memcpy(refusal.mod, "enhanced_resolution.dll", 24u);
    memcpy(refusal.host_version, "0.4.4", 6u);
    memcpy(refusal.here_version, "0.4.10", 7u);
    ut_check(mp_screens_refusal_lines(&refusal, band, sizeof band, host_row, here_row,
                                      sizeof host_row),
             "a DLL the host does not allow is named");
    text_format(want, sizeof want, mp_text(MP_TEXT_REFUSED_NOT_ALLOWED),
                "enhanced_resolution.dll");
    ut_checkf(strcmp(band, want) == 0, "the band reads '%s' and should read '%s'", band, want);
    text_format(want, sizeof want, mp_text(MP_TEXT_REFUSED_HOST_ROW),
                mp_text(MP_TEXT_REFUSED_NOT_ALLOWED_WORD));
    ut_checkf(strcmp(host_row, want) == 0, "the host's row says it is not allowed, and not the "
              "host's release (%s)", host_row);
    mp_mod_census_describe_stamp(refusal.here_stamp, built, sizeof built);
    text_format(want, sizeof want, "0.4.10, %s", built);
    ut_checkf(strstr(here_row, want) != NULL,
              "another release of this project shows its number and date here (%s)", here_row);

    refusal.here_version[0] = '\0';
    ut_check(mp_screens_refusal_lines(&refusal, NULL, 0u, host_row, here_row, sizeof host_row),
             "any other DLL is named the same way");
    text_format(want, sizeof want, mp_text(MP_TEXT_REFUSED_HERE_ROW), built);
    ut_checkf(strcmp(here_row, want) == 0, "and shows its date alone, since its own version says "
              "nothing here (%s)", here_row);
    refusal.here_stamp = 0x00000010u;
    ut_check(mp_screens_refusal_lines(&refusal, NULL, 0u, host_row, here_row, sizeof host_row) &&
                 strstr(here_row, "id 00000010") != NULL,
             "a stamp that is no plausible build date is shown as the number it is");

    ut_section("more DLLs than named, by hand: the band names the first not named, and no rows");
    refusal.sub = MP_MOD_SUB_NOT_NAMED;
    ut_check(mp_screens_refusal_lines(&refusal, band, sizeof band, host_row, here_row,
                                      sizeof host_row),
             "the first DLL the request could not name is named");
    text_format(want, sizeof want, mp_text(MP_TEXT_REFUSED_TOO_MANY), "enhanced_resolution.dll");
    ut_checkf(strcmp(band, want) == 0, "the band reads '%s' and should read '%s'", band, want);
    ut_checkf(host_row[0] == '\0' && here_row[0] == '\0',
              "and there are no rows, since the host judged how many there are and no build "
              "('%s', '%s')", host_row, here_row);

    ut_section("a refused DLL that names nothing this build can say keeps the word");
    refusal.sub = MP_MOD_SUB_NO_LIST;
    ut_check(!mp_screens_refusal_lines(&refusal, band, sizeof band, NULL, NULL, 0u),
             "no readable list is said with the word, even with a name beside it");
    refusal.sub = 0u;
    ut_check(!mp_screens_refusal_lines(&refusal, band, sizeof band, NULL, NULL, 0u),
             "a refusal whose detail did not decode, sub 0, keeps the word");
    refusal.sub = 9u;
    ut_check(!mp_screens_refusal_lines(&refusal, band, sizeof band, NULL, NULL, 0u),
             "a sub this build does not know is not guessed at");
    refusal.sub    = MP_MOD_SUB_NOT_ALLOWED;
    refusal.mod[0] = '\0';
    ut_check(!mp_screens_refusal_lines(&refusal, band, sizeof band, NULL, NULL, 0u),
             "and a detail with no name says nothing either");
}

/* A connected client that waits for its host: the band says so. */
static void check_a_connected_client(void)
{
    ut_section("a connected client's band says it waits for its host");
    ut_check(!a_judged_lobby(0x9500u, &s_host_statement, &s_host_statement, &NONE_OUTSIDE) &&
                 mp_session_is_connected(&s_client),
             "the same statement joins");
    check_the_band("connected", mp_text(MP_TEXT_JOIN_CONNECTED));
    close_lobby();
}

int main(void)
{
    mp_mod_census_take();
    memset(&s_host_statement, 0, sizeof s_host_statement);
    s_host_statement.damage = 0x84273DBBu;
    s_host_statement.roster = 0xE1D7140Du;
    mp_mod_census_required_builds(&s_host_statement);
    ut_check(s_host_statement.count == 1u &&
                 s_host_statement.mods[0].id == MP_WIRE_MOD_MULTIPLAYER,
             "the census gives the build of the module this code runs in, as the multiplayer");

    check_another_build();
    check_missing_here();
    check_missing_at_the_host();
    check_other_game_data();
    check_a_missing_data_file();
    check_the_values_a_game_has();
    check_what_is_not_named();
    check_a_dll_the_host_does_not_allow();
    check_a_long_name_in_every_language();
    check_what_names_no_dll();
    check_the_records_of_a_dll();
    check_a_connected_client();
    return ut_summary("mp_menu_screens_band");
}
