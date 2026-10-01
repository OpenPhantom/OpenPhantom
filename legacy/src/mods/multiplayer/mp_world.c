/* mp_world.c: the map's own moments on the wire, and the measurement of how far the two maps run
 * apart while only the moments travel.
 *
 * Two halves that share a world pointer and nothing else. The first hulls the engine's opener and
 * closer, turns the local player's triggers into events and performs the far player's; the second
 * describes this side's moving movers once a second and holds the far side's description against
 * them without touching anything.
 *
 * A third half lives in mp_world_apply.c and is the only one that WRITES a mover: the host's
 * description of the doors and buttons that are away from the position the level authored, so a
 * player who joins a running game is not standing behind a door that is shut only for him. It
 * reaches the map through the record access this file exports and through the gate below, which
 * is what keeps its opener calls from being read back out as the local player's triggers.
 *
 * The opener writes four latches and no geometry: whether the mover is in the ticking list, which
 * way it is going, the time it started and its dwell counter. What actually moves a mover is the
 * integrator, and the integrator is pulled by whoever needs the mover to be current, a collision
 * probe, the rider carrier, the face gradient or the draw pass, each of which brings it up to the
 * world clock itself. That is why performing an event does not have to happen at a particular
 * point inside the substep: nothing reads a stale pose, because the next reader integrates. What
 * it does have to be is outside the second body's bank window, where the hero record holds the
 * puppet's content, and once per substep at a stable point, which is why it runs beside the
 * puppet's own events on the render tick they share.
 *
 * SIZE NOTE: the seam this note used to name has been cut, twice. The digest codec and
 * the comparison were the half that knew no address and ran in the unit tests with nothing else in
 * the process, and they are now mp_world_digest.c; the host's state note took the same cut, its
 * message and its decisions to mp_world_state.c and its walk over the mover table to
 * mp_world_apply.c. What is left here is the half that knows where a mover is. THE NEXT SEAM, if
 * this file grows again, is the trigger side: the two hulls, the repeat table and the outgoing
 * events have nothing to do with the digest, the comparison or the phase controller they share
 * this file with, and they hold their own state.
 */
#include "mp_world.h"

#include "mp_armed.h"
#include "mp_cells.h"
#include "mp_crate_opener.h"
#include "mp_events.h"
#include "mp_puppet_anim.h"
#include "mp_signatures.h"
#include "mp_wire.h"
#include "mp_world_apply.h"
#include "mp_world_phase.h"
#include "mp_world_state.h"

#include "common/detour.h"
#include "common/logging.h"
#include "common/memory.h"

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <string.h>

/* A digest is sent once a second, so two of them are this many local substeps apart. A digest that
 * took longer than that to arrive plus a little slack was resent, which the reliable channel does
 * until it is acknowledged, and it then describes a world that has moved on. */
#define MP_WORLD_DIGEST_SLACK 6u


/* Repeat triggers. A walk plate fires every substep for as long as somebody stands on it, and on
 * a door that is already open each of those calls does exactly one thing: it zeroes the dwell so
 * the door stays open. Sending thirty two of those a second would be thirty two messages for one
 * fact. Sending none of them would let the far side's copy of that door close under a player who
 * is standing on the plate holding it open. So a repeat that only refreshed the dwell travels at
 * a quarter of the substep rate, which bounds the far side's dwell at a quarter second while the
 * plate is held; a call that actually changed the mover's state always travels at once.
 *
 * The table is indexed by the low byte of the id, so two movers 256 apart share a slot. A shared
 * slot can only delay a refresh, which is optional by construction, and never a state change,
 * which does not consult it. */
#define MP_WORLD_REFRESH_SLOTS 256u
#define MP_WORLD_REFRESH_TICKS 8u

typedef void(__cdecl *mover_gate_fn_t)(uint32_t world, int32_t mover_index);

/* What a trigger found before it ran, so what it did can be told from what it did not do. */
typedef struct mover_watch {
    uint32_t active;
    uint32_t dir;
    float    dwell;
} mover_watch_t;

typedef struct mp_world_state {
    bool             installed;
    bool             complete;
    bool             catching;      /* the hulls stand and the local player's triggers travel */
    bool             measuring;     /* the digest is built and the far side's is compared */
    detour_t         open_hull;
    detour_t         close_hull;
    mover_gate_fn_t  open_gate;     /* how this module reaches the engine's opener */
    mover_gate_fn_t  close_gate;
    bool             performing;    /* the call the engine is in now is this module's own */

    mp_event_queue_t out;           /* caught here, drained by the bridge */
    mp_event_queue_t in[MP_WORLD_CLOCKS];   /* arrived from the wire, held for their clock */
    uint32_t         tick;
    uint32_t         render_tick[MP_WORLD_CLOCKS];
    bool             render_known[MP_WORLD_CLOCKS];

    bool             refusal_logged;

    /* The free runners' phase. Only a client over a real link corrects: the host owns the map, and
     * in the loopback both ends share one, so a controller there would push the movers against
     * their own measurement. */
    bool             correcting;
    mp_world_phase_t phase;
    uint32_t         last_digest_tick;   /* the sender's substep on the last digest that counted */
    uint32_t         last_digest_local;  /* our own substep when it arrived */
    bool             digest_seen;
    uint32_t         digests_late;

    uint32_t         last_sent[MP_WORLD_REFRESH_SLOTS];
    bool             sent_once[MP_WORLD_REFRESH_SLOTS];

    mp_world_stats_t stats;
} mp_world_state_t;

static mp_world_state_t world_state;

/* ==============================================================================================
 * The engine half. The digest codec and the comparison it feeds are pure and live next door in
 * mp_world_digest.c; this file is the part that knows where a mover is.
 * ============================================================================================ */

/* Every read below catches the fault rather than asking the page query first, and that is a
 * measured difference rather than a taste: the hulls run inside the engine's own call, and one walk
 * plate reaches them four times a substep for as long as somebody stands on it. The guarded copy
 * is a few instructions; VirtualQuery is a system call, and there would be ten of them per
 * trigger. */
static bool read_u32(uintptr_t address, uint32_t *out)
{
    return memory_try_read(address, out, sizeof *out);
}

uint32_t mp_world_pointer(void)
{
    uintptr_t cell  = mp_cells_address(MP_CELL_LEVEL);
    uint32_t  world = 0;

    if (cell == 0 || !read_u32(cell, &world)) {
        return 0u;
    }
    return world;
}

/* How many movers this world has. A count outside the bound is a pointer that is not a world:
 * the field is signed in the engine, so a torn read comes back here as a very large unsigned. */
bool mp_world_mover_count(uint32_t world, uint32_t *count)
{
    if (world == 0u || !read_u32(world + MP_LEVEL_MOVER_COUNT, count)) {
        return false;
    }
    return *count <= MP_WORLD_MOVER_LIMIT;
}

bool mp_world_mover_at(uint32_t world, uint32_t index, uint32_t *mover)
{
    uint32_t count = 0;

    if (!mp_world_mover_count(world, &count) || index >= count) {
        return false;
    }
    return read_u32(world + WORLD_MOVER_TABLE + index * 4u, mover) && *mover != 0u;
}

static bool read_watch(uint32_t world, int32_t index, mover_watch_t *out)
{
    uint32_t mover = 0;

    memset(out, 0, sizeof *out);
    if (index < 0 || !mp_world_mover_at(world, (uint32_t)index, &mover)) {
        return false;
    }
    return read_u32(mover + MOVER_ACTIVE, &out->active) &&
           read_u32(mover + MOVER_DIR, &out->dir) &&
           memory_try_read(mover + MOVER_DWELL, &out->dwell, sizeof out->dwell);
}

bool mp_world_mover_read(uint32_t world, uint32_t index, mp_world_mover_t *out)
{
    uint32_t mover = 0;

    memset(out, 0, sizeof *out);
    if (!mp_world_mover_at(world, index, &mover) ||
        !read_u32(mover + MOVER_TYPE, &out->type) ||
        !read_u32(mover + MOVER_DIR, &out->dir) ||
        !read_u32(mover + MOVER_ACTIVE, &out->active) ||
        !memory_try_read(mover + MOVER_POSE, &out->pose, sizeof out->pose) ||
        !memory_try_read(mover + MOVER_LENGTH, &out->length, sizeof out->length) ||
        !memory_try_read(mover + MOVER_SPEED, &out->speed, sizeof out->speed) ||
        !memory_try_read(mover + MOVER_DWELL, &out->dwell, sizeof out->dwell)) {
        return false;
    }
    return out->type < MP_WORLD_MOVER_TYPES && out->dir <= MOVER_DIR_MAX &&
           out->length > 0.0f && out->pose == out->pose && out->dwell == out->dwell;
}

/* The same record as the wire would describe it, for holding a received entry against. */
void mp_world_as_entry(uint32_t index, const mp_world_mover_t *read, mp_world_entry_t *entry)
{
    entry->id     = (uint16_t)index;
    entry->type   = (uint8_t)read->type;
    entry->dir    = (uint8_t)read->dir;
    entry->active = (uint8_t)(read->active != 0u ? 1u : 0u);
    entry->pose   = read->pose / read->length;
    entry->dwell  = read->dwell;
}

/* Whether a repeat trigger of this mover may travel again yet. Read signed, so a wrapped substep
 * counter answers the same as an unwrapped one. */
static bool refresh_due(uint32_t id)
{
    size_t slot = (size_t)(id % MP_WORLD_REFRESH_SLOTS);

    if (!world_state.sent_once[slot]) {
        return true;
    }
    return (int32_t)(world_state.tick - world_state.last_sent[slot]) >=
           (int32_t)MP_WORLD_REFRESH_TICKS;
}

static void mark_sent(uint32_t id)
{
    size_t slot = (size_t)(id % MP_WORLD_REFRESH_SLOTS);

    world_state.sent_once[slot] = true;
    world_state.last_sent[slot] = world_state.tick;
}

/* A census of every call in the retail code finds eight callers of the opener: four inside the
 * plate trigger at 0x00408E3A (0x00408E89, 0x00408EB3, 0x00408EDE, 0x00408F06), the block
 * landing at 0x0040A322, the savegame restore at 0x0040ADDC, the packed opener at 0x0040BD98
 * and the script opcode at 0x0042E2D8; the closer has two, 0x0040ADFF in the restore and
 * 0x0042E2ED in the script. The four that sit close together looked like a cascade of movers
 * opening each other; they are one four iteration loop over the plate's trigger slots that the
 * compiler unrolled, 0x2A, 0x2B and 0x28 bytes apart, and the opener's own body contains no
 * call at all. So there is no re-entry from the engine's side and no depth counter; what the
 * `performing` flag guards is this module's own call into the opener, which must not be read
 * back out as the local player's trigger.
 *
 * Three of those callers are not the local player. The savegame restore opens or closes every
 * mover whose saved state differs, a burst the outgoing ring of sixteen bounds and counts. The
 * block landing changes the block's type from 7 to 4 or 3 and then opens it, so the event
 * travels but the far side cannot honour it whole, since neither the type change nor the block's
 * position is on the wire. The script opcode is deliberately not told apart: both machines run
 * the same state machines, so a scripted open already fires twice and the event makes it three,
 * which on a door is a longer open, on a one shot nothing, and on a push button in its open
 * waiting state a second press that shuts it, so the far side can toggle where the local side
 * does not. Telling it apart would need the script dispatcher as a further site.
 *
 * The bank guard the actions module has, `mp_bank_active_class() == 1`, is left out here on
 * purpose: it would matter only if the puppet probed the ground and its feet reached the plate
 * trigger, and the puppet has no ground probe and is not ticked through the player pipeline.
 *
 * What one trigger did, and whether that is worth a message. A call that moved the mover between
 * states always is. A call that only put the dwell back is the plate being held down, and it
 * travels at the refresh rate. A call that did nothing at all, which is most of them once the
 * per type latches have had their say, travels never. */
static void note_gate(uint32_t world, int32_t index, const mover_watch_t *before, uint8_t mode)
{
    mover_watch_t after;
    mp_event_t    event;
    bool          changed;
    bool          refreshed;

    if (!read_watch(world, index, &after)) {
        ++world_state.stats.unread;
        return;
    }
    changed   = after.active != before->active || after.dir != before->dir;
    refreshed = !changed && after.dwell < before->dwell;
    if (!changed && !refreshed) {
        ++world_state.stats.unchanged;
        return;
    }
    if (!changed && !refresh_due((uint32_t)index)) {
        ++world_state.stats.unchanged;
        return;
    }

    memset(&event, 0, sizeof event);
    event.kind       = MP_EVENT_MOVER;
    event.tick       = world_state.tick;
    event.mover_id   = (uint16_t)index;
    event.mover_mode = mode;
    mp_event_queue_push(&world_state.out, &event);
    mark_sent((uint32_t)index);
    ++world_state.stats.caught;
    if (!changed) {
        ++world_state.stats.refreshed;
    }
}

/* The two sites, byte for byte. Both are `void __cdecl (world, moverIndex)`:
 *
 *     bapmap_openMover at 0x00408B50
 *       55 8B EC 83 EC 20      push ebp; mov ebp, esp; sub esp, 0x20
 *       83 7D 08 00 / 74 14    cmp [ebp+8], 0; je            ; a world at all
 *       83 7D 0C 00 / 7C 0E    cmp [ebp+0xC], 0; jl          ; an index that is not negative
 *       8B 45 08 / 8B 4D 0C    the two arguments
 *       3B 88 20 06 00 00      cmp ecx, [eax+0x620]          ; below the world's mover count
 *
 *     bapmap_closeMover at 0x00408DF5
 *       55 8B EC 51            push ebp; mov ebp, esp; push ecx
 *       then the same three guards, two bytes earlier
 *
 * The bodies are the same three guards and only the frame set-up tells them apart, six bytes
 * against four, which is why each pattern carries its head: the shared tail matches in both. A
 * jump overwrites five bytes and the declared prologue is the whole instructions covering them,
 * so the opener's is six and the closer's is eight, its four plus the `83 7D 08 00` that follows.
 * Dropping the closer's eight byte head leaves a tail that also occurs in the opener at +0x0A,
 * and that candidate is rejected because the bytes in front of it, `EC 83 EC 20 83 7D 08 00`,
 * are neither the declared prologue nor a jump, so exactly one candidate survives each way. Both
 * resolve in all five shipped builds and in the editor's recompile.
 *
 * The hulls. Both read the mover before the engine's own call and again after it, because what
 * has to be decided is whether the call did anything, and the engine's opener answers that only
 * through the record. A call this module made itself is skipped: the far player's event must not
 * be sent back to the far player. Whether the hook can even see that call depends on how the
 * detour chain resolved at install time, which this module cannot know, so the flag is set for
 * both shapes rather than for the one that happens to hold today. */
static bool catching_now(void)
{
    return mp_armed_transport() && world_state.catching && !world_state.performing;
}

static void __cdecl hook_open_mover(uint32_t world, int32_t mover_index)
{
    mover_gate_fn_t original = (mover_gate_fn_t)world_state.open_hull.original;
    mover_watch_t   before;
    bool            watched = catching_now() && read_watch(world, mover_index, &before);

    original(world, mover_index);
    if (watched && !mp_crate_opener_is_a_sink(world, mover_index)) {
        note_gate(world, mover_index, &before, MP_EVENT_MOVER_OPEN);
    }
}

static void __cdecl hook_close_mover(uint32_t world, int32_t mover_index)
{
    mover_gate_fn_t original = (mover_gate_fn_t)world_state.close_hull.original;
    mover_watch_t   before;
    bool            watched = catching_now() && read_watch(world, mover_index, &before);

    original(world, mover_index);
    if (watched) {
        note_gate(world, mover_index, &before, MP_EVENT_MOVER_CLOSE);
    }
}

static bool install_hull(mp_site_t site, detour_t *hull, const void *hook, const char *what)
{
    uintptr_t address = mp_signatures_address(site);

    if (address == 0 || !detour_install(hull, address, hook, mp_signatures_prologue(site))) {
        log_warning("the mover %s is not hulled, so the local player's doors, lifts and "
                    "platforms do not reach the far side", what);
        return false;
    }
    return true;
}

/* Every clock's held events forgotten and its replay unknown; what each dropped stays counted. */
static void forget_incoming(void)
{
    size_t clock;

    for (clock = 0; clock < MP_WORLD_CLOCKS; ++clock) {
        uint32_t dropped = world_state.in[clock].dropped;

        mp_event_queue_init(&world_state.in[clock]);
        world_state.in[clock].dropped   = dropped;
        world_state.render_known[clock] = false;
    }
}

bool mp_world_install(bool catch_events, bool measure)
{
    uintptr_t open_address;
    uintptr_t close_address;
    bool      hulls = true;

    if (world_state.installed) {
        return world_state.complete;
    }
    world_state.installed = true;
    mp_event_queue_init(&world_state.out);
    forget_incoming();

    if (!catch_events && !measure) {
        world_state.complete = true;
        return true;
    }

    open_address  = mp_signatures_address(MP_SITE_BAPMAP_OPEN_MOVER);
    close_address = mp_signatures_address(MP_SITE_BAPMAP_CLOSE_MOVER);
    if (open_address == 0 || close_address == 0 || mp_cells_address(MP_CELL_LEVEL) == 0) {
        log_warning("the map does not travel: the mover opener, the mover closer or the world "
                    "cell did not resolve");
        return false;
    }
    world_state.measuring = measure;

    if (catch_events) {
        hulls = install_hull(MP_SITE_BAPMAP_OPEN_MOVER, &world_state.open_hull,
                             (const void *)&hook_open_mover, "opener");
        hulls = install_hull(MP_SITE_BAPMAP_CLOSE_MOVER, &world_state.close_hull,
                             (const void *)&hook_close_mover, "closer") && hulls;
        world_state.catching = hulls;
    }

    /* Where this module reaches the engine. Through the hull's own trampoline when one stands, so
     * a performed event runs the engine's prologue and not this file's hook; through the site
     * itself when the hull did not install, which is the shape a build with an unresolved site
     * ends up in and is still worth having, because an event that arrives is better performed
     * than dropped. */
    world_state.open_gate = world_state.open_hull.installed
                                ? (mover_gate_fn_t)world_state.open_hull.original
                                : (mover_gate_fn_t)open_address;
    world_state.close_gate = world_state.close_hull.installed
                                 ? (mover_gate_fn_t)world_state.close_hull.original
                                 : (mover_gate_fn_t)close_address;

    /* The applying half, which needs one more site and is worth having on its own: this side can
     * describe its map and count what the two sides disagree about without being able to write a
     * mover, and it says so when it cannot. */
    (void)mp_world_apply_install();

    world_state.complete = hulls;
    if (!world_state.complete) {
        /* All or nothing on the sending side: with one of the two hulls missing this side would
         * report half of what its player does, which is worse to reason about than reporting
         * none of it. Receiving is unaffected and stays on. */
        log_warning("the map does not send: a hull named above did not stand, so this side "
                    "performs the far player's mover triggers but sends none of its own");
    } else if (catch_events && measure) {
        log_info("the map is on the wire: the local player's mover triggers travel as events, and "
                 "the two maps are compared once a second; a client steers its free runners back "
                 "into phase from that comparison, a host measures and leaves them alone");
    } else if (catch_events) {
        log_info("the map is on the wire: the local player's mover triggers travel as events");
    } else {
        log_info("the map is measured but not sent: neither side's mover triggers travel, and the "
                 "comparison once a second is therefore of two free running maps");
    }
    return world_state.complete;
}

void mp_world_set_tick(uint32_t tick)
{
    world_state.tick = tick;
}

void mp_world_clear(void)
{
    uint32_t dropped_out = world_state.out.dropped;

    mp_event_queue_init(&world_state.out);
    world_state.out.dropped = dropped_out;
    forget_incoming();
    memset(world_state.sent_once, 0, sizeof world_state.sent_once);

    /* Every debt is a claim about the map that was here a moment ago, and the level prime rewrites
     * timeBase for every mover in it. Paying an old debt into a new map would move a mover for a
     * reason that no longer exists. */
    mp_world_phase_reset(&world_state.phase);
    world_state.digest_seen = false;
    mp_world_apply_forget();
}

void mp_world_note_arrival(void)
{
    memset(world_state.sent_once, 0, sizeof world_state.sent_once);
}

void mp_world_queue_event(size_t clock, const mp_event_t *event)
{
    if (clock >= MP_WORLD_CLOCKS || event == NULL || event->kind != MP_EVENT_MOVER) {
        return;
    }
    mp_event_queue_push(&world_state.in[clock], event);
}

void mp_world_note_render_tick(size_t clock, uint32_t tick)
{
    if (clock < MP_WORLD_CLOCKS) {
        world_state.render_tick[clock]  = tick;
        world_state.render_known[clock] = true;
    }
}

bool mp_world_render_tick(size_t clock, uint32_t *out)
{
    if (out == NULL || clock >= MP_WORLD_CLOCKS || !world_state.render_known[clock]) {
        return false;
    }
    *out = world_state.render_tick[clock];
    return true;
}

bool mp_world_gate(uint32_t world, uint32_t id, bool open)
{
    mover_gate_fn_t gate  = open ? world_state.open_gate : world_state.close_gate;
    uint32_t        mover = 0;

    if (gate == NULL || !mp_world_mover_at(world, id, &mover)) {
        return false;
    }
    world_state.performing = true;
    gate(world, (int32_t)id);
    world_state.performing = false;
    return true;
}

static void perform(uint32_t world, const mp_event_t *event)
{
    if (!mp_world_gate(world, event->mover_id, event->mover_mode == MP_EVENT_MOVER_OPEN)) {
        ++world_state.stats.perform_refused;
        if (!world_state.refusal_logged) {
            world_state.refusal_logged = true;
            log_warning("a mover event named mover %u, which this side's level has no record "
                        "for, or the opener did not resolve; later ones are counted",
                        (unsigned)event->mover_id);
        }
        return;
    }
    ++world_state.stats.performed;
}

/* Only a client over a real link corrects the map. Set from the bridge once, before any substep
 * runs, because the mode is decided when the transport is installed. */
void mp_world_set_correcting(bool correcting)
{
    if (world_state.correcting != correcting) {
        mp_world_phase_reset(&world_state.phase);
    }
    world_state.correcting = correcting;
}

/* One substep of the phase controller: every free runner that owes something gets at most a
 * quarter substep of its own travel taken off or added on, and the whole of it is a shift of
 * timeBase. The engine reads timeBase once per integration, subtracts it from the level clock,
 * clamps a negative difference to zero and then overwrites it with the clock, so what is written
 * here changes exactly one advance and is gone. The pose is never touched: the keyframe track is
 * evaluated at whatever pose comes out, and a value the mover never travelled through would skip
 * the edges that switch collision faces, start sounds and reveal bodies.
 */
static void pay_phase_debts(uint32_t world)
{
    uint32_t count = 0;
    uint32_t index;

    if (!world_state.correcting || mp_world_phase_owing(&world_state.phase) == 0u ||
        !mp_world_mover_count(world, &count)) {
        return;
    }
    for (index = 0; index < count; ++index) {
        mp_world_mover_t read;
        uint32_t     mover = 0;
        uint32_t     active = 0;
        float        bite;
        float        time_base = 0.0f;

        if (!mp_world_mover_at(world, index, &mover) ||
            !read_u32(mover + MOVER_ACTIVE, &active) || active == 0u ||
            !mp_world_mover_read(world, index, &read) || read.type != MP_WORLD_TYPE_ALWAYS_ON) {
            continue;
        }
        bite = mp_world_phase_bite(&world_state.phase, (uint16_t)index, read.pose, read.speed,
                                   read.length);
        if (bite == 0.0f) {
            continue;
        }
        if (!memory_try_read(mover + MOVER_TIMEBASE, &time_base, sizeof time_base) ||
            time_base != time_base) {
            continue;
        }
        time_base += bite;
        if (!memory_try_write(mover + MOVER_TIMEBASE, &time_base, sizeof time_base)) {
            ++world_state.stats.write_faults;
        }
    }
}

/* One clock's held events, oldest first, and one that is not due yet holds everything behind it:
 * the ring is in its sender's order, and a door opened before the platform it stands on was
 * raised is the one ordering a map has. Two players' doors have no order between them, so one
 * player's early event holds nobody else's. */
static void run_clock(uint32_t world, size_t clock)
{
    mp_event_queue_t *in = &world_state.in[clock];
    mp_event_t        held[MP_EVENT_QUEUE_SLOTS];
    size_t            held_count = 0;
    bool              waiting = false;
    mp_event_t        event;
    size_t            index;

    while (mp_event_queue_pop(in, &event)) {
        if (!waiting) {
            mp_puppet_anim_due_t due = mp_puppet_anim_event_due(event.tick,
                                                                world_state.render_tick[clock],
                                                                world_state.render_known[clock]);

            waiting = due == MP_PUPPET_ANIM_WAIT;
            if (due == MP_PUPPET_ANIM_LATE) {
                ++world_state.stats.events_late;
            } else if (due == MP_PUPPET_ANIM_FORCED) {
                ++world_state.stats.events_forced;
            }
        }
        if (waiting) {
            held[held_count++] = event;
            continue;
        }
        perform(world, &event);
    }
    for (index = 0; index < held_count; ++index) {
        mp_event_queue_push(in, &held[index]);
    }
}

void mp_world_run_due(void)
{
    uint32_t world;
    size_t   clock;

    world = mp_world_pointer();

    /* Every substep, whether or not an event is due: a debt is paid off over seconds and the
     * mover has to be looked at each time it integrates, not each time somebody opens a door. */
    pay_phase_debts(world);

    for (clock = 0; clock < MP_WORLD_CLOCKS; ++clock) {
        run_clock(world, clock);
    }
}

size_t mp_world_build_digest(uint32_t tick, uint8_t *buffer, size_t capacity)
{
    mp_world_digest_t digest;
    uint32_t          world;
    uint32_t          count = 0;
    uint32_t          index;

    if (!world_state.measuring || (tick % MP_WORLD_DIGEST_TICKS) != 0u) {
        return 0u;
    }
    world = mp_world_pointer();
    if (!mp_world_mover_count(world, &count)) {
        return 0u;
    }
    mp_world_digest_init(&digest, tick);
    mp_world_digest_set_level(&digest, (uint16_t)count);
    for (index = 0; index < count; ++index) {
        mp_world_mover_t read;

        if (!mp_world_mover_read(world, index, &read)) {
            continue;   /* a record that did not read, or one with no travel to be a fraction of */
        }
        /* Away from the position the level was authored with, which is not the same question as
         * whether the engine still ticks this mover. A door that has latched open is off the
         * ticking list and is still open, and there were sixty two records in the shipped levels
         * that the old test, list membership alone, could never describe. */
        if (!mp_world_state_in_digest(read.type, read.active, read.dir)) {
            continue;
        }
        switch (mp_world_digest_add(&digest, index, read.type, read.dir, read.active, read.pose,
                                    read.length, read.dwell)) {
        case MP_WORLD_ADD_FULL:
            ++world_state.stats.capped;
            break;
        case MP_WORLD_ADD_REFUSED:
            ++world_state.stats.refused;
            break;
        case MP_WORLD_ADD_OK:
        default:
            break;
        }
    }
    if (digest.count == 0u) {
        return 0u;
    }
    ++world_state.stats.digests_out;
    return mp_world_digest_encode(&digest, buffer, capacity);
}

void mp_world_receive_digest(const uint8_t *buffer, size_t bytes)
{
    mp_world_digest_t digest;
    uint32_t          world;
    uint32_t          count = 0;
    size_t            index;
    bool              late;

    if (!world_state.measuring) {
        return;
    }
    if (!mp_world_digest_decode(buffer, bytes, &digest)) {
        ++world_state.stats.torn;
        return;
    }
    ++world_state.stats.digests_in;
    world = mp_world_pointer();
    if (!mp_world_mover_count(world, &count)) {
        world_state.stats.unreadable += (uint32_t)digest.count;
        return;
    }

    /* A mover id is an index into one level's table. The session outlives a level load on purpose,
     * so a digest can arrive that describes the map the sender had a moment ago, and every id in it
     * would then name a different mover here. Comparing those would report disagreement that is
     * really two maps, which is what the field measured, up to 104 in one run, as movers whose
     * type the two sides did not agree on; a type is authored and cannot differ between two
     * machines in the same level. */
    if (digest.level != (uint16_t)count) {
        ++world_state.stats.digests_elsewhere;
        return;
    }

    /* The digest travels on the reliable channel, which puts an unacknowledged message back into
     * every packet until it lands. A resend arrives carrying the pose of a world that has moved on
     * since, and a free runner at sixty pose units a second crosses a tenth of its ring in the time
     * a resend takes. It is still worth comparing, because the comparison is what the report reads;
     * it is not worth CORRECTING against, so a late digest is counted and measures nothing.
     *
     * Late is measured against the two sides' own clocks rather than a wall clock: the sender's
     * ticks between this digest and the last are what it took to build them, and our own substeps
     * between the two arrivals are what it took to deliver them. */
    late = world_state.digest_seen &&
           (world_state.tick - world_state.last_digest_local) >
               (digest.tick - world_state.last_digest_tick) + MP_WORLD_DIGEST_SLACK;
    if (late) {
        ++world_state.stats.digests_late;
    }
    world_state.digest_seen       = true;
    world_state.last_digest_tick  = digest.tick;
    world_state.last_digest_local = world_state.tick;

    for (index = 0; index < digest.count; ++index) {
        const mp_world_entry_t *wire = &digest.entry[index];
        mp_world_entry_t        local;
        mp_world_mover_t            read;

        if (wire->id >= count) {
            ++world_state.stats.missing;
            continue;
        }
        if (!mp_world_mover_read(world, wire->id, &read)) {
            ++world_state.stats.unreadable;
            continue;
        }
        mp_world_as_entry(wire->id, &read, &local);
        mp_world_note(&world_state.stats, wire, &local);

        /* Only the free runners, and only where the two sides agree what they are looking at.
         * Every other type is put back into step by the event that starts its next cycle. */
        if (!world_state.correcting || late || wire->type != local.type ||
            wire->type != MP_WORLD_TYPE_ALWAYS_ON) {
            continue;
        }
        (void)mp_world_phase_measure(&world_state.phase, wire->id,
                                     mp_world_phase_delta(local.pose, wire->pose),
                                     read.speed, read.length);
    }
}

void mp_world_send(uint32_t tick, mp_world_send_fn_t send)
{
    mp_event_t event;
    uint8_t    buffer[MP_WORLD_DIGEST_MAX_BYTES];
    uint8_t    note[MP_WORLD_STATE_MAX_BYTES];
    size_t     bytes;

    if (send == NULL) {
        return;
    }
    while (mp_event_queue_peek(&world_state.out, &event)) {
        bytes = mp_event_encode(&event, buffer, sizeof buffer);
        if (bytes != 0u && !send(buffer, bytes)) {
            return;   /* the channel is full; the trigger waits and the ring ages it */
        }
        (void)mp_event_queue_pop(&world_state.out, &event);
    }
    bytes = mp_world_build_digest(tick, buffer, sizeof buffer);
    if (bytes != 0u) {
        (void)send(buffer, bytes);
    }
    bytes = mp_world_apply_build(tick, note, sizeof note);
    if (bytes != 0u && !send(note, bytes)) {
        mp_world_apply_note_refused();
    }
}

bool mp_world_take_message(size_t clock, const uint8_t *note, size_t bytes)
{
    mp_event_t event;

    if (mp_world_is_digest(note, bytes)) {
        mp_world_receive_digest(note, bytes);
        return true;
    }
    if (mp_world_apply_take(note, bytes)) {
        return true;   /* the host's description of its own map, which only a client acts on */
    }
    if (note == NULL || bytes != MP_EVENT_MOVER_BYTES || note[0] != MP_EVENT_MOVER) {
        return false;
    }
    if (mp_event_decode(note, bytes, &event)) {
        mp_world_queue_event(clock, &event);
    }
    return true;
}

const mp_world_stats_t *mp_world_statistics(void)
{
    return &world_state.stats;
}

void mp_world_report(void)
{
    const mp_world_stats_t *stats = &world_state.stats;
    uint32_t                dropped = world_state.out.dropped;
    size_t                  clock;
    size_t                  type;

    if (!world_state.installed) {
        return;
    }
    for (clock = 0; clock < MP_WORLD_CLOCKS; ++clock) {
        dropped += world_state.in[clock].dropped;
    }
    log_info("  the map on the wire: %u trigger(s) sent (%u of them a held plate refreshing a "
             "dwell), %u that changed nothing, %u unread, %u dropped; %u performed, %u refused, "
             "%u late / %u forced",
             (unsigned)stats->caught, (unsigned)stats->refreshed, (unsigned)stats->unchanged,
             (unsigned)stats->unread,
             (unsigned)dropped,
             (unsigned)stats->performed, (unsigned)stats->perform_refused,
             (unsigned)stats->events_late, (unsigned)stats->events_forced);

    /* Before the return below, because the state note and the digest are separately switched: a
     * build that only sends the map's state still has something to say here. */
    mp_world_apply_report();

    if (!world_state.measuring) {
        return;
    }
    log_info("  the map compared: %u digest(s) sent, %u received, %u torn, %u about another level; "
             "%u entry(s) over the "
             "cap, %u refused, %u named a mover this side has not got, %u unreadable",
             (unsigned)stats->digests_out, (unsigned)stats->digests_in, (unsigned)stats->torn,
             (unsigned)stats->digests_elsewhere,
             (unsigned)stats->capped, (unsigned)stats->refused, (unsigned)stats->missing,
             (unsigned)stats->unreadable);
    /* Outside the loop below, which is where this block used to sit: it was written once and
     * printed eight times, once per mover type, and eight identical lines about the free runners
     * read as eight measurements rather than as one. */
    if (world_state.correcting || world_state.phase.taken != 0u) {
        log_info("    the free runners: %u debt(s) taken, %u settled, %u discarded, %u still "
                 "owing; %u integration(s) nudged, worst debt %u thousandth(s) of the travel, "
                 "%u over the slots, %u write fault(s); %u digest(s) arrived late and measured "
                 "nothing",
                 (unsigned)world_state.phase.taken, (unsigned)world_state.phase.settled,
                 (unsigned)world_state.phase.discarded,
                 (unsigned)mp_world_phase_owing(&world_state.phase),
                 (unsigned)world_state.phase.nudges, (unsigned)world_state.phase.worst_milli,
                 (unsigned)world_state.phase.full, (unsigned)stats->write_faults,
                 (unsigned)stats->digests_late);
    } else {
        log_info("    the free runners: this side does not correct the map, so their phase is "
                 "measured and left alone; %u digest(s) arrived late",
                 (unsigned)stats->digests_late);
    }

    for (type = 0; type < MP_WORLD_MOVER_TYPES; ++type) {
        const mp_world_type_stats_t *row = &stats->type[type];

        if (row->compared == 0u) {
            continue;
        }
        log_info("    mover type %u: %u compared, worst %u thousandth(s) of the travel apart, "
                 "under 1/256 %u, under 1/64 %u, under 1/16 %u, under 1/4 %u, beyond %u; "
                 "%u direction and %u type disagreement(s)",
                 (unsigned)type, (unsigned)row->compared, (unsigned)row->worst_milli,
                 (unsigned)row->bucket[0], (unsigned)row->bucket[1], (unsigned)row->bucket[2],
                 (unsigned)row->bucket[3], (unsigned)row->bucket[4],
                 (unsigned)row->dir_mismatch, (unsigned)row->type_mismatch);
    }
}
