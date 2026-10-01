/* mp_enemy_blast.c: a blast at an actor's node, from the host's director to a client's engine. See
 * the header.
 */
#include "mp_enemy_blast.h"

#include "mp_enemy_sync.h"
#include "mp_enemy_wire.h"
#include "mp_signatures_effects.h"
#include "mp_world_event.h"
#include "mp_world_event_rule.h"

#include "common/detour.h"
#include "common/logging.h"
#include "common/memory.h"

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <string.h>

/* `void Plr_ExplodeAt(const vec3 *at, f32 pitch)`, cdecl, the pitch taken as its bits so nothing
 * here rounds it; the answer is whatever the engine leaves in eax and is handed back unchanged. */
typedef int32_t(__cdecl *explode_at_fn_t)(const float *at, uint32_t pitch_bits);

typedef struct blast_state {
    detour_t        detour;
    explode_at_fn_t original;
    bool            installed;
    uintptr_t       asking;          /* the actor whose command 19 is running on this host */

    uint32_t heard;                  /* host: command 19 heard while this side describes */
    uint32_t posted;
    uint32_t without_actor;          /* host: a blast no command of the director had asked for */
    uint32_t off_level;              /* host: a point the record's quantisation cannot carry */
    uint32_t withheld;               /* client: a replica's own command 19 */
    uint32_t played;                 /* client: blasts of the host's played here */
    uint32_t without_place;
} blast_state_t;

static blast_state_t blast;

bool mp_enemy_blast_director(uintptr_t actor, bool client_of_a_session)
{
    if (client_of_a_session) {
        if (mp_world_event_output_is_the_hosts(actor, client_of_a_session)) {
            ++blast.withheld;
            return true;
        }
        return false;
    }
    if (mp_enemy_sync_describing()) {
        blast.asking = actor;
        ++blast.heard;
    }
    return false;
}

/* The point as the record carries a position, or refused: an event without it could not be
 * performed anywhere, so it is not posted. */
static void post(uintptr_t actor, const float *at, uint32_t pitch_bits)
{
    float    point[3];
    uint32_t packed = 0;
    uint8_t  tail[sizeof pitch_bits];
    size_t   i;

    if (!memory_try_read((uintptr_t)at, point, sizeof point)) {
        ++blast.off_level;
        return;
    }
    for (i = 0; i < 3u; ++i) {
        if (!mp_enemy_wire_put_position(point[i], &packed)) {
            ++blast.off_level;
            return;
        }
    }
    memcpy(tail, &pitch_bits, sizeof tail);
    if (mp_world_event_post_at_actor_placed(MP_WORLD_EVENT_EXPLODE_AT, actor, 0u, point, 0.0f,
                                            tail, sizeof tail)) {
        ++blast.posted;
    }
}

/* engine: void Plr_ExplodeAt(const vec3 *at, f32 pitch) */
static int32_t __cdecl hook_explode_at(const float *at, uint32_t pitch_bits)
{
    uintptr_t actor = blast.asking;

    blast.asking = 0u;
    if (actor != 0u && at != NULL) {
        post(actor, at, pitch_bits);
    } else if (mp_enemy_sync_describing()) {
        ++blast.without_actor;
    }
    return blast.original(at, pitch_bits);
}

/* On a client, the host's blast at the host's point with the host's pitch, through the trampoline
 * so this side's own hull does not see it. The replica is not read. */
static bool play_blast(const mp_world_event_t *event, uintptr_t replica)
{
    float    at[3];
    uint32_t pitch_bits = 0;
    size_t   i;

    (void)replica;
    if (!event->has_place || event->tail_bytes != sizeof pitch_bits) {
        ++blast.without_place;
        return false;
    }
    for (i = 0; i < 3u; ++i) {
        at[i] = mp_enemy_wire_get_position(event->place[i]);
    }
    memcpy(&pitch_bits, event->tail, sizeof pitch_bits);
    (void)blast.original(at, pitch_bits);
    ++blast.played;
    return true;
}

bool mp_enemy_blast_install(void)
{
    uintptr_t site;

    if (blast.installed) {
        return true;
    }
    site = mp_signatures_effects_address(MP_EFFECTS_SITE_EXPLODE_AT);
    if (site == 0u || mp_signatures_effects_callee(MP_EFFECTS_SITE_DIRECTOR_BLAST,
                                                    MP_EFFECTS_DIRECTOR_BLAST_CALL,
                                                    MP_EFFECTS_SITE_EXPLODE_AT) == 0u) {
        log_warning("a blast at an actor's node stays on the host: Plr_ExplodeAt %s, or the "
                    "director's command 19 does not call it",
                    site != 0u ? "resolved" : "did not resolve");
        return false;
    }
    if (!detour_install(&blast.detour, site, (const void *)&hook_explode_at,
                        mp_signatures_effects_prologue(MP_EFFECTS_SITE_EXPLODE_AT))) {
        log_error("Plr_ExplodeAt at %08X refused the detour", (unsigned)site);
        return false;
    }
    blast.original  = (explode_at_fn_t)blast.detour.original;
    blast.installed = true;
    (void)mp_world_event_set_player(MP_WORLD_EVENT_EXPLODE_AT, &play_blast);
    log_info("a blast at an actor's node travels: Plr_ExplodeAt at %08X is hulled, the one call "
             "of it the director's command 19 makes names it", (unsigned)site);
    return true;
}

void mp_enemy_blast_report(void)
{
    if (!blast.installed) {
        log_info("  a blast at an actor's node: the hull never stood on this side");
        return;
    }
    log_info("  a blast at an actor's node (host): %u heard from the director, %u posted to the "
             "world events, %u without the actor that asked for it, %u at a point off the level "
             "| (client): %u of a replica's own withheld, %u played here, %u without a place",
             (unsigned)blast.heard, (unsigned)blast.posted, (unsigned)blast.without_actor,
             (unsigned)blast.off_level, (unsigned)blast.withheld, (unsigned)blast.played,
             (unsigned)blast.without_place);
}
