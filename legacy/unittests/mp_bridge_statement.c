/* mp_bridge_statement.c: this side's statement is made once per process and handed to each
 * transport.
 *
 * The real module against stand-ins (mp_bridge_statement_stand_in.c). Before the shot module's init
 * the fingerprint is 0 and nothing is made, the census untouched, and while the DLL installs a
 * transport that asks is told the statement is held; after it, the first question makes the
 * statement and takes the census once, every later question and every transport gets the same
 * number without a second census, and what a transport hands its session decodes to the same
 * statement, with the census's DLLs outside this release behind the mods. A session that does not
 * take the statement is said as a warning, never as a success. The ini path, which learns from its
 * 32nd substep, runs in a process of its own (mp_bridge_statement_ini.c), because a process makes
 * its statement only once.
 */
#include "unittest.h"

#include "mp_bridge_statement_stand_in.h"

#include "mp_bridge.h"
#include "mp_bridge_content.h"
#include "mp_bridge_statement.h"
#include "mp_mod_manifest_rule.h"
#include "mp_session.h"

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <string.h>

#define DAMAGE 0x84273DBBu

/* What the stand-in census answers: one DLL a list can name, and one whose name is too long. */
static const char *const OUTSIDE[] = {
    "fps_counter_hud.dll", "a_name_far_longer_than_thirty_one_characters.dll",
};

static bool handed_the_statement(mp_session_t *session, const mp_mod_manifest_t *own)
{
    mp_mod_manifest_t handed;

    return session->statement_bytes != 0u &&
           mp_mod_manifest_decode(session->statement, session->statement_bytes, &handed) &&
           handed.damage == own->damage && handed.roster == own->roster &&
           handed.count == own->count && handed.mods[0].stamp == own->mods[0].stamp &&
           handed.mods[0].image == own->mods[0].image;
}

static void check_before_the_shot_init(void)
{
    mp_mod_manifest_t own;

    ut_section("before the shot module's init there is nothing to state");

    stand_in_set_damage(true, false, 0x11111111u);
    ut_check(mp_bridge_statement_fingerprint() == 0u, "the fingerprint answers 0");
    ut_check(!mp_bridge_statement_own(&own), "no statement is made");
    ut_check(stand_in_census_calls() == 0u, "and the census is not taken");
}

static void check_while_the_dll_installs(void)
{
    mp_bridge_state_t *st = stand_in_state();
    mp_mod_manifest_t  own;

    ut_section("while the DLL installs, the statement is held");

    stand_in_set_damage(true, true, DAMAGE);
    mp_bridge_statement_hold(true);
    memset(st, 0, sizeof *st);
    st->mode = MP_BRIDGE_UDP_HOST;
    mp_bridge_statement_learn(false);
    ut_check(!st->content_decided && !mp_bridge_statement_own(&own) &&
                 stand_in_census_calls() == 0u,
             "a transport armed while the DLL installs states nothing and takes no census");
    ut_check(stand_in_log_has("is not taken while this DLL installs"), "and says why");
    ut_check(mp_bridge_statement_fingerprint() == 0u && stand_in_census_calls() == 0u,
             "and a question from anywhere else answers 0 until the hold is gone");
    mp_bridge_statement_hold(false);
}

static uint32_t check_the_first_question_makes_it(void)
{
    mp_mod_manifest_t own;
    uint32_t          first;

    ut_section("the first question makes the statement, once");

    stand_in_set_damage(true, true, DAMAGE);
    stand_in_set_foreign(OUTSIDE, 2u, 2u, true);
    first = mp_bridge_statement_fingerprint();
    ut_checkf(first != 0u && stand_in_census_calls() == 1u,
              "after the init it answers a number (%08X) and takes the census once",
              (unsigned)first);
    ut_check(mp_bridge_statement_fingerprint() == first && stand_in_census_calls() == 1u,
             "a second question answers the same number without a second census");
    ut_check(mp_bridge_statement_own(&own) && own.damage == DAMAGE &&
                 own.roster == STAND_IN_ROSTER && own.count == 1u &&
                 own.mods[0].stamp == STAND_IN_BUILD_STAMP &&
                 mp_mod_manifest_fingerprint(&own) == first,
             "the statement holds the damage table, the roster and the build, and folds to it");
    return first;
}

static void check_each_transport_gets_it(uint32_t made)
{
    mp_bridge_state_t *st = stand_in_state();
    mp_mod_manifest_t  own;

    ut_section("each transport is handed the one statement");

    /* The transports here are a lobby's, which the menu puts up. */
    stand_in_set_armed_by_menu(true);
    (void)mp_bridge_statement_own(&own);
    memset(st, 0, sizeof *st);
    st->mode = MP_BRIDGE_UDP_HOST;
    mp_bridge_statement_learn(false);
    ut_checkf(st->content_decided && st->content == made,
              "a host's arming decides its content as the statement's number (%08X)",
              (unsigned)st->content);
    ut_check(handed_the_statement(stand_in_host(), &own) &&
                 stand_in_host()->judge == mp_bridge_content_judge,
             "and hands its session the statement's bytes and the judge");
    ut_check(stand_in_census_calls() == 1u, "with no second census");
    ut_check(stand_in_log_has("the content fingerprint was made earlier in this process") &&
                 stand_in_log_has("known before the lobby: yes") &&
                 stand_in_log_has("the LAN announce carries"),
             "the line says it was made earlier, known before the lobby, and names the announce");
    ut_check(stand_in_log_has("every DLL outside this release it names against [multiplayer] "
                              "AllowMods"),
             "and that the host holds the DLLs outside this release against its list");
    ut_check(!stand_in_log_has("so hosting is not refused on this path"),
             "and a host the menu put up is not told what a host of NetRole is told");

    memset(st, 0, sizeof *st);
    st->mode = MP_BRIDGE_UDP_CLIENT;
    st->connect_pending = true;
    mp_bridge_statement_learn(false);
    ut_check(st->content_decided && st->content == made &&
                 handed_the_statement(stand_in_client(), &own) && stand_in_census_calls() == 1u,
             "a second transport, a client, gets the same number and statement, still one census");
    ut_check(stand_in_connects() == 0u && st->connect_pending,
             "and the arming never connects: a lobby's join is the lobby's to begin");
    ut_check(stand_in_log_has("2 DLL(s) outside this release (1 named)"),
             "the client's line counts the DLLs outside this release and the ones it named");

    st->content = 0u;
    mp_bridge_statement_learn(true);
    ut_check(st->content == 0u, "a transport that has decided does not learn again");

    memset(st, 0, sizeof *st);
    st->mode = MP_BRIDGE_LOOPBACK;
    mp_bridge_statement_learn(false);
    ut_check(!st->content_decided, "and the loopback states nothing");
}

/* The list behind the mods: the census's two DLLs, one named, the long one counted. */
static void check_the_list_behind_the_mods(void)
{
    mp_mod_manifest_t own;
    mp_mod_foreign_t  foreign;
    mp_session_t     *host = stand_in_host();
    unsigned          count = 0u;
    unsigned          named = 0u;
    char              first[80];

    ut_section("behind the mods, the DLLs outside this release");

    (void)mp_bridge_statement_own(&own);
    mp_mod_foreign_decode(host->statement, host->statement_bytes,
                          MP_MOD_MANIFEST_HEAD_BYTES +
                              (size_t)own.count * MP_MOD_MANIFEST_MOD_BYTES,
                          &foreign);
    ut_checkf(host->statement_bytes == 42u && foreign.form == MP_MOD_FOREIGN_SOUND &&
                  foreign.judged == 1u && foreign.count == 2u && foreign.stated == 1u &&
                  strcmp(foreign.names[0], "fps_counter_hud.dll") == 0,
              "42 bytes: the mods, then two DLLs counted and fps_counter_hud.dll named (%u)",
              (unsigned)host->statement_bytes);
    ut_check(mp_bridge_statement_foreign(&count, &named, first, sizeof first) && count == 2u &&
                 named == 1u && strcmp(first, OUTSIDE[1]) == 0,
             "the statement remembers the first it could not name, for a refusal that asks");
}

/* A session that does not take the statement leaves a host judging nobody and a join admitted
 * unjudged, and neither may read like a success. */
static void check_a_statement_the_session_refuses(void)
{
    mp_bridge_state_t *st = stand_in_state();

    ut_section("a statement the session does not take is a warning, never a success");

    stand_in_set_statement_taken(false);
    stand_in_log_clear();
    memset(st, 0, sizeof *st);
    st->mode = MP_BRIDGE_UDP_HOST;
    mp_bridge_statement_learn(false);
    ut_check(stand_in_log_has("was not taken by the session") &&
                 stand_in_log_has("this host judges no join") &&
                 !stand_in_log_has("hosting with content fingerprint"),
             "a host says it judges no join, and not that it hosts with a judge");
    stand_in_log_clear();
    memset(st, 0, sizeof *st);
    st->mode = MP_BRIDGE_UDP_CLIENT;
    mp_bridge_statement_learn(false);
    ut_check(stand_in_log_has("this join states nothing and a host admits it unjudged") &&
                 !stand_in_log_has("joining with content fingerprint"),
             "a client says its join states nothing, and not what it states");
    stand_in_set_statement_taken(true);
}

int main(void)
{
    uint32_t made;

    check_before_the_shot_init();
    check_while_the_dll_installs();
    made = check_the_first_question_makes_it();
    check_each_transport_gets_it(made);
    check_the_list_behind_the_mods();
    check_a_statement_the_session_refuses();

    return ut_summary("mp_bridge_statement");
}
