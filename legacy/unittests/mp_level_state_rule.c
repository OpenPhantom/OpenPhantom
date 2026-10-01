/* mp_level_state_rule.c: the level's state as a note, and the decisions a client makes about it.
 *
 * The sizes are the shipped ones: FEDSHIP holds 20 emitter placements and 151 level lights and its
 * gas room's fog is one state; the largest level holds 403 lights. The note is encoded into a
 * buffer of exactly the size one message of the reliable channel carries, which is what the caller
 * hands it, not into a comfortable one.
 */
#include "unittest.h"

#include "mp_channel.h"
#include "mp_events.h"
#include "mp_level_state_rule.h"
#include "mp_wire.h"
#include "mp_world_state.h"

#include <stdbool.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

static uint8_t s_buffer[MP_CHANNEL_MESSAGE_BYTES];

/* The gas room's green, half way down its twenty second ramp. */
static void fill_fog(mp_level_fog_state_t *fog)
{
    memset(fog, 0, sizeof *fog);
    fog->flags = (uint8_t)(MP_LEVEL_FOG_HAS_START | MP_LEVEL_FOG_HAS_END | MP_LEVEL_FOG_RAMP |
                           MP_LEVEL_FOG_COLOUR);
    fog->cur       = 0x41200418u;   /* 10.001f */
    fog->end       = 0x41800347u;   /* 16.002f */
    fog->target    = 0x3F800347u;   /* 1.0001f */
    fog->span      = 0x41A00000u;   /* 20.0f */
    fog->left      = 10u;
    fog->colour[0] = 0xAAu;
    fog->colour[1] = 0xFFu;
    fog->colour[2] = 0xAAu;
}

static void fill(mp_level_state_note_t *note, uint16_t emitters, uint16_t lights, bool fog)
{
    uint16_t i;

    mp_level_state_note_init(note, 12345u, 97u, 4u);
    note->parts = (uint8_t)(MP_LEVEL_STATE_PART_EMITTERS | MP_LEVEL_STATE_PART_LIGHTS);
    note->emitters = emitters;
    for (i = 0; i < emitters; ++i) {
        note->emitter[i] = (uint8_t)(i % 4u);
    }
    note->lights = lights;
    for (i = 0; i < lights; ++i) {
        mp_level_state_set_light(note, i, (i % 3u) == 0u);
    }
    if (fog) {
        note->parts |= (uint8_t)MP_LEVEL_STATE_PART_FOG;
        fill_fog(&note->fog);
    }
}

static bool same_fog(const mp_level_fog_state_t *a, const mp_level_fog_state_t *b)
{
    return a->flags == b->flags && a->cur == b->cur && a->end == b->end &&
           a->target == b->target && a->span == b->span && a->left == b->left &&
           a->colour[0] == b->colour[0] && a->colour[1] == b->colour[1] &&
           a->colour[2] == b->colour[2];
}

static bool same_note(const mp_level_state_note_t *a, const mp_level_state_note_t *b)
{
    size_t i;

    if (a->tick != b->tick || a->level != b->level || a->generation != b->generation ||
        a->parts != b->parts || a->emitters != b->emitters || a->lights != b->lights ||
        !same_fog(&a->fog, &b->fog)) {
        return false;
    }
    for (i = 0; i < a->emitters; ++i) {
        if (a->emitter[i] != b->emitter[i]) {
            return false;
        }
    }
    for (i = 0; i < a->lights; ++i) {
        if (mp_level_state_light_on(a, i) != mp_level_state_light_on(b, i)) {
            return false;
        }
    }
    return a->loops == b->loops && a->fog_viewers == b->fog_viewers &&
           a->escort_health == b->escort_health && a->escort_flags == b->escort_flags &&
           a->journal_newest == b->journal_newest && a->journal_count == b->journal_count &&
           memcmp(a->loop, b->loop, sizeof a->loop) == 0 &&
           memcmp(a->fog_viewer, b->fog_viewer, sizeof a->fog_viewer) == 0 &&
           memcmp(a->journal, b->journal, sizeof a->journal) == 0;
}

/* The four parts of the second form of the note, each at its largest. */
static void fill_the_later_parts(mp_level_state_note_t *note)
{
    size_t i;

    note->parts |= (uint8_t)(MP_LEVEL_STATE_PART_ACTOR_LOOPS | MP_LEVEL_STATE_PART_FOG_VIEWERS |
                             MP_LEVEL_STATE_PART_ESCORT | MP_LEVEL_STATE_PART_JOURNAL);
    note->loops = (uint8_t)MP_LEVEL_STATE_LOOPS_MAX;
    for (i = 0; i < MP_LEVEL_STATE_LOOPS_MAX; ++i) {
        note->loop[i].key  = (uint16_t)(20u + i * 17u);
        note->loop[i].life = (uint8_t)(i + 1u);
        note->loop[i].call = (uint16_t)(1100u + i);
    }
    note->fog_viewers = (uint8_t)MP_LEVEL_STATE_FOG_VIEWERS_MAX;
    for (i = 0; i < MP_LEVEL_STATE_FOG_VIEWERS_MAX; ++i) {
        note->fog_viewer[i].key      = (uint16_t)(300u + i);
        note->fog_viewer[i].life     = 2u;
        note->fog_viewer[i].script   = (uint16_t)(51u + i * 300u);
        note->fog_viewer[i].flags    = (uint8_t)(i & 3u);
        note->fog_viewer[i].place[0] = (uint16_t)(40000u + i);
        note->fog_viewer[i].place[1] = 18000u;
        note->fog_viewer[i].place[2] = 17000u;
    }
    note->escort_health = 73u;
    note->escort_flags  = (uint8_t)MP_LEVEL_STATE_ESCORT_SHOWN;
    note->journal_newest = 5u;
    note->journal_count  = (uint8_t)MP_LEVEL_STATE_JOURNAL_MAX;
    for (i = 0; i < MP_LEVEL_STATE_JOURNAL_MAX; ++i) {
        static const uint8_t KINDS[5] = { MP_LEVEL_JOURNAL_EMITTER, MP_LEVEL_JOURNAL_LIGHT,
                                          MP_LEVEL_JOURNAL_FOG, MP_LEVEL_JOURNAL_CRAWL,
                                          MP_LEVEL_JOURNAL_ESCORT };
        mp_level_journal_entry_t *entry = &note->journal[i];

        entry->sequence = (uint16_t)(65526u + i);   /* across the wrap to 5 */
        if (entry->sequence == 0u) {
            entry->sequence = 65535u;
        }
        entry->kind = KINDS[i % 5u];
        entry->a    = entry->kind == MP_LEVEL_JOURNAL_FOG ? 11u : (uint8_t)(i & 1u);
        entry->b    = (uint16_t)(i * 3u);
        entry->c    = 0x41800000u + (uint32_t)i;
    }
}

static void check_the_codec(void)
{
    mp_level_state_note_t note;
    mp_level_state_note_t back;
    size_t                bytes;
    size_t                cut;

    ut_section("a note goes round whole, at the shipped sizes and at the largest");

    fill(&note, 20u, 151u, true);
    bytes = mp_level_state_encode(&note, s_buffer, sizeof s_buffer);
    ut_checkf(bytes == 61u, "FEDSHIP's note with the gas room's fog is 61 bytes (%u)",
              (unsigned)bytes);
    ut_check(bytes == mp_level_state_bytes(&note), "and the size the rule predicts");
    ut_check(mp_level_state_decode(s_buffer, bytes, &back) && same_note(&note, &back),
             "and it reads back as it was written");

    fill(&note, (uint16_t)MP_LEVEL_STATE_MAX_EMITTERS, (uint16_t)MP_LEVEL_STATE_MAX_LIGHTS, true);
    bytes = mp_level_state_encode(&note, s_buffer, sizeof s_buffer);
    ut_checkf(bytes == 229u,
              "the largest note of the first three parts is %u bytes", (unsigned)bytes);
    ut_check(mp_level_state_decode(s_buffer, bytes, &back) && same_note(&note, &back),
             "and reads back whole");
    fill_the_later_parts(&note);
    bytes = mp_level_state_encode(&note, s_buffer, sizeof s_buffer);
    ut_checkf(bytes == MP_LEVEL_STATE_MAX_BYTES && bytes == 524u,
              "with the loops, the viewers' fog, the escort and a full journal the largest note is "
              "%u bytes", (unsigned)bytes);
    ut_checkf(bytes <= MP_CHANNEL_MESSAGE_BYTES,
              "and one message carries it: %u against %u", (unsigned)bytes,
              (unsigned)MP_CHANNEL_MESSAGE_BYTES);
    ut_check(mp_level_state_decode(s_buffer, bytes, &back) && same_note(&note, &back),
             "and reads back whole");

    ut_section("a note is read whole or not at all");
    fill(&note, 20u, 151u, true);
    bytes = mp_level_state_encode(&note, s_buffer, sizeof s_buffer);
    for (cut = 0; cut < bytes; ++cut) {
        if (mp_level_state_decode(s_buffer, cut, &back)) {
            break;
        }
    }
    ut_checkf(cut == bytes, "every note shorter than its own length is refused (%u of %u)",
              (unsigned)cut, (unsigned)bytes);
    s_buffer[bytes] = 0u;
    ut_check(!mp_level_state_decode(s_buffer, bytes + 1u, &back),
             "and so is one with a byte behind it");
    ut_check(mp_level_state_encode(&note, s_buffer, bytes - 1u) == 0u,
             "and an encoder given one byte too few writes nothing");

    ut_section("a part this build does not read is refused, the reserved eighth bit");
    fill(&note, 3u, 9u, true);
    bytes = mp_level_state_encode(&note, s_buffer, sizeof s_buffer);
    s_buffer[11] |= (uint8_t)MP_LEVEL_STATE_PART_RESERVED;
    ut_check(!mp_level_state_decode(s_buffer, bytes, &back),
             "a note that sets the reserved bit is not one this build can read, as a build on 31 "
             "cannot read one that sets any of the four new ones");
    note.parts |= (uint8_t)MP_LEVEL_STATE_PART_RESERVED;
    ut_check(mp_level_state_encode(&note, s_buffer, sizeof s_buffer) == 0u,
             "and nothing here writes one");

    ut_section("the four new parts: read whole, and refused where a value names nothing");
    fill(&note, 3u, 9u, true);
    fill_the_later_parts(&note);
    bytes = mp_level_state_encode(&note, s_buffer, sizeof s_buffer);
    ut_check(bytes == mp_level_state_bytes(&note) &&
                 mp_level_state_decode(s_buffer, bytes, &back) && same_note(&note, &back),
             "loops, viewers, escort and journal come back as they went");
    for (cut = 0; cut < bytes; ++cut) {
        if (mp_level_state_decode(s_buffer, cut, &back)) {
            break;
        }
    }
    ut_checkf(cut == bytes, "and no shorter note reads (%u of %u)", (unsigned)cut,
              (unsigned)bytes);
    note.escort_health = (uint8_t)(MP_LEVEL_STATE_ESCORT_HEALTH + 1u);
    ut_check(mp_level_state_encode(&note, s_buffer, sizeof s_buffer) == 0u,
             "an escort past 100 is refused, the engine clamps there");
    fill_the_later_parts(&note);
    note.loop[3].key = (uint16_t)MP_WIRE_KEY_COUNT;
    ut_check(mp_level_state_encode(&note, s_buffer, sizeof s_buffer) == 0u,
             "a loop on a key no enemy has is refused");
    fill_the_later_parts(&note);
    note.fog_viewer[1].flags = 0x04u;
    ut_check(mp_level_state_encode(&note, s_buffer, sizeof s_buffer) == 0u,
             "a viewer's fog with a flag this build does not know is refused");
    fill_the_later_parts(&note);
    note.journal[2].a = 8u;   /* a fog entry, and command 8 is a weapon */
    ut_check(mp_level_state_encode(&note, s_buffer, sizeof s_buffer) == 0u,
             "a journal's fog entry names one of the director's fog commands or is refused");
    fill_the_later_parts(&note);
    note.journal[4].kind = (uint8_t)MP_LEVEL_JOURNAL_KINDS;
    ut_check(mp_level_state_encode(&note, s_buffer, sizeof s_buffer) == 0u,
             "so is an entry of a kind past the table");
    fill_the_later_parts(&note);
    note.journal[0].sequence = 0u;
    ut_check(mp_level_state_encode(&note, s_buffer, sizeof s_buffer) == 0u,
             "and one with the number 0");
    fill_the_later_parts(&note);
    bytes = mp_level_state_encode(&note, s_buffer, sizeof s_buffer);
    s_buffer[bytes - 1u - (MP_LEVEL_STATE_JOURNAL_MAX * MP_LEVEL_STATE_JOURNAL_BYTES)] =
        (uint8_t)(MP_LEVEL_STATE_JOURNAL_MAX + 1u);
    ut_check(!mp_level_state_decode(s_buffer, bytes, &back),
             "a journal count past what the note carries is refused on the way in");

    ut_section("a fog state that says nothing whole is refused");
    fill(&note, 0u, 0u, true);
    note.fog.flags |= 0x40u;
    ut_check(mp_level_state_encode(&note, s_buffer, sizeof s_buffer) == 0u,
             "a flag this build does not know");
    fill(&note, 0u, 0u, true);
    note.fog.flags = (uint8_t)(note.fog.flags & ~MP_LEVEL_FOG_RAMP);
    ut_check(mp_level_state_encode(&note, s_buffer, sizeof s_buffer) == 0u,
             "and a ramp that still runs with no target and length set");
    fill(&note, 0u, 0u, true);
    bytes = mp_level_state_encode(&note, s_buffer, sizeof s_buffer);
    s_buffer[MP_LEVEL_STATE_HEADER_BYTES + 3u] = 0x80u;   /* the fog part's flags byte */
    ut_check(!mp_level_state_decode(s_buffer, bytes, &back),
             "and the same on the way in");

    ut_section("the bits past the last light say nothing");
    fill(&note, 0u, 9u, false);
    mp_level_state_set_light(&note, 12u, true);
    bytes = mp_level_state_encode(&note, s_buffer, sizeof s_buffer);
    ut_check(mp_level_state_decode(s_buffer, bytes, &back) && !mp_level_state_light_on(&back, 12u),
             "a bit set past the count does not travel");
}

/* Random bytes behind the tag: nothing is read past the end, and whatever is taken says again
 * exactly what it was given. */
static void check_random_bytes(void)
{
    mp_level_state_note_t back;
    mp_level_state_note_t alike;
    uint8_t               again[MP_LEVEL_STATE_MAX_BYTES + 8u];
    unsigned              taken = 0;
    unsigned              faithful = 0;
    unsigned              round;

    ut_section("random bytes behind the tag");
    srand(29u);
    for (round = 0; round < 20000u; ++round) {
        size_t bytes = (size_t)(rand() % (int)(MP_LEVEL_STATE_MAX_BYTES + 8u));
        size_t i;

        for (i = 0; i < bytes; ++i) {
            s_buffer[i] = (uint8_t)(rand() & 0xFF);
        }
        if (bytes > 0u) {
            s_buffer[0] = (uint8_t)MP_LEVEL_STATE_TAG;
        }
        if (bytes > 11u) {
            s_buffer[11] &= 0x0Bu;   /* only emitters, lights and fog, so some of them decode */
        }
        if (!mp_level_state_decode(s_buffer, bytes, &back)) {
            continue;
        }
        ++taken;
        /* Said again, it has the same length and reads back alike; the bits past the last light
         * and the last placement are the one thing the encoder writes as 0. */
        if (mp_level_state_bytes(&back) == bytes &&
            mp_level_state_encode(&back, again, sizeof again) == bytes &&
            mp_level_state_decode(again, bytes, &alike) && same_note(&back, &alike)) {
            ++faithful;
        }
    }
    ut_checkf(taken > 0u, "some random notes decode (%u), so the check has something to read",
              taken);
    ut_checkf(faithful == taken, "and every one taken says again what it was given (%u of %u)",
              faithful, taken);
}

static void check_the_emitters(void)
{
    const char  own[16]   = "Steam";
    const char  other[16] = "RepSmoke";
    const float at[3]     = { 122.5f, 132.4f, 43.2f };
    const float there[3]  = { 122.5f, 132.4f, 44.2f };
    unsigned    host;
    unsigned    here;

    ut_section("a slot is the placement's own only while it holds the placement's emitter");
    ut_check(mp_level_state_emitter_owned(3u, own, own, at, at),
             "in use, under the template's name, at the placement's own position");
    ut_check(!mp_level_state_emitter_owned(0u, own, own, at, at),
             "a free slot is nobody's: its lifetime ended and the pool took it back");
    ut_check(!mp_level_state_emitter_owned(3u, other, own, at, at),
             "a slot under another name is somebody else's, a bolt's trail or another cloud");
    ut_check(!mp_level_state_emitter_owned(3u, own, own, there, at),
             "and so is one of the same name somewhere else");

    ut_section("what a side sees of one placement");
    ut_check(mp_level_state_emitter_seen(-1, false, false) == MP_LEVEL_EMITTER_NEVER,
             "no slot is never spawned");
    ut_check(mp_level_state_emitter_seen(5, false, false) == MP_LEVEL_EMITTER_GONE,
             "a slot that is not its own is gone, whatever its flag says");
    ut_check(mp_level_state_emitter_seen(5, true, false) == MP_LEVEL_EMITTER_ON, "its own, on");
    ut_check(mp_level_state_emitter_seen(5, true, true) == MP_LEVEL_EMITTER_OFF, "its own, off");

    ut_section("what a client does, over every pair");
    for (host = 0; host < 4u; ++host) {
        for (here = 0; here < 4u; ++here) {
            mp_level_act_t act = mp_level_state_emitter_act((mp_level_emitter_t)host,
                                                            (mp_level_emitter_t)here);
            mp_level_act_t want;

            if (host == MP_LEVEL_EMITTER_GONE) {
                want = MP_LEVEL_ACT_NONE;
            } else if ((host == MP_LEVEL_EMITTER_ON) == (here == MP_LEVEL_EMITTER_ON)) {
                want = MP_LEVEL_ACT_NONE;
            } else if (here == MP_LEVEL_EMITTER_GONE) {
                want = MP_LEVEL_ACT_NOT_OWNED;
            } else {
                want = host == MP_LEVEL_EMITTER_ON ? MP_LEVEL_ACT_ON : MP_LEVEL_ACT_OFF;
            }
            ut_checkf(act == want, "host %u, here %u: %u", host, here, (unsigned)act);
        }
    }
    ut_check(mp_level_state_emitter_act(MP_LEVEL_EMITTER_GONE, MP_LEVEL_EMITTER_ON) ==
                 MP_LEVEL_ACT_NONE,
             "a cloud the host's pool has ended is not raised or put out here");
    ut_check(mp_level_state_emitter_act(MP_LEVEL_EMITTER_NEVER, MP_LEVEL_EMITTER_OFF) ==
                 MP_LEVEL_ACT_NONE,
             "never spawned and switched off are the same thing on screen");
    ut_check(mp_level_state_emitter_act(MP_LEVEL_EMITTER_OFF, MP_LEVEL_EMITTER_GONE) ==
                 MP_LEVEL_ACT_NONE,
             "and a slot not its own here is left alone when both show nothing");
}

static void check_the_fog(void)
{
    int32_t command;

    ut_section("only the four fog commands may be replayed with a stand-in");
    for (command = -1; command <= 20; ++command) {
        bool fog = command == 6 || command == 7 || command == 11 || command == 12;

        ut_checkf(mp_level_state_fog_command(command) == fog, "command %d", (int)command);
    }
    ut_check(mp_level_state_fog_is_ramp(11) && mp_level_state_fog_is_ramp(12) &&
             !mp_level_state_fog_is_ramp(6) && !mp_level_state_fog_is_ramp(7),
             "11 and 12 ramp, 6 and 7 set an edge");
    ut_check(mp_level_state_fog_fits(0) && mp_level_state_fog_fits(65535) &&
             !mp_level_state_fog_fits(65536) && !mp_level_state_fog_fits(-1),
             "a ramp's length travels in a journal entry's sixteen bits and one past them is "
             "refused");
}

static void check_the_throttle(void)
{
    uint32_t tick;
    unsigned sent = 0;
    unsigned held = 0;
    bool     sent_before = false;
    bool     change_before = false;
    uint32_t last_sent = 0;
    uint32_t last_change = 0;

    ut_section("when a host sends");
    ut_check(mp_level_state_due(false, false, 0u, false, 0u) == MP_LEVEL_SEND_CHANGE,
             "a level's first note goes at once");
    ut_check(mp_level_state_due(true, true, 1u, true, 1u) == MP_LEVEL_SEND_HELD,
             "a change one substep after the last is held back");
    ut_check(mp_level_state_due(true, true, 4u, true, 4u) == MP_LEVEL_SEND_CHANGE,
             "and goes four substeps after it");
    ut_check(mp_level_state_due(false, true, 31u, true, 31u) == MP_LEVEL_SEND_NONE,
             "no change and no second passed: nothing");
    ut_check(mp_level_state_due(false, true, 32u, true, 40u) == MP_LEVEL_SEND_REPEAT,
             "a second: the repeat");
    ut_check(mp_level_state_due(true, true, 32u, true, 2u) == MP_LEVEL_SEND_REPEAT,
             "a held change rides the repeat that falls due");

    /* A droid at the edge of its distance test switches its sparks on and off every substep. */
    for (tick = 0; tick < 64u; ++tick) {
        mp_level_send_t due = mp_level_state_due(true, sent_before, tick - last_sent,
                                                 change_before, tick - last_change);

        if (due == MP_LEVEL_SEND_CHANGE || due == MP_LEVEL_SEND_REPEAT) {
            ++sent;
            sent_before = true;
            last_sent   = tick;
            if (due == MP_LEVEL_SEND_CHANGE) {
                change_before = true;
                last_change   = tick;
            }
        } else if (due == MP_LEVEL_SEND_HELD) {
            ++held;
        }
    }
    ut_checkf(sent == 16u && held == 48u,
              "a switch that flips every substep for two seconds sends 16 notes, not 64 (%u "
              "sent, %u held)", sent, held);
}

static void check_the_order(void)
{
    mp_level_taken_t taken;
    bool             first = false;

    ut_section("what a client takes");
    memset(&taken, 0, sizeof taken);
    ut_check(mp_level_state_order(&taken, 97u, 98u, 0u, 5u, &first) == MP_LEVEL_ORDER_OTHER_LEVEL,
             "a note about another level is refused");
    ut_check(mp_level_state_order(&taken, 97u, 97u, 3u, 5u, &first) == MP_LEVEL_ORDER_TAKE &&
             first, "the first note of a level is taken and is its first");

    taken.any = true;
    taken.level = 97u;
    taken.generation = 3u;
    taken.tick = 500u;
    taken.newest_generation = 3u;
    ut_check(mp_level_state_order(&taken, 97u, 97u, 2u, 900u, &first) ==
                 MP_LEVEL_ORDER_OLD_GENERATION,
             "a note of an older generation is refused, however late its tick");
    ut_check(mp_level_state_order(&taken, 97u, 97u, 3u, 500u, &first) == MP_LEVEL_ORDER_OLD_TICK,
             "inside one level and generation a tick that is not newer is refused");
    ut_check(mp_level_state_order(&taken, 97u, 97u, 3u, 501u, &first) == MP_LEVEL_ORDER_TAKE &&
             !first, "and a newer one is taken, and is not the first");
    ut_check(mp_level_state_order(&taken, 97u, 97u, 4u, 12u, &first) == MP_LEVEL_ORDER_TAKE &&
             first,
             "a new generation is taken whatever its tick, because a host's substeps start over "
             "with a session, and it is the first of its generation");
}

/* What a client does with a whole note, placement by placement: how many it switches. */
static unsigned actions(const mp_level_state_note_t *host, const mp_level_emitter_t *here)
{
    unsigned n = 0;
    size_t   i;

    for (i = 0; i < host->emitters; ++i) {
        mp_level_act_t act = mp_level_state_emitter_act((mp_level_emitter_t)host->emitter[i],
                                                        here[i]);

        n += (act == MP_LEVEL_ACT_ON || act == MP_LEVEL_ACT_OFF) ? 1u : 0u;
    }
    return n;
}

static void check_a_savegame_and_a_late_joiner(void)
{
    mp_level_state_note_t note;
    mp_level_emitter_t    here[20];
    mp_level_taken_t      taken;
    bool                  first = false;
    size_t                i;

    ut_section("a savegame both sides restored: the first note after it switches nothing");
    fill(&note, 20u, 0u, false);
    for (i = 0; i < 20u; ++i) {
        here[i] = (mp_level_emitter_t)note.emitter[i];
    }
    ut_check(actions(&note, here) == 0u,
             "the client restored the host's own file, so no placement differs and no line of "
             "changes follows the restore");
    ut_check((note.parts & MP_LEVEL_STATE_PART_FOG) == 0u,
             "and the host's fog began again at the restore, so its first note carries none to lay "
             "over the fog the savegame put back");
    memset(&taken, 0, sizeof taken);
    ut_check(mp_level_state_order(&taken, 97u, 97u, 4u, 12345u, &first) == MP_LEVEL_ORDER_TAKE &&
             first, "a client that forgot everything at the restore takes the next note as the "
             "first of its level");

    ut_section("a player who joins late is given the level as it stands");
    for (i = 0; i < 20u; ++i) {
        here[i] = MP_LEVEL_EMITTER_NEVER;   /* the authored start, before any script ran */
    }
    fill(&note, 20u, 0u, true);
    /* fill() gives placement i the value i % 4: five on, five off, five never, five gone. */
    ut_checkf(actions(&note, here) == 5u,
              "the five placements the host has on are switched on, and nothing else (%u)",
              actions(&note, here));
    ut_check((note.parts & MP_LEVEL_STATE_PART_FOG) != 0u &&
             (note.fog.flags & MP_LEVEL_FOG_COLOUR) != 0u,
             "and its fog travels as the host's state, the green included, which the fog's own "
             "rule turns into the commands that reach it");
}

static void check_the_tag(void)
{
    size_t bytes;

    ut_section("a note is told apart by its tag, never by its length");
    for (bytes = 0; bytes <= MP_LEVEL_STATE_MAX_BYTES; ++bytes) {
        if (bytes >= MP_LEVEL_STATE_HEADER_BYTES) {
            ut_checkf(bytes != MP_EVENT_FOREIGN_SLOT_NOTE_BYTES &&
                      bytes != MP_EVENT_FOREIGN_ACK_BYTES,
                      "a note of %u bytes is neither of the two tagless messages", (unsigned)bytes);
        }
    }
    ut_check(MP_LEVEL_STATE_TAG != MP_WORLD_STATE_TAG && MP_LEVEL_STATE_TAG != MP_EVENT_HIT &&
             MP_LEVEL_STATE_TAG != MP_EVENT_MOVER,
             "and the tag is its own");
    s_buffer[0] = (uint8_t)MP_WORLD_STATE_TAG;
    ut_check(!mp_level_state_is_note(s_buffer, 40u), "a map note is not one");
}

int main(void)
{
    check_the_codec();
    check_random_bytes();
    check_the_emitters();
    check_the_fog();
    check_the_throttle();
    check_the_order();
    check_a_savegame_and_a_late_joiner();
    check_the_tag();

    return ut_summary("mp_level_state_rule");
}
