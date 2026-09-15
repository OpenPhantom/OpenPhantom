/* stand_in_watch.c: see stand_in_watch.h. */
#include "stand_in_watch.h"
#include "speaker_rest.h"

#include "common/frame_hook.h"
#include "common/logging.h"
#include "common/memory.h"

#include <windows.h>

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <string.h>

#define PENDING_FRAME_LIMIT   30        /* half a second: a face that stops ticking */
#define WATCH_LIMIT_MS        60000u    /* after the face's last line */
#define VOICED_LIMIT          4
#define KEY_SIZE              9

static struct {
    stand_in_play_clip_fn_t   play_clip;
    const volatile uint32_t  *speaker_lock;
    uint32_t                  nudges;

    /* the face, 0 when none */
    uintptr_t record;
    uint32_t  body;
    uint32_t  anchor;          /* the anchor's body, what the speaker cell holds on its line */
    DWORD     last_line_ms;
    int32_t   last_latch;      /* the clip its script last asked for, as last seen here */
    int32_t   stand_clip;      /* the script's own stand, -1 until seen */
    int32_t   voiced[VOICED_LIMIT];
    int       voiced_count;
    bool      parked;          /* its body on the stand by this, not by its script */
    int32_t   parked_clip;
    bool      cut_armed;       /* the gesture of the last nudge ends with the voice */
    int32_t   cut_clip;        /* that gesture, -1 until the script has put it on */
    char      key[KEY_SIZE];
    float     distance;

    /* a gesture's restart waiting on the face's script */
    bool      pending;
    int32_t   pending_mode;    /* the script's mode at the line */
    int32_t   pending_ticks;   /* and its tick count in that mode */
    int       pending_frames;  /* frames waited so far */
} watch;

uintptr_t stand_in_base_track(uint32_t body)
{
    uint32_t thing = 0;
    uint32_t puppet = 0;
    int32_t  slot = -1;

    if (!memory_try_read((uintptr_t)body + BODY_THING_OFFSET, &thing, sizeof thing) ||
        thing == 0 ||
        !memory_try_read((uintptr_t)thing + THING_PUPPET_OFFSET, &puppet, sizeof puppet) ||
        puppet == 0 ||
        !memory_try_read((uintptr_t)body + BODY_BASE_SLOT_OFFSET, &slot, sizeof slot) ||
        slot < 0 || slot >= PUPPET_TRACK_LIMIT) {
        return 0;
    }
    return (uintptr_t)puppet + PUPPET_TRACKS_OFFSET + (uint32_t)slot * PUPPET_TRACK_STRIDE;
}

/* The clip put on the base layer through the crossfade the script interpreter uses, and its
 * track's mode word, the clip's own flags, given `set` and stripped of `clear`. The play-once
 * bit is what the interpreter adds after the same call for an Animation opcode in its mode 0,
 * and what the puppet tests before the clip's own loop flag. */
static void put_clip(uint32_t body, int32_t clip, uint32_t set, uint32_t clear)
{
    uintptr_t track;
    uint32_t  mode = 0;

    watch.play_clip(body, clip, PLAY_CLIP_CROSSFADE);
    track = stand_in_base_track(body);
    if (track != 0 && memory_try_read(track + TRACK_MODE_OFFSET, &mode, sizeof mode)) {
        *(volatile uint32_t *)(track + TRACK_MODE_OFFSET) = (mode | set) & ~clear;
    }
}

static bool anchor_speaking(void)
{
    return watch.anchor != 0 && *watch.speaker_lock == watch.anchor;
}

static bool seen_voiced(int32_t clip)
{
    int i;

    for (i = 0; i < watch.voiced_count; ++i) {
        if (watch.voiced[i] == clip) {
            return true;
        }
    }
    return false;
}

/* The body brought in on the voice: see the header. */
static void nudge(void)
{
    uint32_t  body = watch.body;
    uintptr_t track = stand_in_base_track(body);
    int32_t   clip = -1;
    int32_t   latch = -1;

    if (track == 0 ||
        !memory_try_read((uintptr_t)body + BODY_BASE_CLIP_OFFSET, &clip, sizeof clip) ||
        !memory_try_read(watch.record + CHARACTER_CLIP_LATCH_OFFSET, &latch, sizeof latch)) {
        return;
    }
    ++watch.nudges;
    watch.cut_armed = true;
    watch.cut_clip  = -1;
    if (speaker_rest_clip_is_stand(body, clip)) {
        if (latch >= 0 && latch != clip && !speaker_rest_clip_is_stand(body, latch)) {
            watch.parked   = false;
            watch.cut_clip = latch;
            put_clip(body, latch, TRACK_MODE_PLAY_ONCE, 0);
            log_info("line %s is an anchor's: the body standing in for it, %08X, %.1f units "
                     "away, was parked on its stand, clip %d, with its script asking for clip "
                     "%d; the gesture is played with the voice (stand-in %u)", watch.key,
                     (unsigned)body, (double)watch.distance, (int)clip, (int)latch,
                     (unsigned)watch.nudges);
            return;
        }
        *(volatile int32_t *)(track + TRACK_COMPLETE_OFFSET) = 1;
        log_info("line %s is an anchor's: the body standing in for it, %08X, %.1f units away, "
                 "was on its stand, clip %d, in the script's play-once mode; the pass is ended "
                 "so the script moves on with the voice (stand-in %u)", watch.key,
                 (unsigned)body, (double)watch.distance, (int)clip, (unsigned)watch.nudges);
        return;
    }
    watch.cut_clip = clip;
    put_clip(body, clip, TRACK_MODE_PLAY_ONCE, 0);
    log_info("line %s is an anchor's: the body standing in for it, %08X, %.1f units away, was "
             "on clip %d in the script's play-once mode; the pass starts again with the voice "
             "(stand-in %u)", watch.key, (unsigned)body, (double)watch.distance, (int)clip,
             (unsigned)watch.nudges);
}

/* A restart waiting on the face's script: done once the script has ticked twice, or changed
 * mode and ticked once in the new one, unless that new mode asked for a clip of its own. */
static void settle_pending(void)
{
    int32_t mode = 0;
    int32_t ticks = 0;
    int32_t latch = -1;
    bool    script_ticked;

    if (++watch.pending_frames > PENDING_FRAME_LIMIT ||
        !memory_try_read(watch.record + CHARACTER_AI_MODE_OFFSET, &mode, sizeof mode) ||
        !memory_try_read(watch.record + CHARACTER_MODE_TICKS_OFFSET, &ticks, sizeof ticks)) {
        watch.pending = false;
        return;
    }
    script_ticked = (mode == watch.pending_mode) ? ticks >= watch.pending_ticks + 2
                                                 : ticks >= 1;
    if (!script_ticked) {
        return;
    }
    watch.pending = false;
    if (mode != watch.pending_mode &&
        memory_try_read(watch.record + CHARACTER_CLIP_LATCH_OFFSET, &latch, sizeof latch) &&
        !speaker_rest_clip_is_stand(watch.body, latch)) {
        watch.cut_armed = false;
        log_info("line %s is an anchor's, and the script of the body standing in for it, %08X, "
                 "answered the line itself, mode %d to %d and clip %d: left to it", watch.key,
                 (unsigned)watch.body, (int)watch.pending_mode, (int)mode, (int)latch);
        return;
    }
    nudge();
}

/* The clip the face's script asks for, each time it changes: a stand is noted as the script's
 * own; anything else is voiced, and remembered, while the anchor speaks, and an unvoiced
 * repeat of a remembered one parks the body on that stand. */
static void follow_latch(void)
{
    int32_t latch = -1;
    int32_t clip = -1;

    if (!memory_try_read(watch.record + CHARACTER_CLIP_LATCH_OFFSET, &latch, sizeof latch) ||
        latch == watch.last_latch) {
        return;
    }
    watch.last_latch = latch;
    if (latch < 0) {
        return;
    }
    if (speaker_rest_clip_is_stand(watch.body, latch)) {
        watch.stand_clip = latch;
        watch.parked = false;
        return;
    }
    if (anchor_speaking()) {
        if (!seen_voiced(latch) && watch.voiced_count < VOICED_LIMIT) {
            watch.voiced[watch.voiced_count++] = latch;
        }
        if (watch.cut_armed && watch.cut_clip < 0) {
            watch.cut_clip = latch;     /* the gesture the ended stand pass led to */
        }
        return;
    }
    if (!seen_voiced(latch) || watch.stand_clip < 0 ||
        !memory_try_read((uintptr_t)watch.body + BODY_BASE_CLIP_OFFSET, &clip, sizeof clip) ||
        clip != latch) {
        return;
    }
    /* The stand with its own flags less the play-once bit, so it wraps and the script sits
     * on its node until a flag it waits on, or the next line, moves it. */
    put_clip(watch.body, watch.stand_clip, 0, TRACK_MODE_PLAY_ONCE);
    watch.parked      = true;
    watch.parked_clip = watch.stand_clip;
    log_info("the body standing in for %s, %08X, was put on clip %d again by its own script "
             "with nothing being said: its stand, clip %d, is put back under it, wrapping, "
             "until a line", watch.key, (unsigned)watch.body, (int)latch,
             (int)watch.stand_clip);
}

/* The voice has ended: the gesture it brought on, if still running, has its pass ended, so
 * the script moves on to its stand as it would have at the gesture's own end. The call
 * gesture is 1.9 seconds for a word of 0.6, and the double waved on past the voice. */
static void cut_with_voice(void)
{
    uintptr_t track = stand_in_base_track(watch.body);
    int32_t   clip = -1;
    int32_t   complete = 0;

    watch.cut_armed = false;
    if (track == 0 ||
        !memory_try_read((uintptr_t)watch.body + BODY_BASE_CLIP_OFFSET, &clip, sizeof clip) ||
        clip != watch.cut_clip ||
        !memory_try_read(track + TRACK_COMPLETE_OFFSET, &complete, sizeof complete) ||
        complete != 0) {
        return;
    }
    *(volatile int32_t *)(track + TRACK_COMPLETE_OFFSET) = 1;
    log_info("the voice of %s has ended with the body standing in for it, %08X, still on "
             "clip %d: the pass is ended with it, and the script moves on", watch.key,
             (unsigned)watch.body, (int)clip);
}

static void on_frame(void)
{
    int32_t clip = -1;

    if (watch.record == 0) {
        return;
    }
    if (GetTickCount() - watch.last_line_ms > WATCH_LIMIT_MS) {
        stand_in_watch_forget();
        return;
    }
    if (watch.pending) {
        settle_pending();
    }
    if (watch.cut_armed && watch.cut_clip >= 0 && !anchor_speaking()) {
        cut_with_voice();
    }
    if (watch.parked &&
        (!memory_try_read((uintptr_t)watch.body + BODY_BASE_CLIP_OFFSET, &clip, sizeof clip) ||
         clip != watch.parked_clip)) {
        watch.parked = false;       /* something else put a clip on */
    }
    follow_latch();
}

bool stand_in_watch_is_parked(uint32_t body)
{
    return body != 0 && body == watch.body && watch.parked;
}

void stand_in_watch_forget(void)
{
    stand_in_play_clip_fn_t   play_clip = watch.play_clip;
    const volatile uint32_t  *speaker_lock = watch.speaker_lock;
    uint32_t                  nudges = watch.nudges;

    memset(&watch, 0, sizeof watch);
    watch.play_clip    = play_clip;
    watch.speaker_lock = speaker_lock;
    watch.nudges       = nudges;
    watch.stand_clip   = -1;
    watch.last_latch   = -1;
}

void stand_in_watch_line(uintptr_t record, uint32_t body, uint32_t anchor_body,
                         const char *key, float distance)
{
    int32_t clip = -1;
    int32_t mode = 0;
    int32_t ticks = 0;

    if (record != watch.record) {
        stand_in_watch_forget();
        watch.record = record;
        watch.body   = body;
    }
    watch.anchor       = anchor_body;
    watch.last_line_ms = GetTickCount();
    watch.distance     = distance;
    strncpy(watch.key, key, KEY_SIZE - 1);
    watch.key[KEY_SIZE - 1] = '\0';

    if (!memory_try_read((uintptr_t)body + BODY_BASE_CLIP_OFFSET, &clip, sizeof clip) ||
        !memory_try_read(record + CHARACTER_AI_MODE_OFFSET, &mode, sizeof mode) ||
        !memory_try_read(record + CHARACTER_MODE_TICKS_OFFSET, &ticks, sizeof ticks)) {
        return;
    }
    if (speaker_rest_clip_is_stand(body, clip) && !watch.parked) {
        nudge();            /* the script's own stand pass: ended, and it moves on next tick */
        return;
    }
    /* A gesture, or a stand this parked the body on: the script may answer the line itself
     * within two ticks, so the nudge waits for them. */
    watch.pending        = true;
    watch.pending_mode   = mode;
    watch.pending_ticks  = ticks;
    watch.pending_frames = 0;
}

bool stand_in_watch_install(stand_in_play_clip_fn_t play_clip,
                            const volatile uint32_t *speaker_lock)
{
    stand_in_watch_forget();
    watch.play_clip    = play_clip;
    watch.speaker_lock = speaker_lock;
    return frame_hook_add(on_frame);
}
