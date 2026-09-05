/* sfx_mute.h: silence ONE family of sounds for exactly the post-movie curtain's own lifetime.
 *
 * The curtain (render_curtain.c) hides the PICTURE, not the simulation: real frames keep ticking
 * behind it, so a real sound effect tied to the same transient the curtain exists to hide still
 * fires audibly even though nothing is visible.
 *
 * It is not the SFX master volume, and the name is older than the method. Muting the master was
 * built first and field-testing killed it: the level's own opening line starts inside the same
 * window on the same channel and lost its first three words. What ships instead skips only the
 * calls whose sound name matches the shape the offending transient shares across characters, and
 * lets everything else through. sfx_mute.c carries the capture that found it and the two attempts
 * before this one.
 */
#ifndef SFX_MUTE_H
#define SFX_MUTE_H

/* Resolves the one site this needs, bapsound_play. Safe to call even when it does not resolve:
 * sfx_mute_begin/end then simply do nothing, and the curtain works as it did before this file
 * existed, with the thud audible behind it. */
void sfx_mute_install(void);

/* Arms the suppression. Idempotent: a second call before the matching sfx_mute_end is a no-op,
 * so a repeated raise cannot lose track of whether one is already running. */
void sfx_mute_begin(void);

/* Disarms it, and logs how many calls were skipped. A call with nothing armed is a no-op. */
void sfx_mute_end(void);

#endif /* SFX_MUTE_H */
