/* mp_voice_engine.c: the engine's voice state, read and written through bound cells. See the
 * header. */
#include "mp_voice_engine.h"

#include "common/memory.h"

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <string.h>

/* A sound reference holds its flags and then its name, the key the engine's resolver hashes:
 * `ref + 4`, a name of at most 0x34 bytes. Only the log reads it. */
#define REF_NAME_AT    0x04u
#define REF_NAME_BYTES 0x34u

bool mp_voice_engine_eye(uintptr_t eye_cell, float eye[3])
{
    uint32_t pointer = 0u;

    return eye_cell != 0u && memory_read_u32(eye_cell, &pointer) && pointer != 0u &&
           memory_try_read((uintptr_t)pointer, eye, 3u * sizeof(float));
}

bool mp_voice_engine_will_start(const mp_voice_cells_t *cells, const void *speaker)
{
    uint32_t held    = 0u;
    uint32_t restart = 0u;

    if (!memory_try_read_u32(cells->speaker_cell, &held) ||
        !memory_try_read_u32(cells->restart_cell, &restart)) {
        return true;
    }
    return (uintptr_t)held != (uintptr_t)speaker || restart != 0u;
}

static uintptr_t channel_at(const mp_voice_bank_t *bank, int32_t index)
{
    if (bank == NULL || bank->base == 0u || index < 0 || (size_t)index >= bank->count) {
        return 0u;
    }
    return bank->base + (size_t)index * bank->stride;
}

bool mp_voice_engine_channel(const mp_voice_bank_t *bank, int32_t index, uintptr_t line_cell,
                             mp_voice_channel_t *out)
{
    uintptr_t at       = channel_at(bank, index);
    uint32_t  flags    = 0u;
    uint32_t  priority = 0u;
    uint32_t  owner    = 0u;

    memset(out, 0, sizeof *out);
    if (at == 0u || !memory_try_read(at + bank->flags_at, &flags, sizeof flags) ||
        !memory_try_read(at + bank->priority_at, &priority, sizeof priority) ||
        !memory_try_read(at + bank->owner_at, &owner, sizeof owner)) {
        return false;
    }
    out->busy     = (flags & bank->free_bit) == 0u;
    out->playing  = (flags & bank->playing_bit) != 0u;
    out->priority = priority;
    out->of_line  = line_cell != 0u && (uintptr_t)owner == line_cell;
    return true;
}

/* The bank as it stands; 0 channels when any of them did not read, so that nothing is claimed of a
 * bank that was only half seen. */
static size_t read_the_bank(const mp_voice_bank_t *bank, uintptr_t line_cell,
                            mp_voice_channel_t *out, size_t *free_count)
{
    size_t count = bank->count < MP_VOICE_CHANNELS_MAX ? bank->count : MP_VOICE_CHANNELS_MAX;
    size_t index;

    *free_count = 0u;
    for (index = 0; index < count; ++index) {
        if (!mp_voice_engine_channel(bank, (int32_t)index, line_cell, &out[index])) {
            *free_count = 0u;
            return 0u;
        }
        *free_count += out[index].busy ? 0u : 1u;
    }
    return count;
}

void mp_voice_engine_before(const mp_voice_answer_cells_t *cells, int32_t line,
                            mp_voice_call_t *call)
{
    mp_voice_channel_t channels[MP_VOICE_CHANNELS_MAX];
    int32_t            option = 0;

    call->line         = line;
    call->voices_known = memory_try_read(cells->voices_cell, &option, sizeof option);
    call->voices_on    = option != 0;
    call->latch_known  = memory_try_read(cells->latch_cell, &call->latch_before,
                                         sizeof call->latch_before);
    call->free_known   = read_the_bank(&cells->bank, 0u, channels, &call->free_before) != 0u;
}

void mp_voice_engine_after(const mp_voice_answer_cells_t *cells, uintptr_t line_cell, bool asked,
                           mp_voice_call_t *call)
{
    size_t free_after = 0u;

    call->asked        = asked;
    call->latch_known  = call->latch_known &&
                         memory_try_read(cells->latch_cell, &call->latch_after,
                                         sizeof call->latch_after);
    call->handle_known = memory_try_read(line_cell, &call->handle, sizeof call->handle);
    call->channels     = read_the_bank(&cells->bank, line_cell, call->channel, &free_after);
}

uint32_t mp_voice_engine_let_go(const mp_voice_bank_t *bank, uintptr_t line_cell, int32_t keep)
{
    const uint32_t nobody = 0u;
    uint32_t       let_go = 0u;
    size_t         index;

    if (bank == NULL || line_cell == 0u || keep < 0) {
        return 0u;
    }
    for (index = 0; index < bank->count; ++index) {
        mp_voice_channel_t channel;
        uintptr_t          at = channel_at(bank, (int32_t)index);

        if ((int32_t)index != keep &&
            mp_voice_engine_channel(bank, (int32_t)index, line_cell, &channel) && channel.busy &&
            channel.of_line &&
            memory_try_write(at + bank->owner_at, &nobody, sizeof nobody)) {
            ++let_go;
        }
    }
    return let_go;
}

void mp_voice_engine_wav(const mp_voice_bank_t *bank, int32_t index, char *name, size_t size)
{
    uintptr_t at  = channel_at(bank, index);
    uint32_t  ref = 0u;
    char      read[REF_NAME_BYTES + 1u];
    size_t    length;

    if (name == NULL || size == 0u) {
        return;
    }
    name[0] = '\0';
    memset(read, 0, sizeof read);
    if (at == 0u || !memory_try_read(at + bank->ref_at, &ref, sizeof ref) || ref == 0u ||
        !memory_try_read((uintptr_t)ref + REF_NAME_AT, read, REF_NAME_BYTES)) {
        return;
    }
    length = strnlen(read, REF_NAME_BYTES);
    length = length < size - 1u ? length : size - 1u;
    memcpy(name, read, length);
    name[length] = '\0';
}
