/* mp_fog_viewers.c: the fog of a room, played by each machine for its own player. See the
 * header.
 *
 * SIZE NOTE: a little over six hundred lines. The runs, their drive and the report share one state
 * record with the reading of this side's own copy of the level's scripts. That reading,
 * level_bound, class_script and read_entry with the counters they keep, is the seam if this
 * grows. */
#include "mp_fog_viewers.h"

#include "mp_cells.h"
#include "mp_enemy_bind.h"
#include "mp_enemy_sync.h"
#include "mp_enemy_wire.h"
#include "mp_fog_viewers_rule.h"
#include "mp_level_state_bind.h"
#include "mp_level_state_drawn.h"
#include "mp_level_state_fog.h"
#include "mp_level_state_internal.h"

#include "common/logging.h"
#include "common/memory.h"
#include "common/text.h"

#include <math.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

/* The level's scripts: how many, the bytes of the buffer that holds them, and the array of their
 * records at the head of that buffer. The records follow the array back to back. */
#define WORLD_SCRIPT_COUNT 0x1F8u
#define WORLD_SCRIPT_BYTES 0x1FCu
#define WORLD_SCRIPTS      0x200u

/* One script record: its entry count, its operand pool, and its entries of eight bytes from +0x20,
 * each a 16 bit opcode, a 16 bit branch and a 32 bit operand. */
#define SCRIPT_ENTRY_COUNT 0x00u
#define SCRIPT_POOL        0x0Cu
#define SCRIPT_ENTRIES     0x20u
#define ENTRY_BYTES        8u
#define SCRIPT_ENTRY_CAP   8192u   /* a count past this is not a script */
#define SCRIPT_COUNT_CAP   1024u

/* An actor: the script it runs, the state of that script, and where it stands. */
#define ACTOR_SCRIPT 0x00u
#define ACTOR_STATE  0x7Cu
#define ACTOR_POS    0xD0u

/* The player's object, and where it stands: the place a script's distance test measures to. */
#define OBJECT_POS 0x18u

/* The most scripts of one level played per viewer; the shipped levels have one. */
#define BOUND_MAX 4u

#define RUNS MP_LEVEL_STATE_FOG_VIEWERS_MAX

typedef struct script_view {
    uintptr_t record;
    uint32_t  pool;
    uint32_t  pool_words;   /* up to the next record, so no read leaves this script */
} script_view_t;

typedef struct bound_script {
    uint16_t       script;
    uint32_t       fingerprint;
    mp_fog_bound_t bound;
} bound_script_t;

typedef struct run {
    bool            used;
    bool            refused;   /* a client whose own copy of the script did not bind */
    bool            seen;      /* the host found its actor in this substep */
    uint16_t        key;
    uint8_t         life;
    uint16_t        script;
    uint8_t         flags;     /* MP_LEVEL_STATE_FOG_VIEWER_ */
    float           place[3];
    mp_fog_viewer_t viewer;
} run_t;

/* Kept across the reset, like the rest of the report: a level's binding is said before the reset
 * that ends it. */
typedef struct viewer_counts {
    uint32_t scripts;
    uint32_t with_fog;
    uint32_t bound;
    uint32_t shared;
    uint32_t unclassed;
    uint32_t wrong_entries;
    uint32_t runs;
    uint32_t runs_refused;
    uint32_t unnamed;   /* the host's, runs on a key past the wire's */
    uint32_t transitions[MP_FOG_TRANSITIONS];
    uint32_t commands;
    uint32_t withheld;
    uint32_t not_running;
    uint32_t started_away;
    uint32_t green_at_end;
    uint32_t ended_unseen;
    uint32_t banked;
    uint32_t dead;
    uint32_t no_body;
    uint32_t gone;
    uint32_t entries_read;
    uint32_t operand[MP_FOG_OPERAND_PAST_POOL + 1];   /* by mp_fog_operand_t */
} viewer_counts_t;

typedef struct viewers_state {
    bool            attempted;   /* the scripts of this world and level were read */
    bool            ticked;      /* the host's own viewer has run, last in substep `ticked_at` */
    uint32_t        ticked_at;
    uint32_t        world;
    uint16_t        level;
    size_t          bound_count;
    bound_script_t  bound[BOUND_MAX];
    run_t           run[RUNS];
    viewer_counts_t n;
} viewers_state_t;

static viewers_state_t viewers;

static bool read_entry(const void *script, uint32_t index, mp_fog_script_entry_t *out)
{
    const script_view_t *view = (const script_view_t *)script;
    uint8_t              raw[ENTRY_BYTES];
    int16_t              opcode = 0;
    int32_t              operand = 0;

    memset(out, 0, sizeof *out);
    if (!memory_try_read(view->record + SCRIPT_ENTRIES + (uintptr_t)index * ENTRY_BYTES, raw,
                         sizeof raw)) {
        return false;
    }
    memcpy(&opcode, raw, sizeof opcode);
    memcpy(&operand, raw + 4, sizeof operand);
    out->opcode = opcode;
    ++viewers.n.entries_read;
    switch (mp_fog_viewers_operand(opcode, operand, view->pool_words)) {
    case MP_FOG_OPERAND_INLINE:
        out->word[0] = operand;
        break;
    case MP_FOG_OPERAND_POOL:
        /* The handler reads the pool at the operand; words that do not read leave the opcode
         * alone, which no row and no fingerprint matches. */
        ++viewers.n.operand[MP_FOG_OPERAND_POOL];
        (void)memory_try_read((uintptr_t)view->pool + (uintptr_t)operand * 4u, out->word,
                              sizeof out->word);
        break;
    case MP_FOG_OPERAND_NAME:
        ++viewers.n.operand[MP_FOG_OPERAND_NAME];
        break;
    case MP_FOG_OPERAND_UNUSED:
        ++viewers.n.operand[MP_FOG_OPERAND_UNUSED];
        break;
    case MP_FOG_OPERAND_PAST_POOL:
    default:
        ++viewers.n.operand[MP_FOG_OPERAND_PAST_POOL];
        break;
    }
    return true;
}

static const char *mode_name(int32_t mode)
{
    static const char *const NAMES[MP_FOG_COMPARISONS] = { "==", ">=", "<=", "!=", ">", "<" };

    return mode >= 0 && mode < MP_FOG_COMPARISONS ? NAMES[mode] : "?";
}

/* One script of the level, classed by its fingerprint and, for a per viewer one, bound. `end` is
 * where its record ends: the next record, or the end of the level's script buffer. */
static void class_script(uint16_t index, uintptr_t record, uint32_t end)
{
    script_view_t              view;
    int32_t                    count = 0;
    uint32_t                   fingerprint = 0;
    uint32_t                   fogs = 0;
    const mp_fog_script_row_t *row;
    bound_script_t            *slot;
    uint32_t                   wrong;

    view.record     = record;
    view.pool       = 0u;
    view.pool_words = 0u;
    if (record == 0u || !memory_try_read(record + SCRIPT_ENTRY_COUNT, &count, sizeof count) ||
        !memory_read_u32(record + SCRIPT_POOL, &view.pool)) {
        return;
    }
    view.pool_words = mp_fog_viewers_pool_words(view.pool, end);
    if (count <= 0 || (uint32_t)count > SCRIPT_ENTRY_CAP ||
        !mp_fog_viewers_fingerprint((uint32_t)count, read_entry, &view, &fingerprint, &fogs) ||
        fogs == 0u) {
        return;
    }
    ++viewers.n.with_fog;
    row = mp_fog_viewers_row_of(fingerprint);
    if (row == NULL) {
        ++viewers.n.unclassed;
        log_info("the fog of the viewer: script %u of this level (fingerprint %08X, %u fog "
                 "command(s)) is in no class, so its fog is the level's and shared",
                 (unsigned)index, (unsigned)fingerprint, (unsigned)fogs);
        return;
    }
    if (row->cls != MP_FOG_CLASS_PER_VIEWER) {
        ++viewers.n.shared;
        return;
    }
    if (viewers.bound_count >= BOUND_MAX) {
        return;
    }
    slot  = &viewers.bound[viewers.bound_count];
    wrong = mp_fog_viewers_bind(row, (uint32_t)count, read_entry, &view, &slot->bound);
    if (wrong != 0u) {
        viewers.n.wrong_entries += wrong;
        log_warning("the fog of the viewer: script %u of this level is %s by its fingerprint, and "
                    "%u of its entries did not hold what the table says, so its fog stays shared",
                    (unsigned)index, row->name, (unsigned)wrong);
        return;
    }
    slot->script      = index;
    slot->fingerprint = fingerprint;
    ++viewers.bound_count;
    ++viewers.n.bound;
    log_info("the fog of the viewer: script %u of this level is %s (fingerprint %08X) and is "
             "played for each player alone: left at %s %.4f and entered at %s %.4f, the end in "
             "state %d, as this side's own copy of the script says",
             (unsigned)index, row->name, (unsigned)fingerprint, mode_name(slot->bound.leave.mode),
             (double)slot->bound.leave.distance, mode_name(slot->bound.enter.mode),
             (double)slot->bound.enter.distance, (int)slot->bound.end_state);
}

/* The table, said once when the first level's scripts are read, so a log names the classes a
 * build knows before it names any script. */
static void say_the_table(void)
{
    static bool said;
    char        line[160];
    size_t      at = 0;
    size_t      i;

    if (said) {
        return;
    }
    said    = true;
    line[0] = '\0';
    for (i = 0; i < mp_fog_viewers_rows() && at + 1u < sizeof line; ++i) {
        const mp_fog_script_row_t *row = mp_fog_viewers_row(i);

        at += text_format(line + at, sizeof line - at, "%s%s %s", i == 0u ? "" : ", ", row->name,
                          row->cls == MP_FOG_CLASS_PER_VIEWER ? "per viewer" : "shared");
    }
    log_info("the fog of the viewer knows %u fog script(s) by their fingerprints: %s; any other "
             "script that sets fog is shared", (unsigned)mp_fog_viewers_rows(), line);
}

/* The level's scripts, read once for each world and level, and only where the director can be
 * called: without it this side could play no viewer's fog, and refusing the script's own would
 * leave the room with none. */
static bool level_bound(void)
{
    uint32_t world = mp_level_state_bind_world();
    uint16_t level = 0;
    int32_t  count = 0;
    int32_t  bytes = 0;
    uint32_t table = 0;
    uint32_t i;

    if (world == 0u || !mp_level_state_level(&level) || !mp_level_state_bind_has_director()) {
        return false;
    }
    if (viewers.attempted && viewers.world == world && viewers.level == level) {
        return viewers.bound_count != 0u;
    }
    viewers.attempted   = true;
    viewers.world       = world;
    viewers.level       = level;
    viewers.bound_count = 0u;
    say_the_table();
    if (!memory_try_read((uintptr_t)world + WORLD_SCRIPT_COUNT, &count, sizeof count) ||
        !memory_read_u32((uintptr_t)world + WORLD_SCRIPTS, &table) || count < 0 ||
        (uint32_t)count > SCRIPT_COUNT_CAP || table == 0u) {
        return false;
    }
    /* Without the buffer's length the last script has no end, and no pool word of it is read. */
    if (!memory_try_read((uintptr_t)world + WORLD_SCRIPT_BYTES, &bytes, sizeof bytes) ||
        bytes < 0) {
        bytes = 0;
    }
    viewers.n.scripts += (uint32_t)count;
    for (i = 0; i < (uint32_t)count; ++i) {
        uint32_t record = 0;
        uint32_t end    = 0;

        if (!memory_read_u32((uintptr_t)table + (uintptr_t)i * 4u, &record)) {
            continue;
        }
        if (i + 1u < (uint32_t)count) {
            (void)memory_read_u32((uintptr_t)table + (uintptr_t)(i + 1u) * 4u, &end);
        } else if (bytes != 0) {
            end = table + (uint32_t)bytes;
        }
        class_script((uint16_t)i, (uintptr_t)record, end);
    }
    return viewers.bound_count != 0u;
}

static const mp_fog_bound_t *bound_for(uint16_t script)
{
    size_t i;

    for (i = 0; i < viewers.bound_count; ++i) {
        if (viewers.bound[i].script == script) {
            return &viewers.bound[i].bound;
        }
    }
    return NULL;
}

/* Where this machine's own player stands, as a distance test measures it: the player's object. A
 * far player's bank would hold a puppet's, and a dead player keeps his state. */
static bool own_body(float at[3], bool *dead)
{
    uintptr_t block  = mp_cells_address(MP_CELL_HERO_BLOCK);
    uint32_t  object = 0;
    uint32_t  corpse = 0;

    *dead = false;
    if (mp_level_state_banked()) {
        ++viewers.n.banked;
        return false;
    }
    if (block == 0u || !memory_try_read_u32(block + MP_HERO_BLOCK_HACTOR, &object) ||
        object == 0u || !memory_try_read((uintptr_t)object + OBJECT_POS, at, 3u * sizeof(float))) {
        ++viewers.n.no_body;
        return false;
    }
    if (memory_try_read_u32(block + MP_HERO_BLOCK_DEAD, &corpse) && corpse == 1u) {
        ++viewers.n.dead;
        *dead = true;
    }
    return true;
}

static void play_half(const mp_fog_bound_t *bound, uint8_t transition, uint8_t half)
{
    const mp_fog_half_t *h = &bound->half[transition][half];
    size_t               i;

    for (i = 0; i < h->count; ++i) {
        if (mp_level_state_fog_play(MP_LEVEL_DRAWN_VIEWER, h->command[i].command, h->command[i].a1,
                                    h->command[i].a2, 0u)) {
            ++viewers.n.commands;
        }
    }
}

/* One substep of this side's own viewer of one run. */
static void drive(run_t *run, const mp_fog_bound_t *bound)
{
    mp_fog_input_t in;
    mp_fog_step_t  step;
    float          body[3];

    memset(&in, 0, sizeof in);
    in.active = (run->flags & MP_LEVEL_STATE_FOG_VIEWER_ACTIVE) != 0u;
    in.ended  = (run->flags & MP_LEVEL_STATE_FOG_VIEWER_ENDED) != 0u;
    if (in.active && !in.ended && run->viewer.pending >= (uint8_t)MP_FOG_TRANSITIONS &&
        own_body(body, &in.dead)) {
        float dx = run->place[0] - body[0];
        float dy = run->place[1] - body[1];
        float dz = run->place[2] - body[2];

        in.measured = true;
        in.distance = sqrtf(dx * dx + dy * dy + dz * dz);
    }
    step = mp_fog_viewers_decide(&run->viewer, bound, &in);
    switch ((mp_fog_outcome_t)step.outcome) {
    case MP_FOG_PLAY:
        if (step.half == 0u) {
            ++viewers.n.transitions[step.transition];
        }
        viewers.n.green_at_end += step.green_at_end ? 1u : 0u;
        play_half(bound, step.transition, step.half);
        break;
    case MP_FOG_STARTED_AWAY:
        ++viewers.n.started_away;
        break;
    case MP_FOG_ENDED_UNSEEN:
        ++viewers.n.ended_unseen;
        break;
    case MP_FOG_UNDECIDED_DEAD:
    case MP_FOG_UNDECIDED_UNMEASURED:
    case MP_FOG_NOTHING:
    default:
        break;
    }
}

mp_fog_viewers_verdict_t mp_fog_viewers_hear(uintptr_t actor)
{
    int32_t script = -1;

    if (actor == 0u || !level_bound() ||
        !memory_try_read(actor + ACTOR_SCRIPT, &script, sizeof script) || script < 0 ||
        script > (int32_t)UINT16_MAX || bound_for((uint16_t)script) == NULL) {
        return MP_FOG_VIEWERS_SHARED;
    }
    /* Refused only while this side's own viewer runs: it runs at the end of a substep with a
     * peer to send to, and a host with none would otherwise leave its own room with no fog. Let
     * through, it is still the room's and never the level's fog, which a client joining later is
     * told. */
    if (!viewers.ticked || mp_level_state_substep() - viewers.ticked_at > 1u) {
        ++viewers.n.not_running;
        return MP_FOG_VIEWERS_OWN_ENGINE;
    }
    ++viewers.n.withheld;
    return MP_FOG_VIEWERS_WITHHELD;
}

static run_t *run_for(uint16_t key, uint8_t life, uint16_t script)
{
    run_t *free_slot = NULL;
    size_t i;

    for (i = 0; i < RUNS; ++i) {
        run_t *run = &viewers.run[i];

        if (run->used && run->key == key && run->life == life && run->script == script) {
            return run;
        }
        if (!run->used && free_slot == NULL) {
            free_slot = run;
        }
    }
    if (free_slot != NULL) {
        memset(free_slot, 0, sizeof *free_slot);
        free_slot->used   = true;
        free_slot->key    = key;
        free_slot->life   = life;
        free_slot->script = script;
        mp_fog_viewers_viewer_init(&free_slot->viewer);
        ++viewers.n.runs;
    }
    return free_slot;
}

/* The host's walk: every actor that runs a script this side plays per viewer is a run. */
static void visit(uintptr_t actor, void *user)
{
    int32_t               script = -1;
    int32_t               state = -1;
    uint32_t              key = 0;
    uint8_t               life = 0;
    const mp_fog_bound_t *bound;
    run_t                *run;

    (void)user;
    if (!memory_try_read(actor + ACTOR_SCRIPT, &script, sizeof script) || script < 0 ||
        script > (int32_t)UINT16_MAX) {
        return;
    }
    bound = bound_for((uint16_t)script);
    if (bound == NULL || !mp_enemy_bind_index(actor, &key) || key > UINT16_MAX) {
        return;
    }
    /* The life is the census's, which runs after this walk: an actor woken in this substep waits
     * one, until the census reads it under its key, as the loops of the actors wait for theirs. */
    if (mp_enemy_sync_actor_for(key) != actor || !mp_enemy_sync_generation(key, &life)) {
        return;
    }
    run = run_for((uint16_t)key, life, (uint16_t)script);
    if (run == NULL) {
        return;
    }
    run->seen = true;
    (void)memory_try_read(actor + ACTOR_POS, run->place, sizeof run->place);
    if (memory_try_read(actor + ACTOR_STATE, &state, sizeof state) && state == bound->end_state) {
        run->flags = (uint8_t)MP_LEVEL_STATE_FOG_VIEWER_ENDED;
    } else if ((run->flags & MP_LEVEL_STATE_FOG_VIEWER_ENDED) == 0u) {
        run->flags = (uint8_t)MP_LEVEL_STATE_FOG_VIEWER_ACTIVE;
    }
}

void mp_fog_viewers_host_tick(void)
{
    bool   complete = false;
    size_t i;

    if (!level_bound()) {
        return;
    }
    viewers.ticked    = true;
    viewers.ticked_at = mp_level_state_substep();
    for (i = 0; i < RUNS; ++i) {
        viewers.run[i].seen = false;
    }
    (void)mp_enemy_bind_walk_whole(visit, NULL, &complete, NULL);
    for (i = 0; i < RUNS; ++i) {
        run_t *run = &viewers.run[i];

        if (!run->used) {
            continue;
        }
        /* An actor that is gone takes its script's end with it: the only way the shipped script
         * removes its actor is out of its end state, a hundred and ninety nine substeps in. */
        if (!run->seen && complete && (run->flags & MP_LEVEL_STATE_FOG_VIEWER_ENDED) == 0u) {
            run->flags = (uint8_t)MP_LEVEL_STATE_FOG_VIEWER_ENDED;
            ++viewers.n.gone;
        }
        drive(run, bound_for(run->script));
    }
}

void mp_fog_viewers_describe(mp_level_state_note_t *note)
{
    size_t i;
    size_t k;

    if (note == NULL) {
        return;
    }
    for (i = 0; i < RUNS; ++i) {
        const run_t           *run = &viewers.run[i];
        mp_level_fog_viewer_t *out;

        if (!run->used || note->fog_viewers >= (uint8_t)MP_LEVEL_STATE_FOG_VIEWERS_MAX) {
            continue;
        }
        /* A key the enemy record cannot name would have the encoder refuse the whole note. */
        if (run->key >= MP_WIRE_KEY_COUNT) {
            ++viewers.n.unnamed;
            continue;
        }
        out = &note->fog_viewer[note->fog_viewers];
        memset(out, 0, sizeof *out);
        out->key    = run->key;
        out->life   = run->life;
        out->script = run->script;
        out->flags  = run->flags;
        for (k = 0; k < 3u; ++k) {
            uint32_t wire = 0;

            (void)mp_enemy_wire_put_position(run->place[k], &wire);
            out->place[k] = (uint16_t)wire;
        }
        ++note->fog_viewers;
        note->parts |= (uint8_t)MP_LEVEL_STATE_PART_FOG_VIEWERS;
    }
}

void mp_fog_viewers_client_take(const mp_level_state_note_t *note, bool first)
{
    size_t i;
    size_t k;

    if (first) {
        memset(viewers.run, 0, sizeof viewers.run);
    }
    if (note == NULL || (note->parts & MP_LEVEL_STATE_PART_FOG_VIEWERS) == 0u) {
        return;
    }
    (void)level_bound();
    for (i = 0; i < note->fog_viewers; ++i) {
        const mp_level_fog_viewer_t *in  = &note->fog_viewer[i];
        run_t                       *run = run_for(in->key, in->life, in->script);

        if (run == NULL) {
            continue;
        }
        run->flags = in->flags;
        for (k = 0; k < 3u; ++k) {
            run->place[k] = mp_enemy_wire_get_position(in->place[k]);
        }
        if (bound_for(run->script) == NULL && !run->refused) {
            run->refused = true;
            ++viewers.n.runs_refused;
            log_warning("the fog of the viewer: the host plays script %u per viewer and this side "
                        "cannot, so this player sees none of that room's fog",
                        (unsigned)run->script);
        }
    }
}

void mp_fog_viewers_client_tick(void)
{
    size_t i;

    if (!level_bound()) {
        return;
    }
    for (i = 0; i < RUNS; ++i) {
        const mp_fog_bound_t *bound;

        if (!viewers.run[i].used) {
            continue;
        }
        bound = bound_for(viewers.run[i].script);
        if (bound != NULL) {
            drive(&viewers.run[i], bound);
        }
    }
}

void mp_fog_viewers_reset(void)
{
    viewer_counts_t n = viewers.n;

    memset(&viewers, 0, sizeof viewers);
    viewers.n = n;
}

void mp_fog_viewers_report(bool host)
{
    const viewer_counts_t *n = &viewers.n;

    log_info("  the fog of the viewer (%s): %u script(s) read, %u with fog (%u played per viewer "
             "here, %u shared, %u in no class), %u entr(ies) that did not hold what the table "
             "says; %u run(s), %u this side could not play, %u not said for a key past the "
             "wire's; transitions start %u, leave %u, "
             "enter %u, end %u, %u command(s) played; %u withheld on the host (none journaled), "
             "%u left to this side's own engine while its viewer did not run (none journaled); "
             "started away and left clear %u; left green at the end %u; ended before it began "
             "here %u; undecided "
             "in a bank window %u, while dead %u, with no body %u; %u run(s) gone without their "
             "end",
             host ? "host" : "client", (unsigned)n->scripts, (unsigned)n->with_fog,
             (unsigned)n->bound, (unsigned)n->shared, (unsigned)n->unclassed,
             (unsigned)n->wrong_entries, (unsigned)n->runs, (unsigned)n->runs_refused,
             (unsigned)n->unnamed,
             (unsigned)n->transitions[MP_FOG_START], (unsigned)n->transitions[MP_FOG_LEAVE],
             (unsigned)n->transitions[MP_FOG_ENTER], (unsigned)n->transitions[MP_FOG_END],
             (unsigned)n->commands, (unsigned)n->withheld, (unsigned)n->not_running,
             (unsigned)n->started_away,
             (unsigned)n->green_at_end, (unsigned)n->ended_unseen, (unsigned)n->banked,
             (unsigned)n->dead, (unsigned)n->no_body, (unsigned)n->gone);
    log_info("  the fog of the viewer's own copy (%s): %u entr(ies) read, %u pool read(s) made, %u "
             "not made because the entry names a script or a state rather than a pool slot, %u "
             "not made for an opcode whose words no row reads, %u refused past the script's own "
             "pool",
             host ? "host" : "client", (unsigned)n->entries_read,
             (unsigned)n->operand[MP_FOG_OPERAND_POOL], (unsigned)n->operand[MP_FOG_OPERAND_NAME],
             (unsigned)n->operand[MP_FOG_OPERAND_UNUSED],
             (unsigned)n->operand[MP_FOG_OPERAND_PAST_POOL]);
}
