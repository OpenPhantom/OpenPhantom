/* mp_clip_rule.h: where a body's clip stands against the sender's, and which of its events a move
 * of the playhead passes.
 *
 * Layer 1, pure. Two kinds of body follow a sender's clip on this machine: the replica of an enemy
 * the host owns and the puppet of a far player. Both run their own keyframe clock, which the
 * engine's draw advances at this machine's frame rate, and both have to answer the same three
 * questions, so the answers live here once.
 *
 * How far apart two playheads are. A clip that loops puts one side near its end and the other
 * near its start while they are a frame apart, so the distance is taken around the loop, with the
 * length of the clip as this machine has it. A clip that holds or releases at its end does not
 * wrap and is measured straight.
 *
 * Whether a head is past the window. A clip started on this machine runs from its first frame,
 * and the engine then fires every event of the clip as the head passes it, the first frame's
 * included. That is right for a start the sender made a moment ago: the head it reports lies no
 * further on than the wire's own delay. A head further on than the window belongs to a clip that
 * began long before this machine saw it, a body built late or a record that was lost, and it is
 * put there at once, with the events it passes left behind as old news. The window is ten
 * substeps of world time, above the eight a far key's records are at most apart, and it is the
 * window the world events keep as well.
 *
 * Which events a move passes. The engine's dispatcher fires an event of the clip when its frame
 * lies in the half open span from the old head to the new one, the old end included and the new
 * one not; a count of what a move passes has to use the same span or it counts a different set.
 */
#ifndef MULTIPLAYER_MP_CLIP_RULE_H
#define MULTIPLAYER_MP_CLIP_RULE_H

#include <stdbool.h>
#include <stdint.h>

/* The window, in substeps of world time. */
#define MP_CLIP_ENTRY_WINDOW_SUBSTEPS 10u

/* The substep length a caller that could not read one assumes: the engine's own at 32 Hz. */
#define MP_CLIP_DEFAULT_SUBSTEP_SECONDS (1.0f / 32.0f)

/* The signed distance from `local_head` to `wire_head`, in frames, positive when the wire is
 * ahead. Around the loop for a clip that loops and whose length is known, straight otherwise. */
float mp_clip_head_distance(float wire_head, float local_head, bool loops, float num_frames);

/* Whether a head of `frames` on a clip running at `fps` frames a second lies further on than the
 * window, for substeps of `substep_seconds`. A rate or a length that is not a positive number
 * answers no, so a clip nobody can measure starts from its first frame. */
bool mp_clip_beyond_window(float frames, float fps, float substep_seconds);

/* Whether the engine's dispatcher fires an event on frame `event_frame` for a head that moves from
 * `from` to `to`: from <= event_frame < to. */
bool mp_clip_event_in_span(int32_t event_frame, float from, float to);

#endif /* MULTIPLAYER_MP_CLIP_RULE_H */
