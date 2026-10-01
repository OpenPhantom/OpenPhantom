/* character_model.h: wear somebody else's body and keep your own.
 *
 * Replacing the PLAYER means writing a hero slot and asking the engine to respawn, so the new
 * character brings its own clip table, its own collision size and its own animation problems.
 * This is the other, much smaller thing a person usually means by "play as somebody else": the
 * geometry changes and nothing else does. Obi-Wan's controls, Obi-Wan's clips, Obi-Wan's speeds,
 * a different body.
 *
 * It is done by rebinding the live render handle rather than by respawning:
 *
 *   the pose stamp is flipped so the engine cannot mistake the new skeleton for a posed one;
 *   the puppet is set aside, the four per node arrays are released, the puppet is put back;
 *   the model is bound, which sizes those arrays off the NEW node count and bakes the rest pose;
 *   the object is scaled to what the target asset asks for;
 *   the weapon model is put back on its mount node, which the rebind has just invalidated.
 *
 * Why the roster is no longer three. The clips keep coming out of the player's own asset and a
 * clip drives joints by ORDINAL, so a model whose node table is not the hero's, node for node, used
 * to be refused: exactly six of the 268 shipped assets carry Obi-Wan's 48 names in his order, and
 * only three of those also carry his body part masks. That is a test of SAMENESS, and it was the
 * right test only for as long as there was nothing between the clip and the skeleton.
 *
 * character_nodemap.c is that something. It translates the player's clips into the target rig's
 * ordinal space by node NAME, so the requirement drops from "the same table" to "enough of the same
 * names", and it answers both one sided cases: a target node the player's rig has no name for holds
 * its rest pose, a player track the target has no node for is dropped. What a row still has to
 * clear is a floor rather than an identity, and the floor is the waist, the chest and the head. It
 * is measured on the live pair when the row is first chosen, and a row that does not clear it is
 * remembered as refused and reads n/a afterwards.
 *
 * It also keeps the target's own build. A keyframe entry carries the joint's ABSOLUTE offset from
 * its parent, which on 46 of the player's 48 joints is exactly the joint's own rest offset in every
 * clip, so a clip carries bone lengths as well as movement. character_clipcopy.c shifts each
 * translated entry by the difference between the two rest poses, and the compositor's own
 * subtraction then leaves movement and nothing else. A small droid runs the way Obi-Wan runs and
 * stays a small droid.
 *
 * What this does not copy: the collision cylinder at obj+0xb8 and obj+0xbc. Not because moving a
 * number nobody can see is untidy, but because the player's world collision never reads those two
 * words in the first place (his probes pass literals, and head clearance is an absolute band
 * above his feet that takes no object at all), and because the two words go into his SAVE BLOCK.
 * A save made while a model is borrowed would hand his own body a foreign cylinder permanently,
 * which is the one kind of damage this feature is not allowed to be able to do.
 *
 * The addresses. Every player wall probe passes a literal into bapmap_probeWall (0x0040C1AE),
 * among them the 0.25 the routine at 0x0044C36D pushes at 0x0044C42B and 0x0044C527, and
 * Plr_HeadClearance (0x0040C464) takes a point and a mask and no object. The two words are
 * written out by bapobj_saveObject's worker (0x00410B31, 0x00410B40) and read back by
 * bapobj_restoreObject's (0x00410DCA, 0x00410DD9); the player's save chunk writer (0x004479D2)
 * hands his own bapObj to it, and the restore overwrites what the bind put there.
 */
#ifndef CHARACTER_MODEL_H
#define CHARACTER_MODEL_H

#include <stdbool.h>
#include <stdint.h>

uint32_t character_model_count(void);

/* The name shown in the panel, and NULL for an id out of range. */
const char *character_model_name(uint32_t id);

/* False keeps the row on screen and reads it as n/a. A row is offered only when every entry point
 * resolved, the guards are in place, the player is in one of the four hero slots the engine's own
 * asset table has, and the blade half below is settled for him: observed to work if his own rig
 * carries a blade node, and not needed if it does not. */
bool character_model_is_available(uint32_t id);

/* Whether this is the model on the body right now. Answered from the engine, so a level change
 * that built a fresh body is reflected without anything having to notice that it happened. */
bool character_model_is_current(uint32_t id);

/* Carries the swap out there and then, because nothing about it needs the engine's permission the
 * way the respawn next door does. False when it could not be done, and then nothing was changed. */
bool character_model_select(uint32_t id);

/* Once a frame, outside every window of the multiplayer: takes down a swap that ended without
 * being told, and says the model worn again when the note refused it the last time. */
void character_model_tick(void);

/* ==============================================================================================
 * THE BLADE LOCK, and it is a precondition rather than a refinement.
 *
 * Plr_SetBladeSize rebuilds the sabre's four vertices from the pair it captured at spawn and hands
 * them to bapobj_setNodeMeshVerts, which edits the .baf's own node geometry. There is no per
 * instance copy of that mesh. So while the player wears somebody else's model, every one of those
 * writes lands in the asset that every NPC built from it is also drawn from, permanently: switch
 * back and the Qui-Gon standing next to you keeps Obi-Wan's blade until the process ends. The
 * engine knows this about itself, which is why player_despawn's last act is to put the mesh back.
 *
 * The guard on Plr_SetBladeSize in model_blade_guard.c declines when the rig has no sabre node.
 * That test cannot catch this one: the player is still Obi-Wan or Qui-Gon and his sabre node is
 * still there. The guard therefore has to ask this question as well, and this is the question.
 *
 * Calling it also RECORDS that the guard asked, and nothing else sets that flag. A build whose
 * guard does not ask can never offer a row to a player who owns a blade: the panel reads n/a and
 * the log says why. That is the whole handshake, and it is deliberately one that cannot be
 * satisfied by accident.
 *
 * It is asked of a player who owns a blade and of nobody else. Plr_SetBladeSize asserts on the
 * player's blade node being non zero, and that assert stands in front of the mesh write rather than
 * beside it, so a player whose blade node is zero cannot reach the write however the function is
 * entered. For him the handshake is unobtainable for the same reason it is unnecessary. What the
 * swap must not do is give such a player a blade node: character_model.c writes that slot as zero
 * for him whatever the borrowed rig carries.
 * ============================================================================================ */
bool character_model_borrows_shared_mesh(void);

/* The two halves of the decision, exposed because they are the part worth checking without a
 * game. `character_model_candidate_of` takes an asset name with or without its suffix, ignores
 * case, and answers the roster index or -1. `character_model_hero_is_supported` answers whether
 * the value is one of the four the engine indexes its hero asset table with; it decides nothing
 * about the blade any more. */
bool character_model_hero_is_supported(int32_t hero_index);
int32_t character_model_candidate_of(const char *asset);

/* ==============================================================================================
 * What the far bodies' path borrows from the swap. It puts models on bodies the multiplayer
 * builds, and a player who never opened the panel has to be able to see them, so the swap's own
 * resolution is asked for from there as well: the five sites, the translation's hook and the
 * blade guard. Idempotent; true once all of them stand.
 * ============================================================================================ */
struct character_model_sites;

bool character_model_resolve(void);

/* The resolved sites, or NULL before character_model_resolve has succeeded. */
const struct character_model_sites *character_model_sites(void);

/* Whether the blade guard has asked this module at least once. A far Jedi's own spawn asks it. */
bool character_model_blade_lock_seen(void);

/* A roster row's asset, loaded once and then kept resident like the player's own, and the model it
 * carries in `out_model` (0 when it carries none). NULL when the swap is not resolved or the asset
 * does not load. Nothing is measured here: a row's fit is measured against the player's own rig
 * and belongs to his panel, and the one verdict this can leave is that an asset does not load,
 * which is as true for his body as for any other. */
void *character_model_load(uint32_t id, uintptr_t *out_model);

#endif /* CHARACTER_MODEL_H */
