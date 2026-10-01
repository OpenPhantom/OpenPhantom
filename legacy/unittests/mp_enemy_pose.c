/* mp_enemy_pose.c: the census's read of a puppet, against the read it was before the swap.
 *
 * The host reads every enemy's clip, both playheads and its turned nodes once a substep, and the
 * reads moved from the asking form of common/memory, one VirtualQuery each, to the trying form, one
 * guarded copy each. That is only a swap if every record comes out the same, so the reading code
 * as it was is kept below word for word as the reference, and both are run over the same body made
 * up in this process's memory, case by case, until every branch of the old code has had its turn:
 * turned nodes 0, 2, 4 and 6 of 21, the 48 of the largest shipped rig, a count word with its high
 * bits set, no nodes and too many, a missing table, thing and puppet, a NaN angle, a base slot of
 * -1 and 4, an overlay on slot 2, a negative and a NaN playhead, a track with no keyframe, and a
 * node table whose end runs into a page nobody may read.
 *
 * The body sits in the first of two pages and the second is no access, so the last case is a real
 * fault and not a pretend one. The offsets are the module's: body thing +0x9C, clip +0xE8, base
 * slot +0xEC, overlay clip +0xF4, overlay slot +0xF8; thing asset +0x04, puppet +0x18, node table
 * +0x24; asset node count +0x54; puppet tracks from +0x08, 0x14C apart, each with its rate at
 * +0x10, the two times at +0x120 and +0x124, the keyframe at +0x128 and the mode at +0x13C; a
 * keyframe's frame count at +0x34.
 */
#include "unittest.h"

#include "mp_enemy_pose.h"
#include "mp_enemy_wire.h"

#include "common/memory.h"

#include <windows.h>

#include <math.h>
#include <stdbool.h>
#include <stdint.h>
#include <string.h>

/* ==============================================================================================
 * The reference: mp_enemy_pose.c's reading half before the swap, word for word.
 * ============================================================================================ */

#define B_THING         0x9Cu
#define B_CUR_CLIP      0xE8u
#define B_BASE_SLOT     0xECu
#define B_OVERLAY_CLIP  0xF4u
#define B_OVERLAY_SLOT  0xF8u
#define THING_ACTOR     0x04u
#define THING_PUPPET    0x18u
#define THING_NODE_ROT  0x24u
#define ACTOR_NODES     0x54u
#define KEYFRAME_FRAMES 0x34u
#define NODE_ROT_STRIDE 0x0Cu
#define PUPPET_TRACKS    0x08u
#define TRACK_STRIDE     0x14Cu
#define TRACK_FPS        0x10u
#define TRACK_TIME       0x120u
#define TRACK_TIME_PREV  0x124u
#define TRACK_KEYFRAME   0x128u
#define TRACK_MODE_FLAGS 0x13Cu
#define PUPPET_SLOTS     4
#define MODE_HOLDEND    0x01u
#define MODE_RELEASEEND 0x02u
#define ANGLE_SCALE 182.044444f

static bool thing_of(uint32_t body, uint32_t *out)
{
    return memory_read_u32((uintptr_t)body + B_THING, out) && *out != 0u;
}

static uintptr_t track_address(uint32_t body, uint32_t slot_offset, int32_t *slot_out,
                               uint32_t *puppet_out)
{
    uint32_t thing  = 0;
    uint32_t puppet = 0;
    int32_t  slot   = 0;

    if (!memory_read_u32((uintptr_t)body + slot_offset, (uint32_t *)&slot) ||
        slot < 0 || slot >= PUPPET_SLOTS) {
        return 0;   /* -1 is the engine's own "no track here", and it is the common case */
    }
    if (!thing_of(body, &thing) ||
        !memory_read_u32((uintptr_t)thing + THING_PUPPET, &puppet) || puppet == 0u) {
        return 0;
    }
    if (slot_out != NULL) {
        *slot_out = slot;
    }
    if (puppet_out != NULL) {
        *puppet_out = puppet;
    }
    return (uintptr_t)puppet + PUPPET_TRACKS + (uintptr_t)slot * TRACK_STRIDE;
}

static bool reference_base_track(uint32_t body, mp_enemy_pose_track_t *out)
{
    uintptr_t track;
    uint32_t  frames = 0;

    if (out == NULL) {
        return false;
    }
    track = track_address(body, B_BASE_SLOT, &out->slot, &out->puppet);
    out->track = (uint32_t)track;
    if (track == 0 || !memory_read_u32(track + TRACK_KEYFRAME, &out->keyframe) ||
        out->keyframe == 0u ||
        !memory_try_read(track + TRACK_FPS, &out->fps, sizeof out->fps) ||
        !memory_try_read(track + TRACK_TIME, &out->head, sizeof out->head) ||
        !memory_try_read(track + TRACK_TIME_PREV, &out->time_prev, sizeof out->time_prev) ||
        !memory_read_u32(track + TRACK_MODE_FLAGS, &out->mode_flags)) {
        return false;
    }
    if (out->head != out->head || out->time_prev != out->time_prev) {
        return false;   /* a NaN playhead is not a position */
    }
    if (out->time_prev > out->head) {
        out->head = out->time_prev;
    }
    out->num_frames = memory_read_u32((uintptr_t)out->keyframe + KEYFRAME_FRAMES, &frames)
                          ? (float)frames
                          : 0.0f;
    out->loops = (out->mode_flags & (MODE_HOLDEND | MODE_RELEASEEND)) == 0u;
    return true;
}

static uint32_t node_count(uint32_t body)
{
    uint32_t thing = 0;
    uint32_t asset = 0;
    uint32_t count = 0;

    if (!thing_of(body, &thing) ||
        !memory_read_u32((uintptr_t)thing + THING_ACTOR, &asset) || asset == 0u ||
        !memory_read_u32((uintptr_t)asset + ACTOR_NODES, &count)) {
        return 0;
    }
    return count;
}

static uintptr_t node_rotation_address(uint32_t body, uint32_t node)
{
    uint32_t thing = 0;
    uint32_t table = 0;

    if (node >= node_count(body) || !thing_of(body, &thing) ||
        !memory_read_u32((uintptr_t)thing + THING_NODE_ROT, &table) || table == 0u) {
        return 0;
    }
    return (uintptr_t)table + (uintptr_t)node * NODE_ROT_STRIDE;
}

static bool read_playhead(uint32_t body, uint32_t slot_offset, uint32_t *out)
{
    uintptr_t track  = track_address(body, slot_offset, NULL, NULL);
    float     frames = 0.0f;
    int32_t   ticks;

    if (track == 0 || !memory_try_read(track + TRACK_TIME, &frames, sizeof frames)) {
        return false;
    }
    if (frames != frames || frames < 0.0f) {
        return false;   /* a NaN playhead is not a position, and neither is a negative one */
    }
    ticks = (int32_t)(frames * 16.0f + 0.5f);
    *out  = (ticks > 65535) ? 65535u : (uint32_t)ticks;
    return true;
}

static void read_twists(uint32_t body, mp_enemy_record_t *record)
{
    uint32_t nodes = node_count(body);
    uint32_t node;
    uint32_t taken = 0;

    if (nodes == 0u || nodes > 256u) {
        return;
    }
    for (node = 0; node < nodes && taken < MP_ENEMY_MAX_TWISTS; ++node) {
        uintptr_t at = node_rotation_address(body, node);
        float     angles[2];

        if (at == 0 || !memory_try_read(at, angles, sizeof angles)) {
            continue;
        }
        if (angles[0] == 0.0f && angles[1] == 0.0f) {
            continue;
        }
        if (angles[0] != angles[0] || angles[1] != angles[1]) {
            continue;   /* a NaN angle is not a rotation */
        }
        record->twist[taken].node  = (uint8_t)node;
        record->twist[taken].pitch = (uint16_t)((int32_t)(angles[0] * ANGLE_SCALE) & 0xFFFF);
        record->twist[taken].yaw   = (uint16_t)((int32_t)(angles[1] * ANGLE_SCALE) & 0xFFFF);
        ++taken;
    }
    record->value[MP_ENEMY_F_TWISTS] = taken;
}

static void reference_pose_read(uint32_t body, mp_enemy_record_t *record)
{
    uint32_t raw = 0;

    if (record == NULL) {
        return;
    }
    if (memory_read_u32((uintptr_t)body + B_CUR_CLIP, &raw)) {
        record->value[MP_ENEMY_F_CLIP] = raw & 0xFFu;
    }
    if (read_playhead(body, B_BASE_SLOT, &raw)) {
        record->value[MP_ENEMY_F_HEAD] = raw;
        record->value[MP_ENEMY_F_STATE] |= MP_ENEMY_HAS_HEAD;
    }
    if (read_playhead(body, B_OVERLAY_SLOT, &raw)) {
        uint32_t clip = 0;

        record->value[MP_ENEMY_F_OVERLAY_HEAD] = raw;
        record->value[MP_ENEMY_F_STATE] |= MP_ENEMY_HAS_OVERLAY;
        if (memory_read_u32((uintptr_t)body + B_OVERLAY_CLIP, &clip)) {
            record->value[MP_ENEMY_F_OVERLAY_CLIP] = clip & 0xFFu;
        }
    }
    read_twists(body, record);
}

/* ==============================================================================================
 * The body, in two pages, the second of which may not be read.
 * ============================================================================================ */

#define AT_BODY      0x000u
#define AT_THING     0x100u
#define AT_ASSET     0x140u
#define AT_KEYFRAME  0x240u
#define AT_PUPPET    0x280u
#define AT_TABLE     0x800u

static uint8_t *pages;
static size_t   page;

static uint32_t addr(uint32_t offset)
{
    return (uint32_t)(uintptr_t)(pages + offset);
}

static void put32(uint32_t offset, uint32_t value)
{
    memcpy(pages + offset, &value, sizeof value);
}

static void putf(uint32_t offset, float value)
{
    memcpy(pages + offset, &value, sizeof value);
}

static uint32_t track_at(uint32_t slot)
{
    return AT_PUPPET + PUPPET_TRACKS + slot * TRACK_STRIDE;
}

/* A standing actor of `nodes` nodes playing clip 5 on base slot 0 at frame 12.5 of 60, holding
 * its end, no overlay, and every node at rest. */
static void build(uint32_t nodes)
{
    memset(pages, 0, page);
    put32(AT_BODY + B_THING, addr(AT_THING));
    put32(AT_BODY + B_CUR_CLIP, 5u);
    put32(AT_BODY + B_BASE_SLOT, 0u);
    put32(AT_BODY + B_OVERLAY_CLIP, 7u);
    put32(AT_BODY + B_OVERLAY_SLOT, 0xFFFFFFFFu);
    put32(AT_THING + THING_ACTOR, addr(AT_ASSET));
    put32(AT_THING + THING_PUPPET, addr(AT_PUPPET));
    put32(AT_THING + THING_NODE_ROT, addr(AT_TABLE));
    put32(AT_ASSET + ACTOR_NODES, nodes);
    put32(AT_KEYFRAME + KEYFRAME_FRAMES, 60u);
    putf(track_at(0) + TRACK_FPS, 30.0f);
    putf(track_at(0) + TRACK_TIME, 12.5f);
    putf(track_at(0) + TRACK_TIME_PREV, 12.0f);
    put32(track_at(0) + TRACK_KEYFRAME, addr(AT_KEYFRAME));
    put32(track_at(0) + TRACK_MODE_FLAGS, MODE_HOLDEND);
}

static void turn(uint32_t table, uint32_t node, float pitch, float yaw)
{
    putf(table + node * NODE_ROT_STRIDE, pitch);
    putf(table + node * NODE_ROT_STRIDE + 4u, yaw);
}

/* Both reads over the body as it stands: the record and the base track, the same field for field
 * and the same verdict. */
static void same(const char *what)
{
    mp_enemy_record_t     swapped;
    mp_enemy_record_t     reference;
    mp_enemy_pose_track_t track_swapped;
    mp_enemy_pose_track_t track_reference;
    bool                  ok_swapped;
    bool                  ok_reference;
    uint32_t              body = addr(AT_BODY);

    memset(&swapped, 0, sizeof swapped);
    memset(&reference, 0, sizeof reference);
    mp_enemy_pose_read(body, &swapped);
    reference_pose_read(body, &reference);
    ut_checkf(memcmp(&swapped, &reference, sizeof swapped) == 0,
              "%s: the record is the reference's, field for field", what);

    memset(&track_swapped, 0, sizeof track_swapped);
    memset(&track_reference, 0, sizeof track_reference);
    ok_swapped   = mp_enemy_pose_base_track(body, &track_swapped);
    ok_reference = reference_base_track(body, &track_reference);
    ut_checkf(ok_swapped == ok_reference &&
                  memcmp(&track_swapped, &track_reference, sizeof track_swapped) == 0,
              "%s: and so is the base track, %s", what, ok_reference ? "read" : "refused");
}

static uint32_t twists_of(void)
{
    mp_enemy_record_t record;

    memset(&record, 0, sizeof record);
    mp_enemy_pose_read(addr(AT_BODY), &record);
    return record.value[MP_ENEMY_F_TWISTS];
}

static void check_the_nodes(void)
{
    static const uint32_t turned[] = { 0u, 2u, 4u, 6u };
    size_t                i;
    uint32_t              n;

    ut_section("the turned nodes");
    for (i = 0; i < sizeof turned / sizeof turned[0]; ++i) {
        build(21u);
        for (n = 0; n < turned[i]; ++n) {
            turn(AT_TABLE, 3u + 2u * n, 10.0f + (float)n, -20.0f - (float)n);
        }
        ut_checkf(twists_of() == (turned[i] < MP_ENEMY_MAX_TWISTS ? turned[i]
                                                                  : MP_ENEMY_MAX_TWISTS),
                  "21 nodes, %u turned: the record carries %u", (unsigned)turned[i],
                  (unsigned)(turned[i] < MP_ENEMY_MAX_TWISTS ? turned[i] : MP_ENEMY_MAX_TWISTS));
        same(turned[i] == 0u ? "21 nodes, none turned"
             : turned[i] == 2u ? "21 nodes, 2 turned"
             : turned[i] == 4u ? "21 nodes, 4 turned" : "21 nodes, 6 turned");
    }

    build(48u);
    turn(AT_TABLE, 47u, 5.0f, 6.0f);
    turn(AT_TABLE, 30u, 1.0f, 0.0f);
    same("48 nodes, the largest shipped rig, turned near its end");

    build(0x40000015u);
    turn(AT_TABLE, 1u, 5.0f, 6.0f);
    same("a count word with its high bits set");

    build(0u);
    same("no nodes");

    build(257u);
    turn(AT_TABLE, 1u, 5.0f, 6.0f);
    same("257 nodes, one past the bound");

    build(256u);
    turn(AT_TABLE, 1u, 5.0f, 6.0f);
    same("256 nodes, the bound itself");

    build(21u);
    turn(AT_TABLE, 2u, NAN, 6.0f);
    turn(AT_TABLE, 4u, 7.0f, 8.0f);
    same("a NaN angle beside a good one");

    build(21u);
    put32(AT_THING + THING_NODE_ROT, 0u);
    same("no node table");
}

static void check_the_links(void)
{
    ut_section("the links from body to track");
    build(21u);
    put32(AT_BODY + B_THING, 0u);
    same("no thing");

    build(21u);
    put32(AT_THING + THING_PUPPET, 0u);
    same("no puppet");

    build(21u);
    put32(AT_THING + THING_ACTOR, 0u);
    same("no asset");
}

static void check_the_playheads(void)
{
    ut_section("the playheads");
    build(21u);
    put32(AT_BODY + B_BASE_SLOT, 0xFFFFFFFFu);
    same("base slot -1");

    build(21u);
    put32(AT_BODY + B_BASE_SLOT, 4u);
    same("base slot 4, past the last");

    build(21u);
    put32(AT_BODY + B_OVERLAY_SLOT, 2u);
    putf(track_at(2) + TRACK_TIME, 3.25f);
    same("an overlay on slot 2");

    build(21u);
    putf(track_at(0) + TRACK_TIME, -1.0f);
    same("a negative playhead");

    build(21u);
    putf(track_at(0) + TRACK_TIME, NAN);
    same("a NaN playhead");

    build(21u);
    putf(track_at(0) + TRACK_TIME_PREV, 20.0f);
    same("the previous time ahead of the current");

    build(21u);
    put32(track_at(0) + TRACK_KEYFRAME, 0u);
    same("a track with no keyframe");

    build(21u);
    putf(track_at(0) + TRACK_TIME, 5000.0f);
    same("a playhead past what the wire carries");
}

/* A node table that begins fifteen entries before the seam: nodes 15 to 20 lie in the page
 * nobody may read, so each of them is a refusal inside the loop, and the loop goes on. */
static void check_the_seam(void)
{
    uint32_t table = (uint32_t)page - 15u * NODE_ROT_STRIDE;

    ut_section("a node table that runs into a page nobody may read");
    build(21u);
    put32(AT_THING + THING_NODE_ROT, addr(table));
    turn(table, 2u, 1.0f, 2.0f);
    turn(table, 14u, 3.0f, 4.0f);
    same("21 nodes, the last six past the seam");
    ut_check(twists_of() == 2u, "the two turned nodes before the seam still travel");
}

int main(void)
{
    SYSTEM_INFO info;
    DWORD       previous;

    GetSystemInfo(&info);
    page  = info.dwPageSize;
    pages = (uint8_t *)VirtualAlloc(NULL, 2u * page, MEM_RESERVE | MEM_COMMIT, PAGE_READWRITE);
    if (pages == NULL || !VirtualProtect(pages + page, page, PAGE_NOACCESS, &previous)) {
        ut_check(0, "two pages, the second no access (prerequisite)");
        return ut_summary("the census's read of a puppet");
    }

    check_the_nodes();
    check_the_links();
    check_the_playheads();
    check_the_seam();

    (void)VirtualFree(pages, 0, MEM_RELEASE);
    return ut_summary("the census's read of a puppet");
}
