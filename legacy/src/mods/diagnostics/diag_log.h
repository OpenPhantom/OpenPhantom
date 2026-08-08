/* diag_log.h: a separate, flood-protected log stream for the observation layer.
 *
 * Why a separate file. The diagnostics write hundreds of lines; mixing them into
 * engine_fixes.log would make the install record unreadable, and one wants to be able to throw
 * the observation away between two attempts. What was INSTALLED still goes to the shared log.
 *
 * FLOOD PROTECTION. The simulation runs at 32 substeps/s and the renderer at up to 240 frames/s.
 * Events that fire per frame (a mover opening under a pressure plate, the player mode, a channel
 * update) are DEBOUNCED at the call site, only the CHANGE is logged. Above that sit two generic
 * brakes here:
 *
 *   * IDENTICAL-LINE COLLAPSE, the same line twice in a row is counted rather than printed, and
 *     handed in at the next change as "(previous line xN)". That loses no information about WHAT
 *     happened, only about how often, and the count comes with it.
 *   * TOKEN BUCKET. MaxLinesPerSecond lines per second; anything above is counted and reported
 *     as "N lines suppressed", so one never falls for a silent hole.
 */
#ifndef DIAG_LOG_H
#define DIAG_LOG_H

#include <stdbool.h>

/* `max_lines_per_second` of 0 disables the token bucket. `also_to_main_log` mirrors every line
 * into engine_fixes.log. Returns false when the file could not be opened. */
bool diag_log_open(int max_lines_per_second, bool also_to_main_log);

void diag_log_write(const char *format, ...);

#endif /* DIAG_LOG_H */
