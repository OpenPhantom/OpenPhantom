/* fov_poll.c: see fov_poll.h. */
#include "fov_poll.h"

#include <stdbool.h>
#include <stdint.h>

bool fov_poll_due(fov_poll_t *poll, uint32_t now_ms, uint32_t period_ms)
{
    if (poll->looked && (uint32_t)(now_ms - poll->last_ms) < period_ms) {
        return false;
    }
    poll->looked  = true;
    poll->last_ms = now_ms;
    ++poll->looks;
    return true;
}
