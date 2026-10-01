/* mp_world_holds.h: three engine cells a session holds on every machine for as long as it runs,
 * and gives back at its one exit.
 *
 * Layer 3. Two cheats change what a client replays of the host's world: happy sends every kind 7
 * bolt half as fast again, and evil force turns a kind 11 bolt into a kind 18, each read at the
 * moment a bolt is spawned, the NPC bolts a client replays for the host included. The engine reads
 * them nowhere else and nothing but its cheat console writes them. So a CLIENT holds both on the
 * host's values, which the host says in its settings note (mp_host_settings); until one has come,
 * and against a dedicated server, which says none, it holds them at 0. The host keeps its own:
 * they are the session's.
 *
 * The third is the 60fps cheat, which moves the substep ladder from 1/32 to 1/64 for every frame
 * it is on, and a session counts in substeps of 1/32. EVERY machine holds it at 0, the host too,
 * and nothing about it depends on a note.
 *
 * Held on each frame begin, which runs before the frame's substep length is chosen, so a cell put
 * back in a frame is the one that frame plays with; a cheat typed during a session is put back at
 * the next frame. Held while this machine plays in a running session, the one answer of
 * mp_session_now that the host's settings and the hero carry take too. What each cell held before
 * the hold began is kept and written back when that answer turns to no, at the next frame begin
 * while the transport still stands (the host ended the session, or a client's host said goodbye),
 * and at once when the transport comes down, which is the one place a session ends for good and
 * where no further frame begin would run.
 */
#ifndef MULTIPLAYER_MP_WORLD_HOLDS_H
#define MULTIPLAYER_MP_WORLD_HOLDS_H

#include <stdbool.h>
#include <stdint.h>

/* Every frame begin while a transport stands (multiplayer.c, after the game data's line). */
void mp_world_holds_frame(void);

/* The transport is down: every held cell gets this side's own value back, now. */
void mp_world_holds_withdraw(void);

/* What the holds did over the run, the numbers the report prints. A client's frames of holding the
 * cheats are split by where the value came from: the host's note, or 0 while none had come, which
 * is what a dedicated server's clients hold for good. A host holds no cheat and counts neither. */
typedef struct mp_world_holds_counts {
    uint32_t happy_held_at;               /* the last value each cheat was held on */
    uint32_t evil_force_held_at;
    uint32_t frames_at_host_value;
    uint32_t frames_without_host_value;
    uint32_t cheat_writes;                /* writes that changed a cheat cell */
    uint32_t fast_frames;                 /* frames 60fps was held at 0, on any machine */
    uint32_t fast_writes;
    uint32_t exits;
    bool     last_exit_whole;             /* the last exit put every held cell back */
    uint32_t refusals;                    /* reads and writes a cell would not take */
} mp_world_holds_counts_t;

void mp_world_holds_counts(mp_world_holds_counts_t *out);

void mp_world_holds_report(void);

#endif /* MULTIPLAYER_MP_WORLD_HOLDS_H */
