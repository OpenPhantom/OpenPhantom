/* render_curtain.h: a black rectangle drawn straight into the game's own rendered frame for a
 * while after a movie, so the position-settle transient video_overlay.c's own header documents has
 * somewhere to finish before the player sees it - and so does anything recording the game.
 *
 * The first version of this was a separate overlay window. It worked for a player watching their
 * own monitor and was invisible to a recording made specifically to show it working: most "game
 * capture" style recorders hook the game's own Present call and grab the raw backbuffer before
 * Windows' compositor ever draws a separate window on top of it, so a window-based curtain reaches
 * the player's eyes but never reaches that captured frame. This one is drawn by the game itself,
 * on the frame it actually presents, so any capture method sees exactly what the player sees.
 */
#ifndef RENDER_CURTAIN_H
#define RENDER_CURTAIN_H

#include <stdbool.h>

/* Resolves the sites this needs and installs the one redirect. Safe to call even when they do not
 * resolve: render_curtain_begin then simply does nothing; a movie plays exactly as it would
 * without this file. */
void render_curtain_install(void);

/* How long the curtain stays fully opaque before it starts fading. 0 disables it outright. */
void render_curtain_set_hold_ms(unsigned milliseconds);

/* How long the curtain then takes to fade from opaque to transparent. 0 skips the fade. */
void render_curtain_set_fade_ms(unsigned milliseconds);

/* Whether sfx_mute.h's own suppression is armed alongside the curtain. See its own header for what
 * it silences and why. */
void render_curtain_set_mute_enabled(bool enabled);

/* Arms the curtain: opaque starting on the next real frame drawn, for HoldMs, then fading for
 * FadeMs, then gone. Idempotent while already armed - a second call before the first finishes does
 * not restart or extend it. */
void render_curtain_begin(void);

#endif /* RENDER_CURTAIN_H */
