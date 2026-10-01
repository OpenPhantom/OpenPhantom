/* spawn_mode.c: see spawn_mode.h. */
#include "spawn_mode.h"

#include "character_prop.h"
#include "character_prop_body.h"
#include "input_owner.h"
#include "npc_spawn_describe.h"
#include "npc_spawn_link.h"
#include "npc_spawn_node.h"
#include "npc_spawner.h"
#include "overlay_input.h"
#include "overlay_notice.h"
#include "player_slot.h"
#include "session_lock.h"
#include "spawn_banner.h"
#include "spawn_ghost.h"
#include "spawn_keys.h"
#include "spawn_look.h"
#include "spawn_place.h"
#include "spawn_reason.h"
#include "world_camera.h"
#include "world_probe.h"

#include "common/logging.h"
#include "common/memory.h"
#include "common/text.h"

#include <windows.h>

#include <stdbool.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

/* The window messages and keys this reads, spelled as the engine and the panel compare them. */
#define MSG_LEFT_BUTTON     0x0201
#define MSG_RIGHT_BUTTON    0x0204
#define MSG_RIGHT_BUTTON_UP 0x0205
#define MSG_MIDDLE_BUTTON   0x0207
#define KEY_ESCAPE          0x1B
#define WHEEL_NOTCH         120

/* The player record's dead flag: the death entry at 0x004500B0 loads the record out of the cell
 * player_slot.c reads (0x004500C2, mov ecx,[0x004B5220]) and writes 1 here (0x004500C8,
 * mov dword [ecx+0x394],1); nothing clears it in place. */
#define PLAYER_DEAD         0x394u

typedef struct mode_counters {
    uint32_t placed;
    uint32_t place_refused;
    uint32_t removed;
    uint32_t remove_refused;
    uint32_t notches;
} mode_counters_t;

static struct {
    bool              draw_armed;
    bool              draw_said;
    bool              waiting_said;
    spawn_ghost_ops_t ops;
    bool              ghost_drawn;    /* in this frame's world pass */
    bool              have_scene;
    spawn_look_t      look;           /* what this frame's look found */
    int32_t           wheel_rest;     /* notches not yet whole, from a fine wheel */
    mode_counters_t   counters;
    /* What the mode says about itself at the top of the picture. The farewell outlives the mode
     * by a couple of seconds, which is the whole point of it: a player who clicked once more on
     * the way out is owed the tally of what that click did. */
    char              farewell[96];
    uint32_t          farewell_ms;
    bool              said;           /* the state of the group has been said once */
    uint32_t          said_world;     /* the world it was said in */
    const char       *said_why;       /* and the answer it gave, compared by pointer because
                                       * spawn_reason_why answers with literals of its own */
} mode;

/* ============================================================================================
 * The ghost's draw path.
 * ============================================================================================ */

/* Called for every object the world pass dispatches, inside the borrowed weapon's lock. The ghost
 * is drawn once a frame, right before the player's own body: a place in the world pass every frame
 * that shows the player has, where the dispatcher's globals are the world's. */
static void before_thing_draw(const void *thing, const float *root)
{
    (void)root;
    if (mode.look.player_thing == 0 || (uintptr_t)thing != mode.look.player_thing ||
        mode.ghost_drawn) {
        return;
    }
    mode.ghost_drawn = true;
    (void)spawn_ghost_draw(spawn_ghost_state(), &mode.ops, npc_spawn_node_epoch());
}

/* Resolve first, then hook: the borrowed weapon's patterns are cut from untouched bytes and one of
 * them is the prologue the hook writes over. Without it the mode still places; the marks show where
 * the entity would stand. */
static void arm_the_draw_path(void)
{
    const character_prop_sites_t *sites;

    if (mode.draw_armed) {
        return;
    }
    sites = character_prop_sites();
    if (sites == NULL || !character_prop_draw_install()) {
        if (!mode.draw_said) {
            mode.draw_said = true;
            log_warning("npc spawner: the ghost cannot be drawn, the render handle's entry points "
                        "or the draw hook did not resolve; the marks still show the place");
        }
        return;
    }
    mode.ops.init        = sites->thing_init;
    mode.ops.set_model   = sites->set_model;
    mode.ops.free_arrays = sites->free_arrays;
    mode.ops.draw        = sites->thing_draw;
    spawn_ghost_set_ops(&mode.ops);
    character_prop_draw_set_extra(&before_thing_draw);
    mode.draw_armed = true;
    log_info("npc spawner: the ghost is drawn before the player's body, through the borrowed "
             "weapon's draw hook, with a render handle of its own");
}

/* ============================================================================================
 * The facts the mode's state reads, and what it says about them.
 * ============================================================================================ */

static bool chosen_kind(npc_spawner_kind_t *out)
{
    int32_t chosen = npc_spawner_chosen();

    return chosen >= 0 && npc_spawner_kind((uint32_t)chosen, out);
}

/* Everything the rule reads. The four the panel's group also reads are written by
 * spawn_reason.c, which is the only copy of that list; the three below it are the mode's own,
 * and only SPAWN_REASON_PLACE looks at them. */
static void gather(spawn_reason_facts_t *facts)
{
    spawn_reason_group_facts(facts, session_lock_holds_the_spawner(), npc_spawn_link_active(),
                             npc_spawner_is_available(), npc_spawner_kind_count());
    facts->builder = npc_spawner_can_raise();
    facts->camera  = world_camera_available();
    facts->chosen  = npc_spawner_chosen() >= 0;
}

/* The state of the group, once a world and once per answer, with the numbers that decided it. A
 * run that ends with the panel greyed is read off this line and nothing else: the two that matter
 * are the session's cap and this machine's world slot, since a grant record carrying neither reads
 * as a session that runs no copies. Success and failure are different sentences. */
static void say_the_state(const spawn_reason_facts_t *facts, const char *why, uint32_t world)
{
    npc_spawn_link_facts_t link;

    if (mode.said && mode.said_world == world && mode.said_why == why) {
        return;
    }
    mode.said       = true;
    mode.said_world = world;
    mode.said_why   = why;
    npc_spawn_link_facts(&link);
    log_info("npc spawner: the spawn group is %s in this world: %s; a session runs %u and runs "
             "the copies %u, its grant record was read %u, cap %u, world slot %u, epoch %u; a "
             "level with a player %u, a copy can be raised here %u, %u kind(s) offered, %u alive",
             why == NULL ? "usable" : "not usable",
             why == NULL ? "nothing stands in the way" : why,
             session_lock_running() ? 1u : 0u, facts->session ? 1u : 0u, link.read ? 1u : 0u,
             link.cap, link.own_slot, link.epoch, facts->level ? 1u : 0u,
             facts->builder ? 1u : 0u, facts->kinds, npc_spawner_alive());
}

/* Why the mode cannot be on, in the order spawn_place_frame asks: the panel, the player, his
 * death, then what the engine side said. NULL when nothing stands in the way. */
static const char *why_not(const spawn_place_facts_t *facts, const char *unavailable)
{
    if (!facts->panel_open) {
        return "the panel is not open";
    }
    if (!facts->player) {
        return "no player";
    }
    if (facts->player_dead) {
        return "the player is dead";
    }
    return facts->available ? NULL : unavailable;
}

static const char *off_word(spawn_place_off_t why, const char *unavailable)
{
    switch (why) {
    case SPAWN_PLACE_OFF_ASKED:       return "asked";
    case SPAWN_PLACE_OFF_PANEL:       return "the panel was shut";
    case SPAWN_PLACE_OFF_WORLD:       return "the world changed";
    case SPAWN_PLACE_OFF_PLAYER:      return "no player";
    case SPAWN_PLACE_OFF_DIED:        return "the player died";
    case SPAWN_PLACE_OFF_UNAVAILABLE: return unavailable != NULL ? unavailable : "unavailable";
    case SPAWN_PLACE_STILL_ON:
    default:                          return "?";
    }
}

static void say_the_step(const spawn_place_step_t *step, const char *why)
{
    npc_spawner_kind_t kind;
    char               label[ENTITY_NAME_MAX + 1u];

    if (step->entered) {
        memset(&mode.counters, 0, sizeof mode.counters);
        memset(&spawn_ghost_state()->counters, 0, sizeof spawn_ghost_state()->counters);
        /* Notches the wheel gave the game before the mode had it are not a turn. */
        (void)input_owner_take_wheel();
        mode.wheel_rest = 0;
        /* A refusal of before, "could not start" among them, says nothing about this life. */
        npc_spawn_link_forget_refusal();
        arm_the_draw_path();
        label[0] = '\0';
        if (chosen_kind(&kind)) {
            spawn_look_label(&kind, label, sizeof label);
        }
        log_info("npc spawner: the placement mode is on: %s; the panel hides and the pointer is "
                 "free, %s", label,
                 session_lock_panel_may_pause()
                     ? "and the world is held as under the panel, running two substeps after "
                       "each copy placed"
                     : "and the world runs, as it does in every session");
    } else if (step->left != SPAWN_PLACE_STILL_ON) {
        spawn_ghost_let_go();
        spawn_banner_ended(mode.farewell, sizeof mode.farewell, mode.counters.placed,
                           mode.counters.removed,
                           mode.counters.place_refused + mode.counters.remove_refused);
        mode.farewell_ms = (uint32_t)GetTickCount();
        log_info("npc spawner: the placement mode is off: %s; it placed %u, refused %u click(s), "
                 "removed %u, refused %u removal(s), turned %u notch(es); the ghost was bound %u "
                 "time(s), drawn %u, culled %u, refused %u", off_word(step->left, why),
                 mode.counters.placed, mode.counters.place_refused, mode.counters.removed,
                 mode.counters.remove_refused, mode.counters.notches,
                 spawn_ghost_state()->counters.binds, spawn_ghost_state()->counters.draws,
                 spawn_ghost_state()->counters.culled, spawn_ghost_state()->counters.refused);
    } else if (step->refused) {
        npc_spawn_link_facts_t link;

        /* A refusal always has its reason from why_not; the words below are only for a frame
         * that has none, which would be a new refusal nobody named. The numbers behind the
         * session are on the line as well, so a refusal that names the session can be checked
         * against what the multiplayer published rather than believed. */
        npc_spawn_link_facts(&link);
        log_info("npc spawner: the placement mode could not start: %s; a session runs %u and runs "
                 "the copies %u, its grant record was read %u, cap %u, world slot %u, epoch %u",
                 why != NULL ? why : "for a reason not named", session_lock_running() ? 1u : 0u,
                 link.active ? 1u : 0u, link.read ? 1u : 0u, link.cap, link.own_slot, link.epoch);
        npc_spawn_link_refuse_here(why != NULL ? why : "the placement mode could not start");
        /* And in the band, because the key that asks for the mode is pressed with the panel
         * shut as often as open: without this the press answered only in the log, and the
         * reason it names is usually one the player can act on. */
        {
            char said[OVERLAY_NOTICE_MAX];

            text_format(said, sizeof said, "Refused: %s",
                        why != NULL ? why : "the placement mode could not start");
            overlay_notice_say(said);
        }
    }
}

/* ============================================================================================
 * What the clicks and the wheel asked for.
 * ============================================================================================ */

static void turn(void)
{
    int32_t delta = input_owner_take_wheel() + mode.wheel_rest;
    int32_t notches = delta / WHEEL_NOTCH;

    mode.wheel_rest = delta - notches * WHEEL_NOTCH;
    if (notches == 0) {
        return;
    }
    spawn_place_turn(spawn_place_state(), notches, (GetAsyncKeyState(VK_SHIFT) & 0x8000) != 0,
                     (GetAsyncKeyState(VK_CONTROL) & 0x8000) != 0, mode.look.facing);
    mode.counters.notches += (uint32_t)abs(notches);
}

/* A click in a single player game clears the refusal row first, as the row's own spawn does, and
 * says why when it raises nothing and nothing else said; a session's refusals are its link's. */
static void place_one(void)
{
    npc_spawn_desc_t desc;
    bool             single = !npc_spawn_link_active();
    bool             raised;

    if (single) {
        npc_spawn_link_forget_refusal();
    }
    if (!mode.look.camera || mode.look.verdict != SPAWN_SPOT_OK) {
        ++mode.counters.place_refused;
        log_info("npc spawner: the click places nothing: %s (struck at %.2f)",
                 mode.look.camera ? spawn_place_spot_word(mode.look.verdict)
                                  : "the camera cannot be read",
                 (double)mode.look.strike);
        return;
    }
    if (!npc_spawn_describe_at(mode.look.spot, mode.look.facing, &desc)) {
        ++mode.counters.place_refused;
        log_info("npc spawner: the click places nothing: the chosen entity cannot be described");
        return;
    }
    raised = single ? npc_spawner_raise_described(&desc) : npc_spawn_link_spawn_described(&desc);
    if (!raised) {
        ++mode.counters.place_refused;
        log_info("npc spawner: the click places nothing: the spawner refused %s, see above",
                 desc.file);
        if (single && npc_spawn_link_refusal() == NULL) {
            npc_spawn_link_refuse_here("nothing was raised, see the log");
        }
        return;
    }
    ++mode.counters.placed;
    log_info("npc spawner: the click places %s at %.1f %.1f %.1f facing %.0f (struck at %.2f, "
             "moved %.2f off a wall)", desc.file, (double)desc.position[0],
             (double)desc.position[1], (double)desc.position[2], (double)desc.facing,
             (double)mode.look.strike, (double)mode.look.moved);
    if (session_lock_panel_may_pause()) {
        spawn_place_settle_begin(spawn_place_state(), npc_spawn_node_standing(),
                                 npc_spawn_node_substeps(), (uint32_t)GetTickCount());
    }
}

/* A single copy goes through the engine's own delete, here, in a single player game and on the
 * host of a session, whose copies are all its own to remove. The session's census sees it gone and
 * the host's enemy block takes it off every client, as it does for every copy the host removes. A
 * client has no way yet to ask its host for one copy, which would be a new wish on the wire. */
static void remove_one(void)
{
    switch (spawn_place_remove_verdict(mode.look.hover.actor != 0,
                                       npc_spawn_link_active() && npc_spawn_link_is_client(),
                                       mode.look.hover.ridden)) {
    case SPAWN_REMOVE_NOTHING:
        ++mode.counters.remove_refused;
        log_info("npc spawner: the right click removes nothing: no copy under the pointer");
        return;
    case SPAWN_REMOVE_CLIENT:
        ++mode.counters.remove_refused;
        log_info("npc spawner: the right click removes nothing: %s (%u) is %s, and a client cannot "
                 "yet ask its host to remove a single copy", mode.look.hover.name,
                 mode.look.hover.key, mode.look.hover.mine ? "this player's" : "another player's");
        return;
    case SPAWN_REMOVE_RIDDEN:
        ++mode.counters.remove_refused;
        log_info("npc spawner: the right click removes nothing: the player rides %s (%u)",
                 mode.look.hover.name, mode.look.hover.key);
        return;
    case SPAWN_REMOVE_DELETE:
    default:
        break;
    }
    if (!npc_spawner_delete(mode.look.hover.actor)) {
        ++mode.counters.remove_refused;
        log_info("npc spawner: the right click removes nothing: %s (%u) could not be deleted, no "
                 "delete or a save window is open", mode.look.hover.name, mode.look.hover.key);
        return;
    }
    ++mode.counters.removed;
    log_info("npc spawner: the right click removes %s (%u)", mode.look.hover.name,
             mode.look.hover.key);
    memset(&mode.look.hover, 0, sizeof mode.look.hover);
}

/* The settle after a copy placed in a single player game: how it ended goes in the log, so a field
 * run shows the world ran and for how long. */
static void follow_the_settle(spawn_place_t *place)
{
    uint32_t       now   = (uint32_t)GetTickCount();
    uint32_t       taken = now - place->settle_ms;
    uint32_t       ran   = 0;
    spawn_settle_t settle;

    settle = spawn_place_settle_tick(place, npc_spawn_node_standing(), npc_spawn_node_substeps(),
                                     now, &ran);
    if (settle == SPAWN_SETTLE_DONE) {
        log_info("npc spawner: the world ran %u substep(s) in %u ms for the copy placed, and is "
                 "held again", ran, taken);
    } else if (settle == SPAWN_SETTLE_TIMED_OUT) {
        log_info("npc spawner: the world ran %u substep(s) in %u ms for the copy placed, fewer "
                 "than %u, and is held again", ran, taken, SPAWN_PLACE_SETTLE_SUBSTEPS);
    }
}

/* ============================================================================================ */

void spawn_mode_install(void)
{
    world_camera_resolve();
    world_probe_resolve();
    spawn_keys_load();
}

void spawn_mode_frame(void)
{
    spawn_place_t       *place = spawn_place_state();
    spawn_place_facts_t  facts;
    spawn_reason_facts_t reason;
    spawn_place_step_t   step;
    npc_spawner_kind_t   kind;
    const char          *why = NULL;
    const uint8_t       *player;
    uint32_t             dead = 0;

    mode.have_scene        = false;
    mode.look.player_thing = 0;
    mode.ghost_drawn       = false;
    /* With the panel shut and the mode off there is nothing to decide, and nothing of the spawner
     * is asked: the level it would count is none of this frame's business. */
    if (!overlay_input_is_open() && !place->on) {
        place->available   = false;
        place->unavailable = NULL;
        place->asked_on    = false;
        return;
    }
    player            = (const uint8_t *)player_slot_current();
    facts.panel_open  = overlay_input_is_open();
    facts.player      = player != NULL;
    facts.player_dead = player != NULL &&
                        memory_try_read((uintptr_t)player + PLAYER_DEAD, &dead, sizeof dead) &&
                        dead != 0u;
    facts.world       = npc_spawn_node_epoch();
    gather(&reason);
    why               = spawn_reason_why(&reason, SPAWN_REASON_PLACE);
    facts.available   = why == NULL;
    say_the_state(&reason, why, facts.world);
    why               = why_not(&facts, why);
    facts.unavailable = why;
    step = spawn_place_frame(place, &facts);
    say_the_step(&step, why);
    follow_the_settle(place);
    if (!place->on) {
        return;
    }
    if (input_owner_now() != INPUT_OWNER_PLACEMENT) {
        spawn_ghost_want_none(spawn_ghost_state());
        if (!mode.waiting_said) {
            mode.waiting_said = true;
            log_info("npc spawner: the placement mode waits: the free camera has the mouse");
        }
        return;
    }
    if (mode.waiting_said) {
        mode.waiting_said = false;
        log_info("npc spawner: the placement mode goes on: the free camera let go of the mouse");
    }
    if (!chosen_kind(&kind)) {
        return;
    }
    spawn_look_at(&mode.look, &kind, npc_spawner_chosen());
    turn();
    /* The right click first: a copy raised by a click in the same frame may cull a corpse to make
     * room, and the copy the look found under the pointer could be that one. */
    if (spawn_place_take_remove(place)) {
        remove_one();
    }
    if (spawn_place_take_click(place, (uint32_t)GetTickCount())) {
        place_one();
    }
    mode.have_scene = true;
}

void spawn_mode_draw(void)
{
    if (mode.have_scene) {
        spawn_marks_draw(&mode.look.scene);
    }
}

/* How long the tally stands after the mode ends. Long enough to read one short line and short
 * enough that it is gone before the panel is used for anything else. */
#define FAREWELL_MS 2000u

void spawn_mode_banner(void)
{
    const spawn_place_t *place = spawn_place_state();
    npc_spawner_kind_t   kind;
    char                 label[ENTITY_NAME_MAX + 1u];
    char                 line[96];

    if (place->on) {
        /* The mode is on and standing still, because the free camera has the pointer. The log has
         * said so since the mode was written and the picture said nothing at all. */
        if (input_owner_now() != INPUT_OWNER_PLACEMENT) {
            spawn_marks_banner(spawn_banner_waiting());
            return;
        }
        label[0] = '\0';
        if (chosen_kind(&kind)) {
            spawn_look_label(&kind, label, sizeof label);
        }
        /* The session's cap when there is one, and this machine's own otherwise, which is the
         * same choice the line under the pointer already makes. */
        spawn_banner_placing(line, sizeof line, label, npc_spawner_alive(),
                             npc_spawn_link_active() ? npc_spawn_link_cap()
                                                     : NPC_SPAWNER_ALIVE_MAX);
        spawn_marks_banner(line);
        return;
    }
    if (mode.farewell[0] != '\0') {
        if ((uint32_t)GetTickCount() - mode.farewell_ms < FAREWELL_MS) {
            spawn_marks_banner(mode.farewell);
        } else {
            mode.farewell[0] = '\0';
        }
    }
}

bool spawn_mode_key(int32_t virtual_key)
{
    input_owner_t owner = input_owner_now();

    if (owner == INPUT_OWNER_PLACEMENT) {
        if (virtual_key == KEY_ESCAPE || spawn_keys_is(SPAWN_KEY_PLACE, virtual_key)) {
            spawn_place_ask(spawn_place_state(), false);
            return true;
        }
        if (spawn_keys_is(SPAWN_KEY_FACE, virtual_key)) {
            spawn_place_face_player(spawn_place_state());
            return true;
        }
        return false;
    }
    if (owner == INPUT_OWNER_PANEL && spawn_keys_is(SPAWN_KEY_PLACE, virtual_key)) {
        spawn_place_ask(spawn_place_state(), true);
        return true;
    }
    return false;
}

bool spawn_mode_button(int32_t message)
{
    if (input_owner_now() != INPUT_OWNER_PLACEMENT) {
        return false;
    }
    switch (message) {
    case MSG_LEFT_BUTTON:
        spawn_place_ask_click(spawn_place_state());
        return true;
    case MSG_RIGHT_BUTTON:
        spawn_place_ask_remove(spawn_place_state());
        return true;
    case MSG_MIDDLE_BUTTON:
        spawn_place_face_player(spawn_place_state());
        return true;
    case MSG_RIGHT_BUTTON_UP:
        return true;
    default:
        return false;
    }
}

bool spawn_mode_opens_on(int32_t virtual_key)
{
    return spawn_keys_is(SPAWN_KEY_PLACE, virtual_key);
}

void spawn_mode_ask(bool on)
{
    spawn_place_ask(spawn_place_state(), on);
}
