/* mp_body_gate.c: the engine's own gate in front of a body's contact handler. See the header.
 *
 * Everything here runs on the scheduler's thread: the hit the host performed from the substep
 * task, the spawn from the same task slot, the dispatcher from inside a contact delivery.
 */
#include "mp_body_gate.h"

#include "mp_body.h"
#include "mp_body_internal.h"

#include "mp_bank.h"
#include "mp_cells.h"
#include "mp_signatures_contact.h"

#include "common/logging.h"
#include "common/memory.h"
#include "common/patch.h"

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

/* The engine's task record, as far as a contact delivery reads it: 0x2C bytes, the contact slot
 * at +0x18, the wake flag pointer at +0x1C and the wake value at +0x20, the countdown at +0x24
 * that task_run writes when a handler asks to run again next substep. A node of this feature's
 * keeps everything but the slot at nought, so its wake flag is never written through. */
#define TASK_NODE_WORDS         11u
#define TASK_NODE_CONTACT_WORD  (0x18u / 4u)
#define TASK_NODE_CONTACT_SLOT  0x18u

/* task_run takes the node and answers its handler's status word with the reschedule bit taken
 * out; cdecl, one argument, the caller cleans up. */
typedef uint32_t(__cdecl *task_run_fn_t)(uint32_t node);

typedef struct own_node {
    uint32_t words[TASK_NODE_WORDS];
} own_node_t;

_Static_assert(sizeof(own_node_t) == 0x2Cu, "a task node is the engine's 0x2C byte record");

typedef struct gate_state {
    bool          own_nodes;        /* a session: the far bodies take a node of their own */
    task_run_fn_t task_run;         /* resolved when the switch goes on */
    bool          performing;       /* a hit the host judged is being performed right now */

    own_node_t    nodes[MP_BANK_FAR_MAX];

    mp_body_gate_life_fn_t lives;   /* whether this machine's player lives, or NULL */

    uint32_t      delivered;        /* hits the host performed, through task_run */
    uint32_t      met_empty;        /* and the ones that met an empty slot */
    uint32_t      met_empty_living; /* of those, on a living body: nothing could hurt it */
    uint32_t      around;           /* delivered around the gate: task_run did not resolve */
    uint32_t      nowhere;          /* nothing to deliver to: no slot, no handler */

    uint32_t      nodes_given;
    uint32_t      nodes_refused;
    uint32_t      far_while_empty;  /* contacts on a far body with this machine's slot empty */
    uint32_t      slot_kept;        /* far body builds across which the player's slot was kept */
    uint32_t      slot_kept_empty;  /* of those, with the slot empty: a local corpse */
    uint32_t      slot_faults;      /* a slot that could not be written back */
} gate_state_t;

static gate_state_t gate;

mp_body_delivery_t mp_body_gate_verdict(bool slot_known, uint32_t slot, bool task_run_known,
                                        bool handler_known)
{
    if (!slot_known) {
        return MP_BODY_DELIVERY_NOWHERE;
    }
    if (slot == 0u) {
        return MP_BODY_DELIVERY_EMPTY_SLOT;
    }
    if (task_run_known) {
        return MP_BODY_DELIVERY_THROUGH_TASK_RUN;
    }
    return handler_known ? MP_BODY_DELIVERY_AROUND : MP_BODY_DELIVERY_NOWHERE;
}

/* The player's node, and the word in its contact slot. False when either does not read. */
static bool read_local_slot(uint32_t *node, uint32_t *slot)
{
    uintptr_t cell = mp_cells_address(MP_CELL_PLAYER_TASK_NODE);

    *node = 0u;
    *slot = 0u;
    return cell != 0u && memory_try_read_u32(cell, node) && *node != 0u &&
           memory_try_read_u32((uintptr_t)*node + TASK_NODE_CONTACT_SLOT, slot);
}

bool mp_body_run_engine_contact(void)
{
    uint32_t node = 0u;
    uint32_t slot = 0u;
    bool     known = read_local_slot(&node, &slot);
    bool     done;

    switch (mp_body_gate_verdict(known, slot, gate.task_run != NULL, mp_body_handler_known())) {
    case MP_BODY_DELIVERY_THROUGH_TASK_RUN:
        gate.performing = true;
        (void)gate.task_run(node);
        gate.performing = false;
        ++gate.delivered;
        return true;
    case MP_BODY_DELIVERY_EMPTY_SLOT:
        /* The engine empties the slot for a death and for a re-entry and fills it again at the
         * spawn, so an empty slot on a living body is a body nothing can hurt: counted apart. */
        ++gate.met_empty;
        gate.met_empty_living += (gate.lives != NULL && gate.lives()) ? 1u : 0u;
        return false;
    case MP_BODY_DELIVERY_AROUND:
        gate.performing = true;
        done = mp_body_call_the_handler();
        gate.performing = false;
        gate.around += done ? 1u : 0u;
        return done;
    case MP_BODY_DELIVERY_NOWHERE:
    default:
        ++gate.nowhere;
        return false;
    }
}

void mp_body_gate_view(mp_body_gate_view_t *out)
{
    uintptr_t task_cell = mp_cells_address(MP_CELL_TASK_SERVICE);
    uint32_t  node = 0u;
    uint32_t  slot = 0u;
    uint32_t  running = 0u;
    bool      known = read_local_slot(&node, &slot);

    if (out == NULL) {
        return;
    }
    if (!known) {
        out->slot = MP_BODY_SLOT_UNREAD;
    } else if (slot == 0u) {
        out->slot = MP_BODY_SLOT_EMPTY;
    } else if ((uintptr_t)slot == mp_body_dispatcher_address()) {
        out->slot = MP_BODY_SLOT_DISPATCHER;
    } else {
        out->slot = MP_BODY_SLOT_ENGINE;
    }
    out->task_known     = node != 0u && task_cell != 0u && memory_read_u32(task_cell, &running);
    out->task_is_player = out->task_known && running == node;
    if (gate.performing) {
        out->source = MP_BODY_SOURCE_HOST_HIT;
    } else if (mp_body_dispatching()) {
        out->source = MP_BODY_SOURCE_CONTACT;
    } else {
        out->source = MP_BODY_SOURCE_PHASES;
    }
}

void mp_body_gate_set_own_nodes(bool on)
{
    gate.own_nodes = on;
    if (on && gate.task_run == NULL) {
        gate.task_run =
            (task_run_fn_t)mp_signatures_contact_address(MP_CONTACT_SITE_TASK_RUN);
        if (gate.task_run == NULL) {
            log_warning("the engine's own task run did not resolve, so a hit the host performs "
                        "here calls the handler directly and walks past a corpse's empty slot; "
                        "the death entry's own watch is then the only net");
        }
    }
}

void mp_body_gate_give_node(size_t index, uint32_t object)
{
    own_node_t *node;

    if (!gate.own_nodes || !mp_bank_index_ok(index) || object == 0u) {
        return;
    }
    node = &gate.nodes[index - 1u];
    node->words[TASK_NODE_CONTACT_WORD] = (uint32_t)mp_body_dispatcher_address();
    if (patch_write_u32(object + BAPOBJ_HANDLER_TASK, (uint32_t)(uintptr_t)node) !=
        PATCH_RESULT_OK) {
        ++gate.nodes_refused;
        return;
    }
    ++gate.nodes_given;
}

bool mp_body_gate_keep_local_slot(uint32_t *slot)
{
    uint32_t node = 0u;

    return slot != NULL && read_local_slot(&node, slot);
}

void mp_body_gate_put_back_local_slot(uint32_t slot)
{
    uint32_t node = 0u;
    uint32_t now = 0u;

    if (!read_local_slot(&node, &now) ||
        (now != slot &&
         patch_write_u32((uintptr_t)node + TASK_NODE_CONTACT_SLOT, slot) != PATCH_RESULT_OK)) {
        ++gate.slot_faults;
        return;
    }
    ++gate.slot_kept;
    gate.slot_kept_empty += slot == 0u ? 1u : 0u;
}

void mp_body_gate_note_far_contact(void)
{
    uint32_t node = 0u;
    uint32_t slot = 0u;

    if (read_local_slot(&node, &slot) && slot == 0u) {
        ++gate.far_while_empty;
    }
}

void mp_body_gate_set_life_test(mp_body_gate_life_fn_t lives)
{
    gate.lives = lives;
}

void mp_body_gate_report(void)
{
    log_info("  the hits the host performed on this player: %u delivered through the engine's own "
             "task run, %u met an empty contact slot and did nothing, %u of them on a living body "
             "(must be 0) and the rest on a corpse or a body on its way back, %u delivered around "
             "it (must be 0); %u with no slot or handler to deliver to (must be 0)",
             (unsigned)gate.delivered, (unsigned)gate.met_empty, (unsigned)gate.met_empty_living,
             (unsigned)gate.around, (unsigned)gate.nowhere);
    log_info("  the far bodies' own contact nodes: %u body(s) given one, %u write(s) refused; %u "
             "contact(s) on a far body while this machine's own slot was empty; this machine's "
             "own slot kept across %u build(s) of a far body, %u of them while it was empty, %u "
             "that could not be read or written back",
             (unsigned)gate.nodes_given, (unsigned)gate.nodes_refused,
             (unsigned)gate.far_while_empty, (unsigned)gate.slot_kept,
             (unsigned)gate.slot_kept_empty, (unsigned)gate.slot_faults);
}
