/* mp_bridge_content.c: the join check against the real modules.
 *
 * Two sessions meet over the loopback. The host holds the bridge's own judge and its own statement,
 * made from the census of this process; the client states another build of the multiplayer, other
 * game data, a mod the host's list does not know, DLLs outside this release, or bytes that are no
 * statement. Then the record the lobby's screen reads is held against what the host said. Nothing
 * here is written again in the test: the codec, the judgement, the detail and the session are the
 * modules that ship. Then the content note from a client's lobby, and floods of refused requests
 * against the line budget.
 *
 * Every statement here carries the list of DLLs outside this release, as a build like this one
 * always sends it, judged and empty unless a case says otherwise; a statement without it is the
 * case that is refused for it. A test program has no release number, so its own census judges
 * nothing and the list a client states is written here. The host's [multiplayer] AllowMods is
 * written into the ini beside the program and taken away again, the file too when this test made
 * it; the ini is shared with other tests, which is why this one holds the ctest lock on it.
 */
#include "unittest.h"

#include "mp_bridge_content.h"
#include "mp_bridge_lobby.h"
#include "mp_lobby.h"
#include "mp_loopback.h"
#include "mp_mod_allow.h"
#include "mp_mod_census.h"
#include "mp_mod_manifest_rule.h"
#include "mp_session.h"
#include "mp_transport.h"
#include "mp_wire.h"

#include "common/ini.h"

#include <windows.h>

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <string.h>

/* Static, not on the stack: two sessions and the loopback ring are several megabytes. */
static mp_loopback_t s_net;
static mp_session_t  s_host;
static mp_session_t  s_client;

static mp_mod_manifest_t s_host_statement;

/* The DLLs outside this release a client states, and whether its build judged. */
typedef struct outside {
    const char *const *names;
    size_t             listed;
    size_t             count;
    bool               judged;
} outside_t;

static const outside_t NONE_OUTSIDE = { NULL, 0u, 0u, true };

/* The statement's bytes: the mods, and the list behind them. */
static size_t encode(const mp_mod_manifest_t *manifest, const outside_t *outside, uint8_t *bytes)
{
    size_t length = mp_mod_manifest_encode(manifest, bytes, MP_MOD_STATEMENT_MAX_BYTES);

    if (length != 0u && outside != NULL) {
        length += mp_mod_foreign_encode(outside->names, outside->listed, outside->count,
                                        outside->judged, bytes + length,
                                        MP_MOD_STATEMENT_MAX_BYTES - length, NULL);
    }
    return length;
}

static void state(mp_session_t *session, const mp_mod_manifest_t *manifest,
                  const outside_t *outside)
{
    uint8_t bytes[MP_MOD_STATEMENT_MAX_BYTES];
    size_t  length = encode(manifest, outside, bytes);

    ut_check(length != 0u && mp_session_set_statement(session, bytes, length),
             "the statement is encoded and taken");
}

/* Both sides until the client is connected or refused, or the cap is hit. */
static void run(uint32_t *now)
{
    int tick;

    for (tick = 0; tick < 200; ++tick) {
        *now += 16u;
        mp_loopback_pump(&s_net, *now);
        mp_session_update(&s_host, *now);
        mp_session_update(&s_client, *now);
        if (mp_session_is_connected(&s_client) ||
            mp_session_last_deny(&s_client) != MP_DENY_NONE) {
            return;
        }
    }
}

/* `outside` NULL states the mods alone; `client_statement` NULL states the raw bytes. */
static void meet(const mp_mod_manifest_t *client_statement, const outside_t *outside,
                 const uint8_t *raw, size_t raw_bytes, uint32_t *now)
{
    static mp_transport_t host_t;
    static mp_transport_t client_t;

    mp_loopback_init(&s_net, NULL, 0xC0DEu);
    host_t   = mp_loopback_transport(&s_net, MP_LOOPBACK_ENDPOINT_A);
    client_t = mp_loopback_transport(&s_net, MP_LOOPBACK_ENDPOINT_B);
    mp_session_init(&s_host, MP_SESSION_HOST, &host_t, 0x777u);
    mp_session_init(&s_client, MP_SESSION_CLIENT, &client_t, 0x888u);
    state(&s_host, &s_host_statement, &NONE_OUTSIDE);
    mp_session_set_judge(&s_host, mp_bridge_content_judge);
    if (client_statement != NULL) {
        state(&s_client, client_statement, outside);
    } else {
        ut_check(mp_session_set_statement(&s_client, raw, raw_bytes), "the raw bytes are taken");
    }
    mp_bridge_content_bind(&s_host, &s_client, true);
    mp_session_connect(&s_client, MP_LOOPBACK_ENDPOINT_A);
    run(now);
}

static void check_the_same_statement(uint32_t *now)
{
    mp_mod_refusal_t refusal;

    ut_section("the same game data and the same build are admitted");

    meet(&s_host_statement, &NONE_OUTSIDE, NULL, 0u, now);
    ut_check(mp_session_is_connected(&s_client) &&
                 mp_session_last_deny(&s_client) == MP_DENY_NONE,
             "the client joins");
    ut_check(!mp_bridge_content_refusal(&refusal) && refusal.reason == 0u,
             "and the record says it was not refused");
}

static void check_another_build(uint32_t *now)
{
    mp_mod_manifest_t client = s_host_statement;
    mp_mod_refusal_t  refusal;
    uint32_t          here_stamp = 0u;
    uint32_t          here_image = 0u;
    char              here_version[MP_MOD_REFUSAL_VERSION_MAX];

    ut_section("another build of the multiplayer is refused, and the record names it");

    client.mods[0].stamp ^= 0x00010000u;
    meet(&client, &NONE_OUTSIDE, NULL, 0u, now);
    ut_check(!mp_session_is_connected(&s_client) &&
                 mp_session_last_deny(&s_client) == MP_DENY_MODS,
             "the client is refused for a mod, reason 7");
    ut_check(mp_bridge_content_refusal(&refusal) && refusal.reason == (uint8_t)MP_DENY_MODS &&
                 refusal.sub == MP_MOD_SUB_OTHER_BUILD,
             "the record says a mod, and another build");
    ut_checkf(strcmp(refusal.mod, "multiplayer.dll") == 0 &&
                  refusal.host_stamp == s_host_statement.mods[0].stamp,
              "it names multiplayer.dll and the host's stamp (%s, %08X)", refusal.mod,
              (unsigned)refusal.host_stamp);
    (void)mp_mod_census_build_of(MP_WIRE_MOD_MULTIPLAYER, &here_stamp, &here_image, here_version,
                                 sizeof here_version);
    ut_check(refusal.here_stamp == here_stamp && strcmp(refusal.here_version, here_version) == 0,
             "and this side's own build beside it, out of the census");
}

static void check_other_game_data(uint32_t *now)
{
    mp_mod_manifest_t client = s_host_statement;
    mp_mod_refusal_t  refusal;

    ut_section("other game data is refused as game data, and the record names the file");

    client.roster ^= 0x5Au;
    meet(&client, &NONE_OUTSIDE, NULL, 0u, now);
    ut_check(mp_session_last_deny(&s_client) == MP_DENY_CONTENT,
             "the client is refused for the game data, reason 3");
    ut_check(mp_bridge_content_refusal(&refusal) && refusal.sub == MP_MOD_SUB_ROSTER &&
                 strcmp(refusal.mod, "characters.ini") == 0 &&
                 refusal.host_stamp == s_host_statement.roster && refusal.host_version[0] == '\0',
             "characters.ini, the host's fingerprint of it, and no version");

    client = s_host_statement;
    client.damage ^= 0x5Au;
    meet(&client, &NONE_OUTSIDE, NULL, 0u, now);
    ut_check(mp_bridge_content_refusal(&refusal) && refusal.sub == MP_MOD_SUB_DAMAGE &&
                 strcmp(refusal.mod, "damage.txt") == 0,
             "and damage.txt for another damage table");
}

static void check_what_is_not_judged(uint32_t *now)
{
    mp_mod_manifest_t client = s_host_statement;
    static const uint8_t NOT_A_STATEMENT[] = { 1u, 2u, 3u };

    ut_section("what the judge passes over");

    client.count         = (uint8_t)(client.count + 1u);
    client.mods[client.count - 1u].id    = 0xC7u;
    client.mods[client.count - 1u].stamp = 1u;
    meet(&client, &NONE_OUTSIDE, NULL, 0u, now);
    ut_check(mp_session_is_connected(&s_client),
             "a mod whose number this build's list does not know is admitted");

    meet(NULL, NULL, NOT_A_STATEMENT, sizeof NOT_A_STATEMENT, now);
    ut_check(mp_session_is_connected(&s_client),
             "bytes that are no statement are admitted unjudged rather than refusing a player");
}

/* The host reads its list as it does before it hosts. */
static void host_allows(const char *list)
{
    ut_check(ini_write_string("multiplayer", "AllowMods", list) &&
                 strcmp(mp_mod_allow_read(), list) == 0,
             "the host reads [multiplayer] AllowMods");
}

static void check_a_dll_outside_this_release(uint32_t *now)
{
    static const char *const X[] = { "x.dll" };
    const outside_t          one = { X, 1u, 1u, true };
    const outside_t          more = { X, 1u, 3u, true };
    const outside_t          unjudging = { X, 1u, 1u, false };
    mp_mod_refusal_t         refusal;
    mp_bridge_content_judge_counts_t before;
    mp_bridge_content_judge_counts_t after;
    uint32_t                 here_stamp = 0u;
    uint32_t                 here_image = 0u;
    char                     host_version[MP_MOD_REFUSAL_VERSION_MAX];

    ut_section("a DLL outside this release the host's list does not name is refused by name");
    host_allows("");
    meet(&s_host_statement, &one, NULL, 0u, now);
    ut_check(!mp_session_is_connected(&s_client) &&
                 mp_session_last_deny(&s_client) == MP_DENY_FOREIGN_DLL,
             "the client stating x.dll is refused, reason 8");
    (void)mp_mod_census_build_of(MP_WIRE_MOD_MULTIPLAYER, &here_stamp, &here_image, host_version,
                                 sizeof host_version);
    ut_checkf(mp_bridge_content_refusal(&refusal) &&
                  refusal.reason == (uint8_t)MP_DENY_FOREIGN_DLL &&
                  refusal.sub == MP_MOD_SUB_NOT_ALLOWED && strcmp(refusal.mod, "x.dll") == 0 &&
                  refusal.host_stamp == 0u && strcmp(refusal.host_version, host_version) == 0,
              "the record names x.dll and the host's release, and no stamp of the host's (%s)",
              refusal.mod);
    ut_check(refusal.here_stamp == 0u && refusal.here_version[0] == '\0',
             "and nothing of this side's, whose census does not hold x.dll");

    ut_section("the same DLL named in the host's [multiplayer] AllowMods is let through");
    mp_bridge_content_judge_counts(&before);
    host_allows("x");
    meet(&s_host_statement, &one, NULL, 0u, now);
    mp_bridge_content_judge_counts(&after);
    ut_check(mp_session_is_connected(&s_client), "the client joins");
    ut_checkf(after.let_through > before.let_through,
              "and the request is counted as let through (%u)", (unsigned)after.let_through);

    ut_section("more DLLs than a request names cannot be held against the list");
    meet(&s_host_statement, &more, NULL, 0u, now);
    ut_check(mp_session_last_deny(&s_client) == MP_DENY_FOREIGN_DLL &&
                 mp_bridge_content_refusal(&refusal) && refusal.sub == MP_MOD_SUB_NOT_NAMED,
             "three loaded and one named: refused, reason 8, sub 2");

    ut_section("a statement without the list, from the same build, is refused for it");
    meet(&s_host_statement, NULL, NULL, 0u, now);
    ut_check(mp_session_last_deny(&s_client) == MP_DENY_FOREIGN_DLL &&
                 mp_bridge_content_refusal(&refusal) && refusal.sub == MP_MOD_SUB_NO_LIST &&
                 refusal.mod[0] == '\0',
             "no list at all: refused, reason 8, sub 3, with no name");

    ut_section("a side that judged nothing is admitted and counted");
    mp_bridge_content_judge_counts(&before);
    host_allows("");
    meet(&s_host_statement, &unjudging, NULL, 0u, now);
    mp_bridge_content_judge_counts(&after);
    ut_check(mp_session_is_connected(&s_client) && after.unjudging > before.unjudging,
             "a build without a release number of its own names nothing it could be held to");
}

/* The statement stands from the arming, so the content note goes out in the lobby already: a
 * client in its lobby, connected, whose lobby tick is handed the statement's number as the pump
 * hands it bridge.content, puts a 0x93 with that number in front of the host before any level
 * exists. The tick is the lobby's own, the one the pump calls, not the content module's. */
static void check_the_note_goes_out_in_the_lobby(uint32_t *now)
{
    uint8_t  note[64];
    size_t   bytes = 0u;
    uint32_t heard = 0u;
    uint32_t expected = mp_mod_manifest_fingerprint(&s_host_statement);
    bool     got = false;
    int      tick;

    ut_section("the content note goes out from the lobby, carrying the statement's number");

    meet(&s_host_statement, &NONE_OUTSIDE, NULL, 0u, now);
    ut_check(mp_session_is_connected(&s_client), "the client is in its lobby, connected");
    mp_bridge_lobby_bind(&s_host, &s_client, NULL, true, NULL);
    mp_bridge_content_reset();
    mp_bridge_lobby_tick(false, expected);
    for (tick = 0; tick < 100 && !got; ++tick) {
        *now += 16u;
        mp_loopback_pump(&s_net, *now);
        mp_session_update(&s_host, *now);
        mp_session_update(&s_client, *now);
        while (!got && mp_session_read_reliable(&s_host, 0, note, sizeof note, &bytes)) {
            got = mp_lobby_is_content(note, bytes) && mp_lobby_content_decode(note, bytes, &heard);
        }
    }
    ut_checkf(got && heard == expected && expected != 0u,
              "the host holds a 0x93 with the statement's number (%08X against %08X)",
              (unsigned)heard, (unsigned)expected);
    ut_check(!mp_bridge_lobby_content_mismatch(), "and nothing disagrees about it");
}

/* Every refusal is either said or counted, over every reason the judge has. */
static bool every_refusal_accounted(const mp_bridge_content_judge_counts_t *c)
{
    return c->said + c->unsaid == c->damage + c->roster + c->missing_at_host +
                                      c->missing_at_joiner + c->other_build + c->not_allowed +
                                      c->not_named + c->no_list;
}

/* Requests the list lets through have lines of their own: a flood of them says the first eight of
 * the process and counts the rest, and leaves the lines a refusal needs to the refusals. It runs
 * ahead of the refusals' floods, while fewer than eight refusals are said, so that a line taken
 * from them would show. */
static void check_a_flood_let_through(void)
{
    static const char *const X[] = { "x.dll" };
    const outside_t                  one = { X, 1u, 1u, true };
    mp_bridge_content_judge_counts_t before;
    mp_bridge_content_judge_counts_t after;
    mp_bridge_content_judge_counts_t next;
    mp_mod_manifest_t                stranger = s_host_statement;
    uint8_t                          own[MP_MOD_STATEMENT_MAX_BYTES];
    uint8_t                          theirs[MP_MOD_STATEMENT_MAX_BYTES];
    uint8_t                          detail[MP_MOD_REFUSAL_DETAIL_BYTES];
    size_t                           own_bytes = encode(&s_host_statement, &NONE_OUTSIDE, own);
    size_t                           far_bytes;
    size_t                           detail_bytes = 0u;
    unsigned                         i;
    unsigned                         admitted = 0u;

    ut_section("a flood of requests the list lets through is said up to a bound of its own");

    host_allows("x");
    far_bytes = encode(&s_host_statement, &one, theirs);
    mp_bridge_content_judge_counts(&before);
    for (i = 0; i < 100u; ++i) {
        if (mp_bridge_content_judge(own, own_bytes, theirs, far_bytes, detail, sizeof detail,
                                    &detail_bytes) == MP_MOD_REFUSE_NONE) {
            ++admitted;
        }
    }
    mp_bridge_content_judge_counts(&after);
    ut_checkf(admitted == 100u && after.let_through - before.let_through == 100u,
              "all 100 are let through with x.dll and counted (%u)",
              (unsigned)(after.let_through - before.let_through));
    ut_checkf(after.let_through_said == 8u,
              "their lines stop at eight in the process and the rest is counted (%u said)",
              (unsigned)after.let_through_said);
    ut_checkf(before.said < 8u && after.said == before.said && after.unsaid == before.unsaid,
              "and none of them takes a line or a count of the refusals (%u said so far)",
              (unsigned)after.said);

    stranger.mods[0].stamp ^= 0x00FF0000u;
    far_bytes = encode(&stranger, &NONE_OUTSIDE, theirs);
    (void)mp_bridge_content_judge(own, own_bytes, theirs, far_bytes, detail, sizeof detail,
                                  &detail_bytes);
    mp_bridge_content_judge_counts(&next);
    ut_checkf(next.said == after.said + 1u,
              "so the refusal after them is still said (%u said)", (unsigned)next.said);
}

/* The judge runs on a request before the cookie proves its sender. A flood of forged requests
 * must cost counts, not a warning line each; the lines are for the first eight of the process. */
static void check_a_flood_is_counted_not_said(void)
{
    static const char *const X[] = { "x.dll" };
    const outside_t                  one = { X, 1u, 1u, true };
    mp_bridge_content_judge_counts_t before;
    mp_bridge_content_judge_counts_t after;
    mp_mod_manifest_t                stranger = s_host_statement;
    uint8_t                          own[MP_MOD_STATEMENT_MAX_BYTES];
    uint8_t                          theirs[MP_MOD_STATEMENT_MAX_BYTES];
    uint8_t                          detail[MP_MOD_REFUSAL_DETAIL_BYTES];
    size_t                           own_bytes = encode(&s_host_statement, &NONE_OUTSIDE, own);
    size_t                           far_bytes;
    size_t                           detail_bytes = 0u;
    unsigned                         i;
    unsigned                         refused = 0u;

    ut_section("a flood of requests from another build is counted, and only the first are said");

    stranger.mods[0].stamp ^= 0x00FF0000u;
    far_bytes = encode(&stranger, &NONE_OUTSIDE, theirs);
    mp_bridge_content_judge_counts(&before);
    for (i = 0; i < 100u; ++i) {
        if (mp_bridge_content_judge(own, own_bytes, theirs, far_bytes, detail, sizeof detail,
                                    &detail_bytes) == MP_MOD_REFUSE_MODS) {
            ++refused;
        }
    }
    mp_bridge_content_judge_counts(&after);
    ut_checkf(after.judged - before.judged == 100u && refused == 100u &&
                  after.other_build - before.other_build == 100u,
              "all 100 are judged and refused as another build (%u)", refused);
    ut_checkf(after.said <= 8u, "at most eight refusals of the process are said (%u)",
              (unsigned)after.said);
    ut_checkf(every_refusal_accounted(&after),
              "and every refusal is either said or counted as unsaid (%u said, %u more counted)",
              (unsigned)after.said, (unsigned)after.unsaid);

    ut_section("a flood of requests with a DLL the list does not name is counted the same way");
    host_allows("");
    far_bytes = encode(&s_host_statement, &one, theirs);
    refused = 0u;
    mp_bridge_content_judge_counts(&before);
    for (i = 0; i < 100u; ++i) {
        if (mp_bridge_content_judge(own, own_bytes, theirs, far_bytes, detail, sizeof detail,
                                    &detail_bytes) == MP_MOD_REFUSE_FOREIGN) {
            ++refused;
        }
    }
    mp_bridge_content_judge_counts(&after);
    ut_checkf(refused == 100u && after.not_allowed - before.not_allowed == 100u &&
                  after.said <= 8u && every_refusal_accounted(&after),
              "all 100 refused for x.dll, still at most eight said in the process, every one "
              "accounted for (%u refused, %u said)", refused, (unsigned)after.said);
}

static void check_the_host_side(void)
{
    mp_mod_refusal_t refusal;

    ut_section("a host asks nothing of the record");

    mp_bridge_content_bind(&s_host, &s_client, false);
    ut_check(!mp_bridge_content_refusal(&refusal), "a host has no refusal of its own");
    ut_check(!mp_bridge_content_refusal(NULL), "no record to fill is refused");
}

int main(void)
{
    uint32_t now = 0u;
    bool     ini_existed = GetFileAttributesA(ini_path()) != INVALID_FILE_ATTRIBUTES;

    /* A run cut off earlier may have left the key behind. */
    (void)WritePrivateProfileStringA("multiplayer", "AllowMods", NULL, ini_path());
    mp_mod_census_take();
    memset(&s_host_statement, 0, sizeof s_host_statement);
    s_host_statement.damage = 0x84273DBBu;
    s_host_statement.roster = 0xE1D7140Du;
    mp_mod_census_required_builds(&s_host_statement);
    ut_check(s_host_statement.count == 1u &&
                 s_host_statement.mods[0].id == MP_WIRE_MOD_MULTIPLAYER &&
                 s_host_statement.mods[0].stamp != 0u,
             "the census gives the build of the module this code runs in, as the multiplayer");

    check_the_same_statement(&now);
    check_another_build(&now);
    check_other_game_data(&now);
    check_what_is_not_judged(&now);
    check_a_dll_outside_this_release(&now);
    check_the_note_goes_out_in_the_lobby(&now);
    check_a_flood_let_through();
    check_a_flood_is_counted_not_said();
    check_the_host_side();

    (void)WritePrivateProfileStringA("multiplayer", "AllowMods", NULL, ini_path());
    if (!ini_existed) {
        (void)DeleteFileA(ini_path());
    }
    return ut_summary("mp_bridge_content");
}
