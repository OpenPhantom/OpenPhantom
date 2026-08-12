/* mode_filter.h: stop unusable display modes from eating the engine's 64 mode slots.
 *
 * The engine asks DirectDraw to enumerate every display mode and records what comes back into a
 * table of exactly 64 records. Its callback CANCELS the enumeration once that table is full, so
 * the modes it hears about last are never heard about at all.
 *
 * It records every bit depth. A driver that reports 8, 16 and 32 bit therefore spends roughly two
 * thirds of those 64 slots on modes the engine will reject a moment later, because the mode list
 * it builds from that table keeps only entries whose bit depth is exactly 16. What reaches the
 * options screen is whatever resolutions happened to fit in the third that was left, in whatever
 * order the driver enumerated them, which is why one machine shows a full list and the next shows
 * a handful.
 *
 * This filter answers the callback for the modes that cannot survive that later test, without
 * letting them take a slot: anything that is not 16 bit RGB, and any resolution already in the
 * table. Everything else is handed to the engine's own callback untouched. Nothing is added, no
 * mode the engine would have kept is dropped, and the enumeration is not stopped early.
 * ============================================================================================ */
#ifndef MODE_FILTER_H
#define MODE_FILTER_H

#include <stdbool.h>

/* Resolves the enumeration callback and detours it. Returns true only when the filter is really
 * in force. Call BEFORE the graphics device starts, because the enumeration runs once, during
 * startup, and a filter installed afterwards has nothing left to filter. */
bool mode_filter_install(bool enabled);

/* One line saying what the filter did, for the log to carry next to the mode list itself. Safe to
 * call when nothing was installed; it then says nothing. */
void mode_filter_log_summary(void);

#endif /* MODE_FILTER_H */
