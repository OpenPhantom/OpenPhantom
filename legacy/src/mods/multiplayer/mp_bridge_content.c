/* mp_bridge_content.c: what two machines play with, judged at the join and compared again over
 * the content note.
 *
 * The join: a client's request states the game data, the builds of the required mods and the DLLs
 * outside this release it has loaded, and the host's judge compares the first two with its own and
 * holds the third against its [multiplayer] AllowMods, and refuses with the file, the mod or the
 * DLL. The note 0x93: each side sends the game data and the builds folded into one number as soon
 * as it has one and a peer is connected, over the reliable channel, and compares what it hears
 * against what it has. The statement stands from the arming, so the note goes out in the lobby
 * already, and a difference found there ends the session in the lobby: a client that finds one
 * leaves; a host names it and stays, and in a deathmatch sends that client away. The fingerprint
 * folds exactly what the judge compares of the two, so a join the judge admitted is never ended by
 * the note; the note is for joins nobody judged (the ini path before its first substep, a dedicated
 * server in between). It stays for as long as a field run has not shown that the join catches all
 * it did.
 *
 * SIZE NOTE: over 600 lines. The judge answers for three things, the game data, the required mods
 * and the DLLs outside this release, on the host's side and in the client's reading of a refusal.
 * The seam is the host's judge with its counts and its lines, which shares nothing with the
 * content note but the rule module and would move to a file of its own.
 */
#include "mp_bridge_lobby.h"
#include "mp_bridge_content.h"

#include "mp_bridge_statement.h"
#include "mp_lobby.h"
#include "mp_mod_allow.h"
#include "mp_mod_census.h"
#include "mp_mod_manifest_rule.h"
#include "mp_session.h"
#include "mp_trust.h"
#include "mp_wire.h"

#include "common/logging.h"
#include "common/text.h"

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

/* The rule module sits below the session and repeats the session's numbers for its three reasons;
 * this file includes both and holds them to each other. */
_Static_assert(MP_MOD_REFUSE_GAME_DATA == (unsigned)MP_DENY_CONTENT,
               "the judge's reason for game data is no longer the session's MP_DENY_CONTENT");
_Static_assert(MP_MOD_REFUSE_MODS == (unsigned)MP_DENY_MODS,
               "the judge's reason for a mod is no longer the session's MP_DENY_MODS");
_Static_assert(MP_MOD_REFUSE_FOREIGN == (unsigned)MP_DENY_FOREIGN_DLL,
               "the judge's reason for a DLL is no longer the session's MP_DENY_FOREIGN_DLL");
_Static_assert(MP_MOD_MANIFEST_MAX_BYTES <= MP_SESSION_STATEMENT_BYTES,
               "a full statement no longer fits the room a request has for it");
_Static_assert(MP_MOD_STATEMENT_MAX_BYTES == MP_SESSION_STATEMENT_BYTES,
               "the rule's limit for a whole statement is no longer the room a request has");
_Static_assert(MP_MOD_REFUSAL_DETAIL_BYTES <= MP_SESSION_DENY_DETAIL_BYTES,
               "a refusal's detail no longer fits the room a denial has for it");

/* How many refusals of the judge are written as a line in one process. The judge runs on a
 * request, before the cookie has proven its sender, so a flood of forged requests would otherwise
 * cost a warning line each where it used to cost one digest; past this, a refusal is counted and
 * the report says how many went unsaid. */
#define REFUSALS_SAID_MAX 8u

/* The same for requests let through with DLLs the list names, under a bound of its own: a client
 * repeats its request until the challenge comes, and requests that pass must not use up the lines
 * a refusal needs. */
#define LET_THROUGH_SAID_MAX 8u

/* A list of names as a log line prints it, and one file name as long as the system writes it. */
#define NAMES_BYTES     256u
#define FILE_NAME_BYTES 260u

/* What the host's judge has done in this process, for the report. */
static mp_bridge_content_judge_counts_t judged;

typedef struct content_state {
    mp_session_t *host;
    mp_session_t *client;
    bool          is_client;

    uint32_t own;
    uint32_t far_side;
    bool     sent;
    bool     mismatch;

    /* A host's clients each say their own. Kept with the connection it came on, so the next
     * player on the same peer index is not judged by the one before. */
    uint32_t peer_content[MP_SESSION_MAX_PEERS];
    uint64_t peer_connection[MP_SESSION_MAX_PEERS];
    uint32_t sent_away;

    /* A client's refusals it has already said, by the session's count of them. */
    uint32_t refusals_said;
} content_state_t;

static content_state_t cs;

void mp_bridge_content_bind(mp_session_t *host, mp_session_t *client, bool is_client)
{
    cs.host      = host;
    cs.client    = client;
    cs.is_client = is_client;
}

static void copy_text(char *out, size_t capacity, const char *text)
{
    size_t length = 0;

    while (text != NULL && text[length] != '\0' && length + 1u < capacity) {
        out[length] = text[length];
        ++length;
    }
    out[length] = '\0';
}

/* The name a refusal carries: the data file, or the required mod out of this build's own table,
 * or the DLL outside this release as the request named it. The joining side reads it as text,
 * because its own table may be shorter. */
static const char *refused_name(const mp_mod_verdict_t *verdict, const mp_mod_foreign_t *foreign,
                                const mp_mod_required_t *table, size_t count)
{
    const char *name;

    if (verdict->reason == MP_MOD_REFUSE_FOREIGN) {
        return verdict->sub == MP_MOD_SUB_NOT_ALLOWED && verdict->mod < MP_MOD_FOREIGN_NAMES_MAX
                   ? foreign->names[verdict->mod]
                   : "";
    }
    if (verdict->mod == MP_MOD_NO_MOD) {
        return verdict->sub == MP_MOD_SUB_DAMAGE ? "damage.txt" : "characters.ini";
    }
    name = mp_mod_manifest_required_name(table, count, verdict->mod);
    return name != NULL ? name : "a required mod";
}

/* A refusal for a DLL outside this release, as the host says it. */
static void say_foreign_refusal(const mp_mod_verdict_t *verdict, const mp_mod_foreign_t *foreign,
                                const char *name)
{
    if (verdict->sub == MP_MOD_SUB_NOT_ALLOWED) {
        log_warning("a join was refused: %s is loaded there, is not of this release and "
                    "[multiplayer] AllowMods does not name it", name);
    } else if (verdict->sub == MP_MOD_SUB_NOT_NAMED) {
        log_warning("a join was refused: %u DLL(s) outside this release are loaded there and %u "
                    "are named, so the rest cannot be held against [multiplayer] AllowMods",
                    (unsigned)foreign->count, (unsigned)foreign->stated);
    } else {
        log_warning("a join was refused: the statement carries no readable list of DLLs outside "
                    "this release (%s), which a build like this one always sends",
                    foreign->form == MP_MOD_FOREIGN_ABSENT ? "none" : "malformed");
    }
}

static void say_host_refusal(const mp_mod_verdict_t *verdict, const mp_mod_foreign_t *foreign,
                             const char *name)
{
    char there[48];
    char here[48];

    if (judged.said >= REFUSALS_SAID_MAX) {
        ++judged.unsaid;
        return;
    }
    ++judged.said;
    if (verdict->reason == MP_MOD_REFUSE_FOREIGN) {
        say_foreign_refusal(verdict, foreign, name);
        return;
    }
    if (verdict->reason == MP_MOD_REFUSE_GAME_DATA) {
        log_warning("a join was refused: %s differs, %08X there and %08X here", name,
                    (unsigned)verdict->joiner_stamp, (unsigned)verdict->host_stamp);
        return;
    }
    if (verdict->sub == MP_MOD_SUB_MISSING_AT_JOINER) {
        log_warning("a join was refused: %s is missing there", name);
        return;
    }
    if (verdict->sub == MP_MOD_SUB_MISSING_AT_HOST) {
        log_warning("a join was refused: %s is required and missing here", name);
        return;
    }
    mp_mod_census_describe_stamp(verdict->joiner_stamp, there, sizeof there);
    mp_mod_census_describe_stamp(verdict->host_stamp, here, sizeof here);
    log_warning("a join was refused: %s is built %s (%08X, image %08X) there and %s (%08X, image "
                "%08X) here", name, there, (unsigned)verdict->joiner_stamp,
                (unsigned)verdict->joiner_image, here, (unsigned)verdict->host_stamp,
                (unsigned)verdict->host_image);
}

static void count_refusal(const mp_mod_verdict_t *verdict)
{
    if (verdict->reason == MP_MOD_REFUSE_GAME_DATA) {
        ++*(verdict->sub == MP_MOD_SUB_DAMAGE ? &judged.damage : &judged.roster);
    } else if (verdict->reason == MP_MOD_REFUSE_FOREIGN) {
        ++*(verdict->sub == MP_MOD_SUB_NOT_ALLOWED ? &judged.not_allowed
            : verdict->sub == MP_MOD_SUB_NOT_NAMED ? &judged.not_named
                                                   : &judged.no_list);
    } else if (verdict->sub == MP_MOD_SUB_MISSING_AT_HOST) {
        ++judged.missing_at_host;
    } else if (verdict->sub == MP_MOD_SUB_MISSING_AT_JOINER) {
        ++judged.missing_at_joiner;
    } else {
        ++judged.other_build;
    }
}

/* An admitted request, and when it carries DLLs outside this release, which ones the list let
 * through. */
static void admit(const mp_mod_foreign_t *foreign)
{
    const char *names[MP_MOD_FOREIGN_NAMES_MAX];
    char        listed[NAMES_BYTES];
    uint8_t     i;

    ++judged.admitted;
    if (foreign->judged == 0u) {
        ++judged.unjudging;
        return;
    }
    if (foreign->count == 0u) {
        return;
    }
    ++judged.let_through;
    if (judged.let_through_said >= LET_THROUGH_SAID_MAX) {
        return;
    }
    ++judged.let_through_said;
    for (i = 0; i < foreign->stated && i < MP_MOD_FOREIGN_NAMES_MAX; ++i) {
        names[i] = foreign->names[i];
    }
    log_info("a join request was let through with %u DLL(s) outside this release, each named in "
             "[multiplayer] AllowMods: %s", (unsigned)foreign->count,
             mp_mod_foreign_list(names, i, listed, sizeof listed));
}

uint8_t mp_bridge_content_judge(const uint8_t *own, size_t own_bytes, const uint8_t *far,
                                size_t far_bytes, uint8_t *detail, size_t capacity,
                                size_t *detail_bytes)
{
    size_t                   count = 0;
    const mp_mod_required_t *table = mp_mod_manifest_required(&count);
    mp_mod_manifest_t        host;
    mp_mod_manifest_t        joiner;
    mp_mod_verdict_t         verdict;
    mp_mod_foreign_t         foreign;
    char                     version[MP_MOD_REFUSAL_VERSION_MAX];
    uint32_t                 stamp = 0u;
    uint32_t                 image = 0u;
    const char              *name;

    *detail_bytes = 0u;
    ++judged.judged;
    if (!mp_mod_manifest_decode(own, own_bytes, &host) ||
        !mp_mod_manifest_decode(far, far_bytes, &joiner)) {
        ++judged.unparsed;
        return MP_MOD_REFUSE_NONE;
    }
    memset(&foreign, 0, sizeof foreign);
    mp_mod_manifest_judge(&host, &joiner, table, count, &verdict);
    /* The DLLs outside this release only once the builds agree, so a joiner of another build is
     * told that first, and never about a list its build may write in another form. */
    if (verdict.reason == MP_MOD_REFUSE_NONE) {
        mp_mod_foreign_decode(far, far_bytes,
                              MP_MOD_MANIFEST_HEAD_BYTES +
                                  (size_t)joiner.count * MP_MOD_MANIFEST_MOD_BYTES,
                              &foreign);
        mp_mod_foreign_judge(&foreign, mp_mod_allow_list(), &verdict);
    }
    if (verdict.reason == MP_MOD_REFUSE_NONE) {
        admit(&foreign);
        return MP_MOD_REFUSE_NONE;
    }
    count_refusal(&verdict);
    name = refused_name(&verdict, &foreign, table, count);
    version[0] = '\0';
    if (verdict.reason == MP_MOD_REFUSE_FOREIGN) {
        (void)mp_mod_census_build_of(MP_WIRE_MOD_MULTIPLAYER, &stamp, &image, version,
                                     sizeof version);
    } else if (verdict.mod != MP_MOD_NO_MOD) {
        (void)mp_mod_census_build_of(verdict.mod, &stamp, &image, version, sizeof version);
    }
    say_host_refusal(&verdict, &foreign, name);
    *detail_bytes = mp_mod_refusal_detail_encode(&verdict, name, version, detail, capacity);
    return verdict.reason;
}

/* This side's own of a DLL outside this release the host refused: under NOT_ALLOWED the one the
 * host named, under NOT_NAMED the first this side's request could not name; its stamp and, for
 * another release of this project, its number, out of this side's census by name. */
static void here_of_foreign(mp_mod_refusal_t *out)
{
    char     name[FILE_NAME_BYTES];
    unsigned count = 0u;
    unsigned named = 0u;

    copy_text(name, sizeof name, out->sub == MP_MOD_SUB_NOT_ALLOWED ? out->mod : "");
    if (out->sub == MP_MOD_SUB_NOT_NAMED) {
        (void)mp_bridge_statement_foreign(&count, &named, name, sizeof name);
    }
    copy_text(out->mod, sizeof out->mod, name);
    if (name[0] != '\0') {
        (void)mp_mod_census_foreign_by_name(name, &out->here_stamp, out->here_version,
                                            sizeof out->here_version);
    }
}

bool mp_bridge_content_refusal(mp_mod_refusal_t *out)
{
    const uint8_t    *bytes = NULL;
    size_t            length;
    mp_mod_verdict_t  verdict;
    mp_mod_manifest_t mine;
    char              name[MP_MOD_REFUSAL_NAME_MAX];
    char              version[MP_MOD_REFUSAL_VERSION_MAX];
    mp_deny_reason_t  reason;
    uint32_t          image = 0u;

    if (out == NULL) {
        return false;
    }
    memset(out, 0, sizeof *out);
    if (!cs.is_client || cs.client == NULL) {
        return false;
    }
    reason = mp_session_last_deny(cs.client);
    if (reason != MP_DENY_CONTENT && reason != MP_DENY_MODS && reason != MP_DENY_FOREIGN_DLL) {
        return false;
    }
    out->reason = (uint8_t)reason;
    length = mp_session_last_deny_detail(cs.client, &bytes);
    memset(&verdict, 0, sizeof verdict);
    if (!mp_mod_refusal_detail_decode(bytes, length, &verdict, name, version)) {
        return true;   /* a deathmatch host's sending away, which carries no detail */
    }
    out->sub        = verdict.sub;
    out->host_stamp = verdict.host_stamp;
    copy_text(out->mod, sizeof out->mod, name);
    copy_text(out->host_version, sizeof out->host_version, version);
    if (reason == MP_DENY_FOREIGN_DLL) {
        here_of_foreign(out);
    } else if (reason == MP_DENY_MODS) {
        (void)mp_mod_census_build_of(verdict.mod, &out->here_stamp, &image, out->here_version,
                                     sizeof out->here_version);
    } else if (mp_bridge_statement_own(&mine)) {
        out->here_stamp = verdict.sub == MP_MOD_SUB_DAMAGE ? mine.damage : mine.roster;
    }
    return true;
}

/* A client's line for a refusal for a DLL outside this release. */
static void say_client_foreign(const mp_mod_refusal_t *refusal)
{
    char     built[48];
    unsigned count = 0u;
    unsigned named = 0u;

    if (refusal->sub == MP_MOD_SUB_NOT_ALLOWED) {
        mp_mod_census_describe_stamp(refusal->here_stamp, built, sizeof built);
        log_warning("the host refused this join: %s is loaded on this side and the host does not "
                    "allow it; this side has it %s%s, built %s (%08X)", refusal->mod,
                    refusal->here_version[0] != '\0' ? "as release " : "with no release number",
                    refusal->here_version, built, (unsigned)refusal->here_stamp);
    } else if (refusal->sub == MP_MOD_SUB_NOT_NAMED) {
        (void)mp_bridge_statement_foreign(&count, &named, NULL, 0u);
        log_warning("the host refused this join: this side has %u DLL(s) outside this release and "
                    "the request named %u; the first not named is %s", count, named,
                    refusal->mod[0] != '\0' ? refusal->mod : "unknown here");
    } else {
        log_warning("the host refused this join: this side's statement carried no readable list "
                    "of DLLs outside this release");
    }
}

/* A client says each refusal of its join once, with what the host's detail named. */
static void say_client_refusal(void)
{
    mp_mod_refusal_t refusal;
    uint32_t         refusals;
    char             host_built[48];
    char             here_built[48];

    if (!cs.is_client || cs.client == NULL) {
        return;
    }
    refusals = mp_session_denied(cs.client);
    if (refusals == cs.refusals_said) {
        return;
    }
    cs.refusals_said = refusals;
    if (!mp_bridge_content_refusal(&refusal) || refusal.sub == 0u) {
        return;
    }
    if (refusal.reason == (uint8_t)MP_DENY_FOREIGN_DLL) {
        say_client_foreign(&refusal);
        return;
    }
    if (refusal.reason == (uint8_t)MP_DENY_CONTENT) {
        log_warning("the host refused this join: %s differs, the host has %08X, this side %08X",
                    refusal.mod, (unsigned)refusal.host_stamp, (unsigned)refusal.here_stamp);
        return;
    }
    mp_mod_census_describe_stamp(refusal.host_stamp, host_built, sizeof host_built);
    mp_mod_census_describe_stamp(refusal.here_stamp, here_built, sizeof here_built);
    if (refusal.sub == MP_MOD_SUB_MISSING_AT_JOINER) {
        log_warning("the host refused this join: %s is missing on this side; the host has %s "
                    "built %s (%08X)", refusal.mod, refusal.host_version, host_built,
                    (unsigned)refusal.host_stamp);
    } else if (refusal.sub == MP_MOD_SUB_MISSING_AT_HOST) {
        log_warning("the host refused this join: %s is missing at the host; this side has %s "
                    "built %s (%08X)", refusal.mod, refusal.here_version, here_built,
                    (unsigned)refusal.here_stamp);
    } else {
        log_warning("the host refused this join: %s is another build, the host has %s built %s "
                    "(%08X), this side %s built %s (%08X)", refusal.mod, refusal.host_version,
                    host_built, (unsigned)refusal.host_stamp, refusal.here_version, here_built,
                    (unsigned)refusal.here_stamp);
    }
}

/* It was missing entirely once. mp_bridge_lobby_content_settled was written, tested and given a
 * screen text, and no line called it: a search of the tree found only its definition and its
 * declaration. So both sides reported '00000000' against '00000000' for good and the check the
 * whole move behind the level load was made for did not exist.
 *
 * The gate asks two things. A fingerprint this side has not settled yet is settled; one that
 * is settled but has not gone out to anybody, because nobody was connected when it came to be,
 * is tried again, and a join asks for that too. Once it is out, the gate keeps it from
 * re-entering on every frame. */
void mp_bridge_content_tick(uint32_t content, bool is_host)
{
    say_client_refusal();
    if (content != 0u && (content != cs.own || !cs.sent)) {
        mp_bridge_lobby_content_settled(content, is_host);
    }
}

/* A host keeps what each client said, against the connection it said it on. */
static void remember(size_t peer_index, uint32_t fingerprint)
{
    const mp_peer_t *peer;

    if (cs.is_client || cs.host == NULL || peer_index >= MP_SESSION_MAX_PEERS) {
        return;
    }
    peer = mp_session_peer(cs.host, peer_index);
    if (peer == NULL || peer->state != MP_PEER_CONNECTED) {
        return;
    }
    cs.peer_content[peer_index]    = fingerprint;
    cs.peer_connection[peer_index] = peer->connection_id;
}

/* A host's answer to a client whose content differs from its own, once both are known: in a
 * deathmatch it is sent away with the reason. In a co-op game it is named, and goes by itself. */
static void judge(size_t peer_index)
{
    const mp_peer_t *peer;
    uint32_t         said;

    if (!mp_trust_checking() || cs.is_client || cs.host == NULL || cs.own == 0u ||
        peer_index >= MP_SESSION_MAX_PEERS) {
        return;
    }
    said = cs.peer_content[peer_index];
    if (said == 0u || said == cs.own) {
        return;
    }
    cs.peer_content[peer_index] = 0u;
    peer = mp_session_peer(cs.host, peer_index);
    if (peer == NULL || peer->state != MP_PEER_CONNECTED ||
        peer->connection_id != cs.peer_connection[peer_index]) {
        return;   /* somebody else holds the index now, and has not said */
    }
    if (mp_session_drop(cs.host, peer_index, MP_DENY_CONTENT)) {
        ++cs.sent_away;
        log_warning("the client on peer %u plays with content %08X against this host's %08X and "
                    "is sent away: a deathmatch does not take a stranger's word for the "
                    "damage table", (unsigned)peer_index, (unsigned)said, (unsigned)cs.own);
    }
}

bool mp_bridge_lobby_take_content(size_t peer_index, const uint8_t *note, size_t bytes)
{
    uint32_t far = 0;

    if (!mp_lobby_is_content(note, bytes)) {
        return false;
    }
    if (!mp_lobby_content_decode(note, bytes, &far)) {
        return true;
    }
    cs.far_side = far;
    remember(peer_index, far);
    if (cs.own != 0u && far != cs.own) {
        cs.mismatch = true;
        log_warning("the far side plays with different content: %08X against this build's %08X: "
                    "the join was not judged by this statement (the ini path before its first "
                    "substep, a dedicated server in between, or a build of another age)",
                    (unsigned)far, (unsigned)cs.own);
        if (cs.is_client) {
            log_warning("leaving the session: two machines that disagree about the damage table "
                        "would disagree about every shot");
            mp_bridge_lobby_leave();
        } else {
            judge(peer_index);
        }
    }
    return true;
}

void mp_bridge_lobby_content_settled(uint32_t fingerprint, bool is_host)
{
    uint8_t       note[MP_LOBBY_CONTENT_BYTES];
    mp_session_t *session = is_host ? cs.host : cs.client;

    if (fingerprint == 0u || session == NULL) {
        return;
    }
    cs.own = fingerprint;
    if (!cs.sent) {
        if (mp_lobby_content_encode(fingerprint, note, sizeof note) == MP_LOBBY_CONTENT_BYTES &&
            mp_session_broadcast_reliable(session, note, sizeof note) != 0u) {
            cs.sent = true;
        }
    }
    /* The far side may have spoken first, while this one still had no level. */
    if (cs.far_side != 0u && cs.far_side != fingerprint && !cs.mismatch) {
        cs.mismatch = true;
        log_warning("the far side plays with different content: %08X against this build's %08X",
                    (unsigned)cs.far_side, (unsigned)fingerprint);
        if (!is_host) {
            mp_bridge_lobby_leave();
        }
    }
    /* And on a host, every client that spoke first, each against what it said. */
    if (is_host) {
        size_t i;

        for (i = 0; i < MP_SESSION_MAX_PEERS; ++i) {
            judge(i);
        }
    }
}

bool mp_bridge_lobby_content_mismatch(void)
{
    return cs.mismatch;
}

/* A peer that has just arrived was not there when the note went out, so it goes out again. It
 * used to go once, to whoever was connected at that moment: a host that had loaded a level
 * before its client told nobody, and a client that came later never heard it. */
void mp_bridge_content_note_join(void)
{
    cs.sent = false;
}

void mp_bridge_content_reset(void)
{
    cs.own      = 0u;
    cs.far_side = 0u;
    cs.sent     = false;
    cs.mismatch = false;
    memset(cs.peer_content, 0, sizeof cs.peer_content);
    memset(cs.peer_connection, 0, sizeof cs.peer_connection);
    /* A refusal already said stays said; the next one to count is the next the session takes. */
    cs.refusals_said = cs.client != NULL ? mp_session_denied(cs.client) : 0u;
}

void mp_bridge_content_judge_counts(mp_bridge_content_judge_counts_t *out)
{
    if (out != NULL) {
        *out = judged;
    }
}

void mp_bridge_content_report(void)
{
    mp_mod_refusal_t                 refusal;
    mp_bridge_content_judge_counts_t counts;
    mp_mod_manifest_t                statement;
    char                             stated[16];

    mp_bridge_content_judge_counts(&counts);
    /* Read, never made here: a report that made the statement would take the census in the middle
     * of itself, on a transport that never needed one. */
    if (mp_bridge_statement_own(&statement)) {
        text_format(stated, sizeof stated, "%08X",
                    (unsigned)mp_mod_manifest_fingerprint(&statement));
    } else {
        text_format(stated, sizeof stated, "none made");
    }
    log_info("  the content check: this side %08X, the far side %08X%s; %u client(s) sent away "
             "for it", (unsigned)cs.own, (unsigned)cs.far_side, cs.mismatch ? "  MISMATCH" : "",
             (unsigned)cs.sent_away);
    log_info("  the join check (host): statement %s; %u request(s) judged, %u admitted, %u "
             "refused for game data (damage.txt %u, characters.ini %u), %u refused for a mod "
             "(missing at the host %u, missing at the joiner %u, another build %u), %u refusal(s) "
             "said, %u more counted; %u statement(s) that did not decode and were admitted "
             "unjudged", stated, (unsigned)counts.judged,
             (unsigned)counts.admitted, (unsigned)(counts.damage + counts.roster),
             (unsigned)counts.damage, (unsigned)counts.roster,
             (unsigned)(counts.missing_at_host + counts.missing_at_joiner + counts.other_build),
             (unsigned)counts.missing_at_host, (unsigned)counts.missing_at_joiner,
             (unsigned)counts.other_build, (unsigned)counts.said, (unsigned)counts.unsaid,
             (unsigned)counts.unparsed);
    log_info("  the join check (host), DLLs outside this release: %u request(s) refused (not "
             "allowed %u, more than named %u, no list %u), %u request(s) let through with one the "
             "list names, %u statement(s) from a build that judged nothing",
             (unsigned)(counts.not_allowed + counts.not_named + counts.no_list),
             (unsigned)counts.not_allowed, (unsigned)counts.not_named, (unsigned)counts.no_list,
             (unsigned)counts.let_through, (unsigned)counts.unjudging);
    if (mp_bridge_content_refusal(&refusal)) {
        log_info("  the join check (this client): refused for %s, reason %u, sub %u, %s: the host "
                 "%s %08X, this side %s %08X",
                 refusal.reason == (uint8_t)MP_DENY_MODS          ? "a mod"
                 : refusal.reason == (uint8_t)MP_DENY_FOREIGN_DLL ? "a DLL outside this release"
                                                                  : "the game data",
                 (unsigned)refusal.reason, (unsigned)refusal.sub,
                 refusal.mod[0] != '\0' ? refusal.mod : "(no detail)", refusal.host_version,
                 (unsigned)refusal.host_stamp, refusal.here_version, (unsigned)refusal.here_stamp);
    } else {
        log_info("  the join check (this client): %s",
                 cs.is_client ? "not refused for the game data, a mod or a DLL"
                              : "none, this is a host");
    }
    mp_mod_census_report();
    mp_mod_allow_report();
}
