/* mp_quest_relay.c: who owns the story, and how often it is said.
 *
 * The header carries the rule. What is here is the cadence, the level guard and the counters that
 * make a silence tell them apart from a session where nothing happened.
 */
#include "mp_quest_relay.h"

#include "mp_enemy_spawn.h"
#include "mp_scratch_bind.h"
#include "mp_trust.h"

#include "common/logging.h"

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <string.h>

typedef struct mp_quest_relay {
    bool     installed;
    bool     host;

    /* The story as this side last agreed it: what a host last sent, or what a client last heard
     * with its own accepted claims folded in. Unknown until the first of either, and forgotten
     * when the level changes, because a bit index means a different item in a different level's
     * bank only in the sense that the bank itself has been rewritten under it. */
    mp_quest_set_t truth;
    bool           truth_known;
    uint16_t       level;
    bool           level_known;

    uint32_t last_sent_substep;
    bool     ever_sent;

    uint32_t states_sent;
    uint32_t states_unsent;
    uint32_t states_taken;
    uint32_t states_elsewhere;   /* a note about a level this side is not in */
    uint32_t states_refused;     /* a client's description arriving at the host */
    uint32_t bank_writes;        /* times the bank actually moved because of one */

    uint32_t claims_sent;
    uint32_t claims_unsent;      /* the channel was full; the local copy was NOT moved */
    uint32_t claims_taken;
    uint32_t claims_refused;     /* a claim arriving at a client, or about another level */
    uint32_t claims_torn;
    uint32_t claims_checked;     /* a client's claim in a deathmatch, which has no story */

    uint32_t read_faults;
} mp_quest_relay_t;

static mp_quest_relay_t quest;

bool mp_quest_relay_install(void)
{
    if (quest.installed) {
        return true;
    }
    if (!mp_scratch_bind_installed()) {
        log_warning("the shared story cannot travel: the campaign bank is not bound, so quest "
                    "items and keys stay on the machine that picked them up");
        return false;
    }
    memset(&quest, 0, sizeof quest);
    quest.installed = true;
    log_info("the shared story is bound: bits %u to %u of the campaign bank (%u quest item and "
             "key bits) belong to the host and are repeated to every client",
             (unsigned)MP_QUEST_FIRST_BIT, (unsigned)MP_QUEST_LAST_BIT,
             (unsigned)MP_QUEST_BIT_COUNT);
    return true;
}

void mp_quest_relay_set_host(bool host)
{
    quest.host = host;
}

/* The level this side is in, and whether it has changed since the last tick. A change forgets the
 * truth: the bank has been rewritten wholesale under us by a load, a restart or a restore, and a
 * client that went on writing the old story over the new bank would undo the level's own opening
 * state. The host simply describes what it now has. */
static bool level_is_steady(uint16_t *out)
{
    uint16_t now = 0;

    if (!mp_enemy_spawn_level_identity(&now)) {
        quest.level_known = false;
        quest.truth_known = false;
        return false;
    }
    if (!quest.level_known || quest.level != now) {
        quest.level_known = true;
        quest.level       = now;
        quest.truth_known = false;   /* say it again from what the bank now holds */
    }
    *out = now;
    return true;
}

/* ==============================================================================================
 * The host: what is, repeated.
 * ============================================================================================ */

static void describe(uint32_t substep, mp_quest_relay_send_fn_t send, uint16_t level)
{
    mp_quest_set_t live;
    uint8_t        note[MP_QUEST_STATE_BYTES];
    size_t         bytes;
    bool           moved;

    if (!mp_scratch_bind_read_quest(&live)) {
        ++quest.read_faults;
        return;
    }
    moved = !quest.truth_known || !mp_quest_equal(&quest.truth, &live);
    if (!moved && quest.ever_sent &&
        (uint32_t)(substep - quest.last_sent_substep) < MP_QUEST_REPEAT_SUBSTEPS) {
        return;
    }
    if (moved && quest.truth_known) {
        uint32_t at = mp_quest_first_difference(&quest.truth, &live, 0u);

        log_info("the shared story moved: bit %u is now %s, %u of %u item and key bits are held",
                 (unsigned)(MP_QUEST_FIRST_BIT + at), mp_quest_get(&live, at) ? "SET" : "clear",
                 (unsigned)mp_quest_count(&live), (unsigned)MP_QUEST_BIT_COUNT);
    }
    bytes = mp_quest_encode_state(&live, level, note, sizeof note);
    if (bytes == 0u) {
        ++quest.read_faults;
        return;
    }
    if (send == NULL || !send(note, bytes)) {
        ++quest.states_unsent;
        /* And it backs off rather than hammering. Without this the next substep finds the truth
         * still unknown, builds the same note and asks again, thirty two times a second, and every
         * one of those attempts is a chance to win a slot from a savegame transfer that needs it
         * more. The note is absolute and repeated, so waiting a beat costs nothing at all. */
        quest.last_sent_substep = substep;
        quest.ever_sent         = true;
        return;
    }
    ++quest.states_sent;
    quest.truth             = live;
    quest.truth_known       = true;
    quest.ever_sent         = true;
    quest.last_sent_substep = substep;
}

/* ==============================================================================================
 * The client: what the host said, written down, and what moved here, offered.
 * ============================================================================================ */

static void propose_and_apply(mp_quest_relay_send_fn_t send, uint16_t level)
{
    mp_quest_set_t live;
    uint32_t       at    = 0u;
    uint32_t       sent  = 0u;
    bool           wrote = false;

    if (!quest.truth_known) {
        return;   /* nothing has been said yet; there is no truth to measure against */
    }
    if (!mp_scratch_bind_read_quest(&live)) {
        ++quest.read_faults;
        return;
    }

    /* Everything this side changed since the last agreement, offered one bit at a time and capped,
     * so a bank that was just rewritten cannot fill the reliable channel in one substep. */
    while (sent < MP_QUEST_CLAIMS_PER_SUBSTEP) {
        uint8_t note[MP_QUEST_CLAIM_BYTES];
        size_t  bytes;
        bool    value;

        at = mp_quest_first_difference(&quest.truth, &live, at);
        if (at >= MP_QUEST_BIT_COUNT) {
            break;
        }
        value = mp_quest_get(&live, at);
        bytes = mp_quest_encode_claim(at, value, level, note, sizeof note);
        if (bytes == 0u || send == NULL || !send(note, bytes)) {
            ++quest.claims_unsent;
            break;   /* the channel is full; try again next substep and do NOT believe it here */
        }
        ++quest.claims_sent;
        ++sent;
        /* Believed here as well, so the bit does not flicker for a round trip and the same claim
         * is not sent again every substep. The host's next repeat corrects it if it disagreed. */
        mp_quest_put(&quest.truth, at, value);
        log_info("this side changed quest bit %u to %s and has told the host, which decides",
                 (unsigned)(MP_QUEST_FIRST_BIT + at), value ? "SET" : "clear");
        ++at;
    }

    /* And the story back over this side's bank, whole. This is the line that makes the two
     * machines one campaign, and it runs whether or not anything was claimed. */
    if (mp_scratch_bind_write_quest(&quest.truth, &wrote) && wrote) {
        ++quest.bank_writes;
    }
}

void mp_quest_relay_tick(uint32_t substep, mp_quest_relay_send_fn_t send)
{
    uint16_t level = 0;

    if (!quest.installed || !level_is_steady(&level)) {
        return;
    }
    if (quest.host) {
        describe(substep, send, level);
    } else {
        propose_and_apply(send, level);
    }
}

/* ==============================================================================================
 * The wire.
 * ============================================================================================ */

static bool take_state(const uint8_t *note, size_t bytes)
{
    mp_quest_set_t heard;
    uint16_t       said = 0;
    bool           wrote = false;

    if (!mp_quest_decode_state(note, bytes, &heard, &said)) {
        return false;
    }
    ++quest.states_taken;
    if (!quest.installed) {
        return true;
    }
    if (quest.host) {
        ++quest.states_refused;   /* a peer does not get to say what the story is */
        return true;
    }
    if (!quest.level_known || said != quest.level) {
        ++quest.states_elsewhere;
        return true;
    }
    quest.truth       = heard;
    quest.truth_known = true;
    if (mp_scratch_bind_write_quest(&quest.truth, &wrote) && wrote) {
        ++quest.bank_writes;
        log_info("the host's story was written into this side's campaign bank: %u of %u item and "
                 "key bits are held", (unsigned)mp_quest_count(&quest.truth),
                 (unsigned)MP_QUEST_BIT_COUNT);
    }
    return true;
}

static bool take_claim(const uint8_t *note, size_t bytes)
{
    mp_quest_set_t live;
    uint32_t       index = 0;
    uint16_t       said  = 0;
    bool           value = false;
    bool           wrote = false;

    if (!mp_quest_is_claim(note, bytes)) {
        return false;
    }
    ++quest.claims_taken;
    if (!quest.installed) {
        return true;
    }
    if (!mp_quest_decode_claim(note, bytes, &index, &value, &said)) {
        ++quest.claims_torn;
        return true;
    }
    if (!quest.host || !quest.level_known || said != quest.level) {
        ++quest.claims_refused;   /* a client does not decide, and a level is not another level */
        return true;
    }
    if (mp_trust_checking()) {
        ++quest.claims_checked;   /* a deathmatch has no story for a client to add to */
        return true;
    }
    if (!mp_scratch_bind_read_quest(&live)) {
        ++quest.read_faults;
        return true;
    }
    if (mp_quest_get(&live, index) == value) {
        return true;   /* the host already agrees; the next repeat tells the claimant so */
    }
    mp_quest_put(&live, index, value);
    if (mp_scratch_bind_write_quest(&live, &wrote) && wrote) {
        ++quest.bank_writes;
        log_info("a client changed quest bit %u to %s and the host has taken it into the story",
                 (unsigned)(MP_QUEST_FIRST_BIT + index), value ? "SET" : "clear");
    }
    return true;
}

bool mp_quest_relay_take_message(const uint8_t *note, size_t bytes)
{
    return take_state(note, bytes) || take_claim(note, bytes);
}

/* How to read the two lines. The words saying nothing has been agreed yet, on a client that has
 * been in a level for a while, are the failure this module can have: the host is not sending, or
 * its notes are about another level. No writes into this side's bank on a client whose host
 * holds items is the other one; and a claim count held back by a full channel beside a story
 * that is not moving is a change believed here and told to nobody. */
void mp_quest_relay_report(void)
{
    mp_quest_set_t live;
    uint32_t       quest_writes = 0;

    if (!quest.installed) {
        log_info("  the shared story: NOT RUNNING, so a quest item one player picks up stays on "
                 "that player's machine");
        return;
    }
    mp_scratch_bind_counters(NULL, NULL, NULL, &quest_writes);
    log_info("  the shared story (%s): %u of %u bits held here%s | sent %u state(s), %u refused by "
             "a full channel | taken %u state(s), %u about another level, %u refused on role",
             quest.host ? "the host, which decides" : "a client, which is told",
             mp_scratch_bind_read_quest(&live) ? (unsigned)mp_quest_count(&live) : 0u,
             (unsigned)MP_QUEST_BIT_COUNT,
             quest.truth_known ? "" : ", and NOTHING HAS BEEN AGREED YET",
             (unsigned)quest.states_sent, (unsigned)quest.states_unsent,
             (unsigned)quest.states_taken, (unsigned)quest.states_elsewhere,
             (unsigned)quest.states_refused);
    log_info("  the shared story's claims: %u sent, %u held back by a full channel | %u taken, %u "
             "refused, %u refused in a deathmatch, %u torn | %u write(s) into this side's "
             "bank, %u read fault(s)",
             (unsigned)quest.claims_sent, (unsigned)quest.claims_unsent,
             (unsigned)quest.claims_taken, (unsigned)quest.claims_refused,
             (unsigned)quest.claims_checked, (unsigned)quest.claims_torn,
             (unsigned)quest_writes, (unsigned)quest.read_faults);
}
