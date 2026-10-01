/* shared_note.h: a small record one mod publishes under a name and another one reads.
 *
 * Why a record and not a call. Feature DLLs never depend on each other at run time, and shared
 * code is shared by linking it statically into each of them. So two mods that have something to
 * say to each other compile this same file, neither knows whether the other is loaded, and what
 * passes between them is a fixed size record with a name on it. If the other side is not there, a
 * read simply finds nothing, which is the behaviour a missing DLL should have.
 *
 * The first thing that needed it: a fix that tells a second machine which model the local player
 * is wearing, when that model was swapped by a different DLL. A swapped model is visible in the
 * engine but not nameable from there: the geometry block carries the name of the source mesh
 * rather than of the actor file, and several actors share one mesh name, so a reader that
 * guessed from it would pick the wrong character in the colliding cases. The name has to come
 * from whoever set it. The multiplayer and the developer overlay pass it and several other
 * records this way.
 *
 * One process, not one machine. The name is qualified with the process id, so two copies of the
 * game running side by side on one machine, which is how a two player feature gets tested, do not
 * read each other's notes.
 *
 * Tearing. There is no lock, because a lock between two mods is a dependency in the shape this
 * file exists to avoid. Instead the record carries a serial that is odd exactly while a write is
 * in progress: a reader takes the serial, copies, takes it again, and accepts only when the two
 * agree and are even. A reader that loses the race gets a refusal rather than half of two records,
 * and its next call, a substep later, wins.
 */
#ifndef COMMON_SHARED_NOTE_H
#define COMMON_SHARED_NOTE_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

/* The name a note is filed under: short, printable, and the same literal on both sides. */
#define SHARED_NOTE_NAME_MAX 32u

/* What one note may hold. Big enough for a handful of fixed fields and a couple of asset names,
 * and small enough that a whole note is copied under the serial without the copy being the reason
 * a reader loses the race. */
#define SHARED_NOTE_BYTES 256u

/* Writes `count` bytes under `name`, replacing whatever was there. False when the name or the
 * length is not one this can carry, or when the operating system refused the mapping; in that
 * case nothing was published and a reader goes on seeing the previous note.
 *
 * Publishing the same content again still bumps the serial. Callers that only want to speak on a
 * change compare before calling; this layer does not, because "did it change" is a question about
 * the content and belongs to whoever owns it. */
bool shared_note_publish(const char *name, const void *bytes, size_t count);

/* Copies the note filed under `name` into `bytes`. False when nobody has published it, when the
 * buffer is smaller than what was published, or when a write was in progress in both attempts.
 *
 * `out_count` receives how many bytes the note holds; `out_serial`, when it is not NULL, receives
 * the publication count, which is what a caller compares to tell a repeat from a change. Both are
 * left alone on a false. */
bool shared_note_read(const char *name, void *bytes, size_t capacity, size_t *out_count,
                      uint32_t *out_serial);

/* Drops this process's handle on a note. The note itself lives as long as somebody holds it open,
 * so a publisher that unpublishes while a reader has it open leaves the reader with the last
 * content rather than with a fault. */
void shared_note_forget(const char *name);

/* Whether a name is one this can file: one to SHARED_NOTE_NAME_MAX minus one characters, letters,
 * digits and underscore only. Exposed because it is the whole of the naming rule and a test can
 * drive it without an operating system. */
bool shared_note_name_is_sound(const char *name);

#endif /* COMMON_SHARED_NOTE_H */
