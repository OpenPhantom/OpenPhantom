/* diag_core_class.h: which logical processors are efficiency cores, and whether the calling thread
 * is on one right now.
 *
 * The frame line counts the frames that began on an efficiency core. Windows 11 may move a process
 * whose window is behind another onto those cores, where a frame of this single threaded game
 * takes longer, and with several instances on one machine that is the question a slow second
 * raises first. The count has to be taken on the game thread, frame by frame: the processor a
 * thread is on says nothing about any other thread, and a thread that wanders between cores is not
 * described by one sample a second.
 *
 * Windows names the classes itself. Every logical processor carries an efficiency class, lower
 * meaning more efficient; a hybrid part gives its efficiency cores a lower class than its
 * performance cores, and a part without the distinction gives every processor the same one. So an
 * efficiency core here is a processor whose class is below the highest class on the machine, and a
 * machine with one class has none.
 */
#ifndef DIAG_CORE_CLASS_H
#define DIAG_CORE_CLASS_H

#include <stdbool.h>
#include <stdint.h>

/* Processor groups mapped, 64 logical processors each. A 32-bit game on a machine with more
 * processors than this is not a case anyone runs, and an unmapped processor answers "not an
 * efficiency core" rather than a guess. */
#define DIAG_CORE_GROUPS 4u

typedef struct diag_core_entry {
    uint16_t group;
    uint8_t  number;              /* the logical processor within its group */
    uint8_t  efficiency_class;
} diag_core_entry_t;

typedef struct diag_core_map {
    uint64_t efficient[DIAG_CORE_GROUPS];   /* bit n of word g: processor n of group g */
    unsigned efficient_count;
    unsigned processor_count;
} diag_core_map_t;

/* Builds the map from `count` entries of Windows' processor list. Pure. */
void diag_core_map_build(diag_core_map_t *map, const diag_core_entry_t *entries, unsigned count);

/* Whether processor `number` of `group` is an efficiency core in `map`. Pure. */
bool diag_core_map_is_efficient(const diag_core_map_t *map, uint16_t group, uint8_t number);

/* Reads this machine's processor list once and says in the log what it found. False when the list
 * cannot be had, before Windows 10, and then every frame counts as not on an efficiency core. */
bool diag_core_class_init(void);

/* Whether the calling thread is on an efficiency core right now. One call to
 * GetCurrentProcessorNumberEx and a bit test, cheap enough for every frame. */
bool diag_core_class_efficient_now(void);

#endif /* DIAG_CORE_CLASS_H */
