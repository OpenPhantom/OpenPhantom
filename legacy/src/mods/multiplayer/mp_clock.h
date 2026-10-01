/* mp_clock.h: the tick and time bookkeeping of the network model, as pure logic.
 *
 * Layer 1. No engine, no address, no socket and no operating system clock: every millisecond this
 * module ever sees comes in as a parameter, and everything it hands back is ticks or fractions of
 * one. It runs in the project's unit tests with nothing else in the process.
 *
 * The model is the one Quake 3 shipped and nothing newer, on purpose. There is no shared clock and
 * no synchronisation protocol. The host counts its fixed simulation substeps as an unsigned 32 bit
 * tick, thirty-two per second, and stamps every snapshot with that tick. The client estimates the
 * server's time as the newest snapshot's tick plus its own locally elapsed time, renders foreign
 * bodies a fixed buffer of snapshot intervals in the past, and blends between the two received
 * snapshots that bracket that moment. The alternative, stretching or shrinking the client's
 * simulation step until the clocks meet, is off the table here: this engine's ladder is fixed at
 * thirty-two substeps a second and hangs on data cells, so the step length is not ours to vary.
 * What replaces it is the pace decision at the bottom of this header, which occasionally runs one
 * extra prediction substep or skips one and leaves the step length alone.
 *
 * Why ticks are counted and never derived from a millisecond clock. The engine clamps the frame
 * delta feeding its simulation at a tenth of a second (the clamp at 0x00475706 writes the value
 * back, so the frame begin message sees the raw delta and every later message the clamped one),
 * so under load simulated time falls behind the wall clock and never ahead; below ten frames a
 * second it falls behind permanently. A tick computed from elapsed milliseconds would therefore
 * disagree with the simulation it claims to stamp. The host tick here advances only when the host
 * reports a substep, so it is the simulation's own count, whatever the wall clock did. The
 * engine's own simulation clock cell at 0x00868728 has the same defect from the other side and is
 * never sent as a timestamp.
 *
 * What this module is used for now. The far body ran on it once, with arrival stamps the bridge
 * read off the operating system's tick count at the pump, and the render moment then moved in
 * half ticks and occasionally backwards at every beat between the two machines' ladders. The far
 * body runs on the receiver's own substep count with a regulated lag instead, and what remains in
 * use here is the span conversion the dedicated server's re-stamping calls; the estimate, the
 * bracket, the pace decision and the arrival statistics stay built and tested.
 *
 * Three things are deliberately not built. The estimate is rebased hard on every newest snapshot
 * and never slewed; if foreign bodies visibly stutter at a rebase in the field, a bounded slew is
 * the named seam. There is no cap on the gap the bracket blends across, because where a body
 * sliding in a straight line for seconds becomes worse than one that freezes is the session
 * layer's line to draw. And there is no transit or round trip estimate: for interpolation the one
 * way offset cancels, and anything that needs the real offset is lag compensation's business.
 *
 * Why refusal instead of extrapolation. Before the first snapshot there is nothing to blend, and
 * on buffer underrun the only alternative to refusing is extrapolating the foreign body forward.
 * Its future depends on input this machine does not have; an extrapolated body that later meets
 * the truth is corrected by a visible teleport, which reads worse than a body holding its last
 * blended pose for a few frames. So foreign bodies are never freely extrapolated, and the caller
 * is told no. The clamp above makes this reachable in an honest way: when the host falls behind
 * its wall clock, the client's estimate runs ahead of the snapshots and interpolation refuses
 * rather than inventing positions.
 */
#ifndef MULTIPLAYER_MP_CLOCK_H
#define MULTIPLAYER_MP_CLOCK_H

#include <stdbool.h>
#include <stdint.h>

/* The ladder's rate. A tick is 1/32 s, which is 31.25 ms and not a whole number of milliseconds,
 * so the internal unit is the microsecond: a tick is exactly 31250 of them and no rounding exists
 * inside the module. Rounding happens only in the two millisecond conversion helpers below, and
 * each states its direction. */
#define MP_CLOCK_TICK_RATE   32u
#define MP_CLOCK_US_PER_TICK 31250u

/* The interpolation buffer, in snapshot intervals. At this protocol's rate one interval is exactly
 * one tick, because a snapshot goes out on every substep; the constant still says intervals so
 * that the day the two rates decouple is visible in the arithmetic. Two intervals, 62.5 ms, is the
 * floor for a wired LAN; three, 93.75 ms, survives one lost snapshot with nothing visible and is
 * the default. The HOST dictates the value: a client choosing its own would choose where in time
 * it sees everybody else, which is a fairness parameter, not a preference. */
#define MP_CLOCK_BUFFER_MIN_INTERVALS     2u
#define MP_CLOCK_BUFFER_MAX_INTERVALS     3u
#define MP_CLOCK_BUFFER_DEFAULT_INTERVALS 3u

/* How many received snapshot ticks the clock remembers for bracketing. Half a second at full rate,
 * which is several times the largest legal buffer plus the reordering depth a 32 per second stream
 * meaningfully produces; a snapshot older than everything in a full history is useless for
 * blending and is refused rather than kept. */
#define MP_CLOCK_HISTORY 16u

/* Past this the estimate is refused: the microsecond arithmetic would wrap somewhat later anyway,
 * and a session that has not seen a snapshot for an hour has no server time worth estimating. */
#define MP_CLOCK_ESTIMATE_LIMIT_MS 3600000u

/* The pace decision's constants. The target band is the host's input buffer target of one to two
 * ticks; the fill must sit outside the band for a full streak of reports, an eighth of a second of
 * evidence, before one substep is inserted or skipped; and after a correction the decision rests
 * for a second of reports, because the correction's effect only shows up in the reported fill
 * after a round trip, and deciding again before seeing it means correcting twice. One tick per
 * second is also the cap this puts on the correction rate, about three percent of the clock,
 * orders of magnitude above what real crystals drift, which is of the order of a hundred parts
 * in a million. Both constants are chosen rather than measured: a streak of four is an eighth of
 * a second of evidence so one late packet never moves the clock, and a cooldown of a second
 * covers any round trip this protocol is played over. Field measurement of both is outstanding. */
#define MP_CLOCK_PACE_TARGET_MIN_TICKS 1u
#define MP_CLOCK_PACE_TARGET_MAX_TICKS 2u
#define MP_CLOCK_PACE_STREAK           4u
#define MP_CLOCK_PACE_COOLDOWN         32u

/* The blend target for foreign bodies: the two received snapshot ticks bracketing the render
 * moment and how far between them it lies. alpha is in [0,1): zero means exactly the from tick,
 * and one is never returned because the moment is strictly before the to tick. The two ticks are
 * received ones and not necessarily adjacent; across a loss gap the pair spans the gap and alpha
 * runs through it, which is what makes a lost snapshot invisible with a buffer of three. */
typedef struct mp_clock_interp {
    uint32_t from_tick;
    uint32_t to_tick;
    float    alpha;
} mp_clock_interp_t;

/* The input pace machine, deliberately separate from the clock state so its decision is testable
 * as the pure function it is. All zeroes is the idle state. */
typedef struct mp_clock_pace {
    uint32_t low_streak;
    uint32_t high_streak;
    uint32_t cooldown;
} mp_clock_pace_t;

typedef struct mp_clock {
    /* Host side: the substep count. Nothing else, which is the point of the model. */
    uint32_t host_tick;

    /* Client side: the newest snapshot by tick, not by arrival, and when it arrived locally. */
    bool     have_snapshot;
    uint32_t newest_tick;
    uint32_t newest_arrival_ms;
    uint32_t buffer_intervals;

    /* Received snapshot ticks, ascending in wrapped order, oldest first. */
    uint32_t history[MP_CLOCK_HISTORY];
    uint32_t history_count;

    /* The arrival spacing estimator. mean and jitter are in microseconds. */
    bool     have_arrival;
    bool     have_interval;
    uint32_t last_arrival_ms;
    uint32_t mean_interval_us;
    uint32_t jitter_us;
} mp_clock_t;

void mp_clock_init(mp_clock_t *clock);

/* Host side. One call per simulation substep; the tick read afterwards is the stamp for the
 * snapshot of that substep. The counter wraps after four years and every comparison in this module
 * reads tick differences as signed, so the wrap is ordinary arithmetic, not an event. */
void     mp_clock_host_substep(mp_clock_t *clock);
uint32_t mp_clock_host_tick(const mp_clock_t *clock);

/* A snapshot arrived, carrying the server tick, at the caller's local millisecond clock. Returns
 * false when it changed nothing: a duplicate tick, or one older than everything a full history
 * holds. A newer tick advances the estimate base; an older one that fills a gap is kept for
 * bracketing but never moves the estimate backwards. */
bool mp_clock_snapshot(mp_clock_t *clock, uint32_t server_tick, uint32_t arrival_ms);

/* The host's dictate, applied by the session layer. Refuses anything outside two to three
 * intervals and leaves the current value standing. The clock itself never changes the buffer:
 * the jitter estimate below exists so a caller can choose, and choosing is the caller's. */
bool     mp_clock_set_buffer(mp_clock_t *clock, uint32_t intervals);
uint32_t mp_clock_buffer(const mp_clock_t *clock);

/* The estimated server time at the caller's local millisecond clock: the newest snapshot's tick
 * plus the time elapsed locally since it arrived, as a whole tick and the microseconds into it.
 * Offset from the true server clock by the one way transit of that snapshot, which is constant
 * enough for interpolation because the buffer sits on top of it. False before the first snapshot
 * and once the newest snapshot is more than an hour old. */
bool mp_clock_estimate(const mp_clock_t *clock, uint32_t local_ms,
                       uint32_t *tick, uint32_t *frac_us);

/* Where to render foreign bodies now: the bracket around the estimated server time minus the
 * buffer. False before the first snapshot, when the render moment lies before everything the
 * history holds, and on buffer underrun, where blending would become extrapolation; the header
 * block says why refusal is the right answer to all three. */
bool mp_clock_interpolation(const mp_clock_t *clock, uint32_t local_ms, mp_clock_interp_t *out);

/* The input pace decision, one call per received fill report from the snapshot header. Returns
 * +1 to run one extra prediction substep this frame, because the host's input buffer for this
 * client is starving and our inputs arrive after they were needed; -1 to skip one, because the
 * buffer is overfull and every input waits too long; 0 otherwise, which is the ordinary answer.
 * The hysteresis lives in the pace state: a fill bouncing in and out of the band resets the
 * streaks and never produces a decision, and a decision is followed by a cooldown. */
int32_t mp_clock_pace_decide(mp_clock_pace_t *pace, uint32_t fill_ticks);

/* The smoothed snapshot arrival spacing and its mean deviation, in microseconds, for a caller
 * choosing between two and three buffer intervals. False until two arrivals have produced a first
 * interval. Reading it obliges nobody: the clock never retunes itself with it. */
bool mp_clock_arrival_stats(const mp_clock_t *clock, uint32_t *mean_us, uint32_t *jitter_us);

/* Span conversions, defined for spans up to twelve days, where the intermediate product would
 * wrap. Both round to the nearest millisecond or tick; ticks_to_ms rounds a half up, and
 * ms_to_ticks can never land on a half because four times a millisecond count is never exactly
 * half of 125. */
uint32_t mp_clock_ticks_to_ms(uint32_t ticks);
uint32_t mp_clock_ms_to_ticks(uint32_t ms);

#endif /* MULTIPLAYER_MP_CLOCK_H */
