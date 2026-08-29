/* diag_write_watch.h: name the instruction that writes a chosen four bytes of the game's memory.
 *
 * Knows nothing about characters. The caller finds an address worth watching and hands it over;
 * this arms a hardware data breakpoint on it and reports every instruction that writes it.
 */
#ifndef DIAG_WRITE_WATCH_H
#define DIAG_WRITE_WATCH_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

/* Prepares the handler and remembers the calling thread as the one to watch on. Called from a
 * frame callback, because that runs on the thread the simulation runs on and no other thread's
 * debug registers are of any use here. Safe to call repeatedly. */
bool diag_write_watch_prepare(void);

/* Arms the watch on four bytes at `address`. `what` is repeated in every report so a log read
 * later says what was being watched. Replaces any previous watch. False, with a log line naming
 * the reason, when the address is not aligned, the handler is not prepared, or the debug registers
 * could not be written. */
bool diag_write_watch_arm(uintptr_t address, const char *what);

/* Stops watching. Called when the watched object may no longer exist, since the address would
 * otherwise still be watched after the memory behind it was reused for something else. */
void diag_write_watch_disarm(void);

bool diag_write_watch_is_armed(void);

/* Writes out whatever the handler recorded since the last call. Called from the frame callback:
 * the handler itself records into a small buffer and does no file work, because it runs inside an
 * exception on the simulation thread. */
void diag_write_watch_report(void);

#endif /* DIAG_WRITE_WATCH_H */
