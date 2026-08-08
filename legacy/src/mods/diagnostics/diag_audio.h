/* diag_audio.h: observers for the sound and music subsystems.
 *
 * THE FUNNEL: all four trigger routes into audio (SoundPlaces, script op 0x603, mover keyframes,
 * actor slots) end in bapsound_play, and its flag word decides FIRST what a "sound" even is,
 * bit 0x8 is a music state, bit 0x400 a music sequence, everything else a voice. That one hook is
 * therefore simultaneously the complete music-trigger trace.
 */
#ifndef DIAG_AUDIO_H
#define DIAG_AUDIO_H

/* Returns how many observers were installed. */
int diag_audio_install(int audio_level, int census_milliseconds);
int diag_music_install(int music_level);

#endif /* DIAG_AUDIO_H */
