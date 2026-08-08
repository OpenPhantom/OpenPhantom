/* diagnostics.h: the ini-switched observation layer.
 *
 * Produces: diagnostics.dll
 *
 * WHAT THIS IS, and what it explicitly is NOT
 *
 *   It is a tool for finding faults, not a feature. Every hook calls the original, returns its
 *   result unchanged, and touches neither registers nor flags nor any game field. A diagnostic
 *   hook that changes the game is a defect.
 *
 *   If an area is not switched on in the ini, its detour is not installed at all. With
 *   Enabled=0 this DLL touches not one byte of the image; it does not even read .text.
 *
 * The levels: 0 = off, 1 = events, 2 = additionally the fine-grained traffic (channel
 * allocation, opcodes).
 */
#ifndef DIAGNOSTICS_H
#define DIAGNOSTICS_H

#include <stdbool.h>

typedef struct diagnostics_config {
    bool enabled;             /* master switch: 0 => this DLL does NOTHING */
    int  audio;               /* 1 = play/stop/volume/zones, 2 = plus the channel allocation   */
    int  music;               /* 1 = state/sequence/volume                                     */
    int  trigger;             /* 1 = mover commands, 2 = plus every integrator phase change    */
    int  fsm;                 /* 1 = AI mode changes, 2 = plus every executed opcode           */
    int  level;               /* 1 = level loading + the cutscene lock                          */
    int  player;              /* 1 = mode changes of the 14-mode state machine                  */
    int  dialogue;            /* 1 = spoken lines + voice files                                 */
    int  fx;                  /* 1 = emitters, 2 = plus every decal STAMPED, 3 = plus every
                               *     decal DRAW, which is a different question entirely          */
    int  audio_census_ms;     /* >0: list the occupied sound channels every N ms                */
    int  max_lines_per_second;
    bool also_to_main_log;
} diagnostics_config_t;

void diagnostics_install(void);

/* Read-only, for the subsystem modules. Valid after diagnostics_install(). */
const diagnostics_config_t *diagnostics_config(void);

#endif /* DIAGNOSTICS_H */
