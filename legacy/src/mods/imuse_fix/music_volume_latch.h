/* music_volume_latch.h: the two-deep value rule behind music_volume.c, with no engine in it.
 *
 * The audio screen silences the music on the line before it detaches to switch 3-D provider, and
 * the re-attach reloads MVOL from obi.ini, which the screen has not written yet. So the value to
 * put back after a provider change is the one BEFORE the current one whenever the current one is
 * a zero standing in front of the detach: that zero is the screen's, and a player who chose
 * silence has already pushed their own zero into `previous` on the call before.
 *
 * The three hooks in music_volume.c feed this and read it; nothing here calls anything, so the
 * unit test links this file and drives it the way the screen does. */
#ifndef IMUSE_FIX_MUSIC_VOLUME_LATCH_H
#define IMUSE_FIX_MUSIC_VOLUME_LATCH_H

#include <stdbool.h>

typedef struct music_volume_latch {
    float current;
    float previous;
    bool  seen_any;

    bool  restore_valid;
    float restore_value;
} music_volume_latch_t;

/* One bapMusicSetVolume call, whoever made it. */
void music_volume_latch_set(music_volume_latch_t *latch, float volume);

/* One bapMusicDetach call: arms a restore when the current value is a zero standing in front of
 * the detach, and nothing otherwise. */
void music_volume_latch_detach(music_volume_latch_t *latch);

/* Asked once the original bapMusicAttach has returned and applied the file's value. True, with the
 * value to put back, when a restore was armed; the arming is consumed either way. */
bool music_volume_latch_take_restore(music_volume_latch_t *latch, float *out_value);

/* The value taken above has been applied through the engine's own setter, so both depths hold it:
 * the next detach must not find the file's value behind it. */
void music_volume_latch_restored(music_volume_latch_t *latch, float value);

#endif /* IMUSE_FIX_MUSIC_VOLUME_LATCH_H */
