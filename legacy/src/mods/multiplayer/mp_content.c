/* mp_content.c: the character roster as one number. See the header.
 */
#include "mp_content.h"

#include "common/character_profile.h"
#include "common/logging.h"

#include <stddef.h>
#include <stdint.h>

uint32_t mp_content_roster_fingerprint(void)
{
    uint32_t hash = 2166136261u;
    uint32_t count;
    uint32_t i;

    /* Asked for rather than assumed. The roster is loaded once and the call is idempotent, so
     * this costs a flag after the first time; leaving it out would have made the fingerprint
     * answer zero on every build where nothing else happened to want the roster yet, and a guard
     * that is installed and always answers zero reads exactly like a guard that works. */
    (void)character_profile_load();
    count = character_profile_count();

    if (count == 0u) {
        return 0u;   /* no roster beside the game: nothing to compare */
    }
    for (i = 0; i < count; ++i) {
        const character_profile_t *profile = character_profile_at(i);
        size_t                     c;
        int                        role;

        if (profile == NULL) {
            continue;
        }
        /* The name and the ordinals, in roster order. The order is hashed with them because the
         * index into this roster is used as an identity by the panels that switch a character. */
        for (c = 0; profile->asset[c] != '\0' && c < sizeof profile->asset; ++c) {
            hash ^= (unsigned char)profile->asset[c];
            hash *= 16777619u;
        }
        for (role = 0; role < CHARACTER_ROLE_COUNT; ++role) {
            uint32_t clip = (uint32_t)profile->clip[role];

            hash ^= clip & 0xffu;
            hash *= 16777619u;
            hash ^= (clip >> 8) & 0xffu;
            hash *= 16777619u;
        }
    }
    return hash != 0u ? hash : 1u;
}

void mp_content_report(void)
{
    log_info("  the character roster is %08X over %u profile(s)",
             (unsigned)mp_content_roster_fingerprint(), (unsigned)character_profile_count());
}
