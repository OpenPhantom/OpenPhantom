/* idle_clip.c: see idle_clip.h. */
#include "idle_clip.h"

#include "common/logging.h"
#include "common/memory.h"

#include <math.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <string.h>

/* --- The chain from a body to a clip's entries, every offset read out of retail ---------------- *
 *
 *   body+0x14         bapActor*, the loaded .baf ("track <= pBapObj->pActor->numTracks" is the
 *                     engine's own assert string, at 0x004AA750)
 *   actor+0xC8        numTracks, the clip count bapobj_playClip (0x0041263F) bounds a request by
 *   actor+0xE4        ppClips, one pointer per clip to its descriptor
 *   descriptor+0x0C   var1, 1 on the model's stand clip and 0 on every pass that plays once
 *   descriptor+0x14   track flags, 4 on the stand clip and 0 on the rest; both copied onto the
 *                     puppet track by bapobj_playClip
 *   descriptor+0x3C   rdKeyframe*, the .key block that follows the descriptor inline
 *
 * The keyframe block, checked against nabcit2.baf byte by byte:
 *
 *   +0x00  name[40]        "nc2lmout.key"
 *   +0x28  flags           0x10 on every clip
 *   +0x2C  type            0xFFFF, or 0x7F on the walk and run
 *   +0x30  fps             float
 *   +0x34  numFrames
 *   +0x38  numJoints       16 on this model
 *   +0x3C  nodes           rdKeyframeNode*, 44 bytes each: name[32], node index, entry count,
 *                          entries*
 *   entry, 56 bytes        frame (a float), flags (1 position, 2 rotation, 0 on the last),
 *                          pos[3], rot[3] in degrees 0 to 360, dpos[3], drot[3] per frame
 *
 * The puppet holds the entry whose frame it has passed and adds the per-frame delta times the
 * frames since, so an entry's delta is the next entry's value less its own over the gap, and
 * the last entry, which the clip ends on, carries none. */
#define BODY_ACTOR_OFFSET            0x14u
#define ACTOR_TRACK_COUNT_OFFSET     0xC8u
#define ACTOR_CLIPS_OFFSET           0xE4u
#define CLIP_LOOP_OFFSET             0x0Cu
#define CLIP_TRACK_FLAGS_OFFSET      0x14u
#define CLIP_KEYFRAME_OFFSET         0x3Cu
#define KEYFRAME_FPS_OFFSET          0x30u
#define KEYFRAME_FRAMES_OFFSET       0x34u
#define KEYFRAME_JOINTS_OFFSET       0x38u
#define KEYFRAME_NODES_OFFSET        0x3Cu
#define NODE_NAME_SIZE               32u
#define NODE_RECORD_SIZE             44u
#define NODE_COUNT_OFFSET            0x24u
#define NODE_ENTRIES_OFFSET          0x28u

#define STAND_CLIP_LOOP              1u
#define STAND_CLIP_TRACK_FLAGS       4u

typedef struct key_entry {
    float    frame;
    uint32_t flags;
    float    pos[3];
    float    rot[3];
    float    dpos[3];
    float    drot[3];
} key_entry_t;
_Static_assert(sizeof(key_entry_t) == 56, "a keyframe entry is 56 bytes in the engine");

#define ENTRY_FLAG_ROTATION          2u

/* Eight seconds at the clip's own rate, an entry every eight frames, the last entry a repeat of
 * the first so the pass closes on itself. Every period below divides eight seconds. */
#define IDLE_FPS                     26.0f
#define IDLE_SECONDS                 8.0f
#define IDLE_FRAMES                  208u
#define IDLE_STEP                    8u
#define IDLE_ENTRIES                 (IDLE_FRAMES / IDLE_STEP + 1u)
#define MAX_JOINTS                   16u

#define TWO_PI                       6.28318530718f

/* The motion, per joint: an amplitude in degrees, a period in seconds and a phase per axis, on
 * the model's own node names. Axis 0 is the pitch on every joint the shipped clips move: the
 * upper arm raises through it, the thigh swings through it, the head nods through it; axis 2
 * turns the head. Axis 1 is the roll and is left at zero on the trunk: a first pass rolled the
 * waist and read as a man swaying side to side, and he should stay centred. A joint not named
 * here keeps its position and stays at the rest rotation. */
typedef struct joint_motion {
    const char *node;
    float       amplitude[3];
    float       period[3];
    float       phase[3];
} joint_motion_t;

static const joint_motion_t MOTIONS[] = {
    { "waist",   { 0.8f, 0.0f, 1.0f }, { 4.0f, 8.0f, 8.0f }, { 0.0f, 0.0f, 1.6f } },
    { "chest",   { 1.5f, 0.0f, 0.8f }, { 4.0f, 8.0f, 8.0f }, { 3.1f, 0.0f, 0.0f } },
    { "head",    { 2.0f, 0.0f, 8.0f }, { 4.0f, 8.0f, 8.0f }, { 1.0f, 0.0f, 0.6f } },
    { "ruparm",  { 3.0f, 0.0f, 1.5f }, { 8.0f, 8.0f, 8.0f }, { 0.0f, 0.0f, 0.0f } },
    { "luparm",  { 3.0f, 0.0f, 1.5f }, { 8.0f, 8.0f, 8.0f }, { 3.1f, 0.0f, 3.1f } },
    { "rforarm", { 2.5f, 0.0f, 0.0f }, { 8.0f, 8.0f, 8.0f }, { 0.8f, 0.0f, 0.0f } },
    { "lforarm", { 2.5f, 0.0f, 0.0f }, { 8.0f, 8.0f, 8.0f }, { 3.9f, 0.0f, 0.0f } },
    { "rthigh",  { 1.5f, 0.0f, 0.0f }, { 8.0f, 8.0f, 8.0f }, { 0.0f, 0.0f, 0.0f } },
    { "lthigh",  { 1.5f, 0.0f, 0.0f }, { 8.0f, 8.0f, 8.0f }, { 3.1f, 0.0f, 0.0f } },
    { "rcalf",   { 1.5f, 0.0f, 0.0f }, { 8.0f, 8.0f, 8.0f }, { 3.1f, 0.0f, 0.0f } },
    { "lcalf",   { 1.5f, 0.0f, 0.0f }, { 8.0f, 8.0f, 8.0f }, { 0.0f, 0.0f, 0.0f } }
};
#define MOTION_COUNT (sizeof MOTIONS / sizeof MOTIONS[0])

static key_entry_t idle_entries[MAX_JOINTS][IDLE_ENTRIES];

static const joint_motion_t *motion_for(const char *node)
{
    size_t i;

    for (i = 0; i < MOTION_COUNT; ++i) {
        if (strcmp(MOTIONS[i].node, node) == 0) {
            return &MOTIONS[i];
        }
    }
    return NULL;
}

static float angle_at(const joint_motion_t *motion, unsigned axis, float seconds)
{
    if (motion == NULL || motion->amplitude[axis] == 0.0f) {
        return 0.0f;
    }
    return motion->amplitude[axis] *
           sinf(TWO_PI * seconds / motion->period[axis] + motion->phase[axis]);
}

static float wrap_degrees(float degrees)
{
    while (degrees < 0.0f) {
        degrees += 360.0f;
    }
    while (degrees >= 360.0f) {
        degrees -= 360.0f;
    }
    return degrees;
}

/* Fills one joint's entries: the position it had in the clip being replaced, the authored
 * rotation at each step and the per-frame delta to the next one. The final entry repeats the
 * first, at the last frame, with no delta. */
static void author_joint(key_entry_t *out, const char *node, const float base_pos[3])
{
    const joint_motion_t *motion = motion_for(node);
    float                 raw[IDLE_ENTRIES][3];
    unsigned              k;
    unsigned              axis;

    for (k = 0; k < IDLE_ENTRIES; ++k) {
        float seconds = (float)(k * IDLE_STEP) / IDLE_FPS;

        for (axis = 0; axis < 3; ++axis) {
            raw[k][axis] = angle_at(motion, axis, seconds);
        }
    }
    for (k = 0; k < IDLE_ENTRIES; ++k) {
        key_entry_t *entry = &out[k];
        bool         last = (k + 1 == IDLE_ENTRIES);

        memset(entry, 0, sizeof *entry);
        entry->frame = (float)(k * IDLE_STEP);
        entry->flags = last ? 0u : ENTRY_FLAG_ROTATION;
        memcpy(entry->pos, base_pos, sizeof entry->pos);
        for (axis = 0; axis < 3; ++axis) {
            entry->rot[axis]  = wrap_degrees(raw[k][axis]);
            entry->drot[axis] = last ? 0.0f : (raw[k + 1][axis] - raw[k][axis]) / (float)IDLE_STEP;
        }
    }
}

bool idle_clip_install(uint32_t body, int32_t clip_index)
{
    uint32_t actor = 0;
    int32_t  track_count = 0;
    uint32_t clips = 0;
    uint32_t clip = 0;
    uint32_t keyframe = 0;
    uint32_t joints = 0;
    uint32_t nodes = 0;
    uint32_t first_entries = 0;
    uint32_t j;

    if (clip_index < 0 ||
        !memory_try_read(body + BODY_ACTOR_OFFSET, &actor, sizeof actor) || actor == 0 ||
        !memory_try_read(actor + ACTOR_TRACK_COUNT_OFFSET, &track_count, sizeof track_count) ||
        clip_index >= track_count ||
        !memory_try_read(actor + ACTOR_CLIPS_OFFSET, &clips, sizeof clips) || clips == 0 ||
        !memory_try_read(clips + (uint32_t)clip_index * 4u, &clip, sizeof clip) || clip == 0 ||
        !memory_try_read(clip + CLIP_KEYFRAME_OFFSET, &keyframe, sizeof keyframe) ||
        keyframe == 0 ||
        !memory_try_read(keyframe + KEYFRAME_JOINTS_OFFSET, &joints, sizeof joints) ||
        joints == 0 || joints > MAX_JOINTS ||
        !memory_try_read(keyframe + KEYFRAME_NODES_OFFSET, &nodes, sizeof nodes) || nodes == 0 ||
        !memory_try_read(nodes + NODE_ENTRIES_OFFSET, &first_entries, sizeof first_entries)) {
        log_warning("idle clip: the chain from body %08X to clip %d did not read as a loaded "
                    "model, so the clip is left as shipped", (unsigned)body, clip_index);
        return false;
    }
    if (first_entries == (uint32_t)(uintptr_t)idle_entries[0]) {
        return true;                                    /* this load already carries it */
    }

    for (j = 0; j < joints; ++j) {
        uint32_t node = nodes + j * NODE_RECORD_SIZE;
        char     name[NODE_NAME_SIZE + 1] = {0};
        int32_t  count = 0;
        uint32_t entries = 0;
        float    base_pos[3] = { 0.0f, 0.0f, 0.0f };

        if (!memory_try_read(node, name, NODE_NAME_SIZE) ||
            !memory_try_read(node + NODE_COUNT_OFFSET, &count, sizeof count) ||
            !memory_try_read(node + NODE_ENTRIES_OFFSET, &entries, sizeof entries)) {
            log_warning("idle clip: node %u of clip %d is not readable, so the clip is left as "
                        "shipped", (unsigned)j, clip_index);
            return false;
        }
        if (count > 0 && entries != 0) {
            (void)memory_try_read(entries + offsetof(key_entry_t, pos), base_pos,
                                  sizeof base_pos);
        }
        author_joint(idle_entries[j], name, base_pos);
    }
    for (j = 0; j < joints; ++j) {
        uint32_t node = nodes + j * NODE_RECORD_SIZE;

        *(int32_t *)(uintptr_t)(node + NODE_COUNT_OFFSET)    = (int32_t)IDLE_ENTRIES;
        *(uint32_t *)(uintptr_t)(node + NODE_ENTRIES_OFFSET) = (uint32_t)(uintptr_t)idle_entries[j];
    }
    *(float *)(uintptr_t)(keyframe + KEYFRAME_FPS_OFFSET)       = IDLE_FPS;
    *(uint32_t *)(uintptr_t)(keyframe + KEYFRAME_FRAMES_OFFSET) = IDLE_FRAMES;
    *(uint32_t *)(uintptr_t)(clip + CLIP_LOOP_OFFSET)           = STAND_CLIP_LOOP;
    *(uint32_t *)(uintptr_t)(clip + CLIP_TRACK_FLAGS_OFFSET)    = STAND_CLIP_TRACK_FLAGS;

    log_info("idle clip: clip %d of the model behind body %08X now carries the generated idle, "
             "%u joints, %.0f s at %.0f frames a second, flagged like the model's own stand",
             clip_index, (unsigned)body, (unsigned)joints, (double)IDLE_SECONDS, (double)IDLE_FPS);
    return true;
}
