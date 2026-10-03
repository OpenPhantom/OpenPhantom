/* mp_body_internal.h: what the two halves of the far body module share.
 *
 * The module was one file until the far bodies learned to wear the far player's own actor. The
 * seam is the one its own size note had already named and measured: the build, which reads the
 * player's block once and never again, on one side; the contact dispatcher, the shot hull, the
 * tick and the collision restore, which run every substep and never read that block, on the
 * other. Nothing here is public: mp_body.h stays the interface, and this header exists so the two
 * translation units agree about one record rather than about a copy of it.
 *
 * The record itself lives in mp_body.c and is reached through mp_body_far_at, because a mutable
 * global in a header is what this tree forbids and because "which indices are banks" is one
 * answer in one place. The wear tick reads it as well: the serial, the slot and the size wish
 * are the body's, and a copy of them in that module would be a second answer to one question.
 */
#ifndef MULTIPLAYER_MP_BODY_INTERNAL_H
#define MULTIPLAYER_MP_BODY_INTERNAL_H

#include "mp_cells.h"

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

/* How many bytes of actor name a far body carries. The same 32 as the actor header's own field
 * and as the wire's asset field, so a name crosses all three without a second rule. */
#define MP_BODY_ASSET_MAX MP_ACTOR_NAME_BYTES

/* Fields of the hero block and the body object, all byte-confirmed against player_spawnHero. The
 * body handle the spawn stores is a raw bapObj pointer, not a handle, so its class fields are
 * reachable directly. */
#define HERO_BLOCK_HACTOR      0x0Cu   /* the raw bapObj pointer the spawn allocates */
#define HERO_BLOCK_HERO_INDEX  0x6Cu   /* which hero the player is, and its status index */
#define HERO_BLOCK_POS         0x118u  /* three floats */
#define HERO_BLOCK_HEADING     0x2A0u  /* one float */
#define BAPOBJ_OBJ_CLASS       0x04u
#define BAPOBJ_SHOOTER_CLASS   0x08u   /* the side a body and a shot carry */
#define BAPOBJ_POS             0x18u   /* three floats; the tick normally seeds this from the
                                        * block */
#define BAPOBJ_PREV_POS        0x54u   /* three floats; must equal pos or the interpolation
                                        * shivers */
#define BAPOBJ_SCALE           0x30u   /* three floats, the size the object is drawn at. The
                                        * engine's own bapobj_setScale stores these three and
                                        * the render handle's culling radius at +0x154 beside
                                        * them, so a size goes through that setter and these are
                                        * only read, to leave a size that already stands */
#define BAPOBJ_CYLINDER_RADIUS 0xB8u   /* one float, copied unscaled from the actor at the bind */
#define BAPOBJ_CYLINDER_HEIGHT 0xBCu   /* one float; both are zeroed with the class by a death
                                        * clip */
#define BAPOBJ_HANDLER_TASK    0xA4u   /* the node a contact on this body is delivered to; the
                                        * spawn writes the player's node here */

/* One far body: what its bank's spawn left behind and what its ticks and deaths have done since.
 *
 * `hero` and `slot` were one field and had to become two the moment a body could wear something
 * other than a shipped hero. `hero` is what the far player chose and is what the wire compares
 * against; `slot` is the entry of the hero table the body actually rides, which for a foreign
 * asset is the borrowed one and has nothing to do with the far player's choice. Everything that
 * hangs off the ENGINE's idea of a hero, the sabre arm above all, hangs off `slot`. */
typedef struct mp_body_far {
    bool     spawned;
    /* Which body this is, counted at every spawn and every take down that succeeded. The object
     * address cannot tell: the engine's object list hands back the lowest free slot, so a body
     * built again in the same call sits where the last one sat. */
    uint32_t serial;
    /* Whether a far player occupies this bank right now. A bank nobody occupies gets no body, and
     * a body whose bank empties is taken down. It is seeded true at the install, so the local
     * provocation that spawns a second body with no wire under it behaves as it always did; a
     * session says otherwise once it knows. */
    bool     occupied;
    bool     failed;            /* three refused builds in a row: the body rests until asked
                                 * again */
    int32_t  hero;              /* the hero the far player chose, or the local one before any
                                 * wire */
    int32_t  slot;              /* the hero table entry the body rides, -1 before it stands */
    char     asset[MP_BODY_ASSET_MAX];   /* the actor ASKED for, empty for the hero's own */
    bool     wearing_asset;     /* the asked-for actor is what the body was really built from */
    /* How big the far player asked to be drawn, 1.0 being the size of the asset the body shows
     * and 0 before anybody asked. A wish that outlives the body: a rebuilt body comes back at its
     * own size, and the wear tick puts the product on again by comparing it with the object. */
    float    scale_wanted;
    bool     death_reported;    /* the stop on a dead body is logged once */
    uint32_t ticks;             /* substeps it has been ticked through the pipeline */
    uint32_t tick_faults;       /* a tick that hit a memory fault and left bank 0 restored */
    bool     cylinder_saved;    /* the two words below were read off the object at the spawn */
    uint32_t cylinder_radius_bits;
    uint32_t cylinder_height_bits;
    uint32_t collision_restores;
    uint32_t restore_faults;
    bool     restore_logged;
    bool     restore_fault_logged;
    bool     health_seen;
    int32_t  health_known;
    uint32_t health_changes;
    /* The object a scene made passable, nought for none, and the far player's dead edge as the
     * puppet last took it. The mark is the object's and goes with the body; the edge is the far
     * player's and outlives a rebuild, as the puppet's own does. */
    uint32_t passable_object;
    bool     lies_dead;

    /* The appearance the far side asked for, and when it asked. A wish rather than an immediate
     * rebuild because a rebuild may only run in the task slot with no bank window open, and the
     * caller that learns of the change is not always standing there. */
    bool     wish;
    int32_t  wish_hero;
    char     wish_asset[MP_BODY_ASSET_MAX];
    uint32_t wish_age;          /* substeps the wish has waited */
    uint32_t rebuilds;
    uint32_t rebuild_refusals;
    uint32_t spawn_attempts;    /* refused builds since the last one that stood */
} mp_body_far_t;

/* Bank `index`'s record, or NULL for an index that is no far bank. */
mp_body_far_t *mp_body_far_at(size_t index);

/* Whether the module installed, which every door has to test before it touches the engine. */
bool mp_body_module_ready(void);

/* Clear the module state a rebuilt body must not inherit: the collision the old model was bound
 * with, the death report latch, and the health the old body was last seen at. Lives with the
 * record rather than with the build, because the same fields are what the tick and the restore
 * read. */
void mp_body_forget_body_state(mp_body_far_t *far);

/* Put bank `index`'s body `object` back to its collision after a death clip took it away; the
 * caller names the object it read, so nothing here can fail to read it. The death clip's
 * authored event makes the draw pass zero the object's class and both cylinder words, after which
 * the pair pass never sees the body again; the far player's respawn gives him a new body on his
 * machine and the puppet has none, so this writes the co-op classes and the cylinder saved at the
 * spawn back onto the same object. Checked writes, counted, logged once; false when nothing was
 * saved or a write refused. Only the owner of the far bodies' scene collision calls it. */
bool mp_body_collision_restore_at(size_t index, uint32_t object);

/* The far bodies made passable for a scene, which live in mp_body_passable.c. The class a body
 * built now is stamped with: 0 while a scene wants the far bodies passable and the far player
 * lives, which also marks `object` as made passable; the bank's own class otherwise. The side is
 * the bank's either way. */
int32_t mp_body_collision_class_at(size_t index, uint32_t object);

/* A contact the dispatcher delivers on `object`: counted there when it is a body made passable,
 * because no engine test should post one. And that half's report line. */
void mp_body_passable_note_contact(uint32_t object);
void mp_body_passable_report(void);

/* The shot hull, which lives in mp_body_shot.c. The install places it on the shot_spawn site it
 * resolved and learns whether it took; the report reads the shots left without their side and
 * lets the hull say its own line. */
bool     mp_body_shot_install(uintptr_t site, size_t prologue);
uint32_t mp_body_shot_side_faults(void);
void     mp_body_shot_report(void);

/* The dispatcher, for the gate in mp_body_gate.c: its address, which a far body's own node holds
 * in its slot; whether a contact delivery is running through it right now; whether it has a
 * handler to pass on to, and that handler run once directly, which is the way around the engine's
 * gate a build without task_run is left with. */
uintptr_t mp_body_dispatcher_address(void);
bool      mp_body_dispatching(void);
bool      mp_body_handler_known(void);
bool      mp_body_call_the_handler(void);

#endif /* MULTIPLAYER_MP_BODY_INTERNAL_H */
