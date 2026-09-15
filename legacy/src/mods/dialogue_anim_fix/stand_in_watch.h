/* stand_in_watch.h: the face brought in on an anchor's line, and what is done to its body.
 *
 * stand_in.c decides which body stands in for a line; this decides when and how it is nudged,
 * and follows it afterwards. Three things, all on the one face at a time:
 *
 *   The nudge. A face on its stand is dealt with at once: if its script is asking for that
 *   stand, the pass is ended by raising the complete flag the script's own Animation node
 *   polls, so it moves on next tick to whatever it plays next; if its script is asking for a
 *   gesture the body is not on, which is a body parked by the third point below, the gesture
 *   is played. A face mid gesture has the gesture started again from its first frame, but
 *   only after its script has ticked twice, or changed mode and ticked once in the new one,
 *   and only if that new mode did not ask for a clip of its own that is not a stand. The wait
 *   is for the scripts that answer a line themselves: the anchor's script and the face's run
 *   in the same substep, the anchor's first, so the transports line's flag is seen and the
 *   talk mode set in the tick the line starts in, and that mode's Animation opcode puts the
 *   talk clip on in the tick after. A restart made inside the opcode, or at the end of that
 *   frame, landed under the talk clip, a gesture fading straight into the talk.
 *
 *   The voiced clips. While the anchor holds the speaker cell, every clip the face's script
 *   asks for is remembered as one that goes with a voice: the call gesture the first line
 *   brings on, the talk clip of the transports line.
 *
 *   The unvoiced repeat. The double's script runs its call gesture and its stance round and
 *   round for as long as the scene lasts, so with the two calls in time with the voice a third
 *   wave came 3.6 seconds after the second with nothing said, and the transports line found him
 *   mid wave. So once a face has been brought in, a clip its script asks for on its own, one
 *   already seen voiced, while the anchor is not speaking, has the script's own stand put back
 *   under it with the clip's own flags, so it wraps and the script sits on its node: the node
 *   is re-evaluated from the top of the mode every tick, so a flag the script waits on is
 *   still seen, and a line arriving finds the body parked and plays the gesture itself. Only
 *   clips seen voiced, only that face, only for a minute after its last line.
 */
#ifndef STAND_IN_WATCH_H
#define STAND_IN_WATCH_H

#include <stdbool.h>
#include <stdint.h>

/* The body and its puppet track, as speaker_gesture.c reads them. */
#define BODY_THING_OFFSET             0x9Cu
#define BODY_BASE_CLIP_OFFSET         0xE8u
#define BODY_BASE_SLOT_OFFSET         0xECu
#define THING_PUPPET_OFFSET           0x18u
#define PUPPET_TRACKS_OFFSET          0x08u
#define PUPPET_TRACK_STRIDE           0x14Cu
#define PUPPET_TRACK_LIMIT            8
#define TRACK_MODE_OFFSET             0x13Cu
#define TRACK_COMPLETE_OFFSET         0x140u
#define TRACK_MODE_PLAY_ONCE          0x01u    /* set by the Animation opcode in its mode 0 */
#define PLAY_CLIP_CROSSFADE           4

/* The character record's script cells. */
#define CHARACTER_AI_MODE_OFFSET      0x7Cu    /* the script's mode */
#define CHARACTER_MODE_TICKS_OFFSET   0x8Cu    /* ticks since the last set-mode, 0 on it */
#define CHARACTER_CLIP_LATCH_OFFSET   0x1C0u   /* the clip the script last asked for */

typedef int32_t (__cdecl *stand_in_play_clip_fn_t)(uint32_t body, int32_t clip, int32_t mode);

/* The track the base clip of `body` is on, or 0 when the chain to it does not read. */
uintptr_t stand_in_base_track(uint32_t body);

/* `speaker_lock` is the conversation's speaker cell, the body of the actor whose line plays.
 * Returns false when the per-frame hook could not be installed. */
bool stand_in_watch_install(stand_in_play_clip_fn_t play_clip,
                            const volatile uint32_t *speaker_lock);

/* `record` and `body` are the face chosen for the line `key`, `anchor_body` the anchor's own
 * body, the one the speaker cell holds while its line plays. */
void stand_in_watch_line(uintptr_t record, uint32_t body, uint32_t anchor_body,
                         const char *key, float distance);

/* Whether `body` is the watched face, parked on its stand by the watch: such a body is a
 * candidate for a line even though its track carries no play-once bit. */
bool stand_in_watch_is_parked(uint32_t body);

/* The level is going away. */
void stand_in_watch_forget(void);

#endif /* STAND_IN_WATCH_H */
