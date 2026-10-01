/* character_bodies.h: the bodies that wear a borrowed model, and the pairs they are translated by.
 *
 * The swap used to have one body, the player's own, and every module of it kept that body in a
 * handful of globals. A far player who wears a model is another body on this machine, and up to
 * MODEL_WEAR_BANKS of them stand beside the player's own. So the bookkeeping is two tables: one of
 * BODIES, each a render handle wearing a target model over a reference model, and one of PAIRS. A
 * pair is a reference rig and a target rig, which is all the translation is measured from, so two
 * bodies of the same hero in the same model are translated by the same pair and its clip copies.
 *
 * This file reads nothing. Every value out of the engine arrives as a body_reads_t filled by the
 * caller, so every decision over the two tables can be driven by a test without a game. The reads
 * are in character_nodemap.c, beside the pose hook that needs them most often.
 *
 * A pair is shared only with a body found alive in the same pass. A clip copy keeps pointers
 * into the keyframe pool of the reference asset wherever its shift is zero. A far body's reference
 * is its hero asset, and taking that body down gives the asset back to the resource layer, which
 * may free it and load it again at the same address. Two pointers that agree can then name two
 * different assets. A pair one of whose wearers was proven alive at this scene end still holds its
 * reference through that wearer, so there the same address is the same asset; any other pair with
 * the same two addresses is not trusted, and a fresh pair is measured instead.
 */
#ifndef CHARACTER_BODIES_H
#define CHARACTER_BODIES_H

#include "common/model_wear_note.h"

#include <stdbool.h>
#include <stdint.h>

/* The player's own body and one per far bank the note can describe. A pair has at most one
 * wearer of its own per body, so the pair table can never need more rows than the body table. */
#define BODY_MAX       (1u + MODEL_WEAR_BANKS)
#define PAIR_MAX       BODY_MAX
#define BODY_NONE      0xFFFFFFFFu

/* The weapon nodes a far body's rig carries of its own and keeps hidden. One word hides a whole
 * subtree, and over the shipped rigs that carry a hand the longest list is two. */
#define BODY_HIDE_MAX  8u

typedef struct body_entry {
    bool      used;
    bool      local;          /* the player's own body: the waterline follows it              */
    bool      two_sided;      /* the backface drop is taken off its draw                       */
    uint8_t   bank;           /* 0 for the player's own body, 1..MODEL_WEAR_BANKS for a far one */
    uint32_t  serial;         /* a far body's serial when it was dressed                       */
    uintptr_t thing;          /* the render handle                                             */
    uintptr_t obj;            /* the bapObj that owns it                                       */
    uintptr_t block;          /* a far body's bank block; 0 for the player's own               */
    uintptr_t reference;      /* the model the clips were authored for: the actor's own        */
    uintptr_t target;         /* the model put on                                              */
    uint32_t  target_nodes;
    uint32_t  pair;
    uint32_t  checked;        /* the pass this body was last found alive in                    */
    uint32_t  hidden_count;
    uint32_t  hidden[BODY_HIDE_MAX];   /* a far body's own weapon slots, hidden again per draw */
} body_entry_t;

typedef struct body_pair {
    bool      used;
    uintptr_t reference;
    uintptr_t target;
    uint32_t  target_nodes;
    uint32_t  wearers;
    uint32_t  made;           /* which pair this row holds, counted over the process */
} body_pair_t;

typedef struct character_bodies {
    body_entry_t body[BODY_MAX];
    body_pair_t  pair[PAIR_MAX];
    uint32_t     pass;
    uint32_t     made;
} character_bodies_t;

/* What the engine says about a body right now. A read that failed leaves its field zero, which no
 * body in the table can agree with. */
typedef struct body_reads {
    uintptr_t block_body;     /* block+0x0C, read for a far body only                   */
    uintptr_t obj_thing;      /* obj+0x9C, the handle the object owns                  */
    uintptr_t worn;           /* thing+0x04, the model on the handle                   */
    uint32_t  worn_nodes;     /* that model's node count                               */
    uintptr_t actor_model;    /* (obj+0x14)+0xE0, the model the body's actor names     */
} body_reads_t;

/* What arming or forgetting a body does beside the table. */
typedef struct body_plan {
    bool clear_cursors;       /* the puppet's keyframe cursors are cleared             */
    bool waterline;           /* the swim correction is armed or taken down with it    */
} body_plan_t;

/* The row of the body on this render handle, or BODY_NONE. */
uint32_t character_bodies_find(const character_bodies_t *t, uintptr_t thing);

/* The row of the player's own body (bank 0) or of a far bank, or BODY_NONE. */
uint32_t character_bodies_find_bank(const character_bodies_t *t, uint32_t bank);

bool     character_bodies_any(const character_bodies_t *t);

/* The one predicate for "this handle still wears what the row says", asked by the pose hook, the
 * draw hook, the forgetting of a row and the far path's check that a body is alive. The handle
 * wears the target with the node count the copies were sized for, the object still owns the
 * handle, and the object's actor still names the reference, so an address the allocator handed
 * to somebody else cannot agree by accident. A far body's block must still name its object as
 * well: that read is the multiplayer's memory, always valid, and it is what catches a far body
 * that was taken down before anything of the engine's is read. The player's own body is not
 * asked about a block, because inside a bank window his block carries a far body's record. */
bool     character_bodies_holds(const body_entry_t *body, const body_reads_t *reads);

/* A pair for this reference and target: an existing one only when `share_pass` is not zero and
 * one of its wearers was found alive in that pass, otherwise a free row, and then `fresh` says the
 * caller has to measure it and build its copies. BODY_NONE when every row is taken. */
uint32_t character_bodies_pair_take(character_bodies_t *t, uintptr_t reference, uintptr_t target,
                                    uint32_t target_nodes, uint32_t share_pass, bool *fresh);

/* Gives back a pair nobody wears, which is what a failed arm after a fresh take leaves. */
void     character_bodies_pair_drop(character_bodies_t *t, uint32_t pair);

/* Files a body on the pair it names. BODY_NONE when the handle already has a row, the owner (the
 * player or that far bank) already has one, the pair is not taken, or the table is full. */
uint32_t character_bodies_add(character_bodies_t *t, const body_entry_t *body);

/* Takes a row out. Answers the pair when this was its last wearer, and the caller then releases
 * what was built for it; BODY_NONE otherwise. */
uint32_t character_bodies_remove(character_bodies_t *t, uint32_t index);

/* A new pass over the bodies, never zero, and a row found alive in it is marked with it. */
uint32_t character_bodies_begin_pass(character_bodies_t *t);
void     character_bodies_mark(character_bodies_t *t, uint32_t index, uint32_t pass);

/* Arming a row clears the cursors of whatever puppet the handle has; forgetting one clears them
 * only while the handle still wears the target, since a handle that no longer does may be a block
 * somebody else owns now. The waterline goes with the player's own body and with no other. */
body_plan_t character_bodies_on_arm(const body_entry_t *body);
body_plan_t character_bodies_on_disarm(const body_entry_t *body, const body_reads_t *reads);

/* What the pose hook does for the handle it is asked to pose. A handle without a row is posed as
 * the engine built it, and one whose row holds from the copies of its pair. A row that does not
 * hold while the handle still wears the row's target is the case the table has no answer for: its
 * tracks name the reference's clips and its keyframe cursors stand in the target's ordinal space,
 * and the compositor's first pass stops a cursor only where it meets the last entry of a joint's
 * pool, so a cursor from the other space walks past it. That body is posed with its tracks off
 * for the call, which is its bind pose. A handle that wears anything else is somebody else's. */
typedef enum body_pose {
    BODY_POSE_ENGINE = 0,     /* the engine's own call, nothing substituted        */
    BODY_POSE_TRANSLATED,     /* each track's clip swapped for the pair's copy     */
    BODY_POSE_FROZEN          /* every track's flags cleared for the call          */
} body_pose_t;

body_pose_t character_bodies_pose(const body_entry_t *body, bool holds, uintptr_t worn);

/* ==============================================================================================
 * The blade guard's question, as a decision over what the block holds.
 *
 * The guard on Plr_SetBladeSize asks whether the mesh under the blade is borrowed. A far body's
 * resize arrives with the far body's record in the player's block, at the same block address, so
 * only the BODY in the block tells the two apart. A body that is not the swapped one is the
 * engine's business and nothing is taken down for it; the swapped body declines while its swap is
 * live, and a swap on it that is no longer live is taken down.
 * ============================================================================================ */
typedef enum body_guard {
    BODY_GUARD_ENGINE = 0,    /* let the resize through                                 */
    BODY_GUARD_DECLINE,       /* the mesh is borrowed: refuse the resize                */
    BODY_GUARD_TAKE_DOWN      /* the swap on this body ended: take it down, then let it */
} body_guard_t;

body_guard_t character_bodies_guard(bool borrowed, bool body_read, uintptr_t body_in_block,
                                    uintptr_t swapped_body, bool swap_live);

/* ==============================================================================================
 * The far path's decision for one bank, before anything is loaded.
 *
 * The facts are gathered in the order they are listed, and a fact past the first one that fails
 * may be left false: the answer depends only on the first that fails.
 * ============================================================================================ */
typedef struct far_facts {
    bool asked;               /* the bank has a body: a serial and an object              */
    bool wants;               /* and names a model                                        */
    bool entry;               /* a row stands for the bank's body, found alive this pass  */
    bool entry_is_wish;       /* and it was dressed with the model the bank names now     */
    bool answered;            /* this serial and this model were answered for good before */
    bool block_sound;         /* the block may be read and written as the contract says   */
    bool able;                /* sites, guard and translation stand                       */
    bool jedi;                /* block+0x6C is 0 or 1                                     */
    bool blade_seen;          /* the blade guard has been asked at least once             */
    bool window_shut;         /* no bank window is open at this scene end                 */
    bool block_names_body;    /* block+0x0C is the object the bank names                  */
    bool fresh;               /* the body wears its own actor's model                     */
} far_facts_t;

typedef enum far_verdict {
    FAR_NONE = 0,             /* nothing asked: the answer says so                        */
    FAR_KEEP,                 /* the answer given before for this serial and model stands */
    FAR_WORN,                 /* the row wears the model: WORN, echo the row's model      */
    FAR_REFUSE,               /* REFUSED for `reason`                                     */
    FAR_WAIT,                 /* no answer yet; asked again at the next scene end         */
    FAR_LOAD                  /* nothing stands in the way: load the asset and go on      */
} far_verdict_t;

typedef struct far_step {
    far_verdict_t verdict;
    uint8_t       reason;     /* MODEL_WEAR_REASON_* for FAR_REFUSE */
} far_step_t;

far_step_t character_bodies_far_step(const far_facts_t *facts);

/* Whether an answer holds for as long as the serial and the model do. Two refusals do not: the
 * blade guard may be asked later, and a row may come free. */
bool       character_bodies_far_final(uint8_t state, uint8_t reason);

/* What a refusal is called in the log. */
const char *character_bodies_reason_text(uint8_t reason);

#endif /* CHARACTER_BODIES_H */
