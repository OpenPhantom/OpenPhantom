/* mp_body_wear.h: a far body in the model its player wears, asked of the overlay and followed.
 *
 * A far player who wears a borrowed model over their hero is shown here in that model only if the
 * developer overlay puts it on: the overlay owns the rebind of a render handle and the translation
 * of the hero's clips onto the borrowed rig, and the multiplayer owns the body. The two DLLs may
 * not call each other, so they talk through two records (common/model_wear_note.h). This module is
 * the multiplayer's end of them: it says which body should wear which model, reads what came of
 * it, and follows the answer with everything on this side that depends on the rig a body shows.
 *
 * One tick per bank, after the spawn tick of that bank and before its puppet window, in an order
 * that is the design:
 *
 *   1. the answer, before the wish, so an answer never waits a substep behind a wish that moved;
 *   2. the body: its serial, its object, and whether it wears something, asked of the engine;
 *   3. the wish: the far player's model, empty in a deathmatch, with no overlay listening, and for
 *      a model that broke MP_BODY_WEAR_BROKEN_LIMIT of the bank's bodies;
 *   4. the rebuild, and when one is asked for the tick of this bank ends there, because taking the
 *      body down calls back into this module and forgets what the tick was holding;
 *   5. the first sight of a worn body, once per body: the puppet's blade, rotations and effects
 *      start over, since they describe the hero's rig;
 *   6. the wish record, published when it changed;
 *   7. the size, the one place a far body's scale is written.
 *
 * "Worn" is the engine's word and not the answer's: the render handle draws another model than
 * the actor names. An answer can be dropped on the way or be for an older body, and the body wears
 * the model all the same; everything that must not touch a borrowed rig asks this.
 */
#ifndef MULTIPLAYER_MP_BODY_WEAR_H
#define MULTIPLAYER_MP_BODY_WEAR_H

#include "mp_blade_rule.h"

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

/* A model the overlay answers BROKEN for, on this many of a bank's bodies while it stays the wish,
 * is not asked for that bank again until its far player wears another. BROKEN is two allocations
 * refused in one dressing, more often a state of the process than chance, and every round of it
 * is a take down, a spawn and a dressing that fails again. */
#define MP_BODY_WEAR_BROKEN_LIMIT 3u

/* What a bank's body is at one substep, read by the tick's engine half. */
typedef struct mp_body_wear_body {
    uint32_t serial;        /* the body serial the body module counts, 0 = never built */
    uint32_t object;        /* the bapObj, 0 when no body stands */
    uint32_t thing;         /* its render handle, 0 when it has none that reads */
    int32_t  slot;          /* the hero slot it was built on, -1 when none stands */
    bool     worn;          /* the render handle draws another model than the actor names */
    float    actor_scale;   /* the actor's own scale, which the bind sized the object with */
    float    factor;        /* the size the far player asked for, 0 = never asked */
} mp_body_wear_body_t;

/* The tick for bank `bank`: the body read from the body module and the engine, then the steps
 * above. Runs from the spawn tick with no window open; does nothing before the bodies installed. */
void mp_body_wear_tick(size_t bank);

/* The same tick with the body already read and the wall clock at this tick in milliseconds, which
 * the wait before a rebuild is measured on. The engine half above calls it; a test hands it a body
 * and a clock of its own making, which is the whole of the engine this module reads. */
void mp_body_wear_tick_body(size_t bank, const mp_body_wear_body_t *body, uint32_t now_ms);

/* Bank `bank`'s body has been taken down. Its entry in the wish is published again at once, with
 * no object and no model, and what was held about the body is forgotten: after a level end or a
 * session end no tick runs to say so, and the overlay must not keep a translation on a body that
 * went back to the pool. Safe from inside this module's own tick, which ends after a rebuild. */
void mp_body_wear_note_down(size_t bank);

/* A deathmatch asks a model only where the overlay hangs the far player's own weapon on it, and
 * empties the wish again for a body it dressed without one (mp_wear_rule_deathmatch_asks). A
 * wearer with no weapon at the borrowed rig's hand swings at a node nothing measures and strikes
 * nobody on every machine that dressed him, which is a look out of a deathmatch and the game
 * inside one. Only the wish is emptied; which rig the rotations belong to is still the far
 * player's model. Set from one place, where the announced mode is set. */
void mp_body_wear_set_deathmatch(bool deathmatch);

/* Whether bank `bank`'s body wore something at its last tick. False for an index no bank has. */
bool mp_body_wear_worn(size_t bank);

/* Whether that body also carries its player's OWN weapon at the hand of the rig it wears, which
 * the overlay answers and this module reads. It is what the withheld contact, the swept blade and
 * the deathmatch's wish all turn on, and it is false for every body that wears nothing: such a
 * body carries whatever the engine drew for its own actor, and nothing here is withheld from it.
 */
bool mp_body_wear_weapon(size_t bank);

/* The node bank `bank`'s blade hangs off on a body that wears a borrowed model and carries its
 * player's weapon: the engine's own answer for the hero's blade name asked on that body, which
 * the overlay's name hook turns into the hand the weapon is drawn at, whose sphere is the drawn
 * weapon's. 0 for every other body and whenever the lookup did not resolve or answered nothing,
 * so a caller falls back on the node the spawn left in the record and a body in its hero's own
 * model is never asked at all. */
uint32_t mp_body_wear_worn_blade_node(size_t bank, uint32_t object);

/* Whether the node rotations from bank `bank`'s far player fit the rig its body shows. */
bool mp_body_wear_rig_is_senders(size_t bank);

/* The puppet's blade length and blade light for the record the window installed, run as mp_blade
 * answered for it. No far body calls the engine's length tick: its setter writes the four
 * vertices into the mesh of the model the body draws, which is shared by every body of that model,
 * the local player's among them. So for a Jedi body the arithmetic of that tick runs over the
 * block's own three fields, the length and the two light fields, and the far player's blade has a
 * length of its own for whoever draws it; nothing of any mesh is touched. Answers whether a length
 * was stepped.
 *
 * A body in its hero's own model and a worn one are stepped the same way and counted apart,
 * because the report reads the worn bodies' steps in its own line. Withheld whole and counted:
 * a worn body whose hero carries no blade, and a body whose spawn read no blade, no Jedi hero or
 * a rig without sabreblad01 (Panaka's and the Queen's among them). The light tick runs whole only
 * for a Jedi body in its hero's own model, and only as far as its release for every other, so the
 * light is taken away and not placed at a sphere nobody wrote. The engine's light tick is handed
 * in so that a test can stand in for it. */
typedef void(__cdecl *mp_body_wear_engine_fn_t)(void);
bool mp_body_wear_blade_state_run(uintptr_t record, mp_blade_tick_t verdict);
void mp_body_wear_blade_light_run(uintptr_t record, mp_blade_tick_t verdict,
                                  mp_body_wear_engine_fn_t tick);

/* The light tick's release half, with the record the window installed and the engine's light
 * tick handed in, so that a test can stand in for both. The tick gives the light's slot back
 * first and always, and places the light only while the record's equipped slot is the sabre's; so
 * it runs with another slot in that field for the length of the call, and the field is written
 * back after it. A record that does not read runs nothing and answers false; the caller counts
 * the release, since only it knows which kind of body it was for. */
bool mp_body_wear_light_release_only(uintptr_t record, mp_body_wear_engine_fn_t tick);

typedef struct mp_body_wear_counters {
    uint32_t asked;                 /* models asked of the overlay, one per body and model */
    uint32_t worn;                  /* WORN answers taken */
    uint32_t weapon_nodes;          /* worn bodies whose weapon node the engine named */
    uint32_t refused;               /* REFUSED answers taken */
    uint32_t unanswered;            /* bodies asked that went, or still stand, with no answer */
    uint32_t rebuilt;               /* bodies taken down to be built again as their hero */
    uint32_t rebuilds_refused;      /* take downs for a rebuild the body module refused */
    uint32_t blade_ticks_withheld;  /* puppet blade length ticks skipped for a worn body */
    uint32_t blade_steps;           /* worn Jedi bodies' lengths stepped without a mesh */
    uint32_t blade_step_faults;     /* steps whose block would not read or not take the length */
    uint32_t own_steps;             /* and the same for Jedi bodies in their hero's own model */
    uint32_t own_step_faults;
    uint32_t blade_ticks_bladeless; /* and for a body whose spawn read no blade */
    uint32_t light_releases;        /* blade light ticks run for a worn body only to release */
    uint32_t light_releases_bladeless; /* and for a body whose spawn read no blade */
    uint32_t slot_writes_refused;   /* equipped slot writes around one of those, refused */
    uint32_t notes_down;            /* the wish published again as a body went down */
    uint32_t sizes_written;         /* sizes put on an object through the engine's setter */
    uint32_t publish_faults;        /* wish records the channel refused */
} mp_body_wear_counters_t;

void mp_body_wear_get_counters(mp_body_wear_counters_t *out);

#endif /* MULTIPLAYER_MP_BODY_WEAR_H */
