/* character_mount.h: the node a weapon name resolves to on a borrowed rig, so a blow lands.
 *
 * ==============================================================================================
 * Where the damage goes, because it is not where the swing animation is.
 *
 * Both weapon paths resolve a node by NAME against the living body on every use, and the borrowed
 * rig does not carry those names.
 *
 * MELEE. The swing entry reads the swing row's node name id, resolves it against the body and
 * stores the answer in the body's contact cell. The actor to actor contact then opens with two
 * compares: the contact event and the contact NODE, and a node of zero skips the whole branch
 * before any sphere is computed. So a name the rig cannot answer does not misplace the hit, it
 * removes it: nothing is measured, no contact event is sent, and no damage is dealt. The receiving
 * side has the same test a second time.
 *
 * RANGED. The weapon row names the muzzle node the same way, and the node query does NOT write its
 * output vector when the node it was given carries no mesh. The fire path uses the vector anyway,
 * so the bolt leaves an uninitialised stack address.
 *
 * ==============================================================================================
 * The answer is the node the weapon is visibly hanging on
 *
 * character_prop.c draws the player's own weapon mesh at the borrowed rig's right hand. That hand
 * is therefore the one node on the borrowed rig where the weapon actually is, and it is the node
 * this module substitutes into the lookup. Contact, muzzle and picture then fall on one point:
 * the hand answers the name, and character_prop answers the sphere for that same node with the
 * world centre of the mesh it is drawing.
 *
 * Nothing is written into a model. The hook substitutes a RETURN VALUE, for one body pointer, and
 * only while character_prop is carrying something on it.
 *
 * ==============================================================================================
 * Why blade names are no longer refused
 *
 * An earlier rule refused `sabreblad*`, `sabreblade*` and `blade*` outright, because a non zero
 * answer for one of those would reach playerRecord+0x4C, and Plr_SetBladeSize rebuilds that node's
 * vertices inside the .baf every actor of the asset is drawn from. Refusing them also left every
 * swing row dead, since all nineteen rows a hero can reach name `sabreblad01`.
 *
 * The cell has exactly one writer, the player spawn, and the spawn allocates a FRESH body before
 * it resolves its six names. The hook answers for one body pointer, the one character_prop is
 * carrying a weapon on, so the spawn's lookups reach a body this module declines to answer for.
 * The blade guard in model_blade_guard.c refuses the resize while a model is borrowed on top of
 * that.
 *
 * ==============================================================================================
 * The one asker that must not be answered, and why it is not a name
 *
 * The reasoning above weighs the blade names against the swing rows and comes out in favour of
 * answering, and that is still right: all nineteen swing rows a hero can reach name `sabreblad01`,
 * so refusing it would leave a borrowed body swinging at nothing. What it missed is that one
 * caller asks for those names as GEOMETRY. The sabre effect registration resolves the four card
 * names against the living body and keeps each answer as a node index, and the pass that draws
 * those records copies the node's WHOLE MESH into a four vertex buffer. Answering it with the hand
 * hands it a hand mesh, and the copy runs off the end of a stack frame.
 *
 * So the discrimination cannot be by name: the same name is a place on the body to one caller and
 * a quad to another. It is by ASKER, and character_cards.c makes it by holding this module across
 * the registration. That is why the hold below exists and why it is not a rule in the pure half.
 */
#ifndef CHARACTER_MOUNT_H
#define CHARACTER_MOUNT_H

#include <stdbool.h>
#include <stdint.h>

/* The engine's node name field is 16 bytes with the terminator inside it. */
#define MOUNT_NAME_BYTES   16u

/* Leave the engine's own answer alone. */
#define MOUNT_NO_NODE      (-1)

/* ==============================================================================================
 * THE PURE HALF. No engine, no state, and this is where the whole decision is.
 * ============================================================================================ */

/* Whether a node name belongs to the weapon geometry family. The rule is the three prefixes the
 * shipped rigs author, `gun`, `sabre` and `blade`, and nothing else. It is a family test and never
 * a per character list: no name here is tied to who wears it.
 *
 * Those three cover every name the two tables can ask for except `waist`, `lhand`, `rhand` and
 * `lfoot`, which are body parts a rig either has or genuinely does not. */
bool character_mount_name_is_weapon_geometry(const char *name);

/* Whether a name is sabre BLADE geometry, namely `blade*`, `sabreblad*` and `sabreblade*`. Kept
 * because it names the sub family the header above discusses, and it is what a reader checks the
 * claim about playerRecord+0x4C against. */
bool character_mount_name_is_blade(const char *name);

/* THE RULE. Which node should answer a lookup for `wanted`, or MOUNT_NO_NODE for "leave the
 * engine's answer alone".
 *
 * `engine_answer` is what the lookup itself came back with, and anything other than zero means the
 * rig carries the name and this rule leaves it; whether that node holds a sphere where the weapon
 * is, is character_mount_answer_found's question. `hand` is the node the player's weapon is being
 * drawn on.
 *
 * Zero and below are refused for `hand` as well, and not only as a range test: zero is the value
 * the contact gate rejects, so answering with it would leave the blow doing nothing while looking
 * like it had been repaired. */
int32_t character_mount_answer(const char *wanted, int32_t engine_answer, int32_t hand);

/* ==============================================================================================
 * THE LIVE HALF.
 * ============================================================================================ */

/* The weapon geometry `model` carries of its own, as node slots, so that a borrowed body does not
 * wear two weapons.
 *
 * The family rule above decides what counts, which is what keeps this answer and the substitution
 * from having two opinions about the same node. `hand` is the node record the player's weapon is
 * drawn on: neither it nor anything on its parent chain is ever answered with, because thing+0x28
 * gates the pose CONCATENATION as well as the draw and hiding an ancestor of the hand would leave
 * the hand's own world matrix unbuilt. `mount` is the engine's own name for name id 7, or NULL
 * when the caller could not read it; it is not spelled here.
 *
 * Answers how many slots were written, at most `max`, each one already measured against `model`'s
 * own node count. Nodes it answers with are not descended into, because one word in the visibility
 * table takes the whole subtree: measured over the 132 shipped rigs that carry a hand, the longest
 * list is two.
 *
 * Reading only, and nothing is remembered between calls. */
uint32_t character_mount_own_weapon_nodes(uintptr_t model, uintptr_t hand, const char *mount,
                                          uint32_t *out_slots, uint32_t max);

/* The rule for a name the rig does carry, on the body character_prop draws the player's weapon on.
 *
 * The engine found `wanted` at `found`, and the swing starter stores that answer as the contact
 * node (0x0044E880, 0x0044E8A3). The node holds no sphere where the weapon is when it has no mesh,
 * so bapobj_nodeSphere turns back at 0x0041429C before it writes the centre and the pair pass
 * takes the centre off its own stack, or when it or an ancestor is one of `hidden`, the rig's own
 * weapon words, whose matrices the concatenation never builds. Such a node is answered with
 * `equipped`, the node character_prop answers the drawn weapon's sphere for.
 *
 * `found` indexes the node array the way bapobj_nodeSphere does (0x0041428B); `hidden` holds
 * matrix slots, the index thing+0x28 is read with. MOUNT_NO_NODE leaves the engine's answer: a
 * name outside the weapon family, a node that holds its sphere, an `equipped` of 0 or below, or
 * one equal to `found`. Reads only `model`; nothing is remembered. */
int32_t character_mount_answer_found(uintptr_t model, const char *wanted, int32_t found,
                                     const uint32_t *hidden, uint32_t hidden_count,
                                     int32_t equipped);

/* Resolves the player record, the node name lookup and the engine's node name table, cross checks
 * the three against each other and puts the hook in front of the lookup. Idempotent; false leaves
 * the engine untouched and then a borrowed body deals no damage, which is what it did before.
 *
 * Installed from character_prop_draw.c together with the other two detours of this feature, so
 * that the picture, the muzzle and the blow are armed by one call and never half of them. */
bool character_mount_install(void);

bool character_mount_is_armed(void);

/* Holds the substitution off, or lets it back on, and answers what it was before. While it is held
 * the hook returns the engine's own result for every name and every body, which is the behaviour
 * this executable shipped with.
 *
 * The caller is character_cards.c and the reason is above the include guard: one asker needs a
 * blade name to name a four vertex quad, and there is no answer this module could give it that
 * would be both non zero and true. Taking the previous value back rather than writing false is not
 * ceremony: it is what keeps a second bracket, if one is ever needed, from cancelling the first. */
bool character_mount_hold(bool on);

/* The player record this module resolved, so a caller can cross check it against its own. 0 before
 * character_mount_install() has succeeded. */
uintptr_t character_mount_player_record(void);

#endif /* CHARACTER_MOUNT_H */
