/* mp_stopwatch.h: where the host's substep spends its time.
 *
 * Layer 1, and it changes nothing. Every function here reads a clock and adds to a counter.
 *
 * Why it exists. The host draws about half the frames the client draws, and the cap steps down
 * because a share of its frames will not fit the budget. The share names itself: in one measured
 * second 32 of 216 frames were too expensive, and 32 is exactly the substep rate. The expensive
 * frames are the frames that carry a substep, so the question is not why the host's frames are
 * dear but what the host's substep costs, and in which of its stages.
 *
 * Why a histogram and not a mean. A mean of 32 substeps hides the one that took 9 ms, and the one
 * that took 9 ms is what the player sees and what starves the client's buffer. The buckets are
 * powers of two in microseconds, so one line shows both the floor and the tail.
 *
 * Why minutes as well. A level's histogram mixes its second minute with its tenth, and a host
 * whose substep grew dearer over a long level looks the same in it as one that was dear from the
 * start. So each stage also keeps a mean and a worst for every minute of the level, and the census
 * the number of actors it read, which is what separates more actors from a dearer read.
 *
 * Why this cannot become the cause it measures. One QueryPerformanceCounter is a few tens of
 * nanoseconds and there are at most a few dozen pairs in a substep, so the instrument costs a
 * microsecond or two in a budget of 31250. A finished stage is converted to microseconds and put in
 * its minute with two divisions, nanoseconds against stages of microseconds. Nothing a substep
 * calls here allocates, locks, logs or touches engine memory, and the report is printed once, at a
 * level end, outside the substep. The one other line, the setup of a session, is printed once per
 * arming, in the menu.
 */
#ifndef MULTIPLAYER_MP_STOPWATCH_H
#define MULTIPLAYER_MP_STOPWATCH_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

/* The stages, in the order a substep runs them. Every one prints a row, entered or not, so a
 * client's report shows by its empty rows which work is the host's alone. */
typedef enum mp_stopwatch_stage {
    MP_WATCH_ENGINE_TASKS,   /* from the enemies' activation scan to this side's task: the rest of
                              * the enemy task in the engine's first slot and every task between.
                              * A host runs its enemies' minds there and a client parks them, so
                              * the host's row less the client's is the AI */
    MP_WATCH_SUBSTEP,        /* the whole of it, pre-tick entry to substep end exit */
    MP_WATCH_TICK_PRE,       /* the first half: receive, decode, apply */
    MP_WATCH_TICK_POST,      /* the second half: the far bodies, the relays, the target resolver */
    MP_WATCH_RANGE_GATE,     /* in the second half: the far bodies the range gate is asked about */
    MP_WATCH_FAR_WINDOW,     /* in the second half: one far body's window, once for each bank */
    MP_WATCH_SUBSTEP_END,    /* the last half: this is where the host SENDS */
    MP_WATCH_CENSUS,         /* in the last half: the census of the enemies */
    MP_WATCH_PEER_SEND,      /* in the last half: the world to one peer, chosen and encoded */
    MP_WATCH_STAGE_COUNT
} mp_stopwatch_stage_t;

#define MP_WATCH_BUCKETS 12u

/* How many minutes of a level keep a row of their own. A later minute goes into the last one,
 * whose row then says so. */
#define MP_WATCH_MINUTES 32u

/* Armed from the ini. Unarmed, every call below is one comparison and a return, and no clock is
 * read at all. */
void mp_stopwatch_set_armed(bool armed);
bool mp_stopwatch_armed(void);

/* A stage begins and ends. Nesting is allowed and expected: the substep encloses its halves and a
 * half encloses its stages. Each stage keeps its own start, so an unbalanced pair spoils only its
 * own row. */
void mp_stopwatch_enter(mp_stopwatch_stage_t stage);
void mp_stopwatch_leave(mp_stopwatch_stage_t stage);

/* A stage that begins at a moment another module sees rather than at a call of its own: the first
 * mark since the stage last ended starts it, and a later one before it ends is the same moment seen
 * again, not an imbalance. */
void mp_stopwatch_mark(mp_stopwatch_stage_t stage);

/* The substep's own entry. It ends the engine's tasks ahead of it when the activation scan marked
 * their start in this substep, and enters the whole substep. A substep whose enemy task did not
 * reach the scan adds no run to that row. */
void mp_stopwatch_enter_substep(void);

/* The number of actors the census of this substep read, for the census's own minute row. A
 * substep whose census did not run adds nothing. */
void mp_stopwatch_note_census(bool ran, uint32_t actors);

/* Printed at every level end and at the end: every stage's row, the engine's share between this
 * side's halves, and a row per stage minute by minute. The rows then start over, because the next
 * report is about the next level. */
void mp_stopwatch_report(void);

/* Pure, so a test drives it: which bucket a duration in microseconds belongs to. Bucket 0 is
 * under 16 us, then each bucket is twice the one before it, and the last one is everything above.
 * The floor is 16 because nothing this instrument measures is meaningfully faster and a finer
 * floor would spend buckets where no decision lives. */
size_t mp_stopwatch_bucket(uint64_t microseconds);

/* One minute of one row: how many runs finished in it, their sum and the dearest, in
 * microseconds; for the census the actors in place of the microseconds. */
typedef struct mp_stopwatch_minute {
    uint32_t runs;
    uint32_t worst;
    uint64_t total;
} mp_stopwatch_minute_t;

/* Pure. The minute of the level a moment falls in, counted from 0 and not clamped; 0 for a
 * counter without a rate. */
uint32_t mp_stopwatch_minute_of(uint64_t elapsed_ticks, uint64_t ticks_per_minute);

/* Pure. One run into a minute. */
void mp_stopwatch_minute_add(mp_stopwatch_minute_t *minute, uint32_t value);

/* Pure. The minutes as the report prints them, `1 850/2100, 2 870/2300`, the minute counted from
 * 1, then its mean and its worst; a minute with no run is left out, `none` when every one is, and
 * the last minute reads `32+` when later minutes went into it. A row that would not fit ends in
 * `...` rather than in half a number. Returns the length written. */
size_t mp_stopwatch_minute_text(const mp_stopwatch_minute_t *minutes, size_t count,
                                bool last_holds_later, char *out, size_t size);

/* Pure. What the engine did between this side's three halves: the whole substep less the three,
 * nought when the three add up to more. */
uint64_t mp_stopwatch_between(uint64_t whole_us, uint64_t pre_us, uint64_t post_us,
                              uint64_t end_us);

/* Everything below runs armed or not. It times moments that happen once per arming, where a clock
 * costs nothing, and the arming that stood the host's menu still for eleven seconds had none on
 * it. */

/* A reading of the performance counter, and the microseconds between two readings. Both answer 0
 * on a machine without the counter, and the difference saturates rather than wraps, so it goes to
 * a %u as it is. */
uint64_t mp_stopwatch_ticks(void);
uint32_t mp_stopwatch_micros(uint64_t from, uint64_t to);

/* A duration already kept in counter ticks, a sum or a largest, in microseconds. Not the function
 * above with a start of 0: that one reads a start of 0 as "no reading" and answers 0. Saturates
 * like it. */
uint32_t mp_stopwatch_duration_micros(uint64_t ticks);

/* A session set up from the menu, timed as a whole. The end prints one line, and a warning in its
 * place when the menu stood still for MP_WATCH_SETUP_WARN_MS or longer. */
#define MP_WATCH_SETUP_WARN_MS 100u
void mp_stopwatch_setup_begin(void);
void mp_stopwatch_setup_end(void);

#endif /* MULTIPLAYER_MP_STOPWATCH_H */
