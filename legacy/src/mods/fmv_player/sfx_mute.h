/* sfx_mute.h: silence the SFX channel for exactly the post-movie curtain's own lifetime.
 *
 * The curtain (video_overlay.c) hides the PICTURE, not the simulation - real frames keep ticking
 * behind it, so a real sound effect tied to the same transient the curtain exists to hide still
 * fires audibly even though nothing is visible. This mutes the engine's own SFX master volume for
 * exactly as long as the curtain is up and restores the player's real setting afterward, rather
 * than touching anything at the OS or mixer level.
 */
#ifndef SFX_MUTE_H
#define SFX_MUTE_H

/* Resolves the sites this needs. Safe to call even when they do not resolve: sfx_mute_begin/end
 * then simply do nothing, and the curtain works exactly as it did before this file existed. */
void sfx_mute_install(void);

/* Sets the SFX master volume to 0, remembering whatever it actually was. Idempotent: a second call
 * before the matching sfx_mute_end is a no-op, so the curtain's own defensive re-arm path (see
 * video_overlay.c) can never overwrite an already-remembered volume with 0. */
void sfx_mute_begin(void);

/* Restores whatever sfx_mute_begin last remembered. A call with nothing remembered is a no-op. */
void sfx_mute_end(void);

#endif /* SFX_MUTE_H */
