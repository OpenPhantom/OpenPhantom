/* character_clipcopy.c: the private clips of each pair, built from clips in the test's own memory.
 *
 * A clip here is laid out the way the engine's parser leaves one: a 0x84 byte header with the node
 * count at +0x38 and the node table at +0x3C, node records of 0x2C bytes with the entry count at
 * +0x24 and the entry pool at +0x28, and entries of 0x38 bytes with the offset at +0x08. The arenas
 * are the module's own, reserved and committed as they are in the game.
 *
 * What is held still is what the pairs must not share and what they must: every pair has its own
 * cache and its own arena, a second wearer never throws a pair's copies away, one pair's release
 * leaves another's copies whole, and a record without a shift still points into the source's own
 * pool, which is why a pair may live no longer than the asset it was built from.
 */
#include "unittest.h"

#include "character_bodies.h"
#include "character_clipcopy.h"

#include <windows.h>

#include <stdbool.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

#define CLIP_HEADER_BYTES 0x84u
#define CLIP_NUM_NODES    0x38u
#define CLIP_TYPE         0x2Cu
#define CLIP_NODES        0x3Cu
#define CLIPNODE_BYTES    0x2Cu
#define CLIPNODE_ENTRIES  0x24u
#define CLIPNODE_POOL     0x28u
#define KEYENTRY_BYTES    0x38u
#define ENTRIES           3u
#define CLIPS             (192u + 2u)

typedef struct fake_clip {
    uint8_t header[CLIP_HEADER_BYTES];
    uint8_t records[2u * CLIPNODE_BYTES];
    uint8_t pool[2u][ENTRIES * KEYENTRY_BYTES];
} fake_clip_t;

static fake_clip_t *clips;

static void put_u32(uint8_t *at, uint32_t value)
{
    memcpy(at, &value, sizeof value);
}

static uint32_t get_u32(uintptr_t at)
{
    uint32_t value;

    memcpy(&value, (const void *)at, sizeof value);
    return value;
}

static float get_float(uintptr_t at)
{
    float value;

    memcpy(&value, (const void *)at, sizeof value);
    return value;
}

/* A two node clip whose second record's first entry sits at an offset of 1.0. */
static void build_clip(fake_clip_t *clip)
{
    uint32_t r;
    float    one = 1.0f;

    memset(clip, 0, sizeof *clip);
    put_u32(clip->header + CLIP_NUM_NODES, 2u);
    put_u32(clip->header + CLIP_TYPE, 0xFFFFu);
    put_u32(clip->header + CLIP_NODES, (uint32_t)(uintptr_t)clip->records);
    for (r = 0; r < 2u; ++r) {
        put_u32(clip->records + r * CLIPNODE_BYTES + CLIPNODE_ENTRIES, ENTRIES);
        put_u32(clip->records + r * CLIPNODE_BYTES + CLIPNODE_POOL,
                (uint32_t)(uintptr_t)clip->pool[r]);
    }
    memcpy(&clip->pool[1][0x08u], &one, sizeof one);
}

/* The reservation an address lies in, by the system's own account of it. */
static void *reservation_of(uintptr_t address)
{
    MEMORY_BASIC_INFORMATION info;

    if (VirtualQuery((LPCVOID)address, &info, sizeof info) == 0) {
        return NULL;
    }
    return info.AllocationBase;
}

static uintptr_t record_of(uintptr_t copy, uint32_t j)
{
    return (uintptr_t)get_u32(copy + CLIP_NODES) + (uintptr_t)j * CLIPNODE_BYTES;
}

static void arm(uint32_t pair, float shift, bool *armed)
{
    int32_t        map[NODEMAP_MAX_NODES];
    uint32_t       type[NODEMAP_MAX_NODES];
    nodemap_rest_t rebase[NODEMAP_MAX_NODES];
    uint32_t       i;

    for (i = 0; i < NODEMAP_MAX_NODES; ++i) {
        map[i] = NODEMAP_NO_TRACK;
        type[i] = 0xFFFFu;
    }
    memset(rebase, 0, sizeof rebase);
    map[0] = 1;              /* the target's node 0 is driven by the reference's node 1 */
    map[1] = 0;
    rebase[0].pos[0] = shift;
    *armed = character_clipcopy_arm(pair, map, type, rebase, 2u, true);
}

int main(void)
{
    uintptr_t copy_a;
    uintptr_t copy_b;
    uintptr_t again;
    size_t    released;
    bool      armed = false;
    uint32_t  i;
    uint32_t  built = 0;

    clips = (fake_clip_t *)calloc(CLIPS, sizeof *clips);
    if (clips == NULL) {
        return 1;
    }
    for (i = 0; i < CLIPS; ++i) {
        build_clip(&clips[i]);
    }

    ut_section("the address space for every pair is reserved once");
    ut_check(character_clipcopy_reserve(), "the reservation stands");
    ut_check(character_clipcopy_reserve(), "and asking again is the same answer");
    ut_check(character_clipcopy_for(0u, (uintptr_t)&clips[0]) == 0u,
             "an unarmed pair copies nothing");

    ut_section("each pair builds its own copies");
    arm(0u, 0.0f, &armed);
    ut_check(armed, "pair 0 arms without a shift");
    arm(1u, 0.5f, &armed);
    ut_check(armed, "pair 1 arms with one");
    copy_a = character_clipcopy_for(0u, (uintptr_t)&clips[0]);
    copy_b = character_clipcopy_for(1u, (uintptr_t)&clips[0]);
    ut_check(copy_a != 0u && copy_b != 0u && copy_a != copy_b,
             "the same clip gets one copy per pair");
    ut_check(character_clipcopy_for(0u, (uintptr_t)&clips[0]) == copy_a,
             "and asking again answers the copy already built");
    ut_check(get_u32(copy_a + CLIP_NUM_NODES) == 2u, "the copy is in the target's node count");

    ut_section("pair 0's share is reserved apart from the far pairs' shares");
    ut_check(character_clipcopy_far_reserved(), "the far pairs' shares are reserved as well");
    ut_check(reservation_of(copy_a) != NULL && reservation_of(copy_a) != reservation_of(copy_b),
             "pair 0's copies lie in a reservation of their own, so a refused reservation of the "
             "far shares cannot take the player's own swap with it");

    ut_section("a record without a shift keeps the source's own pool");
    ut_check(get_u32(record_of(copy_a, 0u) + CLIPNODE_POOL) ==
             (uint32_t)(uintptr_t)clips[0].pool[1],
             "the target's node 0 reads the reference's node 1 entries where the asset keeps them");
    ut_check(get_u32(record_of(copy_b, 0u) + CLIPNODE_POOL) !=
             (uint32_t)(uintptr_t)clips[0].pool[1],
             "a shifted record gets a pool of its own");
    ut_near(get_float((uintptr_t)get_u32(record_of(copy_b, 0u) + CLIPNODE_POOL) + 0x08u), 1.5,
            1e-6, "and its entries carry the shift");
    ut_near(get_float((uintptr_t)clips[0].pool[1] + 0x08u), 1.0, 1e-6,
            "while the source's own entries are untouched");

    ut_section("a second wearer never starts a pair again");
    arm(0u, 0.25f, &armed);
    ut_check(!armed, "arming a pair that is armed is refused");
    ut_check(character_clipcopy_for(0u, (uintptr_t)&clips[0]) == copy_a,
             "and the copies its wearer poses from are still the same");

    ut_section("one pair's release leaves the others whole");
    released = character_clipcopy_release(0u);
    ut_check(released > 0u, "the released pair gives its arena back");
    ut_check(character_clipcopy_for(0u, (uintptr_t)&clips[0]) == 0u, "and copies nothing more");
    ut_check(character_clipcopy_for(1u, (uintptr_t)&clips[0]) == copy_b,
             "pair 1 still answers its copy");
    ut_check(get_u32(copy_b + CLIP_NUM_NODES) == 2u, "and the copy still reads");
    ut_check(character_clipcopy_release(0u) == 0u, "a pair released twice gives nothing back");
    arm(0u, 0.0f, &armed);
    ut_check(armed, "and a released pair can be armed again");
    again = character_clipcopy_for(0u, (uintptr_t)&clips[0]);
    ut_check(again != 0u && get_u32(again + CLIP_NUM_NODES) == 2u,
             "into committed memory it can write");

    ut_section("a full cache refuses the next clip and leaves the others working");
    arm(2u, 0.0f, &armed);
    for (i = 0; i < CLIPS; ++i) {
        if (character_clipcopy_for(2u, (uintptr_t)&clips[i]) != 0u) {
            ++built;
        }
    }
    ut_checkf(built == 192u, "192 copies and no more (%u)", (unsigned)built);
    ut_check(reservation_of(character_clipcopy_for(2u, (uintptr_t)&clips[0])) ==
                 reservation_of(copy_b),
             "and the far pairs' shares lie in one reservation");
    ut_check(character_clipcopy_for(1u, (uintptr_t)&clips[5]) != 0u,
             "another pair's cache is its own and still takes a clip");

    ut_section("the bounds");
    ut_check(character_clipcopy_for(PAIR_MAX, (uintptr_t)&clips[0]) == 0u, "no pair past the last");
    ut_check(character_clipcopy_release(PAIR_MAX) == 0u, "and no release of one");
    arm(PAIR_MAX, 0.0f, &armed);
    ut_check(!armed, "nor arming one");
    ut_check(character_clipcopy_for(1u, 0u) == 0u, "and no copy of no clip");

    for (i = 0; i < PAIR_MAX; ++i) {
        (void)character_clipcopy_release(i);
    }
    free(clips);
    return ut_summary("the clip copies of each pair");
}
