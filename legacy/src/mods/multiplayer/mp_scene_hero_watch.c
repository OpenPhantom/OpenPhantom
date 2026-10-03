/* mp_scene_hero_watch.c: the hero a scene drives, watched on the host. See the header. */
#include "mp_scene_hero_watch.h"

#include "mp_bank.h"
#include "mp_cells.h"
#include "mp_enemy_bind.h"
#include "mp_scene_bind.h"
#include "mp_scene_hero_rule.h"
#include "mp_scene_host.h"
#include "mp_scene_rule.h"
#include "mp_target.h"

#include "common/logging.h"
#include "common/memory.h"

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <string.h>

/* The actor's fields from its start up to the walk request, read in one piece. */
#define ACTOR_BYTES (MP_CHARACTER_MOVE_REQUESTED + 4u)

/* Milliseconds a substep, for the line's seconds. */
#define MS_A_SUBSTEP 31.25f

/* A put's line this many times in a process, and after that the report's counts alone. */
#define PUT_LINES_MAX 32u

typedef struct hero_watch {
    bool                  hosted;     /* a substep ran here with this side hosting */
    uint32_t              substep;    /* the last one */
    uintptr_t             actor;      /* the actor driving the player's body, nought for none */
    uint32_t              key;        /* its placement index, for the life asked before a write */
    bool                  keyed;
    bool                  searched;      /* the actor was looked for in `searched_at` */
    uint32_t              searched_at;
    mp_scene_hero_clock_t clock;

    uint32_t parked;          /* substeps the player module was parked by the grab */
    uint32_t confirmed;       /* of those, the remembered actor confirmed by two reads */
    uint32_t walks;           /* walks of the actor list to find it */
    uint32_t found;           /* walks that found it */
    uint32_t walking_free;    /* substeps a hero walked while no scene of the host's stood */
    uint32_t puts;
    uint32_t puts_after_detour;
    uint32_t puts_refused;    /* the player not standing, the actor not live, a write refused */
    uint32_t longest_still;
    uint32_t blind;           /* walking substeps with no goal of its own read */
    uint32_t longest_blind;
    uint32_t lost;            /* stretches of 96 of them in a row, each named */
    uint32_t lines;
    uint32_t lost_lines;
    uint32_t askers_sought;   /* walks for the actor that asks the grab */
    uint32_t askers_found;
} hero_watch_t;

static hero_watch_t w;

static void forget(void)
{
    w.actor    = 0u;
    w.keyed    = false;
    w.searched = false;
    mp_scene_hero_rule_reset(&w.clock);
}

/* The player module as the engine's grab leaves it: parked at nought with the running state in its
 * store, and a body. It is the grab half of mp_scene_running, asked with no lock, so the two cannot
 * come to different answers about what a grab is. The grab is player_suspend at 0x00450F25, which
 * writes the store and then nought, and the actor list's tick at 0x00432BF2 asks it for every actor
 * carrying the handover bit before it ticks that actor at all. */
static bool hero_parked(uint32_t *object)
{
    uintptr_t block  = mp_cells_address(MP_CELL_HERO_BLOCK);
    uint32_t  module = 1u;
    uint32_t  saved  = 0u;

    *object = 0u;
    if (block == 0u || mp_bank_active() != 0u ||
        !memory_try_read_u32(block + MP_HERO_BLOCK_MODULE_STATE, &module) ||
        !memory_try_read_u32(block + MP_HERO_BLOCK_SAVED_MODULE_STATE, &saved) ||
        !memory_try_read_u32(block + MP_HERO_BLOCK_OBJECT, object)) {
        *object = 0u;
        return false;
    }
    return mp_scene_running(0, module, saved, *object != 0u);
}

/* Two reads: the actor's flags carry the handover bit, which the spawn copies from its placement's
 * flags, and its body is the player's own object, which only the grab hands an actor. The spawn
 * is spawn_actor at 0x00437250: `stateFlags = rec->flags`, and for the handover bit no body and
 * state 0x10. The grab's second half, enemy_hostOnPlayer at 0x004377EE, sets the actor's body to
 * the live player's object and its position to that body's. */
static bool drives_the_player(uintptr_t actor, uint32_t object)
{
    uint32_t flags = 0u;
    uint32_t body  = 0u;

    return actor != 0u && memory_try_read_u32(actor + MP_CHARACTER_STATE_FLAGS, &flags) &&
           (flags & MP_PLACEMENT_F_HOSTS_PLAYER) != 0u &&
           memory_try_read_u32(actor + MP_CHARACTER_BODY, &body) && body == object;
}

typedef struct hero_search {
    uint32_t  object;
    uintptr_t found;
} hero_search_t;

static void visit(uintptr_t actor, void *user)
{
    hero_search_t *search = (hero_search_t *)user;

    if (search->found == 0u && drives_the_player(actor, search->object)) {
        search->found = actor;
    }
}

/* The whole walk, which the enemy block's census does not count among its own. */
static uintptr_t find_the_hero(uint32_t object)
{
    hero_search_t search;
    bool          complete = false;
    uint32_t      capacity = 0u;

    search.object = object;
    search.found  = 0u;
    ++w.walks;
    (void)mp_enemy_bind_walk_whole(&visit, &search, &complete, &capacity);
    if (search.found != 0u) {
        ++w.found;
    }
    return search.found;
}

static bool read_the_walk(uintptr_t actor, mp_scene_hero_reading_t *reading)
{
    uint8_t  bytes[ACTOR_BYTES];
    uint32_t flags     = 0u;
    int32_t  requested = 0;

    if (!memory_try_read(actor, bytes, sizeof bytes)) {
        return false;
    }
    memcpy(&flags, bytes + MP_CHARACTER_STATE_FLAGS, sizeof flags);
    memcpy(&requested, bytes + MP_CHARACTER_MOVE_REQUESTED, sizeof requested);
    reading->actor          = actor;
    reading->move_requested = requested != 0;
    reading->detour         = (flags & MP_CHARACTER_F_DETOUR) != 0u;
    memcpy(&reading->move_speed, bytes + MP_CHARACTER_MOVE_SPEED, sizeof reading->move_speed);
    memcpy(&reading->move_mode, bytes + MP_CHARACTER_MOVE_MODE, sizeof reading->move_mode);
    memcpy(&reading->waypoint, bytes + MP_CHARACTER_WAYPOINT, sizeof reading->waypoint);
    memcpy(reading->target, bytes + MP_CHARACTER_MOVE_TARGET, sizeof reading->target);
    memcpy(reading->position, bytes + MP_CHARACTER_POS, sizeof reading->position);
    return true;
}

/* Why a stuck walk is not put, or NULL when it may be. The body it moves is the player's own, and
 * the engine's live player test is asked first; then whether the actor is still the placement's
 * live actor, because a remembered address the pool has taken back holds old bytes. */
static const char *why_not_put(void)
{
    if (!mp_scene_bind_player_stands()) {
        return "the player does not stand";
    }
    if (!w.keyed || !mp_enemy_bind_is_live(w.actor, w.key, NULL)) {
        return "the actor is no longer the placement's live one";
    }
    return NULL;
}

/* The stuck walk is put onto its goal: the stored target first, then the position. While a detour
 * is flagged the next walk measures the position against the stored target and clears the flag,
 * and the walk after it resolves the goal itself and arrives; with no detour the next one arrives.
 * The engine's pose commit at the end of that tick writes the player's body from the position.
 * The two words have these readers: the stored target, the walk while a detour or a clear sight is
 * flagged and the height follow of a flying or swimming mode; the position, the floor probe of the
 * next tick, the walk's arrival box, the move step and the pose commit.
 *
 * By address: the walk is move_chaseDrive at 0x00429DF4, which takes the stored target at +0xA0
 * whenever 0x400000 (a detour) or 0x800000 (a clear sight) is set, clears 0x400000 on arriving and
 * returns, and otherwise resolves the goal, stores it and on arriving moves the waypoint on. The
 * height follow is move_trackZ at 0x0042A8DE, called by enemy_postTick at 0x004362C8 for a move
 * mode of 2 and up. The arrival box is ai_pointWithinBox at 0x0042BE45 on the position at +0xD0.
 * The floor probe is in enemy_preTick at 0x00435C67, which also clears the walk request at +0x174
 * before every script run, so a 1 read after the tick means the walk ran in it; enemy_postTick
 * clears both flags when it reads nought there. The pose commit is enemy_commitPose at 0x004333E1,
 * the last call of enemy_postTick, which writes the body's position from the actor's. */
static void put_onto_the_goal(uint16_t scene, const float goal[3])
{
    const char *why = why_not_put();
    bool        detour = w.clock.detour_ran;

    if (why == NULL && (!memory_try_write(w.actor + MP_CHARACTER_MOVE_TARGET, goal,
                                          3u * sizeof(float)) ||
                        !memory_try_write(w.actor + MP_CHARACTER_POS, goal, 3u * sizeof(float)))) {
        why = "a write was refused";
    }
    if (why != NULL) {
        ++w.puts_refused;
    } else {
        ++w.puts;
        w.puts_after_detour += detour ? 1u : 0u;
    }
    if (w.lines >= PUT_LINES_MAX) {
        return;
    }
    ++w.lines;
    if (why != NULL) {
        log_warning("the hero of scene %u stood still for %u substep(s) on its way to %.2f %.2f "
                    "%.2f, and was not put onto it: %s", (unsigned)scene,
                    (unsigned)MP_SCENE_HERO_STILL_SUBSTEPS, (double)goal[0], (double)goal[1],
                    (double)goal[2], why);
        return;
    }
    log_info("the hero of scene %u stood still for %u substep(s) (%.1f s) on its way to %.2f %.2f "
             "%.2f (%.2f u off, the best %.2f, a detour ran: %s): put onto it", (unsigned)scene,
             (unsigned)MP_SCENE_HERO_STILL_SUBSTEPS,
             (double)((float)MP_SCENE_HERO_STILL_SUBSTEPS * MS_A_SUBSTEP / 1000.0f),
             (double)goal[0], (double)goal[1], (double)goal[2], (double)w.clock.distance,
             (double)w.clock.best, detour ? "yes" : "no");
}

/* A walk that has gone round detour after detour without once reading its own goal: there is no
 * goal to put it on, and none is guessed. Said with the same budget as a put. */
static void say_lost(uint16_t scene)
{
    ++w.lost;
    if (w.lost_lines >= PUT_LINES_MAX) {
        return;
    }
    ++w.lost_lines;
    log_warning("the hero of scene %u walked for %u substep(s) with no goal of its own read "
                "(detour after detour), so it was not put anywhere", (unsigned)scene,
                (unsigned)MP_SCENE_HERO_STILL_SUBSTEPS);
}

/* The actor remembered from the last substep, confirmed; otherwise found again. The two reads
 * alone would confirm a slot the pool has taken back, whose bytes still stand, so the remembered
 * actor is also asked whether it is its placement's live one. */
static bool know_the_hero(uint32_t object, uint32_t substep)
{
    w.searched    = true;
    w.searched_at = substep;
    if (drives_the_player(w.actor, object) && w.keyed &&
        mp_enemy_bind_is_live(w.actor, w.key, NULL)) {
        ++w.confirmed;
        return true;
    }
    w.actor = find_the_hero(object);
    w.keyed = w.actor != 0u && mp_enemy_bind_index(w.actor, &w.key);
    return w.actor != 0u;
}

void mp_scene_hero_watch_tick(uint32_t substep, uint16_t scene_serial)
{
    mp_scene_hero_reading_t reading;
    mp_scene_hero_verdict_t verdict;
    float                   goal[3] = { 0.0f, 0.0f, 0.0f };
    uint32_t                object  = 0u;

    w.substep = substep;
    if (!mp_target_hosting()) {
        forget();
        return;
    }
    w.hosted = true;
    if (!hero_parked(&object)) {
        forget();
        return;
    }
    ++w.parked;
    if (!know_the_hero(object, substep) || !read_the_walk(w.actor, &reading)) {
        (void)mp_scene_hero_rule_step(&w.clock, substep, NULL, NULL);
        return;
    }
    verdict = mp_scene_hero_rule_step(&w.clock, substep, &reading, goal);
    if (w.clock.longest_still > w.longest_still) {
        w.longest_still = w.clock.longest_still;
    }
    if (w.clock.longest_blind > w.longest_blind) {
        w.longest_blind = w.clock.longest_blind;
    }
    if (verdict == MP_SCENE_HERO_BLIND || verdict == MP_SCENE_HERO_LOST) {
        ++w.blind;
    }
    if (verdict == MP_SCENE_HERO_LOST) {
        say_lost(scene_serial);
    }
    if (mp_scene_hero_rule_walked_lately(&w.clock, substep) && !mp_scene_host_stands(NULL)) {
        ++w.walking_free;
    }
    if (verdict == MP_SCENE_HERO_PUT) {
        put_onto_the_goal(scene_serial, goal);
    }
}

uintptr_t mp_scene_hero_watch_driver(uint32_t substep)
{
    uint32_t object = 0u;

    if (!hero_parked(&object)) {
        /* Nothing drives a module that is not parked. On a client the tick above never gets
         * past its first question, so this is where the remembered actor is let go there: the
         * release of what a scene holds removes it, its pool slot keeps its bytes, and the two
         * reads would confirm it again at the next level's restore. */
        forget();
        return 0u;
    }
    if (!w.searched || w.searched_at != substep) {
        (void)know_the_hero(object, substep);
    }
    return w.actor;
}

bool mp_scene_hero_watch_walking(void)
{
    return mp_scene_hero_rule_walked_lately(&w.clock, w.substep);
}

/* The actor list's header keeps the walk's cursor at +0x08, and a slot begins with its link word,
 * four bytes in front of the actor. */
#define LIST_CURSOR_OFFSET 0x08u
#define ACTOR_LINK_BEHIND  4u

typedef struct asker_search {
    uint32_t  cursor;
    uintptr_t found;
} asker_search_t;

static void visit_for_the_asker(uintptr_t actor, void *user)
{
    asker_search_t *search = (asker_search_t *)user;
    uint32_t        link   = 0u;

    if (search->found == 0u && memory_try_read_u32(actor - ACTOR_LINK_BEHIND, &link) &&
        link == search->cursor) {
        search->found = actor;
    }
}

/* The engine's grab takes no argument, so who asks is read off the walk. The actor list's tick,
 * enemy_tickAll at 0x00432BF2, takes each actor from list_next at 0x0046EABF, which stores the
 * actor's own link word into the list's cursor before it hands the actor out, and asks the grab
 * at 0x00432CAE with nothing in between but a counter, a read of the actor's state and the test
 * of its handover bit. So the actor that asks is the one whose link word equals the cursor, the
 * last of the chain with a cursor of nought. The chain is walked by hand, as every walk of this
 * feature is, and never through the engine's iterator. */
uintptr_t mp_scene_hero_watch_asker(void)
{
    uintptr_t      cell = mp_cells_address(MP_CELL_ENEMY_POOL);
    uint32_t       list = 0u;
    asker_search_t search;
    bool           complete = false;
    uint32_t       capacity = 0u;

    search.cursor = 0u;
    search.found  = 0u;
    if (cell == 0u || !memory_try_read_u32(cell, &list) || list == 0u ||
        !memory_try_read_u32((uintptr_t)list + LIST_CURSOR_OFFSET, &search.cursor)) {
        return 0u;
    }
    ++w.askers_sought;
    (void)mp_enemy_bind_walk_whole(&visit_for_the_asker, &search, &complete, &capacity);
    if (search.found != 0u) {
        ++w.askers_found;
    }
    return search.found;
}

void mp_scene_hero_watch_report(void)
{
    if (!w.hosted) {
        log_info("  the hero watch: idle, this side hosted no substep");
        return;
    }
    log_info("  the hero watch (the host): %u put onto their target (%u after a detour), the "
             "longest stand-still %u substep(s); %u put(s) refused (the player not standing, the "
             "actor not live or a write refused)", (unsigned)w.puts, (unsigned)w.puts_after_detour,
             (unsigned)w.longest_still, (unsigned)w.puts_refused);
    log_info("  the hero watch's search (the host): %u substep(s) with the player parked by a "
             "grab, %u of them on an actor confirmed by two reads, %u walk(s) of the actor list, "
             "%u of them found it; %u substep(s) a driven hero walked while no scene of the "
             "host's stood; %u walking substep(s) with no goal of its own read, the longest "
             "stretch %u, %u stretch(es) of 96 named",
             (unsigned)w.parked, (unsigned)w.confirmed, (unsigned)w.walks, (unsigned)w.found,
             (unsigned)w.walking_free, (unsigned)w.blind, (unsigned)w.longest_blind,
             (unsigned)w.lost);
    log_info("  the hero watch's look for who asks the grab (the host): %u walk(s) of the actor "
             "list while a placement was written down, %u of them found the actor the list's "
             "cursor stands behind", (unsigned)w.askers_sought, (unsigned)w.askers_found);
}
