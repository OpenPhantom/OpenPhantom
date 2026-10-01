/* mp_starter_rule.c: which clip an engine starter plays, and whether an actor carries it. See the
 * header. */
#include "mp_starter_rule.h"

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

/* The setter's raw mode, 0x0044B496 to 0x0044B553, in the order it tests: the slot already held
 * is a holster unless both are empty hands, then the sabre asked for, then a weapon out of empty
 * hands, then empty hands asked for, then one weapon for another. */
bool mp_starter_weapon_clip(uint32_t equipped, uint32_t wanted, uint32_t *clip)
{
    uint32_t played;

    if (equipped == wanted) {
        if (equipped == MP_STARTER_SLOT_EMPTY) {
            return false;
        }
        wanted = MP_STARTER_SLOT_EMPTY;
    }
    if (wanted == MP_STARTER_SLOT_SABRE) {
        played = MP_STARTER_CLIP_DRAW_SABRE;
    } else if (equipped == MP_STARTER_SLOT_EMPTY) {
        played = MP_STARTER_CLIP_FROM_NONE;
    } else if (wanted == MP_STARTER_SLOT_EMPTY) {
        played = MP_STARTER_CLIP_HOLSTER;
    } else {
        played = MP_STARTER_CLIP_SWAP;
    }
    if (clip != NULL) {
        *clip = played;
    }
    return true;
}

bool mp_starter_weapon_slot_ok(uint32_t wanted)
{
    return wanted < MP_STARTER_WEAPON_ROWS;
}

bool mp_starter_swing_overlay_clip(uint32_t row_clip, uint32_t *clip)
{
    if (row_clip != MP_STARTER_CLIP_MIDAIR) {
        return false;
    }
    if (clip != NULL) {
        *clip = row_clip;
    }
    return true;
}

/* The engine's own test at 0x004128FA is `jle`, signed and inclusive, so it lets an ordinal equal
 * to the count through and reads the entry behind the table. */
bool mp_starter_clip_fits(uint32_t clip, uint32_t count)
{
    return count <= (uint32_t)INT32_MAX && clip < count;
}
