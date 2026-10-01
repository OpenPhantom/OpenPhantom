/* mp_bridge_world_check.c: the loopback's check of the world codec.
 *
 * In the loopback both ends of the wire are this process, so every world the client end decodes
 * can be held against the world the host end built at the same tick. That is the in-process proof
 * that a body comes out of the codec as it went in, to the wire's own quantisation. It shares
 * nothing with the world's two ends but the host's history, which it reads through
 * mp_bridge_world_built, and it is the seam mp_bridge_world.c had named for itself: it left when
 * that file needed the room for the host's send to be measured.
 */
#include "mp_bridge_world.h"

#include "mp_snapshot.h"
#include "mp_wire.h"

#include "common/logging.h"

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

typedef struct world_check {
    uint32_t checks;
    uint32_t faults;
    bool     fault_logged;
    float    worst_error;
} world_check_t;

static world_check_t check;

void mp_bridge_world_verify(const mp_snapshot_t *decoded)
{
    const mp_snapshot_t *built = mp_bridge_world_built(decoded->tick);
    size_t               slot;
    int                  axis;

    if (built == NULL) {
        return;   /* the host's ring has moved past this tick; nothing to compare against */
    }
    for (slot = 0; slot < MP_SNAPSHOT_MAX_BODIES; ++slot) {
        if (!mp_snapshot_has_body(decoded, slot) || !mp_snapshot_has_body(built, slot)) {
            continue;
        }
        for (axis = 0; axis < 3; ++axis) {
            float error = decoded->body[slot].position[axis] - built->body[slot].position[axis];

            if (error < 0.0f) {
                error = -error;
            }
            ++check.checks;
            if (error > check.worst_error) {
                check.worst_error = error;
            }
            if (error > MP_WIRE_POSITION_ERROR) {
                ++check.faults;
                if (!check.fault_logged) {
                    check.fault_logged = true;
                    log_error("the decoded world differs from the built one at tick %u, slot %u, "
                              "axis %d: %f against %f; later faults are counted, not logged",
                              (unsigned)decoded->tick, (unsigned)slot, axis,
                              decoded->body[slot].position[axis],
                              built->body[slot].position[axis]);
                }
            }
        }
    }
}

void mp_bridge_world_check_counts(uint32_t *checks, uint32_t *faults, float *worst_error)
{
    if (checks != NULL) {
        *checks = check.checks;
    }
    if (faults != NULL) {
        *faults = check.faults;
    }
    if (worst_error != NULL) {
        *worst_error = check.worst_error;
    }
}
