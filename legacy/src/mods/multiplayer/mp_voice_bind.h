/* mp_voice_bind.h: where the judgement of a spoken line reaches the engine, read and checked.
 *
 * Layer 2, the only layer that knows an address. Every number, cell and call the judgement needs is
 * read out of the engine's own code, and each one is named by two places that have to agree, or it
 * is not used: the reach by the two pushes of the voice, the field setter by the voice's two calls
 * and by its own pattern, field 0 by the setter's switch table and its first arm, the subtitle
 * option and the line on show by the render and the update, the channel of a line where it is
 * reset and where it is handed to the voice, the speaker by the speak entry and the restart latch.
 *
 * Three more bindings stand beside the rule's and fail on their own: the hearing radius, the
 * engine's answer to a line, and the priority a presented voice is handed. Each says in its own
 * line whether it bound, and what the rule loses without it.
 */
#ifndef MULTIPLAYER_MP_VOICE_BIND_H
#define MULTIPLAYER_MP_VOICE_BIND_H

#include "mp_voice_rule.h"

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

typedef struct mp_voice_cells {
    float     reach;
    uintptr_t reach_at[2];       /* the range push and the far push */
    uintptr_t set_field;         /* bapsound_setField */
    uintptr_t volume_cell;       /* field 0 of a voice */
    uintptr_t eye_cell;          /* holds a POINTER to the eye */
    uintptr_t option_cell;       /* the subtitle option */
    uintptr_t shown_cell;        /* the line on show */
    uintptr_t bark_cell;         /* the channel handle of a line */
    uintptr_t speaker_cell;      /* who the conversation record says speaks */
    uintptr_t restart_cell;      /* the restart latch */
    uintptr_t module_proc;       /* the dialogue module's handler, for its hull */
    size_t    module_prologue;
} mp_voice_cells_t;

/* NULL with everything read and agreed, else the reason, for the line that says so. Resolves the
 * voice sites first. */
const char *mp_voice_bind_cells(mp_voice_cells_t *out);

typedef struct mp_voice_reply_site {
    uintptr_t call;          /* the E8 that voices the reply */
    uintptr_t voice;         /* what it calls, which the bark's call names as well */
    uint32_t  body_offset;   /* where in a body the reply is voiced */
} mp_voice_reply_site_t;

/* The same for the reply's call. */
const char *mp_voice_bind_reply(mp_voice_reply_site_t *out);

/* The hearing radius: the lock level the voice asks for and the two full volume radii it pushes,
 * read where they stand right behind the voice's own pattern, the two calls there naming the
 * setter the rule bound. */
typedef struct mp_voice_hear_cells {
    uint32_t  min_free_bits;
    uint32_t  min_scene_bits;
    int32_t   lock_level;
    uintptr_t free_at;      /* the push of the radius outside a scene's lock */
    uintptr_t scene_at;     /* the push of the radius under it */
    uintptr_t lock_at;      /* the push of the lock level */
} mp_voice_hear_cells_t;

/* The radii a line is judged by: the pushes above, made radii by the factor the ini gave, which is
 * checked first and is the default when it is no number in the range. Without the pushes the
 * radius is the engine's admission, which is what the rule judged by before the hearing radius
 * came in: the rule stands, only its threshold is lost. NULL when the pushes were read and made
 * radii, else why they were not. */
const char *mp_voice_bind_radii(const mp_voice_cells_t *rule, float factor_raw,
                                mp_voice_hear_cells_t *hear, mp_voice_hearing_t *hearing,
                                bool *factor_as_given);

/* The engine's twelve channels, as the search for a channel and the freeing of one name them. */
typedef struct mp_voice_bank {
    uintptr_t base;
    size_t    count;
    size_t    stride;
    size_t    flags_at;
    uint32_t  free_bit;
    uint32_t  playing_bit;
    size_t    priority_at;
    size_t    owner_at;     /* the handle cell the channel writes -1 into when it is freed */
    size_t    ref_at;       /* the sound reference, whose name the log quotes */
} mp_voice_bank_t;

/* What the engine's answer to a line is read from: the voice option and the latch against the
 * same line, each named twice, and the bank. */
typedef struct mp_voice_answer_cells {
    uintptr_t       voices_cell;
    uintptr_t       latch_cell;
    mp_voice_bank_t bank;
} mp_voice_answer_cells_t;

const char *mp_voice_bind_answer(const mp_voice_cells_t *rule, mp_voice_answer_cells_t *out);

/* Field 5, the priority a voice is handed: the cell the setter's arm stores and playByName's reset
 * writes back, which have to be one, and the resting value that reset writes. */
typedef struct mp_voice_priority_cells {
    uintptr_t cell;
    int32_t   resting;
    uintptr_t named_at[2];  /* the setter's store and playByName's reset */
} mp_voice_priority_cells_t;

const char *mp_voice_bind_priority(const mp_voice_cells_t *rule, mp_voice_priority_cells_t *out);

#endif /* MULTIPLAYER_MP_VOICE_BIND_H */
