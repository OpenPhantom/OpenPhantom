#ifndef IMUSE_FIX_MUSIC_VOLUME_H
#define IMUSE_FIX_MUSIC_VOLUME_H

#include <stdbool.h>

/* The music volume across a 3-D provider change.
 *
 * The audio options screen writes MVOL to obi.ini when it CLOSES, from bapMusicGetVolume. Changing
 * the provider inside that screen does this first, at 0x004427AF and the two calls after it:
 *
 *     bapMusicSetVolume(0.0f);      silence the music for the switch
 *     bapMusicDetach();
 *     bapsnd3d_openProvider(pick);
 *     bapMusicAttach();             re-reads MVOL from the file and applies it
 *
 * The slider's new position has not reached the file yet, so the attach puts the file's old value
 * back and that is what the exit write then saves. Drag the slider, change provider, leave: the
 * drag is gone. Change provider, then drag: it sticks. That order dependence is why the report
 * reads as intermittent.
 *
 * If music was already detached the attach is skipped entirely, so the 0.0f from the first line
 * survives and MVOL=0 is written instead.
 *
 * The SFX slider beside it is immune because nothing on that path zeroes or re-reads SVOL, and its
 * exit write takes an integer straight from the getter with no trip through the file. */
bool music_volume_install(void);

/* True once the three sites resolved and the detours took. */
bool music_volume_is_active(void);

#endif /* IMUSE_FIX_MUSIC_VOLUME_H */
