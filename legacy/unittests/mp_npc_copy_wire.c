/* mp_npc_copy_wire.c: the two messages of the NPC copies and the description they carry.
 *
 * What would be silent if it were wrong: a rounding that is not idempotent, so the host's own
 * overlay and a client build a copy a hundredth of a unit apart and every later comparison of the
 * two descriptions fails; a form the rules refuse that goes out anyway, which the other side reads
 * as a copy under a key it may not use; and a byte in the wrong place, which a build of the other
 * tree reads as another field.
 */
#include "unittest.h"

#include "mp_npc_copy_wire.h"
#include "mp_wire.h"

#include "common/npc_spawn_note.h"

#include <math.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <string.h>

static uint32_t s_random = 0x4B2u;

static uint32_t next_random(void)
{
    s_random = s_random * 1664525u + 1013904223u;
    return s_random >> 8;
}

static npc_spawn_note_desc_t a_desc(void)
{
    npc_spawn_note_desc_t desc;

    memset(&desc, 0, sizeof desc);
    desc.source      = 17u;
    desc.behaviour   = 2u;
    desc.position[0] = 120.25f;
    desc.position[1] = -2.0f;
    desc.position[2] = 45.5f;
    desc.facing      = 90.0f;
    memcpy(desc.file, "droideka.baf", NPC_SPAWN_FILE_MAX);
    return desc;
}

static void check_the_description(void)
{
    npc_spawn_note_desc_t desc = a_desc();
    npc_spawn_note_desc_t back = {0};
    uint8_t               once[MP_NPC_COPY_DESC_BYTES];
    uint8_t               twice[MP_NPC_COPY_DESC_BYTES];

    ut_section("a description in 23 bytes, rounded once and for good");

    ut_check(mp_npc_copy_desc_put(&desc, once) && mp_npc_copy_desc_get(once, &back),
             "a sound description goes out and comes back");
    ut_check(back.source == 17u && back.behaviour == 2u && back.flags == 0u &&
                 memcmp(back.file, desc.file, NPC_SPAWN_FILE_MAX) == 0,
             "with its source, behaviour, flags and file as they were");
    ut_check(fabsf(back.position[0] - 120.25f) < 0.01f && fabsf(back.position[1] + 2.0f) < 0.01f &&
                 fabsf(back.facing - 90.0f) < 0.01f,
             "and its place and facing within the wire's step");
    ut_check(mp_npc_copy_desc_put(&back, twice) && memcmp(once, twice, sizeof once) == 0,
             "and what came back goes out as the same bytes");
    ut_check(once[0] == 17u && once[1] == 2u && once[2] == 0u &&
                 memcmp(once + 11, "droideka.baf", NPC_SPAWN_FILE_MAX) == 0,
             "source, behaviour and flags lead, the file closes it at byte 11");

    back = desc;
    ut_check(mp_npc_copy_desc_round(&back) && mp_npc_copy_desc_round(&back) &&
                 mp_npc_copy_desc_put(&back, twice) && memcmp(once, twice, sizeof once) == 0,
             "rounding twice is rounding once");

    desc.position[0] = 400.0f;
    ut_check(!mp_npc_copy_desc_put(&desc, once), "a place past the wire's range is refused");
    desc.position[0] = -129.0f;
    ut_check(!mp_npc_copy_desc_put(&desc, once), "and before it");
    back = desc;
    ut_check(!mp_npc_copy_desc_round(&back) && back.position[0] == -129.0f,
             "and a rounding that cannot happen leaves the description alone");
    desc = a_desc();
    desc.file[0] = ' ';
    ut_check(!mp_npc_copy_desc_put(&desc, once), "a description the contract calls unsound");
    memset(once, 0, sizeof once);
    ut_check(!mp_npc_copy_desc_get(once, &back), "and 23 zero bytes are no description");
}

static void check_random_descriptions(void)
{
    unsigned bad = 0;
    unsigned i;

    ut_section("two thousand random descriptions: one rounding, then fixed");

    for (i = 0; i < 2000u; ++i) {
        npc_spawn_note_desc_t desc;
        uint8_t               a[MP_NPC_COPY_DESC_BYTES];
        uint8_t               b[MP_NPC_COPY_DESC_BYTES];
        size_t                length = 1u + next_random() % NPC_SPAWN_FILE_MAX;
        size_t                c;

        memset(&desc, 0, sizeof desc);
        desc.flags       = (uint8_t)(next_random() & 1u);
        desc.source      = (uint8_t)(next_random() % (desc.flags != 0u ? 256u : 255u));
        desc.behaviour   = (uint8_t)(next_random() % NPC_SPAWN_BEHAVIOURS);
        desc.position[0] = (float)(next_random() % 51200u) / 100.0f - 128.0f;
        desc.position[1] = (float)(next_random() % 51200u) / 100.0f - 128.0f;
        desc.position[2] = (float)(next_random() % 51200u) / 100.0f - 128.0f;
        desc.facing      = (float)(next_random() % 72000u) / 100.0f - 360.0f;
        for (c = 0; c < length; ++c) {
            desc.file[c] = (char)('!' + next_random() % 94u);
        }
        if (!mp_npc_copy_desc_round(&desc) || !mp_npc_copy_desc_put(&desc, a) ||
            !mp_npc_copy_desc_round(&desc) || !mp_npc_copy_desc_put(&desc, b) ||
            memcmp(a, b, sizeof a) != 0) {
            ++bad;
        }
    }
    ut_checkf(bad == 0u, "every one of them (%u were not)", bad);
}

static void check_the_wish(void)
{
    mp_npc_copy_wish_t wish;
    mp_npc_copy_wish_t back = {0};
    uint8_t            buffer[40];
    size_t             bytes;

    ut_section("a wish is 30 bytes, a spawn or a removal of one's own, and nothing else");

    memset(&wish, 0, sizeof wish);
    wish.kind   = NPC_SPAWN_WISH_SPAWN;
    wish.serial = 0x1234u;
    wish.level  = 0x0203u;
    wish.world  = 7u;
    wish.desc   = a_desc();
    bytes = mp_npc_copy_wish_encode(&wish, buffer, sizeof buffer);
    ut_check(bytes == MP_NPC_COPY_WISH_BYTES && buffer[0] == MP_NPC_COPY_WISH_TAG &&
                 buffer[1] == NPC_SPAWN_WISH_SPAWN && buffer[6] == 7u,
             "a spawn: the tag, the kind, and the world at byte 6");
    ut_check(mp_npc_copy_wish_is(buffer, bytes) && mp_npc_copy_wish_decode(buffer, bytes, &back) &&
                 back.serial == 0x1234u && back.level == 0x0203u && back.world == 7u &&
                 back.desc.source == 17u,
             "and back, whole");
    ut_check(!mp_npc_copy_wish_is(buffer, bytes - 1u) && !mp_npc_copy_wish_is(buffer, bytes + 1u),
             "one byte short or long is not a wish");
    buffer[0] = MP_NPC_COPY_ENTRY_TAG;
    ut_check(!mp_npc_copy_wish_is(buffer, bytes), "nor another tag at that length");

    wish.serial = 0u;
    ut_check(mp_npc_copy_wish_encode(&wish, buffer, sizeof buffer) == MP_NPC_COPY_WISH_BYTES,
             "serial 0 travels: every 65536th serial in the note ends in it");
    wish.kind = NPC_SPAWN_WISH_REMOVE_OWN;
    ut_check(mp_npc_copy_wish_encode(&wish, buffer, sizeof buffer) == 0u,
             "a removal with a description is refused");
    memset(&wish.desc, 0, sizeof wish.desc);
    bytes = mp_npc_copy_wish_encode(&wish, buffer, sizeof buffer);
    ut_check(bytes == MP_NPC_COPY_WISH_BYTES && mp_npc_copy_wish_decode(buffer, bytes, &back) &&
                 back.kind == NPC_SPAWN_WISH_REMOVE_OWN,
             "without one it travels");
    buffer[20] = 1u;
    ut_check(!mp_npc_copy_wish_decode(buffer, bytes, &back),
             "and a removal whose description bytes are not zero is not read");
    wish.kind = NPC_SPAWN_WISH_RESTORE;
    ut_check(mp_npc_copy_wish_encode(&wish, buffer, sizeof buffer) == 0u,
             "a restore never travels");
    wish.kind = NPC_SPAWN_WISH_REMOVE_ALL;
    ut_check(mp_npc_copy_wish_encode(&wish, buffer, sizeof buffer) == 0u,
             "nor a removal of all");
    wish.kind = NPC_SPAWN_WISH_SPAWN;
    wish.desc = a_desc();
    bytes     = mp_npc_copy_wish_encode(&wish, buffer, sizeof buffer);
    buffer[1] = NPC_SPAWN_WISH_RESTORE;
    ut_check(!mp_npc_copy_wish_decode(buffer, bytes, &back),
             "and one that arrives anyway is not read");
}

static mp_npc_copy_entry_t a_build(void)
{
    mp_npc_copy_entry_t entry;

    memset(&entry, 0, sizeof entry);
    entry.kind       = NPC_SPAWN_GRANT_BUILD;
    entry.k          = 0x7Fu;
    entry.generation = 9u;
    entry.owner      = 3u;
    entry.level      = 0x4455u;
    entry.world      = 2u;
    entry.desc       = a_desc();
    return entry;
}

static void check_the_entry(void)
{
    mp_npc_copy_entry_t entry = a_build();
    mp_npc_copy_entry_t back = {0};
    uint8_t             buffer[40];
    size_t              bytes;

    ut_section("an entry is 32 bytes: a copy to build, or a wish refused");

    bytes = mp_npc_copy_entry_encode(&entry, buffer, sizeof buffer);
    ut_check(bytes == MP_NPC_COPY_ENTRY_BYTES && buffer[0] == MP_NPC_COPY_ENTRY_TAG &&
                 buffer[1] == NPC_SPAWN_GRANT_BUILD && buffer[4] == 9u && buffer[5] == 3u &&
                 buffer[8] == 2u && buffer[9] == 17u,
             "a build: generation at 4, owner at 5, world at 8, the description from 9");
    ut_check(mp_npc_copy_entry_is(buffer, bytes) &&
                 mp_npc_copy_entry_decode(buffer, bytes, &back) && back.k == 0x7Fu &&
                 back.generation == 9u && back.owner == 3u && back.level == 0x4455u &&
                 back.desc.behaviour == 2u,
             "and back, whole");
    ut_check(!mp_npc_copy_entry_is(buffer, bytes - 1u) &&
                 !mp_npc_copy_entry_is(buffer, bytes + 1u),
             "one byte short or long is not an entry");
    buffer[0] = MP_NPC_COPY_WISH_TAG;
    ut_check(!mp_npc_copy_entry_is(buffer, bytes), "nor the right length under another tag");
    buffer[0] = MP_NPC_COPY_ENTRY_TAG;

    entry.k = MP_WIRE_COPY_MAX;
    ut_check(mp_npc_copy_entry_encode(&entry, buffer, sizeof buffer) == 0u,
             "a build past the copies is refused");
    entry            = a_build();
    entry.generation = 0u;
    ut_check(mp_npc_copy_entry_encode(&entry, buffer, sizeof buffer) == 0u,
             "a build of life 0, which every reader takes for unknown");
    entry       = a_build();
    entry.owner = NPC_SPAWN_WORLD_SLOTS;
    ut_check(mp_npc_copy_entry_encode(&entry, buffer, sizeof buffer) == 0u,
             "an owner past the session");
    entry = a_build();
    bytes = mp_npc_copy_entry_encode(&entry, buffer, sizeof buffer);
    buffer[4] = 0u;
    ut_check(!mp_npc_copy_entry_decode(buffer, bytes, &back),
             "a build of life 0 that arrives is not read");

    memset(&entry, 0, sizeof entry);
    entry.kind   = NPC_SPAWN_GRANT_REFUSED;
    entry.serial = 0xBEEFu;
    entry.reason = NPC_SPAWN_REFUSED_CAP;
    entry.owner  = 4u;
    entry.level  = 0x0101u;
    entry.world  = 5u;
    bytes = mp_npc_copy_entry_encode(&entry, buffer, sizeof buffer);
    ut_check(bytes == MP_NPC_COPY_ENTRY_BYTES && buffer[4] == NPC_SPAWN_REFUSED_CAP &&
                 buffer[5] == 4u,
             "a refusal: its reason where a build has its life, its asker where the owner is");
    ut_check(mp_npc_copy_entry_decode(buffer, bytes, &back) && back.serial == 0xBEEFu &&
                 back.reason == NPC_SPAWN_REFUSED_CAP && back.owner == 4u && back.world == 5u &&
                 back.k == 0u && back.generation == 0u,
             "and back, with the level and world it was given, and no k or life");
    buffer[30] = 1u;
    ut_check(!mp_npc_copy_entry_decode(buffer, bytes, &back),
             "a refusal whose description bytes are not zero is not read");
    entry.reason = 0u;
    ut_check(mp_npc_copy_entry_encode(&entry, buffer, sizeof buffer) == 0u,
             "a refusal without a reason is not sent");
    entry.reason = NPC_SPAWN_REFUSED_POOL;
    entry.desc   = a_desc();
    ut_check(mp_npc_copy_entry_encode(&entry, buffer, sizeof buffer) == 0u,
             "nor one with a description");
    memset(&entry, 0, sizeof entry);
    entry.kind   = NPC_SPAWN_GRANT_CANCEL;
    ut_check(mp_npc_copy_entry_encode(&entry, buffer, sizeof buffer) == 0u,
             "a cancel never travels: a removal goes the relay's way");
}

int main(void)
{
    check_the_description();
    check_random_descriptions();
    check_the_wish();
    check_the_entry();
    return ut_summary("mp_npc_copy_wire");
}
