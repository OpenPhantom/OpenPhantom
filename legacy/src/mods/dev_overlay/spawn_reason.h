/* spawn_reason.h: why the entity spawner cannot be used, decided in one place.
 *
 * The panel's group, the placement mode and the line the log prints all answer the same question,
 * and while they answered it separately they drifted apart. The mode refused with "the session
 * runs no copies" whenever ANY multiplayer session was running, and the group's rows were greyed
 * only when the multiplayer really was not running the copies. In a co-op session where it was
 * running them the rows beside the mode were open and the mode was dead, on both machines, and the
 * words it printed named a state that was not the one it had asked about.
 *
 * So the question is a function of facts here and nowhere else. Pure: no engine, no globals, and a
 * test can drive every case of it.
 */
#ifndef DEV_OVERLAY_SPAWN_REASON_H
#define DEV_OVERLAY_SPAWN_REASON_H

#include <stdbool.h>
#include <stdint.h>

/* What the spawner's callers read before they offer anything, gathered once a frame. */
typedef struct spawn_reason_facts {
    bool     session_holds;   /* a session runs and the multiplayer does not run the copies in it */
    bool     session;         /* the multiplayer runs the copies, so the host grants and the
                               * machine that asked does not have to be able to raise one */
    bool     level;           /* the spawn routine resolved, a level is loaded, a player is in it */
    bool     builder;         /* a copy can be raised on this machine */
    bool     camera;          /* the camera cells resolved, so the pointer becomes a place */
    bool     chosen;          /* a kind is picked */
    uint32_t kinds;           /* kinds on offer */
} spawn_reason_facts_t;

/* What is being asked for. PLACE carries everything GROUP needs and more. */
typedef enum spawn_reason_need {
    SPAWN_REASON_GROUP = 0,   /* anything in the group: the list, the behaviour, the remove */
    SPAWN_REASON_PLACE        /* the placement mode, which also needs a kind and the camera */
} spawn_reason_need_t;

/* Why that cannot be had, in one short sentence, or NULL when it can. The sentence is what the
 * panel writes on its own row and what the log prints, so the two cannot say different things. The
 * strings are literals of this file and live as long as the process, which is what lets a caller
 * compare two answers by pointer to see whether anything changed. */
const char *spawn_reason_why(const spawn_reason_facts_t *facts, spawn_reason_need_t need);

/* The four facts SPAWN_REASON_GROUP reads, written into `out` with everything else cleared.
 *
 * Both gatherings in the tree go through it. There were two lists of these four, one in
 * overlay_spawn.c for the chip on the group's band and one in spawn_mode.c for the sentence
 * the row under the keys repeats, so the word `see why` and the `Why:` it points at were two
 * readings of one state. They agreed; what they did not have was any reason to go on agreeing,
 * and the next fact the group's half of the rule grows would have been added to one of them.
 *
 * A caller that also asks SPAWN_REASON_PLACE fills the three the mode adds on top afterwards;
 * this clears them, so a PLACE caller must set them after calling, not before. */
void spawn_reason_group_facts(spawn_reason_facts_t *out, bool session_holds, bool session,
                              bool level, uint32_t kinds);

#endif /* DEV_OVERLAY_SPAWN_REASON_H */
