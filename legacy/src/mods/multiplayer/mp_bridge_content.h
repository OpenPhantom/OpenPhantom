/* mp_bridge_content.h: how the lobby drives the fingerprint comparison, internal to the bridge.
 *
 * The three calls other modules make, taking the far side's note, settling this side's own and
 * asking whether the two disagree, are declared in mp_bridge_lobby.h, where they have always been.
 */
#ifndef MULTIPLAYER_MP_BRIDGE_CONTENT_H
#define MULTIPLAYER_MP_BRIDGE_CONTENT_H

#include "mp_mod_manifest_rule.h"
#include "mp_session.h"

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

/* The host's judge of a join request (mp_session_judge_fn): the two statements decoded and judged
 * by mp_mod_manifest_judge against this build's list of required mods, and when they agree, the
 * list of DLLs outside this release behind the joiner's mods by mp_mod_foreign_judge against
 * [multiplayer] AllowMods as it was last read (mp_mod_allow_list). A refusal carries a detail with
 * the file's, the mod's or the DLL's name and the host's release number, and the first eight
 * refusals of the process are said as a line, the rest counted: the judge runs before the cookie
 * has proven a sender. A request let through with DLLs the list names is said the same way, under
 * a bound of its own, so requests that pass never crowd out the refusals' lines. A statement that
 * does not decode is admitted unjudged and counted. */
uint8_t mp_bridge_content_judge(const uint8_t *own, size_t own_bytes, const uint8_t *far,
                                size_t far_bytes, uint8_t *detail, size_t capacity,
                                size_t *detail_bytes);

/* What the judge has done in this process. It counts requests, not joins: a client repeats its
 * request until the challenge comes, and every repeat is judged. */
typedef struct mp_bridge_content_judge_counts {
    uint32_t judged;
    uint32_t admitted;
    uint32_t unparsed;           /* a statement that did not decode, admitted unjudged */
    uint32_t damage;             /* refused for damage.txt */
    uint32_t roster;             /* refused for characters.ini */
    uint32_t missing_at_host;
    uint32_t missing_at_joiner;
    uint32_t other_build;
    uint32_t not_allowed;        /* refused for a DLL outside this release the list does not name */
    uint32_t not_named;          /* refused for more such DLLs than the request named */
    uint32_t no_list;            /* refused for no readable list of them */
    uint32_t said;               /* refusals written as a line */
    uint32_t unsaid;             /* refusals past those, only counted */
    uint32_t let_through;        /* admitted with DLLs outside this release, each one listed */
    uint32_t let_through_said;   /* of those, written as a line */
    uint32_t unjudging;          /* admitted from a build that judged nothing of its own */
} mp_bridge_content_judge_counts_t;

/* The counts, as the report prints them. */
void mp_bridge_content_judge_counts(mp_bridge_content_judge_counts_t *out);

/* Why this client's last join was refused, for the screen: true when the host refused it for its
 * game data (MP_DENY_CONTENT), a required mod (MP_DENY_MODS) or a DLL outside this release
 * (MP_DENY_FOREIGN_DLL), with what the host's detail said and this side's own values beside it;
 * see mp_mod_refusal_t for what each field holds under each reason. A deathmatch host that sends a
 * client away for its content sends no detail, and then only the reason is filled. False, with
 * everything zero, for any other answer and on a host. */
bool mp_bridge_content_refusal(mp_mod_refusal_t *out);

/* The two sessions the note travels on, and which side this is. From mp_bridge_lobby_bind. */
void mp_bridge_content_bind(mp_session_t *host, mp_session_t *client, bool is_client);

/* Settles this side's fingerprint once it exists. From the lobby's tick, in a lobby and a level. */
void mp_bridge_content_tick(uint32_t content, bool is_host);

/* Forgets both fingerprints and the verdict, for a new session. From mp_bridge_lobby_reset. */
void mp_bridge_content_reset(void);

/* A peer has just arrived: this side's fingerprint goes out again, to everybody. From the
 * lobby's join. */
void mp_bridge_content_note_join(void);

/* The report line. From mp_bridge_lobby_report. */
void mp_bridge_content_report(void);

#endif /* MULTIPLAYER_MP_BRIDGE_CONTENT_H */
