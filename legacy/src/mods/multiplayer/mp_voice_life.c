/* mp_voice_life.c: the life of the channel a line was voiced on, and how it ended. See the
 * header. */
#include "mp_voice_life.h"

#include "mp_stopwatch.h"
#include "mp_voice_engine.h"

#include "common/memory.h"

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

typedef struct life_state {
    const mp_voice_bindings_t *bind;
    bool                       on;
    bool                       silent;
    bool                       named;
    int32_t                    line;
    int32_t                    channel;
    uint64_t                   since;
    mp_voice_life_counts_t     count;
} life_state_t;

static life_state_t life;

void mp_voice_life_bind(const mp_voice_bindings_t *bind)
{
    life.bind = bind;
}

static bool handle_of_the_line(int32_t *handle)
{
    return life.bind != NULL &&
           memory_try_read(life.bind->cells.bark_cell, handle, sizeof *handle);
}

static bool channel_of_the_life(mp_voice_channel_t *channel)
{
    return life.bind != NULL && life.bind->answer_read &&
           mp_voice_engine_channel(&life.bind->answer.bank, life.channel,
                                   life.bind->cells.bark_cell, channel);
}

static void end_the_life(mp_voice_end_t end, bool channel_read)
{
    uint32_t lived;

    if (!life.on) {
        return;
    }
    life.on = false;
    lived   = mp_stopwatch_micros(life.since, mp_stopwatch_ticks()) / 1000u;
    if (life.silent) {
        ++life.count.silent_lives;
        life.count.silent_ms += lived;
        if (lived > life.count.silent_longest) {
            life.count.silent_longest = lived;
        }
    } else {
        ++life.count.aloud_lives;
        life.count.aloud_ms += lived;
    }
    life.count.handles_taken += end == MP_VOICE_END_TAKEN ? 1u : 0u;
    if (life.named) {
        mp_voice_report_ending(life.line, lived, end, channel_read);
    }
}

void mp_voice_life_begin(int32_t line, bool silent, bool field_consumed, bool named)
{
    int32_t handle  = -1;
    bool    channel = handle_of_the_line(&handle) && handle >= 0;

    if (silent && field_consumed) {
        ++life.count.silent_played;
        life.count.silent_no_channel += channel ? 0u : 1u;
    }
    if (!channel) {
        return;
    }
    life.on      = true;
    life.silent  = silent;
    life.named   = named;
    life.line    = line;
    life.channel = handle;
    life.since   = mp_stopwatch_ticks();
}

/* The voice of the last line either still plays for a line, and the new one begins over it, or
 * ended on its own since the last frame. */
void mp_voice_life_overtake(void)
{
    mp_voice_channel_t channel;
    bool               read;

    if (!life.on) {
        return;
    }
    read = channel_of_the_life(&channel);
    end_the_life((read && channel.busy && channel.playing && channel.of_line) ? MP_VOICE_END_NEWER
                                                                           : MP_VOICE_END_OWN,
                 read);
}

void mp_voice_life_frame(bool block_active)
{
    mp_voice_channel_t channel;
    int32_t            handle = -1;
    bool               known;
    bool               read;

    if (!life.on) {
        return;
    }
    known = handle_of_the_line(&handle);
    if (known && handle >= 0) {
        return;
    }
    read = channel_of_the_life(&channel);
    end_the_life(mp_voice_end_of(known, handle, read, &channel, block_active), read);
}

void mp_voice_life_drop(void)
{
    life.on = false;
}

void mp_voice_life_counts(mp_voice_life_counts_t *out)
{
    if (out != NULL) {
        *out = life.count;
    }
}
