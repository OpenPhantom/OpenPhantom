/* spawn_banner.c: see spawn_banner.h. */
#include "spawn_banner.h"

#include "common/text.h"

void spawn_banner_placing(char *out, size_t size, const char *label, uint32_t alive, uint32_t cap)
{
    if (out == NULL || size == 0u) {
        return;
    }
    if (label == NULL || label[0] == '\0') {
        text_format(out, size, "Placing: nothing is chosen yet");
        return;
    }
    if (cap == 0u) {
        /* A session whose host has not published a cap. Saying "of 16" here would be this
         * machine's own limit read out as though it were the one that applies. */
        text_format(out, size, "Placing: %s   %u alive", label, alive);
        return;
    }
    text_format(out, size, "Placing: %s   %u of %u alive", label, alive, cap);
}

void spawn_banner_ended(char *out, size_t size, uint32_t placed, uint32_t removed,
                        uint32_t refused)
{
    if (out == NULL || size == 0u) {
        return;
    }
    if (placed == 0u && removed == 0u && refused == 0u) {
        text_format(out, size, "Placement ended");
        return;
    }
    text_format(out, size, "Placement ended: placed %u, removed %u, %u refused", placed, removed,
                refused);
}

const char *spawn_banner_waiting(void)
{
    return "Placement waits: the free camera has the mouse";
}
