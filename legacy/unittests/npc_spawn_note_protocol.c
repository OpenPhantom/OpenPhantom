/* npc_spawn_note_protocol.c: how the spawn contract compares serials and keeps its answers, and
 * three hundred random records through the channel and back.
 *
 * What would be silent if it were wrong: a comparison that calls a grant old when it is new, so
 * the multiplayer drops it before the overlay saw it; an answer written at one bit and read at
 * another, so a copy one machine refused for good is handed to it again, or a built one is taken
 * for refused; and a gap an epoch left in the serials that shifts the answers by 32 or more, which
 * in C is no shift at all but undefined behaviour; and a record that does not come back byte for
 * byte, which is two DLLs reading one field differently. And a ring that takes a serial out of
 * order, which moves every answer after it onto the wrong entry.
 */
#include "unittest.h"

#include "common/npc_spawn_note.h"

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <string.h>

static void check_serial_after(void)
{
    ut_section("a serial comes after a mark when it is less than half the counter ahead");

    ut_check(npc_spawn_note_serial_after(1u, 0u), "1 after 0, the first serial after none");
    ut_check(!npc_spawn_note_serial_after(5u, 5u), "a serial is not after itself");
    ut_check(!npc_spawn_note_serial_after(4u, 5u), "nor after a later one");
    ut_check(npc_spawn_note_serial_after(2u, 0xFFFFFFFEu), "across the wrap, 2 after 0xFFFFFFFE");
    ut_check(!npc_spawn_note_serial_after(0xFFFFFFFEu, 2u), "and not the other way round");
    ut_check(npc_spawn_note_serial_after(0x7FFFFFFFu, 0u), "the farthest one ahead");
    ut_check(!npc_spawn_note_serial_after(0x80000000u, 0u),
             "and half the counter ahead is not after: it is as far behind");
}

static void check_the_answers(void)
{
    npc_spawn_wish_record_t wishes;

    ut_section("the answer to a grant is read at its own bit, and the wrap changes nothing");

    memset(&wishes, 0, sizeof wishes);
    wishes.grants_done = 100u;
    wishes.refused     = (1u << 1) | (1u << 31);
    ut_check(npc_spawn_note_answer(&wishes, 101u) == NPC_SPAWN_ANSWER_OPEN,
             "a grant past the acknowledgement is open");
    ut_check(npc_spawn_note_answer(&wishes, 100u) == NPC_SPAWN_ANSWER_DONE,
             "the acknowledged one, bit 0 clear, is done");
    ut_check(npc_spawn_note_answer(&wishes, 99u) == NPC_SPAWN_ANSWER_REFUSED,
             "the one before it, bit 1 set, is refused");
    ut_check(npc_spawn_note_answer(&wishes, 98u) == NPC_SPAWN_ANSWER_DONE, "bit 2 clear, done");
    ut_check(npc_spawn_note_answer(&wishes, 69u) == NPC_SPAWN_ANSWER_REFUSED,
             "the oldest one kept, bit 31");
    ut_check(npc_spawn_note_answer(&wishes, 68u) == NPC_SPAWN_ANSWER_LOST,
             "and one older than that is lost, not guessed");
    ut_check(npc_spawn_note_answer(&wishes, 100u - 0x7FFFFFFFu) == NPC_SPAWN_ANSWER_LOST,
             "as is the oldest the counter can name");
    ut_check(npc_spawn_note_answer(&wishes, 100u + 0x7FFFFFFFu) == NPC_SPAWN_ANSWER_OPEN,
             "while the farthest ahead is open");

    wishes.grants_done = 2u;
    wishes.refused     = 1u << 4;
    ut_check(npc_spawn_note_answer(&wishes, 0xFFFFFFFEu) == NPC_SPAWN_ANSWER_REFUSED,
             "across the wrap, four behind is still bit 4");
    ut_check(npc_spawn_note_answer(&wishes, 3u) == NPC_SPAWN_ANSWER_OPEN, "and one ahead is open");
    ut_check(npc_spawn_note_answer(NULL, 1u) == NPC_SPAWN_ANSWER_OPEN,
             "and with no record there is no answer yet");
}

static void check_acknowledging(void)
{
    npc_spawn_wish_record_t wishes;

    ut_section("what the overlay acknowledges is what the multiplayer reads back");

    memset(&wishes, 0, sizeof wishes);
    ut_check(npc_spawn_note_acknowledge(&wishes, 1u, false) && wishes.grants_done == 1u,
             "the first grant, done");
    ut_check(npc_spawn_note_acknowledge(&wishes, 2u, true) &&
                 npc_spawn_note_acknowledge(&wishes, 3u, false),
             "the second refused, the third done");
    ut_check(npc_spawn_note_answer(&wishes, 1u) == NPC_SPAWN_ANSWER_DONE &&
                 npc_spawn_note_answer(&wishes, 2u) == NPC_SPAWN_ANSWER_REFUSED &&
                 npc_spawn_note_answer(&wishes, 3u) == NPC_SPAWN_ANSWER_DONE,
             "and each reads back as it was said");
    ut_check(!npc_spawn_note_acknowledge(&wishes, 3u, true) &&
                 npc_spawn_note_answer(&wishes, 3u) == NPC_SPAWN_ANSWER_DONE,
             "a grant already answered is not answered again");
    ut_check(!npc_spawn_note_acknowledge(&wishes, 2u, false) && wishes.grants_done == 3u,
             "nor one before the mark");
    ut_check(!npc_spawn_note_acknowledge(NULL, 4u, false), "and no record takes no answer");

    ut_check(npc_spawn_note_acknowledge(&wishes, 7u, false), "three emptied by an epoch, then 7");
    ut_check(npc_spawn_note_answer(&wishes, 7u) == NPC_SPAWN_ANSWER_DONE &&
                 npc_spawn_note_answer(&wishes, 4u) == NPC_SPAWN_ANSWER_REFUSED &&
                 npc_spawn_note_answer(&wishes, 6u) == NPC_SPAWN_ANSWER_REFUSED,
             "the skipped ones are marked not done here");
    ut_check(npc_spawn_note_answer(&wishes, 3u) == NPC_SPAWN_ANSWER_DONE &&
                 npc_spawn_note_answer(&wishes, 2u) == NPC_SPAWN_ANSWER_REFUSED,
             "and the ones before the gap kept their answers, four bits further on");

    ut_check(npc_spawn_note_acknowledge(&wishes, 7u + 31u, false) &&
                 npc_spawn_note_answer(&wishes, 38u) == NPC_SPAWN_ANSWER_DONE &&
                 npc_spawn_note_answer(&wishes, 8u) == NPC_SPAWN_ANSWER_REFUSED &&
                 npc_spawn_note_answer(&wishes, 7u) == NPC_SPAWN_ANSWER_DONE,
             "a gap of 31 keeps the answer before it in the last bit");
    wishes.refused = 0u;
    ut_check(npc_spawn_note_acknowledge(&wishes, 38u + 32u, false) &&
                 npc_spawn_note_answer(&wishes, 70u) == NPC_SPAWN_ANSWER_DONE &&
                 npc_spawn_note_answer(&wishes, 39u) == NPC_SPAWN_ANSWER_REFUSED &&
                 npc_spawn_note_answer(&wishes, 38u) == NPC_SPAWN_ANSWER_LOST,
             "a gap of 32 marks all it skipped and keeps nothing older");
    ut_check(npc_spawn_note_acknowledge(&wishes, 70u + 40u, false) &&
                 npc_spawn_note_answer(&wishes, 110u) == NPC_SPAWN_ANSWER_DONE,
             "and a wider gap answered done reads done");
    wishes.grants_done = 7u;
    ut_check(npc_spawn_note_acknowledge(&wishes, 7u + 40u, true),
             "a gap of forty, wider than the answers kept");
    ut_check(npc_spawn_note_answer(&wishes, 47u) == NPC_SPAWN_ANSWER_REFUSED &&
                 npc_spawn_note_answer(&wishes, 46u) == NPC_SPAWN_ANSWER_REFUSED &&
                 npc_spawn_note_answer(&wishes, 16u) == NPC_SPAWN_ANSWER_REFUSED &&
                 npc_spawn_note_answer(&wishes, 15u) == NPC_SPAWN_ANSWER_LOST,
             "leaves only its own answer and skipped ones, and no shifted-in garbage");

    wishes.grants_done = 0xFFFFFFFEu;
    wishes.refused     = 0u;
    ut_check(npc_spawn_note_acknowledge(&wishes, 0xFFFFFFFFu, true) &&
                 npc_spawn_note_acknowledge(&wishes, 0u, false) &&
                 npc_spawn_note_acknowledge(&wishes, 1u, true),
             "three answers across the wrap");
    ut_check(npc_spawn_note_answer(&wishes, 0xFFFFFFFFu) == NPC_SPAWN_ANSWER_REFUSED &&
                 npc_spawn_note_answer(&wishes, 0u) == NPC_SPAWN_ANSWER_DONE &&
                 npc_spawn_note_answer(&wishes, 1u) == NPC_SPAWN_ANSWER_REFUSED &&
                 npc_spawn_note_answer(&wishes, 0xFFFFFFFEu) == NPC_SPAWN_ANSWER_DONE,
             "read back as said, the one before them untouched");
}

/* 1 when the record's answer to `serial` is not the one the overlay gave, 0 when it is. */
static uint32_t answered_wrongly(const npc_spawn_wish_record_t *wishes, uint32_t serial,
                                 const bool *said)
{
    npc_spawn_answer_t meant = said[serial % 1024u] ? NPC_SPAWN_ANSWER_REFUSED
                                                    : NPC_SPAWN_ANSWER_DONE;

    return npc_spawn_note_answer(wishes, serial) == meant ? 0u : 1u;
}

/* One run of the protocol the header states: the multiplayer keeps at most five grants waiting
 * and asks about each in the read that drops it; the overlay answers a few a frame; now and then an
 * epoch empties what waits, and the overlay acknowledges past the gap. With `ask_late` the
 * multiplayer drops a grant as soon as it is answered but asks about it only every fiftieth step,
 * which the header forbids; that run has to lose answers, or this one proves nothing. Answers the
 * number read wrongly; `asked` gets the number asked about. */
static uint32_t run_protocol(bool ask_late, uint32_t *asked)
{
    npc_spawn_wish_record_t wishes;
    uint32_t                state   = 0x51u;
    uint32_t                next    = 1u;   /* the multiplayer shows oldest .. next - 1 */
    uint32_t                oldest  = 1u;
    uint32_t                wrong   = 0u;
    uint32_t                late[1024];
    size_t                  waiting = 0;
    bool                    said[1024] = {false};
    unsigned                step;

    *asked = 0u;
    memset(&wishes, 0, sizeof wishes);
    for (step = 0; step < 10000u; ++step) {
        state = state * 1664525u + 1013904223u;
        if ((state >> 24) % 97u == 0u) {
            next  += (state >> 4) % 40u;   /* published once, emptied by an epoch unanswered */
            oldest = next;
        }
        while (next - oldest < NPC_SPAWN_GRANT_SLOTS) {
            ++next;
        }
        if ((state >> 9) % 3u != 0u) {
            uint32_t answers = (state >> 12) % 4u;

            while (answers-- > 0u && npc_spawn_note_serial_after(next - 1u, wishes.grants_done)) {
                uint32_t serial = wishes.grants_done + 1u;
                bool     refuse = ((state >> (answers + 16u)) & 1u) != 0u;

                if (npc_spawn_note_serial_after(oldest, serial)) {
                    serial = oldest;   /* the record starts past a gap an epoch left */
                }
                (void)npc_spawn_note_acknowledge(&wishes, serial, refuse);
                said[serial % 1024u] = refuse;
            }
        }
        if ((state >> 20) % 4u != 0u) {
            while (oldest != next && !npc_spawn_note_serial_after(oldest, wishes.grants_done)) {
                if (!ask_late) {
                    wrong += answered_wrongly(&wishes, oldest, said);
                    ++*asked;
                } else if (waiting < sizeof late / sizeof late[0]) {
                    late[waiting++] = oldest;
                }
                ++oldest;
            }
        }
        if (ask_late && step % 50u == 49u) {
            size_t i;

            for (i = 0; i < waiting; ++i) {
                wrong += answered_wrongly(&wishes, late[i], said);
                ++*asked;
            }
            waiting = 0;
        }
    }
    return wrong;
}

static void check_the_protocol_never_loses_an_answer(void)
{
    uint32_t asked      = 0u;
    uint32_t asked_late = 0u;
    uint32_t wrong      = run_protocol(false, &asked);
    uint32_t wrong_late = run_protocol(true, &asked_late);

    ut_section("ten thousand steps of the protocol, with epochs, lose no answer");

    ut_checkf(asked > 1000u && wrong == 0u, "%u grants asked about, %u answered wrongly",
              (unsigned)asked, (unsigned)wrong);
    ut_checkf(wrong_late > 0u, "and a multiplayer that asks after it dropped loses some: %u of %u",
              (unsigned)wrong_late, (unsigned)asked_late);
}

/* A small generator, so that the fuzz below is the same on every run. */
static uint32_t next_random(uint32_t *state)
{
    *state = *state * 1664525u + 1013904223u;
    return *state >> 8;
}

static void random_desc(npc_spawn_note_desc_t *desc, uint32_t *state)
{
    static const char letters[] = "abcdefghijklmnopqrstuvwxyz0123456789_.";
    size_t            length    = 1u + next_random(state) % NPC_SPAWN_FILE_MAX;
    size_t            i;

    memset(desc, 0, sizeof *desc);
    desc->flags     = (uint8_t)(next_random(state) & NPC_SPAWN_DESC_ARCHIVE);
    desc->source    = (uint8_t)(next_random(state) % (desc->flags != 0u ? 256u : 255u));
    desc->behaviour = (uint8_t)(next_random(state) % NPC_SPAWN_BEHAVIOURS);
    for (i = 0; i < 3u; ++i) {
        desc->position[i] = (float)((int32_t)(next_random(state) % 60000u) - 30000) * 0.125f;
    }
    desc->facing = (float)(next_random(state) % 3600u) * 0.1f;
    for (i = 0; i < length; ++i) {
        desc->file[i] = letters[next_random(state) % (sizeof letters - 1u)];
    }
}

static void check_random_records_round_trip(void)
{
    uint32_t state    = 0x2651u;
    unsigned mismatch = 0;
    unsigned round;

    ut_section("three hundred random sound records each come back byte for byte");

    for (round = 0; round < 300u; ++round) {
        npc_spawn_wish_record_t  wishes;
        npc_spawn_wish_record_t  wishes_back;
        npc_spawn_grant_record_t grants;
        npc_spawn_grant_record_t grants_back;
        size_t                   i;

        memset(&wishes, 0, sizeof wishes);
        wishes.version     = 1u;
        wishes.count       = (uint8_t)(next_random(&state) % (NPC_SPAWN_WISH_SLOTS + 1u));
        wishes.panel       = (uint8_t)(next_random(&state) & 0x0Fu);
        wishes.first       = next_random(&state);
        wishes.grants_done = next_random(&state);
        wishes.refused     = next_random(&state);
        for (i = 0; i < wishes.count; ++i) {
            wishes.entry[i].kind  = (uint8_t)(1u + next_random(&state) % 4u);
            wishes.entry[i].epoch = (uint8_t)next_random(&state);
            if (wishes.entry[i].kind == NPC_SPAWN_WISH_SPAWN ||
                wishes.entry[i].kind == NPC_SPAWN_WISH_RESTORE) {
                random_desc(&wishes.entry[i].desc, &state);
            }
        }
        memset(&grants, 0, sizeof grants);
        grants.version      = 1u;
        grants.count        = (uint8_t)(next_random(&state) % (NPC_SPAWN_GRANT_SLOTS + 1u));
        grants.own_slot     = (uint8_t)(next_random(&state) % NPC_SPAWN_WORLD_SLOTS);
        grants.first        = next_random(&state);
        grants.wishes_taken = next_random(&state);
        grants.cap          = (uint16_t)(next_random(&state) % 129u);
        grants.epoch        = (uint8_t)next_random(&state);
        for (i = 0; i < grants.count; ++i) {
            npc_spawn_grant_t *g = &grants.entry[i];

            g->kind  = (uint8_t)(1u + next_random(&state) % 4u);
            g->owner = (uint8_t)(next_random(&state) % NPC_SPAWN_WORLD_SLOTS);
            if (g->kind == NPC_SPAWN_GRANT_REFUSED) {
                g->wish   = 1u + next_random(&state);
                g->reason = (uint8_t)(1u + next_random(&state) % 9u);
            } else {
                g->generation = (uint8_t)next_random(&state);
                g->key = (uint16_t)(NPC_SPAWN_KEY_FIRST + next_random(&state) % 128u);
            }
            if (g->kind == NPC_SPAWN_GRANT_BUILD) {
                random_desc(&g->desc, &state);
            }
        }
        if (!npc_spawn_note_publish_wishes(&wishes) || !npc_spawn_note_read_wishes(&wishes_back) ||
            memcmp(&wishes, &wishes_back, sizeof wishes) != 0 ||
            !npc_spawn_note_publish_grants(&grants) || !npc_spawn_note_read_grants(&grants_back) ||
            memcmp(&grants, &grants_back, sizeof grants) != 0) {
            ++mismatch;
        }
    }
    ut_checkf(mismatch == 0u, "all of them (%u did not)", mismatch);
}

static npc_spawn_grant_t a_cancel(uint8_t generation)
{
    npc_spawn_grant_t grant;

    memset(&grant, 0, sizeof grant);
    grant.kind       = NPC_SPAWN_GRANT_CANCEL;
    grant.generation = generation;
    grant.key        = (uint16_t)NPC_SPAWN_KEY_FIRST;
    return grant;
}

static void check_the_rings(void)
{
    npc_spawn_grant_record_t grants;
    npc_spawn_grant_record_t back;
    npc_spawn_wish_record_t  wishes;
    npc_spawn_grant_t        cancel = a_cancel(1u);
    npc_spawn_grant_t        broken = a_cancel(1u);
    npc_spawn_wish_t         removal;
    uint32_t                 i;

    ut_section("the rings take entries in serial order, drop what was dealt with, restart ahead");
    memset(&grants, 0, sizeof grants);
    grants.first = 1u;
    broken.key   = 3u;
    ut_check(!npc_spawn_note_grants_append(&grants, 2u, &cancel) &&
                 !npc_spawn_note_grants_append(&grants, 0u, &cancel) &&
                 !npc_spawn_note_grants_append(&grants, 1u, &broken) && grants.count == 0u,
             "an empty ring takes only its first serial, and only a sound entry");
    for (i = 0; i < NPC_SPAWN_GRANT_SLOTS; ++i) {
        cancel.generation = (uint8_t)(i + 1u);
        ut_check(npc_spawn_note_grants_append(&grants, 1u + i, &cancel), "serial after serial");
    }
    ut_check(!npc_spawn_note_grants_append(&grants, 1u + NPC_SPAWN_GRANT_SLOTS, &cancel),
             "a full ring takes nothing more");
    ut_check(npc_spawn_note_grants_drop(&grants, 0u) == 0u &&
                 npc_spawn_note_grants_drop(&grants, 2u) == 2u && grants.first == 3u &&
                 grants.count == NPC_SPAWN_GRANT_SLOTS - 2u && grants.entry[0].generation == 3u,
             "dropped up to what was dealt with, the rest moved to the front");
    ut_check(npc_spawn_note_publish_grants(&grants) && npc_spawn_note_read_grants(&back) &&
                 back.first == 3u && back.count == grants.count &&
                 back.entry[back.count - 1u].generation == NPC_SPAWN_GRANT_SLOTS,
             "and what is left publishes and reads back whole");
    npc_spawn_note_grants_restart(&grants);
    ut_check(grants.count == 0u && grants.first == 1u + NPC_SPAWN_GRANT_SLOTS &&
                 npc_spawn_note_grants_append(&grants, 1u + NPC_SPAWN_GRANT_SLOTS, &cancel),
             "a restart empties it and names the next serial first, never an earlier one");

    memset(&grants, 0, sizeof grants);
    grants.first = 0xFFFFFFFEu;
    ut_check(npc_spawn_note_grants_append(&grants, 0xFFFFFFFEu, &cancel) &&
                 npc_spawn_note_grants_append(&grants, 0xFFFFFFFFu, &cancel) &&
                 npc_spawn_note_grants_drop(&grants, 0xFFFFFFFFu) == 2u && grants.first == 0u,
             "the drop compares by distance, up to the end of the counter");
    memset(&grants, 0, sizeof grants);
    grants.first = 10u;
    (void)npc_spawn_note_grants_append(&grants, 10u, &cancel);
    ut_check(npc_spawn_note_grants_drop(&grants, 0xFFFFFFF0u) == 0u && grants.count == 1u,
             "and a mark far behind by distance is behind, however large its number");

    memset(&wishes, 0, sizeof wishes);
    memset(&removal, 0, sizeof removal);
    removal.kind  = NPC_SPAWN_WISH_REMOVE_OWN;
    wishes.first  = 40u;
    ut_check(!npc_spawn_note_wishes_append(&wishes, 41u, &removal) &&
                 npc_spawn_note_wishes_append(&wishes, 40u, &removal) &&
                 npc_spawn_note_wishes_append(&wishes, 41u, &removal),
             "the wish ring takes its serials in order as well");
    removal.kind = 9u;
    ut_check(!npc_spawn_note_wishes_append(&wishes, 42u, &removal) &&
                 npc_spawn_note_wishes_drop(&wishes, 40u) == 1u && wishes.first == 41u &&
                 wishes.count == 1u,
             "refuses an unsound wish, and drops what the multiplayer took");
}

int main(void)
{
    check_serial_after();
    check_the_answers();
    check_acknowledging();
    check_the_protocol_never_loses_an_answer();
    check_random_records_round_trip();
    check_the_rings();
    return ut_summary("npc_spawn_note_protocol");
}
