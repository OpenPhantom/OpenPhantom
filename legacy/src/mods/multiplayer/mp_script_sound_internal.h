/* mp_script_sound_internal.h: what the host's half of the scripts' sounds shares with a client's.
 *
 * The engine routines the install resolved, and the reading of a sound call record. Nothing here is
 * public: mp_script_sound.h stays the interface.
 */
#ifndef MULTIPLAYER_MP_SCRIPT_SOUND_INTERNAL_H
#define MULTIPLAYER_MP_SCRIPT_SOUND_INTERNAL_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

/* The engine's "play sound call N at P", its funnel for one record, and the pin. */
typedef void(__cdecl *mp_script_sound_play_call_fn_t)(uint32_t call, int32_t *handle,
                                                      const float *position);
typedef int32_t(__cdecl *mp_script_sound_play_fn_t)(const void *record, int32_t *handle,
                                                    const float *position);
typedef void(__cdecl *mp_script_sound_pin_fn_t)(uint32_t channel, const float *position);

typedef struct mp_script_sound_engine {
    mp_script_sound_play_call_fn_t play_call;   /* read out of the call before it moved */
    mp_script_sound_play_fn_t      play;        /* the funnel, NULL when it did not resolve */
    mp_script_sound_pin_fn_t       pin;         /* NULL when it did not resolve */
    uintptr_t                      bank;        /* the twelve channels, 0 with no pin */
    uintptr_t                      level_cell;  /* the world the records are read out of */
} mp_script_sound_engine_t;

const mp_script_sound_engine_t *mp_script_sound_engine(void);

/* The maximum distance a record is heard at, beside its flag word. */
#define MP_SCRIPT_SOUND_RECORD_MAX_DIST 0x2Cu

/* A sound call record of the level this side runs, whole, into `record` (0x40 bytes). False for
 * an index past the level's table or a read that failed. */
bool mp_script_sound_record(uint32_t call, uint32_t record[16]);

/* The flag word and the reach of a record read above. */
uint32_t mp_script_sound_record_flags(const uint32_t record[16]);
float    mp_script_sound_record_reach(const uint32_t record[16]);

/* A client's players and the director's hand, handed in by the install. */
void mp_script_sound_client_install(void);

/* A client's line of the report. */
void mp_script_sound_client_report(void);

#endif /* MULTIPLAYER_MP_SCRIPT_SOUND_INTERNAL_H */
