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
    int  trigger;             /* 1 = mover commands, 2 = plus every integrator phase change,
                               *     3 = plus the census of which call site reaches the
                               *     integrator, which patches nothing further. 4 to 6 add the
                               *     path censuses; see diagnostics.c for what each one asks  */
    int  fsm;                 /* 1 = AI mode changes, 2 = plus every executed opcode           */
    int  level;               /* 1 = level loading + the cutscene lock                          */
    int  player;              /* 1 = mode changes of the 14-mode state machine                  */
    int  dialogue;            /* 1 = spoken lines + voice files                                 */
    int  fx;                  /* 1 = emitters, 2 = plus every decal STAMPED, 3 = plus every
                               *     decal DRAW, which is a different question entirely          */
    /* The machine under the game rather than the game itself: frame timing, CPU, page faults and
     * the graphics load, plus the neighbourhood of every hitch at level 2. It is the one observer
     * here that answers "why does the picture stutter" rather than "what is the game doing". */
    int  frame;               /* 1 = one summary a second, 2 = plus a dump around every hitch   */
    int  frame_hitch_percent; /* how far past the median counts as a hitch; 0 = the default     */
    /* Which of the engine's two presentation paths is live. One line, not a stream, and it decides
     * whether a DirectDraw wrapper owns the presentation at all or the frame goes past it into the
     * window through GDI. */
    int  present;             /* 1 = the live path, 2 = plus the addresses it resolved to       */

    /* The engine's own generic ballistic-physics list (blaster bolts, confirmed; whatever else
     * shares it, unconfirmed): a live count every 30 frames, plus a position sample of the first
     * few once the count passes a threshold. See diag_projectiles.c's own header. */
    int  projectiles;         /* 1 = on, no further levels                                      */

    /* The engine's own character pool, walked read only: who is standing near the player, what the
     * engine calls them, and whether they are gaining or losing height. See diag_characters.c. */
    int  characters;          /* 1 = the ones near the player, 2 = every live one               */
    int  characters_radius;   /* world units around the player that level 1 reports             */
    char characters_watch[16];/* a character name: put a hardware write watch on it              */
    int  characters_watch_velocity; /* 1 = watch its velocity Z, 0 = its position Z             */

    int  audio_census_ms;     /* >0: list the occupied sound channels every N ms                */
    int  max_lines_per_second;
    bool also_to_main_log;
} diagnostics_config_t;

void diagnostics_install(void);

/* Read-only, for the subsystem modules. Valid after diagnostics_install(). */
const diagnostics_config_t *diagnostics_config(void);

#endif /* DIAGNOSTICS_H */
