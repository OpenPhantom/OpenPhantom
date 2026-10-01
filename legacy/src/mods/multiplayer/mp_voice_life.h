/* mp_voice_life.h: the life of the channel a line was voiced on, and how it ended.
 *
 * Layer 3. A host's script is paced by the channel of the line it spoke, not by the line's text:
 * the conversation waits while the handle names a channel and goes on when the engine writes -1
 * into it. So every line handed over and voiced is timed from the call to the frame its handle
 * goes, silent lines and heard ones apart, which is what says whether "kept alive silent" keeps a
 * channel alive at all.
 *
 * The handle can go for four reasons, and each is told apart in the line that names the end: the
 * voice's own end; the end of an older voice whose owner's handle was the same cell, while the new
 * one still plays; the block closing and letting the handle go; and a newer line said over it.
 * Split from mp_voice.c, which asks here and hands over what it bound.
 */
#ifndef MULTIPLAYER_MP_VOICE_LIFE_H
#define MULTIPLAYER_MP_VOICE_LIFE_H

#include "mp_voice_report.h"

#include <stdbool.h>
#include <stdint.h>

/* The bindings the life reads through: the line's handle and, when read, the bank. Kept by
 * pointer; the judgement owns them. */
void mp_voice_life_bind(const mp_voice_bindings_t *bind);

/* After a call that handed a voice over: a handle that names a channel begins a life. `silent`
 * with `field_consumed` is a silent line the engine reached, counted whether it got a channel. */
void mp_voice_life_begin(int32_t line, bool silent, bool field_consumed, bool named);

/* Before a new line is handed over, while the last one's channel is still what it was. */
void mp_voice_life_overtake(void);

/* Once a frame of the dialogue module: the life ends when the handle goes. */
void mp_voice_life_frame(bool block_active);

/* The session or the world went: a life cut by that is not counted. */
void mp_voice_life_drop(void);

void mp_voice_life_counts(mp_voice_life_counts_t *out);

#endif /* MULTIPLAYER_MP_VOICE_LIFE_H */
