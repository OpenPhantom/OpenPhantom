/* music_sites.h: the five engine locations imuse_fix needs, found by shape.
 *
 * Kept apart from the feature for the same reason player_sites.c and camera_sites.c are: the
 * patterns and the byte evidence behind them are a responsibility of their own, and the feature
 * that uses them should read as behaviour rather than as a byte table.
 */
#ifndef IMUSE_FIX_MUSIC_SITES_H
#define IMUSE_FIX_MUSIC_SITES_H

#include <stdbool.h>
#include <stdint.h>

/* Every cell is read out of a matched operand, never written down. In the Edit Tool's recompiled
 * build of this engine all five have moved, the music latch pair from 005BAB90/94 to
 * 005BAB40/44 and the pause latch from 006CCFE0 to 006CCF90, which is the whole argument for
 * doing it this way. */
typedef struct music_sites {
    /* void __cdecl bapMusicPause(void) / bapMusicResume(void). Neither takes an argument and
     * neither caller cleans up any, in all five shipped images. */
    uintptr_t pause_function;
    uintptr_t resume_function;

    volatile int32_t *attached;      /* g_musicAttached: 1 = the music system is up */
    volatile int32_t *paused;        /* g_musicPaused: the latch the two functions above write */
    volatile int32_t *sys_pause_on;  /* sys_pause's re-entrancy latch: 1 while the pause menu owns
                                      * the frame and its own resume broadcast is still pending */

    volatile int32_t *state_latch;   /* the last music STATE cue handed to IMUSE.DLL */
    volatile int32_t *sequence_latch;/* the last music SEQUENCE cue */
} music_sites_t;

/* Resolves all five. Logs one line per site, and one line naming which cross-check failed when a
 * cell is reported differently by two independent patterns. Returns false when anything the
 * feature cannot work without is missing; the latches are optional and are then NULL. */
bool music_sites_resolve(music_sites_t *out);

#endif /* IMUSE_FIX_MUSIC_SITES_H */
