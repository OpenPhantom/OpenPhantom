/* npc_spawn_list.c: nodes out of an engine list for the length of one call, and back.
 *
 * A list is built here the way the engine lays one out, a header and nodes of a link word and an
 * element, and the chain is walked the way the engine's own `list_next` walks it. What would be
 * silent if it were wrong: a node put back in the wrong place, which changes the order the enemy
 * tick visits every actor in; two neighbours taken together and put back crossed; a node left
 * outside the chain, which is an actor the engine never ticks or deletes again; and a node given
 * the free mark, which the allocator would hand to the next spawn while it is still alive.
 */
#include "unittest.h"

#include "npc_spawn_list.h"

#include <stdint.h>
#include <string.h>

#define NODES 8u

typedef struct node {
    uint32_t link;
    uint32_t element[7];   /* element[0] is the test's own tag */
} node_t;

static uint32_t s_header[6];
static node_t   s_node[NODES];

static uintptr_t list(void)
{
    return (uintptr_t)s_header;
}

static uint32_t address(uint32_t i)
{
    return (uint32_t)(uintptr_t)&s_node[i];
}

/* A chain of the given node numbers, in that order; every other node free. */
static void build(const uint32_t *order, uint32_t n)
{
    uint32_t i;

    memset(s_header, 0, sizeof s_header);
    for (i = 0; i < NODES; ++i) {
        s_node[i].link       = NPC_SPAWN_LIST_FREE;
        s_node[i].element[0] = 100u + i;
    }
    s_header[1] = n != 0u ? address(order[0]) : 0u;
    for (i = 0; i < n; ++i) {
        s_node[order[i]].link = (i + 1u < n) ? address(order[i + 1u]) : 0u;
    }
}

/* The chain as the engine walks it, as node numbers, into `out`; answers how many. */
static uint32_t walk(uint32_t *out)
{
    uint32_t at = s_header[1];
    uint32_t n  = 0;

    while (at != 0u && n < NODES + 1u) {
        const node_t *node = (const node_t *)(uintptr_t)at;

        out[n++] = (uint32_t)(node - s_node);
        at = node->link;
    }
    return n;
}

static bool chain_is(const uint32_t *expect, uint32_t n)
{
    uint32_t seen[NODES + 1u];

    return walk(seen) == n && memcmp(seen, expect, n * sizeof *seen) == 0;
}

/* Picks the elements whose tag is in the caller's set of node numbers. */
typedef struct pick_set {
    uint32_t count;
    uint32_t node[NODES];
    uint32_t asked;
} pick_set_t;

static bool pick(uintptr_t element, void *user)
{
    pick_set_t     *set = (pick_set_t *)user;
    const uint32_t *tag = (const uint32_t *)element;
    uint32_t        i;

    ++set->asked;
    for (i = 0; i < set->count; ++i) {
        if (*tag == 100u + set->node[i]) {
            return true;
        }
    }
    return false;
}

/* The node numbers a walk visited, in order. */
typedef struct visit_log {
    uint32_t count;
    uint32_t node[NODES + 1u];
} visit_log_t;

static void visit(uintptr_t element, void *user)
{
    visit_log_t    *log = (visit_log_t *)user;
    const uint32_t *tag = (const uint32_t *)element;

    if (log->count <= NODES) {
        log->node[log->count++] = *tag - 100u;
    }
}

static void one_case(const char *what, const uint32_t *order, uint32_t n, const uint32_t *take,
                     uint32_t t, const uint32_t *left, uint32_t l)
{
    npc_spawn_unlinked_t out[NODES];
    pick_set_t           set;
    uint32_t             count = 0;
    uint32_t             repaired = 0;
    uint32_t             links[NODES];
    uint32_t             i;
    bool                 untouched = true;

    ut_section(what);
    build(order, n);
    memset(&set, 0, sizeof set);
    set.count = t;
    memcpy(set.node, take, t * sizeof *take);
    for (i = 0; i < NODES; ++i) {
        links[i] = s_node[i].link;
    }
    ut_check(npc_spawn_list_detach(list(), 0x80u, pick, &set, out, NODES, &count) && count == t,
             "the picked nodes are taken out");
    ut_check(chain_is(left, l), "and the chain walks only the others, in their order");
    for (i = 0; i < NODES; ++i) {
        uint32_t j;
        bool     picked = false;

        for (j = 0; j < t; ++j) {
            picked = picked || take[j] == i;
        }
        if (picked) {
            untouched = untouched && s_node[i].link == links[i];
        }
        if (links[i] != NPC_SPAWN_LIST_FREE) {
            untouched = untouched && s_node[i].link != NPC_SPAWN_LIST_FREE;
        }
    }
    ut_check(untouched, "a node taken out keeps its own link word, and none got the free mark");
    ut_check(npc_spawn_list_relink(list(), out, count, &repaired) == t && repaired == 0u,
             "every one goes back to its own place");
    ut_check(chain_is(order, n), "and the chain is the one before, node for node");
    ut_check(s_header[2] == s_header[1], "with the cursor on the head, where the engine leaves it");
}

int main(void)
{
    static const uint32_t CHAIN[] = { 3u, 0u, 5u, 1u, 6u, 2u };

    {
        static const uint32_t NONE_LEFT[] = { 3u, 0u, 5u, 1u, 6u, 2u };
        one_case("nothing picked", CHAIN, 6u, NULL, 0u, NONE_LEFT, 6u);
    }
    {
        static const uint32_t TAKE[] = { 3u };
        static const uint32_t LEFT[] = { 0u, 5u, 1u, 6u, 2u };
        one_case("the head picked", CHAIN, 6u, TAKE, 1u, LEFT, 5u);
    }
    {
        static const uint32_t TAKE[] = { 2u };
        static const uint32_t LEFT[] = { 3u, 0u, 5u, 1u, 6u };
        one_case("the last node picked", CHAIN, 6u, TAKE, 1u, LEFT, 5u);
    }
    {
        static const uint32_t TAKE[] = { 5u, 1u };
        static const uint32_t LEFT[] = { 3u, 0u, 6u, 2u };
        one_case("two neighbours in the middle", CHAIN, 6u, TAKE, 2u, LEFT, 4u);
    }
    {
        static const uint32_t TAKE[] = { 3u, 0u, 2u };
        static const uint32_t LEFT[] = { 5u, 1u, 6u };
        one_case("the first two and the last", CHAIN, 6u, TAKE, 3u, LEFT, 3u);
    }
    {
        static const uint32_t TAKE[] = { 3u, 0u, 5u, 1u, 6u, 2u };
        one_case("all of them", CHAIN, 6u, TAKE, 6u, NULL, 0u);
    }

    ut_section("the limit is the number of nodes, exactly");
    {
        npc_spawn_unlinked_t out[NODES];
        pick_set_t           set;
        visit_log_t          log;
        uint32_t             count = 0;
        uint32_t             repaired = 0;

        build(CHAIN, 6u);
        memset(&set, 0, sizeof set);
        set.count   = 1u;
        set.node[0] = 2u;
        ut_check(npc_spawn_list_detach(list(), 6u, pick, &set, out, NODES, &count) && count == 1u,
                 "a chain of six walks under a limit of six");
        (void)npc_spawn_list_relink(list(), out, count, &repaired);
        ut_check(!npc_spawn_list_detach(list(), 5u, pick, &set, out, NODES, &count) &&
                     chain_is(CHAIN, 6u),
                 "and is refused under a limit of five, with nothing left out");
        memset(&log, 0, sizeof log);
        ut_check(npc_spawn_list_each(list(), 6u, visit, &log) && log.count == 6u,
                 "a walk of six under six visits all");
        memset(&log, 0, sizeof log);
        ut_check(!npc_spawn_list_each(list(), 5u, visit, &log),
                 "and under five says it did not reach the end");
    }

    ut_section("a walk visits the chain in its order");
    {
        static const uint32_t SHORT[] = { 3u, 0u, 5u };
        visit_log_t           log;

        build(CHAIN, 6u);
        memset(&log, 0, sizeof log);
        ut_check(npc_spawn_list_each(list(), 0x80u, visit, &log) && log.count == 6u &&
                     memcmp(log.node, CHAIN, sizeof CHAIN) == 0,
                 "every element, head first");
        s_node[5].link = NPC_SPAWN_LIST_FREE;
        memset(&log, 0, sizeof log);
        ut_check(!npc_spawn_list_each(list(), 0x80u, visit, &log) && log.count == 2u &&
                     memcmp(log.node, SHORT, 2u * sizeof *SHORT) == 0,
                 "and a free mark stops it, after what came before");
    }

    ut_section("a chain that is not what it should be");
    {
        npc_spawn_unlinked_t out[NODES];
        pick_set_t           set;
        uint32_t             count = 7u;

        build(CHAIN, 6u);
        memset(&set, 0, sizeof set);
        set.count   = 1u;
        set.node[0] = 3u;
        s_node[1].link = NPC_SPAWN_LIST_FREE;   /* a free mark inside the chain */
        ut_check(!npc_spawn_list_detach(list(), 0x80u, pick, &set, out, NODES, &count) &&
                     count == 0u,
                 "a free mark in the chain refuses the detach");
        s_node[1].link = address(6u);
        ut_check(chain_is(CHAIN, 6u), "and the head it had already taken is back");

        build(CHAIN, 6u);
        s_node[2].link = address(3u);           /* a loop */
        ut_check(!npc_spawn_list_detach(list(), 0x80u, pick, &set, out, NODES, &count),
                 "a loop runs into the limit and is refused");
        s_node[2].link = 0u;
        ut_check(chain_is(CHAIN, 6u), "with nothing left out");

        build(CHAIN, 6u);
        set.count   = 2u;
        set.node[1] = 6u;
        ut_check(!npc_spawn_list_detach(list(), 0x80u, pick, &set, out, 1u, &count),
                 "more picks than room to remember them is refused");
        ut_check(chain_is(CHAIN, 6u), "and the one taken before the room ran out is back");
    }

    ut_section("a chain that changed while the nodes were out");
    {
        static const uint32_t AFTER[] = { 1u, 3u, 0u, 6u, 2u };
        npc_spawn_unlinked_t  out[NODES];
        pick_set_t            set;
        uint32_t              count = 0;
        uint32_t              repaired = 0;

        build(CHAIN, 6u);
        memset(&set, 0, sizeof set);
        set.count   = 1u;
        set.node[0] = 1u;
        ut_check(npc_spawn_list_detach(list(), 0x80u, pick, &set, out, NODES, &count) &&
                     count == 1u,
                 "node 1 is out");
        s_node[5].link = address(2u);           /* its predecessor now points elsewhere */
        s_node[0].link = address(6u);           /* and node 5 fell out of the chain as well */
        ut_check(npc_spawn_list_relink(list(), out, count, &repaired) == 1u && repaired == 1u,
                 "node 1 cannot go back after node 5, and is put at the head instead of lost");
        ut_check(chain_is(AFTER, 5u), "so it is in the chain again, first");
    }

    return ut_summary("npc_spawn_list");
}
