/* npc_spawn_link.c: see npc_spawn_link.h. */
#include "npc_spawn_link.h"

#include "actor_loader.h"
#include "npc_spawn_describe.h"
#include "npc_spawn_list.h"
#include "npc_spawn_node.h"
#include "npc_spawn_save.h"
#include "npc_spawn_session.h"
#include "npc_spawner.h"

#include "common/logging.h"
#include "common/memory.h"
#include "common/npc_spawn_note.h"
#include "common/text.h"

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <string.h>

#define ACTOR_KEY 0x18u   /* the actor's index, 256 + k for a copy */

static npc_spawn_session_t link_session;
static bool                link_ready;
static bool                link_fresh;    /* the grant record was read this frame */
static uint32_t            link_epochs;   /* the session's epoch count last said in the log */
static const char         *link_refusal;  /* the last wish's refusal, for the group's row */

/* The refusals decided here, on this machine and before any wish: no room ahead, the single player
 * cap, a copy the engine would not raise. Until 2026-09-21 they stood in the log alone, with the
 * five numbers behind them, while the row under the keys stayed empty, because that row read only
 * the session's refusal and outside a session there is none. For the player there is one question,
 * why nothing appeared, so these take the same row. A copy of the text, since the callers build it;
 * forgotten at the next spawn and when the world changes, as a session's refusal is. */
static char     local_refusal[48];
static uint32_t local_world;

/* The flyers' anchors, as last read: a refused read keeps the last good record, whose epoch says
 * whether it still belongs to this world. */
static npc_spawn_anchor_record_t link_anchors;
static bool                      link_anchors_read;
static uint32_t                  flyers_moved;   /* frame-records put over an owner's anchor */
static uint32_t                  flyers_kept;    /* left where they were, no anchor for them */
static uint32_t                  flyers_local;   /* over the local player, as outside a session */

static uint8_t panel_flags(void)
{
    uint8_t flags = 0;

    if (npc_spawner_can_raise()) {
        flags |= (uint8_t)NPC_SPAWN_PANEL_BUILDER;
    }
    if (npc_spawn_save_keeps_copies_out()) {
        flags |= (uint8_t)NPC_SPAWN_PANEL_SAVE_HULL;
    }
    if (actor_loader_is_available()) {
        flags |= (uint8_t)NPC_SPAWN_PANEL_ARCHIVE;
    }
    if (npc_spawn_save_can_refuse()) {
        flags |= (uint8_t)NPC_SPAWN_PANEL_TRIPOD_LOCK;
    }
    return flags;
}

/* One walk of the pool: whether an actor carries `key`, which one, and how many stand. */
typedef struct pool_look {
    uint32_t  key;
    uintptr_t actor;
    uint32_t  count;
} pool_look_t;

static void look(uintptr_t actor, void *user)
{
    pool_look_t *seen = (pool_look_t *)user;
    uint32_t     key  = 0;

    ++seen->count;
    if (seen->actor == 0 && memory_try_read_u32(actor + ACTOR_KEY, &key) && key == seen->key) {
        seen->actor = actor;
    }
}

static bool look_at_the_pool(uint32_t key, pool_look_t *seen)
{
    uintptr_t list = npc_spawn_save_pool_list();

    memset(seen, 0, sizeof *seen);
    seen->key = key;
    return list != 0 && npc_spawn_list_each(list, NPC_SPAWN_POOL_CAPACITY, &look, seen);
}

/* What was built from a grant this frame, said once the frame is over. Every refusal names its
 * reason, and until now a grant that BUILT wrote nothing of its own: in the field a wish that
 * worked looked exactly like a wish that never left, which is the one shape this project does not
 * ship. The line waits for the end of the frame because the owner, the player who ordered the
 * copy, is written into the session's table only after the builder has returned. */
#define LINK_BUILT_LINES 8u   /* the first this many a world; after that the epoch line counts */

typedef struct link_built {
    uint32_t key;
    char     file[NPC_SPAWN_FILE_MAX + 1u];
    float    position[3];
    bool     saved;
} link_built_t;

static link_built_t built_here[NPC_SPAWN_GRANT_SLOTS];
static uint32_t     built_count;    /* this frame */
static uint32_t     built_said;     /* lines written in this world */
static uint32_t     built_world;    /* and which world that was */
static bool         built_quieted;  /* the cap was announced once */

static void note_the_build(const npc_spawn_desc_t *desc, uint32_t key, bool saved)
{
    link_built_t *one;

    if (built_count >= NPC_SPAWN_GRANT_SLOTS) {
        return;   /* a frame cannot answer more grants than the record holds */
    }
    one = &built_here[built_count++];
    one->key   = key;
    one->saved = saved;
    text_format(one->file, sizeof one->file, "%s", desc->file);
    memcpy(one->position, desc->position, sizeof one->position);
}

static void say_what_was_built(void)
{
    uint32_t i;

    if (built_world != (uint32_t)link_session.epoch) {
        built_world   = (uint32_t)link_session.epoch;
        built_said    = 0u;
        built_quieted = false;
    }
    for (i = 0; i < built_count; ++i) {
        const link_built_t *one   = &built_here[i];
        uint8_t             owner = 0;
        bool                known;

        if (built_said >= LINK_BUILT_LINES) {
            if (!built_quieted) {
                built_quieted = true;
                log_info("npc spawner: the copies built here are no longer named one by one in "
                         "this world; the epoch line counts the rest");
            }
            break;
        }
        ++built_said;
        /* World slot 255 on the line means the session holds no life under that key any more, so
         * nobody can be named: a cancel that arrived in the same frame, or a generation of 0. It
         * is printed rather than hidden, because a copy standing with no owner is worth seeing. */
        known = npc_spawn_session_owner(&link_session, one->key - NPC_SPAWN_KEY_FIRST, &owner);
        log_info("npc spawner: the grant of %u built %s for world slot %u at %.1f %.1f %.1f%s, "
                 "%u built here in this world", one->key, one->file,
                 known ? (unsigned)owner : 0xFFu, (double)one->position[0],
                 (double)one->position[1], (double)one->position[2],
                 one->saved ? ", as saved" : "", built_said);
    }
    built_count = 0u;
}

/* A granted copy is built only on a key no actor carries, a corpse included, and only while the
 * pool keeps its reserve for the level: two machines' pools are not one pool, so every builder
 * counts again. */
static npc_spawn_outcome_t build(void *user, const npc_spawn_desc_t *desc, uint32_t key,
                                 const npc_spawn_saved_t *saved)
{
    pool_look_t seen;

    (void)user;
    if (!look_at_the_pool(key, &seen)) {
        return NPC_SPAWN_OUTCOME_REFUSED;   /* a pool this build cannot read builds nothing */
    }
    if (seen.actor != 0) {
        log_warning("npc spawner: the grant of %u is refused, since an actor carries that key here",
                    key);
        return NPC_SPAWN_OUTCOME_REFUSED;
    }
    if (seen.count + 1u + NPC_SPAWN_POOL_RESERVE > NPC_SPAWN_POOL_CAPACITY) {
        log_warning("npc spawner: the grant of %u is refused, since the actor pool holds %u and "
                    "keeps %u for the level", key, seen.count, NPC_SPAWN_POOL_RESERVE);
        return NPC_SPAWN_OUTCOME_REFUSED;
    }
    if (!npc_spawner_raise_granted(desc, key, saved)) {
        return NPC_SPAWN_OUTCOME_REFUSED;
    }
    note_the_build(desc, key, saved != NULL);
    return NPC_SPAWN_OUTCOME_DONE;
}

/* Whatever carries `key` goes, a corpse included, found by a fresh walk and never by a pointer
 * kept; the gun the player sits on stays. Nothing to remove is an answer only a whole walk can
 * give: a walk that broke off may have passed the copy, and a DONE then would leave it standing
 * with nobody asking again. */
static npc_spawn_outcome_t remove_key(void *user, uint32_t key)
{
    pool_look_t seen;

    (void)user;
    if (!look_at_the_pool(key, &seen)) {
        log_warning("npc spawner: the removal of %u is refused, since the actor pool could not be "
                    "walked to its end", key);
        return NPC_SPAWN_OUTCOME_REFUSED;
    }
    if (seen.actor == 0) {
        return NPC_SPAWN_OUTCOME_DONE;   /* nothing here carries it */
    }
    if (npc_spawn_save_rides(key) || !npc_spawner_delete(seen.actor)) {
        return NPC_SPAWN_OUTCOME_REFUSED;   /* ridden, or no delete to remove it with */
    }
    return NPC_SPAWN_OUTCOME_DONE;
}

static const char *wish_name(uint8_t kind)
{
    switch (kind) {
    case NPC_SPAWN_WISH_SPAWN:      return "the spawn";
    case NPC_SPAWN_WISH_REMOVE_OWN: return "the removal of this player's copies";
    case NPC_SPAWN_WISH_REMOVE_ALL: return "the removal of every copy";
    case NPC_SPAWN_WISH_RESTORE:    return "the restore";
    default:                        return "the wish";
    }
}

static const char *reason_name(uint8_t reason)
{
    switch (reason) {
    case NPC_SPAWN_REFUSED_CAP:        return "the session holds as many as its host allows";
    case NPC_SPAWN_REFUSED_POOL:       return "the host's actor pool is too full";
    case NPC_SPAWN_REFUSED_NO_LEVEL:   return "no level, or the world changed under it";
    case NPC_SPAWN_REFUSED_NO_BUILDER: return "the host has no panel that could build it";
    case NPC_SPAWN_REFUSED_SERVER:     return "the host is a dedicated server";
    case NPC_SPAWN_REFUSED_TOO_FAST:   return "more wishes than a player may send";
    case NPC_SPAWN_REFUSED_NOT_BUILT:  return "the host's panel could not build it";
    case NPC_SPAWN_REFUSED_NO_PARKING: return "this machine cannot hold a copy still";
    case NPC_SPAWN_REFUSED_UNSOUND:    return "the wire cannot carry its description";
    case NPC_SPAWN_REFUSED_NO_KEY:     return "every free key is still resting";
    default:                           return NULL;
    }
}

/* The same reasons, short enough for a row of the panel under "Refused: ". */
static const char *reason_word(uint8_t reason)
{
    switch (reason) {
    case NPC_SPAWN_REFUSED_CAP:        return "the host's cap is reached";
    case NPC_SPAWN_REFUSED_POOL:       return "the host's actor pool is full";
    case NPC_SPAWN_REFUSED_NO_LEVEL:   return "no level, or the world changed";
    case NPC_SPAWN_REFUSED_NO_BUILDER: return "the host cannot build it";
    case NPC_SPAWN_REFUSED_SERVER:     return "the host is a dedicated server";
    case NPC_SPAWN_REFUSED_TOO_FAST:   return "too many wishes too fast";
    case NPC_SPAWN_REFUSED_NOT_BUILT:  return "the host could not build it";
    case NPC_SPAWN_REFUSED_NO_PARKING: return "this machine cannot hold it";
    case NPC_SPAWN_REFUSED_UNSOUND:    return "it cannot travel";
    case NPC_SPAWN_REFUSED_NO_KEY:     return "every free key is still resting";
    default:                           return "a reason this panel does not know";
    }
}

/* A refusal is the panel's only answer to a wish that went nowhere, so each one is said. */
static void refused(void *user, uint32_t wish, uint8_t kind, uint8_t reason)
{
    const char *why = reason_name(reason);

    (void)user;
    link_refusal = reason_word(reason);
    if (why != NULL) {
        log_warning("npc spawner: %s, wish %u, is refused: %s", wish_name(kind), wish, why);
    } else {
        log_warning("npc spawner: %s, wish %u, is refused for reason %u", wish_name(kind), wish,
                    (unsigned)reason);
    }
}

static const npc_spawn_session_ops_t OPS = { &build, &remove_key, NULL, &refused };

/* What the session's copies came to, once an epoch: the line a field run is read by. */
static void say_the_counters(void)
{
    const npc_spawn_session_counters_t *c = &link_session.counters;

    log_info("npc spawner: the flyers so far, record-frames: %u over their owner's anchor, %u "
             "kept for want of one, %u over the local player", flyers_moved, flyers_kept,
             flyers_local);
    log_info("npc spawner: at epoch %u the panel has made %u wishes (%u ended by an epoch, %u "
             "refused, %u not carried, %u without room), built %u of the grants (%u refused, "
             "%u as saved, %u saved states lost), removed %u (%u ridden, %u stale), handed on "
             "%u, removed %u of the %u copies an old world left here (%u taken by a level's "
             "end), and held every grant back %u times", (unsigned)link_session.epoch, c->wishes,
             c->ended,
             c->refusals, c->unsound, c->queue_full, c->built, c->build_refused,
             c->restores_placed, c->restores_lost, c->cancelled, c->cancel_ridden,
             c->cancel_stale, c->owners, c->old_removed, c->old_found, c->old_gone, c->held_back);
}

void npc_spawn_link_tick(void)
{
    npc_spawn_grant_record_t grants;
    bool                     read;
    bool                     now;

    if (!link_ready) {
        npc_spawn_session_init(&link_session, panel_flags());
        link_ready = true;
    }
    npc_spawn_session_set_panel(&link_session, panel_flags());
    npc_spawn_session_set_world(&link_session, npc_spawn_node_epoch());
    if (npc_spawn_node_epoch() != local_world) {
        local_world = npc_spawn_node_epoch();
        npc_spawn_link_forget_refusal();   /* a refusal of the last world says nothing here */
    }
    read       = npc_spawn_note_read_grants(&grants);
    link_fresh = read;
    {
        npc_spawn_anchor_record_t anchors;

        if (npc_spawn_note_read_anchors(&anchors)) {
            link_anchors      = anchors;
            link_anchors_read = true;
        }
    }
    now        = !npc_spawn_node_loading() && !npc_spawn_save_window_open() &&
          npc_spawner_is_available();
    if (npc_spawn_session_frame(&link_session, read ? &grants : NULL, now, &OPS)) {
        npc_spawn_session_published(
            &link_session, npc_spawn_note_publish_wishes(npc_spawn_session_record(&link_session)));
    }
    say_what_was_built();   /* after the frame: the owner is in the table only now */
    if (link_session.counters.epochs != link_epochs) {
        link_epochs  = link_session.counters.epochs;
        link_refusal = NULL;   /* a refusal of the last world says nothing about this one */
        say_the_counters();
    }
}

const char *npc_spawn_link_refusal(void)
{
    return link_refusal;
}

void npc_spawn_link_refuse_here(const char *why)
{
    if (why == NULL || why[0] == '\0') {
        return;
    }
    text_format(local_refusal, sizeof local_refusal, "%s", why);
    link_refusal = local_refusal;
    log_info("npc spawner: refused here: %s", local_refusal);
}

void npc_spawn_link_forget_refusal(void)
{
    link_refusal = NULL;
}

bool npc_spawn_link_fresh(void)
{
    return link_fresh;
}

bool npc_spawn_link_flyer_point(uint32_t key, const float standing[3], float out[3])
{
    switch (npc_spawn_session_flyer(&link_session, link_anchors_read ? &link_anchors : NULL, key,
                                    out)) {
    case NPC_SPAWN_FLYER_ANCHOR:
        ++flyers_moved;
        return true;
    case NPC_SPAWN_FLYER_KEEP:
        ++flyers_kept;
        return false;
    case NPC_SPAWN_FLYER_LOCAL:
    default:
        memcpy(out, standing, 3u * sizeof(float));
        ++flyers_local;
        return true;
    }
}

bool npc_spawn_link_active(void)
{
    return npc_spawn_session_active(&link_session);
}

bool npc_spawn_link_is_client(void)
{
    return npc_spawn_session_is_client(&link_session);
}

uint32_t npc_spawn_link_cap(void)
{
    return npc_spawn_link_active() && !npc_spawn_link_is_client() ? link_session.cap : 0u;
}

void npc_spawn_link_facts(npc_spawn_link_facts_t *out)
{
    if (out == NULL) {
        return;
    }
    out->read     = link_session.read;
    out->fresh    = link_fresh;
    out->active   = npc_spawn_link_active();
    out->client   = npc_spawn_link_is_client();
    out->cap      = link_session.cap;
    out->own_slot = link_session.own_slot;
    out->epoch    = link_session.epoch;
}

/* A wish is asked from the placement mode's own frame, never from a window message.
 *
 * Choosing where a spawn stands is what probes the world, and every one of those probes walks the
 * engine's single global polygon list. A click arrives on a window message, and since the panel
 * does not hold the simulation in a session, a window message can land in the middle of a substep
 * doing that same walk. The mode's click is taken in the frame hook, a place in the engine's own
 * flow, so the description below is already made there and this only hands it on. */
bool npc_spawn_link_spawn_described(const npc_spawn_desc_t *desc)
{
    if (desc == NULL || !npc_spawn_link_active()) {
        return false;
    }
    link_refusal = NULL;
    if (!npc_spawn_session_wish(&link_session, (uint8_t)NPC_SPAWN_WISH_SPAWN, desc, NULL)) {
        log_warning("npc spawner: the spawn of %s could not be asked of the host", desc->file);
        return false;
    }
    log_info("npc spawner: a %s is asked of the %s, placed with the mouse", desc->file,
             npc_spawn_link_is_client() ? "host" : "session");
    return true;
}

bool npc_spawn_link_owns(uint32_t key)
{
    uint8_t owner = 0;

    if (!npc_spawn_link_active()) {
        return true;
    }
    if (!npc_spawn_key_is_copy(key) ||
        !npc_spawn_session_owner(&link_session, key - NPC_SPAWN_KEY_FIRST, &owner)) {
        return false;   /* a copy this panel holds no life of is nobody's it could speak for */
    }
    return owner == link_session.own_slot;
}

bool npc_spawn_link_remove(bool everybody)
{
    uint8_t kind = everybody ? (uint8_t)NPC_SPAWN_WISH_REMOVE_ALL
                             : (uint8_t)NPC_SPAWN_WISH_REMOVE_OWN;

    if (!npc_spawn_link_active() || (everybody && npc_spawn_link_is_client())) {
        return false;   /* only the host removes another player's copies */
    }
    link_refusal = NULL;
    if (!npc_spawn_session_wish(&link_session, kind, NULL, NULL)) {
        log_warning("npc spawner: %s could not be asked of the session", wish_name(kind));
        return false;
    }
    log_info("npc spawner: %s is asked of the session", wish_name(kind));
    return true;
}

uint32_t npc_spawn_link_restore(const npc_spawn_saved_t *held, uint32_t count)
{
    uint32_t wished = 0;
    uint32_t i;

    if (held == NULL || !npc_spawn_link_active() || npc_spawn_link_is_client()) {
        return 0u;
    }
    for (i = 0; i < count; ++i) {
        wished += npc_spawn_session_wish(&link_session, (uint8_t)NPC_SPAWN_WISH_RESTORE,
                                         &held[i].desc, &held[i])
                      ? 1u
                      : 0u;
    }
    return wished;
}
