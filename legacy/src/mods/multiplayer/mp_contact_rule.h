/* mp_contact_rule.h: what a contact between two players is to this machine, and what each of the
 * three delivery paths does with that answer.
 *
 * Layer 1, pure. The friendly fire gate in mp_body_death.c answers a contact between players, and
 * it used to answer with a yes or a no. There are three answers, not two. A contact between two
 * FAR bodies, one client's player touching another's on the host, is neither allowed nor refused
 * here: it belongs to the victim's own machine, which replays the attacker's shot or swing on its
 * own body and asks the same gate there, with its own player as the one touched. Read as a yes,
 * the host reported it to the victim as a hit, and the victim ran that report with no gate at all,
 * so two clients hurt each other with friendly fire off. And with it on, twice.
 *
 * The three paths read the answer differently, and each reading is written down once, here.
 * Below them is the one rule a far body's collision is written by while a scene plays.
 *
 *   - a far body that is a puppet: its contacts are never carried out here. Only an allowed one
 *     becomes the victim's message and the pain and blood shown for it; a refused one and one that
 *     is not this machine's business send nothing. The weapon's spark is not a hit and is judged
 *     elsewhere, whatever the answer.
 *   - a far body whose contacts are routed into its bank window, and this machine's own body: the
 *     engine's handler runs unless the contact was refused. Not this machine's business reads as
 *     it always did there, because neither path reports anything to another machine.
 */
#ifndef MULTIPLAYER_MP_CONTACT_RULE_H
#define MULTIPLAYER_MP_CONTACT_RULE_H

#include <stdbool.h>
#include <stdint.h>

typedef enum mp_contact_verdict {
    MP_CONTACT_ALLOWED = 0,   /* judged here and allowed, or not a contact between players */
    MP_CONTACT_REFUSED,       /* the rule set, or an ally's side, refuses it here */
    MP_CONTACT_NOT_OURS       /* two far bodies: the victim's own machine judges it */
} mp_contact_verdict_t;

/* Whether a puppet's contact is reported to the victim's machine and shown as a hurt here. */
bool mp_contact_rule_puppet_reports(mp_contact_verdict_t verdict);

/* Whether a delivery that runs the engine's own handler on this machine goes ahead. */
bool mp_contact_rule_carries_out(mp_contact_verdict_t verdict);

/* ============================ A far body in the way of a scene =================================
 *
 * While a scene plays on the host, the far players' bodies there are made passable: their object
 * class goes to 0, which the engine's collision tests skip, so no actor of the scene and not the
 * host's own body is held up by a far player standing in its way. One owner writes that word, per
 * body and only when the answer changes. It is asked with the far player's dead edge as the puppet
 * takes it, and with the object it made passable, nought for none, because a body built again is
 * another object, one that was never made passable.
 *
 *   - A living body is made passable once, and given back once, the class, the side and the
 *     cylinder, when the scene no longer wants it.
 *   - A body lying dead is left as its death clip left it, which is passable already. One still
 *     marked when the scene ends is only unmarked: its revival puts the collision back.
 *   - A revival puts the collision back, unless the scene wants the body passable: then it is made
 *     passable, and given back with the others at the end.
 */
typedef enum mp_contact_collision {
    MP_COLLISION_KEEP = 0,   /* nothing to write, and the mark stays as it is */
    MP_COLLISION_PASSABLE,   /* write class 0 and mark this object */
    MP_COLLISION_RESTORE,    /* write the class, the side and the cylinder back, and unmark */
    MP_COLLISION_LEFT_DEAD,  /* unmark and write nothing: the body lies as its death clip left it */
    MP_COLLISION_UNMARK      /* the mark names an object that is gone: drop it and write nothing */
} mp_contact_collision_t;

/* `passable` is the scene's wish, `alive` the far player's dead edge, `revived` true when asked at
 * the moment he stands again, `marked` the object this owner made passable or nought, `object` the
 * body the bank shows now. */
mp_contact_collision_t mp_contact_rule_collision(bool passable, bool alive, bool revived,
                                                 uint32_t marked, uint32_t object);

#endif /* MULTIPLAYER_MP_CONTACT_RULE_H */
