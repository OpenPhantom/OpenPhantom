/* mp_memory_watch.h: what this side's guarded reads and writes cost and refused, in the report.
 *
 * The run time reads of the multiplayer stopped asking the system about every address first; the
 * asking form is left for installation checks. Two things can still go wrong without a word, and
 * this is where they are said. A run time read the swap missed still asks, one system call each,
 * and its caller is named here with how often it was called, how often it was refused, what the
 * questions cost, and how long the run of like pages behind each address was, since that length is
 * what a question costs. And a read or write that now catches its fault instead of being refused
 * beforehand costs an exception each, so every fault is counted here by kind and by caller, with
 * the address it was handed; a range refused before the touch, outside anything the process could
 * own, is counted beside them.
 *
 * The asking side is counted only while the substep is measured, because timing every question
 * costs two clock reads; the faults are counted always, because counting one costs nothing until
 * it happens. Arming runs the guarded read's self-test once: a read that runs off a page it may
 * read into one nobody holds must be refused with exactly one fault.
 */
#ifndef MULTIPLAYER_MP_MEMORY_WATCH_H
#define MULTIPLAYER_MP_MEMORY_WATCH_H

#include <stdbool.h>

/* From the configuration, once: `enabled` is the multiplayer's own switch, and with it off nothing
 * here is armed; `measuring` is the substep stopwatch's switch. */
void mp_memory_watch_arm(bool enabled, bool measuring);

/* Printed at every level end and at the end, then the per level counts start again. */
void mp_memory_watch_report(void);

/* A level has begun: the asking side starts over, so "in this level" means from here, and its
 * table of callers holds the level's own rather than the first thirty two of the menu and the
 * load. The faults are kept to the level's end, where they are reported with the rest. */
void mp_memory_watch_level_begins(void);

#endif /* MULTIPLAYER_MP_MEMORY_WATCH_H */
