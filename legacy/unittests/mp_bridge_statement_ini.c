/* mp_bridge_statement_ini.c: the ini path's statement, learned from its 32nd substep.
 *
 * The ini path arms its transport when the DLL starts, before the shot module's init. Its arming
 * then states nothing, and a build whose shot table never shows the init keeps asking for 31
 * substeps and makes the statement on the 32nd with a damage table of 0; its join waits for that
 * and begins there. Its own process, because the statement is made once per process and the other
 * test has made it from the first question. The line must say what was seen, not a fixed "yes".
 * A host on this path stood before any menu, so nothing refused it, and it says so. A host the menu
 * put up says nothing of the kind, even when its statement too waited for a substep: the menu
 * judged it before the transport went up.
 */
#include "unittest.h"

#include "mp_bridge_statement_stand_in.h"

#include "mp_bridge.h"
#include "mp_bridge_statement.h"
#include "mp_mod_manifest_rule.h"

#include <stdbool.h>
#include <stdint.h>
#include <string.h>

int main(void)
{
    mp_bridge_state_t *st = stand_in_state();
    mp_mod_manifest_t  own;
    unsigned           substep;

    ut_section("the ini path learns from its 32nd substep");

    stand_in_set_damage(true, false, 0x22222222u);
    memset(st, 0, sizeof *st);
    st->mode            = MP_BRIDGE_UDP_CLIENT;
    st->connect_pending = true;
    st->host_endpoint   = 7u;

    mp_bridge_statement_learn(false);
    ut_check(!st->content_decided && !mp_bridge_statement_own(&own) &&
                 stand_in_census_calls() == 0u,
             "the arming before the init states nothing and takes no census");
    ut_check(stand_in_log_has("is not taken at the arming"), "and says so");

    for (substep = 1u; substep <= 31u; ++substep) {
        st->substep = substep;
        mp_bridge_statement_learn(true);
    }
    ut_check(!st->content_decided && stand_in_census_calls() == 0u && stand_in_connects() == 0u,
             "31 substeps without the init state nothing, and the join waits");

    st->substep = 32u;
    mp_bridge_statement_learn(true);
    ut_check(st->content_decided && mp_bridge_statement_own(&own) && own.damage == 0u &&
                 st->content == mp_mod_manifest_fingerprint(&own) &&
                 stand_in_census_calls() == 1u,
             "the 32nd makes the statement with a damage table of 0, and one census");
    ut_check(stand_in_connects() == 1u && !st->connect_pending,
             "and the join begins from that substep");
    ut_check(stand_in_log_has("taken in substep 32") && stand_in_log_has("shot init seen: no") &&
                 stand_in_log_has("known before the lobby: no"),
             "the line says the substep, that the init was not seen, and not before the lobby");
    ut_check(!stand_in_log_has("LAN announce"), "and a client names no announce");

    ut_section("a host on the ini path says hosting was not refused there");
    stand_in_set_armed_by_menu(false);
    memset(st, 0, sizeof *st);
    st->mode    = MP_BRIDGE_UDP_HOST;
    st->substep = 33u;
    mp_bridge_statement_learn(true);
    ut_check(stand_in_log_has("so hosting is not refused on this path") &&
                 stand_in_log_has("a request that arrived before it was admitted unjudged"),
             "the warning, and the line that a request before the statement went unjudged");

    ut_section("a host the menu put up is not told so, though its statement waited too");
    stand_in_log_clear();
    stand_in_set_armed_by_menu(true);
    memset(st, 0, sizeof *st);
    st->mode    = MP_BRIDGE_UDP_HOST;
    st->substep = 34u;
    mp_bridge_statement_learn(true);
    ut_check(stand_in_log_has("hosting with content fingerprint") &&
                 !stand_in_log_has("so hosting is not refused on this path"),
             "a host of the menu whose statement came from a substep hosts without that warning");
    ut_check(stand_in_log_has("a request that arrived before it was admitted unjudged"),
             "and still says that a request before the statement went unjudged");

    return ut_summary("mp_bridge_statement_ini");
}
