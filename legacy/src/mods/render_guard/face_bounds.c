#include "face_bounds.h"

#include <stdbool.h>
#include <stdint.h>

bool face_bounds_vertex_count_refused(uint32_t count, uint32_t max_vertices)
{
    /* Strictly greater, so the limit itself is a face the engine still draws. The array holds 32
     * outcodes and the authored limit is 30, which is what leaves the two vertices of room between
     * the contract every caller was written against and the byte that is the saved return address. */
    return count > max_vertices;
}

bool face_bounds_pool_would_overflow(uint32_t used, uint32_t capacity, uint32_t count)
{
    if (capacity == 0u) {
        return false;                    /* the bound is not in force, so nothing is refused */
    }

    /* A cursor already past the ceiling is refused before anything is computed from it, and that
     * test is also what makes the subtraction below safe: after it, used is at most capacity, so
     * capacity - used cannot borrow. Asking the question the other way round, as
     * used + count > capacity, wraps on a large count and lets exactly the face through that this
     * function exists to stop. */
    if (used > capacity) {
        return true;
    }

    return count > capacity - used;
}
