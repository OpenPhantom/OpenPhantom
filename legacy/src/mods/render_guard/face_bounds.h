/* face_bounds.h: the two bounds the deferred face path never applies, as arithmetic.
 *
 * Split out of render_guard.c for one reason. These two comparisons are the whole point of that
 * DLL, and each can be wrong in either direction without anything on screen or in the log saying
 * so: too strict and polygons vanish from a scene that was always inside the engine's own limits,
 * too lax and the write this feature exists to prevent has already happened by the time anyone
 * looks. The engine cell, the configuration and the log stay with the caller, so what is left here
 * is arguments in and an answer out, and unittests/face_bounds.c links the functions the game runs
 * rather than a copy of them.
 */
#ifndef FACE_BOUNDS_H
#define FACE_BOUNDS_H

#include <stdbool.h>
#include <stdint.h>

/* True when a face carrying `count` vertices has to be refused, because one clip outcode per vertex
 * would not fit the 32 byte array the submit function keeps on its own stack. That array is at
 * [esp+0x18] in the function at 0x00487D20, so it runs to [esp+0x37]; the frame is `sub esp,0x28`
 * plus four pushes, which puts the saved return address at [esp+0x38]. That byte is outcode[32] and
 * the 33rd vertex of a face writes it, so this is the bound between a refused polygon and a
 * corrupted return.
 *
 * `max_vertices` is the limit in force and comes from the caller's configuration. A count equal to
 * it is accepted and one above it is refused, which is why the authored 30 refuses nothing a correct
 * scene submits.
 *
 * A limit of 0 does not switch this bound off. It refuses every face that carries a vertex at all,
 * and keeping the limit at 1 or above is the caller's job. */
bool face_bounds_vertex_count_refused(uint32_t count, uint32_t max_vertices);

/* True when accepting `count` more vertices would take the shared vertex pool's cursor past the end
 * of the pool. `used` is the cursor at 0x00867380 as the engine currently holds it, read by the
 * caller, and `capacity` is the ceiling in force, counted in vertices of 32 bytes each.
 *
 * A capacity of 0 means the bound is not in force and nothing is refused. That is how the caller
 * switches it off when the cursor could not be located or the configuration asked for it.
 *
 * The sum of the cursor and the count is never formed. On a large count `used + count` wraps, and a
 * wrapped sum is small, so that form answers that a face overrunning the pool by billions of
 * vertices fits comfortably. The question is asked as a subtraction that has been shown not to
 * borrow first. A cursor already past the ceiling refuses everything, including a face with no
 * vertices at all. */
bool face_bounds_pool_would_overflow(uint32_t used, uint32_t capacity, uint32_t count);

#endif /* FACE_BOUNDS_H */
