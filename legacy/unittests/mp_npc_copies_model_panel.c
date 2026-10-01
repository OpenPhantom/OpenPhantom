/* mp_npc_copies_model_panel.c: the overlays of the two-machine model. See
 * mp_npc_copies_model.h.
 *
 * The model's own overlay speaks the records of the contract and nothing else: it answers a step or
 * two late, now and then holds every grant alike, stamps a wish with the epoch it read and ends its
 * wishes when the epoch moves. An outside overlay, handed in to npc_model_run_with, takes its place
 * and reaches the pool through npc_model_build and npc_model_remove; the counts are kept here the
 * same way for either.
 */
#include "mp_npc_copies_model_internal.h"

#include "mp_npc_copies.h"

#include "common/npc_spawn_note.h"

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <string.h>

/* ---- The overlays: they speak the records of the contract, and nothing else. ---- */

/* Only a tracked wish is judged: one the overlay kept back behind its record was never given a
 * state here, and its epoch reads as zero. */
void answered(sim_t *s, panel_t *p, uint32_t wish, bool builds)
{
    if (wish == 0u || wish >= WISHES || p->track.state[wish] != W_OPEN) {
        return;
    }
    if (++p->track.answers[wish] > 1u) {
        ++s->counts.answered_twice;
    }
    if (builds && p->track.epoch[wish] != p->view.epoch) {
        ++s->counts.granted_across_epochs;   /* described in one world, built in the next */
    }
}

int machine_index(const sim_t *s, const machine_t *m)
{
    return m == &s->client ? 1 : 0;
}

machine_t *machine_at(sim_t *s, int machine)
{
    return machine == 1 ? &s->client : &s->host;
}

void panel_reset(sim_t *s, machine_t *m, uint32_t first, bool absent)
{
    panel_t *p = &m->panel;
    uint32_t w;

    for (w = 0; w < WISHES; ++w) {
        if (p->track.state[w] == W_OPEN && p->track.answers[w] == 0u) {
            p->track.state[w] = W_EXEMPT;   /* the process it waited in is gone */
        }
    }
    memset(&p->record, 0, sizeof p->record);
    memset(&p->view, 0, sizeof p->view);
    p->record.first = first;
    p->record.panel = absent ? 0u : (uint8_t)NPC_SPAWN_PANEL_BUILDER;
    p->absent       = absent;
    p->epoch        = 0u;
    if (s->overlay != NULL) {
        s->overlay->start(s->overlay->user, machine_index(s, m), first, absent);
        p->record = *s->overlay->record(s->overlay->user, machine_index(s, m));
    }
}

/* The overlay reads the grant record. A new epoch ends every wish of another one, queued or sent,
 * and the wishes the multiplayer took leave the ring; an outside overlay does that itself. */
static void panel_read(sim_t *s, machine_t *m)
{
    panel_t *p = &m->panel;
    uint32_t w;

    p->view = *mp_npc_copies_run_record(&m->run);
    if (p->view.epoch != p->epoch) {
        p->epoch = p->view.epoch;
        for (w = 0; w < WISHES; ++w) {
            if (p->track.state[w] == W_OPEN && p->track.answers[w] == 0u &&
                p->track.epoch[w] != p->epoch) {
                p->track.state[w] = W_EXEMPT;
            }
        }
        while (s->overlay == NULL && p->record.count > 0u &&
               p->record.entry[0].epoch != p->epoch) {
            (void)npc_spawn_note_wishes_drop(&p->record, p->record.first);
        }
    }
    if (s->overlay == NULL) {
        (void)npc_spawn_note_wishes_drop(&p->record, p->view.wishes_taken);
    }
}

uint32_t panel_wish(sim_t *s, machine_t *m, uint8_t kind, bool tracked)
{
    panel_t         *p      = &m->panel;
    uint32_t         serial = p->record.first + p->record.count;
    npc_spawn_wish_t wish;

    if (p->absent) {
        return 0u;
    }
    memset(&wish, 0, sizeof wish);
    wish.kind  = kind;
    wish.epoch = p->epoch;
    if (kind == NPC_SPAWN_WISH_SPAWN || kind == NPC_SPAWN_WISH_RESTORE) {
        random_desc(s, &wish.desc);
    }
    if (s->overlay != NULL) {
        serial    = s->overlay->wish(s->overlay->user, machine_index(s, m), kind, &wish.desc);
        p->record = *s->overlay->record(s->overlay->user, machine_index(s, m));
        if (serial == 0u) {
            return 0u;   /* behind the record, and not tracked here */
        }
    } else if (!npc_spawn_note_wishes_append(&p->record, serial, &wish)) {
        return 0u;   /* a full ring: the overlay keeps it back, and nothing is open yet */
    }
    if (tracked && serial < WISHES) {
        p->track.state[serial] = W_OPEN;
        p->track.epoch[serial] = p->epoch;
    }
    return serial;
}

/* What an overlay does with one grant: true when it refuses it. */
static bool overlay_does(sim_t *s, machine_t *m, const npc_spawn_grant_t *g, bool host)
{
    uint32_t k = g->key >= NPC_SPAWN_KEY_FIRST ? g->key - NPC_SPAWN_KEY_FIRST : 0u;
    actor_t *a = g->kind == NPC_SPAWN_GRANT_REFUSED ? NULL : find(m->pool, g->key);

    switch (g->kind) {
    case NPC_SPAWN_GRANT_BUILD:
        answered(s, &m->panel, g->wish, true);
        if (a != NULL) {
            ++s->counts.built_over_an_actor;
            return true;
        }
        if ((host && roll(s, 50u) == 0u) || (a = add(m->pool, g->key)) == NULL) {
            return true;
        }
        if (!host) {
            ++s->counts.built;
            mp_npc_copies_run_spawned(&m->run, k, (uintptr_t)(0x1000u + k), true);
        }
        return false;
    case NPC_SPAWN_GRANT_CANCEL:
        if (a != NULL && m->ridden[k]) {
            ++s->counts.rides;
            return true;
        }
        if (a != NULL) {
            if (host) {
                despawn_to_client(s, k, g->generation);
            }
            a->used = false;
        }
        return false;
    case NPC_SPAWN_GRANT_REFUSED:
        answered(s, &m->panel, g->wish, false);
        return false;
    default:
        return false;
    }
}

/* An outside overlay's step, and what it answered, counted as the model's own would be. */
static void outside_step(sim_t *s, machine_t *m)
{
    panel_t *p      = &m->panel;
    uint32_t before = p->record.grants_done;
    bool     now    = s->quiet || roll(s, 10u) != 0u;
    uint32_t i;

    s->overlay->step(s->overlay->user, machine_index(s, m), &p->view, now);
    p->record = *s->overlay->record(s->overlay->user, machine_index(s, m));
    for (i = 0; i < p->view.count; ++i) {
        const npc_spawn_grant_t *g      = &p->view.entry[i];
        uint32_t                 serial = p->view.first + i;

        if (!npc_spawn_note_serial_after(serial, before) ||
            npc_spawn_note_serial_after(serial, p->record.grants_done)) {
            continue;   /* answered before this step, or not yet */
        }
        if (g->kind == NPC_SPAWN_GRANT_BUILD || g->kind == NPC_SPAWN_GRANT_REFUSED) {
            answered(s, p, g->wish, g->kind == NPC_SPAWN_GRANT_BUILD);
        }
    }
}

/* Up to two grants a step, in order, unless the overlay holds every grant alike this step. */
void panel_step(sim_t *s, machine_t *m, bool host)
{
    panel_t *p    = &m->panel;
    unsigned done = 0;
    uint32_t i;

    if (p->absent) {
        return;
    }
    panel_read(s, m);
    if (s->overlay != NULL) {
        outside_step(s, m);
        return;
    }
    if (!s->quiet && roll(s, 10u) == 0u) {
        return;   /* not now: a load, a save window, no world */
    }
    for (i = 0; i < p->view.count && done < 2u; ++i) {
        uint32_t serial = p->view.first + i;

        if (!npc_spawn_note_serial_after(serial, p->record.grants_done)) {
            continue;
        }
        (void)npc_spawn_note_acknowledge(&p->record, serial,
                                         overlay_does(s, m, &p->view.entry[i], host));
        ++done;
    }
}

/* ---- What an outside overlay reaches the engine through. ---- */

bool npc_model_build(int machine, uint32_t key)
{
    sim_t     *s = &s_sim;
    machine_t *m = machine_at(s, machine);
    actor_t   *a = find(m->pool, key);
    uint32_t   k = key - NPC_SPAWN_KEY_FIRST;

    if (a != NULL) {
        ++s->counts.built_over_an_actor;
        return false;
    }
    if ((machine == 0 && roll(s, 50u) == 0u) || add(m->pool, key) == NULL) {
        return false;
    }
    if (machine == 1) {
        ++s->counts.built;
        mp_npc_copies_run_spawned(&m->run, k, (uintptr_t)(0x1000u + k), true);
    }
    return true;
}

bool npc_model_remove(int machine, uint32_t key)
{
    sim_t     *s = &s_sim;
    machine_t *m = machine_at(s, machine);
    actor_t   *a = find(m->pool, key);
    uint32_t   k = key - NPC_SPAWN_KEY_FIRST;
    uint8_t    g = 0;

    if (a != NULL && m->ridden[k]) {
        ++s->counts.rides;
        return false;
    }
    if (a != NULL) {
        if (machine == 0 && mp_npc_copies_generation(&m->run.host, k, &g)) {
            despawn_to_client(s, k, g);
        }
        a->used = false;
    }
    return true;
}
