/* mp_target.h: whom an NPC fights when there is more than one player.
 *
 * The engine has one player and says so in one place. `resolve_target` (0x0042b61c) answers five
 * target kinds, and two of them ARE the player: kind 0 is the player's body, kind 1 is the nearer
 * of the player and an ally. Every chase, every aim, every shot and every parry reads the thing it
 * caches in `character+0x200`, so those two kinds are the whole of an NPC's interest in a human.
 *
 * A far player's body exists on the host and is a body like any other, in one of the co-op classes
 * 5, 6 and 7. It is simply not the one the engine asks about. That is why a client can walk through
 * a room and be ignored, and why a shot the client lands makes the enemy turn on the HOST: the hit
 * lands, the script notices it was hit, and the only human it can resolve is the local one.
 *
 * This file answers the same question with every player in it. It sits behind the engine's own
 * answer rather than in front of it: the original runs first, and its result is kept unless a far
 * body is a better answer. Better means one of two things, in this order:
 *
 *   The one that hurt it. A hit is remembered per placement with the substep it landed on, and for
 *   as long as that memory is fresh the actor turns on whoever landed it. The rule behind
 *   it: everyone must be answerable for their own shot.
 *
 *   The nearest one. With no fresh memory, the closest player wins, which is what the engine's own
 *   kind 1 already does between the player and an ally.
 *
 * Neither rule ever answers a far player lying dead, the same as the engine's own answer for the
 * local player, which asks player_getActorIfAlive: a script that kills whoever it finds, a fan or
 * a force field, finds nobody in a corpse and so cannot kill it again.
 *
 * Only a host runs this. A client's NPCs are parked replicas and resolve nothing.
 *
 * One caller is never answered with a far player: the conversation menu's facing test, known by
 * the address its call returns to. It takes the heading from this machine's own player, so its
 * position has to be that player's too; answered with a far one, a far client passes the test and
 * the conversation opens on the host's screen, and with the host dead the heading is read off no
 * body at all. The engine answers it as it would with nobody else in the world.
 *
 * Every answer to kind 0 and kind 1 is also remembered per actor, whoever gave it, for the scene
 * watch: which player a script last asked about says whose its run is, and with it whose a scene
 * is that the run sets off. The memory of a placement is let go when its actor is removed, so the
 * next life of that placement does not open a door on its predecessor's answer.
 *
 * For a few actors the engine's own answer, the host, is kept and no far player is weighed against
 * it: the actors of the host's own scene, an actor that has taken something of the host's and not
 * given it back, and the actor of a scene just ended (mp_target_rule_claim). Who they are is
 * asked of whoever was set to answer it; with nobody set, no answer is kept.
 *
 * One kind of actor is answered before either rule: an NPC copy that follows or helps goes with
 * the player it belongs to. Its owner's body while the owner stands, and the engine's own answer
 * for the host otherwise: for kind 0 the host's body, or no target at all when the host is dead
 * too, and for kind 1 the nearer of the host and an ally. The proximity rule never sees such a
 * copy, or it would hand a follower to whoever came nearest.
 *
 * Not established: whether an NPC's script can hold a target across states in a way that outlives
 * the cache. The scripts on the traced paths resolve on every use, but the AI machine has 137
 * scripts and only the ones on those paths were read.
 */
#ifndef MULTIPLAYER_MP_TARGET_H
#define MULTIPLAYER_MP_TARGET_H

#include "mp_npc_copies.h"
#include "mp_target_rule.h"
#include "mp_wire.h"

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

/* How long an actor remembers who hurt it, in substeps. Ten seconds at 32 Hz: long enough that a
 * fight follows the one who started it, short enough that an enemy left behind goes back to
 * answering the room rather than a player who has walked out of the level. */
#define MP_TARGET_AGGRO_SUBSTEPS 320u

/* One memory per enemy key (mp_wire.h), which is what an actor answers with when asked which
 * record it came from: the placements of the largest shipped level and the copies an editor
 * can spawn, so this is the whole range and not a guess. */
#define MP_TARGET_SLOTS MP_WIRE_KEY_COUNT

/* Resolves the site and detours it. False with a log line when the site did not resolve; nothing
 * is patched in that case and the engine keeps its own answer. */
bool mp_target_install(void);
bool mp_target_installed(void);

/* Only a host redirects. Set once the bridge knows which side it is. */
void mp_target_set_host(bool is_host);

/* The substep this side is on, for the memory's age. */
void mp_target_tick(uint32_t substep);

/* A contact landed on `victim_actor` and `attacker_object` delivered it. Called for every contact
 * the host sees; the ones whose attacker is not a player body are dropped here rather than at the
 * call site, so the caller does not have to know what a player body is. */
void mp_target_note_attack(uintptr_t victim_actor, uint32_t attacker_object);

/* A key handed to a new life, a copy's, forgets whom its last one was angry with. */
void mp_target_forget(uint32_t key);

/* The actor of `key` was removed: what it last heard about the player is nobody's any more. Who
 * hurt it stays, because a placement that wakes again within that memory is the same fight. */
void mp_target_forget_answer(uint32_t key);

/* Who says for which actors the host stays the player: it fills what it knows about `actor` into
 * the evidence, all but whether the engine answered. NULL for nobody, and then no answer is
 * kept. Asked on a host for every question about the player the engine answered, after the
 * copies' rule and before a far player is weighed. */
typedef void (*mp_target_claim_fn_t)(uintptr_t actor, mp_target_claim_evidence_t *evidence);
void mp_target_set_claim(mp_target_claim_fn_t claim);

/* The body of the player of world slot `slot` on this machine, false when nobody stands there:
 * gone, dead, or not resolved yet. */
typedef bool (*mp_target_owner_fn_t)(uint8_t slot, uint32_t *body);

/* The host's table of copies, for whom a copy goes with, and who answers where an owner stands;
 * NULL for either on a client and with no copies. With no table a copy is answered like any
 * other NPC; with no answer for the owner, every owner reads as gone and a copy goes with the
 * host. */
void mp_target_set_copies(const mp_npc_copies_t *copies, mp_target_owner_fn_t owner);

/* Whether this machine answers for every player now: installed, a host, and a transport up. The
 * one predicate the hull and the scene watch both ask, so the two cannot disagree about whether a
 * script's answer was worth remembering. */
bool mp_target_hosting(void);

/* What `actor` last heard when a script asked for a player, by kind 0 or kind 1, while this
 * machine was hosting. False when nothing is on record. `player` false means the answer was an
 * ally or nobody; otherwise `bank` names whose body it was, 0 for this machine's own player and 1
 * and up for a far one. `age` is in substeps. */
bool mp_target_last_answer(uintptr_t actor, bool *player, uint8_t *bank, uint32_t *age);

/* Whether a player hurt `actor` lately, from the same memory the resolver turns it by, and whose
 * bank that player is. */
bool mp_target_last_attacker(uintptr_t actor, uint8_t *bank);

/* Where the player of `bank` stands, off the body this resolver answers with. False for a bank
 * with no body. */
bool mp_target_player_position(uint8_t bank, float out[3]);

void mp_target_report(void);

#endif /* MULTIPLAYER_MP_TARGET_H */
