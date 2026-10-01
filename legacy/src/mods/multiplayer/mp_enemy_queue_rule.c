/* mp_enemy_queue_rule.c: the two newest records of one replica, written one substep apart. See the
 * header. */
#include "mp_enemy_queue_rule.h"

#include "mp_enemy_wire.h"

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

/* Nothing is kept and nothing waited: what a queue is after any way out of holding its key. */
static void empty(mp_enemy_queue_t *queue)
{
    queue->held          = false;
    queue->older_waited  = false;
    queue->mirror_waited = false;
    queue->behind        = 0u;
}

mp_enemy_queue_take_t mp_enemy_queue_take(mp_enemy_queue_t *queue, const mp_enemy_record_t *mirror,
                                          bool unwritten, bool same_life, bool lands,
                                          uint32_t *fell)
{
    bool held;

    *fell = 0u;
    if (queue == NULL || mirror == NULL) {
        return MP_ENEMY_QUEUE_ALONE;
    }
    held = queue->held;

    /* A record for no body here: nothing is held for a body that takes nothing. */
    if (!lands) {
        *fell = held ? 1u : 0u;
        empty(queue);
        return held ? MP_ENEMY_QUEUE_NO_BODY : MP_ENEMY_QUEUE_ALONE;
    }
    /* A new life: the old life's records never reach the body, and the new one is not kept behind
     * anything, so the flush writes it at once. */
    if (!same_life) {
        *fell = (unwritten ? 1u : 0u) + (held ? 1u : 0u);
        empty(queue);
        return *fell != 0u ? MP_ENEMY_QUEUE_NEW_LIFE : MP_ENEMY_QUEUE_ALONE;
    }
    /* The body has the mirror, so nothing stands in front of the new record. A record can only be
     * kept while the mirror behind it is unwritten, so there is nothing kept either. */
    if (!unwritten) {
        queue->held          = false;
        queue->older_waited  = false;
        queue->mirror_waited = false;
        return MP_ENEMY_QUEUE_ALONE;
    }
    /* The mirror goes in front. What it replaces there, if anything, is the oldest of three. */
    *fell                = held ? 1u : 0u;
    queue->older         = *mirror;
    queue->held          = true;
    queue->older_waited  = queue->mirror_waited;
    queue->mirror_waited = false;
    return held ? MP_ENEMY_QUEUE_THIRD : MP_ENEMY_QUEUE_KEPT;
}

const mp_enemy_record_t *mp_enemy_queue_next(const mp_enemy_queue_t *queue,
                                             const mp_enemy_record_t *mirror, bool *late)
{
    bool held = queue != NULL && queue->held;

    if (late != NULL) {
        *late = held ? queue->older_waited : (queue != NULL && queue->mirror_waited);
    }
    return held ? &queue->older : mirror;
}

bool mp_enemy_queue_handled(mp_enemy_queue_t *queue)
{
    bool held;

    if (queue == NULL) {
        return false;
    }
    held                = queue->held;
    queue->held         = false;
    queue->older_waited = false;
    return held;
}

uint32_t mp_enemy_queue_flushed(mp_enemy_queue_t *queue, bool unwritten)
{
    if (queue == NULL) {
        return 0u;
    }
    queue->mirror_waited = unwritten;
    queue->behind        = unwritten ? queue->behind + 1u : 0u;
    return queue->behind;
}

uint32_t mp_enemy_queue_clear(mp_enemy_queue_t *queue)
{
    uint32_t fell;

    if (queue == NULL) {
        return 0u;
    }
    fell = queue->held ? 1u : 0u;
    empty(queue);
    return fell;
}
