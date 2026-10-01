/* mp_timeline.h: the receiver's replay of the sender's ticks, one per own substep.
 *
 * Layer 1, pure. No engine, no socket and no wall clock: every input is a tick number and every
 * output is a tick number and eighths of one, so the module runs in the unit test with nothing
 * else in the process.
 *
 * What it replaces. The peer body used to be rendered at an estimate of the sender's time built
 * from the operating system's tick count and the moment each packet was pumped. That estimate
 * moved in half ticks and occasionally backwards, because the tick count steps by sixteen
 * milliseconds and the pump runs once per substep, so the far body stood and lurched at every
 * beat between the two machines' ladders. Both ladders run at thirty-two substeps a second, so
 * the honest clock for the far body is this machine's own substep count: the render tick
 * advances by exactly one sender tick per own substep, and the only thing regulated is how far
 * behind the newest received tick it runs.
 *
 * The regulation is integer and slow. The render tick sits a target lag behind the newest tick;
 * while the lag stays within a tick of the target nothing is done. When it sits outside that
 * band for eight substeps in a row, the next eight substeps advance by nine eighths (the render
 * catches up one tick) or seven eighths (it falls back one), and then the decision rests for
 * thirty-two substeps, in which the streaks do not count. A lag far past the target is a stall
 * that has ended, and the render is rebased at once. The render never passes the newest tick:
 * when the sender stops, the render stops at the last sample and waits until the lag is back at
 * the target before it moves again, and that wait is counted with the rebases.
 *
 * How far behind can itself be measured. A fixed target is a guess about the line: three ticks
 * cost ninety four milliseconds whether the stream arrives like a metronome or in clumps. What
 * the buffer has to cover is the longest run of own substeps that delivered no new sender tick,
 * because the render eats one tick of lag in each of them, and that run is countable here with
 * no clock in sight, out of what the module already sees: every arrival and every substep. The
 * measure is that longest run over a window and the target is one tick more than it, so a stream
 * that never misses a substep asks for one tick and a stream that goes quiet for two substeps at
 * a time asks for three.
 *
 * Up at once, down slowly. The window is four buckets of sixty four substeps, so it remembers
 * between six and eight seconds. Raising reads only the newest two buckets and takes effect in
 * the substep that measures the gap, before the render has run out. Lowering reads all four,
 * gives back one tick at a time, and is barred for a second and a half after any change and for a
 * whole window after a reset. The gap between the two readings is the hysteresis: while the recent
 * window asks for no more than the target and the long window asks for no less, nothing moves,
 * so a stream whose gaps recur inside the window holds its target instead of pumping between two
 * values. A target that pumps is worse than one that is too large, because every change of it
 * travels through the slews above as a shift of the time being shown.
 *
 * Raising ignores that bar, because raising cannot pump: it only ever moves up, only ever to
 * what was just measured, and it stops at the ceiling. An underrun costs a standing body, while
 * thirty milliseconds of lag costs nothing anyone can see. A run of quiet substeps is counted
 * only while the render is still running: past the halt the sender has stopped rather than
 * jittered, which is what the halt and the resync are for. So a stall raises the target by the
 * ticks the render actually spent waiting and by no more, and three seconds of silence cost the
 * same as a tenth of a second instead of pinning the target at the ceiling for the next minute.
 *
 * The regulation is off until it is switched on, so a caller that says nothing keeps the fixed
 * target it asked for, and switching it off again puts the target back to the value init was
 * given. Neither switch, and no change of the target, disturbs a running slew, its cooldown or a
 * halt; only the two streaks are cleared, because they were counted against a target that has
 * gone. The measurement runs either way, so the reading is there to report even when nothing
 * acts on it.
 *
 * Every difference of two ticks is read as a signed number, so the counter's wrap is arithmetic
 * and not an event, as in every other module of this feature.
 */
#ifndef MULTIPLAYER_MP_TIMELINE_H
#define MULTIPLAYER_MP_TIMELINE_H

#include <stdbool.h>
#include <stdint.h>

/* The lag target in ticks. One is the floor: it is the smallest buffer that still absorbs the
 * beat between two ladders of the same nominal rate, and on a wired line that is the whole of what
 * there is to absorb. Six is the ceiling: at a hundred and eighty eight milliseconds the buffer has
 * reached the widest a rewind window may be in this design, and a line needing more than that is
 * not jittering but failing, for which the halt and the resync are the honest answer rather than
 * more buffer. Three stays the default: it survives one lost sample with nothing visible, which is
 * what an unmeasured line has to assume.
 *
 * Why the floor is one and not zero: two ladders of the same nominal rate with a hundred parts per
 * million of crystal drift cross a substep boundary about once every ten thousand substeps, five
 * minutes, and that crossing is a single empty substep followed by a double. The demand of a real
 * wired line is therefore one tick most of the time and two for the eight seconds after a
 * crossing. */
#define MP_TIMELINE_TARGET_MIN     1u
#define MP_TIMELINE_TARGET_MAX     6u
#define MP_TIMELINE_TARGET_DEFAULT 3u

/* One substep advances the render by eight eighths; a slew by nine or seven. */
#define MP_TIMELINE_EIGHTHS 8u

/* The band around the target in which nothing is done, the streak of substeps outside it that
 * earns a slew, the length of a slew, the rest after one, and the lag past the target that is a
 * stall rather than jitter.
 *
 * One correction takes at least eight plus thirty two plus eight substeps, a second and a half,
 * which caps the regulation at two thirds of a tick per second, about two percent of the rate.
 * Real crystal drift is parts per million; the ten minute drift test runs the two ladders two
 * tenths of a hertz apart, six tenths of a percent and three times what any two machines will
 * do, and the regulation absorbs it with a hundred and eighteen slews each way and no resync. A
 * sixteenth over sixteen substeps, the other slew shape considered, is a constant change away.
 * A receiver that ran no substep for three seconds finds ninety six new ticks, which is the
 * resync case, and rebases once. */
#define MP_TIMELINE_BAND       1u
#define MP_TIMELINE_STREAK     8u
#define MP_TIMELINE_SLEW       8u
#define MP_TIMELINE_COOLDOWN   32u
#define MP_TIMELINE_RESYNC_LAG 8u

/* The measured target, and WHAT is measured is the whole point of it.
 *
 * The first version of this counted substeps that delivered no new tick, and it was wrong in a way
 * that cost real milliseconds: the engine's substep ladder does not run evenly. It runs one to
 * four substeps in a frame, so a sender that hitches emits four ticks at once and then nothing for
 * three substeps. Counting arrivals, that reads as a gap of three and buys three ticks of buffer.
 * But those four ticks are already IN THE HISTORY: the render has everything it needs and never
 * comes close to running dry. The receiver was paying for jitter that had already been absorbed.
 *
 * So what is measured now is the DIP: how far the lag fell below its target, which is how much of
 * the buffer the jitter actually ate. A burst raises the lag and dips nothing. A real stall, where
 * nothing arrives while the render keeps advancing, dips exactly as deep as the stall was long.
 * The buffer a stream needs is its deepest dip plus one, and nothing more. Driven against the
 * module, a sender that clumps four substeps measured a target of five under the old count and
 * the floor under the dip, with zero halts: the render was fed the whole way, so the four extra
 * ticks the old count bought were buying nothing. One real four substep stall measured five and
 * took eighteen seconds to return; it now measures four and is back at the floor in fifteen.
 *
 * The window is MP_TIMELINE_LAG_BUCKETS buckets of MP_TIMELINE_LAG_BUCKET substeps, two seconds
 * apiece, each holding the deepest dip it saw. Raising reads the newest MP_TIMELINE_LAG_RECENT of
 * them so one bad bucket is enough and the answer is immediate; lowering reads all of them and is
 * barred for MP_TIMELINE_LAG_LOCK substeps after any change, and for a whole window after a reset,
 * so nothing is given back before the window has been seen once. */
#define MP_TIMELINE_LAG_BUCKET  64u
#define MP_TIMELINE_LAG_BUCKETS  4u
#define MP_TIMELINE_LAG_RECENT   2u
#define MP_TIMELINE_LAG_LOCK    48u
#define MP_TIMELINE_LAG_WINDOW (MP_TIMELINE_LAG_BUCKET * MP_TIMELINE_LAG_BUCKETS)

typedef struct mp_timeline {
    uint32_t target_lag;
    uint32_t init_target;    /* the target init was given, restored when the regulation is off */
    bool     auto_lag;       /* the target is measured rather than given */

    bool     have_newest;
    uint32_t newest_tick;    /* the newest tick received, by tick order */
    uint32_t first_tick;     /* the first tick received since the reset, for the warm-up */

    bool     warm;           /* the render tick is valid */
    bool     halted;         /* the render reached the newest tick and waits for the lag */
    uint32_t render_tick;
    uint8_t  render_phase8;  /* eighths of a tick past render_tick, 0..7 */

    uint32_t high_streak;    /* substeps in a row with the lag above the band */
    uint32_t low_streak;     /* substeps in a row with the lag below the band */
    uint32_t slew_left;      /* substeps left of the running slew */
    uint32_t slew_step;      /* eighths per substep while slewing */
    uint32_t cooldown;       /* substeps left in which the streaks do not count */

    uint32_t dip_bucket[MP_TIMELINE_LAG_BUCKETS];   /* the deepest dip per bucket, newest first */
    uint32_t bucket_left;    /* substeps left in the newest bucket */
    uint32_t lag_lock;       /* substeps left before the target may fall again */

    uint32_t inserted;       /* slews that fell back a tick */
    uint32_t skipped;        /* slews that caught up a tick */
    uint32_t resyncs;        /* rebases after a stall, and resumptions after a halt */
    uint32_t halts;          /* times the render reached the newest tick and stopped */
    uint32_t target_changes; /* times the regulation moved the target */
} mp_timeline_t;

/* An empty timeline with the given target lag, clamped to the legal range, and the regulation
 * off: the target stays where it was put until a caller asks for it to be measured. */
void mp_timeline_init(mp_timeline_t *timeline, uint32_t target_lag);

/* Back to empty: the warm-up starts over, every counter and the measurement are cleared, and the
 * target goes back to the value init was given. The regulation stays on or off as it was, being
 * a setting rather than peer state. */
void mp_timeline_reset(mp_timeline_t *timeline);

/* Measure the target instead of keeping it, or stop and put it back to the value init was given.
 * Switching either way leaves a running slew, a cooldown and a halt exactly as they were. */
void mp_timeline_set_auto_lag(mp_timeline_t *timeline, bool enabled);

/* A sample with this tick arrived. Only a tick newer than the newest held moves anything; an
 * older or duplicate tick is a reordered arrival and is ignored here, though the caller may still
 * store its sample. */
void mp_timeline_note_received(mp_timeline_t *timeline, uint32_t tick);

/* Once per own substep, after the substep's arrivals were noted. True when the render tick is
 * valid after the call: the warm-up finished, or the render moved, or it resumed after a halt.
 * False before the first tick, during the warm-up, and while halted. */
bool mp_timeline_advance(mp_timeline_t *timeline);

/* Whether the render tick is valid at all; the two readings below are meaningless before it is. */
bool     mp_timeline_render_known(const mp_timeline_t *timeline);
uint32_t mp_timeline_render_tick(const mp_timeline_t *timeline);
uint8_t  mp_timeline_render_phase8(const mp_timeline_t *timeline);

/* The newest tick noted, and the lag of the render tick behind it in whole ticks; both zero
 * before anything was noted. */
uint32_t mp_timeline_newest_tick(const mp_timeline_t *timeline);
int32_t  mp_timeline_lag(const mp_timeline_t *timeline);
bool     mp_timeline_halted(const mp_timeline_t *timeline);

uint32_t mp_timeline_inserted(const mp_timeline_t *timeline);
uint32_t mp_timeline_skipped(const mp_timeline_t *timeline);
uint32_t mp_timeline_resyncs(const mp_timeline_t *timeline);
uint32_t mp_timeline_halts(const mp_timeline_t *timeline);

/* What the regulation chose, what it measured and how often it moved. The measure is kept
 * whether the regulation is on or not, so a fixed target can be reported against what a
 * measured one would have asked for. */
uint32_t mp_timeline_target_lag(const mp_timeline_t *timeline);

/* The deepest dip in the window: how far the lag fell below its target at the worst moment,
 * which is how much of the buffer the stream's jitter actually ate. Zero on a stream that
 * never made the render wait, however unevenly its packets arrived. */
uint32_t mp_timeline_dip(const mp_timeline_t *timeline);
uint32_t mp_timeline_target_changes(const mp_timeline_t *timeline);
bool     mp_timeline_auto_lag(const mp_timeline_t *timeline);

#endif /* MULTIPLAYER_MP_TIMELINE_H */
