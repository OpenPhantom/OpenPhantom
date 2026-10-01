/* mp_body_gate.h: the engine's own gate in front of a body's contact handler, asked by every
 * contact this feature delivers or lets through.
 *
 * ================================ The gate the engine has =====================================
 *
 * The engine delivers a contact by running the receiver's handler node through task_run, and
 * task_run refuses a node whose contact slot is empty. The death entry empties the slot of the
 * task being run and the re-entry empties the player's, so a corpse and a body on its way back
 * cannot be hurt, shoved, killed or handed a pickup. That is one of the three reasons the engine
 * enters a death once per life.
 *
 * A hit the host performed on this player used to be delivered by calling the saved handler
 * directly. That walked past the empty slot, and the death entry then emptied the slot of this
 * feature's own task, which is what was being run, so every report of the same fan that followed
 * killed the corpse again. Such a hit now goes through task_run with the player's node, so the
 * slot decides, the death empties the right one, and the dispatcher in the slot sees the contact
 * like any other.
 *
 * ================================ A node for each far body ====================================
 *
 * Every body the engine spawns as a hero carries the player's node as its handler node, the far
 * bodies this feature builds included. While the local player is dead or on his way back that
 * node's slot is empty, and every contact on a far body was dropped at the engine's gate: a client
 * could not be hurt by anything on the host for as long as the host lay dead. A far body in a
 * session is therefore given a node of its own, whose slot holds the dispatcher. task_run reads a
 * node's slot, its wake flag and its wake value and writes its countdown; it does not ask whether
 * the node is scheduled, and nothing in the engine compares a body's node with the player's.
 *
 * The spawn that builds a far body writes the engine's own handler into the player's slot
 * whatever it held, which reopened the gate on a local corpse whenever a far body was built while
 * the local player lay dead. The build keeps the slot and puts it back.
 */
#ifndef MULTIPLAYER_MP_BODY_GATE_H
#define MULTIPLAYER_MP_BODY_GATE_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

/* What a hit the host performed on this player does. */
typedef enum mp_body_delivery {
    MP_BODY_DELIVERY_THROUGH_TASK_RUN,   /* the engine's own delivery; the engine asks the slot */
    MP_BODY_DELIVERY_EMPTY_SLOT,         /* a corpse or a body on its way back: nothing */
    MP_BODY_DELIVERY_AROUND,             /* no task_run on this build: the handler, directly */
    MP_BODY_DELIVERY_NOWHERE             /* nothing to deliver to at all */
} mp_body_delivery_t;

/* The rule, pure. `slot` is the player's contact slot, read only when `slot_known`. An empty slot
 * delivers nothing whichever way is available, because that is what the engine's gate does; the
 * direct call is only the way on a build where task_run did not resolve. */
mp_body_delivery_t mp_body_gate_verdict(bool slot_known, uint32_t slot, bool task_run_known,
                                        bool handler_known);

/* What the contact path looked like at the moment of a death, for the death entry's one line. */
typedef enum mp_body_slot_word {
    MP_BODY_SLOT_DISPATCHER,     /* the slot holds this feature's dispatcher */
    MP_BODY_SLOT_ENGINE,         /* the slot holds a handler of the engine's */
    MP_BODY_SLOT_EMPTY,
    MP_BODY_SLOT_UNREAD
} mp_body_slot_word_t;

typedef enum mp_body_source {
    MP_BODY_SOURCE_HOST_HIT,     /* a hit the host judged, being performed here */
    MP_BODY_SOURCE_CONTACT,      /* a contact the engine delivered here, through the dispatcher */
    MP_BODY_SOURCE_PHASES        /* neither: the engine's own phases */
} mp_body_source_t;

typedef struct mp_body_gate_view {
    mp_body_slot_word_t slot;
    bool                task_known;
    bool                task_is_player;   /* the task being run is the player's node */
    mp_body_source_t    source;
} mp_body_gate_view_t;

void mp_body_gate_view(mp_body_gate_view_t *out);

/* A session's far bodies take a node of their own from now on, and task_run is resolved for the
 * hits the host performs here. Off, nothing is given and a body keeps the player's node. */
void mp_body_gate_set_own_nodes(bool on);

/* The far body of bank `index` has just been built as `object`: its handler node becomes its
 * own, with the dispatcher in the slot. Nothing while the switch above is off. */
void mp_body_gate_give_node(size_t index, uint32_t object);

/* Around the engine's spawn of a far body: the player's contact slot as it stood before, and the
 * same value written back after, so that a corpse's empty slot stays empty. False when the slot
 * could not be read, and then nothing is put back. */
bool mp_body_gate_keep_local_slot(uint32_t *slot);
void mp_body_gate_put_back_local_slot(uint32_t slot);

/* The dispatcher is delivering a contact on a far body. Counted when this machine's own slot is
 * empty at that moment, which before the nodes of their own was a contact the engine dropped. */
void mp_body_gate_note_far_contact(void);

/* Whether this machine's player lives: a running module and no corpse. Handed in by the module
 * that owns that answer, because this one is linked into programs that know nothing of re-entries.
 * Asked only when a hit the host performed meets an empty slot; NULL counts nothing there. */
typedef bool (*mp_body_gate_life_fn_t)(void);
void mp_body_gate_set_life_test(mp_body_gate_life_fn_t lives);

void mp_body_gate_report(void);

#endif /* MULTIPLAYER_MP_BODY_GATE_H */
