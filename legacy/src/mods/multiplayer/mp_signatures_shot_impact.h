/* mp_signatures_shot_impact.h: the engine's shot impact, where an explosive spawns its sub shot,
 * and the two calls that prove it does.
 *
 * Layer 2. The impact of a rocket spawns a ring and the impact of a thermal detonator spawns a
 * fireball, each with shooter class 0 and so with nothing that says whose it is. The hull on the
 * impact is what ties them to their bolt. It is found by its own head, and the two spawns inside
 * it by their own pattern, and the two have to agree: the spawns lie inside the impact and call
 * the shot constructor the main table resolved.
 */
#ifndef MULTIPLAYER_MP_SIGNATURES_SHOT_IMPACT_H
#define MULTIPLAYER_MP_SIGNATURES_SHOT_IMPACT_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

typedef enum mp_shot_impact_site {
    MP_SHOT_IMPACT_SITE_HEAD,        /* 0x0045391C  shot_impact, hulled */
    MP_SHOT_IMPACT_SITE_SUB_SHOTS,   /* 0x00453ABF  its two spawns of a sub shot, class 0 */
    MP_SHOT_IMPACT_SITE_COUNT
} mp_shot_impact_site_t;

/* Where the two spawns lie inside their site: the ring's and the fireball's. */
#define MP_SHOT_IMPACT_RING_CALL     0x1Fu
#define MP_SHOT_IMPACT_FIREBALL_CALL 0x38u

/* The site's address, the table resolved on the first ask and said in one line. */
uintptr_t mp_signatures_shot_impact_address(mp_shot_impact_site_t site);
size_t    mp_signatures_shot_impact_prologue(mp_shot_impact_site_t site);

/* True when both sub shot spawns lie inside the impact's own body and both call `shot_spawn`,
 * the constructor the main table resolved. False when anything did not resolve. */
bool mp_signatures_shot_impact_proves_sub_shots(uintptr_t shot_spawn);

#endif /* MULTIPLAYER_MP_SIGNATURES_SHOT_IMPACT_H */
