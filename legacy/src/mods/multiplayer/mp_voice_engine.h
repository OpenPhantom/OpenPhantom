/* mp_voice_engine.h: the engine's voice state, read and written through the cells mp_voice_bind
 * read out of the engine's own code.
 *
 * Layer 2. It knows no address: every cell comes in from the binding. What it knows is the shape
 * of what those cells hold, the eye behind a pointer and the engine's bank of channels, and the
 * one write this feature makes into that bank, the owner's handle of an older voice let go.
 *
 * The judgement in mp_voice.c asks here before and after it hands a line to the engine, and the
 * pure reading of what came back is mp_voice_heard_of in the rule.
 */
#ifndef MULTIPLAYER_MP_VOICE_ENGINE_H
#define MULTIPLAYER_MP_VOICE_ENGINE_H

#include "mp_voice_bind.h"
#include "mp_voice_rule.h"

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

/* The eye the engine admits a placed voice by. The cell holds a POINTER to it: two reads. */
bool mp_voice_engine_eye(uintptr_t eye_cell, float eye[3]);

/* The engine's own edge of a line: a new speaker, or the restart latch armed. An unreadable cell
 * answers "starts", which only costs a place slot. */
bool mp_voice_engine_will_start(const mp_voice_cells_t *cells, const void *speaker);

/* One channel as it stands; `of_line` marks one whose owner's handle is `line_cell`. */
bool mp_voice_engine_channel(const mp_voice_bank_t *bank, int32_t index, uintptr_t line_cell,
                             mp_voice_channel_t *out);

/* Before a line is handed over: the voice option, the latch and how many channels are free. */
void mp_voice_engine_before(const mp_voice_answer_cells_t *cells, int32_t line,
                            mp_voice_call_t *call);

/* After the call: the latch, the line's handle and the bank as the engine left it. */
void mp_voice_engine_after(const mp_voice_answer_cells_t *cells, uintptr_t line_cell, bool asked,
                           mp_voice_call_t *call);

/* Every voice still owning `line_cell` other than channel `keep`, the one the handle names after
 * a call, lets go of it: its owner's handle is set to nought, so it plays to its end and cannot
 * write -1 into the handle of the line on `keep`. Nothing with `keep` negative: a handle that names
 * no channel has nothing to protect, and the voice that owns it is the one the engine left there.
 * Answers how many were let go. */
uint32_t mp_voice_engine_let_go(const mp_voice_bank_t *bank, uintptr_t line_cell, int32_t keep);

/* The name of the wav a channel plays, for a line of the log; "" when it does not read. */
void mp_voice_engine_wav(const mp_voice_bank_t *bank, int32_t index, char *name, size_t size);

#endif /* MULTIPLAYER_MP_VOICE_ENGINE_H */
