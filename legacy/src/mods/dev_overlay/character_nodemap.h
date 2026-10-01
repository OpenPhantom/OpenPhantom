/* character_nodemap.h: put the player's own animation on somebody else's skeleton, by NAME.
 *
 * The engine has no name layer in the pose path, and that is the whole reason this module exists.
 * The pose compositor picks the animation track for a skeleton node out of the node's own word at
 * node+0x44, and it uses that same word to address the joint matrix array and the per track
 * keyframe cursor. So an animation track reaches a joint by ORDINAL, the ordinal is authored into
 * the model, and every shipped model authors it equal to the node's own position in the array. A
 * model whose nodes are in another order is therefore driven by tracks meant for other joints.
 *
 * What this module does not do, and the reason is the same word: it does not rewrite node+0x44.
 * That word is read by the compositor as a clip index and by the hierarchy concatenation as a
 * joint matrix row, while the compositor WRITES that row by array position. Change the word and
 * the two disagree, so the local matrix a joint is posed into is not the one its world matrix is
 * built from. The value has to stay equal to the ordinal, which leaves the clip as the only side
 * of the pair that can be moved.
 *
 * So the clip is moved. For as long as a swap is live, the player's puppet is handed a private
 * copy of whatever clip it is playing: the same header and a node table reordered so that entry j
 * is the track for the name the TARGET model carries at node j. The copy is built once per clip
 * and kept until the swap ends. Nothing the engine owns is written, and the substitution is
 * visible only inside the compositor call, so every other reader of a track still sees the clip
 * the engine put there.
 *
 * The copy also re-seats the pose on the target's own build, and that is the difference between
 * wearing a body and being drawn as one.
 *
 * A keyframe entry carries an ABSOLUTE local pose: three floats of offset from the parent joint at
 * entry+0x08 and three of orientation at entry+0x14, plus the two rates the compositor extrapolates
 * them with. Measured over the player's own 114 clips, 46 of his 48 joints carry an entry offset
 * equal to the joint's own authored rest offset to within 1e-5 across every clip; only the waist
 * ever translates. So an entry offset is not motion, it is the length of the bone, authored into
 * the animation.
 *
 * The compositor subtracts the TARGET joint's rest pose from that absolute value, blends the
 * remainder by track weight, and adds the same rest pose back. At full weight the two cancel
 * exactly, so the target joint lands on the reference rig's absolute pose and wears the reference
 * rig's bone lengths. That cancellation is why no edit to the model can fix this: whatever rest
 * pose the target's nodes carry, it is subtracted and added back. The clip is the only movable
 * side, a second time.
 *
 * So every entry of a translated record is shifted by `restTarget[j] - restReference[i]`, once,
 * into a private pool. The compositor's own subtraction then leaves `clip - restReference`, which
 * is the reference rig's motion measured from its own rest, and its addition puts that motion on
 * the target's rest. A joint that does not translate in the clip comes out on the target's own
 * offset; the waist keeps its authored travel; a rig whose rest orientation differs keeps the
 * orientation its meshes were modelled in. The rate fields are NOT shifted, because the derivative
 * of a constant is zero. A record whose shift is exactly zero keeps the asset's own pool, so an
 * identical rig costs no memory and comes out bit for bit as it did before.
 *
 * The two one sided cases are both answered by the same field, numEntries at clip node +0x24. The
 * compositor skips a node whose keyframe record has no entries, and a skipped node comes out at
 * its authored rest pose rather than at zero.
 *
 *   a target node the reference rig has no name for gets an empty record, so it holds its bind
 *     pose and stays attached to its parent. Jar Jar's ears do not move and do not disappear.
 *   a reference track whose name the target does not carry is simply never referenced by the new
 *     node table, so it is dropped. Obi-Wan's ponytail track drives nothing on a droid.
 *
 * The body part mask is reproduced in the copy. The compositor decides per node and per frame
 * whether a clip plays at its high or its low priority by ANDing the node's part word at node+0x48
 * with the clip's own type word, and that is what lets a firing overlay own the arm of a running
 * body. Almost every model outside the six hero rigs authors every node as one part, so the mask
 * cannot be evaluated on the target. The copy answers it on the REFERENCE side instead: a node
 * whose reference part word does not intersect the clip's type gets an empty record, and the
 * copy's own type word is set so that everything left in the table wins its node. The overlay
 * therefore reaches exactly the joints it was authored for, whatever the target rig says about
 * itself. One difference is not reproduced: a node outside an overlay's mask used to receive that
 * overlay at the low priority, which mattered only while a crossfade held the base clip below full
 * weight, and in the copy it receives nothing at all.
 */
#ifndef CHARACTER_NODEMAP_H
#define CHARACTER_NODEMAP_H

#include "character_bodies.h"

#include <stdbool.h>
#include <stdint.h>

/* The per track keyframe cursor array is 64 entries wide and the word above it is the track's
 * clock, so a rig with more nodes than this would write its last cursors over the clock. The
 * largest rig the game ships has 48. */
#define NODEMAP_MAX_NODES     64u

/* No mapping at all for this target node, so it holds its bind pose. */
#define NODEMAP_NO_TRACK      (-1)

typedef struct nodemap_fit {
    uint32_t reference_nodes;
    uint32_t target_nodes;
    uint32_t matched;      /* target nodes that found a reference name       */
    uint32_t held;         /* target nodes with no track, held at bind pose  */
    uint32_t dropped;      /* reference tracks with no target node           */
    bool     spine;        /* the target carries the reference waist, chest and head */
} nodemap_fit_t;

/* A joint's authored rest pose: the offset to its parent and the orientation its meshes are
 * modelled in. On a live model the two sit at node+0x6c and node+0x78, six floats in a row. */
typedef struct nodemap_rest {
    float pos[3];
    float euler[3];
} nodemap_rest_t;

/* The node name comparison every lookup here makes: case insensitive, bounded at the engine's name
 * field, and never a match between two empty names. */
bool character_nodemap_same_name(const char *left, const char *right);

/* THE PURE HALF, and the half a test can drive; it lives in character_restmap.c.
 *
 * Fills `map` with one entry per TARGET node: the reference node ordinal that drives it, or
 * NODEMAP_NO_TRACK. `map` must hold `target_count` entries. A reference name is claimed at most
 * once, so a target that repeats a name gets a track on its first occurrence only.
 *
 * False when either side is empty or wider than NODEMAP_MAX_NODES, and then nothing was written.
 * The comparison is case insensitive, which costs nothing and does not depend on the observation
 * that every shipped node name is already lower case. */
bool character_nodemap_build(const char *const *reference, uint32_t reference_count,
                             const char *const *target, uint32_t target_count,
                             int32_t *map, nodemap_fit_t *fit);

/* Whether a fit is one this module will carry. It is not a safety test: every shipped rig is safe
 * to translate. It is the floor below which the result is a rigid prop rather than a character,
 * and it is the reference rig's waist, chest and head, the three the engine's own node name table
 * calls ids 0, 2 and 1. */
bool character_nodemap_fit_is_offered(const nodemap_fit_t *fit);

/* The other pure half. Fills one shift per TARGET node: what has to be added to the reference
 * clip's absolute pose so that the compositor's own subtraction of the target's rest leaves the
 * reference's motion measured from the REFERENCE's rest. That is `target[j] - reference[map[j]]`,
 * and it is zero for a target node with no track, which never reaches a shifted entry anyway.
 *
 * `rebase` must hold `target_count` entries. False when either side is empty or wider than
 * NODEMAP_MAX_NODES, and then nothing was written. */
bool character_nodemap_rebase(const nodemap_rest_t *reference, uint32_t reference_count,
                              const nodemap_rest_t *target, uint32_t target_count,
                              const int32_t *map, nodemap_rest_t *rebase);

/* Whether a shift changes anything at all. A record whose shift is zero keeps the asset's own
 * keyframe pool, which is what makes a swap between two identical rigs cost no memory and come out
 * exactly as it did before this layer existed. */
bool character_nodemap_shift_is_zero(const nodemap_rest_t *shift);

/* Applies one shift to one keyframe entry in place. `entry` is the 0x38 byte record: the offset at
 * +0x08 and the orientation at +0x14 move, the two rate triples at +0x20 and +0x2c do not, because
 * a constant added to a value does not change how fast the value moves. */
void character_nodemap_shift_entry(void *entry, const nodemap_rest_t *shift);

/* ============================================================================================ */

/* Resolves the pose compositor and puts the substitution hook in front of it. Idempotent, and
 * false when the compositor did not resolve, in which case no swap may be offered: without it the
 * player's tracks would drive the target's joints by the wrong ordinal. */
bool character_nodemap_install(void);

/* Measures a pair of live models without arming anything. False when either model cannot be read
 * or is wider than NODEMAP_MAX_NODES. */
bool character_nodemap_measure(uintptr_t reference_model, uintptr_t target_model,
                               nodemap_fit_t *fit);

/* One body the translation is armed for. The player's own body is `local`, bank 0 and no block; a
 * far body names its bank, its serial and its bank block. `share_pass` lets the body play a pair
 * another body plays already, when one of that pair's wearers was found alive in that pass; zero
 * always measures a pair of its own. */
typedef struct nodemap_body {
    uintptr_t thing;
    uintptr_t obj;
    uintptr_t block;
    uintptr_t reference;
    uintptr_t target;
    bool      local;
    uint8_t   bank;
    uint32_t  serial;
    uint32_t  share_pass;
} nodemap_body_t;

/* Arms the translation for one body. The hook substitutes for that handle while it still wears
 * the target, and a row already filed for the same handle by the same owner is replaced; a handle
 * somebody else filed is refused. False leaves nothing armed for this body and changes no other
 * body's row. */
bool character_nodemap_arm(const nodemap_body_t *body);

/* Takes one body's row down and clears its puppet's keyframe cursors while the handle still wears
 * the target, since they were left in the target's ordinal space. The pair's copies go with its
 * last wearer, and the waterline with the player's own body. */
void character_nodemap_disarm(uintptr_t thing);
void character_nodemap_disarm_local(void);

/* Whether any body is translated at all. */
bool character_nodemap_is_armed(void);

/* Whether the body on this handle still wears what its row says, asked of the engine now, and the
 * row when it does. The draw hook asks this; `out` may be NULL. */
bool character_nodemap_holds(uintptr_t thing, body_entry_t *out);

/* The far bodies' path walks the table, asks each row the same question by its index, and marks
 * the rows it found alive in its pass. */
const character_bodies_t *character_nodemap_bodies(void);
bool     character_nodemap_row_holds(uint32_t index);
uint32_t character_nodemap_begin_pass(void);
void     character_nodemap_mark(uint32_t index, uint32_t pass);

/* What the draw hook does for a row beside the pose: the backface drop taken off, and a far body's
 * own weapon nodes hidden again before each draw. False when the handle has no row. */
bool character_nodemap_set_two_sided(uintptr_t thing, bool on);
bool character_nodemap_any_two_sided(void);
bool character_nodemap_set_hidden(uintptr_t thing, const uint32_t *slots, uint32_t count);

/* One node index on a live model, by name, case insensitive. Answers NODEMAP_NO_TRACK when the
 * model does not carry the name, which the engine's own lookup cannot do: it answers 0, and 0 is
 * a valid node. */
int32_t character_nodemap_find(uintptr_t model, const char *name);

/* The model's node count, or 0 when it cannot be read. */
uint32_t character_nodemap_node_count(uintptr_t model);

#endif /* CHARACTER_NODEMAP_H */
