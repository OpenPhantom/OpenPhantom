/* fog_owner.c: whose fog the device shows, and the band push, over an engine the test plays.
 *
 * The engine below holds what the rule reads and what the push writes: the device's colour and
 * band cells, the level record's band and colour, and the substep count. The effects write them
 * the way fx_rampFog, the director's commands 6 and 7, the ramp's tick and the effects' restore do,
 * and the frame runs the tick's decision with the production functions. The tick as it was is kept
 * beside it as the reference: it pushed through applyLevelFog, which puts the level's colour back,
 * and judged "settled" from its own bookkeeping.
 *
 * What would be silent if it were wrong:
 *
 *   a push while the gas room's colour is on the device, which puts the level's colour back and
 *   takes the green away for the rest of the room, on every machine whose band moved;
 *   the one substep between the room's band and its colour, in which a push would put this
 *   module's band end where the room's belongs, with nothing left to restore it from;
 *   a grace that is never given again because the same band came back, as it does on every way
 *   back into the room;
 *   the band push touching the colour or leaving the fog bit other than it found it.
 */
#include "unittest.h"

#include "fog_owner.h"

#include <stdbool.h>
#include <stdint.h>
#include <string.h>

#define GREEN 0xAAFFAAu

/* The packed colours of the eleven shipped levels, world+0x214 from header +0x8C. */
static const struct {
    const char *name;
    uint32_t    colour;
} LEVELS[] = {
    { "ASSAULT", 0xFFC864u }, { "BIGCITY", 0xD69D65u }, { "ESPA", 0xC2AA59u },
    { "FEDSHIP", 0x000000u }, { "FINAL", 0x282C44u },   { "GARDEN", 0x349DFFu },
    { "GUNGA", 0x000410u },   { "MAUL", 0xC2AA59u },    { "QUEEN", 0xFFC864u },
    { "RACE", 0xBE9D69u },    { "SWAMP", 0xBA792Cu },
};

/* ==============================================================================================
 * The engine the test plays
 * ============================================================================================ */
typedef struct engine {
    uint32_t          colour;         /* the device's colour cells, packed */
    fog_regime_band_t cells;          /* the device's band cells */
    uint32_t          flags;          /* the render flags word */
    uint32_t          level_colour;   /* world+0x214 */
    fog_regime_band_t record;         /* world+0x218 and +0x21C */
    uint32_t          substep;
    uint32_t          commits;
    uint32_t          calls[8];       /* the band push's calls, in order: 1 get, 2 set, 3 range */
    uint32_t          call_count;
    uint32_t          set_flags[4];
    uint32_t          set_count;
} engine_t;

static engine_t eng;

static uint32_t __cdecl fake_get_flags(void)
{
    if (eng.call_count < 8u) {
        eng.calls[eng.call_count++] = 1u;
    }
    return eng.flags;
}

static void __cdecl fake_set_flags(uint32_t flags)
{
    if (eng.call_count < 8u) {
        eng.calls[eng.call_count++] = 2u;
    }
    if (eng.set_count < 4u) {
        eng.set_flags[eng.set_count++] = flags;
    }
    eng.flags = flags;
    ++eng.commits;
}

static void __cdecl fake_set_range(uint32_t start_bits, uint32_t end_bits)
{
    if (eng.call_count < 8u) {
        eng.calls[eng.call_count++] = 3u;
    }
    memcpy(&eng.cells.start, &start_bits, sizeof start_bits);
    memcpy(&eng.cells.end, &end_bits, sizeof end_bits);
}

static const fog_device_writes_t WRITES = { fake_get_flags, fake_set_flags, fake_set_range };

static fog_regime_band_t band(float start, float end)
{
    fog_regime_band_t b;

    b.start = start;
    b.end   = end;
    return b;
}

static bool same(fog_regime_band_t a, fog_regime_band_t b)
{
    return a.start == b.start && a.end == b.end;
}

/* The level's fog apply: the record's colour and band to the device, committed. */
static void engine_apply_level(void)
{
    eng.colour = eng.level_colour;
    eng.cells  = eng.record;
    eng.commits += 2u;
}

/* ==============================================================================================
 * This module, the new way and the old
 * ============================================================================================ */
typedef struct module {
    fog_regime_band_t current;
    fog_regime_band_t written;
    fog_regime_band_t pushed;
    fog_owner_grace_t grace;
    bool              substep_clock;
    uint32_t          made;
    uint32_t          held;
    uint32_t          made_on_green;
} module_t;

static fog_owner_view_t look(const module_t *m)
{
    fog_owner_view_t view;

    memset(&view, 0, sizeof view);
    view.colour_read  = true;
    view.device_rgb   = eng.colour;
    view.level_rgb    = eng.level_colour;
    view.cells_read   = true;
    view.cells        = eng.cells;
    view.pushed       = m->pushed;
    view.substep_read = m->substep_clock;
    view.substep      = eng.substep;
    return view;
}

static fog_holder_t judge(module_t *m)
{
    fog_owner_view_t view = look(m);

    return fog_owner_judge(&view, &m->grace, NULL);
}

/* push_record_band: the record's band, if the device is this module's to write. */
static void push_record(module_t *m)
{
    if (judge(m) != FOG_HELD_BY_LEVEL) {
        ++m->held;
        return;
    }
    m->made_on_green += eng.colour == GREEN ? 1u : 0u;
    fog_owner_push_band(&WRITES, &eng.record);
    m->pushed = eng.record;
    ++m->made;
}

/* One frame of the tick: the band moved to `to`, then the decision the tick makes. */
static void frame_now(module_t *m, fog_regime_band_t to)
{
    fog_holder_t holder = judge(m);

    m->current = to;
    if (fog_owner_band_settled(&m->current, &m->written, true, holder,
                               same(eng.cells, m->current))) {
        return;
    }
    eng.record = m->current;
    m->written = m->current;
    push_record(m);
}

/* The tick as it was, word for word in what it decided: settled from its own bookkeeping, and the
 * push through applyLevelFog's original. */
static void frame_before(module_t *m, fog_regime_band_t to)
{
    m->current = to;
    if (same(m->current, m->written) && same(m->pushed, m->current)) {
        return;
    }
    eng.record = m->current;
    m->written = m->current;
    m->made_on_green += eng.colour == GREEN ? 1u : 0u;
    engine_apply_level();
    m->pushed = eng.record;
    ++m->made;
}

/* The level's fog apply through this module's hook: the record gets this module's band, the
 * original gives it to the device, and what it gave is noted as this module's. */
static void level_apply(module_t *m, bool noted)
{
    eng.record = m->current;
    m->written = m->current;
    engine_apply_level();
    if (noted) {
        m->pushed = eng.record;
    }
}

/* ==============================================================================================
 * The rule
 * ============================================================================================ */
static void check_the_colour_is_the_owner(void)
{
    size_t i;
    bool   green_everywhere = true;
    bool   own_nowhere = true;

    ut_section("the colour says who holds the fog");
    for (i = 0; i < sizeof LEVELS / sizeof LEVELS[0]; ++i) {
        green_everywhere = green_everywhere && fog_owner_effects_hold(GREEN, LEVELS[i].colour);
        own_nowhere = own_nowhere && !fog_owner_effects_hold(LEVELS[i].colour, LEVELS[i].colour);
    }
    ut_check(green_everywhere,
             "the gas room's pale green is not the own colour of any of the eleven levels");
    ut_check(own_nowhere,
             "and a level's own colour on the device is the level's fog on all eleven");
    ut_check(!fog_owner_effects_hold(0x00000000u, 0xFF000000u) &&
                 !fog_owner_effects_hold(0x12C2AA59u, 0x00C2AA59u),
             "only the three colour bytes count, whatever sits above them");
    ut_check(fog_owner_effects_hold(0x000000u, 0xC2AA59u),
             "the loading screen's black on a coloured level reads as the effects', which the "
             "level's own apply ends");
}

static void check_the_grace(void)
{
    module_t     m;
    fog_holder_t h;

    ut_section("a band the effects set without their colour is honoured for one substep");
    memset(&eng, 0, sizeof eng);
    memset(&m, 0, sizeof m);
    m.substep_clock  = true;
    eng.level_colour = 0xBE9D69u;
    m.current        = band(6.0f, 20.0f);
    level_apply(&m, true);
    eng.substep = 100u;
    ut_check(judge(&m) == FOG_HELD_BY_LEVEL, "the band the level's apply gave is this module's");

    eng.cells = band(35.004f, 90.009f);   /* RACE's opening, 6 and 7 */
    h = judge(&m);
    ut_check(h == FOG_HELD_FOR_A_SUBSTEP, "a band the director set is left alone");
    ut_check(judge(&m) == FOG_HELD_FOR_A_SUBSTEP, "for every frame of that substep");
    eng.substep = 101u;
    ut_check(judge(&m) == FOG_HELD_BY_LEVEL,
             "and once the next substep has run with no colour, this module's band goes over it");
    push_record(&m);
    ut_check(same(eng.cells, band(6.0f, 20.0f)) && judge(&m) == FOG_HELD_BY_LEVEL,
             "after which the device is this module's again");

    eng.cells   = band(35.004f, 90.009f);
    eng.substep = 180u;
    ut_check(judge(&m) == FOG_HELD_FOR_A_SUBSTEP,
             "the same band coming back later gets a substep of its own, as every way back into "
             "the gas room sets the same band again");

    /* The director's 6 and 7 in two substeps: the start moves first, the end a substep later,
     * and each band is another one. The grace follows the newest band, or the end would go
     * under this module's band the moment it was set. */
    eng.cells   = band(10.0f, 90.009f);
    eng.substep = 181u;
    ut_check(judge(&m) == FOG_HELD_FOR_A_SUBSTEP,
             "another foreign band in the next substep starts its own grace rather than inheriting "
             "the one that has just run out");
    eng.substep = 182u;
    ut_check(judge(&m) == FOG_HELD_BY_LEVEL,
             "and that grace too lasts one substep");

    m.substep_clock = false;
    memset(&m.grace, 0, sizeof m.grace);
    ut_check(judge(&m) == FOG_HELD_BY_LEVEL,
             "without the engine's substep counter there is no grace, and the band goes over it "
             "at once, as it always did");

    eng.colour = GREEN;
    ut_check(judge(&m) == FOG_HELD_BY_EFFECTS,
             "and the colour holds the device whatever the clock");
}

/* ==============================================================================================
 * The gas room, frame by frame, the new way against the old
 * ============================================================================================ */
#define FRAMES_PER_SUBSTEP 2u
#define RAMP_SUBSTEPS      640u   /* command 11's twenty seconds */
#define BACK_SUBSTEPS      32u    /* command 12's one second */

typedef struct gas_run {
    uint32_t          made_in_gas;
    uint32_t          made_on_green;
    uint32_t          held;
    bool              green_before_12;
    fog_regime_band_t cells_before_12;
    bool              level_colour_after;
    bool              record_after;
} gas_run_t;

/* This module's band, wandering as the field of view and the cut move it. */
static fog_regime_band_t wandering(uint32_t frame)
{
    float end = 18.71f + (float)(frame % 50u) * 0.08f;

    return band(end * 0.2857f, end);
}

/* The frames drawn after one substep. */
static void frames_of(module_t *m, bool before, uint32_t *frame)
{
    uint32_t i;

    for (i = 0; i < FRAMES_PER_SUBSTEP; ++i) {
        if (before) {
            frame_before(m, wandering((*frame)++));
        } else {
            frame_now(m, wandering((*frame)++));
        }
    }
}

static void substep_of(module_t *m, bool before, uint32_t *frame)
{
    ++eng.substep;
    frames_of(m, before, frame);
}

static gas_run_t gas_room(bool before)
{
    module_t  m;
    gas_run_t run;
    uint32_t  frame = 0;
    uint32_t  made_at_11;
    uint32_t  i;

    memset(&eng, 0, sizeof eng);
    memset(&m, 0, sizeof m);
    memset(&run, 0, sizeof run);
    eng.flags        = 0x40u | 0x01u;
    eng.level_colour = 0x000000u;         /* FEDSHIP */
    m.substep_clock  = true;
    m.pushed         = band(-1.0f, -1.0f);
    m.current        = wandering(frame);
    level_apply(&m, !before);
    for (i = 0; i < 32u; ++i) {
        substep_of(&m, before, &frame);
    }

    /* 0sta: commands 6 and 7, the room's band, into the device's cells only. */
    ++eng.substep;
    eng.cells.start = 10.001f;
    eng.cells.end   = 16.002f;
    frames_of(&m, before, &frame);

    /* 1gas, a substep later: command 11, the green and a twenty second ramp of the start. */
    ++eng.substep;
    eng.colour = GREEN;
    made_at_11 = m.made;
    frames_of(&m, before, &frame);
    for (i = 0; i < RAMP_SUBSTEPS; ++i) {
        eng.cells.start = 10.001f - 9.0f * (float)(i + 1u) / (float)RAMP_SUBSTEPS;
        substep_of(&m, before, &frame);
    }
    run.green_before_12 = eng.colour == GREEN;
    run.cells_before_12 = eng.cells;

    /* 2wai: command 12, back over a second, then the effects' restore runs applyLevelFog. */
    for (i = 0; i < BACK_SUBSTEPS; ++i) {
        eng.cells.start = 1.0f + 15.0f * (float)(i + 1u) / (float)BACK_SUBSTEPS;
        substep_of(&m, before, &frame);
    }
    run.made_in_gas   = m.made - made_at_11;
    run.made_on_green = m.made_on_green;
    run.held          = m.held;
    level_apply(&m, !before);
    run.level_colour_after = eng.colour == eng.level_colour;
    for (i = 0; i < 4u; ++i) {
        substep_of(&m, before, &frame);
    }
    run.record_after = same(eng.cells, eng.record) && same(eng.record, m.current);
    return run;
}

static void check_the_gas_room(void)
{
    gas_run_t old = gas_room(true);
    gas_run_t now = gas_room(false);

    ut_section("the gas room, as the tick did it: the reference");
    ut_checkf(old.made_on_green > 0u && !old.green_before_12,
              "the old tick pushed while the room was green (%u time(s)), and applyLevelFog put "
              "the level's colour back: the green was gone before the player left",
              (unsigned)old.made_on_green);
    ut_checkf(old.cells_before_12.end != 16.002f,
              "and the room's band end was this module's %.2f, not the room's 16.00",
              (double)old.cells_before_12.end);

    ut_section("the gas room, as the tick does it now");
    ut_checkf(now.made_on_green == 0u && now.made_in_gas == 0u,
              "no push from the room's band to the effects' restore (%u, %u)",
              (unsigned)now.made_on_green, (unsigned)now.made_in_gas);
    ut_check(now.green_before_12, "the room is still green when the player leaves it");
    ut_checkf(now.cells_before_12.end == 16.002f,
              "and its band ends where the room put it, 16.00, because the substep between the "
              "band and the colour was left alone (%.3f)", (double)now.cells_before_12.end);
    ut_checkf(now.held > 0u,
              "every frame this module's band moved in the room, the push was held back (%u)",
              (unsigned)now.held);
    ut_check(now.level_colour_after && now.record_after,
             "after the restore the level's colour is back and the device carries this module's "
             "band, eased on in the room and never lost");
}

/* ==============================================================================================
 * The cheat, which holds the record's band
 * ============================================================================================ */
/* stand_aside_for_the_other_writer: a band that has not moved, pushed when the device is this
 * module's to write and does not show it. */
static void stand_aside(module_t *m, fog_regime_band_t theirs)
{
    fog_holder_t holder = judge(m);

    eng.record = theirs;
    if (!fog_owner_band_settled(&theirs, &theirs, true, holder, same(eng.cells, theirs))) {
        push_record(m);
    }
}

static void check_the_cheat(void)
{
    module_t                m;
    const fog_regime_band_t no_fog = band(4000.0f, 5000.0f);

    ut_section("the no-fog cheat outside the gas room, inside it and after it");
    memset(&eng, 0, sizeof eng);
    memset(&m, 0, sizeof m);
    m.substep_clock  = true;
    eng.level_colour = 0x000000u;
    m.current        = band(5.34f, 18.71f);
    level_apply(&m, true);

    stand_aside(&m, no_fog);
    ut_check(same(eng.cells, no_fog) && m.made == 1u,
             "outside the room the cheat's band is pushed for it");

    ++eng.substep;
    eng.cells = band(10.001f, 16.002f);
    ++eng.substep;
    eng.colour = GREEN;
    stand_aside(&m, no_fog);
    stand_aside(&m, no_fog);
    ut_check(same(eng.cells, band(10.001f, 16.002f)) && m.made == 1u && m.held == 0u,
             "inside it the cheat does nothing: the room's band stays and nothing is even tried");

    eng.record = no_fog;
    engine_apply_level();   /* the restore; the hook leaves a record it did not write alone */
    m.pushed = eng.record;
    stand_aside(&m, no_fog);
    ut_check(same(eng.cells, no_fog) && eng.colour == 0x000000u && m.made == 1u,
             "after the restore the device carries the cheat's band with the level's colour");
}

/* ==============================================================================================
 * The push
 * ============================================================================================ */
static void check_the_push(void)
{
    fog_regime_band_t b = band(6.47f, 22.65f);
    uint32_t          start_bits;
    uint32_t          end_bits;

    ut_section("the band push writes the band and commits, and nothing else");
    memset(&eng, 0, sizeof eng);
    eng.flags  = 0x0000F1C3u | 0x40u;
    eng.colour = GREEN;
    fog_owner_push_band(&WRITES, &b);
    ut_check(eng.call_count == 4u && eng.calls[0] == 1u && eng.calls[1] == 2u &&
                 eng.calls[2] == 3u && eng.calls[3] == 2u,
             "get the flags, set them, the band, set them again, in that order");
    ut_check(eng.set_flags[0] == (0x0000F1C3u & ~0x40u) &&
                 eng.set_flags[1] == (0x0000F1C3u | 0x40u),
             "the first commit without the fog bit and every other bit kept, the second with the "
             "flags exactly as they were");
    memcpy(&start_bits, &b.start, sizeof start_bits);
    memcpy(&end_bits, &b.end, sizeof end_bits);
    ut_check(memcmp(&eng.cells.start, &start_bits, 4) == 0 &&
                 memcmp(&eng.cells.end, &end_bits, 4) == 0,
             "the band arrives bit for bit");
    ut_check(eng.colour == GREEN && eng.flags == (0x0000F1C3u | 0x40u),
             "the colour is untouched, and the fog bit is on after as it was before");

    memset(&eng, 0, sizeof eng);
    eng.flags = 0x0000F183u;
    fog_owner_push_band(&WRITES, &b);
    ut_check(eng.flags == 0x0000F183u && eng.commits == 2u,
             "a fog bit that was off stays off, and there are two commits, as applyLevelFog makes");
}

int main(void)
{
    check_the_colour_is_the_owner();
    check_the_grace();
    check_the_gas_room();
    check_the_cheat();
    check_the_push();

    return ut_summary("fog_owner");
}
