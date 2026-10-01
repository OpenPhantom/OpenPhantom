/* The session list a player picks from, driven over its clock with no game and no socket.
 *
 * Every hard part of a server browser is a timing question, and a timing question answered in the
 * field costs a field run each time somebody changes a number. So the clock is a parameter here and
 * the answers are pinned: a lost datagram must not make a row disappear under the cursor, a
 * favourite must not become a second row when it answers, a row that ages out must not take the
 * selection somewhere else, and the order must not change while nothing changed.
 */
#include "unittest.h"

#include "mp_browser.h"
#include "mp_mod_manifest_rule.h"
#include "mp_settings.h"

#include "common/text.h"

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <string.h>

#define OUR_WIRE 14u
#define OUR_FP   0xABCD1234u

static size_t announce_bytes(uint8_t *buffer, const char *name, uint16_t game_port,
                             uint8_t players, uint8_t slots, uint8_t wire, uint32_t fingerprint)
{
    mp_announce_t announce;

    memset(&announce, 0, sizeof announce);
    announce.wire        = wire;
    announce.fingerprint = fingerprint;
    announce.game_port   = game_port;
    announce.players     = players;
    announce.slots       = slots;
    mp_announce_name_clean(name, announce.name);
    return mp_announce_encode(&announce, buffer, MP_ANNOUNCE_BYTES);
}

static bool hear(mp_browser_t *browser, const char *address, const char *name, uint16_t port,
                 uint8_t players, uint8_t slots)
{
    uint8_t buffer[MP_ANNOUNCE_BYTES];

    (void)announce_bytes(buffer, name, port, players, slots, OUR_WIRE, OUR_FP);
    return mp_browser_heard(browser, address, buffer, sizeof buffer);
}

static void check_a_row_appears_and_merges(void)
{
    mp_browser_t   browser;
    mp_settings_t  settings;

    ut_section("one endpoint is one row, whatever it is heard from");
    mp_browser_init(&browser, OUR_WIRE, OUR_FP);
    mp_browser_tick(&browser, 1000u);

    ut_check(hear(&browser, "192.168.1.42", "Naboo Hangar", 27960u, 2u, 4u), "an announce lands");
    ut_check(browser.rows == 1u, "and makes one row");

    ut_check(hear(&browser, "192.168.1.42", "Naboo Hangar", 27960u, 3u, 4u), "a second announce");
    ut_check(browser.rows == 1u,
             "does not make a second row: the endpoint is the identity, not the packet");
    ut_check(browser.row[0].announce.players == 3u, "and the numbers are the newest ones");

    ut_check(hear(&browser, "192.168.1.42", "Second host", 27961u, 1u, 4u), "another game port");
    ut_check(browser.rows == 2u,
             "IS a second row: two hosts on one machine differ in the game port and nowhere else");

    /* The same endpoint saved as a favourite must not become a row of its own. */
    mp_settings_default(&settings);
    ut_check(mp_settings_remember(&settings, "192.168.1.42", 27960u), "saved");
    mp_browser_set_favourites(&browser, &settings);
    ut_check(browser.rows == 2u,
             "a favourite that is already on the wire merges into the row that is there");
    ut_check(browser.row[0].favourite && browser.row[0].heard,
             "and that row is now both: the favourite gives it a place, the announce the numbers");
}

static void check_a_lost_datagram_does_not_remove_a_row(void)
{
    mp_browser_t browser;

    ut_section("a lost datagram is a lost packet, not a server that stopped");
    mp_browser_init(&browser, OUR_WIRE, OUR_FP);
    mp_browser_tick(&browser, 10000u);
    ut_check(hear(&browser, "10.0.0.5", "Test Rig", 27960u, 1u, 4u), "heard once");
    ut_check(mp_browser_row_live(&browser, &browser.row[0]), "and it is live");

    mp_browser_tick(&browser, 10000u + 2u * MP_BROWSER_CYCLE_MS);
    ut_check(browser.rows == 1u && mp_browser_row_live(&browser, &browser.row[0]),
             "two missed cycles change nothing: one dropped broadcast must be invisible");

    mp_browser_tick(&browser, 10000u + MP_BROWSER_STALE_CYCLES * MP_BROWSER_CYCLE_MS);
    ut_check(browser.rows == 1u, "at the stale threshold the row is still THERE");
    ut_check(!mp_browser_row_live(&browser, &browser.row[0]),
             "but no longer live, so the screen can grey it and freeze its numbers");
    ut_check(browser.row[0].announce.players == 1u,
             "and it keeps the last numbers rather than blanking them");

    mp_browser_tick(&browser, 10000u + MP_BROWSER_DROP_CYCLES * MP_BROWSER_CYCLE_MS);
    ut_check(browser.rows == 0u, "only at the drop threshold does it go");
}

static void check_a_favourite_never_goes_away(void)
{
    mp_browser_t  browser;
    mp_settings_t settings;

    ut_section("a favourite stays in the list whether or not it is up");
    mp_browser_init(&browser, OUR_WIRE, OUR_FP);
    mp_settings_default(&settings);
    ut_check(mp_settings_remember(&settings, "10.0.0.9", 27960u), "saved one that is not running");
    mp_browser_set_favourites(&browser, &settings);
    ut_check(browser.rows == 1u, "it is a row at once, before anything is heard");
    ut_check(!browser.row[0].heard, "with no numbers");

    mp_browser_tick(&browser, 1000u * 60u * 60u);
    ut_check(browser.rows == 1u,
             "and an hour later it is still there: a player who saved a server wants to see it");
    ut_check(!mp_browser_row_live(&browser, &browser.row[0]), "shown as not up");
}

static void check_the_typed_row(void)
{
    mp_browser_t browser;

    ut_section("what is in the address field is a row too, and only ever one");
    mp_browser_init(&browser, OUR_WIRE, OUR_FP);

    mp_browser_set_typed(&browser, "192.168.1.99", 27960u);
    ut_check(browser.rows == 1u && browser.row[0].typed, "a typed endpoint appears");

    mp_browser_set_typed(&browser, "192.168.1.98", 27960u);
    ut_check(browser.row[0].typed == false,
             "typing a different one unmarks the first, or every keystroke would leave a row");

    mp_browser_set_typed(&browser, "192.168", 27960u);
    ut_check(!browser.row[0].typed && !browser.row[1].typed,
             "a half typed address marks nothing: it is malformed for most of its life");

    mp_browser_tick(&browser, MP_BROWSER_DROP_CYCLES * MP_BROWSER_CYCLE_MS + 1u);
    ut_check(browser.rows == 0u,
             "and an abandoned typed row ages away like any other row nobody saved");
}

static void check_the_order(void)
{
    mp_browser_t browser;
    size_t       first;

    ut_section("live before stale, joinable before not, then by name");
    mp_browser_init(&browser, OUR_WIRE, OUR_FP);
    mp_browser_tick(&browser, 100000u);

    ut_check(hear(&browser, "10.0.0.2", "Zebra", 27960u, 1u, 4u), "a live joinable one, late name");
    ut_check(hear(&browser, "10.0.0.3", "Alpha", 27960u, 1u, 4u),
             "a live joinable one, early name");
    first = mp_browser_ordered(&browser, 0u);
    ut_check(first < MP_BROWSER_ROWS_MAX && strcmp(browser.row[first].announce.name, "Alpha") == 0,
             "between two equals the name settles it");

    /* A live server this build cannot join outranks a stale one: it is news either way, and the
     * stale row is only a memory. */
    {
        uint8_t buffer[MP_ANNOUNCE_BYTES];

        (void)announce_bytes(buffer, "AAA future", 27960u, 1u, 4u, OUR_WIRE + 1u, OUR_FP);
        ut_check(mp_browser_heard(&browser, "10.0.0.4", buffer, sizeof buffer),
                 "a live server on a newer wire version still makes a row");
    }
    first = mp_browser_ordered(&browser, 0u);
    ut_check(strcmp(browser.row[first].announce.name, "Alpha") == 0,
             "and it does NOT jump to the top just because its name sorts first: joinable wins "
             "inside the same liveness");
}

static void check_the_selection_survives_the_list_changing(void)
{
    mp_browser_t browser;
    const mp_browser_row_t *chosen;

    ut_section("the selection does not wander when the list moves under it");
    mp_browser_init(&browser, OUR_WIRE, OUR_FP);
    mp_browser_tick(&browser, 100000u);
    ut_check(hear(&browser, "10.0.0.2", "Alpha", 27960u, 1u, 4u), "three servers");
    ut_check(hear(&browser, "10.0.0.3", "Bravo", 27960u, 1u, 4u), "Bravo is heard");
    ut_check(hear(&browser, "10.0.0.4", "Charlie", 27960u, 1u, 4u), "and Charlie, the third");

    mp_browser_move_selection(&browser, 1);
    chosen = mp_browser_selected(&browser);
    ut_check(chosen != NULL && strcmp(chosen->announce.name, "Bravo") == 0,
             "one down from the top is the second row IN SCREEN ORDER, not in array order");

    /* Alpha keeps talking, the other two do not: the array order stays and the screen order does
     * not, and the selection has to follow the row rather than the position. */
    mp_browser_tick(&browser, 100000u + MP_BROWSER_CYCLE_MS);
    ut_check(hear(&browser, "10.0.0.2", "Alpha", 27960u, 2u, 4u), "Alpha is heard again");
    chosen = mp_browser_selected(&browser);
    ut_check(chosen != NULL && strcmp(chosen->announce.name, "Bravo") == 0,
             "and the selection is still on Bravo, not on whatever is now in that position");

    mp_browser_move_selection(&browser, -10);
    chosen = mp_browser_selected(&browser);
    ut_check(chosen != NULL && strcmp(chosen->announce.name, "Alpha") == 0,
             "a big move clamps at the top rather than wrapping");
    mp_browser_move_selection(&browser, 100);
    ut_check(mp_browser_selected(&browser) != NULL, "and at the bottom rather than running off it");
}

static void check_a_row_that_ages_out_takes_nothing_with_it(void)
{
    mp_browser_t browser;

    ut_section("a row that ages out leaves a valid selection behind");
    mp_browser_init(&browser, OUR_WIRE, OUR_FP);
    mp_browser_tick(&browser, 100000u);
    ut_check(hear(&browser, "10.0.0.2", "Alpha", 27960u, 1u, 4u), "two servers");
    ut_check(hear(&browser, "10.0.0.3", "Bravo", 27960u, 1u, 4u), "and Bravo, the second");
    mp_browser_move_selection(&browser, 1);

    mp_browser_tick(&browser, 100000u + MP_BROWSER_DROP_CYCLES * MP_BROWSER_CYCLE_MS);
    ut_check(browser.rows == 0u, "both age away");
    ut_check(mp_browser_selected(&browser) == NULL,
             "and an empty list answers nothing rather than a stale pointer");
    mp_browser_move_selection(&browser, 1);
    ut_check(mp_browser_selected(&browser) == NULL, "moving in an empty list is not a crash");
}

static void check_the_fingerprint_arrives_late(void)
{
    mp_browser_t browser;

    ut_section("a browser in the menu has no fingerprint, and must stop lying when it gets one");
    mp_browser_init(&browser, OUR_WIRE, 0u);
    mp_browser_tick(&browser, 1000u);
    ut_check(hear(&browser, "10.0.0.2", "Alpha", 27960u, 1u, 4u), "a server is heard");
    ut_check(!browser.row[0].joinable,
             "and is not joinable, because this side has no fingerprint yet");

    mp_browser_set_fingerprint(&browser, OUR_FP);
    ut_check(browser.row[0].joinable,
             "and the moment a level makes one true, the row already in the list is re-judged "
             "rather than keeping a verdict from a value that was not yet known");
}

/* Joinable means the same statement again. A host announces the fingerprint of its statement from
 * the moment its transport is armed, and the join screen judges the rows by the fingerprint of its
 * own statement, both through mp_mod_manifest_fingerprint. So the same game data and the same
 * builds are joinable, another build is not, and a browser still judging by 0, as the join screen
 * did before, calls a host with a statement one it cannot join. */
static void check_the_statement_decides_joinable(void)
{
    mp_mod_manifest_t host;
    mp_mod_manifest_t other;
    mp_browser_t      browser;
    uint8_t           buffer[MP_ANNOUNCE_BYTES];
    uint32_t          same;

    ut_section("a host's statement and this side's: the same is joinable, another build is not");
    memset(&host, 0, sizeof host);
    host.damage        = 0x84273DBBu;
    host.roster        = 0xE1D7140Du;
    host.count         = 1u;
    host.mods[0].id    = 0u;
    host.mods[0].stamp = 0x6ABB2222u;
    host.mods[0].image = 0x00A43000u;
    other = host;
    other.mods[0].stamp ^= 0x00010000u;
    same = mp_mod_manifest_fingerprint(&host);
    ut_check(same != 0u, "a statement with anything in it has a fingerprint");

    (void)announce_bytes(buffer, "Host", 27960u, 1u, 4u, OUR_WIRE, same);
    mp_browser_init(&browser, OUR_WIRE, 0u);
    mp_browser_tick(&browser, 1000u);
    ut_check(mp_browser_heard(&browser, "10.0.0.2", buffer, sizeof buffer) &&
                 !browser.row[0].joinable,
             "judged by 0, a host that announces its statement is not joinable");
    mp_browser_set_fingerprint(&browser, mp_mod_manifest_fingerprint(&host));
    ut_check(browser.row[0].joinable,
             "judged by the fingerprint of the same statement on this side, it is");
    mp_browser_set_fingerprint(&browser, mp_mod_manifest_fingerprint(&other));
    ut_check(!browser.row[0].joinable,
             "and by the statement of another build of the multiplayer, it is not again");
}

static void check_the_list_fills_rather_than_churns(void)
{
    mp_browser_t browser;
    char         address[MP_SETTINGS_ADDRESS_MAX];
    size_t       i;

    ut_section("a full list drops the newcomer instead of evicting a row somebody is looking at");
    mp_browser_init(&browser, OUR_WIRE, OUR_FP);
    mp_browser_tick(&browser, 1000u);
    for (i = 0; i < MP_BROWSER_ROWS_MAX + 4u; ++i) {
        (void)text_format(address, sizeof address, "10.0.%u.%u", (unsigned)(i / 250u),
                          (unsigned)(i % 250u) + 1u);
        (void)hear(&browser, address, "Server", 27960u, 1u, 4u);
    }
    ut_check(browser.rows == MP_BROWSER_ROWS_MAX, "the list fills to its maximum and stops");
    ut_check(browser.dropped_full == 4u,
             "and the four it could not take are counted rather than silently lost");
}

static void check_a_stray_datagram(void)
{
    mp_browser_t browser;
    uint8_t      junk[MP_ANNOUNCE_BYTES];

    ut_section("something else on the announce port makes no row");
    mp_browser_init(&browser, OUR_WIRE, OUR_FP);
    memset(junk, 0xEE, sizeof junk);
    ut_check(!mp_browser_heard(&browser, "10.0.0.2", junk, sizeof junk), "refused");
    ut_check(browser.rows == 0u && browser.refused == 1u, "and counted, not shown");
    ut_check(!mp_browser_heard(&browser, "", junk, sizeof junk), "a datagram from nowhere too");
}

int main(void)
{
    check_a_row_appears_and_merges();
    check_a_lost_datagram_does_not_remove_a_row();
    check_a_favourite_never_goes_away();
    check_the_typed_row();
    check_the_order();
    check_the_selection_survives_the_list_changing();
    check_a_row_that_ages_out_takes_nothing_with_it();
    check_the_fingerprint_arrives_late();
    check_the_statement_decides_joinable();
    check_the_list_fills_rather_than_churns();
    check_a_stray_datagram();

    return ut_summary("the session browser");
}
