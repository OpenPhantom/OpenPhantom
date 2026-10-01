/* mp_fog_viewers.c: the fog of a room and the level's shared fog on a host, over an engine the test
 * plays.
 *
 * SIZE NOTE: over six hundred lines, two thirds of them the engine: the world with the gas room's
 * script laid out entry by entry, the census, the director and the log. The seam, if this grows, is
 * that engine into a stand-in file of its own, as the voice's tests keep theirs.
 *
 * The real modules run here: the viewers' walk and drive, and the director's fog hand with its
 * model. The engine under them is played in memory: a world holding a copy of FEDSHIP's gas room
 * script laid out the way the engine keeps a script, the actors that run scripts, the hero block
 * with the player's object, a director that records what it is handed, the journal, and a census
 * that reads an actor only once the census of a substep has run. A host substep runs its scripts
 * first, then the level's state, whose fog finds the room's runs, and then the census; the waking
 * of the room's actor falls between the census before and the one after.
 *
 * What would be silent if it were wrong:
 *
 *   the waking of the gas room making two runs of one actor, the first of which then plays its end,
 *   so that the host sees the level's thin band while it stands in the room and every counter says
 *   the room started twice;
 *   a command of the room's script the host let through because its own viewer did not run, landing
 *   in the fog every client is told, which turns the whole level green for a player joining later;
 *   the shared fog counting a ramp down at 1/32 s a substep on a machine that runs 1/64 s.
 */
#include "unittest.h"

#include "mp_cells.h"
#include "mp_enemy_bind.h"
#include "mp_enemy_sync.h"
#include "mp_fog_viewers.h"
#include "mp_level_state_bind.h"
#include "mp_level_state_drawn.h"
#include "mp_level_state_fog.h"
#include "mp_level_state_internal.h"
#include "mp_level_state_rule.h"

#include "common/logging.h"
#include "common/text.h"

#include <math.h>
#include <stdarg.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

/* The engine's layout of what the module reads: the world's script count, the length of the buffer
 * that holds the scripts and the table at its head, a script's entry count, operand pool and
 * entries of eight bytes, an actor's script, state and place, and the player's object's place. */
#define WORLD_SCRIPT_COUNT 0x1F8u
#define WORLD_SCRIPT_BYTES 0x1FCu
#define WORLD_SCRIPTS      0x200u
#define SCRIPT_ENTRY_COUNT 0x00u
#define SCRIPT_POOL        0x0Cu
#define SCRIPT_ENTRIES     0x20u
#define ENTRY_BYTES        8u
#define ACTOR_SCRIPT       0x00u
#define ACTOR_STATE        0x7Cu
#define ACTOR_POS          0xD0u
#define OBJECT_POS         0x18u

#define GASE_ENTRIES 65u
#define GASE_SCRIPT  1u      /* its index in the test's level; script 0 is no record */
#define SCRIPTS      2u
#define LEVEL        57u
#define KEYS         16u
#define ACTORS       4u
#define PLAYED_MAX   256u

/* The floats the gas room's commands carry, as their bits. */
#define F_10_001  0x41200419u   /* the room's start */
#define F_16_002  0x41800347u   /* the room's end, and the start the way out sets */
#define F_56_0056 0x426005BDu   /* the level's end, which the way out and the script's end set */
#define F_1_0001  0x3F800347u   /* the green ramp's target */
#define F_1       0x3F800000u
#define F_16      0x41800000u

typedef struct script_entry {
    uint32_t index;
    int32_t  opcode;
    int32_t  word[3];
} script_entry_t;

/* FEDSHIP script 51 "gase", the entries the table reads, with the words as the level carries them.
 * Every other entry is a yield. */
static const script_entry_t GASE[] = {
    { 1u, 0x4602, { 200, 0, 0 } },
    { 2u, 0x4400, { 6, 0, 0 } },
    { 7u, 0x0606, { 6, 0, (int32_t)F_10_001 } },
    { 9u, 0x0606, { 7, 0, (int32_t)F_16_002 } },
    { 11u, 0x4400, { 1, 0, 0 } },
    { 16u, 0x0606, { 11, 20, (int32_t)F_1_0001 } },
    { 18u, 0x4400, { 2, 0, 0 } },
    { 28u, 0x0107, { 0, 1, (int32_t)0x41200069u } },
    { 29u, 0x0606, { 12, 1, (int32_t)F_16_002 } },
    { 31u, 0x4400, { 3, 0, 0 } },
    { 35u, 0x0606, { 6, 0, (int32_t)F_16_002 } },
    { 37u, 0x0606, { 7, 0, (int32_t)F_56_0056 } },
    { 39u, 0x4400, { 4, 0, 0 } },
    { 42u, 0x0107, { 0, 2, (int32_t)0x40600093u } },
    { 43u, 0x0606, { 6, 0, (int32_t)F_10_001 } },
    { 45u, 0x0606, { 7, 0, (int32_t)F_16_002 } },
    { 47u, 0x4400, { 5, 0, 0 } },
    { 51u, 0x0606, { 11, 1, (int32_t)0x40000347u } },
    { 53u, 0x4400, { 2, 0, 0 } },
    { 60u, 0x0606, { 6, 0, (int32_t)F_16_002 } },
    { 62u, 0x0606, { 7, 0, (int32_t)F_56_0056 } },
};

#define GASE_ROWS (sizeof GASE / sizeof GASE[0])

/* The head of the script and its seven state labels, whose operands are names and not pool
 * indices. Read as indices they reach far past the level's scripts. */
static const struct {
    uint32_t index;
    char     name[5];
} GASE_NAMES[] = {
    { 0u, "gase" }, { 3u, "0sta" }, { 12u, "1gas" }, { 19u, "2wai" }, { 30u, "3res" },
    { 38u, "4wai" }, { 46u, "5reg" }, { 54u, "zzzz" },
};

#define GASE_NAME_ROWS (sizeof GASE_NAMES / sizeof GASE_NAMES[0])

typedef struct played {
    uintptr_t actor;
    int32_t   command;
    int32_t   a1;
    uint32_t  a2;
} played_t;

typedef struct engine {
    uint8_t  world[0x300];
    uint32_t table[SCRIPTS];
    uint8_t  script[SCRIPT_ENTRIES + GASE_ENTRIES * ENTRY_BYTES];
    uint32_t pool[3u * GASE_ROWS];
    uint8_t  actor[ACTORS][0x100];
    uint8_t  hero_block[0x400];
    uint8_t  hero_object[0x40];
    float    frame_delta;
    bool     frame_delta_cell;

    uintptr_t walked[ACTORS];
    size_t    walked_count;
    uint32_t  key_of[ACTORS];

    bool      was_live[KEYS];
    uint8_t   generation[KEYS];
    uintptr_t census_actor[KEYS];

    uint32_t substep;
    played_t played[PLAYED_MAX];
    size_t   played_count;
    uint32_t journaled;
    uint8_t  stand_in[0x204];
} engine_t;

static engine_t eng;

/* ==============================================================================================
 * The log, kept so the report can be read back. Every entry of common/logging is defined,
 * so the linker never takes the library's file.
 * ============================================================================================ */

#define LINES_KEPT 256u
#define LINE_BYTES 1100u

static char   kept[LINES_KEPT][LINE_BYTES];
static size_t kept_count;

static void keep(const char *format, va_list arguments)
{
    char *line = kept[kept_count % LINES_KEPT];

    (void)text_vformat(line, LINE_BYTES, format, arguments);
    ++kept_count;
}

void log_init(const char *feature_name, bool truncate)
{
    (void)feature_name;
    (void)truncate;
}

void log_shutdown(void)
{
}

void log_info(const char *format, ...)
{
    va_list arguments;

    va_start(arguments, format);
    keep(format, arguments);
    va_end(arguments);
}

void log_warning(const char *format, ...)
{
    va_list arguments;

    va_start(arguments, format);
    keep(format, arguments);
    va_end(arguments);
}

void log_error(const char *format, ...)
{
    va_list arguments;

    va_start(arguments, format);
    keep(format, arguments);
    va_end(arguments);
}

const char *log_path(void)
{
    return "";
}

/* The newest kept line that begins with `prefix`, or NULL. */
static const char *newest_line(const char *prefix)
{
    size_t count = kept_count < LINES_KEPT ? kept_count : LINES_KEPT;
    size_t i;

    for (i = 0; i < count; ++i) {
        const char *line = kept[(kept_count - 1u - i) % LINES_KEPT];

        if (strncmp(line, prefix, strlen(prefix)) == 0) {
            return line;
        }
    }
    return NULL;
}

/* The number written right before `marker` in `line`, or -1. */
static long number_before(const char *line, const char *marker)
{
    const char *at = line != NULL ? strstr(line, marker) : NULL;
    const char *digits;

    if (at == NULL || at == line) {
        return -1;
    }
    digits = at;
    while (digits > line && digits[-1] >= '0' && digits[-1] <= '9') {
        --digits;
    }
    return digits == at ? -1 : strtol(digits, NULL, 10);
}

/* The number written right after `marker` in `line`, or -1. */
static long number_after(const char *line, const char *marker)
{
    const char *at = line != NULL ? strstr(line, marker) : NULL;

    if (at == NULL) {
        return -1;
    }
    at += strlen(marker);
    return (*at >= '0' && *at <= '9') ? strtol(at, NULL, 10) : -1;
}

/* ==============================================================================================
 * What the modules ask of the engine and of the rest of the multiplayer.
 * ============================================================================================ */

uintptr_t mp_cells_address(mp_cell_t cell)
{
    switch (cell) {
    case MP_CELL_HERO_BLOCK:
        return (uintptr_t)eng.hero_block;
    case MP_CELL_FRAME_DELTA:
        return eng.frame_delta_cell ? (uintptr_t)&eng.frame_delta : 0u;
    default:
        return 0u;
    }
}

uint32_t mp_enemy_bind_walk_whole(mp_enemy_bind_visit_fn_t visit, void *user, bool *complete,
                                  uint32_t *capacity)
{
    size_t i;

    for (i = 0; i < eng.walked_count; ++i) {
        visit(eng.walked[i], user);
    }
    if (complete != NULL) {
        *complete = true;
    }
    if (capacity != NULL) {
        *capacity = ACTORS;
    }
    return (uint32_t)eng.walked_count;
}

bool mp_enemy_bind_index(uintptr_t actor, uint32_t *out)
{
    size_t i;

    for (i = 0; i < ACTORS; ++i) {
        if (actor == (uintptr_t)eng.actor[i]) {
            *out = eng.key_of[i];
            return true;
        }
    }
    return false;
}

/* The census as the host's takes it for a placement: a key live now that was not live at the
 * census before counts a new life, and the actor it read stands for the key until the next. The
 * generation of a key the census never counted answers nothing. */
static void census(void)
{
    uint32_t key;

    for (key = 0; key < KEYS; ++key) {
        uintptr_t live = 0u;
        size_t    i;

        for (i = 0; i < eng.walked_count; ++i) {
            uint32_t k = KEYS;

            if (mp_enemy_bind_index(eng.walked[i], &k) && k == key) {
                live = eng.walked[i];
            }
        }
        if (live != 0u && !eng.was_live[key]) {
            ++eng.generation[key];
        }
        eng.was_live[key]     = live != 0u;
        eng.census_actor[key] = live;
    }
}

uintptr_t mp_enemy_sync_actor_for(uint32_t key)
{
    return key < KEYS ? eng.census_actor[key] : 0u;
}

bool mp_enemy_sync_generation(uint32_t key, uint8_t *out)
{
    if (key >= KEYS || out == NULL || eng.generation[key] == 0u) {
        return false;
    }
    *out = eng.generation[key];
    return true;
}

uint32_t mp_level_state_bind_world(void)
{
    return (uint32_t)(uintptr_t)eng.world;
}

bool mp_level_state_bind_has_director(void)
{
    return true;
}

bool mp_level_state_bind_call_director(uintptr_t actor, int32_t command, int32_t a1, int32_t a2)
{
    if (eng.played_count < PLAYED_MAX) {
        played_t *p = &eng.played[eng.played_count++];

        p->actor   = actor;
        p->command = command;
        p->a1      = a1;
        p->a2      = (uint32_t)a2;
    }
    return true;
}

uintptr_t mp_level_state_bind_stand_in(void)
{
    return (uintptr_t)eng.stand_in;
}

void mp_level_state_drawn_note(const char *whose, int32_t command, uint16_t sequence)
{
    (void)whose;
    (void)command;
    (void)sequence;
}

uint16_t mp_level_state_journal_note(uint8_t kind, uint8_t a, uint16_t b, uint32_t c)
{
    (void)kind;
    (void)a;
    (void)b;
    (void)c;
    return (uint16_t)++eng.journaled;
}

bool mp_level_state_level(uint16_t *identity)
{
    if (identity != NULL) {
        *identity = LEVEL;
    }
    return true;
}

uint32_t mp_level_state_substep(void)
{
    return eng.substep;
}

bool mp_level_state_banked(void)
{
    return false;
}

/* ==============================================================================================
 * The world, the actors and a host's substep.
 * ============================================================================================ */

static void put_i16(uint8_t *at, int16_t value)
{
    memcpy(at, &value, sizeof value);
}

static void put_i32(uint8_t *at, int32_t value)
{
    memcpy(at, &value, sizeof value);
}

static void put_u32(uint8_t *at, uint32_t value)
{
    memcpy(at, &value, sizeof value);
}

/* The gas room's script as the engine keeps it: an inline operand in the entry, any other three
 * words in the pool at the index the entry names. */
static void build_the_script(void)
{
    size_t i;

    memset(eng.script, 0, sizeof eng.script);
    put_i32(eng.script + SCRIPT_ENTRY_COUNT, (int32_t)GASE_ENTRIES);
    put_u32(eng.script + SCRIPT_POOL, (uint32_t)(uintptr_t)eng.pool);
    for (i = 0; i < GASE_ENTRIES; ++i) {
        uint8_t *entry = eng.script + SCRIPT_ENTRIES + i * ENTRY_BYTES;

        put_i16(entry, 0x4106);
        put_i32(entry + 4, 0);
    }
    for (i = 0; i < GASE_NAME_ROWS; ++i) {
        uint8_t *entry = eng.script + SCRIPT_ENTRIES + GASE_NAMES[i].index * ENTRY_BYTES;
        int32_t  name;

        memcpy(&name, GASE_NAMES[i].name, sizeof name);
        put_i16(entry, i == 0u ? 0x0000 : 0x0001);
        put_i32(entry + 4, name);
    }
    for (i = 0; i < GASE_ROWS; ++i) {
        uint8_t *entry = eng.script + SCRIPT_ENTRIES + GASE[i].index * ENTRY_BYTES;

        put_i16(entry, (int16_t)GASE[i].opcode);
        if ((GASE[i].opcode & 0x4000) != 0) {
            put_i32(entry + 4, GASE[i].word[0]);
            continue;
        }
        put_i32(entry + 4, (int32_t)(3u * i));
        memcpy(&eng.pool[3u * i], GASE[i].word, sizeof GASE[i].word);
    }
}

static void fresh(void)
{
    memset(&eng, 0, sizeof eng);
    build_the_script();
    eng.table[GASE_SCRIPT] = (uint32_t)(uintptr_t)eng.script;
    put_i32(eng.world + WORLD_SCRIPT_COUNT, (int32_t)SCRIPTS);
    put_u32(eng.world + WORLD_SCRIPTS, (uint32_t)(uintptr_t)eng.table);
    /* The buffer runs from the table through the last script, the gas room, to its pool's end. */
    put_u32(eng.world + WORLD_SCRIPT_BYTES,
            (uint32_t)((uintptr_t)(eng.pool + 3u * GASE_ROWS) - (uintptr_t)eng.table));
    put_u32(eng.hero_block + MP_HERO_BLOCK_HACTOR, (uint32_t)(uintptr_t)eng.hero_object);
    /* The enemy table's reset takes both, on every way out of a level or a session. */
    mp_fog_viewers_reset();
    mp_level_state_fog_reset();
}

static uintptr_t actor(size_t i)
{
    return (uintptr_t)eng.actor[i];
}

/* An actor woken inside a substep: in the pool from now on, running `script`, one unit from where
 * the player stands. */
static void wake(size_t i, uint32_t key, int32_t script)
{
    float at[3] = { 1.0f, 0.0f, 0.0f };

    memset(eng.actor[i], 0, sizeof eng.actor[i]);
    put_i32(eng.actor[i] + ACTOR_SCRIPT, script);
    put_i32(eng.actor[i] + ACTOR_STATE, 0);
    memcpy(eng.actor[i] + ACTOR_POS, at, sizeof at);
    eng.key_of[i]                    = key;
    eng.walked[eng.walked_count++]   = actor(i);
}

/* A new substep: the count the level's state reads as its substep moves on by one. */
static void next_substep(void)
{
    ++eng.substep;
}

/* The end of a host's substep with a peer: the level's state, whose fog ticks and finds the rooms,
 * and then the census. */
static void host_substep_end(void)
{
    mp_level_state_fog_host_tick();
    mp_fog_viewers_host_tick();
    census();
}

/* One fog command of `who`'s script on the host, as the director's hand hears it. True
 * withholds. */
static bool hear(size_t who, int32_t command, int32_t a1, uint32_t a2)
{
    return mp_level_state_fog_hear((void *)actor(who), command, a1, (int32_t)a2,
                                   MP_LEVEL_SIDE_HOST, true);
}

static size_t played(int32_t command, uint32_t a2)
{
    size_t i;
    size_t n = 0u;

    for (i = 0; i < eng.played_count; ++i) {
        n += eng.played[i].command == command && eng.played[i].a2 == a2 ? 1u : 0u;
    }
    return n;
}

static mp_level_state_note_t described(void)
{
    mp_level_state_note_t note;

    mp_level_state_note_init(&note, eng.substep, LEVEL, 1u);
    mp_level_state_fog_describe(&note);
    mp_fog_viewers_describe(&note);
    return note;
}

static bool shared_fog_said(void)
{
    mp_level_state_note_t note = described();

    return (note.parts & MP_LEVEL_STATE_PART_FOG) != 0u;
}

/* ==============================================================================================
 * The checks.
 * ============================================================================================ */

static void check_the_waking(void)
{
    mp_level_state_note_t note;
    const char           *line;
    int                   i;

    ut_section("the gas room woken: one run, found once the census has read its actor");
    fresh();
    next_substep();
    host_substep_end();   /* a host with a peer in the level: its viewer has run */

    next_substep();
    wake(0u, 7u, (int32_t)GASE_SCRIPT);
    ut_check(hear(0u, 6, 0, F_10_001) && hear(0u, 7, 0, F_16_002),
             "the woken script's own fog is refused before the engine, the viewer runs");
    mp_level_state_fog_host_tick();
    mp_fog_viewers_host_tick();
    note = described();
    ut_checkf(note.fog_viewers == 0u && played(6, F_10_001) == 0u,
              "the substep that woke it ends before its census, and no run is made of an actor the "
              "census has not read (%u run(s), %u start(s) played)", (unsigned)note.fog_viewers,
              (unsigned)played(6, F_10_001));
    census();

    for (i = 0; i < 6; ++i) {
        next_substep();
        host_substep_end();
    }
    note = described();
    ut_checkf(note.fog_viewers == 1u && note.fog_viewer[0].key == 7u &&
                  note.fog_viewer[0].life == 1u &&
                  note.fog_viewer[0].flags == (uint8_t)MP_LEVEL_STATE_FOG_VIEWER_ACTIVE,
              "one run, in the life the census counted, and running (%u run(s))",
              (unsigned)note.fog_viewers);
    ut_checkf(played(6, F_10_001) == 1u && played(7, F_16_002) == 1u &&
                  played(11, F_1_0001) == 1u,
              "the room's green begins once, its band and its ramp once each (%u, %u, %u)",
              (unsigned)played(6, F_10_001), (unsigned)played(7, F_16_002),
              (unsigned)played(11, F_1_0001));
    ut_checkf(played(7, F_56_0056) == 0u,
              "and the level's thin band is never played while the player stands in the room "
              "(%u time(s))", (unsigned)played(7, F_56_0056));

    mp_fog_viewers_report(true);
    line = newest_line("  the fog of the viewer (host):");
    ut_checkf(number_before(line, " run(s), ") == 1 && number_before(line, " run(s) gone") == 0 &&
                  number_after(line, "transitions start ") == 1 &&
                  number_after(line, "; left green at the end ") == 0,
              "the report counts one run, one start, no run gone and no end (%ld, %ld, %ld)",
              number_before(line, " run(s), "), number_after(line, "transitions start "),
              number_before(line, " run(s) gone"));
}

/* The report of the viewer's copy: a number that stands after `marker`, or 0 before any line. */
static long copy_count(const char *marker)
{
    const char *line = newest_line("  the fog of the viewer's own copy (host):");

    return line != NULL ? number_before(line, marker) : 0;
}

static void check_what_the_report_keeps(void)
{
    const char *line;
    long        bound_before;
    long        names;
    long        made;
    long        refused;

    ut_section("the level's binding is still said after the reset that ends the level");
    fresh();
    next_substep();
    host_substep_end();
    mp_fog_viewers_report(true);
    bound_before = number_before(newest_line("  the fog of the viewer (host):"),
                                 " played per viewer here");
    fresh();
    mp_fog_viewers_report(true);
    line = newest_line("  the fog of the viewer (host):");
    ut_checkf(bound_before >= 1 && number_before(line, " played per viewer here") == bound_before,
              "the gas room counted before the reset is counted after it, as the scripts read are "
              "(%ld before, %ld after)", bound_before,
              number_before(line, " played per viewer here"));

    ut_section("the copy of the script reads the pool only for the words a row reads");
    names   = copy_count(" not made because the entry names");
    made    = copy_count(" pool read(s) made");
    refused = copy_count(" refused past the script's own pool");
    fresh();
    next_substep();
    host_substep_end();
    mp_fog_viewers_report(true);
    ut_checkf(copy_count(" not made because the entry names") - names == (long)GASE_NAME_ROWS &&
                  copy_count(" pool read(s) made") - made == 26 &&
                  copy_count(" refused past the script's own pool") == refused,
              "the head and the seven labels read nothing, the eleven fog "
              "commands and two distance "
              "tests are read once for the fingerprint and once for the binding, and nothing is "
              "refused (%ld, %ld, %ld)", copy_count(" not made because the entry names") - names,
              copy_count(" pool read(s) made") - made,
              copy_count(" refused past the script's own pool") - refused);

    ut_section("a director entry whose words would run past the level's script buffer");
    fresh();
    put_u32(eng.world + WORLD_SCRIPT_BYTES,
            (uint32_t)((uintptr_t)(eng.pool + 3u * GASE_ROWS - 1u) - (uintptr_t)eng.table));
    refused = copy_count(" refused past the script's own pool");
    next_substep();
    host_substep_end();
    mp_fog_viewers_report(true);
    ut_checkf(copy_count(" refused past the script's own pool") - refused == 1 &&
                  newest_line("the fog of the viewer: script 1 of this level (fingerprint") != NULL,
              "is refused, and the script with one fog command fewer is in no class rather than "
              "bound on a word that is not its own (%ld refused)",
              copy_count(" refused past the script's own pool") - refused);
}

static void check_the_room_is_never_shared(void)
{
    ut_section("a command of a room the host lets through is its own engine's, never the level's");
    fresh();
    wake(0u, 7u, (int32_t)GASE_SCRIPT);
    census();   /* a host with no peer: the census ran once, the viewer never did */

    ut_check(!hear(0u, 6, 0, F_10_001) && !hear(0u, 7, 0, F_16_002) &&
                 !hear(0u, 11, 20, F_1_0001),
             "with no viewer running the host's own engine plays the room's "
             "fog, as it would alone");
    ut_checkf(eng.journaled == 0u && !shared_fog_said(),
              "and none of it is journaled or goes into the fog every client is told (%u "
              "entr(ies))", (unsigned)eng.journaled);

    wake(1u, 8u, 0);
    ut_check(!hear(1u, 6, 0, F_16) && eng.journaled == 1u && shared_fog_said(),
             "a script the table does not class stays the level's: let through, followed and "
             "journaled");

    ut_section("the first substep after a level or a savegame, before the viewer ran again");
    fresh();
    wake(0u, 7u, (int32_t)GASE_SCRIPT);
    census();
    next_substep();
    host_substep_end();
    next_substep();
    ut_check(hear(0u, 6, 0, F_10_001), "while the viewer runs the room's command is refused");
    fresh();
    wake(0u, 7u, (int32_t)GASE_SCRIPT);
    census();
    next_substep();
    ut_checkf(!hear(0u, 6, 0, F_10_001) && eng.journaled == 0u && !shared_fog_said(),
              "after the reset it is the host's engine's alone, and still never the level's (%u "
              "entr(ies))", (unsigned)eng.journaled);
    host_substep_end();
    next_substep();
    ut_check(hear(0u, 6, 0, F_10_001), "and once the viewer has run it is refused again");
}

static void check_the_substep_length(void)
{
    mp_level_state_note_t note;
    int                   i;

    ut_section("the shared fog's ramp counts down by this machine's own substep");
    fresh();
    eng.frame_delta_cell = true;
    eng.frame_delta      = 1.0f / 64.0f;
    wake(1u, 8u, 0);
    (void)hear(1u, 11, 2, F_1);
    for (i = 0; i < 64; ++i) {
        mp_level_state_fog_host_tick();
    }
    note = described();
    ut_checkf(note.fog.left == 1u && (note.fog.flags & MP_LEVEL_FOG_RAMP) != 0u,
              "sixty four substeps of 1/64 s are one second of a two second ramp, and one is left "
              "(%u)", (unsigned)note.fog.left);
    for (i = 0; i < 64; ++i) {
        mp_level_state_fog_host_tick();
    }
    note = described();
    ut_checkf(note.fog.left == 0u, "and sixty four more end it (%u)", (unsigned)note.fog.left);

    fresh();
    wake(1u, 8u, 0);
    (void)hear(1u, 11, 2, F_1);
    for (i = 0; i < 32; ++i) {
        mp_level_state_fog_host_tick();
    }
    note = described();
    ut_checkf(note.fog.left == 1u,
              "a cell that does not read is the engine's own 1/32 s (%u left after 32)",
              (unsigned)note.fog.left);

    fresh();
    eng.frame_delta_cell = true;
    eng.frame_delta      = (float)nan("");
    wake(1u, 8u, 0);
    (void)hear(1u, 11, 2, F_1);
    for (i = 0; i < 64; ++i) {
        mp_level_state_fog_host_tick();
    }
    note = described();
    ut_checkf(note.fog.left == 0u,
              "and so is one that holds no length, rather than a ramp that never ends (%u)",
              (unsigned)note.fog.left);
}

int main(void)
{
    check_the_waking();
    check_what_the_report_keeps();
    check_the_room_is_never_shared();
    check_the_substep_length();
    return ut_summary("the fog of a room and the shared fog on a host");
}
