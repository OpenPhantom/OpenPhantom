/* mp_bridge_statement.c: this side's statement, made once per process, handed to each transport.
 * See the header.
 *
 * Moved out of mp_bridge.c, where it was learn_content, at the seam that file's size note named: it
 * reads the sessions and the bridge's state and none of the pump's three halves.
 */
#include "mp_bridge_statement.h"

#include "mp_bridge.h"
#include "mp_bridge_content.h"
#include "mp_bridge_shared.h"
#include "mp_content.h"
#include "mp_mod_allow.h"
#include "mp_mod_census.h"
#include "mp_mod_manifest_rule.h"
#include "mp_session.h"
#include "mp_world_values.h"

#include "common/character_profile.h"
#include "common/logging.h"
#include "common/text.h"

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <string.h>

/* How many substeps the damage table is asked for before a build that cannot read it goes on
 * without it. A substep runs only inside a level, long after the shot module's init; a build whose
 * shot table did not resolve answers nothing for good, and after a second of that the join goes on
 * with the roster and the builds alone rather than never joining. */
#define STATEMENT_TRIES 32u

/* A list of names as a log line prints it. */
#define NAMES_BYTES 256u

/* This process's statement. Its parts do not change while the process lives: damage.txt is read
 * once at startup, the roster once, and no DLL is loaded or unloaded after the loader. So it is
 * made once, by whichever asks first, and every transport and every reader gets the same one. */
static struct {
    bool              made;
    bool              held;       /* the DLL is installing and the loader may still be loading */
    bool              seen;       /* the damage table was damage.txt's when it was made */
    bool              in_substep; /* made from a substep, which is the ini path's first chance */
    mp_mod_manifest_t manifest;
    uint8_t           bytes[MP_MOD_STATEMENT_MAX_BYTES];
    size_t            length;
    /* The DLLs outside this release, as the census holds them, and which the list named. */
    const char       *foreign[MP_MOD_CENSUS_FOREIGN_MAX];
    bool              stated[MP_MOD_CENSUS_FOREIGN_MAX];
    size_t            listed;
    unsigned          count;
    unsigned          named;
} own;

bool mp_bridge_statement_own(mp_mod_manifest_t *out)
{
    if (out == NULL || !own.made) {
        return false;
    }
    *out = own.manifest;
    return true;
}

void mp_bridge_statement_hold(bool hold)
{
    own.held = hold;
}

bool mp_bridge_statement_foreign(unsigned *count, unsigned *named, char *first_unnamed,
                                 size_t capacity)
{
    size_t i;

    if (!own.made) {
        return false;
    }
    if (count != NULL) {
        *count = own.count;
    }
    if (named != NULL) {
        *named = own.named;
    }
    if (first_unnamed == NULL) {
        return true;
    }
    (void)text_format(first_unnamed, capacity, "%s", "");
    for (i = 0; i < own.listed; ++i) {
        if (!own.stated[i]) {
            (void)text_format(first_unnamed, capacity, "%s", own.foreign[i]);
            break;
        }
    }
    return true;
}

/* The damage table when the shot module's init has put damage.txt into it, else 0. A join
 * compares a 0 like any other value: a build whose shot table did not resolve differs from one
 * whose did. */
static bool damage_table(uint32_t *damage)
{
    bool seen = false;

    *damage = 0u;
    if (!mp_world_values_damage_table(damage, &seen) || !seen) {
        *damage = 0u;
        return false;
    }
    return true;
}

/* The statement and its bytes: the mods, and behind them the list of DLLs outside this release,
 * which remembers which names it could state, for a refusal that says the list was cut. */
static void make_statement(uint32_t damage, bool seen, bool in_substep)
{
    bool   judged = false;
    size_t base;
    size_t i;

    memset(&own.manifest, 0, sizeof own.manifest);
    own.manifest.damage = damage;
    own.manifest.roster = mp_content_roster_fingerprint();
    mp_mod_census_required_builds(&own.manifest);
    own.listed = mp_mod_census_foreign_names(own.foreign, MP_MOD_CENSUS_FOREIGN_MAX, &own.count,
                                             &judged);
    base = mp_mod_manifest_encode(&own.manifest, own.bytes, sizeof own.bytes);
    own.length = base == 0u ? 0u
                            : base + mp_mod_foreign_encode(own.foreign, own.listed, own.count,
                                                           judged, own.bytes + base,
                                                           sizeof own.bytes - base, own.stated);
    own.named = 0u;
    for (i = 0; i < own.listed; ++i) {
        own.named += own.stated[i] ? 1u : 0u;
    }
    own.seen       = seen;
    own.in_substep = in_substep;
    own.made       = true;
}

uint32_t mp_bridge_statement_fingerprint(void)
{
    uint32_t damage = 0u;

    if (!own.made) {
        if (own.held || !damage_table(&damage)) {
            return 0u;   /* before the shot module's init: nothing this side could say yet */
        }
        make_statement(damage, true, false);
    }
    return mp_mod_manifest_fingerprint(&own.manifest);
}

/* Whether the session took the statement. A statement it did not take is no statement: a host
 * without one judges nobody, and a request without one is admitted unjudged, so the success line
 * is not written then. */
static bool taken_by(mp_session_t *session, const char *so)
{
    if (own.length != 0u && mp_session_set_statement(session, own.bytes, own.length)) {
        return true;
    }
    log_warning("this side's statement of %u byte(s) was not taken by the session (room %u), so %s",
                (unsigned)own.length, (unsigned)MP_SESSION_STATEMENT_BYTES, so);
    return false;
}

/* A host armed from NetRole or OBI_NET_ROLE stood before any menu, so nothing refused it; it says
 * so, with the DLLs that would have. Its joiners are judged as at every host. */
static void say_the_ini_host(void)
{
    char listed[NAMES_BYTES];

    log_warning("the transport of NetRole or OBI_NET_ROLE stands before any menu, so hosting is "
                "not refused on this path; DLLs outside this release here: %s; its joiners are "
                "judged as at every host",
                mp_mod_foreign_list(own.foreign, own.count, listed, sizeof listed));
}

static void hand_to_the_session(const mp_bridge_shared_t *b, bool in_substep)
{
    mp_bridge_state_t *st = b->state;

    if (st->mode == MP_BRIDGE_UDP_HOST) {
        mp_session_set_judge(b->host, mp_bridge_content_judge);
        (void)mp_mod_allow_read();
        if (taken_by(b->host, "this host judges no join")) {
            log_info("hosting with content fingerprint %08X from %s: a join request's statement "
                     "is judged against this side's, the game data and %u required mod(s), and "
                     "every DLL outside this release it names against [multiplayer] AllowMods%s",
                     (unsigned)st->content, in_substep ? "a substep" : "the arming",
                     (unsigned)own.manifest.count,
                     in_substep ? "; a request that arrived before it was admitted unjudged" : "");
        }
        /* Asked of who put the transport up, not of when the statement was made: a host of the
         * menu whose shot table did not show its init makes it in a substep as well, and the
         * menu did judge that host before its transport went up. */
        if (!mp_bridge_armed_by_menu()) {
            say_the_ini_host();
        }
        return;
    }
    if (taken_by(b->client, "this join states nothing and a host admits it unjudged")) {
        log_info("joining with content fingerprint %08X: the request states the game data, %u "
                 "required mod(s) and %u DLL(s) outside this release (%u named)",
                 (unsigned)st->content, (unsigned)own.manifest.count, own.count, own.named);
    }
    /* The ini path's join waits for this. A lobby's join is begun by the lobby, which waits for
     * the relay's seat as well, so nothing connects from the arming. */
    if (in_substep && st->connect_pending &&
        mp_session_peer(b->client, 0)->state == MP_PEER_FREE) {
        st->connect_pending = false;
        mp_session_connect(b->client, st->host_endpoint);
    }
}

/* What this transport was handed, and where it came from: made now, at the arming or in a
 * substep, or made earlier in the process and reused. */
static void say_the_statement(bool made_now, bool in_substep, const mp_bridge_state_t *st)
{
    if (!made_now) {
        log_info("the content fingerprint was made earlier in this process (by the join screen, "
                 "or for an earlier transport): damage table %08X, roster %08X, shot init seen: %s",
                 (unsigned)own.manifest.damage, (unsigned)own.manifest.roster,
                 own.seen ? "yes" : "no");
    } else if (in_substep) {
        log_info("the content fingerprint is taken in substep %u: damage table %08X, roster %08X, "
                 "shot init seen: %s", (unsigned)st->substep, (unsigned)own.manifest.damage,
                 (unsigned)own.manifest.roster, own.seen ? "yes" : "no");
    } else {
        log_info("the content fingerprint is taken at the arming: damage table %08X, roster %08X, "
                 "shot init seen: %s", (unsigned)own.manifest.damage,
                 (unsigned)own.manifest.roster, own.seen ? "yes" : "no");
    }
    log_info("the game data of this build: damage.txt %08X, characters.ini %08X (%u profiles), "
             "known before the lobby: %s", (unsigned)own.manifest.damage,
             (unsigned)own.manifest.roster, (unsigned)character_profile_count(),
             own.seen && !own.in_substep ? "yes" : "no");
    if (st->mode == MP_BRIDGE_UDP_HOST) {
        log_info("from here on, this side's content note 0x93 goes out as soon as a peer is "
                 "connected, in the lobby as in a level, and the LAN announce carries %08X",
                 (unsigned)st->content);
    } else {
        log_info("from here on, this side's content note 0x93 goes out as soon as the host is "
                 "connected, in the lobby as in a level");
    }
}

void mp_bridge_statement_learn(bool in_substep)
{
    const mp_bridge_shared_t *b  = mp_bridge_shared();
    mp_bridge_state_t        *st = b->state;
    uint32_t                  damage = 0u;
    bool                      seen;
    bool                      made_now = false;

    if (st->mode == MP_BRIDGE_LOOPBACK || st->content_decided) {
        return;
    }
    if (!own.made) {
        if (own.held) {
            log_info("the content fingerprint is not taken while this DLL installs: the loader "
                     "may still be loading other mods; it is taken from the first substep");
            return;
        }
        seen = damage_table(&damage);
        if (!seen && !in_substep) {
            log_info("the content fingerprint is not taken at the arming: the shot module's init "
                     "has not run yet, so the damage table is still the executable's; it is taken "
                     "from the first substep");
            return;
        }
        if (!seen && ++st->content_tries < STATEMENT_TRIES) {
            return;
        }
        make_statement(damage, seen, in_substep);
        made_now = true;
    }
    st->content         = mp_mod_manifest_fingerprint(&own.manifest);
    st->content_decided = true;
    say_the_statement(made_now, in_substep, st);
    hand_to_the_session(b, in_substep);
}
