/* character_prop_body.h: the bodies a borrowed weapon is drawn on, and how long each one lasts.
 *
 * The weapon a borrowed body carries is a second render handle bound to the model the weapon
 * meshes live in, drawn once per frame at the borrowed rig's hand. Everything that describes ONE
 * such body is a row here: the handle, the reference rig it was measured against, the body it is
 * hung on, the hand it is hung at, and the sphere the last draw left behind.
 *
 * The player's own body is the row of bank 0. A far player's body is a row of its own bank, and
 * the table holds as many as the note between the two DLLs can describe, so the two searches below
 * are the only way anything finds a body: the draw is dispatched per render handle and the sphere
 * and the node lookup are asked per object, and neither of those carries a bank.
 *
 * The module is two files and this is the header between them. character_prop_body.c owns the
 * rows, their lifetime and the block a row is read through, and reads no model. character_prop.c
 * owns what a rig says and what the engine is answered with, and takes a row as an argument. The
 * three declarations at the end are that file's half of the seam, so an arming that has to bind a
 * handle or give a rig its own weapon words back can ask for it without owning it.
 *
 * The seam is a row pointer, which is why it is a seam at all: the fields that made an earlier cut
 * impossible, the shown parts and the visibility state, are written and read inside one frame by
 * both halves, and as fields of a row that is handed over they belong to neither file.
 */
#ifndef CHARACTER_PROP_BODY_H
#define CHARACTER_PROP_BODY_H

#include "character_bodies.h"
#include "character_prop_rig.h"
#include "character_prop_sites.h"

#include <stdbool.h>
#include <stdint.h>

/* A drawn weapon is more than one mesh, and answering for the node the weapon table names alone
 * put the contact point in the fist while the blade did the swinging. The equipped node's whole
 * subtree is shown, so the engine's one sphere per node has to be the sphere containing all of
 * them. A gun is a single mesh with no children and its answer is unchanged.
 *
 * The widest weapon subtree in the shipped assets is seven meshes, so the cap has one to spare. A
 * subtree past it is measured from the parts that fit, which is a smaller sphere, never a wrong
 * one, and never an overrun. */
#define PROP_PARTS_MAX          8u

/* A borrowed rig may carry weapon geometry of its own, and it would then be worn beside the
 * player's. Those words live in the render handle rather than in the model, so they can be taken
 * back.
 *
 * One word hides a whole subtree, because thing+0x28 gates the concatenation and the draw walk
 * before either descends into a child, so a mount covers its entire gun rack in one slot. Measured
 * over the 132 shipped rigs that carry a hand, the longest list is two. The room here is for the
 * case where the mount could not be named out of the engine's table and its children are therefore
 * collected one at a time: the widest of those is `mace`, at 27.
 *
 * A lent list is shorter and this is not widened for it. The far path collects its own words with
 * BODY_HIDE_MAX, which is 8, and it always names the mount, so that walk stops at the mount and
 * the longest list measured over the rigs that carry a hand is two. A rig with nine separate
 * weapon subtrees would have its list cut short by the far path before this file saw it, and the
 * effect of that is a rig wearing one of its own weapons beside the player's, never a write
 * outside a table. */
#define PROP_HIDE_MAX          32u

/* The player's own body. Every other row is a far player's bank. */
#define PROP_BANK_LOCAL         0u

/* A blade is four vertices of three floats, which is the 0x30 byte buffer the engine's own length
 * setter pushes onto its stack and hands to the mesh writer. */
#define PROP_BLADE_FLOATS      12u

/* No weapon is shown. The engine's own name table has 33 entries, so this is not one of them. */
#define PROP_NAME_ID_NONE      0xFFFFFFFFu

/* The two engine fields a row is checked against: the body a player block names, and the render
 * handle an object owns. */
#define PROP_BLOCK_ACTOR     0x0Cu
#define PROP_OBJ_THING       0x9Cu

/* One node of the drawn weapon, kept so that the sphere the engine asks for can be built from every
 * mesh that is visible rather than from the one the weapon table names. Mesh local, because that is
 * the space the engine's own query reads the centre in; the joints come later. */
typedef struct prop_part {
    uint32_t slot;             /* the joint the mesh rides on                     */
    float    centre[3];        /* mesh+0x58, mesh local, as the engine reads it   */
    float    radius;           /* mesh+0x54                                       */
} prop_part_t;

/* Where the words that hide the borrowed rig's own weapon come from, because one visibility word
 * with two writers is how a rig ends up with half of itself invisible. A row that collected them
 * writes them per frame and gives them back; a row that was lent them writes none of them. */
typedef enum prop_hidden {
    PROP_HIDDEN_COLLECTED = 0,
    PROP_HIDDEN_LENT
} prop_hidden_t;

typedef struct prop_body {
    bool      used;               /* a row of the table                                       */
    bool      armed;              /* a body is being carried on right now                     */
    bool      found_said;         /* a found node replaced, said once per model worn          */
    bool      older_said;         /* answered out of an older pass, said once per model worn  */
    uint8_t   bank;               /* PROP_BANK_LOCAL, or a far player's bank                  */

    /* The second render handle. Ours, so it has no level place and no lifetime question. */
    uint8_t   handle[RDTHING_BLOCK_BYTES];
    uintptr_t reference_model;    /* where the drawn weapon's meshes live                     */
    uint32_t  reference_nodes;
    uint32_t  hand_slot;          /* `rhand` in the reference model                           */
    float     hand_inverse[PROP_MATRIX_FLOATS];
    float     hand_pivot[3];      /* node+0x60 of that hand                                   */

    uintptr_t record;             /* the CELL holding the live player block                   */
    uintptr_t block;              /* a far body's own block; 0 when the cell is the answer     */
    uintptr_t obj;
    uintptr_t thing;

    uintptr_t target_model;       /* what the body wears, as last seen                        */
    uint32_t  target_hand_slot;
    uintptr_t target_hand_node;   /* the record, for the chain that may not hide              */
    float     hand_to_hand[PROP_MATRIX_FLOATS];

    prop_hidden_t hidden_from;
    uint32_t      hidden[PROP_HIDE_MAX];
    uint32_t      hidden_count;

    /* The row's own blade, and the one field of a shared mesh this feature ever writes. The
     * mesh is the reference model's, the vertices are the row's, and the pointer at mesh+0x30
     * names them for the length of one draw and is put back inside the same call. */
    uintptr_t blade_mesh;         /* the blade mesh set of the reference model, 0 for none     */
    uint32_t  blade_saved;        /* what mesh+0x30 holds while this row's draw is open        */
    bool      blade_open;
    float     blade_vert[PROP_BLADE_FLOATS];

    uint32_t    shown_name_id;    /* what the visibility table currently says                 */
    prop_part_t shown_part[PROP_PARTS_MAX];
    uint32_t    shown_part_count;
    float       shown_centre[3];  /* the merged sphere, in WORLD, per pass                    */
    float       shown_radius;
    bool        placed;
    uint32_t    placed_pass;      /* which pass measured that sphere                          */
} prop_body_t;

/* The row of `bank`, taken if the table does not hold it yet. NULL only when every row is taken by
 * another bank. A row that is already held is handed back AS IT IS: the handle on it is bound to a
 * model and the arrays behind it belong to the engine, so whether that binding still serves is the
 * caller's question and never this one's. */
prop_body_t *character_prop_body_hold(uint8_t bank);

/* The row of `bank`, or NULL. Takes nothing. */
prop_body_t *character_prop_body_of_bank(uint8_t bank);

/* What a far player's body is dressed with, as the far path knows it at the moment it finishes
 * dressing one. The reference is that player's own hero model, which is where his weapon meshes
 * live; the block is the one the multiplayer keeps for his bank, and the record is the same cell
 * the engine reads any player block through, because inside a bank window his record is in it.
 *
 * `hidden` is LENT. The far path collected those words and writes them per frame; a row that was
 * lent them writes none of them, because one visibility word with two writers is how a rig ends up
 * with half of itself invisible. They are here so that a weapon name landing on the rig's own
 * hidden blade can be told apart from one landing on something that is drawn. */
typedef struct prop_far {
    uint8_t         bank;           /* 1 .. MODEL_WEAR_BANKS                              */
    uintptr_t       record;         /* the cell holding the live player block             */
    uintptr_t       block;          /* the bank's own block                               */
    uintptr_t       obj;
    uintptr_t       thing;
    uintptr_t       reference;      /* the far player's hero model                        */
    const uint32_t *hidden;         /* the far path's words, lent for reading only        */
    uint32_t        hidden_count;
    uint32_t        serial;         /* the body, for the log                              */
} prop_far_t;

/* A far player's body carries his own weapon from here on. The hand is resolved AT ONCE and not
 * at the first draw: the swing starter asks for it inside a bank window, which can be open before
 * this body is ever drawn, and a body that answered nothing there would swing with no contact.
 * False has said in the log why, once for this body. */
bool character_prop_far_arm(const prop_far_t *far);

/* The far bank's row stops carrying a body and gives its render handle's arrays back. Taking a far
 * body down hands its hero asset back to the resource layer, which may free it, and the arrays
 * behind the handle were sized by that asset. */
void character_prop_far_disarm(uint8_t bank);

/* Whether a far bank's body is carrying its player's weapon RIGHT NOW. This is the table's own
 * word and not a copy kept beside it: arming, a refused arm, a disarm and a body that went all
 * move the same field, so there is one place to read and nothing to keep in step. */
bool character_prop_far_carries(uint8_t bank);

/* What the far weapons came to in the level that ended, said once and then counted from zero.
 * Nothing is said for a level in which no far body carried anything. */
typedef enum prop_tally {
    PROP_TALLY_ARMED = 0,
    PROP_TALLY_REFUSED,
    PROP_TALLY_FROM_DRAW,     /* a far body's draw measured its weapon                   */
    PROP_TALLY_MEASURED,      /* measured for a far body this pass did not draw          */
    PROP_TALLY_OLDER,         /* answered out of the last sphere that body did measure   */
    PROP_TALLY_UNANSWERED,    /* nothing was ever measured, and the engine answered       */
    PROP_TALLY_MAX
} prop_tally_t;

void character_prop_body_tally(prop_tally_t what);
void character_prop_body_report(void);

/* The armed row on this render handle, or on this object, or NULL. These are how the three detours
 * find a body: the draw is dispatched per handle, the sphere and the node lookup are asked per
 * object. A row that is not armed carries nothing and is not answered for. */
prop_body_t *character_prop_body_for_thing(const void *thing);
prop_body_t *character_prop_body_for_obj(const void *obj);

/* How many rows are carrying a body. */
uint32_t character_prop_body_armed(void);

/* The block the body's record is in right now, which is where its equipped weapon slot is read
 * from. The engine keeps one cell pointing at the block of the body it is working on, and the
 * multiplayer swaps a far player's block through that cell for the length of a call.
 *
 * The cell is asked first and that order is the answer, not a preference: while a far body's block
 * is in the cell, the row's own copy of that block is one weapon change behind, because it is read
 * back only when the cell is given up again. A search over two candidates would be free to answer
 * with the older of the two.
 *
 * `body->block` of zero means the cell is this row's record and nothing else can be. That is the
 * player's own body: its block is the one the cell names whenever anything asks on its behalf.
 *
 * False means no block here names this body, and it means EXACTLY THAT. It is not a death
 * certificate: a body whose block is not in the cell right now is answered for by the engine, and
 * a caller that took a false for a reason to put the weapon away would take it away the first time
 * somebody else's call was on the stack. */
bool character_prop_block_of(const prop_body_t *body, uintptr_t *out_block);

/* The body is still the one this row was armed on: a block names it, and it still owns the render
 * handle the row holds. `out_block` is that block, which the caller reads the weapon slot out of.
 */
bool character_prop_body_still_ours(const prop_body_t *body, uintptr_t *out_block);

/* A frame went by, and the next object dispatched opens the pass every sphere is stamped with.
 *
 * The sphere a row answers with is measured during a draw, and a body that was not drawn would
 * otherwise go on answering with the sphere of the frame it was last seen in. What decides when
 * one pass ends and the next begins is the only part with a choice in it, and these two calls are
 * apart because of it:
 *
 *   * a sphere has to stay fresh from the draw that measured it until the draws of the NEXT frame,
 *     because the substeps that ask about it all run in between;
 *   * and a pass may not be opened by each body in turn, or the first body drawn in a frame is
 *     stale by the time the last one has been drawn, which is every frame once there is more than
 *     one row.
 *
 * So the scene end says a frame went by, and the first object dispatched after it opens the pass,
 * for every row at once. */
void character_prop_body_frame_ended(void);
void character_prop_body_begin_pass(void);

/* The weapon sphere this row measured, stamped with the pass that measured it, and whether that
 * stamp is the current one. */
void character_prop_body_place(prop_body_t *body, bool placed);
bool character_prop_body_is_placed(const prop_body_t *body);

/* The borrowed rig's own weapon words, given back while the rig that owns them is still on the
 * handle, and then forgotten. Everything that rebinds the body has to come through here first:
 * after the bind those slots name joints of a model that is no longer there, and the table they
 * would be written into belongs to the new one. */
void character_prop_body_forget(prop_body_t *body);

/* The row stops carrying a body. Says so in the log, once. */
void character_prop_body_disarm(prop_body_t *body);

/* The module's own draw is on the stack, so the dispatch hook it goes through knows to leave it
 * alone. One flag covers the table: draws do not nest across bodies. */
bool character_prop_body_is_drawing(void);
void character_prop_body_drawing(bool drawing);

/* ==============================================================================================
 * character_prop.c's half of the seam: what arming a row needs from the side that reads models.
 * ============================================================================================ */

/* The engine addresses and entry points this feature stands on, resolved on the first ask. */
bool character_prop_sites_ready(void);

/* The entry points once they resolved, or NULL: for a second drawer of a handle of its own, the
 * placement mode's ghost, which draws with the same four calls and resolves nothing again. */
const character_prop_sites_t *character_prop_sites(void);

/* Binds the row's render handle to `reference_model` and measures the rest chain from its root to
 * its right hand. Gives the old binding's arrays back first. False leaves the row with no
 * reference model and has said why. */
bool character_prop_bind_reference(prop_body_t *body, uintptr_t reference_model);

/* Writes `value` into the body's visibility table for every weapon node the borrowed rig carries
 * of its own. Declines when the words were lent, when the handle no longer wears the rig they were
 * resolved against, or for any slot outside that rig's own node count. */
void character_prop_write_own_weapons(prop_body_t *body, uint32_t value);

/* The hand of the rig the body is wearing RIGHT NOW, and the matrix that re-parents the weapon
 * onto it, resolved without waiting for a draw. `weapon` is filled with the engine's own name for
 * whatever the body's block says is equipped, so the one line the far path writes can say what is
 * being carried. False has said in the log why the rig can hold nothing. */
bool character_prop_resolve_hand(prop_body_t *body, char weapon[PROP_NAME_BYTES]);

/* Gives the render handle's arrays back and forgets the reference model, so that the next arm
 * binds a fresh one. */
void character_prop_release_reference(prop_body_t *body);

#endif /* CHARACTER_PROP_BODY_H */
