/* mp_actor_loop.c: the loops an actor's script keeps, from the host's note to a client's replica.
 *
 * SIZE NOTE: over six hundred lines. The host's table, the client's starts and the part of the
 * level's state all run over the one census and the one channel bank this file plays; a second file
 * would carry both through a header. The seam, if this grows, is the part's round trip at the end.
 *
 * The host half runs over the enemy table's own census, filled by hand as the neighbouring tests
 * of the block do. The client half runs against an engine this test stands in for: a channel bank
 * of twelve, a play that hands out a channel and writes it into the handle cell, a stop that frees
 * the channel the actor's cell names, and a leave that detaches it. The actors are real memory, so
 * the cells the module reads and the engine writes are real cells.
 *
 * What would be silent if it were wrong:
 *
 *   a loop the host's own engine refused for distance missing from the note, so a peer next to
 *   the actor never hears it;
 *   a loop that outlives command 17, the actor's removal or its life on the host's note;
 *   a new life in an actor's slot whose loop is taken for the old life's and then thrown away;
 *   a report that counts the script's calls, one a substep while the host is far, as starts;
 *   two loops on one replica, or a loop that never starts again after the distance cut;
 *   a second loop started into a cell the replica's own script already filled;
 *   a removed replica's loop left owning a slot the next actor takes;
 *   a part of the level's state that does not read back what was written.
 *
 * The log is kept here, every entry of common/logging, so the host's line can be read back.
 */
#include "unittest.h"

#include "mp_actor_loop.h"
#include "mp_enemy_sync.h"
#include "mp_enemy_sync_internal.h"
#include "mp_level_state_rule.h"
#include "mp_wire.h"

#include "common/logging.h"
#include "common/text.h"

#include <stdarg.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

#define HERE      57u
#define CHANNELS  12
#define ACTORS    20u
#define ACTOR_BYTES 0x100u

/* The actors: real memory, the handle cell and the position where the engine keeps them. */
static uint8_t s_actor[ACTORS][ACTOR_BYTES];

static uintptr_t actor(size_t i)
{
    return (uintptr_t)s_actor[i];
}

static int32_t *cell(size_t i)
{
    return (int32_t *)(void *)(s_actor[i] + MP_ACTOR_LOOP_HANDLE);
}

/* ==============================================================================================
 * The log: the host's line of the report is all a check here reads.
 * ============================================================================================ */

#define HOST_LINE "the actors' loops (host): "

static char s_line[1100];
static char s_host_line[1100];

static void keep(const char *format, va_list arguments)
{
    (void)text_vformat(s_line, sizeof s_line, format, arguments);
    if (strncmp(s_line, HOST_LINE, strlen(HOST_LINE)) == 0) {
        memcpy(s_host_line, s_line, sizeof s_host_line);
    }
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

/* The number the host's line of a fresh report writes right before `marker`, or -1. */
static long reported_before(const char *marker)
{
    const char *at;
    const char *digits;

    s_host_line[0] = '\0';
    mp_actor_loop_report();
    at = strstr(s_host_line, marker);
    if (at == NULL) {
        return -1;
    }
    digits = at;
    while (digits > s_host_line && digits[-1] >= '0' && digits[-1] <= '9') {
        --digits;
    }
    return digits == at ? -1 : strtol(digits, NULL, 10);
}

static float *position(size_t i)
{
    return (float *)(void *)(s_actor[i] + MP_ACTOR_SOUND_POS);
}

/* ==============================================================================================
 * The engine this test stands in for.
 * ============================================================================================ */

typedef struct fake_channel {
    bool         playing;
    uint16_t     call;
    int32_t     *owner;
    const float *follows;
    bool         pinned;
    float        pinned_at[3];
} fake_channel_t;

static fake_channel_t s_channel[CHANNELS];
static bool           s_admit = true;   /* the start gate lets a loop in */
static bool           s_stands[ACTORS];
static unsigned       s_plays;
static unsigned       s_stops;
static unsigned       s_leaves;

static void fake_play(uint16_t call, int32_t *handle, const float *where)
{
    int32_t ch;

    ++s_plays;
    *handle = -1;
    if (!s_admit) {
        return;
    }
    for (ch = 0; ch < CHANNELS; ++ch) {
        if (!s_channel[ch].playing) {
            memset(&s_channel[ch], 0, sizeof s_channel[ch]);
            s_channel[ch].playing = true;
            s_channel[ch].call    = call;
            s_channel[ch].owner   = handle;
            s_channel[ch].follows = where;
            *handle = ch;
            return;
        }
    }
}

/* The engine's free: -1 through the owner, and the slot is empty. */
static void fake_free(int32_t ch)
{
    if (ch < 0 || ch >= CHANNELS) {
        return;
    }
    if (s_channel[ch].owner != NULL) {
        *s_channel[ch].owner = -1;
    }
    memset(&s_channel[ch], 0, sizeof s_channel[ch]);
}

static bool fake_stop(uintptr_t who)
{
    ++s_stops;
    fake_free(*(int32_t *)(who + MP_ACTOR_LOOP_HANDLE));
    return true;
}

static bool fake_leave(int32_t ch, const int32_t *handle, const float where[3])
{
    if (ch < 0 || ch >= CHANNELS || !s_channel[ch].playing || s_channel[ch].owner != handle) {
        return false;
    }
    ++s_leaves;
    s_channel[ch].pinned = true;
    memcpy(s_channel[ch].pinned_at, where, sizeof s_channel[ch].pinned_at);
    s_channel[ch].owner = NULL;
    return true;
}

static bool fake_stands(uint32_t key, uintptr_t who, uint8_t life)
{
    (void)life;
    return key < ACTORS && who == actor(key) && s_stands[key];
}

/* The channel bank read the way the engine keeps it: the channel's owner, and the sound it
 * plays. */
static mp_actor_loop_found_t fake_found(int32_t ch, const int32_t *handle, uint16_t call)
{
    if (ch < 0 || ch >= CHANNELS || !s_channel[ch].playing || s_channel[ch].owner != handle) {
        return MP_ACTOR_LOOP_FOUND_NONE;
    }
    return s_channel[ch].call == call ? MP_ACTOR_LOOP_FOUND_SAME : MP_ACTOR_LOOP_FOUND_OTHER;
}

static const mp_actor_loop_engine_t ENGINE = { &fake_play, &fake_stop, &fake_leave,
                                               &fake_stands, &fake_found };

static unsigned playing(void)
{
    unsigned n = 0;
    int32_t  ch;

    for (ch = 0; ch < CHANNELS; ++ch) {
        n += s_channel[ch].playing ? 1u : 0u;
    }
    return n;
}

/* ==============================================================================================
 * The host.
 * ============================================================================================ */

static bool s_live[ACTORS];

static void fresh(void)
{
    size_t i;

    mp_enemy_sync_reset();
    mp_enemy_sync_set_enabled(true);
    mp_enemy_sync_set_level(true, HERE);
    memset(s_live, 0, sizeof s_live);
    memset(s_channel, 0, sizeof s_channel);
    memset(s_stands, 0, sizeof s_stands);
    for (i = 0; i < ACTORS; ++i) {
        memset(s_actor[i], 0, ACTOR_BYTES);
        *cell(i) = -1;
    }
    s_admit  = true;
    s_plays  = 0u;
    s_stops  = 0u;
    s_leaves = 0u;
    mp_actor_loop_set_engine(&ENGINE);
}

/* One census as the host takes it: who is live under which key, the lives counted. */
static void census(void)
{
    enemy_sync_state_t *s = mp_enemy_sync_state();
    size_t              key;

    for (key = 0; key < ACTORS; ++key) {
        placement_t *p = &s->placement[key];

        p->was_live = p->live;
        p->live     = s_live[key];
        p->actor    = s_live[key] ? actor(key) : 0u;
    }
    s->send_ready = true;
    mp_enemy_sync_describe_census();
}

static mp_level_state_note_t written(void)
{
    mp_level_state_note_t note;

    mp_level_state_note_init(&note, 1u, HERE, 1u);
    mp_actor_loop_write(&note);
    return note;
}

static bool note_has(const mp_level_state_note_t *note, uint16_t key, uint16_t call)
{
    uint8_t i;

    for (i = 0; i < note->loops; ++i) {
        if (note->loop[i].key == key && note->loop[i].call == call) {
            return true;
        }
    }
    return false;
}

static void check_the_host(void)
{
    mp_level_state_note_t note;
    uint16_t              key;

    ut_section("the host's note: one loop per actor's life, the intent and not the channel");
    fresh();
    note = written();
    ut_check((note.parts & MP_LEVEL_STATE_PART_ACTOR_LOOPS) != 0u && note.loops == 0u,
             "the part is always in the note, and with no loop it says none");

    s_live[3] = true;
    census();
    mp_actor_loop_started(actor(3), 3u, 100u);
    note = written();
    ut_check(note.loops == 1u && note_has(&note, 3u, 100u) && note.loop[0].life == 1u,
             "a loop the census's actor started is in the next note, in that actor's life");

    mp_actor_loop_started(actor(3), 3u, 101u);
    note = written();
    ut_check(note.loops == 1u && note_has(&note, 3u, 101u),
             "a second loop of the same actor replaces the first, never two");

    mp_actor_loop_stopped(actor(3));
    note = written();
    ut_check(note.loops == 0u, "command 17 takes it out of the note");

    mp_actor_loop_started(actor(3), 3u, 100u);
    note = written();
    s_live[3] = false;
    census();
    ut_check(note.loops == 1u && written().loops == 0u,
             "the actor removed: the census no longer reads it, and the next note forgets it");

    s_live[3] = true;
    census();
    mp_actor_loop_started(actor(3), 3u, 100u);
    note = written();
    s_live[3] = false;
    census();
    s_live[3] = true;
    census();
    ut_check(note.loops == 1u && note.loop[0].life == 2u && written().loops == 0u,
             "a new life of the key in the same slot is not the life the loop was started in");

    mp_actor_loop_started(actor(5), 5u, 7u);
    note = written();
    ut_check(!note_has(&note, 5u, 7u),
             "an actor the census has not read yet waits, and is not said with a guessed life");
    s_live[5] = true;
    census();
    note = written();
    ut_check(note_has(&note, 5u, 7u), "and is said once the census has read it");

    mp_actor_loop_started(actor(6), 6u, 8u);
    (void)written();
    (void)written();
    (void)written();
    s_live[6] = true;
    census();
    note = written();
    ut_check(!note_has(&note, 6u, 8u),
             "one the census never read in three notes is dropped, not kept for a later life");

    fresh();
    for (key = 0; key < ACTORS; ++key) {
        s_live[key] = true;
    }
    census();
    for (key = 0; key < MP_LEVEL_STATE_LOOPS_MAX + 1u; ++key) {
        mp_actor_loop_started(actor(key), key, (uint16_t)(200u + key));
    }
    note = written();
    ut_check(note.loops == MP_LEVEL_STATE_LOOPS_MAX,
             "a seventeenth loop at once is refused, and sixteen are said");

    mp_enemy_sync_reset();
    mp_enemy_sync_set_enabled(true);
    mp_enemy_sync_set_level(true, HERE);
    note = written();
    ut_check(note.loops == 0u, "the enemy table's reset, every way out of a level, forgets them");
}

#define CALLS   " call(s) of a script that asked for a loop"
#define STARTS  " started one on record"
#define CHANGED " changed the call on record"
#define AGAIN   " asked for the one on record again"

static void check_the_host_counts_and_lives(void)
{
    mp_level_state_note_t note;
    mp_level_state_note_t later;
    long                  before[4];
    int                   i;

    ut_section("the host counts the loops started, not the calls of a script");
    fresh();
    s_live[3] = true;
    census();
    before[0] = reported_before(CALLS);
    before[1] = reported_before(STARTS);
    before[2] = reported_before(CHANGED);
    before[3] = reported_before(AGAIN);
    for (i = 0; i < 100; ++i) {
        mp_actor_loop_started(actor(3), 3u, 100u);   /* the host's gate refused: asked again */
    }
    mp_actor_loop_started(actor(3), 3u, 101u);
    ut_checkf(reported_before(CALLS) - before[0] == 101 &&
                  reported_before(STARTS) - before[1] == 1 &&
                  reported_before(CHANGED) - before[2] == 1 &&
                  reported_before(AGAIN) - before[3] == 99,
              "a hundred and one calls: one loop started, one changed, ninety nine asked again "
              "(%ld, %ld, %ld, %ld)", reported_before(CALLS) - before[0],
              reported_before(STARTS) - before[1], reported_before(CHANGED) - before[2],
              reported_before(AGAIN) - before[3]);

    ut_section("the host: a new life in an actor's slot starts its own loop");
    fresh();
    s_live[3] = true;
    census();
    mp_actor_loop_started(actor(3), 3u, 100u);
    note      = written();
    s_live[3] = false;
    census();
    mp_actor_loop_started(actor(3), 3u, 100u);   /* before the census has read the new life */
    s_live[3] = true;
    census();
    later = written();
    ut_check(note.loops == 1u && note.loop[0].life == 1u && later.loops == 1u &&
                 later.loop[0].life == 2u && note_has(&later, 3u, 100u),
             "its loop is said in its own life, not taken for the old one's and thrown away");

    fresh();
    s_live[3] = true;
    census();
    mp_actor_loop_started(actor(3), 3u, 100u);
    (void)written();
    s_live[3] = false;
    census();
    s_live[3] = true;
    census();
    mp_actor_loop_started(actor(3), 3u, 100u);   /* after the census counted it */
    later = written();
    ut_check(later.loops == 1u && later.loop[0].life == 2u,
             "and so when the census counted the new life before its script called");
}

/* ==============================================================================================
 * A client.
 * ============================================================================================ */

/* The host described `key` in `life` to this side, and the replica stands. */
static void replica(size_t key, uint8_t life)
{
    placement_t *p = &mp_enemy_sync_state()->placement[key];

    p->known      = true;
    p->generation = life;
    p->live       = true;
    p->actor      = actor(key);
    s_stands[key] = true;
}

static void gone(size_t key)
{
    placement_t *p = &mp_enemy_sync_state()->placement[key];

    p->live       = false;
    p->actor      = 0u;
    s_stands[key] = false;
}

static void wants(uint8_t count, const mp_level_loop_t *loops)
{
    mp_level_state_note_t note;

    mp_level_state_note_init(&note, 2u, HERE, 1u);
    note.parts |= (uint8_t)MP_LEVEL_STATE_PART_ACTOR_LOOPS;
    note.loops = count;
    if (count != 0u) {
        memcpy(note.loop, loops, count * sizeof loops[0]);
    }
    mp_actor_loop_take(&note);
}

static void check_the_client(void)
{
    mp_level_loop_t one   = { 1u, 1u, 50u };
    mp_level_loop_t other = { 1u, 1u, 51u };
    mp_level_loop_t two   = { 2u, 1u, 60u };
    mp_level_loop_t pair[2];
    int32_t         ch;

    ut_section("a client: the replica plays what the host wants, one loop at a time");
    fresh();
    mp_actor_loop_flush();
    ut_check(s_plays == 0u, "nothing wanted, nothing played");

    replica(1u, 1u);
    wants(1u, &one);
    mp_actor_loop_flush();
    ch = *cell(1u);
    ut_check(s_plays == 1u && ch >= 0 && s_channel[ch].owner == cell(1u) &&
                 s_channel[ch].follows == position(1u) && s_channel[ch].call == 50u,
             "started on the replica, with its own handle cell and the position it moves with");
    mp_actor_loop_flush();
    ut_check(s_plays == 1u, "and kept, not started a second time in the next substep");

    fake_free(ch);
    mp_actor_loop_flush();
    ut_check(s_plays == 2u && *cell(1u) >= 0,
             "the engine's distance cut ended it: started again, as the host's script would");

    fake_free(*cell(1u));
    s_admit = false;
    mp_actor_loop_flush();
    mp_actor_loop_flush();
    ut_check(*cell(1u) < 0 && s_plays == 4u,
             "a start the gate refuses is asked again each substep");
    s_admit = true;
    mp_actor_loop_flush();
    ut_check(*cell(1u) >= 0 && playing() == 1u, "and it plays once the listener is back");

    wants(1u, &other);
    mp_actor_loop_flush();
    ut_check(playing() == 1u && s_channel[*cell(1u)].call == 51u && s_stops == 1u,
             "another loop wanted: the old one stopped with command 17 first, never two at once");

    wants(0u, NULL);
    mp_actor_loop_flush();
    ut_check(playing() == 0u && s_stops == 2u && *cell(1u) < 0,
             "no longer wanted: stopped on the replica, the engine's own way");
    mp_actor_loop_flush();
    ut_check(s_stops == 2u, "and forgotten, not stopped again");

    ut_section("a client: a removed replica's loop stays where it stood and owns nothing");
    replica(2u, 1u);
    pair[0] = one;
    pair[1] = two;
    wants(2u, pair);
    position(2u)[0] = 11.0f;
    position(2u)[1] = 22.0f;
    position(2u)[2] = 33.0f;
    mp_actor_loop_flush();
    ch = *cell(2u);
    gone(2u);
    position(2u)[0] = 99.0f;   /* the slot already holds another actor's position */
    mp_actor_loop_flush();
    ut_check(s_leaves == 1u && s_channel[ch].playing && s_channel[ch].owner == NULL &&
                 s_channel[ch].pinned && s_channel[ch].pinned_at[0] == 11.0f &&
                 s_channel[ch].pinned_at[2] == 33.0f,
             "it goes on at the last place the replica stood, and no longer owns the slot's cell");
    ut_check(s_stops == 2u, "and it is not stopped: the host's own goes on too");
    *cell(2u) = -1;
    replica(2u, 1u);
    mp_actor_loop_flush();
    ut_check(*cell(2u) >= 0 && *cell(2u) != ch,
             "a replica of the same life built again starts its own, beside the one left behind");

    ut_section("a client: only the replica of the life the host names");
    fresh();
    mp_enemy_sync_state()->placement[4].known      = true;
    mp_enemy_sync_state()->placement[4].generation = 1u;
    {
        mp_level_loop_t later = { 4u, 2u, 70u };

        wants(1u, &later);
    }
    mp_actor_loop_flush();
    ut_check(s_plays == 0u, "no replica here yet: nothing");
    replica(4u, 1u);
    mp_actor_loop_flush();
    ut_check(s_plays == 0u, "a replica of the life before: nothing either");
    replica(4u, 2u);
    mp_actor_loop_flush();
    ut_check(s_plays == 1u, "the replica of that life: started");

    mp_actor_loop_set_engine(NULL);
    fake_free(*cell(4u));
    mp_actor_loop_flush();
    ut_check(s_plays == 1u, "with no engine handed in, a client starts nothing");
}

/* A replica whose own script ran before the host described it may already hold a loop in its
 * cell. */
static void check_the_cell_taken_over(void)
{
    mp_level_loop_t one = { 1u, 1u, 50u };
    int32_t         ch;
    unsigned        plays;

    ut_section("a client takes over the loop a replica's own script already started in its cell");
    fresh();
    replica(1u, 1u);
    fake_play(50u, cell(1u), position(1u));
    ch    = *cell(1u);
    plays = s_plays;
    wants(1u, &one);
    mp_actor_loop_flush();
    ut_checkf(playing() == 1u && *cell(1u) == ch && s_plays == plays,
              "the sound the host wants already plays there: it is taken over, and no second loop "
              "goes into the same cell (%u playing)", playing());
    mp_actor_loop_flush();
    wants(0u, NULL);
    mp_actor_loop_flush();
    ut_check(playing() == 0u && *cell(1u) < 0,
             "and it is stopped the engine's way once the host no longer wants it");

    fresh();
    replica(1u, 1u);
    fake_play(77u, cell(1u), position(1u));
    wants(1u, &one);
    mp_actor_loop_flush();
    ut_checkf(playing() == 1u && *cell(1u) >= 0 && s_channel[*cell(1u)].call == 50u &&
                  s_stops == 1u,
              "another sound in the cell is stopped with command 17 first, then the host's starts "
              "(%u playing)", playing());

    fresh();
    replica(1u, 1u);
    replica(2u, 1u);
    fake_play(60u, cell(2u), position(2u));
    *cell(1u) = *cell(2u);   /* a number the engine has since given to another actor */
    ch        = *cell(2u);
    wants(1u, &one);
    mp_actor_loop_flush();
    ut_check(playing() == 2u && s_stops == 0u && s_channel[ch].owner == cell(2u) &&
                 *cell(1u) != ch,
             "a number in the cell that is no longer its channel is played over, and the other "
             "actor's sound is left alone");
}

/* ==============================================================================================
 * The part of the level's state, written and read back.
 * ============================================================================================ */

static void check_the_part(void)
{
    uint8_t               buffer[MP_LEVEL_STATE_MAX_BYTES];
    mp_level_state_note_t note;
    mp_level_state_note_t back;
    unsigned              round = 0;
    unsigned              torn  = 0;
    unsigned              i;

    ut_section("the loops' part of the level's state reads back what was written");
    srand(292u);
    for (i = 0; i < 2000u; ++i) {
        size_t  bytes;
        size_t  cut;
        uint8_t k;

        mp_level_state_note_init(&note, (uint32_t)rand(), (uint16_t)rand(), (uint32_t)rand());
        note.parts |= (uint8_t)MP_LEVEL_STATE_PART_ACTOR_LOOPS;
        note.loops = (uint8_t)(rand() % (int)(MP_LEVEL_STATE_LOOPS_MAX + 1u));
        for (k = 0; k < note.loops; ++k) {
            note.loop[k].key  = (uint16_t)(rand() % (int)MP_WIRE_KEY_COUNT);
            note.loop[k].life = (uint8_t)rand();
            note.loop[k].call = (uint16_t)rand();
        }
        bytes = mp_level_state_encode(&note, buffer, sizeof buffer);
        if (bytes == 0u || !mp_level_state_decode(buffer, bytes, &back)) {
            continue;
        }
        if (back.loops == note.loops &&
            (note.loops == 0u ||
             memcmp(back.loop, note.loop, note.loops * sizeof note.loop[0]) == 0)) {
            ++round;
        }
        cut = (size_t)(rand() % (int)bytes);
        torn += mp_level_state_decode(buffer, cut, &back) ? 0u : 1u;
    }
    ut_checkf(round == 2000u, "2000 random notes round trip their loops (%u did)", round);
    ut_checkf(torn == 2000u, "and every shorter read of them is refused (%u were)", torn);

    fresh();
    for (i = 0; i < 3u; ++i) {
        s_live[i] = true;
    }
    census();
    mp_actor_loop_started(actor(0), 0u, 10u);
    mp_actor_loop_started(actor(2), 2u, 12u);
    note = written();
    ut_check(mp_level_state_encode(&note, buffer, sizeof buffer) != 0u &&
                 mp_level_state_decode(buffer, mp_level_state_bytes(&note), &back) &&
                 back.loops == 2u && note_has(&back, 0u, 10u) && note_has(&back, 2u, 12u),
             "what the host writes is what a client decodes");
}

int main(void)
{
    check_the_host();
    check_the_host_counts_and_lives();
    check_the_client();
    check_the_cell_taken_over();
    check_the_part();
    return ut_summary("the actors' loops");
}
