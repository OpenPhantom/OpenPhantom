/* fmv_player.h: replace the pre-rendered movies' whole playback path, not just their scale.
 *
 * Produces: fmv_player.dll
 */
#ifndef FMV_PLAYER_H
#define FMV_PLAYER_H

/* The only entry point. There is deliberately no counterpart: see dll_main.c for what this feature
 * holds on to for the life of the process and why giving it back from DllMain would be worse than
 * keeping it. */
void fmv_player_install(void);

#endif /* FMV_PLAYER_H */
