/* npc_spawn_note.c: the three records the spawner and the multiplayer pass each other.
 *
 * Like the other notes, this is tested in one process because that is the real case: both sides
 * are DLLs in the game's process and read each other through the operating system.
 *
 * What would be silent if it were wrong: a key test that lets 255 or 384 through, which puts a
 * copy on a placement's index or past the actor pool; a reader that takes a record the publisher
 * would have refused, which hands the builder a name that is not a file; a refused read that
 * writes into the caller's record, which loses what it had taken; and a count one past the slots
 * that nothing notices, which copies past the end of the record. How serials compare and where an
 * answer is kept is npc_spawn_note_protocol.c.
 */
#include "unittest.h"

#include "common/npc_spawn_note.h"
#include "common/shared_note.h"

#include <math.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <string.h>

static npc_spawn_note_desc_t sound_desc(void)
{
    npc_spawn_note_desc_t desc;

    memset(&desc, 0, sizeof desc);
    desc.source      = 12u;
    desc.behaviour   = 1u;
    desc.position[0] = 1250.5f;
    desc.position[1] = -3.25f;
    desc.position[2] = -880.0f;
    desc.facing      = 270.0f;
    memcpy(desc.file, "battledr.baf", NPC_SPAWN_FILE_MAX);
    return desc;
}

static npc_spawn_wish_t spawn_wish(void)
{
    npc_spawn_wish_t wish;

    memset(&wish, 0, sizeof wish);
    wish.kind  = NPC_SPAWN_WISH_SPAWN;
    wish.epoch = 3u;
    wish.desc  = sound_desc();
    return wish;
}

static npc_spawn_grant_t build_grant(uint16_t key)
{
    npc_spawn_grant_t grant;

    memset(&grant, 0, sizeof grant);
    grant.kind       = NPC_SPAWN_GRANT_BUILD;
    grant.generation = 2u;
    grant.key        = key;
    grant.owner      = 1u;
    grant.desc       = sound_desc();
    return grant;
}

static void check_nothing_published_yet(void)
{
    npc_spawn_wish_record_t   wishes;
    npc_spawn_grant_record_t  grants;
    npc_spawn_anchor_record_t anchors;
    uint8_t                   before[sizeof wishes];

    ut_section("before anybody has published, every read refuses and touches nothing");

    memset(&wishes, 0xAB, sizeof wishes);
    memset(&grants, 0xAB, sizeof grants);
    memset(&anchors, 0xAB, sizeof anchors);
    memcpy(before, &wishes, sizeof before);
    ut_check(!npc_spawn_note_read_wishes(&wishes), "no wish record yet");
    ut_check(!npc_spawn_note_read_grants(&grants), "no grant record yet");
    ut_check(!npc_spawn_note_read_anchors(&anchors), "no anchor record yet");
    ut_check(memcmp(before, &wishes, sizeof before) == 0,
             "and the caller's record is as it was, so a lost read cannot undo what was taken");
}

static void check_keys(void)
{
    ut_section("a copy's key is 256 to 383 and nothing else");

    ut_check(!npc_spawn_key_is_copy(0u), "0 is a placement");
    ut_check(!npc_spawn_key_is_copy(254u), "254 is the largest placement");
    ut_check(!npc_spawn_key_is_copy(255u), "255 is no placement and no copy");
    ut_check(npc_spawn_key_is_copy(256u), "256 is the first copy");
    ut_check(npc_spawn_key_is_copy(383u), "383 is the last one the actor pool allows");
    ut_check(!npc_spawn_key_is_copy(384u), "384 is past the pool");
    ut_check(!npc_spawn_key_is_copy(0xFFFFu), "and the wire's no-enemy is none of them");
}

static void check_descriptions(void)
{
    npc_spawn_note_desc_t desc;

    ut_section("a description names a file, a behaviour and a place, and nothing unknown");

    desc = sound_desc();
    ut_check(npc_spawn_note_desc_is_sound(&desc), "a level kind with twelve characters, no NUL");
    desc.source = NPC_SPAWN_NO_SOURCE;
    ut_check(!npc_spawn_note_desc_is_sound(&desc), "a level kind needs its placement");
    desc.flags = NPC_SPAWN_DESC_ARCHIVE;
    ut_check(npc_spawn_note_desc_is_sound(&desc), "an archive kind may have no donor");
    desc = sound_desc();
    desc.flags = 0x02u;
    ut_check(!npc_spawn_note_desc_is_sound(&desc), "a flag nobody defined");
    desc = sound_desc();
    desc.reserved = 1u;
    ut_check(!npc_spawn_note_desc_is_sound(&desc), "a reserved byte that is not zero");
    desc = sound_desc();
    desc.behaviour = NPC_SPAWN_BEHAVIOURS - 1u;
    ut_check(npc_spawn_note_desc_is_sound(&desc), "the last behaviour");
    desc.behaviour = NPC_SPAWN_BEHAVIOURS;
    ut_check(!npc_spawn_note_desc_is_sound(&desc), "one past it");

    desc = sound_desc();
    memset(desc.file, 0, sizeof desc.file);
    ut_check(!npc_spawn_note_desc_is_sound(&desc), "a name of nothing");
    memcpy(desc.file, "a", 1u);
    ut_check(npc_spawn_note_desc_is_sound(&desc), "a name of one character");
    memcpy(desc.file, "a~b", 3u);
    ut_check(npc_spawn_note_desc_is_sound(&desc), "and one with the last printable character");
    memset(desc.file, 0, sizeof desc.file);
    memcpy(desc.file, "ab c", 4u);
    ut_check(!npc_spawn_note_desc_is_sound(&desc), "a blank inside the name");
    memset(desc.file, 0, sizeof desc.file);
    memcpy(desc.file, "ab\0c", 4u);
    ut_check(!npc_spawn_note_desc_is_sound(&desc),
             "anything after the end, which would make two equal names two different records");
    memset(desc.file, 0, sizeof desc.file);
    desc.file[0] = '\x7F';
    ut_check(!npc_spawn_note_desc_is_sound(&desc), "a character that is not printable");
    desc.file[0] = '\xE4';
    ut_check(!npc_spawn_note_desc_is_sound(&desc), "or not ASCII");

    desc = sound_desc();
    desc.position[1] = (float)NAN;
    ut_check(!npc_spawn_note_desc_is_sound(&desc), "a place that is not a number");
    desc = sound_desc();
    desc.position[0] = (float)NAN;
    ut_check(!npc_spawn_note_desc_is_sound(&desc), "in its first coordinate");
    desc = sound_desc();
    desc.position[2] = (float)-INFINITY;
    ut_check(!npc_spawn_note_desc_is_sound(&desc), "or its last");
    desc = sound_desc();
    desc.facing = (float)INFINITY;
    ut_check(!npc_spawn_note_desc_is_sound(&desc), "a facing that is not finite");
    ut_check(!npc_spawn_note_desc_is_sound(NULL), "and no description at all");
}

static void check_wish_entries(void)
{
    npc_spawn_wish_t wish = spawn_wish();

    ut_section("a wish is a known kind, and only a spawn or a restore carries a description");

    ut_check(npc_spawn_note_wish_is_sound(&wish), "a spawn");
    wish.kind = NPC_SPAWN_WISH_RESTORE;
    ut_check(npc_spawn_note_wish_is_sound(&wish), "a restore");
    wish.desc.behaviour = NPC_SPAWN_BEHAVIOURS;
    ut_check(!npc_spawn_note_wish_is_sound(&wish), "a restore whose description is not sound");
    wish = spawn_wish();
    wish.kind = NPC_SPAWN_WISH_REMOVE_OWN;
    ut_check(!npc_spawn_note_wish_is_sound(&wish), "a removal with a description left in it");
    memset(&wish.desc, 0, sizeof wish.desc);
    ut_check(npc_spawn_note_wish_is_sound(&wish), "a removal of one's own");
    wish.kind = NPC_SPAWN_WISH_REMOVE_ALL;
    ut_check(npc_spawn_note_wish_is_sound(&wish), "a removal of all");
    wish.reserved = 1u;
    ut_check(!npc_spawn_note_wish_is_sound(&wish), "a reserved field that is not zero");
    wish = spawn_wish();
    wish.kind = 0u;
    ut_check(!npc_spawn_note_wish_is_sound(&wish), "kind 0");
    wish.kind = NPC_SPAWN_WISH_RESTORE + 1u;
    ut_check(!npc_spawn_note_wish_is_sound(&wish), "the kind after the last");
}

static void check_grant_entries(void)
{
    npc_spawn_grant_t grant = build_grant(256u);

    ut_section("a grant names a copy's key, an owner inside the session, and answers a wish");

    ut_check(npc_spawn_note_grant_is_sound(&grant), "a build of the first copy");
    grant.key = 383u;
    ut_check(npc_spawn_note_grant_is_sound(&grant), "and of the last");
    grant.key = 255u;
    ut_check(!npc_spawn_note_grant_is_sound(&grant), "a build on 255");
    grant.key = 384u;
    ut_check(!npc_spawn_note_grant_is_sound(&grant), "a build past the pool");
    grant = build_grant(300u);
    grant.owner = NPC_SPAWN_WORLD_SLOTS - 1u;
    ut_check(npc_spawn_note_grant_is_sound(&grant), "the last world slot as its owner");
    grant.owner = NPC_SPAWN_WORLD_SLOTS;
    ut_check(!npc_spawn_note_grant_is_sound(&grant), "an owner past the session");
    grant = build_grant(300u);
    grant.desc.file[0] = '\0';
    ut_check(!npc_spawn_note_grant_is_sound(&grant), "a build without a file");

    grant = build_grant(300u);
    grant.kind = NPC_SPAWN_GRANT_CANCEL;
    ut_check(!npc_spawn_note_grant_is_sound(&grant), "a cancel with a description left in it");
    memset(&grant.desc, 0, sizeof grant.desc);
    ut_check(npc_spawn_note_grant_is_sound(&grant), "a cancel");
    grant.kind = NPC_SPAWN_GRANT_OWNER;
    ut_check(npc_spawn_note_grant_is_sound(&grant), "an owner change");
    grant.key = 12u;
    ut_check(!npc_spawn_note_grant_is_sound(&grant), "an owner change of a placement");

    memset(&grant, 0, sizeof grant);
    grant.kind   = NPC_SPAWN_GRANT_REFUSED;
    grant.wish   = 7u;
    grant.reason = NPC_SPAWN_REFUSED_CAP;
    ut_check(npc_spawn_note_grant_is_sound(&grant), "a refusal of wish 7 for the cap");
    grant.desc.file[0] = 'a';
    ut_check(!npc_spawn_note_grant_is_sound(&grant), "a refusal that carries a description");
    grant.desc.file[0] = '\0';
    grant.reason = 0u;
    ut_check(!npc_spawn_note_grant_is_sound(&grant), "a refusal that gives no reason");
    grant.reason = NPC_SPAWN_REFUSED_POOL;
    grant.wish   = 0u;
    ut_check(!npc_spawn_note_grant_is_sound(&grant), "a refusal of no wish");
    grant.wish = 7u;
    grant.key  = 256u;
    ut_check(!npc_spawn_note_grant_is_sound(&grant), "a refusal that names a key");
    grant.key      = 0u;
    grant.reserved = 1u;
    ut_check(!npc_spawn_note_grant_is_sound(&grant), "a reserved field that is not zero");
    grant.reserved = 0u;
    grant.kind     = NPC_SPAWN_GRANT_OWNER + 1u;
    ut_check(!npc_spawn_note_grant_is_sound(&grant), "the kind after the last");
}

static void check_the_wishes_travel(void)
{
    npc_spawn_wish_record_t said;
    npc_spawn_wish_record_t heard;
    npc_spawn_wish_t        zero;

    ut_section("the wish record arrives as it was said, with the unused entries zeroed");

    memset(&said, 0x5A, sizeof said);
    said.count       = 2u;
    said.panel       = NPC_SPAWN_PANEL_BUILDER | NPC_SPAWN_PANEL_SAVE_HULL;
    said.first       = 41u;
    said.grants_done = 9u;
    said.refused     = 0x2u;
    said.entry[0]    = spawn_wish();
    said.entry[1]    = spawn_wish();
    said.entry[1].desc.flags = NPC_SPAWN_DESC_ARCHIVE;
    ut_check(npc_spawn_note_publish_wishes(&said),
             "published, the bytes past the count being none of the reader's business");
    memset(&heard, 0, sizeof heard);
    ut_check(npc_spawn_note_read_wishes(&heard), "and read back");
    ut_check(heard.version == 1u && heard.count == 2u && heard.first == 41u &&
                 heard.grants_done == 9u && heard.refused == 0x2u &&
                 heard.panel == (NPC_SPAWN_PANEL_BUILDER | NPC_SPAWN_PANEL_SAVE_HULL),
             "with every field as said");
    ut_check(memcmp(heard.entry, said.entry, 2u * sizeof said.entry[0]) == 0,
             "the two entries byte for byte");
    memset(&zero, 0, sizeof zero);
    ut_check(memcmp(&heard.entry[2], &zero, sizeof zero) == 0 &&
                 memcmp(&heard.entry[5], &zero, sizeof zero) == 0 && heard.reserved == 0u,
             "and what was past the count, and the reserved field, as zero");

    said.count = NPC_SPAWN_WISH_SLOTS + 1u;
    ut_check(!npc_spawn_note_publish_wishes(&said), "more entries than the record holds");
    said.count = 2u;
    said.entry[1].kind = 0u;
    ut_check(!npc_spawn_note_publish_wishes(&said), "an unsound entry inside the count");
    memset(&heard, 0, sizeof heard);
    ut_check(npc_spawn_note_read_wishes(&heard) && heard.first == 41u && heard.count == 2u &&
                 heard.entry[1].kind == NPC_SPAWN_WISH_SPAWN,
             "and after both refusals the reader still sees the record before them");
    said.count = 1u;
    ut_check(npc_spawn_note_publish_wishes(&said), "an unsound entry past the count is ignored");
    ut_check(npc_spawn_note_read_wishes(&heard) && heard.count == 1u &&
                 memcmp(&heard.entry[1], &zero, sizeof zero) == 0,
             "and published as zero");
}

static void check_the_grants_and_anchors_travel(void)
{
    npc_spawn_grant_record_t  said;
    npc_spawn_grant_record_t  heard;
    npc_spawn_anchor_record_t where;
    npc_spawn_anchor_record_t there;

    ut_section("the grant and the anchor records arrive as they were said");

    memset(&said, 0, sizeof said);
    said.count        = 3u;
    said.own_slot     = 2u;
    said.first        = 7u;
    said.wishes_taken = 40u;
    said.cap          = 16u;
    said.epoch        = 5u;
    said.entry[0]     = build_grant(256u);
    said.entry[1]     = build_grant(290u);
    said.entry[2].kind   = NPC_SPAWN_GRANT_REFUSED;
    said.entry[2].wish   = 40u;
    said.entry[2].reason = NPC_SPAWN_REFUSED_NO_LEVEL;
    ut_check(npc_spawn_note_publish_grants(&said), "three grants published");
    memset(&heard, 0, sizeof heard);
    ut_check(npc_spawn_note_read_grants(&heard), "and read back");
    ut_check(heard.version == 1u && heard.count == 3u && heard.own_slot == 2u &&
                 heard.first == 7u && heard.wishes_taken == 40u && heard.cap == 16u &&
                 heard.epoch == 5u && memcmp(heard.entry, said.entry, sizeof said.entry) == 0,
             "every field and entry as said");
    said.own_slot = NPC_SPAWN_WORLD_SLOTS;
    ut_check(!npc_spawn_note_publish_grants(&said), "a machine outside the session's slots");
    said.own_slot = 2u;
    said.count    = NPC_SPAWN_GRANT_SLOTS + 1u;
    ut_check(!npc_spawn_note_publish_grants(&said), "more grants than the record holds");

    memset(&where, 0x33, sizeof where);
    where.valid       = (uint16_t)((1u << 0) | (1u << 15));
    where.epoch       = 5u;
    where.point[0][0] = 10.0f;
    where.point[0][1] = 20.0f;
    where.point[0][2] = 30.0f;
    where.point[15][0] = -1.0f;
    where.point[15][1] = -2.0f;
    where.point[15][2] = -3.0f;
    where.point[4][1]  = (float)NAN;
    ut_check(npc_spawn_note_publish_anchors(&where),
             "anchors published, a NaN in a slot that is not valid being no point");
    memset(&there, 0, sizeof there);
    ut_check(npc_spawn_note_read_anchors(&there), "and read back");
    ut_check(there.version == 1u && there.valid == where.valid && there.epoch == 5u &&
                 there.point[0][2] == 30.0f && there.point[15][0] == -1.0f,
             "with the two valid points");
    ut_check(there.point[4][1] == 0.0f && there.point[1][0] == 0.0f,
             "and zero in every slot that is not valid");
    where.valid = (uint16_t)(where.valid | (1u << 4));
    ut_check(!npc_spawn_note_publish_anchors(&where), "a valid slot whose point is not finite");
}

static void check_another_shape_is_refused(void)
{
    npc_spawn_wish_record_t raw;
    npc_spawn_wish_record_t heard;
    uint8_t                 foreign[16];

    ut_section("a record this build would not have published is refused, and nothing is written");

    memset(&heard, 0x77, sizeof heard);
    memset(foreign, 0xAB, sizeof foreign);
    ut_check(shared_note_publish(NPC_SPAWN_WISH_NOTE_NAME, foreign, sizeof foreign),
             "something shorter is filed under the name");
    ut_check(!npc_spawn_note_read_wishes(&heard) && heard.first == 0x77777777u,
             "refused, the caller's record untouched");

    memset(&raw, 0, sizeof raw);
    raw.version  = 2u;
    raw.count    = 1u;
    raw.entry[0] = spawn_wish();
    ut_check(shared_note_publish(NPC_SPAWN_WISH_NOTE_NAME, &raw, sizeof raw) &&
                 !npc_spawn_note_read_wishes(&heard),
             "a version this build does not know");
    raw.version = 1u;
    raw.count   = NPC_SPAWN_WISH_SLOTS + 1u;
    ut_check(shared_note_publish(NPC_SPAWN_WISH_NOTE_NAME, &raw, sizeof raw) &&
                 !npc_spawn_note_read_wishes(&heard),
             "a count past the slots");
    raw.count = 1u;
    raw.entry[0].desc.file[0] = ' ';
    ut_check(shared_note_publish(NPC_SPAWN_WISH_NOTE_NAME, &raw, sizeof raw) &&
                 !npc_spawn_note_read_wishes(&heard),
             "an entry the publisher would have refused");
    ut_check(heard.first == 0x77777777u, "and still nothing was written into the caller's");
    raw.entry[0] = spawn_wish();
    ut_check(shared_note_publish(NPC_SPAWN_WISH_NOTE_NAME, &raw, sizeof raw) &&
                 npc_spawn_note_read_wishes(&heard) && heard.count == 1u,
             "while the same record, sound, is read");
}

static void check_a_count_past_the_slots(void)
{
    /* The entry after the last slot is a sound one, so that a bound one too wide reads it and
     * accepts, instead of reading whatever the stack holds there and refusing by luck. */
    struct {
        npc_spawn_wish_record_t record;
        npc_spawn_wish_t        after;
    } wishes;
    struct {
        npc_spawn_grant_record_t record;
        npc_spawn_grant_t        after;
    } grants;
    size_t i;

    ut_section("a count one past the slots is refused even when the bytes after them look sound");

    memset(&wishes, 0, sizeof wishes);
    for (i = 0; i < NPC_SPAWN_WISH_SLOTS; ++i) {
        wishes.record.entry[i] = spawn_wish();
    }
    wishes.after        = spawn_wish();
    wishes.record.count = NPC_SPAWN_WISH_SLOTS;
    ut_check(npc_spawn_note_publish_wishes(&wishes.record), "every slot in use is published");
    wishes.record.count = NPC_SPAWN_WISH_SLOTS + 1u;
    ut_check(!npc_spawn_note_publish_wishes(&wishes.record), "one more than the slots is not");

    memset(&grants, 0, sizeof grants);
    for (i = 0; i < NPC_SPAWN_GRANT_SLOTS; ++i) {
        grants.record.entry[i] = build_grant((uint16_t)(256u + i));
    }
    grants.after        = build_grant(300u);
    grants.record.count = NPC_SPAWN_GRANT_SLOTS;
    ut_check(npc_spawn_note_publish_grants(&grants.record), "every grant slot in use is published");
    grants.record.count = NPC_SPAWN_GRANT_SLOTS + 1u;
    ut_check(!npc_spawn_note_publish_grants(&grants.record), "one more than the slots is not");
}

static void check_other_shapes_of_grants_and_anchors(void)
{
    npc_spawn_grant_record_t  grants;
    npc_spawn_grant_record_t  heard;
    npc_spawn_anchor_record_t anchors;
    npc_spawn_anchor_record_t there;
    npc_spawn_wish_record_t   wishes;
    npc_spawn_wish_record_t   wishes_heard;

    ut_section("the grant and anchor readers refuse what their publishers would, untouched");

    memset(&grants, 0, sizeof grants);
    grants.count    = 2u;
    grants.entry[0] = build_grant(256u);
    ut_check(!npc_spawn_note_publish_grants(&grants), "an unsound grant inside the count");
    grants.version  = 1u;
    grants.count    = 1u;
    grants.entry[0].kind = NPC_SPAWN_GRANT_OWNER + 1u;
    memset(&heard, 0x77, sizeof heard);
    ut_check(shared_note_publish(NPC_SPAWN_GRANT_NOTE_NAME, &grants, sizeof grants) &&
                 !npc_spawn_note_read_grants(&heard) && heard.first == 0x77777777u,
             "and filed raw, it is not read, the caller's record untouched");
    grants.entry[0] = build_grant(256u);
    grants.version  = 2u;
    ut_check(shared_note_publish(NPC_SPAWN_GRANT_NOTE_NAME, &grants, sizeof grants) &&
                 !npc_spawn_note_read_grants(&heard) && heard.first == 0x77777777u,
             "a grant record of another version");

    memset(&anchors, 0, sizeof anchors);
    anchors.version = 2u;
    memset(&there, 0x77, sizeof there);
    ut_check(shared_note_publish(NPC_SPAWN_ANCHOR_NOTE_NAME, &anchors, sizeof anchors) &&
                 !npc_spawn_note_read_anchors(&there) && there.epoch == 0x77u,
             "an anchor record of another version, the caller's untouched");
    anchors.version      = 1u;
    anchors.valid        = (uint16_t)(1u << 15);
    anchors.point[15][1] = (float)NAN;
    ut_check(shared_note_publish(NPC_SPAWN_ANCHOR_NOTE_NAME, &anchors, sizeof anchors) &&
                 !npc_spawn_note_read_anchors(&there) && there.epoch == 0x77u,
             "an anchor whose last valid slot is not finite");

    /* The first twenty bytes of a sound record: version one and no entries, which is all a
     * reader that took a short record would look at before it accepted it. */
    memset(&wishes, 0, sizeof wishes);
    wishes.version = 1u;
    memset(&wishes_heard, 0x77, sizeof wishes_heard);
    ut_check(shared_note_publish(NPC_SPAWN_WISH_NOTE_NAME, &wishes,
                                 offsetof(npc_spawn_wish_record_t, entry)) &&
                 !npc_spawn_note_read_wishes(&wishes_heard) && wishes_heard.first == 0x77777777u,
             "a record cut short after its head, however sound the head");
}

static void check_what_is_past_the_count_is_zeroed(void)
{
    npc_spawn_grant_record_t said;
    npc_spawn_grant_record_t heard;
    npc_spawn_grant_t        zero;

    ut_section("a grant record is published with nothing but zero past its count");

    memset(&said, 0x5A, sizeof said);
    said.count    = 1u;
    said.own_slot = 3u;
    said.entry[0] = build_grant(260u);
    ut_check(npc_spawn_note_publish_grants(&said) && npc_spawn_note_read_grants(&heard),
             "one grant, the rest of the record full of other bytes");
    memset(&zero, 0, sizeof zero);
    ut_check(memcmp(&heard.entry[1], &zero, sizeof zero) == 0 &&
                 memcmp(&heard.entry[4], &zero, sizeof zero) == 0 && heard.reserved == 0u,
             "reads back with zero past the count and in the reserved byte");
}

static void check_no_record_at_all(void)
{
    npc_spawn_wish_record_t   wishes;
    npc_spawn_anchor_record_t anchors;

    ut_section("no record is never published and never read into");

    /* Every note holds a sound record first, so that only the missing destination can refuse. */
    memset(&wishes, 0, sizeof wishes);
    memset(&anchors, 0, sizeof anchors);
    ut_check(npc_spawn_note_publish_wishes(&wishes) && npc_spawn_note_publish_anchors(&anchors) &&
                 npc_spawn_note_read_wishes(&wishes) && npc_spawn_note_read_anchors(&anchors),
             "a sound wish record and a sound anchor record are there to read");

    ut_check(!npc_spawn_note_publish_wishes(NULL) && !npc_spawn_note_publish_grants(NULL) &&
                 !npc_spawn_note_publish_anchors(NULL),
             "nothing to publish is refused");
    ut_check(!npc_spawn_note_read_wishes(NULL) && !npc_spawn_note_read_grants(NULL) &&
                 !npc_spawn_note_read_anchors(NULL),
             "nowhere to read into is refused");
}

int main(void)
{
    check_nothing_published_yet();
    check_keys();
    check_descriptions();
    check_wish_entries();
    check_grant_entries();
    check_the_wishes_travel();
    check_the_grants_and_anchors_travel();
    check_another_shape_is_refused();
    check_a_count_past_the_slots();
    check_other_shapes_of_grants_and_anchors();
    check_what_is_past_the_count_is_zeroed();
    check_no_record_at_all();
    return ut_summary("npc_spawn_note");
}
