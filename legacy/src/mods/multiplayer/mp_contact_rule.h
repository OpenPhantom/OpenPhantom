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
 * The three paths read the answer differently, and each reading is written down once, here:
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

typedef enum mp_contact_verdict {
    MP_CONTACT_ALLOWED = 0,   /* judged here and allowed, or not a contact between players */
    MP_CONTACT_REFUSED,       /* the rule set, or an ally's side, refuses it here */
    MP_CONTACT_NOT_OURS       /* two far bodies: the victim's own machine judges it */
} mp_contact_verdict_t;

/* Whether a puppet's contact is reported to the victim's machine and shown as a hurt here. */
bool mp_contact_rule_puppet_reports(mp_contact_verdict_t verdict);

/* Whether a delivery that runs the engine's own handler on this machine goes ahead. */
bool mp_contact_rule_carries_out(mp_contact_verdict_t verdict);

#endif /* MULTIPLAYER_MP_CONTACT_RULE_H */
