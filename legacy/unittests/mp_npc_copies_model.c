/* mp_npc_copies_model.c: two machines running the NPC copies' protocol. See the header.
 *
 * SIZE NOTE: over 600 lines. One model of two machines and the channel between them: its steps
 * call each other, and the counts read the whole of its state. The overlays are in
 * mp_npc_copies_model_panel.c, where an outside one can take their place. What each machine decides
 * is mp_npc_copies_run, the code the multiplayer runs; the model is only the world around it.
 */
#include "mp_npc_copies_model_internal.h"

#include "mp_npc_copies.h"
#include "mp_npc_copies_client.h"
#include "mp_npc_copies_run.h"
#include "mp_npc_copy_wire.h"

#include "common/npc_spawn_note.h"

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <string.h>

sim_t s_sim;

uint32_t roll(sim_t *s, uint32_t range)
{
    s->random = s->random * 1664525u + 1013904223u;
    return (s->random >> 8) % range;
}

actor_t *find(actor_t *pool, uint32_t key)
{
    size_t i;

    for (i = 0; i < POOL; ++i) {
        if (pool[i].used && pool[i].key == key) {
            return &pool[i];
        }
    }
    return NULL;
}

actor_t *add(actor_t *pool, uint32_t key)
{
    size_t i;

    for (i = 0; i < POOL; ++i) {
        if (!pool[i].used) {
            memset(&pool[i], 0, sizeof pool[i]);
            pool[i].used = true;
            pool[i].live = true;
            pool[i].key  = (uint16_t)key;
            return &pool[i];
        }
    }
    return NULL;
}

static uint32_t chain_length(const actor_t *pool)
{
    uint32_t count = 0;
    size_t   i;

    for (i = 0; i < POOL; ++i) {
        count += pool[i].used ? 1u : 0u;
    }
    return count;
}

static void post(sim_t *s, message_t *queue, size_t *count, message_t *m)
{
    m->due = s->step + 1u + roll(s, 3u);
    if (*count < QUEUE) {
        queue[(*count)++] = *m;
    }
}

void random_desc(sim_t *s, npc_spawn_note_desc_t *desc)
{
    memset(desc, 0, sizeof *desc);
    desc->source      = (uint8_t)roll(s, 200u);
    desc->behaviour   = (uint8_t)roll(s, NPC_SPAWN_BEHAVIOURS);
    desc->position[0] = (float)roll(s, 2000u) / 10.0f;
    desc->position[1] = (float)roll(s, 2000u) / 10.0f;
    desc->position[2] = (float)roll(s, 900u) / 10.0f;
    memcpy(desc->file, "trooper.baf", 11u);
}

void despawn_to_client(sim_t *s, uint32_t k, uint8_t generation)
{
    message_t m;

    memset(&m, 0, sizeof m);
    m.kind       = M_DESPAWN;
    m.world      = s->host.world;
    m.k          = k;
    m.generation = generation;
    post(s, s->to_client, &s->to_client_count, &m);   /* the relay carries it */
}

/* ---- The wire: in order and late; now and then a channel that will not take a message. ---- */

static bool channel_takes(sim_t *s)
{
    return s->quiet || roll(s, 40u) != 0u;
}

static bool host_broadcast(void *user, const uint8_t *bytes, size_t count)
{
    sim_t              *s = (sim_t *)user;
    message_t           m;
    mp_npc_copy_entry_t entry;

    if (!channel_takes(s) || count > sizeof m.bytes) {
        return false;
    }
    if (mp_npc_copy_entry_decode(bytes, count, &entry) &&
        entry.kind == NPC_SPAWN_GRANT_BUILD && s->last_walk_whole) {
        const actor_t *a = find(s->host.pool, NPC_SPAWN_KEY_FIRST + entry.k);

        /* Judged only after a walk that saw the whole pool: a short one that missed a copy dying
         * in the same step cannot know, and the next whole walk ends it. */
        if (a == NULL || !a->live) {
            ++s->counts.announced_absent;
        }
    }
    memset(&m, 0, sizeof m);
    m.kind  = M_NOTE;
    m.count = count;
    memcpy(m.bytes, bytes, count);
    post(s, s->to_client, &s->to_client_count, &m);
    return true;
}

static bool host_send_to(void *user, uint8_t slot, const uint8_t *bytes, size_t count)
{
    sim_t *s = (sim_t *)user;

    if (slot != CLIENT_SLOT) {
        ++s->counts.misrouted;
        return true;
    }
    return host_broadcast(user, bytes, count);
}

static bool client_broadcast(void *user, const uint8_t *bytes, size_t count)
{
    sim_t    *s = (sim_t *)user;
    message_t m;

    if (!channel_takes(s) || count > sizeof m.bytes) {
        return false;
    }
    memset(&m, 0, sizeof m);
    m.kind  = M_NOTE;
    m.count = count;
    memcpy(m.bytes, bytes, count);
    post(s, s->to_host, &s->to_host_count, &m);
    return true;
}

/* ---- Both records go through the contract's own publisher, which refuses what it cannot carry:
 * a record it refused once would stop both ways at the real notes. ---- */

static void publish_grants(sim_t *s, machine_t *m, bool changed)
{
    if (changed) {
        bool taken = npc_spawn_note_publish_grants(mp_npc_copies_run_record(&m->run));

        s->counts.records_refused += taken ? 0u : 1u;
        mp_npc_copies_run_published(&m->run, taken);
    }
}

static void publish_wishes(sim_t *s, machine_t *m)
{
    if (!m->panel.absent && !npc_spawn_note_publish_wishes(&m->panel.record)) {
        ++s->counts.records_refused;
    }
}

/* ---- The host. ---- */

static void corrupt(sim_t *s)
{
    machine_t *h = &s->host;
    uint32_t   k = roll(s, MP_WIRE_COPY_MAX);

    if (s->breaking == NPC_MODEL_PHANTOM_LIVE && h->run.host.row[k].state == MP_NPC_COPY_FREE &&
        find(h->pool, NPC_SPAWN_KEY_FIRST + k) == NULL) {
        h->run.host.row[k].state      = MP_NPC_COPY_LIVE;   /* a row that believes in nothing */
        h->run.host.row[k].live       = true;
        h->run.host.row[k].generation = 1u;
    } else if (s->breaking == NPC_MODEL_SAID_TWICE) {
        uint32_t w = 1u + roll(s, WISHES - 1u);

        if (s->client.panel.track.answers[w] > 0u) {
            answered(s, &s->client.panel, w, false);   /* said twice */
        }
    }
}


static void host_census(sim_t *s)
{
    machine_t *h          = &s->host;
    bool       short_walk = !s->quiet && roll(s, 8u) == 0u;
    size_t     i;

    mp_npc_copies_run_census_begin(&h->run);
    for (i = 0; i < POOL; ++i) {
        const actor_t *a = &h->pool[i];

        if (!a->used || a->key < NPC_SPAWN_KEY_FIRST || (short_walk && (i & 1u) != 0u)) {
            continue;
        }
        if (s->breaking == NPC_MODEL_HIDDEN_KEY) {
            if (s->hidden_key == 0u) {
                s->hidden_key = a->key;   /* this one actor the walk never reports again */
            }
            if (a->key == s->hidden_key) {
                continue;
            }
        }
        mp_npc_copies_run_census_saw(&h->run, a->key - NPC_SPAWN_KEY_FIRST, a->live);
    }
    mp_npc_copies_run_census_end(&h->run, !short_walk || s->breaking == NPC_MODEL_LYING_WALKS,
                                 chain_length(h->pool), POOL);
    s->last_walk_whole = !short_walk;
    if (!short_walk) {
        for (i = 0; i < MP_WIRE_COPY_MAX; ++i) {
            const mp_npc_copy_row_t *row = &h->run.host.row[i];
            const actor_t           *a   = find(h->pool, NPC_SPAWN_KEY_FIRST + i);

            if (row->state == MP_NPC_COPY_LIVE && row->live && (a == NULL || !a->live)) {
                ++s->counts.live_without_actor;
            }
        }
    }
}

static void host_substep(sim_t *s)
{
    machine_t             *h = &s->host;
    mp_npc_copies_run_in_t in;
    uint64_t               connection_of[NPC_SPAWN_WORLD_SLOTS];
    uint32_t               k;

    memset(connection_of, 0, sizeof connection_of);
    connection_of[CLIENT_SLOT] = s->connection;
    memset(&in, 0, sizeof in);
    in.joined        = (int32_t)(s->step - s->apart_until) >= 0;
    in.level_known   = (int32_t)(s->step - h->lobby_until) >= 0;
    in.level         = LEVEL;
    in.world         = h->world;
    in.connection_of = connection_of;
    in.now_ms        = s->step * 33u;
    in.wishes        = h->panel.absent ? NULL : &h->panel.record;
    in.broadcast     = &host_broadcast;
    in.send_to       = &host_send_to;
    in.user          = s;
    host_census(s);
    publish_wishes(s, h);
    /* Between the walk and the run, where only a row the walk did not make can stand. */
    if (s->breaking == NPC_MODEL_PHANTOM_LIVE && s->step % 8u == 0u && !s->quiet) {
        corrupt(s);
    }
    publish_grants(s, h, mp_npc_copies_run_substep(&h->run, &in));
    while (mp_npc_copies_run_next_granted(&h->run, &k)) {
        ++s->counts.granted;
    }
}

static void host_block(sim_t *s)
{
    machine_t *h = &s->host;
    message_t  m;
    size_t     i;

    memset(&m, 0, sizeof m);
    m.kind  = M_BLOCK;
    m.world = h->world;
    for (i = 0; i < POOL; ++i) {
        const actor_t *a = &h->pool[i];
        uint32_t       k;
        uint8_t        g = 0;

        if (!a->used || a->key < NPC_SPAWN_KEY_FIRST) {
            continue;
        }
        k = a->key - NPC_SPAWN_KEY_FIRST;
        if (mp_npc_copies_describable(&h->run.host, k) &&
            mp_npc_copies_generation(&h->run.host, k, &g)) {
            m.bitmap[k / 8u]  = (uint8_t)(m.bitmap[k / 8u] | (1u << (k % 8u)));
            m.generations[k] = g;
        }
    }
    post(s, s->to_client, &s->to_client_count, &m);
}

/* ---- The client. ---- */

static void client_new_world(sim_t *s, uint8_t world)
{
    machine_t *c = &s->client;

    c->world = world;
    memset(c->pool, 0, sizeof c->pool);   /* the engine takes every actor with the level */
    mp_npc_copies_run_new_world(&c->run);
    c->lobby_until = s->step + roll(s, 30u);
}

static void client_takes(sim_t *s, const message_t *m)
{
    machine_t              *c     = &s->client;
    mp_npc_copies_client_t *table = mp_npc_copies_run_client_table(&c->run);
    uint32_t                k;

    if (m->kind == M_WORLD) {
        client_new_world(s, m->world);
        return;
    }
    if (m->kind == M_NOTE) {
        (void)mp_npc_copies_run_take(&c->run, 0u, 0u, m->bytes, m->count, s->step * 33u);
        return;
    }
    if (table == NULL || m->world != c->world) {
        return;   /* from another world: the relay and the block refuse it by level */
    }
    if (m->kind == M_DESPAWN) {
        bool built = mp_npc_copies_client_replica(table, m->k, m->generation) != 0u;

        mp_npc_copies_run_removed(&c->run, m->k, m->generation, built);
    } else if (m->kind == M_BLOCK) {
        mp_npc_copies_client_block(table, m->bitmap, sizeof m->bitmap);
        for (k = 0; k < MP_WIRE_COPY_MAX; ++k) {
            if ((m->bitmap[k / 8u] & (1u << (k % 8u))) != 0u) {
                (void)mp_npc_copies_client_record(table, k, m->generations[k]);
            }
        }
    }
}

static void client_census(sim_t *s)
{
    machine_t *c          = &s->client;
    bool       short_walk = !s->quiet && roll(s, 10u) == 0u;
    size_t     i;

    mp_npc_copies_run_census_begin(&c->run);
    for (i = 0; i < POOL; ++i) {
        if (c->pool[i].used && !(short_walk && (i & 1u) != 0u)) {
            mp_npc_copies_run_census_saw(&c->run, c->pool[i].key - NPC_SPAWN_KEY_FIRST, true);
        }
    }
    mp_npc_copies_run_census_end(&c->run, !short_walk, chain_length(c->pool), POOL);
}

static void client_substep(sim_t *s)
{
    machine_t             *c = &s->client;
    mp_npc_copies_run_in_t in;

    memset(&in, 0, sizeof in);
    in.joined      = (int32_t)(s->step - s->apart_until) >= 0;
    in.level_known = (int32_t)(s->step - c->lobby_until) >= 0;
    in.level       = LEVEL;
    in.world       = c->world;
    in.own_slot    = (int32_t)(s->step - c->slot_at) >= 0 ? (uint8_t)CLIENT_SLOT : 0u;
    in.can_park    = (int32_t)(s->step - s->unparked_until) >= 0;
    in.now_ms      = s->step * 33u;
    in.wishes      = c->panel.absent ? NULL : &c->panel.record;
    in.broadcast   = &client_broadcast;
    in.user        = s;
    client_census(s);
    publish_wishes(s, c);
    publish_grants(s, c, mp_npc_copies_run_substep(&c->run, &in));
    if (c->panel.absent) {
        uint32_t i;

        for (i = 0; i < c->run.record.count; ++i) {
            if (c->run.record.entry[i].kind == NPC_SPAWN_GRANT_BUILD) {
                ++s->counts.handed_without_panel;
            }
        }
    }
}

static void client_wishes(sim_t *s)
{
    unsigned burst;

    if (s->quiet || roll(s, 25u) != 0u) {
        return;
    }
    burst = roll(s, 30u) == 0u ? 8u : 1u;   /* now and then faster than the host takes */
    while (burst-- > 0u) {
        bool removal = roll(s, 12u) == 0u;

        (void)panel_wish(s, &s->client,
                         removal ? NPC_SPAWN_WISH_REMOVE_OWN : NPC_SPAWN_WISH_SPAWN, !removal);
    }
}

/* ---- The world. ---- */

static void deliver(sim_t *s, message_t *queue, size_t *count, bool to_client)
{
    size_t kept    = 0;
    bool   blocked = false;
    size_t i;

    /* In order, as the reliable channel delivers: one message not due holds back those after it. */
    for (i = 0; i < *count; ++i) {
        blocked = blocked || (int32_t)(s->step - queue[i].due) < 0;
        if (blocked) {
            queue[kept++] = queue[i];
        } else if (to_client) {
            client_takes(s, &queue[i]);
        } else {
            (void)mp_npc_copies_run_take(&s->host.run, CLIENT_SLOT, s->connection,
                                         queue[i].bytes, queue[i].count, s->step * 33u);
        }
    }
    *count = kept;
}

static void arm(machine_t *m, mp_npc_run_role_t role, uint32_t corpse_censuses)
{
    mp_npc_copies_run_init(&m->run);
    mp_npc_copies_run_arm(&m->run, role, CAP, corpse_censuses, MP_NPC_RUN_ORPHAN_BLOCKS);
}

static void host_new_world(sim_t *s, bool new_process)
{
    machine_t *h = &s->host;
    message_t  m;

    ++h->world;
    memset(h->pool, 0, sizeof h->pool);
    if (new_process) {
        arm(h, MP_NPC_RUN_HOST, h->run.host.corpse_censuses);
        panel_reset(s, h, h->panel.record.first + h->panel.record.count, false);
        s->connection += 1u;
    } else {
        mp_npc_copies_run_new_world(&h->run);
    }
    h->lobby_until = s->step + 10u + roll(s, 40u);
    memset(&m, 0, sizeof m);
    m.kind  = M_WORLD;
    m.world = h->world;
    post(s, s->to_client, &s->to_client_count, &m);   /* the client follows later */
}

static void replace_the_client(sim_t *s)
{
    machine_t *c = &s->client;

    s->connection += 1u;
    arm(c, MP_NPC_RUN_CLIENT, 0u);
    memset(c->pool, 0, sizeof c->pool);
    panel_reset(s, c, c->panel.record.first + c->panel.record.count, c->panel.absent);
    s->to_client_count = 0u;
    s->to_host_count   = 0u;
    c->world           = s->host.world;
    c->slot_at         = s->step + 5u;
    c->lobby_until     = s->step;
}

static void the_host_panel_wishes(sim_t *s)
{
    if (roll(s, 60u) == 0u) {
        unsigned restores = roll(s, 20u) == 0u ? 16u : 1u;   /* now and then a load */

        while (restores-- > 0u) {
            (void)panel_wish(s, &s->host,
                             restores > 0u ? NPC_SPAWN_WISH_RESTORE : NPC_SPAWN_WISH_SPAWN, true);
        }
    }
    if (roll(s, 700u) == 0u) {
        (void)panel_wish(s, &s->host, NPC_SPAWN_WISH_REMOVE_ALL, false);
    } else if (roll(s, 900u) == 0u) {
        (void)panel_wish(s, &s->host, NPC_SPAWN_WISH_REMOVE_OWN, false);
    }
}

static void the_level_happens(sim_t *s)
{
    machine_t *h = &s->host;
    size_t     i;

    for (i = 0; i < POOL; ++i) {
        actor_t *a = &h->pool[i];
        uint8_t  g = 0;

        if (a->used && !a->live && (int32_t)(s->step - a->leaves_at) >= 0) {
            if (mp_npc_copies_generation(&h->run.host, a->key - NPC_SPAWN_KEY_FIRST, &g)) {
                despawn_to_client(s, a->key - NPC_SPAWN_KEY_FIRST, g);
            }
            a->used = false;   /* the corpse timer */
        } else if (a->used && a->live && !s->quiet && roll(s, 400u) == 0u) {
            a->live      = false;   /* killed */
            a->leaves_at = s->step + 5u + roll(s, 80u);
        }
    }
    if (s->quiet) {
        memset(h->ridden, 0, sizeof h->ridden);
        memset(s->client.ridden, 0, sizeof s->client.ridden);
        return;
    }
    if (s->breaking == NPC_MODEL_SAID_TWICE && roll(s, 200u) == 0u) {
        corrupt(s);
    }
    if (roll(s, 50u) == 0u) {
        h->ridden[roll(s, 16u)]        = roll(s, 2u) == 0u;
        s->client.ridden[roll(s, 16u)] = roll(s, 2u) == 0u;
    }
    the_host_panel_wishes(s);
    if (roll(s, 3000u) == 0u) {
        s->apart_until = s->step + 20u + roll(s, 60u);   /* the peers lose each other a while */
    }
    if (roll(s, 2000u) == 0u) {
        s->unparked_until = s->step + 20u + roll(s, 40u);   /* the client's hull is not bound */
    }
    if (roll(s, 1500u) == 0u) {
        replace_the_client(s);   /* a new process on the slot, on a new connection */
    }
    if (roll(s, 2500u) == 0u) {
        host_new_world(s, roll(s, 3u) == 0u);
    }
}

static void check_invariants(sim_t *s)
{
    const mp_npc_copies_t *table = &s->host.run.host;
    size_t                 i;
    size_t                 j;

    for (i = 0; i < POOL; ++i) {
        for (j = i + 1u; j < POOL; ++j) {
            s->counts.doubles += (s->host.pool[i].used && s->host.pool[j].used &&
                                  s->host.pool[i].key == s->host.pool[j].key) ? 1u : 0u;
            s->counts.doubles += (s->client.pool[i].used && s->client.pool[j].used &&
                                  s->client.pool[i].key == s->client.pool[j].key) ? 1u : 0u;
        }
    }
    for (i = 0; i < POOL; ++i) {
        const actor_t *a = &s->host.pool[i];

        if (a->used && a->live) {
            const mp_npc_copy_row_t *row     = &table->row[a->key - NPC_SPAWN_KEY_FIRST];
            bool                     leaving = row->state == MP_NPC_COPY_ENDING &&
                               ((row->duties & MP_NPC_COPY_DUTY_CANCEL_NOTE) != 0u ||
                                row->waiting_for == MP_NPC_COPY_WAIT_CANCEL);

            ++s->counts.copies_seen;
            /* A copy built from a grant written before its removal stands until the cancel
             * behind it is done: on its way out, and counted as such. */
            if (!(row->state == MP_NPC_COPY_GRANTED || row->state == MP_NPC_COPY_LIVE ||
                  (row->state == MP_NPC_COPY_ENDING && row->live) || leaving)) {
                ++s->counts.uncounted;
            }
        }
    }
    for (i = 0; i < MP_WIRE_COPY_MAX; ++i) {
        if (table->row[i].state == MP_NPC_COPY_GRANTED &&
            table->censuses - table->row[i].since > MP_NPC_COPIES_SEEN_DEADLINE + 10u) {
            ++s->counts.grants_overdue;
        }
    }
}

static void run_a_step(sim_t *s)
{
    ++s->step;
    the_level_happens(s);
    deliver(s, s->to_host, &s->to_host_count, false);
    host_substep(s);
    panel_step(s, &s->host, true);
    if (s->step % 2u == 0u) {
        host_block(s);
    }
    deliver(s, s->to_client, &s->to_client_count, true);
    client_substep(s);
    panel_step(s, &s->client, false);
    client_wishes(s);
    check_invariants(s);
}

static void settle(sim_t *s)
{
    const mp_npc_copies_t *table = &s->host.run.host;
    uint32_t               k;

    for (k = 0; k < WISHES; ++k) {
        if (s->client.panel.track.state[k] == W_OPEN && s->client.panel.track.answers[k] != 1u) {
            ++s->counts.unanswered;
        }
        if (s->host.panel.track.state[k] == W_OPEN && s->host.panel.track.answers[k] != 1u) {
            ++s->counts.unanswered;
        }
    }
    for (k = 0; k < MP_WIRE_COPY_MAX; ++k) {
        const mp_npc_copy_row_t *row        = &table->row[k];
        bool                     host_has   = row->state == MP_NPC_COPY_LIVE && row->live;
        bool                     client_has = mp_npc_copies_client_state(&s->client.run.client,
                                                                         k) == MP_NPC_HELD_BUILT;

        if (host_has != client_has ||
            (host_has && s->client.run.client.held[k].generation != row->generation)) {
            ++s->counts.mismatched;
        }
    }
    s->counts.entries_held_back = s->client.run.counters.entries_no_panel;
}

void npc_model_run(uint32_t seed, int breaking, npc_model_result_t *out)
{
    npc_model_run_with(seed, breaking, NULL, out);
}

void npc_model_run_with(uint32_t seed, int breaking, const npc_model_overlay_t *overlay,
                        npc_model_result_t *out)
{
    sim_t   *s = &s_sim;
    uint32_t i;

    memset(s, 0, sizeof *s);
    s->random     = seed;
    s->breaking   = breaking;
    s->overlay    = overlay;
    s->connection = 0x100000001ull;
    arm(&s->host, MP_NPC_RUN_HOST, seed % 2u == 0u ? 20u : 0u);
    arm(&s->client, MP_NPC_RUN_CLIENT, 0u);
    panel_reset(s, &s->host, 1u, false);
    panel_reset(s, &s->client, 1u, breaking == NPC_MODEL_NO_CLIENT_PANEL);
    s->client.slot_at = 5u;
    for (i = 0; i < 20000u; ++i) {
        run_a_step(s);
    }
    s->quiet = true;
    for (i = 0; i < 1500u; ++i) {
        run_a_step(s);
    }
    settle(s);
    *out = s->counts;
}
