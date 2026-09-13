/* speaker_rest.c: see speaker_rest.h. */
#include "speaker_rest.h"

#include "common/logging.h"
#include "common/memory.h"

#include <windows.h>

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <string.h>

/* The body and its model's clip table, the same layout idle_clip.c reads. */
#define BODY_ACTOR_OFFSET            0x14u
#define BODY_BASE_CLIP_OFFSET        0xE8u
#define ACTOR_TRACK_COUNT_OFFSET     0xC8u
#define ACTOR_CLIPS_OFFSET           0xE4u
#define CLIP_TRACK_FLAGS_OFFSET      0x14u
#define CLIP_KEYFRAME_OFFSET         0x3Cu
#define KEYFRAME_NAME_SIZE           0x24u
#define CLIP_TABLE_LIMIT             256
#define STAND_CLIP                   0       /* the engine's own idle: clip 0 of every model */

/* The puppet track, the engine's own marks of a parked clip. The mode word at +0x13C is copied
 * from the clip and then given bit 0x01 by the scene's Animation opcode in its play-once mode;
 * the puppet tests that bit before the loop bit, and once the clip completes it pins the clock
 * at the last frame. The slot flags at +0 carry the hold bit the update honours instead for the
 * release mode. A parked body is one whose clock sits at the clip's end under either. The
 * complete flag at +0x140 is not used for this: the script polls it every tick and clears it. */
#define TRACK_FLAGS_OFFSET           0x00u
#define TRACK_TIME_OFFSET            0x120u   /* float, the clock in frames */
#define TRACK_KEYFRAME_OFFSET        0x128u
#define TRACK_MODE_OFFSET            0x13Cu
#define KEYFRAME_FRAMES_OFFSET       0x34u
#define TRACK_FLAG_HELD              0x10u
#define TRACK_MODE_HOLD_END          0x01u
#define TRACK_MODE_RELEASE_END       0x02u
#define TRACK_MODE_LOOP              0x04u

#define PLAY_CLIP_CROSSFADE          4
#define REST_GRACE_MS                500u    /* the script's own follow-up, when it has one */
#define WATCH_LIMIT_MS               60000u  /* after their last line */
#define WATCHED_LIMIT                6

static struct {
    speaker_play_clip_fn_t   play_clip;
    struct {
        uint32_t body;          /* 0 when the slot is free */
        DWORD    last_line_ms;
        int32_t  parked_clip;   /* the clip seen parked, -1 when the body was last seen moving */
        DWORD    parked_ms;
    } watched[WATCHED_LIMIT];
    uint32_t rests;
} rest;

static bool clip_is_death(uint32_t body, int32_t index);

void speaker_rest_install(speaker_play_clip_fn_t play_clip)
{
    rest.play_clip = play_clip;
}

void speaker_rest_note_line_end(uint32_t body, uintptr_t track)
{
    int      slot = -1;
    int      i;
    int32_t  clip = -1;
    uint32_t mode = 0;

    if (rest.play_clip == NULL || body == 0) {
        return;
    }
    if (track != 0 &&
        memory_try_read((uintptr_t)body + BODY_BASE_CLIP_OFFSET, &clip, sizeof clip) &&
        clip != STAND_CLIP &&
        memory_try_read(track + TRACK_MODE_OFFSET, &mode, sizeof mode) &&
        (mode & TRACK_MODE_LOOP) != 0 && (mode & TRACK_MODE_HOLD_END) != 0 &&
        !clip_is_death(body, clip)) {
        /* A looping clip in the play-once mode is a talk clip the script froze to end the
         * talking, and the pass running when the voice stops would nod on past it. The engine
         * drops a menu-line speaker to the stand the moment the voice ends; so does this. */
        rest.play_clip(body, STAND_CLIP, PLAY_CLIP_CROSSFADE);
        ++rest.rests;
        log_info("speaker %08X's voice ended on clip %d, a talk clip in the play-once mode: cut "
                 "with the voice, the body goes to its stand, clip 0 (rest %u)", (unsigned)body,
                 (int)clip, (unsigned)rest.rests);
    }
    for (i = 0; i < WATCHED_LIMIT; ++i) {
        if (rest.watched[i].body == body) {
            slot = i;
            break;
        }
        if (slot < 0 && rest.watched[i].body == 0) {
            slot = i;
        }
    }
    if (slot < 0) {
        /* full: the one longest since their line gives way */
        slot = 0;
        for (i = 1; i < WATCHED_LIMIT; ++i) {
            if (rest.watched[i].last_line_ms < rest.watched[slot].last_line_ms) {
                slot = i;
            }
        }
    }
    rest.watched[slot].body         = body;
    rest.watched[slot].last_line_ms = GetTickCount();
    rest.watched[slot].parked_clip  = -1;
}

/* Whether `word` occurs in `name`, either case. */
static bool name_has(const char *name, const char *word)
{
    size_t n = strlen(word);
    size_t i;

    for (i = 0; name[i] != '\0'; ++i) {
        if (_strnicmp(name + i, word, n) == 0) {
            return true;
        }
    }
    return false;
}

/* The name of clip `index` of the model behind `body`, the keyframe block's, and the clip's
 * own flags. False when the chain does not read. */
static bool clip_name_of(uint32_t body, int32_t index, char name[KEYFRAME_NAME_SIZE + 1],
                         uint32_t *flags)
{
    uint32_t actor = 0;
    int32_t  count = 0;
    uint32_t clips = 0;
    uint32_t clip = 0;
    uint32_t keyframe = 0;

    memset(name, 0, KEYFRAME_NAME_SIZE + 1);
    return index >= 0 &&
           memory_try_read((uintptr_t)body + BODY_ACTOR_OFFSET, &actor, sizeof actor) &&
           actor != 0 &&
           memory_try_read((uintptr_t)actor + ACTOR_TRACK_COUNT_OFFSET, &count, sizeof count) &&
           index < count && count <= CLIP_TABLE_LIMIT &&
           memory_try_read((uintptr_t)actor + ACTOR_CLIPS_OFFSET, &clips, sizeof clips) &&
           clips != 0 &&
           memory_try_read((uintptr_t)clips + (uint32_t)index * 4u, &clip, sizeof clip) &&
           clip != 0 &&
           memory_try_read((uintptr_t)clip + CLIP_KEYFRAME_OFFSET, &keyframe, sizeof keyframe) &&
           keyframe != 0 &&
           memory_try_read((uintptr_t)keyframe, name, KEYFRAME_NAME_SIZE) &&
           memory_try_read((uintptr_t)clip + CLIP_TRACK_FLAGS_OFFSET, flags, sizeof *flags);
}

/* A body parked on its death is left there. The names are the models' own: nc2die1, shmdie1,
 * ankdie1, death1 to death3. A scene actor carries no health cell to ask instead. */
static bool clip_is_death(uint32_t body, int32_t index)
{
    char     name[KEYFRAME_NAME_SIZE + 1];
    uint32_t flags = 0;

    return clip_name_of(body, index, name, &flags) &&
           (name_has(name, "die") || name_has(name, "death"));
}

/* Whether the engine has parked the clip on `track`: its clock at the clip's last frame, and
 * either held or clamped there by its mode. A clip that wraps, looped or not, never has its
 * clock at the end for more than the frame it wraps on, and its mode says so anyway. */
static bool track_is_parked(uintptr_t track, uint32_t *flags, uint32_t *mode)
{
    uint32_t keyframe = 0;
    int32_t  frames = 0;
    float    time = 0.0f;

    *flags = 0;
    *mode = 0;
    return track != 0 &&
           memory_try_read(track + TRACK_FLAGS_OFFSET, flags, sizeof *flags) &&
           memory_try_read(track + TRACK_MODE_OFFSET, mode, sizeof *mode) &&
           ((*flags & TRACK_FLAG_HELD) != 0 ||
            (*mode & (TRACK_MODE_HOLD_END | TRACK_MODE_RELEASE_END)) != 0) &&
           memory_try_read(track + TRACK_TIME_OFFSET, &time, sizeof time) &&
           memory_try_read(track + TRACK_KEYFRAME_OFFSET, &keyframe, sizeof keyframe) &&
           keyframe != 0 &&
           memory_try_read((uintptr_t)keyframe + KEYFRAME_FRAMES_OFFSET, &frames, sizeof frames) &&
           frames > 0 && time >= (float)frames;
}

/* One watched body. Returns true when it is done with, whichever way. */
static bool settle(int i, uint32_t speaker, uintptr_t (*body_track)(uint32_t), DWORD now)
{
    uint32_t body = rest.watched[i].body;
    int32_t  clip = -1;
    uintptr_t track = body_track(body);
    uint32_t flags = 0;
    uint32_t mode = 0;

    if (now - rest.watched[i].last_line_ms > WATCH_LIMIT_MS) {
        return true;
    }
    if (body == speaker ||
        !memory_try_read((uintptr_t)body + BODY_BASE_CLIP_OFFSET, &clip, sizeof clip) ||
        !track_is_parked(track, &flags, &mode)) {
        rest.watched[i].parked_clip = -1;       /* talking, or moving */
        return false;
    }
    if (rest.watched[i].parked_clip != clip) {
        rest.watched[i].parked_clip = clip;     /* just parked: the script's half second */
        rest.watched[i].parked_ms   = now;
        return false;
    }
    if (now - rest.watched[i].parked_ms < REST_GRACE_MS) {
        return false;
    }
    if (clip_is_death(body, clip)) {
        return true;
    }
    if (clip == STAND_CLIP && (flags & TRACK_FLAG_HELD) == 0) {
        /* The stand itself, put on by the script in its play-once mode and frozen after one
         * pass. The freeze bit comes off and the stand carries on from where it stopped, as
         * the model authored it; no new clip, no crossfade. The opcode only sets the bit when
         * it starts a clip, so it does not come back. Only the stand: a talk clip is flagged to
         * loop too, and the freeze on it is how the script ends the talking, so a frozen talk
         * clip goes to the stand below like any other gesture. */
        *(volatile uint32_t *)(track + TRACK_MODE_OFFSET) = mode & ~TRACK_MODE_HOLD_END;
        ++rest.rests;
        rest.watched[i].parked_clip = -1;
        log_info("speaker %08X was parked on their stand, clip 0, which the script put on in "
                 "its play-once mode: the freeze comes off and the stand carries on (rest %u)",
                 (unsigned)body, (unsigned)rest.rests);
        return false;
    }
    rest.play_clip(body, STAND_CLIP, PLAY_CLIP_CROSSFADE);
    ++rest.rests;
    rest.watched[i].parked_clip = -1;
    log_info("speaker %08X was parked on clip %d, held on its last frame, and nothing followed "
             "it: the body goes to its stand, clip 0, with the clip's own flags (rest %u)",
             (unsigned)body, (int)clip, (unsigned)rest.rests);
    return false;                               /* watched on, for the next parking */
}

void speaker_rest_on_frame(uint32_t speaker, uintptr_t (*body_track)(uint32_t body))
{
    DWORD now = GetTickCount();
    int   i;

    if (rest.play_clip == NULL) {
        return;
    }
    for (i = 0; i < WATCHED_LIMIT; ++i) {
        if (rest.watched[i].body != 0 && settle(i, speaker, body_track, now)) {
            rest.watched[i].body = 0;
        }
    }
}
