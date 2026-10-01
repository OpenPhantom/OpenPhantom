/* mp_clip_events.h: a body's own clip events, counted out of its clip table and fired through the
 * engine's dispatcher.
 *
 * Layer 2. A clip carries a table of events, each a frame and a sound, and the engine fires them
 * from its draw: once per advanced track, over the span the advance moved, through one dispatcher
 * that matches the track to its clip by the keyframe it plays. A body that follows a far clock has
 * its head moved by this feature as well, and a move the draw did not make fires nothing by
 * itself. These two calls are what such a move needs: how many events a span holds, read out of the
 * same table the dispatcher reads, and the dispatcher itself for a span the draw skipped.
 *
 *   body+0x14    the actor, whose clip table is at +0xE4 with the clip count at +0xC8
 *   clip+0x3C    the keyframe the clip plays; +0x1C its event count; +0x38 its events, 0x60 apart
 *   event+0x00   the event's frame, an integer
 *   track+0x128  the keyframe the track plays
 */
#ifndef MULTIPLAYER_MP_CLIP_EVENTS_H
#define MULTIPLAYER_MP_CLIP_EVENTS_H

#include <stdbool.h>
#include <stdint.h>

/* Resolves the dispatcher once and says so once. True when it resolved. */
bool mp_clip_events_install(void);

/* Whether a head of `frames` on a track running at `fps` lies past the window at this machine's
 * substep length (mp_clip_beyond_window), which is 1/32 s or 1/64 s under the engine's frame rate
 * cheat. Past the window the events on the way are old news and a move leaves them behind. */
bool mp_clip_events_beyond_window(float frames, float fps);

/* How many events of the clip the track at `track` plays lie in the span from `from` to `to`, as
 * the dispatcher would fire them (mp_clip_event_in_span). 0 for a body, a clip or a table that
 * does not read. */
uint32_t mp_clip_events_between(uint32_t object, uint32_t track, float from, float to);

/* Fires the events of the track in `slot` over the last `frames` its head moved, exactly as the
 * draw fires them after an advance. False when nothing was fired: the dispatcher did not resolve,
 * there is no object, or the span is empty. */
bool mp_clip_events_fire(uint32_t object, uint32_t slot, float frames);

#endif /* MULTIPLAYER_MP_CLIP_EVENTS_H */
