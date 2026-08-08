/* music_probe.c: state the music failure in two numbers, and provoke it on demand.
 *
 * ==============================================================================================
 * What is being measured, and why it needs no interpretation
 *
 * iMUSE's script engine is driven from a WINMM multimedia timer at roughly 50 Hz, and that same
 * heartbeat is the only thing that refills the music buffer. The buffer is played with
 * DSBPLAY_LOOPING, so a heartbeat that stops does not produce silence; it produces the last
 * fragment of music, circling forever. That is the reported defect.
 *
 * The heartbeat is gated on a counter that both threads raise and lower with plain, uninterlocked
 * instructions (`inc [mem]` on one side, load/test/dec/store on the other). Two concurrent
 * releases can lose one decrement, and the counter's own floor test means it can only ever get
 * stuck too HIGH. Stuck high, the heartbeat never runs again.
 *
 * So the whole failure is two numbers:
 *
 *     heartbeat ticks stop advancing   AND   the gate is not zero
 *
 * and this file logs exactly that. It writes NOTHING into the music DLL. If the ticks keep
 * advancing while the music is stuck, the theory above is dead and the log says so plainly,
 * which is worth as much as a confirmation.
 *
 * ---- THE STRESS MODE -------------------------------------------------------------------------
 * A defect that appears once an hour cannot be worked on. `MusicStressHz` drives cue changes far
 * faster than any level does, which raises the collision rate on BOTH sides at once: the game
 * thread takes the gate on every cue, and the timer thread takes it while it ramps group volume,
 * which is exactly what a cue change makes it do.
 *
 * It calls ImSetState DIRECTLY rather than through the engine's own setter, and that is
 * deliberate: the engine latches a cue before handing it over and refuses a repeat of the same
 * value, so driving it through the engine would both fight the latch and leave the game's music
 * state machine somewhere the game did not put it. Going straight to the DLL exercises precisely
 * the traffic the race needs and leaves the game's own latch untouched, so ordinary music resumes
 * as soon as the stress is switched off.
 *
 * It is audibly awful and it is off by default. It is a reproduction tool, not a feature.
 * ============================================================================================ */
#include "music_probe.h"

#include "imuse_guard.h"
#include "imuse_sites.h"

#include "common/ini.h"
#include "common/logging.h"

#include <windows.h>

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <string.h>

#define IMUSE_FIX_SECTION "imuse_fix"

/* The engine's own "no music" cue. Both setters use it as the null state, and driving it and back
 * is a transition the engine itself performs every time the music system is torn down. */
#define IMUSE_STATE_NULL 1000

/* The heartbeat runs at about 50 Hz. Below this many milliseconds without a single tick, while
 * the gate is held, nothing has gone wrong yet, a long frame or a debugger break is enough. */
#define STALL_DECLARED_AFTER_MS 1500u

/* iMUSE feeds its commentary one fragment at a time, "opening ", then the file name, then the
 * line break, so even ordinary play produces many short lines, and the stress mode multiplies
 * that by the cue rate.
 *
 * The cap is deliberately generous rather than tidy. The whole value of the capture is the LAST
 * few lines before the music stops, so a cap that is reached before the failure throws away the
 * only part worth having. A hundred thousand short lines is a large log and a cheap one. */
#define TRACE_LINE_LIMIT 100000

typedef struct probe_config {
    bool    enabled;
    int32_t report_seconds;      /* 0 = only report the anomaly, never the routine line */
    int32_t stress_hz;           /* 0 = off */
    bool    trace;               /* copy iMUSE's own commentary into our log */
    bool    watchdog;            /* release a gate that nobody can be holding any more */
    int32_t watchdog_ms;
    bool    lock_fix;            /* stop it getting stuck instead of noticing that it has */
} probe_config_t;

typedef struct probe_state {
    bool          installed;
    bool          active;
    imuse_sites_t sites;

    DWORD   last_report_ms;
    DWORD   last_tick_change_ms;
    int32_t last_ticks;

    /* The stamp iMUSE writes when a heartbeat BODY runs, and when we last saw it move. The tick
     * counter above only says the Windows timer fired; this says work was done. */
    int32_t last_body_stamp;
    DWORD   last_body_change_ms;

    int32_t watchdog_releases;

    /* The highest gate value seen since the last report. Sampling the gate once per frame FROM THE
     * GAME THREAD almost always reads 0, because the game thread is not inside ImLock at the
     * moment we look, so `gate=0` on a routine line is not a health certificate, it is the
     * expected reading. What the sample can see reliably is the value that never comes back down,
     * and the maximum is what shows a transient overlap on the way there. */
    int32_t gate_max;

    bool    stall_reported;
    int32_t stalls_seen;

    DWORD    last_stress_ms;
    bool     stress_toggle;
    int32_t  stress_cue;
    uint32_t stress_calls;             /* cumulative, so a report can show the rate achieved */
    uint32_t stress_calls_reported;
    bool     stress_silence_reported;

    void (__cdecl *original_trace)(const char *text);
    uint32_t trace_lines;
    bool     trace_capped;
} probe_state_t;

static probe_config_t config;
static probe_state_t  state;

/* ============================================================================================ */
static void load_configuration(void)
{
    config.enabled = ini_read_bool(IMUSE_FIX_SECTION, "MusicProbe", false);
    config.report_seconds = ini_read_int(IMUSE_FIX_SECTION, "MusicProbeSeconds", 10);
    config.stress_hz = ini_read_int(IMUSE_FIX_SECTION, "MusicStressHz", 0);
    config.trace = ini_read_bool(IMUSE_FIX_SECTION, "MusicTrace", false);
    config.lock_fix = ini_read_bool(IMUSE_FIX_SECTION, "MusicLockFix", true);
    config.watchdog = ini_read_bool(IMUSE_FIX_SECTION, "MusicHeartbeatWatchdog", false);
    config.watchdog_ms = ini_read_int(IMUSE_FIX_SECTION, "MusicHeartbeatWatchdogMs", 400);

    /* Below about 200 ms an ordinary long frame or a slow disk read could still be inside the
     * gate legitimately. Above a few seconds the music has already been gone long enough to
     * notice, which defeats the point. */
    if (config.watchdog_ms < 200) {
        config.watchdog_ms = 200;
    }
    if (config.watchdog_ms > 10000) {
        config.watchdog_ms = 10000;
    }

    if (config.report_seconds < 0) {
        config.report_seconds = 0;
    }
    if (config.report_seconds > 600) {
        config.report_seconds = 600;
    }
    /* Above the heartbeat's own rate the extra calls only queue up behind each other, and below
     * 1 Hz it is not a stress test. */
    if (config.stress_hz < 0) {
        config.stress_hz = 0;
    }
    if (config.stress_hz > 200) {
        config.stress_hz = 200;
    }
}

/* ============================================================================================ */
/* The heartbeat body is refused at four separate tests, so "it stopped" is four different
 * findings. Naming which one fired is the whole value of this instrument; three of them point
 * at completely different repairs, and one of them says this analysis was wrong. */
static void report_stall(int32_t ticks, int32_t gate, int32_t reentry, bool timer_alive,
                         DWORD stalled_ms)
{
    const char *verdict;

    state.stalls_seen++;

    if (!timer_alive) {
        verdict = "THE WINDOWS TIMER ITSELF STOPPED - the callback is not being fired at all, "
                  "which is not an iMUSE fault and not something the gate can explain";
    } else if (gate != 0) {
        verdict = "THE GATE IS HELD BY NOBODY. This is the predicted failure: the counter that "
                  "guards the heartbeat is raised and lowered with plain, uninterlocked "
                  "instructions from two threads, and its own floor test means a lost decrement "
                  "can only ever leave it stuck HIGH. Nothing then refills the music buffer, and "
                  "because that buffer is played LOOPING it circles its last fragment forever "
                  "instead of falling silent";
    } else if (reentry != 0) {
        verdict = "THE GATE IS CLEAR BUT THE RE-ENTRANCY FLAG IS SET - the last heartbeat body "
                  "was entered and never returned. That is a DIFFERENT fault from a lost "
                  "decrement and it is not repaired by releasing the gate";
    } else {
        verdict = "NEITHER THE GATE NOR THE RE-ENTRANCY FLAG EXPLAINS IT - both read zero while "
                  "the body still is not running. The remaining test is the 20 ms rate limit "
                  "against a clock, and this analysis does not cover that case";
    }

    log_error("MUSIC HEARTBEAT STALLED for %u ms. timer ticks %d (%s), gate %d (highest seen %d), "
              "re-entrancy %d. %s. Occurrence #%d.",
              (unsigned)stalled_ms, (int)ticks, timer_alive ? "still firing" : "FROZEN",
              (int)gate, (int)state.gate_max, (int)reentry, verdict, (int)state.stalls_seen);
}

/* The repair, and it is deliberately only ever a RELEASE.
 *
 * Writing 0 into a counter another thread might still hold sounds reckless and is not: the
 * condition below is that the heartbeat body has not run for hundreds of milliseconds while the
 * timer keeps firing, and no legitimate holder holds this gate that long, the one place that
 * could, the blocking file read, deliberately drops it around the read. And the underflow that
 * would normally worry you cannot happen, because the release side tests for zero before it
 * decrements: a holder that unlocks after us finds 0 and leaves it at 0. */
static void run_watchdog(int32_t gate, DWORD stalled_ms)
{
    if (!config.watchdog || gate == 0) {
        return;
    }
    if (stalled_ms < (DWORD)config.watchdog_ms) {
        return;
    }

    *state.sites.gate = 0;
    state.watchdog_releases++;
    state.last_body_change_ms = GetTickCount();   /* give it a period to recover before judging */

    log_error("WATCHDOG: released the iMUSE heartbeat gate (was %d) after %u ms with no heartbeat "
              "body and the timer still firing. If the music comes back within a second, the "
              "lost-decrement diagnosis is CONFIRMED in the field. Release #%d.",
              (int)gate, (unsigned)stalled_ms, (int)state.watchdog_releases);
}

static void report_recovered(int32_t ticks, DWORD stalled_ms)
{
    log_warning("the music heartbeat is running again after %u ms (tick counter now %d). A stall "
                "that ENDS by itself does not match a lost decrement, which can only get stuck - "
                "so this one was a long frame or a suspended thread, not the defect.",
                (unsigned)stalled_ms, (int)ticks);
}

static void report_routine(int32_t ticks, int32_t gate, DWORD elapsed_ms)
{
    int32_t  delta = ticks - state.last_ticks;
    uint32_t stress_delta = state.stress_calls - state.stress_calls_reported;
    unsigned rate = 0;
    unsigned stress_rate = 0;

    if (elapsed_ms > 0u) {
        if (delta > 0) {
            rate = (unsigned)((double)delta * 1000.0 / (double)elapsed_ms);
        }
        stress_rate = (unsigned)((double)stress_delta * 1000.0 / (double)elapsed_ms);
    }

    log_info("music heartbeat: %d ticks in %u ms (about %u/s, the timer is set to ~50/s); "
             "gate now %d, highest %d since the last line; stress fired %u cue changes "
             "(about %u/s). A gate of 0 on this line is EXPECTED, not a health certificate, the "
             "game thread does not hold it at the moment it is sampled.",
             (int)delta, (unsigned)elapsed_ms, rate,
             (int)gate, (int)state.gate_max, stress_delta, stress_rate);

    /* An instrument that reports nothing looks exactly like an instrument that found nothing.
     * If the trigger is switched on and has not fired once in a whole reporting period, that is a
     * defect in the trigger and every negative result taken from it is worthless. Say so, once. */
    if (config.stress_hz > 0 && stress_delta == 0u && !state.stress_silence_reported) {
        state.stress_silence_reported = true;
        log_error("MusicStressHz=%d is set but not one cue change was issued in %u ms. The "
                  "reproduction trigger is NOT working, so an absence of stalls in this session "
                  "proves nothing about the defect.",
                  (int)config.stress_hz, (unsigned)elapsed_ms);
    }

    state.gate_max = gate;
    state.stress_calls_reported = state.stress_calls;
}

/* ============================================================================================ */
/* iMUSE's own commentary, copied into our log on its way to the game's handler.
 *
 * This runs on both threads. The game thread reaches it through the cue setters and the stream
 * switch; the timer thread reaches it from inside the heartbeat. That is safe here for a reason
 * rather than by luck: log_* formats into a stack buffer and appends with a single WriteFile on a
 * FILE_APPEND_DATA handle, which the kernel serialises, so two threads cannot tear each other's
 * lines.
 *
 * Every line carries the heartbeat tick counter and the gate. That is the whole point of copying
 * the commentary rather than just reading it: when the music stops, the LAST line says what iMUSE
 * was doing and what the gate stood at while it did it. */
static void __cdecl trace_thunk(const char *text)
{
    if (text != NULL && !state.trace_capped) {
        if (state.trace_lines >= TRACE_LINE_LIMIT) {
            state.trace_capped = true;
            log_warning("iMUSE commentary capped at %u lines, no more will be copied.",
                        (unsigned)TRACE_LINE_LIMIT);
        } else {
            state.trace_lines++;
            /* The text arrives in fragments and often carries its own line break; the log adds
             * one of its own, so a trailing newline would double-space the whole capture. */
            size_t length = strlen(text);
            while (length > 0u && (text[length - 1u] == '\n' || text[length - 1u] == '\r')) {
                length--;
            }
            if (length > 0u) {
                log_info("[iMUSE t=%d g=%d] %.*s",
                         (int)*state.sites.heartbeat_ticks, (int)*state.sites.gate,
                         (int)length, text);
            }
        }
    }

    /* Always hand it on: the game registered this pointer and may be doing something with it. */
    if (state.original_trace != NULL) {
        state.original_trace(text);
    }
}

static void install_trace(void)
{
    if (!config.trace) {
        return;
    }
    if (state.sites.trace_slot == NULL) {
        log_warning("MusicTrace=1 but iMUSE's commentary pointer did not resolve, no capture");
        return;
    }
    state.original_trace = *state.sites.trace_slot;
    *state.sites.trace_slot = trace_thunk;
    log_info("iMUSE's own commentary is being copied into this log, each line stamped with the "
             "heartbeat tick counter and the gate. The game's own handler still receives every "
             "line (previous handler %08X).", (unsigned)(uintptr_t)state.original_trace);
}

/* ============================================================================================ */
static void drive_stress(DWORD now)
{
    DWORD period;

    if (config.stress_hz <= 0 || state.sites.set_state == NULL) {
        return;
    }
    period = 1000u / (DWORD)config.stress_hz;
    if (period == 0u) {
        period = 1u;
    }
    if (now - state.last_stress_ms < period) {
        return;
    }
    state.last_stress_ms = now;

    /* Alternate between the null cue and one real cue, which is a genuine transition in both
     * directions and makes the timer thread ramp group volume, the one thing that makes it
     * take the gate at all. */
    state.stress_toggle = !state.stress_toggle;
    state.sites.set_state(state.stress_toggle ? IMUSE_STATE_NULL : state.stress_cue);
    state.stress_calls++;
}

void music_probe_frame(void)
{
    int32_t ticks;
    int32_t gate;
    DWORD   now;

    if (!state.active) {
        return;
    }

    ticks = *state.sites.heartbeat_ticks;
    gate  = *state.sites.gate;
    now   = GetTickCount();

    if (gate > state.gate_max) {
        state.gate_max = gate;
    }

    if (ticks != state.last_ticks) {
        state.last_tick_change_ms = now;
    }

    /* The signal is the body, not the callback. The timer can keep firing perfectly while the
     * heartbeat is turned away at its gates, and that difference is the whole diagnosis. Where
     * the body stamp could not be resolved, fall back to the callback counter, weaker, because
     * it cannot tell a refused body from a dead timer, and the report says so. */
    if (state.sites.heartbeat_last_ms != NULL) {
        int32_t body = *state.sites.heartbeat_last_ms;
        int32_t reentry = (state.sites.heartbeat_reentry != NULL)
                        ? *state.sites.heartbeat_reentry : 0;
        bool    timer_alive = (now - state.last_tick_change_ms) <= STALL_DECLARED_AFTER_MS;

        if (body != state.last_body_stamp) {
            if (state.stall_reported) {
                report_recovered(ticks, now - state.last_body_change_ms);
                state.stall_reported = false;
            }
            state.last_body_stamp = body;
            state.last_body_change_ms = now;
        } else if (now - state.last_body_change_ms > STALL_DECLARED_AFTER_MS) {
            if (!state.stall_reported) {
                state.stall_reported = true;
                report_stall(ticks, gate, reentry, timer_alive,
                             now - state.last_body_change_ms);
            }
            run_watchdog(gate, now - state.last_body_change_ms);
        }
    } else if (ticks == state.last_ticks &&
               now - state.last_tick_change_ms > STALL_DECLARED_AFTER_MS) {
        if (!state.stall_reported) {
            state.stall_reported = true;
            report_stall(ticks, gate, 0, false, now - state.last_tick_change_ms);
        }
    } else if (ticks != state.last_ticks && state.stall_reported) {
        report_recovered(ticks, now - state.last_tick_change_ms);
        state.stall_reported = false;
    }

    if (config.report_seconds > 0 &&
        now - state.last_report_ms >= (DWORD)config.report_seconds * 1000u) {
        report_routine(ticks, gate, now - state.last_report_ms);
        state.last_report_ms = now;
        state.last_ticks = ticks;
    } else if (ticks != state.last_ticks && config.report_seconds == 0) {
        state.last_ticks = ticks;
    }

    drive_stress(now);
}

/* ============================================================================================ */
bool music_probe_install(void)
{
    if (state.installed) {
        return state.active;
    }
    state.installed = true;

    load_configuration();
    if (!config.enabled) {
        return false;
    }

    if (!imuse_sites_resolve(&state.sites)) {
        log_warning("MusicProbe=1 but iMUSE.DLL could not be read, no heartbeat watch this "
                    "session");
        return false;
    }

    state.last_ticks = *state.sites.heartbeat_ticks;
    state.last_tick_change_ms = GetTickCount();
    state.last_report_ms = state.last_tick_change_ms;
    state.last_stress_ms = state.last_tick_change_ms;
    state.last_body_change_ms = state.last_tick_change_ms;
    state.last_body_stamp = (state.sites.heartbeat_last_ms != NULL)
                          ? *state.sites.heartbeat_last_ms : 0;
    /* One real cue to alternate against. 1 is the first ordinary state in every shipped muscript;
     * if a level does not define it the DLL simply ignores the cue, which still costs it the gate
     * round trip this is here to provoke. */
    state.stress_cue = 1;
    state.active = true;

    /* After state.active, so a line that arrives during installation already finds the two cells
     * readable. Nothing here can be reached before the sites resolved. */
    install_trace();

    /* The repairs come last, so the log reads in the order the reasoning went: what was found,
     * then what was done about it. The watchdog stays whatever the ini says even when these
     * install; it costs two loads a frame and it is the only thing that would still catch a
     * third route into a stuck lock that nobody has found yet. */
    if (config.lock_fix) {
        (void)imuse_guard_install(&state.sites);
    } else {
        log_info("MusicLockFix=0, the music lock keeps the behaviour the DLL shipped with, and "
                 "only the watchdog stands between a stuck lock and looping music");
    }

    if (config.stress_hz > 0) {
        log_warning("MusicStressHz=%d - THE MUSIC IS BEING DELIBERATELY THRASHED to reproduce the "
                    "heartbeat stall. It will stutter and cut, and that is this setting working, "
                    "not a new fault. Set MusicStressHz=0 for normal play.",
                    (int)config.stress_hz);
    }
    if (config.watchdog) {
        log_warning("MusicHeartbeatWatchdog=1, if the heartbeat body stops for %d ms while the "
                    "timer keeps firing, the gate will be RELEASED. That is a repair, and it "
                    "changes what this session measures: with it on you learn whether releasing "
                    "the gate brings the music back, not how often the gate gets stuck.",
                    (int)config.watchdog_ms);
    }
    log_info("music heartbeat watch is live (routine report every %d s, 0 = only on a stall). "
             "A stall is declared after %u ms in which no heartbeat BODY ran%s.",
             (int)config.report_seconds, (unsigned)STALL_DECLARED_AFTER_MS,
             state.sites.heartbeat_last_ms != NULL
                 ? "" : ", the body stamp did not resolve, so this falls back to the weaker "
                        "callback counter and cannot tell a refused body from a dead timer");
    return true;
}
