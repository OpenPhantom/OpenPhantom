/* mp_enemy_nodes.c: the engine half of the hidden nodes, on an actor, a body, a thing and a model
 * made up in this process's memory.
 *
 * Every offset is the one the module reads: the actor's state flags at +0x14, the thing at
 * body+0x9C, the model at thing+0x04, the node array at thing+0x28 and the mesh array at +0x2C,
 * the node count at model+0x54. Both arrays carry canaries past their end, so a write past them is
 * seen, and an entry of 2 stands for a local hidden flag the module has no business rewriting.
 */
#include "unittest.h"

#include "mp_enemy_blast.h"
#include "mp_enemy_nodes.h"
#include "mp_enemy_sync.h"
#include "mp_enemy_sync_internal.h"
#include "mp_enemy_wire.h"

#include <stdbool.h>
#include <stdint.h>
#include <string.h>

#define ENTRIES 72u
#define CANARY  0x5A5A5A5A

typedef struct rig {
    int32_t nodes[ENTRIES + 4u];
    int32_t meshes[ENTRIES + 4u];
    uint8_t model[0x60];
    uint8_t thing[0x30];
    uint8_t body[0xA0];
    uint8_t actor[0x40];
} rig_t;

static void put32(uint8_t *at, uint32_t value)
{
    memcpy(at, &value, sizeof value);
}

static void build(rig_t *r, uint32_t count)
{
    uint32_t i;

    memset(r, 0, sizeof *r);
    for (i = count; i < ENTRIES + 4u; ++i) {
        r->nodes[i]  = CANARY;
        r->meshes[i] = CANARY;
    }
    put32(r->model + 0x54, count);
    put32(r->thing + 0x04, (uint32_t)(uintptr_t)r->model);
    put32(r->thing + 0x28, (uint32_t)(uintptr_t)r->nodes);
    put32(r->thing + 0x2C, (uint32_t)(uintptr_t)r->meshes);
    put32(r->body + 0x9C, (uint32_t)(uintptr_t)r->thing);
}

static uint32_t body_of(rig_t *r)
{
    return (uint32_t)(uintptr_t)r->body;
}

static uintptr_t actor_of(rig_t *r)
{
    return (uintptr_t)r->actor;
}

static mp_enemy_record_t with_state(uint32_t state)
{
    mp_enemy_record_t record;

    memset(&record, 0, sizeof record);
    record.value[MP_ENEMY_F_STATE] = state;
    return record;
}

static void check_the_host_reads(void)
{
    static rig_t      r;
    mp_enemy_record_t record;

    ut_section("the host reads both arrays in the save's form, each with its presence bit");

    build(&r, 48u);
    mp_enemy_nodes_reset();
    r.nodes[4]   = 1;
    r.nodes[40]  = 1;
    r.meshes[10] = 1;
    record = with_state(1u);
    record.value[MP_ENEMY_F_NODES_LO] = 0xFFFFFFFFu;   /* whatever was there is replaced */
    mp_enemy_nodes_read(actor_of(&r), body_of(&r), 14u, &record);
    ut_checkf(record.value[MP_ENEMY_F_NODES_LO] == 0x10u &&
                  record.value[MP_ENEMY_F_NODES_HI] == 0x100u &&
                  record.value[MP_ENEMY_F_MESHES_LO] == 0x400u &&
                  record.value[MP_ENEMY_F_MESHES_HI] == 0u,
              "nodes 4 and 40 and mesh 10 are read (%08X %08X %08X %08X)",
              (unsigned)record.value[MP_ENEMY_F_NODES_LO],
              (unsigned)record.value[MP_ENEMY_F_NODES_HI],
              (unsigned)record.value[MP_ENEMY_F_MESHES_LO],
              (unsigned)record.value[MP_ENEMY_F_MESHES_HI]);
    ut_check(mp_enemy_wire_has(&record, MP_ENEMY_HAS_NODES) &&
                 mp_enemy_wire_has(&record, MP_ENEMY_HAS_MESHES) &&
                 (record.value[MP_ENEMY_F_STATE] & MP_ENEMY_STATE_MASK) == 1u,
             "both presence bits are set beside the state, which is untouched");

    put32(r.thing + 0x2C, 0u);
    record = with_state(1u);
    mp_enemy_nodes_read(actor_of(&r), body_of(&r), 14u, &record);
    ut_check(mp_enemy_wire_has(&record, MP_ENEMY_HAS_NODES) &&
                 !mp_enemy_wire_has(&record, MP_ENEMY_HAS_MESHES) &&
                 record.value[MP_ENEMY_F_MESHES_LO] == 0u,
             "a thing without a mesh array says nothing about meshes");

    put32(r.actor + 0x14, 0x2000u);
    record = with_state(1u);
    mp_enemy_nodes_read(actor_of(&r), body_of(&r), 94u, &record);
    ut_check(!mp_enemy_wire_has(&record, MP_ENEMY_HAS_NODES) &&
                 record.value[MP_ENEMY_F_NODES_LO] == 0u,
             "an actor that carries a player's body is left out: the player's code owns its nodes");

    build(&r, 48u);
    put32(r.body + 0x9C, 0u);
    record = with_state(1u);
    mp_enemy_nodes_read(actor_of(&r), body_of(&r), 14u, &record);
    ut_check(!mp_enemy_wire_has(&record, MP_ENEMY_HAS_NODES) &&
                 !mp_enemy_wire_has(&record, MP_ENEMY_HAS_MESHES),
             "a body with no thing says nothing, so no receiver shows what the host hides");

    build(&r, 70u);
    r.nodes[63] = 1;
    r.nodes[65] = 1;
    record = with_state(1u);
    mp_enemy_nodes_read(actor_of(&r), body_of(&r), 15u, &record);
    ut_checkf(record.value[MP_ENEMY_F_NODES_HI] == 0x80000000u,
              "a model past 64 nodes sends its first 64, as the engine's save does (%08X)",
              (unsigned)record.value[MP_ENEMY_F_NODES_HI]);
}

static void check_the_client_follows(void)
{
    static rig_t      r;
    mp_enemy_record_t record;
    uint32_t          i;

    ut_section("a client writes only what differs, 1 and 0 as the engine does, and never past");

    build(&r, 48u);
    r.nodes[2] = 2;   /* hidden here by a writer of its own, and hidden on the host too */
    r.nodes[5] = 1;   /* hidden here and shown on the host */
    record = with_state(1u | MP_ENEMY_HAS_NODES | MP_ENEMY_HAS_MESHES);
    record.value[MP_ENEMY_F_NODES_LO]  = (1u << 2) | (1u << 7);
    record.value[MP_ENEMY_F_NODES_HI]  = 1u << 15;   /* node 47 */
    record.value[MP_ENEMY_F_MESHES_LO] = 1u << 9;
    mp_enemy_nodes_apply(actor_of(&r), body_of(&r), 14u, &record, NULL);
    ut_checkf(r.nodes[2] == 2 && r.nodes[5] == 0 && r.nodes[7] == 1 && r.nodes[47] == 1 &&
                  r.meshes[9] == 1,
              "2 left as it was, 5 shown, 7 and 47 and mesh 9 hidden (%d %d %d %d %d)",
              (int)r.nodes[2], (int)r.nodes[5], (int)r.nodes[7], (int)r.nodes[47],
              (int)r.meshes[9]);
    for (i = 48u; i < ENTRIES + 4u; ++i) {
        if (r.nodes[i] != CANARY || r.meshes[i] != CANARY) {
            break;
        }
    }
    ut_check(i == ENTRIES + 4u, "nothing past the node count was written in either array");

    build(&r, 40u);
    r.nodes[3] = 1;
    mp_enemy_nodes_apply(actor_of(&r), body_of(&r), 14u, &record, NULL);
    ut_check(r.nodes[3] == 1 && r.nodes[7] == 0 && r.meshes[9] == 0,
             "a mask past this model's node count is another rig, and nothing is written");

    build(&r, 48u);
    r.nodes[3] = 1;
    record = with_state(1u);
    record.value[MP_ENEMY_F_NODES_LO] = 0u;
    mp_enemy_nodes_apply(actor_of(&r), body_of(&r), 14u, &record, NULL);
    ut_check(r.nodes[3] == 1,
             "four words of zero without a presence bit say nothing, and a hidden node stays so");

    record = with_state(1u | MP_ENEMY_HAS_MESHES);
    mp_enemy_nodes_apply(actor_of(&r), body_of(&r), 14u, &record, NULL);
    ut_check(r.nodes[3] == 1, "and a record that speaks for the meshes alone leaves the nodes");
}

static void check_the_replica_line(void)
{
    static rig_t      r;
    mp_enemy_record_t record;

    ut_section("the replica line names the masks here and on the host");

    build(&r, 48u);
    r.nodes[4] = 1;
    ut_check(strcmp(mp_enemy_nodes_describe(body_of(&r)),
                    "nodes 0000000000000010 and meshes 0000000000000000 here") == 0,
             "this body's masks, high word first");
    record = with_state(1u);
    ut_check(strcmp(mp_enemy_nodes_said(&record), "not said by the host") == 0,
             "a record without a presence bit is not said");
    record = with_state(1u | MP_ENEMY_HAS_NODES);
    record.value[MP_ENEMY_F_NODES_HI] = 1u;
    ut_check(strcmp(mp_enemy_nodes_said(&record),
                    "nodes 0000000100000000 and meshes 0000000000000000 on the host") == 0,
             "and one with it names the host's words");
}

/* The two hands this building block gives the director's hull: a host, and a side with no session,
 * always let the engine's arm run. On a client of a session only a replica the host describes is
 * withheld, and an actor the mirror holds under no key is no replica. */
static void check_the_hands(void)
{
    static rig_t r;

    ut_section("the director's weapon commands and its blast, by side");

    build(&r, 48u);
    ut_check(!mp_enemy_nodes_director(actor_of(&r), false),
             "a host or a side alone leaves commands 8 and 9 to the engine");
    ut_check(!mp_enemy_blast_director(actor_of(&r), false),
             "and command 19, which a host that describes nobody does not even keep");
    ut_check(!mp_enemy_nodes_director(actor_of(&r), true) &&
                 !mp_enemy_blast_director(actor_of(&r), true),
             "a client withholds nothing of an actor that is no replica of the host's");
    ut_check(!mp_enemy_nodes_director(0u, true) && !mp_enemy_blast_director(0u, true),
             "and nothing of no actor at all");

    /* The replica of a life the host described, as the mirror holds it: the one question every
     * script output of a client asks, so the same actor gets the same answer from 0x603. */
    {
        enemy_sync_state_t *s = mp_enemy_sync_state();
        placement_t        *p = &s->placement[14u];

        mp_enemy_sync_reset();
        p->actor      = actor_of(&r);
        p->live       = true;
        p->known      = true;
        p->generation = 1u;
        ut_check(mp_enemy_nodes_director(actor_of(&r), true) &&
                     mp_enemy_blast_director(actor_of(&r), true),
                 "a client withholds commands 8, 9 and 19 of the replica of a life the host "
                 "described");
        ut_check(!mp_enemy_nodes_director(actor_of(&r), false),
                 "a host lets them run and reads what they did");
        p->known      = false;
        p->generation = 0u;
        ut_check(!mp_enemy_nodes_director(actor_of(&r), true) &&
                     !mp_enemy_blast_director(actor_of(&r), true),
                 "an actor under a key the host never described runs its own, as its sounds do; "
                 "the old question withheld them for any census actor of a key");
        mp_enemy_sync_reset();
    }
}

int main(void)
{
    check_the_host_reads();
    check_the_client_follows();
    check_the_replica_line();
    check_the_hands();
    return ut_summary("mp_enemy_nodes");
}
