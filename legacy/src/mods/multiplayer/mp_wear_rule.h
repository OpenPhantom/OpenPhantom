/* mp_wear_rule.h: three rules for a far body that may wear a borrowed model, pure.
 *
 * The model is put on by the developer overlay at the end of a scene, and the multiplayer only
 * learns of it through the overlay's answer and through the engine: the render handle of a body
 * that wears something carries another model than its actor names. These rules take what the
 * multiplayer knows at one substep and answer three questions: whether the body has to be built
 * again, whether the node rotations that arrive from the far player fit the rig this body shows,
 * and what size the body is drawn at. No engine, no clock and no state of their own, so every
 * case below runs in a test.
 *
 * Two names that look alike are two different things here. The WISH is the model this machine
 * asks the overlay for; a deathmatch empties it. The SENDER'S MODEL is what the far player wears
 * on the far machine, and it decides which rig the rotations belong to whatever this machine
 * wishes. A deathmatch that emptied the second as well would hand the borrowed rig's joint numbers
 * to the hero's rig.
 */
#ifndef MULTIPLAYER_MP_WEAR_RULE_H
#define MULTIPLAYER_MP_WEAR_RULE_H

#include <stdbool.h>
#include <stdint.h>

/* How long a changed wish has to stand before a body that wears something else is built again, in
 * milliseconds of the wall clock. The repeated player table asks the far player's look once a
 * second, so a table that lags an appearance event flips the wish back for up to a second and then
 * forward again. A rebuild costs the body its puppet state and the shared blade its length, so a
 * flip inside one table period must not cause one: one period plus the time the answer takes is a
 * second and a half. The table's period is wall time and so is the wait; counted in substeps it
 * was 48, which the 60fps cheat's substep of 1/64 made three quarters of a second. */
#define MP_WEAR_SETTLE_MS 1500u

/* What the overlay last answered for the body a bank has now. `read` is false while no answer for
 * that body's serial is in hand; the other fields mean nothing then. `echo` names what the body
 * wears after the answer, "" for nothing, and is never NULL. */
typedef struct mp_wear_answer {
    bool        read;
    uint8_t     state;    /* MODEL_WEAR_STATE_* */
    uint8_t     reason;   /* MODEL_WEAR_REASON_*, NONE unless refused */
    const char *echo;
    float       scale;    /* the worn asset's own scale, for WORN */
    bool        weapon;   /* the WORN body carries its player's weapon at the borrowed hand */
} mp_wear_answer_t;

/* Whether the body has to be taken down and built again as its hero. `worn` is the engine's
 * answer, `wanted` the wish ("" for none) and `stood_ms` how long it has stood unchanged.
 *
 * Only on an answer for this body: without one the rule waits, because an answer the overlay owes
 * and cannot publish would otherwise rebuild the body every half second for as long as it stays
 * owed. An answer that the overlay could neither put a model on nor take it off again rebuilds at
 * once, with no wait: the engine may well report that body as not worn, and its arrays are half
 * the model's and half the hero's until a new body replaces it. Otherwise the wish has to have
 * settled and the body to wear something the wish no longer names, the echo compared and not the
 * refused name. Names are compared regardless of case. */
bool mp_wear_rule_rebuild(bool worn, const mp_wear_answer_t *answer, const char *wanted,
                          uint32_t stood_ms);

/* Whether a deathmatch may leave a bank's wish standing.
 *
 * A far body that wears a borrowed rig with no weapon at that rig's hand swings at a node nothing
 * measures, so its wearer strikes nobody on every machine that dressed him. Outside a deathmatch
 * that is a look; inside one it is the game. So a deathmatch asks for a model only while the
 * overlay says the places a borrowed weapon is drawn and shot from have resolved, and empties the
 * wish again for a body the overlay dressed and hung no weapon on, which the ordinary rebuild
 * rule then takes back to its hero. Nothing here is asked outside a deathmatch: the model is what
 * the far player chose to look like. */
bool mp_wear_rule_deathmatch_asks(bool ready_weapon, const mp_wear_answer_t *answer);

/* Whether the rotations the far player sends belong to the rig this body shows. They are node
 * indices of the rig the SENDER wears (`sender`, "" when none), so they fit when the sender wears
 * no model and this body wears none either, or when the overlay answered WORN with the sender's
 * own model as its echo. The second case includes a body whose model is its hero's own, which the
 * overlay answers WORN without rebinding and the engine reports as not worn: one rig either way. */
bool mp_wear_rule_rig_is_senders(bool worn, const mp_wear_answer_t *answer, const char *sender);

/* The size the body is drawn at: `factor` times the scale of the asset whose model it shows, the
 * worn asset's own scale from a WORN answer or `actor_scale`, the actor's own, for a body that
 * wears nothing. A factor of zero was never asked for and means 1.0. False, and nothing to write,
 * for a body that wears something no WORN answer describes, since its own scale is unknown, and
 * for a product that is not a positive finite number. */
bool mp_wear_rule_scale(float factor, float actor_scale, bool worn, const mp_wear_answer_t *answer,
                        float *out);

/* Two asset names are one name: equal without regard to case, NULL read as "". */
bool mp_wear_rule_same_name(const char *a, const char *b);

/* The blade length of a body whose mesh this machine may not write.
 *
 * Plr_TickBladeState 0x00449A84 is two guarded steps over one field. While the equipped slot is
 * the sabre's and the length at pr+0x210 is not 1, the length grows by twice the substep at
 * pr+0x74 and is clamped at 1; while the requested slot is not the sabre's and the length is not
 * 0, it shrinks by ten times the substep and is clamped at 0. Each step that ran hands the new
 * length to the setter, which writes the mesh AND puts the length into pr+0x298 and one and a
 * half times it into pr+0x29C, the strength and the radius the blade light is placed with.
 *
 * A body that wears a borrowed model may not reach that setter: the mesh it would write is the
 * one its render handle draws, which is the borrowed model's and not the hero's. The arithmetic
 * above touches no mesh at all, so it is repeated here and the three fields are written straight
 * into the block. The two steps are one call and the second reads what the first left, which is
 * the engine's own order: a sabre being put away grows by the substep and shrinks by ten times
 * it in the same substep.
 *
 * The rates and the radius factor are the constants at 0x004A86A0, 0x004A86D0 and 0x004A86D4. */
#define MP_WEAR_BLADE_GROW_RATE    2.0f
#define MP_WEAR_BLADE_SHRINK_RATE 10.0f
#define MP_WEAR_BLADE_LIGHT_RANGE  1.5f

typedef struct mp_wear_blade_step {
    float size;      /* the new pr+0x210 */
    float power;     /* pr+0x298, the blade light's strength */
    float range;     /* pr+0x29C */
    bool  grew;      /* the growing step ran */
    bool  shrank;    /* the shrinking step ran */
} mp_wear_blade_step_t;

/* The step `size` takes over one substep of `substep` seconds with those two slots equipped and
 * requested. True when a step ran and `out` is what the block should hold; false, and nothing to
 * write, when neither step ran or when the length or the substep is not a number. A length
 * outside 0 to 1 is stepped and clamped exactly as the engine would, so a block that holds one
 * comes back inside the range rather than being refused. */
bool mp_wear_rule_blade_step(float size, float substep, uint32_t weapon, uint32_t requested,
                             mp_wear_blade_step_t *out);

#endif /* MULTIPLAYER_MP_WEAR_RULE_H */
