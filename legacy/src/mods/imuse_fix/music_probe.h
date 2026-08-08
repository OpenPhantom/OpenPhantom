/* music_probe.h: watch the iMUSE heartbeat, and optionally provoke the failure on purpose.
 *
 * Observation and reproduction, kept apart from imuse_fix.c because that file is about the music
 * PAUSE and this one changes nothing about it.
 */
#ifndef IMUSE_FIX_MUSIC_PROBE_H
#define IMUSE_FIX_MUSIC_PROBE_H

#include <stdbool.h>
#include <stdint.h>

/* Reads its own keys from the ini. Returns false when nothing in it is switched on, or when
 * iMUSE.DLL could not be read, in both cases it does nothing at all afterwards. */
bool music_probe_install(void);

/* Once per rendered frame, from the caller's frame hook. Cheap: two loads and a comparison,
 * unless a report is due. */
void music_probe_frame(void);

#endif /* IMUSE_FIX_MUSIC_PROBE_H */
