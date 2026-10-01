/* movie_rule.c: who decides when a movie ends, driven over everything it can be given.
 *
 * The rule has one job outside a session, which is to change nothing, and three inside one: the
 * host's movie ends as a player ends it but not on a lost foreground or the close box, a client's
 * cannot be ended by the client, and a client's ends when the host's payload count has grown on the
 * connection the movie began on. Every combination of the gate's four answers is held against a
 * reference written out case by case, and the verdict against the orderings that matter.
 *
 * The waiting line is checked here too: it is the one other pure piece of holding a movie.
 */
#include "unittest.h"

#include "movie_rule.h"
#include "movie_text.h"

#include "common/language.h"
#include "common/movie_note.h"

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <string.h>

static movie_gate_t gate_of(bool running, bool client, bool connected, bool moving)
{
    movie_gate_t gate;

    memset(&gate, 0, sizeof gate);
    gate.running        = running;
    gate.client         = client;
    gate.host_connected = connected;
    gate.host_moving    = moving;
    gate.connection     = connected ? 0x1122334455667788ull : 0u;
    gate.host_payloads  = 100u;
    gate.published      = 7u;
    return gate;
}

/* The decision written the long way, one sentence per case. */
static movie_role_t reference_role(bool read, bool running, bool client, bool connected,
                                   bool moving)
{
    if (!read || !running) {
        return MOVIE_ROLE_FREE;       /* no session: the movie player as it always was */
    }
    if (!client) {
        return MOVIE_ROLE_HOST;       /* the host plays and ends its own */
    }
    if (!connected) {
        return MOVIE_ROLE_ALONE;      /* a client with no host: nobody could end it for it */
    }
    if (moving) {
        return MOVIE_ROLE_SKIP;       /* the host is already in its level */
    }
    return MOVIE_ROLE_LOCKED;
}

static void check_every_gate(void)
{
    unsigned bits;
    unsigned locked = 0u;
    unsigned skipped = 0u;

    ut_section("the role of a movie, over every gate the multiplayer can file");

    for (bits = 0u; bits < 32u; ++bits) {
        bool         read      = (bits & 1u) != 0u;
        bool         running   = (bits & 2u) != 0u;
        bool         client    = (bits & 4u) != 0u;
        bool         connected = (bits & 8u) != 0u;
        bool         moving    = (bits & 16u) != 0u;
        movie_gate_t gate      = gate_of(running, client, connected, moving);
        movie_role_t role      = movie_rule_role(read, &gate);

        ut_checkf(role == reference_role(read, running, client, connected, moving),
                  "read %u, running %u, client %u, connected %u, moving %u: role %d",
                  (unsigned)read, (unsigned)running, (unsigned)client, (unsigned)connected,
                  (unsigned)moving, (int)role);
        locked += role == MOVIE_ROLE_LOCKED ? 1u : 0u;
        skipped += role == MOVIE_ROLE_SKIP ? 1u : 0u;
    }
    ut_checkf(locked == 1u && skipped == 1u,
              "exactly one gate holds a movie and one skips it (%u and %u)", locked, skipped);

    ut_check(movie_rule_role(false, NULL) == MOVIE_ROLE_FREE, "no gate at all is no session");
    {
        movie_gate_t gate = gate_of(true, true, true, false);

        gate.connection = 0u;
        ut_check(movie_rule_role(true, &gate) == MOVIE_ROLE_ALONE,
                 "a client that says connected on connection nought has no host to wait for");
    }
}

static void check_the_ways_out(void)
{
    movie_exits_t free_exits   = movie_rule_exits(MOVIE_ROLE_FREE);
    movie_exits_t alone_exits  = movie_rule_exits(MOVIE_ROLE_ALONE);
    movie_exits_t host_exits   = movie_rule_exits(MOVIE_ROLE_HOST);
    movie_exits_t locked_exits = movie_rule_exits(MOVIE_ROLE_LOCKED);

    ut_section("which ways out end a movie");

    ut_check(free_exits.escape_ends && free_exits.focus_ends && free_exits.close_ends &&
             !free_exits.pump_session,
             "without a session Escape, a lost foreground and the close box end it, and nothing "
             "is pumped: the movie player exactly as it shipped");
    ut_check(alone_exits.escape_ends && alone_exits.focus_ends && alone_exits.close_ends &&
             alone_exits.pump_session,
             "a client with no host ends it the same way, and pumps the session");
    ut_check(host_exits.escape_ends && !host_exits.focus_ends && !host_exits.close_ends &&
             host_exits.pump_session,
             "the host ends it by Escape and never by a lost foreground or the close box");
    ut_check(!locked_exits.escape_ends && !locked_exits.focus_ends && !locked_exits.close_ends &&
             locked_exits.pump_session, "a held client ends it by none of the three");
}

static void check_the_verdict(void)
{
    movie_baseline_t begin;
    movie_gate_t     now = gate_of(true, true, true, false);

    begin.connection    = now.connection;
    begin.host_payloads = 100u;

    ut_section("a held movie ends when the host's count grows on the connection it began on");

    ut_check(movie_rule_verdict(&begin, true, &now, 0u) == MOVIE_VERDICT_GO_ON,
             "the same count on the same connection goes on");
    now.host_payloads = 101u;
    ut_check(movie_rule_verdict(&begin, true, &now, 0u) == MOVIE_VERDICT_HOST_DONE,
             "one payload more is the host's end");
    now.host_payloads = 99u;
    ut_check(movie_rule_verdict(&begin, true, &now, 0u) == MOVIE_VERDICT_GO_ON,
             "a count that went backwards is not the host moving");
    begin.host_payloads = 0xFFFFFFFFu;
    now.host_payloads   = 2u;
    ut_check(movie_rule_verdict(&begin, true, &now, 0u) == MOVIE_VERDICT_HOST_DONE,
             "and one that wrapped still is");
    begin.host_payloads = 100u;
    now.host_payloads   = 150u;
    now.connection      = 0x0BADu;
    ut_check(movie_rule_verdict(&begin, true, &now, 0u) == MOVIE_VERDICT_OTHER_CONNECTION,
             "payloads on another connection are not the host of this movie ending it");

    ut_section("and lets the player go when the session does");

    now = gate_of(true, true, true, false);
    now.running = false;
    ut_check(movie_rule_verdict(&begin, true, &now, 0u) == MOVIE_VERDICT_NOT_RUNNING,
             "a session that stopped running lets it go");
    now = gate_of(true, false, false, false);
    ut_check(movie_rule_verdict(&begin, true, &now, 0u) == MOVIE_VERDICT_NOT_RUNNING,
             "and so does this machine no longer being a client");
    now = gate_of(true, true, false, false);
    ut_check(movie_rule_verdict(&begin, true, &now, 0u) == MOVIE_VERDICT_NOT_CONNECTED,
             "a host that is gone lets it go");
    /* What the multiplayer files once the connection has dropped: nought, whatever came before. */
    now = gate_of(true, true, false, false);
    now.host_payloads = 101u;
    ut_check(now.connection == 0u, "a dropped connection is filed as nought");
    ut_check(movie_rule_verdict(&begin, true, &now, 0u) == MOVIE_VERDICT_NOT_CONNECTED,
             "and such a gate lets the player go, whatever the count did before the drop");

    ut_section("a gate nobody files any more is noticed, a torn read is not a decision");

    now = gate_of(true, true, true, false);
    ut_check(movie_rule_verdict(&begin, false, &now, MOVIE_RULE_QUIET_MS - 1u) ==
             MOVIE_VERDICT_GO_ON, "a read that failed decides nothing on its own");
    ut_check(movie_rule_verdict(&begin, false, &now, MOVIE_RULE_QUIET_MS) == MOVIE_VERDICT_QUIET,
             "three seconds of failed reads is a gate nobody files");
    ut_check(movie_rule_verdict(&begin, true, &now, MOVIE_RULE_QUIET_MS) == MOVIE_VERDICT_QUIET,
             "and so is three seconds of the same publication count");
    now.host_payloads = 101u;
    ut_check(movie_rule_verdict(&begin, true, &now, MOVIE_RULE_QUIET_MS) ==
             MOVIE_VERDICT_HOST_DONE, "the host's end is still the host's end on a quiet gate");
}

static void check_the_quiet_over_turns(void)
{
    uint32_t quiet = 0u;
    unsigned turn;

    ut_section("the quiet counts pumped turns, and a stall counts as one turn");

    ut_check(movie_rule_quiet_after(2500u, true, 40u) == 0u,
             "a gate that moved in the turn starts the quiet again");
    ut_check(movie_rule_quiet_after(0u, false, 1u) == 1u, "a turn of a millisecond adds one");
    ut_check(movie_rule_quiet_after(0u, false, 10000u) == MOVIE_RULE_TURN_CAP_MS,
             "a stall of ten seconds adds no more than one capped turn");
    ut_check(movie_rule_quiet_after(UINT32_MAX - 5u, false, 50u) == UINT32_MAX,
             "and the quiet never wraps back to nothing");

    quiet = movie_rule_quiet_after(quiet, false, 60000u);   /* a whole minute's mode switch */
    for (turn = 0u; turn < 28u; ++turn) {
        quiet = movie_rule_quiet_after(quiet, false, MOVIE_RULE_TURN_CAP_MS);
    }
    ut_checkf(quiet < MOVIE_RULE_QUIET_MS,
              "a minute's stall and 28 turns of a tenth of a second are %u ms of quiet, not yet "
              "three seconds", (unsigned)quiet);
    quiet = movie_rule_quiet_after(quiet, false, MOVIE_RULE_TURN_CAP_MS);
    ut_checkf(quiet >= MOVIE_RULE_QUIET_MS, "one turn more makes it %u", (unsigned)quiet);
}

static movie_verdict_t never_asked(void)
{
    return MOVIE_VERDICT_GO_ON;
}

static void check_letting_go(void)
{
    movie_loop_t loop;

    ut_section("a held movie the session lets go is this side's own for the rest of it");

    memset(&loop, 0, sizeof loop);
    loop.exits         = movie_rule_exits(MOVIE_ROLE_LOCKED);
    loop.poll          = &never_asked;
    loop.held_for_host = true;
    movie_rule_let_go(&loop);
    ut_check(loop.poll == NULL, "it asks the host nothing more");
    ut_check(loop.exits.escape_ends && loop.exits.focus_ends && loop.exits.close_ends,
             "and every way out ends it again");
    ut_check(loop.exits.pump_session, "while the session is still pumped");
    ut_check(loop.held_for_host, "and its keys were still spent while it was held");
}

static void check_the_waiting_line(void)
{
    unsigned language;

    ut_section("the waiting line, in the five languages the game shipped in");

    for (language = 0u; language < (unsigned)LANGUAGE_COUNT; ++language) {
        const char *text = movie_text_waiting((uint8_t)language);
        size_t      i;
        bool        printable = text != NULL && text[0] != '\0';

        for (i = 0; printable && text[i] != '\0'; ++i) {
            printable = text[i] >= 0x20 && text[i] <= 0x7E;
        }
        ut_checkf(printable, "language %u has its own printable ASCII line", language);
    }
    ut_check(strcmp(movie_text_waiting((uint8_t)LANGUAGE_DE), "Warte auf den Host") == 0,
             "the German one reads as the lobby's own");
    ut_check(movie_text_waiting((uint8_t)LANGUAGE_COUNT) ==
                 movie_text_waiting((uint8_t)LANGUAGE_EN),
             "a language past the five is shown in English");
}

int main(void)
{
    check_every_gate();
    check_the_ways_out();
    check_the_verdict();
    check_letting_go();
    check_the_quiet_over_turns();
    check_the_waiting_line();
    return ut_summary("movie_rule");
}
