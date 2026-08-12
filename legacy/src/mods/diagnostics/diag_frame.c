/* diag_frame.c: the frame clock, the machine under it, and the neighbourhood of every hitch.
 *
 * The reasoning is in the header. Three things a maintainer has to hold on to while changing this:
 *
 *   * Nothing expensive happens on the frame path. Two counter reads and a store. The process
 *     times, the fault counts and the graphics counter are sampled on a thread of their own, and
 *     the frame path reads only what that thread has already published. Moving any of it onto the
 *     frame would make the instrument a cause of what it measures.
 *   * The ring is written before it is judged. A hitch is reported from the ring after the fact, so
 *     the frames leading up to it are in the dump. A report built at the moment of the hitch would
 *     carry only the hitch.
 *   * The threshold is relative to the session's own median rather than to a fixed number of
 *     milliseconds, with an absolute floor underneath it. The same build runs at 30 frames a second
 *     and at 300, and a purely fixed threshold would report nothing on one and everything on the
 *     other. The floor is what stops the relative half from doing the same thing in reverse; the
 *     measurement that forced it in is beside HITCH_FLOOR_MS.
 *
 * The seam, if this ever needs one, is the sampler thread and the graphics counter it owns, which
 * touch no state the frame path writes and reach it only through the published sample block. It is
 * not worth taking yet: the two halves are read together far more often than either is read alone,
 * and the sampler is the shorter of them.
 */
#include "diag_frame.h"

#include "diag_log.h"

#include "common/frame_hook.h"
#include "common/ini.h"
#include "common/logging.h"
#include "common/memory.h"

#include <windows.h>
#include <psapi.h>
#include <pdh.h>
#include <pdhmsg.h>

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <stdlib.h>

/* The ring is a little over a second at a high frame rate, which is long enough that the frames
 * leading into a hitch are still in it and short enough to print without burying the log. */
#define FRAME_RING 128u

/* Frames printed either side of a hitch. Eight before is where a cause usually shows, four after
 * says whether it recovered at once or rang for a while. */
#define DUMP_BEFORE 8u
#define DUMP_AFTER  4u

/* At most this many hitch dumps per session. A picture that hitches every frame is a finding all by
 * itself and does not need three hundred dumps to say so; the count keeps rising either way. */
#define MAX_DUMPS 40u

/* The median is taken over this many of the most recent frames. Long enough to be a session's
 * character rather than a moment's, short enough to follow a genuine change of frame rate. */
#define MEDIAN_WINDOW 64u

/* Before this many frames have been seen there is no median worth comparing against, and the first
 * frames of a level are a load rather than play. */
#define WARMUP_FRAMES 240u

/* The default trigger: a frame that took this much longer than the median. 60 per cent of an 11 ms
 * median is about 7 ms, roughly half a frame, which is the smallest lateness a fast pan makes
 * visible. */
#define DEFAULT_HITCH_PERCENT 60

/* And at least this many milliseconds longer than the median, whatever the percentage says.
 *
 * The percentage on its own is scale free, which is what it was chosen for, and that turns into a
 * defect as soon as the frame rate is high. At a 2.2 ms median and a 20 per cent trigger, a frame
 * that ran 0.44 ms long is called a hitch, and 0.44 ms is ordinary scheduler noise that nobody can
 * see. The instrument then reported a dozen hitches a second on a picture the player called
 * perfectly smooth. Worse than the noise, it stopped being a passive observer while it did so: each
 * dump writes synchronously from inside the frame callback, which stretches the frame it is
 * describing and can trip the next one. A relative trigger with no floor turns the hitch detector
 * into a hitch generator.
 *
 * Two milliseconds is the smallest lateness worth a line at any rate. It is under half a frame at
 * 240 Hz, so nothing a person could notice is filtered out, and it is far above the scheduling
 * jitter that was being reported as a finding. */
#define HITCH_FLOOR_MS 2.0

#define SAMPLER_PERIOD_MS 1000u

/* The graphics counter. It is asked for by its English name because that is what the path is on the
 * system this was written on, and it is overridable in the ini because performance counter names
 * are localised by Windows: on a German system this same counter has a different path, and a
 * diagnostic that silently reported no graphics load on half the world's machines would be worse
 * than one that says it could not find the counter. */
#define GPU_COUNTER_DEFAULT "\\GPU Engine(*engtype_3D)\\Utilization Percentage"

#define DIAGNOSTICS_SECTION "diagnostics"

typedef struct frame_record {
    float wall_ms;          /* the wall clock between this frame's end and the previous one   */
    float engine_ms;        /* what the engine itself believed that interval was              */
} frame_record_t;

/* Published once a second by the sampler thread, read by the frame path. Every field is a plain
 * 32-bit value written by one thread and read by another, which on this architecture is atomic
 * enough for a number that is only ever printed. */
typedef struct machine_sample {
    volatile LONG process_cpu_tenths;   /* per cent of ONE core, times ten                     */
    volatile LONG system_cpu_tenths;    /* per cent of the whole machine, times ten            */
    volatile LONG faults_per_second;
    volatile LONG working_set_mb;
    volatile LONG gpu_percent;          /* -1 when the counter could not be read               */
    volatile LONG samples;
} machine_sample_t;

typedef struct diag_frame_state {
    bool          armed;
    int           level;
    float         hitch_factor;

    LARGE_INTEGER frequency;
    LARGE_INTEGER previous_end;
    const float  *engine_delta;

    frame_record_t ring[FRAME_RING];
    uint32_t       written;             /* total frames seen; the ring index is this modulo    */
    uint32_t       pending_dump;        /* frames still to be collected after a hitch          */
    uint32_t       hitches;
    uint32_t       dumps;
    uint32_t       hitch_at;            /* `written` of the frame that tripped it              */
    uint32_t       previous_hitch_at;   /* so the line can say how far apart they are          */

    /* The one second summary. */
    double   window_seconds;
    double   window_worst;
    uint32_t window_frames;
    uint32_t window_hitches;
    double   window_sum;

    machine_sample_t machine;
    HANDLE           sampler;
    volatile LONG    stop;
    bool             warned_no_clock;
} diag_frame_state_t;

static diag_frame_state_t frame_state;

/* ==============================================================================================
 * The sampler thread. Everything in here is allowed to be slow.
 * ============================================================================================ */

static double filetime_seconds(const FILETIME *time)
{
    ULARGE_INTEGER value;

    value.LowPart  = time->dwLowDateTime;
    value.HighPart = time->dwHighDateTime;
    return (double)value.QuadPart * 1.0e-7;    /* hundreds of nanoseconds */
}

/* The graphics counter, opened once and left open. A wildcard instance is used because a machine
 * has one engine per queue per adapter and the interesting one is whichever is doing the 3D work;
 * the values are summed and clamped, which is what a task manager shows. */
typedef struct gpu_counter {
    bool         tried;
    bool         live;
    PDH_HQUERY   query;
    PDH_HCOUNTER counter;
} gpu_counter_t;

static gpu_counter_t gpu;

static void gpu_open(const char *path)
{
    gpu.tried = true;
    if (PdhOpenQueryA(NULL, 0, &gpu.query) != ERROR_SUCCESS) {
        return;
    }
    if (PdhAddEnglishCounterA(gpu.query, path, 0, &gpu.counter) != ERROR_SUCCESS) {
        /* The English helper is the one that survives a localised system; if even it cannot find
         * the path, the counter set is not present on this machine at all, which is the case on
         * anything older than Windows 10 and on some server images. */
        PdhCloseQuery(gpu.query);
        gpu.query = NULL;
        return;
    }
    /* The first collection of a rate counter only establishes the baseline. */
    (void)PdhCollectQueryData(gpu.query);
    gpu.live = true;
}

static LONG gpu_read(void)
{
    PDH_FMT_COUNTERVALUE_ITEM_A *items;
    DWORD  size  = 0;
    DWORD  count = 0;
    double total = 0.0;
    DWORD  i;
    PDH_STATUS status;

    if (!gpu.live) {
        return -1;
    }
    if (PdhCollectQueryData(gpu.query) != ERROR_SUCCESS) {
        return -1;
    }

    status = PdhGetFormattedCounterArrayA(gpu.counter, PDH_FMT_DOUBLE, &size, &count, NULL);
    if (status != PDH_MORE_DATA || size == 0) {
        return -1;
    }
    items = (PDH_FMT_COUNTERVALUE_ITEM_A *)LocalAlloc(LPTR, size);
    if (items == NULL) {
        return -1;
    }
    if (PdhGetFormattedCounterArrayA(gpu.counter, PDH_FMT_DOUBLE, &size, &count, items)
        == ERROR_SUCCESS) {
        for (i = 0; i < count; ++i) {
            total += items[i].FmtValue.doubleValue;
        }
    }
    LocalFree(items);

    if (total < 0.0) {
        total = 0.0;
    }
    if (total > 100.0) {
        total = 100.0;
    }
    return (LONG)(total + 0.5);
}

static DWORD WINAPI sampler_main(LPVOID parameter)
{
    HANDLE   process = GetCurrentProcess();
    FILETIME creation, exit, kernel, user;
    FILETIME idle_before, kernel_before, user_before;
    double   cpu_before = 0.0;
    DWORD    faults_before = 0;
    bool     have_previous = false;

    (void)parameter;
    SetThreadPriority(GetCurrentThread(), THREAD_PRIORITY_BELOW_NORMAL);

    GetSystemTimes(&idle_before, &kernel_before, &user_before);

    while (InterlockedCompareExchange(&frame_state.stop, 0, 0) == 0) {
        PROCESS_MEMORY_COUNTERS memory;
        FILETIME idle_now, kernel_now, user_now;
        double   cpu_now;
        double   busy;
        double   total;

        Sleep(SAMPLER_PERIOD_MS);

        if (GetProcessTimes(process, &creation, &exit, &kernel, &user)) {
            cpu_now = filetime_seconds(&kernel) + filetime_seconds(&user);
            if (have_previous) {
                double share = (cpu_now - cpu_before) / ((double)SAMPLER_PERIOD_MS / 1000.0);

                InterlockedExchange(&frame_state.machine.process_cpu_tenths,
                                    (LONG)(share * 1000.0));
            }
            cpu_before = cpu_now;
        }

        if (GetSystemTimes(&idle_now, &kernel_now, &user_now)) {
            double idle_delta   = filetime_seconds(&idle_now)   - filetime_seconds(&idle_before);
            double kernel_delta = filetime_seconds(&kernel_now) - filetime_seconds(&kernel_before);
            double user_delta   = filetime_seconds(&user_now)   - filetime_seconds(&user_before);

            /* The kernel figure includes idle, which is the trap in this API and the reason a naive
             * reading reports a machine at 100 per cent while it is asleep. */
            total = kernel_delta + user_delta;
            busy  = total - idle_delta;
            if (total > 0.0) {
                InterlockedExchange(&frame_state.machine.system_cpu_tenths,
                                    (LONG)((busy / total) * 1000.0));
            }
            idle_before   = idle_now;
            kernel_before = kernel_now;
            user_before   = user_now;
        }

        if (GetProcessMemoryInfo(process, &memory, sizeof memory)) {
            if (have_previous) {
                InterlockedExchange(&frame_state.machine.faults_per_second,
                                    (LONG)(memory.PageFaultCount - faults_before));
            }
            faults_before = memory.PageFaultCount;
            InterlockedExchange(&frame_state.machine.working_set_mb,
                                (LONG)(memory.WorkingSetSize / (1024u * 1024u)));
        }

        InterlockedExchange(&frame_state.machine.gpu_percent, gpu_read());
        InterlockedIncrement(&frame_state.machine.samples);
        have_previous = true;
    }
    return 0;
}

/* ==============================================================================================
 * The frame path
 * ============================================================================================ */

static int compare_float(const void *left, const void *right)
{
    float a = *(const float *)left;
    float b = *(const float *)right;

    if (a < b) {
        return -1;
    }
    return (a > b) ? 1 : 0;
}

/* The median of the most recent frames, which is what a hitch is measured against. Taken from a
 * copy so that the ring itself is never reordered. */
static float recent_median(void)
{
    float    window[MEDIAN_WINDOW];
    uint32_t have = (frame_state.written < MEDIAN_WINDOW) ? frame_state.written : MEDIAN_WINDOW;
    uint32_t i;

    if (have == 0u) {
        return 0.0f;
    }
    for (i = 0u; i < have; ++i) {
        uint32_t index = (frame_state.written - 1u - i) % FRAME_RING;

        window[i] = frame_state.ring[index].wall_ms;
    }
    qsort(window, have, sizeof window[0], compare_float);
    return window[have / 2u];
}

static void dump_hitch(float median)
{
    uint32_t first;
    uint32_t last;
    uint32_t i;

    ++frame_state.dumps;
    diag_log_write("frame HITCH %u: %.2f ms against a median of %.2f, at frame %u, %u frames "
                   "since the last one. "
                   "cpu %.1f%% of a core, machine %.1f%%, faults %ld/s, working set %ld MB, "
                   "gpu %ld%%",
                   frame_state.hitches,
                   (double)frame_state.ring[frame_state.hitch_at % FRAME_RING].wall_ms,
                   (double)median, frame_state.hitch_at,
                   (frame_state.previous_hitch_at > 0u)
                       ? (frame_state.hitch_at - frame_state.previous_hitch_at) : 0u,
                   (double)frame_state.machine.process_cpu_tenths / 10.0,
                   (double)frame_state.machine.system_cpu_tenths / 10.0,
                   frame_state.machine.faults_per_second,
                   frame_state.machine.working_set_mb,
                   frame_state.machine.gpu_percent);

    first = (frame_state.hitch_at > DUMP_BEFORE) ? (frame_state.hitch_at - DUMP_BEFORE) : 0u;
    last  = frame_state.hitch_at + DUMP_AFTER;
    for (i = first; i <= last && i < frame_state.written; ++i) {
        const frame_record_t *record = &frame_state.ring[i % FRAME_RING];

        /* The engine's own idea of the interval is printed beside the wall clock because the two
         * disagreeing is a finding of its own: a frame that took 25 ms of wall time while the
         * engine believed 11 means the world advanced by 11 and the picture stood still for 25. */
        diag_log_write("  %s f%-6u wall %7.3f ms   engine %7.3f ms   %s",
                       (i == frame_state.hitch_at) ? "->" : "  ", i,
                       (double)record->wall_ms, (double)record->engine_ms,
                       (record->engine_ms > 0.0f &&
                        record->wall_ms > record->engine_ms * 1.5f)
                           ? "the engine's own clock did not stretch with it" : "");
    }

    frame_state.previous_hitch_at = frame_state.hitch_at;

    if (frame_state.dumps == MAX_DUMPS) {
        diag_log_write("frame: %u dumps printed and no more this session. The count goes on rising "
                       "and the summary line still carries it.", MAX_DUMPS);
    }
}

static void report_second(float median)
{
    /* CPU milliseconds per frame is the number that answers "are we compute bound", and a
     * percentage of a core is not. Measured on one session: 68 per cent of a core at 165 frames a
     * second is 4.2 ms of CPU on each of them, which against a 6.09 ms frame is most of it. A
     * reader given only the percentage has to do that division in their head, and the whole point
     * of this line is to be readable at a glance during play. */
    diag_log_write("frame second: %u frames, %.1f fps, median %.2f ms, mean %.2f, worst %.2f, "
                   "%u hitches | cpu %.2f ms/frame (%.1f%% of a core), machine %.1f%%, "
                   "faults %ld/s, working set %ld MB, gpu %ld%% | %u hitches this session",
                   frame_state.window_frames,
                   (frame_state.window_seconds > 0.0)
                       ? (double)frame_state.window_frames / frame_state.window_seconds : 0.0,
                   (double)median,
                   (frame_state.window_frames > 0u)
                       ? frame_state.window_sum / (double)frame_state.window_frames : 0.0,
                   frame_state.window_worst, frame_state.window_hitches,
                   (frame_state.window_frames > 0u)
                       ? ((double)frame_state.machine.process_cpu_tenths / 1000.0) * 1000.0
                         / (double)frame_state.window_frames : 0.0,
                   (double)frame_state.machine.process_cpu_tenths / 10.0,
                   (double)frame_state.machine.system_cpu_tenths / 10.0,
                   frame_state.machine.faults_per_second,
                   frame_state.machine.working_set_mb,
                   frame_state.machine.gpu_percent,
                   frame_state.hitches);

    frame_state.window_frames  = 0u;
    frame_state.window_hitches = 0u;
    frame_state.window_seconds = 0.0;
    frame_state.window_worst   = 0.0;
    frame_state.window_sum     = 0.0;
}

static void on_frame_end(void)
{
    LARGE_INTEGER now;
    frame_record_t *record;
    double   wall_ms;
    float    median;

    if (!QueryPerformanceCounter(&now)) {
        return;
    }
    if (frame_state.previous_end.QuadPart == 0) {
        frame_state.previous_end = now;
        return;
    }

    wall_ms = (double)(now.QuadPart - frame_state.previous_end.QuadPart) * 1000.0
            / (double)frame_state.frequency.QuadPart;
    frame_state.previous_end = now;

    /* The engine's number belongs to the previous row, and getting that wrong made this instrument
     * accuse the engine of not seeing hitches it had seen perfectly well.
     *
     * The engine computes its frame delta at the start of a frame, so the value standing in the
     * cell when this callback runs at the end of one is the period of the frame before. Printed on
     * the current row it looks like a disagreement of exactly one frame, and on a hitch that reads
     * as "the world advanced by 6 ms while the picture stood still for 13". It does not: the 13
     * turns up in the engine's own number one row later. So it is written back one row. */
    record = &frame_state.ring[frame_state.written % FRAME_RING];
    record->wall_ms   = (float)wall_ms;
    record->engine_ms = 0.0f;
    if (frame_state.written > 0u && frame_state.engine_delta != NULL) {
        frame_state.ring[(frame_state.written - 1u) % FRAME_RING].engine_ms =
            *frame_state.engine_delta * 1000.0f;
    }
    ++frame_state.written;

    ++frame_state.window_frames;
    frame_state.window_seconds += wall_ms / 1000.0;
    frame_state.window_sum     += wall_ms;
    if (wall_ms > frame_state.window_worst) {
        frame_state.window_worst = wall_ms;
    }

    /* The dump is collected after the hitch, which is why it is a countdown rather than a call: the
     * frames that follow one are part of the picture and they do not exist yet. */
    if (frame_state.pending_dump > 0u) {
        --frame_state.pending_dump;
        if (frame_state.pending_dump == 0u) {
            dump_hitch(recent_median());
        }
    } else if (frame_state.written > WARMUP_FRAMES) {
        median = recent_median();
        if (median > 0.0f && wall_ms > (double)(median * frame_state.hitch_factor)
                          && wall_ms > (double)median + HITCH_FLOOR_MS) {
            ++frame_state.hitches;
            ++frame_state.window_hitches;
            frame_state.hitch_at = frame_state.written - 1u;
            if (frame_state.level >= 2 && frame_state.dumps < MAX_DUMPS) {
                frame_state.pending_dump = DUMP_AFTER + 1u;
            }
        }
    }

    if (frame_state.window_seconds >= 1.0) {
        report_second(recent_median());
    }
}

/* g_frameDelta, read out of the operand of the frame hook's own first instruction rather than
 * written down. Three builds of this engine ship in one installation, the retail WMAIN.EXE, the
 * wmain.exe beside it and obi.exe, which is a recompile, and an address table would have named a
 * different cell on the recompile without saying so. */
static const float *resolve_engine_delta(void)
{
    uintptr_t site = frame_hook_site();
    uint32_t  address = 0;

    if (site == 0) {
        return NULL;
    }
    if (!memory_read_u32(site + FRAME_HOOK_FRAME_DELTA_OPERAND_OFFSET, &address) ||
        !memory_is_inside_image(address, sizeof(float))) {
        return NULL;
    }
    return (const float *)(uintptr_t)address;
}

int diag_frame_install(int level, int hitch_percent)
{
    char path[256];

    if (level <= 0 || frame_state.armed) {
        return 0;
    }
    if (!QueryPerformanceFrequency(&frame_state.frequency) ||
        frame_state.frequency.QuadPart == 0) {
        log_warning("the performance counter is not available, so the frame clock cannot be "
                    "measured and this observer stays off");
        return 0;
    }
    if (hitch_percent <= 0) {
        hitch_percent = DEFAULT_HITCH_PERCENT;
    }
    frame_state.hitch_factor = 1.0f + ((float)hitch_percent / 100.0f);

    if (!frame_hook_add(on_frame_end)) {
        log_warning("the per-frame hook could not be installed, so frame timing stays off");
        return 0;
    }
    frame_state.engine_delta = resolve_engine_delta();

    if (!ini_read_string(DIAGNOSTICS_SECTION, "FrameGpuCounter", GPU_COUNTER_DEFAULT,
                         path, sizeof path)) {
        path[0] = 0;
    }
    if (path[0] != 0) {
        gpu_open(path);
    }

    frame_state.machine.gpu_percent = -1;
    frame_state.sampler = CreateThread(NULL, 0, sampler_main, NULL, 0, NULL);

    frame_state.armed = true;
    frame_state.level = level;

    log_info("frame timing armed at level %d: one summary a second%s. A hitch is a frame more than "
             "%d per cent past the median of the last %u and at least %.1f ms past it. The "
             "percentage is what makes this comparable across rates, because the same build runs "
             "at 30 frames a second and at 300; the floor beside it is what stops the percentage "
             "from reporting scheduler noise once frames are short, where a fifth of the median is "
             "well under a millisecond. That mattered in practice rather than in theory: without "
             "the floor this instrument reported a dozen hitches a second on a picture that was "
             "called perfectly smooth, and since each dump writes from inside the frame callback "
             "it was stretching the very frames it was measuring. The engine's own idea of each "
             "interval is printed beside the wall clock, written back one row because the engine "
             "computes it at the start of a frame: the two disagreeing means something outside the "
             "engine's own clock stretched that frame. CPU, page faults and the graphics counter "
             "are sampled once a second on a thread of their own, so that the measurement cannot "
             "become a cause. This observer installs no hook of its own.",
             level, (level >= 2) ? ", plus the frames around every hitch" : "",
             hitch_percent, MEDIAN_WINDOW, HITCH_FLOOR_MS);
    if (frame_state.engine_delta == NULL) {
        log_warning("the engine's own frame delta did not resolve, so only the wall clock is "
                    "reported and the 'the engine did not see this one' verdict is unavailable");
    }
    if (!gpu.live) {
        log_warning("the graphics load counter did not open. Windows localises its name, so on a "
                    "non-English system the path differs; set FrameGpuCounter in the ini to the "
                    "local name, or leave it and read the rest, which does not depend on it.");
    }
    return 1;
}
