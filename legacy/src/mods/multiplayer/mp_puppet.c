/* mp_puppet.c: the far player's body on this machine, placed and dressed from samples.
 *
 * SIZE NOTE: between 800 and 900 lines, one subject with one entry point. What is here is
 * the window: the sample, the placement, the weapon, the puppet's rebuilt phase one, the events
 * and the vitals, in the order the window has to run them, and that order is the reason they stay
 * together. The animation channels, the sabre actions, the shot and the guard in front of the
 * engine's starters are modules of their own. The seam, if it grows again, is the far body's
 * feeding: the placement and the weapon on one side, the events and the vitals on the other.
 *
 * The order inside the window is not free. The position and heading go first, because the plan
 * that follows commits them to the object. The base clip goes before the weapon, because the
 * weapon setter plays its draw clip on the overlay channel and must not find that channel taken
 * by a change the state path is about to make. The overlay goes after the weapon, so that the
 * draw clip the setter just started is what the overlay step sees as the puppet's own. The plan
 * runs after all of that and copies the record into the object. The events go last, after the
 * plan, because a shot leaves from where the object stands now and not from where it stood a
 * substep ago.
 *
 * The animation channels themselves are decided and applied in mp_puppet_anim.c. What this file
 * keeps is the sample, the weapon, the puppet's rebuilt phase one, and the events.
 *
 * The puppet's phase one is rebuilt here rather than run from the engine's table. The engine's
 * own copies the substep length into the record, ticks the blade's length and light off the two
 * weapon slots, and runs the aux action last; a puppet needs exactly those three, because a
 * replicated weapon is only a request until the aux commits it, the blade grows only by the
 * substep length, and the force bolt leaves only on the aux's marker. The rest of that phase
 * counts down state a puppet does not own and pushes the force meter onto the local HUD.
 *
 * The blade is a mesh, and the mesh is the model's, not the body's: two bodies drawing the same
 * model share one set of sabre vertices, and the engine's length tick writes a new length into
 * them. So a puppet never runs that tick. Its length is stepped in its own block by the same
 * arithmetic, and its blade is drawn from vertices worked out of that block for the length of its
 * own draw, while the shared mesh is written by the local player's own tick alone. mp_blade answers
 * what runs.
 *
 * Shots, force pushes and the start of a weapon change arrive as events, not as state, and are
 * performed through the engine's own spawner and starters inside the window, so the shot hull
 * stamps the puppet's class on the projectile and the bolt leaves on the push clip's marker
 * exactly as it did on the far machine. The weapon's committed slot travels as state as well and
 * stays as the reconciliation, because it is the only thing that can correct a change whose event
 * was never performed. Each of those starters plays a clip of its own choosing through the
 * engine's overlay player, which tests the ordinal against the actor's clip count only through an
 * assert that ends the process, one too far, and with all four tracks busy writes into the track
 * before the array. So every one of them first asks mp_puppet_starter whether the body's actor
 * carries that clip, and drops what it cannot play, and then asks whether a track is free, and
 * holds its event while none is.
 * Every event carries the sender's tick and waits for the render tick to reach it; a shot carries
 * its muzzle relative to the sender's body and is placed by the puppet's own pose, so the bolt
 * leaves the weapon wherever the puppet is drawn. The sabre actions are performed by their own
 * module in the same order and under the same due-ness.
 *
 * The far player's health and death are state, and they are the far machine's to decide: what
 * touches the puppet here is answered without the engine's handler, and the health that arrives
 * on the wire is written into the puppet's banked status record directly, never through an
 * engine setter, because those write the local player's HUD cells as well. A death shows as the
 * death clip on the base channel, which the state path plays, and as the ground shadow taken
 * away; the death clip's authored event also strips the object's collision, so the far player's
 * respawn, seen here as the dead flag clearing, puts the shadow and the collision back.
 */
#include "mp_puppet.h"

#include "mp_bank.h"
#include "mp_blade.h"
#include "mp_body.h"
#include "mp_body_wear.h"
#include "mp_bridge_appearance.h"
#include "mp_cells.h"
#include "mp_effects.h"
#include "mp_footstep.h"
#include "mp_sound.h"
#include "mp_events.h"
#include "mp_phases.h"
#include "mp_player_sound_play.h"
#include "mp_puppet_anim.h"
#include "mp_puppet_sabre.h"
#include "mp_puppet_shot.h"
#include "mp_puppet_starter.h"
#include "mp_signatures.h"
#include "mp_twist.h"

#include "common/host_image.h"
#include "common/logging.h"
#include "common/memory.h"

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <string.h>

/* Player record fields, the offsets the body and phase modules stand on. The weapon setter at
 * 0x0044B268 writes +0x88 and hangs its continuation (0x0044B5B8) on +0x64; that continuation
 * polls the overlay track's marker and only then calls the commit at 0x0044B609, which is what
 * writes +0x84, so the committed slot is behind a change by the marker frame of the draw clip,
 * every time. The blade tick at 0x00449A84 reads +0x84 and +0x88 and grows +0x210 by twice the
 * frame dt while the sabre is the slot and shrinks it by ten times the dt while it is not the
 * request; the push starter at 0x0044BCDE moves +0x2C0 into the charge and zeroes it. Every one
 * of those starters begins with a return while +0x64 is set. */
#define RECORD_HACTOR         0x0Cu   /* the actor object the clips play on */
#define RECORD_AUX_ACTION     0x64u   /* the running aux updater; 0 means none */
#define RECORD_FRAME_DT       0x74u   /* the substep length the engine's timers phase copies in */
#define RECORD_WEAPON_SLOT    0x84u   /* the equipped slot */
#define RECORD_REQUESTED_SLOT 0x88u   /* the slot a change in flight is heading for */
#define RECORD_POS            0x118u
#define RECORD_HEADING        0x2A0u
#define RECORD_FORCE_METER    0x2C0u  /* what the push starter spends and hands to the bolt */

/* The object's flag word is its first dword; bit 1 is the projected ground shadow, which the
 * retail death clears and a respawn puts back. */
#define OBJECT_FLAGS        0x00u
#define OBJECT_CASTS_SHADOW 0x02u

/* The health is the first dword of the status record; the wire's byte is clamped to the game's
 * own ceiling before it is written, since the engine's own heal clamps there too. */
#define STATUS_HEALTH_OFFSET 0u
#define HEALTH_MAX           100u

/* How many substeps a push may wait for the puppet's aux slot before it is dropped: two seconds,
 * longer than any weapon change or push the aux can be busy with. A weapon change waits the same
 * two seconds for the slot and for a free track. */
#define PUSH_WAIT_LIMIT   64u
#define WEAPON_WAIT_LIMIT 64u

/* And how long a change begun from an event keeps the weapon before the wire's committed slot is
 * expected to have caught up with it: the draw clip's marker is a handful of frames deep, so two
 * seconds is the far player's engine refusing the change or the sample never carrying it. */
#define WEAPON_EVENT_WAIT_LIMIT 64u

typedef void(__cdecl *set_weapon_fn_t)(int32_t mode, int32_t arg);
typedef void(__cdecl *engine_fn_t)(void);

/* One far body's puppet: the sample the window dresses it with, the weapon change and the push
 * it is part way through, its vitals, and the moments still due. Each far bank shows its own
 * player; one record for all of them would perform one player's shot at another's muzzle. */
typedef struct mp_puppet_peer {
    mp_puppet_sample_t sample;

    uint32_t render_tick;
    bool     render_known;

    bool     hero_reported;     /* the far hero has been held against the built one once */
    uint32_t hero_samples;      /* samples taken since this body was built, for the grace */
    int32_t  applied_weapon;    /* -1 until first applied */
    int32_t  weapon_event_slot; /* -1 none: the slot a weapon event started a change to, held
                                 * until the wire's committed slot reports the same */
    uint32_t weapon_event_wait; /* substeps that pending has waited for the wire */
    uint32_t weapon_wait;       /* substeps the weapon event at the head has waited to start */
    uint32_t push_wait;         /* substeps the push at the head has waited for the aux */

    bool     dead_known;        /* the dead latch below has been set from a sample once; kept
                                 * across resets, because a peer that comes back alive after
                                 * dying still needs its shadow and collision put back */
    bool     dead_applied;

    mp_event_queue_t events;    /* the far player's moments, oldest first, each with its tick */
} mp_puppet_peer_t;

/* How long a hero difference is allowed to be the replay delay rather than a disagreement. The
 * timeline holds its target lag at three ticks and re-seats at eight, so anything past a handful
 * of samples is no longer the delay; sixty four is two seconds at the substep ladder and is the
 * same freshness this feature uses elsewhere. */
#define MP_PUPPET_HERO_GRACE_SAMPLES 64u

typedef struct mp_puppet_state {
    mp_puppet_peer_t peer[MP_BANK_FAR_MAX + 1u];   /* by far bank, 0 the spare */

    uint32_t weapon_events;
    uint32_t weapon_dropped;
    uint32_t weapon_refused;
    uint32_t weapon_withheld;
    uint32_t weapon_expired;
    uint32_t force_pushes;
    uint32_t pushes_dropped;
    uint32_t events_late;
    uint32_t events_forced;
    uint32_t write_faults;

    uint32_t health_writes;
    uint32_t health_refused;    /* the bank had no placed status to write into */
    bool     health_refusal_logged;
    uint32_t last_health;       /* the wire's value as last written */

    uint32_t died;
    uint32_t revived;
    uint32_t shadow_faults;

    uint32_t heroes_agreed;     /* bodies whose far player named the hero they were built for */
    uint32_t heroes_apart;      /* and those still apart when the grace ran out */
    uint32_t hero_worst_wait;   /* the most samples any of the agreements took */

    set_weapon_fn_t set_weapon;
    engine_fn_t     start_force_push;
    engine_fn_t     tick_blade_light;
} mp_puppet_state_t;

static mp_puppet_state_t puppet;

/* One far body's puppet by the bank that shows it. An index that is no far bank lands on the
 * spare at 0, which is what a caller outside every window, a unit test, gets. */
static mp_puppet_peer_t *peer_of(size_t bank)
{
    return &puppet.peer[bank <= MP_BANK_FAR_MAX ? bank : 0u];
}

static bool inside_text(uintptr_t address)
{
    uintptr_t text = host_image_text();

    return text != 0 && address >= text && address < text + host_image_text_size();
}

/* The record the window installed, which the player pointer names for the window's duration. */
static bool current_record(uint32_t *record)
{
    uintptr_t pr_cell = mp_cells_address(MP_CELL_PR);

    return pr_cell != 0 && memory_try_read_u32(pr_cell, record) && *record != 0;
}

static void note_write_fault(const char *what)
{
    if (puppet.write_faults == 0u) {
        log_warning("the puppet's %s write was refused; later refusals are counted", what);
    }
    ++puppet.write_faults;
}

/* The engine's phase one at 0x00448C0F also runs the carry and ground probe, the flinch, the aim
 * ease, the attack timers, the shield timer and the force meter tick at 0x004495F6, and that last
 * one pushes the meter onto the local HUD through the status bar's force setter; a puppet running
 * it would flash its meter onto the local player's bar. So the three things a puppet needs are
 * copied out: the frame dt from the frame delta cell, the two blade ticks (the length at
 * 0x00449A84, whose set at 0x00449C6E rebuilds the sabre node's four mesh vertices, which the
 * halo quad spans, so a collapsed mesh is also an invisible halo; and the light at 0x00449B9D,
 * which moves the record's embedded light at +0x218 to the sabre node), and the aux call, last,
 * as the engine makes it. The first two machine run showed why: the far sabre "activated" with
 * no blade, no glow and no light, and a push showed nothing on the other machine, because the
 * puppet plan then ran phases 11 and 12 only, so the aux never ran, the request never committed,
 * and every later weapon change and push was refused in silence by the aux gate. */
static void __cdecl puppet_tick_timers(void)
{
    size_t          bank = mp_bank_active();   /* the body the window installed */
    uintptr_t       dt_cell = mp_cells_address(MP_CELL_FRAME_DELTA);
    uint32_t        record = 0;
    uint32_t        dt = 0;
    uint32_t        aux = 0;
    mp_blade_tick_t blade;

    if (!current_record(&record)) {
        return;
    }
    if (dt_cell != 0 && memory_try_read_u32(dt_cell, &dt) &&
        !mp_bank_window_write_u32((uintptr_t)record + RECORD_FRAME_DT, dt)) {
        note_write_fault("substep length");
    }
    blade = mp_blade_puppet_tick((uintptr_t)record, mp_body_wear_worn(bank));
    (void)mp_body_wear_blade_state_run((uintptr_t)record, blade);
    mp_body_wear_blade_light_run((uintptr_t)record, blade, puppet.tick_blade_light);
    /* The aux is a code pointer the engine wrote into the record; one that points outside the
     * image is a torn record and is not called. */
    if (memory_try_read_u32((uintptr_t)record + RECORD_AUX_ACTION, &aux) && aux != 0 &&
        inside_text((uintptr_t)aux)) {
        ((engine_fn_t)(uintptr_t)aux)();
    }
}

void mp_puppet_resolve(void)
{
    bool shot_spawner = mp_puppet_shot_resolve();

    puppet.set_weapon       = (set_weapon_fn_t)mp_signatures_address(MP_SITE_PLR_SET_WEAPON);
    puppet.start_force_push = (engine_fn_t)mp_signatures_address(MP_SITE_PLR_START_FORCE_PUSH);
    puppet.tick_blade_light = (engine_fn_t)mp_signatures_address(MP_SITE_PLR_TICK_BLADE_LIGHT);
    mp_puppet_anim_resolve();
    mp_puppet_sabre_resolve();
    mp_effects_resolve();
    mp_twist_resolve();
    mp_phases_set_puppet_timers(&puppet_tick_timers);
    mp_puppet_reset_all();

    if (puppet.set_weapon == NULL || puppet.start_force_push == NULL ||
        puppet.tick_blade_light == NULL || !shot_spawner) {
        log_warning("the puppet is dressed with less than the full set: weapon setter %s, push "
                    "starter %s, blade light %s, shot spawner %s",
                    puppet.set_weapon != NULL ? "ok" : "MISSING",
                    puppet.start_force_push != NULL ? "ok" : "MISSING",
                    puppet.tick_blade_light != NULL ? "ok" : "MISSING",
                    shot_spawner ? "ok" : "MISSING");
    }
}

void mp_puppet_reset(size_t bank)
{
    mp_puppet_peer_t *p = peer_of(bank);
    uint32_t          dropped = p->events.dropped;

    p->applied_weapon     = -1;
    p->hero_reported      = false;   /* a new peer may be a different hero */
    p->hero_samples       = 0u;
    p->weapon_event_slot  = -1;
    p->weapon_event_wait  = 0;
    p->weapon_wait        = 0;
    p->sample.twist_count = 0;
    p->render_known       = false;
    p->push_wait          = 0;
    mp_event_queue_init(&p->events);
    p->events.dropped = dropped;   /* the count is the report's, not the peer's */
    mp_puppet_anim_reset(bank);
    mp_puppet_sabre_reset(bank);
    mp_puppet_starter_reset(bank);
    mp_effects_reset(bank);
    mp_twist_reset(bank);
    mp_sound_reset();
    mp_player_sound_forget(bank);
}

void mp_puppet_hero_counts(uint32_t *agreed, uint32_t *apart, uint32_t *worst_wait)
{
    if (agreed != NULL) {
        *agreed = puppet.heroes_agreed;
    }
    if (apart != NULL) {
        *apart = puppet.heroes_apart;
    }
    if (worst_wait != NULL) {
        *worst_wait = puppet.hero_worst_wait;
    }
}

void mp_puppet_reset_all(void)
{
    size_t bank;

    for (bank = 0u; bank <= MP_BANK_FAR_MAX; ++bank) {
        mp_puppet_reset(bank);
    }
}

/* The far player's hero against the one the second body was built for. They agree once the far
 * side's appearance has arrived and been carried out, and a disagreement is worth exactly one
 * line: either no appearance has crossed yet, or the one that crossed was refused because this
 * installation could not load the actor it named. Both leave the body wearing something the far
 * player did not choose, and every clip ordinal on the wire then means something else here, which
 * is why it is said out loud rather than left to look like an animation fault.
 *
 * The number compared is the hero the body was BUILT for, not the slot it rides: a body wearing a
 * foreign actor rides a borrowed slot, and comparing against that would report a disagreement on
 * exactly the case that is working. */
static void note_hero(size_t bank, mp_puppet_peer_t *p, uint8_t far_hero)
{
    int32_t                 built_for = mp_body_hero_at(bank);
    mp_appearance_verdict_t verdict;

    if (p->hero_reported) {
        return;
    }
    ++p->hero_samples;
    verdict = mp_appearance_hero_verdict(built_for, (int32_t)far_hero, p->hero_samples,
                                         MP_PUPPET_HERO_GRACE_SAMPLES);
    if (verdict == MP_APPEARANCE_WAIT) {
        return;
    }
    p->hero_reported = true;
    if (verdict == MP_APPEARANCE_AGREED) {
        ++puppet.heroes_agreed;
        if (p->hero_samples > puppet.hero_worst_wait) {
            puppet.hero_worst_wait = p->hero_samples;
        }
        return;
    }
    ++puppet.heroes_apart;
    log_warning("the far player has played hero %u for %u sample(s) since the body here was built "
                "for hero %d as %s: no appearance has been carried out for it, or the one that "
                "arrived named an actor this installation could not load",
                (unsigned)far_hero, (unsigned)p->hero_samples, (int)built_for,
                mp_body_asset_at(bank));
}

void mp_puppet_set_sample(size_t bank, const mp_puppet_sample_t *sample)
{
    mp_puppet_peer_t *p = peer_of(bank);

    if (sample == NULL) {
        return;
    }
    p->sample = *sample;
    note_hero(bank, p, p->sample.state.hero);
    if (p->sample.twist_count > MP_WIRE_MAX_TWISTS) {
        p->sample.twist_count = MP_WIRE_MAX_TWISTS;
    }
}

void mp_puppet_note_render_tick(size_t bank, uint32_t render_tick)
{
    mp_puppet_peer_t *p = peer_of(bank);

    p->render_tick  = render_tick;
    p->render_known = true;
}

void mp_puppet_queue_event(size_t bank, const mp_event_t *event)
{
    if (event != NULL) {
        mp_event_queue_push(&peer_of(bank)->events, event);
    }
}

uint32_t mp_puppet_force_pushes(void)
{
    return puppet.force_pushes;
}

uint32_t mp_puppet_shots(void)
{
    return mp_puppet_shot_spawned();
}

void mp_puppet_get_counters(mp_puppet_counters_t *out)
{
    size_t bank;

    if (out == NULL) {
        return;
    }
    memset(out, 0, sizeof *out);
    mp_puppet_anim_counters(&out->anim);
    out->weapon_events   = puppet.weapon_events;
    out->weapon_dropped  = puppet.weapon_dropped;
    out->weapon_refused  = puppet.weapon_refused;
    out->weapon_withheld = puppet.weapon_withheld;
    out->weapon_expired  = puppet.weapon_expired;
    out->force_pushes    = puppet.force_pushes;
    out->shots           = mp_puppet_shot_spawned();
    out->shots_unplaced  = mp_puppet_shot_unplaced();
    out->pushes_dropped  = puppet.pushes_dropped;
    for (bank = 0u; bank <= MP_BANK_FAR_MAX; ++bank) {
        out->events_dropped += puppet.peer[bank].events.dropped;
    }
    out->events_late     = puppet.events_late;
    out->events_forced   = puppet.events_forced;
    out->write_faults    = puppet.write_faults;
    mp_puppet_sabre_counters(&out->sabre);
    out->health_writes   = puppet.health_writes;
    out->health_refused  = puppet.health_refused;
    out->last_health     = puppet.last_health;
    out->died            = puppet.died;
    out->revived         = puppet.revived;
    out->shadow_faults   = puppet.shadow_faults;
}

/* The setter treats selecting the equipped slot as a holster, so a slot the puppet already holds
 * is latched without a call. A change already in flight (request and slot disagree) is waited for
 * before anything is judged, or a wire value equal to the old slot would be latched while the aux
 * is about to commit the new one. The setter refuses while an aux runs, which is why the request
 * is read back before the change counts as applied.
 *
 * This is the second way a weapon change reaches the puppet, and the slower one. The change also
 * arrives as an event, at the tick the far player pressed the key, and while that change is in
 * flight the state is BEHIND it: the slot the sample carries is the old one until the far
 * player's own draw clip reaches its marker. Acting on it here would put the weapon the far
 * player just holstered back into the puppet's hand. So a change an event began owns the weapon
 * until the wire agrees with it; a wire that never agrees, which is an event the far side's own
 * engine refused after this one performed it, hands the weapon back after two seconds and is
 * counted.
 *
 * The setter plays its draw clip through the engine's overlay player, which ends the process on
 * a clip the actor lacks and writes into the track before the array when all four are busy. A
 * change the actor cannot draw is latched as applied without a call, so it is judged once until
 * the wire names another slot; a busy set of tracks holds the change back to the next substep,
 * where the state that asked for it is still there. A body wearing a borrowed model is the one
 * exception: its lent rig almost never carries the draw clips, so the two weapon words are
 * written into its record here and the change takes effect without the animation. */
static void apply_weapon(size_t bank, mp_puppet_peer_t *p, uint32_t record, uint32_t object)
{
    uint8_t              wanted = p->sample.state.weapon;
    uint32_t             slot = 0;
    uint32_t             requested = 0;
    mp_starter_verdict_t verdict;

    if (puppet.set_weapon == NULL) {
        return;
    }
    if (p->weapon_event_slot >= 0) {
        if (p->weapon_event_slot == (int32_t)wanted) {
            p->applied_weapon    = (int32_t)wanted;
            p->weapon_event_slot = -1;
            p->weapon_event_wait = 0;
            return;
        }
        if (++p->weapon_event_wait <= WEAPON_EVENT_WAIT_LIMIT) {
            return;
        }
        p->weapon_event_slot = -1;
        p->weapon_event_wait = 0;
        ++puppet.weapon_expired;
    }
    if (wanted == (uint8_t)p->applied_weapon) {
        return;
    }
    if (!memory_try_read_u32((uintptr_t)record + RECORD_WEAPON_SLOT, &slot) ||
        !memory_try_read_u32((uintptr_t)record + RECORD_REQUESTED_SLOT, &requested)) {
        return;
    }
    if (requested != slot) {
        return;
    }
    if (slot == wanted) {
        p->applied_weapon = wanted;
        return;
    }
    if (object == 0u) {
        return;   /* no actor for the draw clip; the state is still there next substep */
    }
    verdict = mp_puppet_starter_weapon(bank, object, slot, wanted, mp_body_wear_worn(bank));
    if (verdict != MP_STARTER_GO) {
        if (verdict == MP_STARTER_SET_ONLY) {
            /* Not latched on the write: the next window reads the slot back and latches there,
             * so a write that never reached the bank keeps asking instead of going quiet. */
            (void)mp_puppet_starter_set_slot(bank, record, wanted);
        } else if (verdict == MP_STARTER_NEVER) {
            p->applied_weapon = wanted;
        }
        return;
    }
    if (!mp_puppet_anim_overlay_track_free(object)) {
        ++puppet.weapon_withheld;
        return;
    }
    puppet.set_weapon(2, (int32_t)wanted);
    if (memory_try_read_u32((uintptr_t)record + RECORD_REQUESTED_SLOT, &requested) &&
        requested == wanted) {
        p->applied_weapon = wanted;
    }
}

/* The weapon change as a moment: the same setter and the same mode the state path uses, run at
 * the render tick the far player pressed the key in rather than at the marker its own draw clip
 * commits on. What it needs is the aux slot, which the setter refuses without, and a free track
 * for the draw clip; either being busy holds the event in order for the two seconds a push waits.
 *
 * A slot the puppet already holds is never handed to the setter: re-selecting the equipped
 * weapon is the engine's own holster gesture, and the puppet would put its weapon away instead of
 * drawing the one that arrived. The pending is latched in that case too, so the state path leaves
 * the weapon alone until the wire has caught up, and so it is for a change whose draw clip the
 * body's actor lacks, which is consumed without a call. */
static bool perform_weapon(size_t bank, mp_puppet_peer_t *p, uint32_t record, uint32_t object,
                           const mp_event_t *event, bool *aux_taken)
{
    uint32_t             slot = 0;
    uint32_t             requested = 0;
    uint32_t             aux = 0;
    bool                 ready;
    mp_starter_verdict_t verdict;

    if (puppet.set_weapon == NULL) {
        return true;   /* nothing to perform it with; the event is consumed, not held */
    }
    if (!memory_try_read_u32((uintptr_t)record + RECORD_WEAPON_SLOT, &slot) ||
        !memory_try_read_u32((uintptr_t)record + RECORD_REQUESTED_SLOT, &requested)) {
        ++puppet.weapon_refused;
        return true;
    }
    if (slot == event->weapon_slot || requested == event->weapon_slot) {
        /* Already there, or already on the way: the state path started this same change in an
         * earlier substep, which is what happens to an event that arrives after its own state.
         * Latching the pending is all that is left to do, and starting it again would put the
         * weapon away. */
        p->weapon_event_slot = (int32_t)event->weapon_slot;
        p->weapon_event_wait = 0;
        return true;
    }
    /* The guard is asked once the setter could run, because only then is the slot it draws from
     * settled; an actor that did not read waits like a busy track. A worn body's change plays no
     * clip and needs neither the aux nor a free track, and it waits for them all the same: one
     * condition for both answers is one place fewer for the two to come apart. */
    ready = !*aux_taken && requested == slot && mp_puppet_anim_overlay_track_free(object) &&
            memory_try_read_u32((uintptr_t)record + RECORD_AUX_ACTION, &aux) && aux == 0u;
    verdict = ready ? mp_puppet_starter_weapon(bank, object, slot, event->weapon_slot,
                                               mp_body_wear_worn(bank))
                    : MP_STARTER_NOT_YET;
    if (verdict == MP_STARTER_NOT_YET) {
        if (++p->weapon_wait > WEAPON_WAIT_LIMIT) {
            p->weapon_wait = 0;
            ++puppet.weapon_dropped;
            return true;
        }
        *aux_taken = true;   /* nothing behind it may take the slot this change is waiting for */
        return false;
    }
    p->weapon_wait = 0;
    /* Only a go reaches the setter. Any other answer is a draw clip the body's actor lacks, and
     * the setter would end the process on it; a worn body takes the slot written into its record
     * instead. Either way the event is consumed and the aux is not taken, because nothing ran. */
    if (verdict != MP_STARTER_GO) {
        if (verdict == MP_STARTER_SET_ONLY) {
            (void)mp_puppet_starter_set_slot(bank, record, event->weapon_slot);
        }
        p->weapon_event_slot = (int32_t)event->weapon_slot;   /* latched, never started */
        p->weapon_event_wait = 0;
        return true;
    }
    puppet.set_weapon(2, (int32_t)event->weapon_slot);
    if (memory_try_read_u32((uintptr_t)record + RECORD_REQUESTED_SLOT, &requested) &&
        requested == event->weapon_slot) {
        p->weapon_event_slot = (int32_t)event->weapon_slot;
        p->weapon_event_wait = 0;
        *aux_taken = true;
        ++puppet.weapon_events;
    } else {
        ++puppet.weapon_refused;
    }
    return true;
}

/* A push needs the aux slot, which a weapon change or an earlier push may hold, and a free track
 * for the push overlay the starter plays: the engine's player writes before the track array when
 * none is free, so the same search the state path makes is made here as well. Answers whether the
 * push was started; a push that has waited two seconds is dropped and counted, because a queue
 * that stalls on one aux would end up dropping everything, and one whose clip the body's actor
 * lacks is dropped at once. */
static bool perform_push(size_t bank, mp_puppet_peer_t *p, uint32_t record, uint32_t object,
                         const mp_event_t *event, bool *aux_taken)
{
    uint32_t             aux = 0;
    mp_starter_verdict_t verdict;

    if (puppet.start_force_push == NULL) {
        return true;   /* nothing to perform it with; the event is consumed, not held */
    }
    verdict = mp_puppet_starter_push(bank, object);
    if (verdict == MP_STARTER_NEVER) {
        return true;   /* dropped without the aux, so nothing behind it waits for it */
    }
    if (verdict == MP_STARTER_GO && !*aux_taken && mp_puppet_anim_overlay_track_free(object) &&
        memory_try_read_u32((uintptr_t)record + RECORD_AUX_ACTION, &aux) && aux == 0) {
        if (!mp_bank_window_write_f32((uintptr_t)record + RECORD_FORCE_METER, event->charge)) {
            note_write_fault("force meter");
        }
        puppet.start_force_push();
        ++puppet.force_pushes;
        p->push_wait = 0;
        *aux_taken = true;   /* the aux is ours now; a second push waits its turn */
        return true;
    }
    if (!*aux_taken && ++p->push_wait > PUSH_WAIT_LIMIT) {
        ++puppet.pushes_dropped;
        p->push_wait = 0;
        return true;
    }
    *aux_taken = true;
    return false;
}

/* The far player's moments, oldest first. One that is not yet due holds everything behind it,
 * because the ring is in the sender's order. A shot is performed at once; a push that finds the
 * aux busy is held for the next window, and the shots behind it are not held back by it. A
 * sabre action held for the aux holds every sabre action behind it as well: a disarm performed
 * ahead of the block it ends would leave that block armed until the fallback. */
static void perform_events(size_t bank, mp_puppet_peer_t *p, uint32_t record, uint32_t object)
{
    mp_event_t held[MP_EVENT_QUEUE_SLOTS];
    size_t     held_count = 0;
    bool       aux_taken = false;
    bool       waiting = false;
    bool       sabre_held = false;
    bool       weapon_held = false;
    mp_event_t event;
    size_t     index;

    while (mp_event_queue_pop(&p->events, &event)) {
        if (!waiting) {
            mp_puppet_anim_due_t due = mp_puppet_anim_event_due(event.tick, p->render_tick,
                                                                p->render_known);

            waiting = due == MP_PUPPET_ANIM_WAIT;
            if (due == MP_PUPPET_ANIM_LATE) {
                ++puppet.events_late;
            } else if (due == MP_PUPPET_ANIM_FORCED) {
                ++puppet.events_forced;
            }
        }
        if (waiting) {
            held[held_count++] = event;
            continue;
        }
        if (event.kind == MP_EVENT_SHOT) {
            mp_puppet_shot_perform(bank, object, &event);
            continue;
        }
        if (event.kind == MP_EVENT_PLAYER_SOUND) {
            mp_player_sound_due(bank, record, object, &event, p->render_tick, p->render_known);
            continue;
        }
        if (event.kind == MP_EVENT_PUSH &&
            !perform_push(bank, p, record, object, &event, &aux_taken)) {
            held[held_count++] = event;
            continue;
        }
        if (event.kind == MP_EVENT_WEAPON) {
            /* A second change behind a held one is held without being tried, so the wait that
             * drops it counts substeps and not events. */
            if (weapon_held || !perform_weapon(bank, p, record, object, &event, &aux_taken)) {
                weapon_held = true;
                held[held_count++] = event;
            }
            continue;
        }
        if (event.kind == MP_EVENT_SABRE) {
            if (sabre_held ||
                mp_puppet_sabre_perform(bank, record, object, &event, &aux_taken) ==
                    MP_PUPPET_SABRE_HOLD) {
                sabre_held = true;
                held[held_count++] = event;
            }
        }
    }
    for (index = 0; index < held_count; ++index) {
        mp_event_queue_push(&p->events, &held[index]);
    }
}

/* The health from the wire into the puppet's banked status record, and the dead flag's two
 * edges onto the object. Inside the window the bank has placed the puppet's status content
 * into the live record, and the writer lands there; a window without a placed status (outside
 * a level) refuses, which is counted and said once. Never through the engine's setter at
 * 0x00459EA9: that writes the value and the two HUD cells of the local player's bar, so a call
 * in the puppet's window would blink the local HUD with the far player's health. The death clip
 * itself (0x19, 0x1A, 0x1B or 0x2A) arrives on the base channel through the state path; what
 * the rising edge adds is the shadow the retail death takes away, and what the falling edge adds
 * is the shadow back and the collision the death clip's authored event stripped, so the revived
 * far player can be hit and can hit again. The stripping is the draw loop's answer to the clip's
 * parameter event: it calls the collision disable at 0x0041401A, which zeroes the object class at
 * +0x04 and both cylinder words at +0xB8 and +0xBC, and the pair pass skips a class of 0, so
 * after one death the puppet would be invisible to every contact. The owner's respawn allocates
 * a new body on his machine; the puppet keeps its object, which is why the words go back here. */
static void apply_vitals(size_t bank, mp_puppet_peer_t *p, uint32_t object)
{
    uint32_t health = p->sample.state.health;
    bool     dead = p->sample.state.dead;
    uint32_t flags = 0;

    if (health > HEALTH_MAX) {
        health = HEALTH_MAX;
    }
    if (mp_bank_status_write_at(bank, STATUS_HEALTH_OFFSET, health)) {
        ++puppet.health_writes;
        puppet.last_health = health;
    } else {
        ++puppet.health_refused;
        if (!puppet.health_refusal_logged) {
            puppet.health_refusal_logged = true;
            log_warning("the far player's health could not be written into the puppet's status "
                        "record: the bank had no placed status in this window, or the write was "
                        "refused; later refusals are counted");
        }
    }

    if (p->dead_known && dead == p->dead_applied) {
        return;
    }
    if (!p->dead_known && !dead) {
        p->dead_known   = true;   /* alive from the first sample: nothing to put back */
        p->dead_applied = false;
        return;
    }
    /* An object that does not read leaves the edge pending for the next window, so a revival
     * seen while the handle was momentarily unreadable still puts the collision back. */
    if (object == 0u || !memory_try_read_u32(object + OBJECT_FLAGS, &flags)) {
        ++puppet.shadow_faults;
        return;
    }
    p->dead_known   = true;
    p->dead_applied = dead;
    flags = dead ? (flags & ~(uint32_t)OBJECT_CASTS_SHADOW) : (flags | OBJECT_CASTS_SHADOW);
    if (!mp_bank_window_write_u32(object + OBJECT_FLAGS, flags)) {
        ++puppet.shadow_faults;
        note_write_fault("shadow flag");
    }
    if (dead) {
        ++puppet.died;
        log_info("the far player died: the puppet's shadow is off and its death clip comes "
                 "from the wire (death %u)", (unsigned)puppet.died);
    } else {
        ++puppet.revived;
        (void)mp_body_collision_restore_at(bank);   /* says so itself, once, or why not */
        log_info("the far player is back: the puppet's shadow is on again and its collision was "
                 "asked back (revival %u)", (unsigned)puppet.revived);
    }
}

static void place(const mp_puppet_peer_t *p, uint32_t record)
{
    int axis;

    for (axis = 0; axis < 3; ++axis) {
        if (!mp_bank_window_write_f32((uintptr_t)record + RECORD_POS + (unsigned)axis * 4u,
                                      p->sample.position[axis])) {
            note_write_fault("position");
        }
    }
    if (!mp_bank_window_write_f32((uintptr_t)record + RECORD_HEADING, p->sample.yaw)) {
        note_write_fault("heading");
    }
}

void __cdecl mp_puppet_apply_window(void)
{
    size_t            bank = mp_bank_active();   /* the far body this window installed */
    mp_puppet_peer_t *p = peer_of(bank);
    mp_sound_anchor_t anchor;
    uint32_t          record = 0;
    uint32_t          object = 0;

    /* First, ahead of every return below. The node rotations are drawn between two substeps now,
     * and the two ends of that pair are parted only by a value this substep actually produces; a
     * substep that turns back here has to leave them together, or the far body sweeps those few
     * degrees from end to end once per substep for as long as the turning back lasts. This window
     * is entered once per own substep from the first resolved pose onward, so it is also the
     * substep clock the rotations are drawn against. */
    mp_twist_open_substep(bank);

    if (!current_record(&record)) {
        return;
    }
    place(p, record);

    /* Everything below acts for a body that is not the one this machine listens from, and several
     * of the engine routines it calls play a sound with no place in it: the weapon setter ignites
     * and extinguishes the sabre, the swept blade strikes the level, the impact arm grunts. One
     * anchor around the whole window covers them and whatever is added to it later, which is the
     * reason it is here rather than at the four call sites. */
    mp_sound_anchor_open(p->sample.position, &anchor);
    (void)memory_try_read_u32((uintptr_t)record + RECORD_HACTOR, &object);
    if (object != 0) {
        mp_puppet_anim_apply_base(bank, object, &p->sample.state.anim, p->sample.state_tick);
    }
    apply_weapon(bank, p, record, object);
    if (object != 0) {
        mp_puppet_anim_apply_overlay(bank, record, object, &p->sample.state.anim);
        /* The rotations are node indices of the SENDER's rig. They are applied only while this
         * body shows that same rig, which the engine and the overlay's answer decide; on any
         * other rig the same numbers are other joints. */
        if (!mp_body_wear_rig_is_senders(bank)) {
            mp_twist_withhold(bank, object);
        } else {
            mp_twist_apply(bank, object, p->sample.twist, p->sample.twist_count);
        }
    }
    mp_phases_run_puppet();
    /* After the plan has committed, because that is the moment the engine would ask. */
    mp_phases_note_ground(object);
    perform_events(bank, p, record, object);
    mp_puppet_sabre_tick(bank, record, object);
    apply_vitals(bank, p, object);

    /* The swept blade against the level, while the blade is armed. The engine runs this from the
     * swing mode's own tick, which a puppet has no mode to run, so without it a far player's
     * sabre marked bodies and left every wall clean. It goes here, after the plan has committed
     * the record into the object, because the sweep asks where the node stands now. */
    if (mp_puppet_sabre_armed(bank)) {
        mp_effects_sweep_blade(bank, record, object);
    }
    /* Last, so the impact effects are placed against the pose everything above has just
     * committed: the node sphere is resolved from the object as it will be drawn. The node
     * sphere at 0x00414231 rebuilds the object's joint matrices lazily, at most once per
     * substep, keyed on the tick counter against the thing's stamp at +0x1C, so whichever caller
     * asks first in a substep fixes the pose every later caller sees. */
    mp_effects_run_in_window(bank, record, object);
    mp_effects_run_hurt(bank, object);
    /* Last of all, because it asks where the feet stand: the pose above has just been
     * committed into the object, and the floor is probed under the position the wire
     * reported rather than read off a cell a puppet never fills. */
    mp_footstep_run(bank, object, p->sample.state.loco, p->sample.state.hero);

    mp_sound_anchor_close(&anchor);
}
