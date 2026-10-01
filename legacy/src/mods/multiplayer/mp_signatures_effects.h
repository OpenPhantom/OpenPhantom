/* mp_signatures_effects.h: the engine functions two effects of a host's NPCs are replayed through
 * on a client, and the calls that prove them.
 *
 * Layer 2. A blast at an actor's node and the arcs of a player zap are cosmetic functions of the
 * engine that a client calls itself when the host says so. Each is found by its own pattern and by
 * the call that names it, and the two answers have to agree.
 */
#ifndef MULTIPLAYER_MP_SIGNATURES_EFFECTS_H
#define MULTIPLAYER_MP_SIGNATURES_EFFECTS_H

#include <stddef.h>
#include <stdint.h>

typedef enum mp_effects_site {
    MP_EFFECTS_SITE_EXPLODE_AT,        /* 0x004512CE  Plr_ExplodeAt, hulled on both sides */
    MP_EFFECTS_SITE_DIRECTOR_BLAST,    /* 0x00429C25  the director's command 19, its only caller */
    MP_EFFECTS_SITE_FXZAPPO_CREATE,    /* 0x0043CD39  fxzappo_create, called and never hulled */
    MP_EFFECTS_SITE_PLAYER_ZAP_ARCS,   /* 0x00455F39  the player zap's two calls of it */
    MP_EFFECTS_SITE_COUNT
} mp_effects_site_t;

/* Where the calls lie inside their sites: the director's call of the blast, and the player zap's
 * first and second arc. */
#define MP_EFFECTS_DIRECTOR_BLAST_CALL 0x08u
#define MP_EFFECTS_ZAP_FIRST_CALL      0x1Fu
#define MP_EFFECTS_ZAP_SECOND_CALL     0x46u

/* The site's address, the table resolved on the first ask and said in one line. */
uintptr_t mp_signatures_effects_address(mp_effects_site_t site);
size_t    mp_signatures_effects_prologue(mp_effects_site_t site);

/* The function the call at `offset` inside `site` names, when it is the one `named` resolved to,
 * and 0 when either did not resolve or the two disagree. */
uintptr_t mp_signatures_effects_callee(mp_effects_site_t site, size_t offset,
                                       mp_effects_site_t named);

#endif /* MULTIPLAYER_MP_SIGNATURES_EFFECTS_H */
