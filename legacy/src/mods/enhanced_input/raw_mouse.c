/* raw_mouse.c: the device's own counts, with their timing intact, on a thread of our own.
 *
 * SIZE NOTE. The file sits between the preferred 400 lines and the 600 mark, and roughly a third of
 * it is comment. The code is a window, a thread, a registration and two interlocked adds; what is
 * long is the account of the compatibility shim that made the first build of this feature inert,
 * the ladder that was built while the cause of that was still unknown, and the unit the reader has
 * to answer in. All three are invisible in the expressions and expensive to rediscover. One seam
 * was measured and rejected: moving the unshimmed resolution into the shared library. Nothing else
 * in the tree registers for raw input, so it would have been a single caller reaching into another
 * directory and the evidence would have ended up further from the call it explains.
 *
 * ==============================================================================================
 * Why a thread and a window of our own, rather than the game's
 *
 * Raw input is delivered as a window message, so something has to own a window and pump it. The
 * obvious choice is the game's own window, by replacing its window procedure. That is the wrong
 * choice here for a reason this tree has already paid for once: a graphics wrapper sits in front of
 * that procedure, and enhanced_resolution's own log recorded it swallowing WM_ACTIVATEAPP before
 * the engine ever saw the message. Subclassing a window somebody else has already subclassed means
 * agreeing about an order that neither side writes down, and getting it wrong costs the game its
 * input rather than costing us our feature.
 *
 * A message-only window on a thread of our own has none of that. It cannot be seen, cannot be
 * activated, cannot take focus and shares nothing with the engine. RIDEV_INPUTSINK is what makes it
 * work: the registration asks for the device's data even while our window is not in the foreground,
 * which it never is.
 *
 * RIDEV_NOLEGACY is the flag deliberately not used, at any rung of the ladder below. It suppresses
 * ordinary mouse messages for the whole process, and the engine's menus, its pointer warp and its
 * own DirectInput device all live on those.
 *
 * The thread is started from the install call rather than from DllMain, so no part of this runs
 * under the loader lock.
 *
 * ==============================================================================================
 * The one thing that must not happen, and what is done about it
 *
 * A registration that succeeds and then delivers nothing would leave the player with a mouse that
 * does not turn the view at all, which is far worse than the jitter this exists to remove. So the
 * packet count is exported, the caller watches it against the engine's own reader, and switches
 * back when raw input has been silent while the engine's reader was not. That is a real fallback
 * rather than a described one.
 *
 * The other failure worth naming is a device that reports absolute positions instead of relative
 * counts. Remote desktops, some tablets and the virtual mice that streaming and VR software install
 * all do it. Absolute packets carry a position in a normalised space rather than a delta, so they
 * are differenced against the previous one here; the first such packet contributes nothing, because
 * there is nothing to difference it against yet.
 */
#include "raw_mouse.h"

#include "mouse_look.h"

#include "common/logging.h"

#include <windows.h>

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <string.h>

#define RAW_MOUSE_CLASS   "obi_engine_fixes_raw_mouse"
#define HID_USAGE_PAGE_GENERIC_DESKTOP 0x01
#define HID_USAGE_GENERIC_MOUSE        0x02

/* How long the install call waits for the thread to report. It only has to create a window and
 * make one registration call, so a second is already generous; the point of the bound is that a
 * thread which never reports must not hang the game's startup. */
#define STARTUP_TIMEOUT_MS 1000

typedef struct raw_mouse_state {
    bool     installed;
    bool     active;

    HANDLE   thread;
    HANDLE   ready;                 /* the thread signals it once, after it has tried to register */
    HWND     window;
    HWND     fallback_window;       /* only created if the message-only one is refused */
    bool     registered;

    /* Written by the raw input thread, read and cleared by the game thread. Interlocked on both
     * sides, because these are the only two cells the two threads share. */
    volatile LONG accumulated_counts;
    volatile LONG packets;             /* lifetime, for the report rate */
    volatile LONG packets_since_take;  /* reset by every take */
    /* The timestamp of the newest packet, in performance counter ticks. Written on the raw thread
     * and read on the game thread; a 64-bit value cannot be read atomically on a 32-bit build, so
     * it is guarded rather than assumed. */
    LONGLONG         newest_tick;
    CRITICAL_SECTION lock;
    LONGLONG         taken_tick;        /* game thread only: the newest packet already consumed */
    LONGLONG         ticks_per_second;

    /* A second, independent drain, for the menu cursor, and both axes rather than one.
     *
     * It exists because the reader above is destructive by design: what it returns has been taken
     * out, so exactly one consumer may call it. The view turn is that consumer. The cursor needs
     * the same packets and must not take them away from it, so the packets are added to two
     * accumulators and each consumer clears its own. Neither can starve the other and neither had
     * to change.
     *
     * The cursor also needs the vertical axis, which the view turn has no use for. */
    volatile LONG cursor_counts_x;
    volatile LONG cursor_counts_y;

    /* The absolute-position case, touched only on the raw input thread. */
    bool  absolute_seen;
    bool  absolute_have_previous;
    LONG  absolute_previous_x;
    LONG  absolute_previous_y;
    bool  warned_absolute;
} raw_mouse_state_t;

static raw_mouse_state_t raw_state;

/* ==============================================================================================
 * The raw input thread
 * ============================================================================================ */
static void accumulate_packet(const RAWMOUSE *mouse)
{
    LONG delta;
    LONG delta_y;

    if ((mouse->usFlags & MOUSE_MOVE_ABSOLUTE) != 0) {
        /* An absolute packet is a position, not a movement, so the movement is the difference
         * between two of them. The first one establishes the origin and contributes nothing. */
        if (!raw_state.absolute_have_previous) {
            raw_state.absolute_have_previous = true;
            raw_state.absolute_previous_x    = mouse->lLastX;
            raw_state.absolute_previous_y    = mouse->lLastY;
            raw_state.absolute_seen          = true;
            return;
        }
        delta   = mouse->lLastX - raw_state.absolute_previous_x;
        delta_y = mouse->lLastY - raw_state.absolute_previous_y;
        raw_state.absolute_previous_x = mouse->lLastX;
        raw_state.absolute_previous_y = mouse->lLastY;
        raw_state.absolute_seen       = true;
    } else {
        delta   = mouse->lLastX;
        delta_y = mouse->lLastY;
    }

    if (delta != 0) {
        InterlockedExchangeAdd(&raw_state.accumulated_counts, delta);
        InterlockedExchangeAdd(&raw_state.cursor_counts_x, delta);
    }
    if (delta_y != 0) {
        InterlockedExchangeAdd(&raw_state.cursor_counts_y, delta_y);
    }
    InterlockedIncrement(&raw_state.packets);
    InterlockedIncrement(&raw_state.packets_since_take);

    /* The arrival time is the whole point of reading the device directly: it is what lets the
     * counts be divided by the time the reports span rather than by the interval they happened
     * to be collected in. */
    {
        LARGE_INTEGER now;
        QueryPerformanceCounter(&now);
        EnterCriticalSection(&raw_state.lock);
        raw_state.newest_tick = now.QuadPart;
        LeaveCriticalSection(&raw_state.lock);
    }
}

static void handle_raw_input(HRAWINPUT handle)
{
    /* A mouse packet is small and fixed, so the buffer is a local of the exact type rather than a
     * two-call size query and an allocation. GetRawInputData fills at most what it is told. */
    RAWINPUT input;
    UINT     size = (UINT)sizeof(input);
    UINT     written;

    written = GetRawInputData(handle, RID_INPUT, &input, &size, (UINT)sizeof(RAWINPUTHEADER));
    if (written == (UINT)-1) {
        return;
    }
    if (input.header.dwType != RIM_TYPEMOUSE) {
        return;
    }
    accumulate_packet(&input.data.mouse);
}

static LRESULT CALLBACK raw_mouse_proc(HWND window, UINT message, WPARAM wparam, LPARAM lparam)
{
    if (message == WM_INPUT) {
        handle_raw_input((HRAWINPUT)lparam);
        /* Documented requirement: an application that handles WM_INPUT must still call the default
         * procedure so that the system can clean the packet up. */
        return DefWindowProcA(window, message, wparam, lparam);
    }
    return DefWindowProcA(window, message, wparam, lparam);
}

/* ==============================================================================================
 * The registration ladder, and why it is a ladder rather than one call
 *
 * The first build made exactly one registration, a message-only window with RIDEV_INPUTSINK, and it
 * came back ERROR_INVALID_PARAMETER on the reporting machine, so the whole feature was inert while
 * the log said only that it had failed. A standalone probe on that same machine then registered
 * every variant successfully, including the one the DLL had used, which means the refusal is a
 * property of the process rather than of the parameters or of the machine.
 *
 * Guessing which process-wide condition causes it has been the losing move all day. So the ladder
 * tries the variants in order of preference, names the one that took, and names the error from each
 * that did not. Whatever the cause turns out to be, the log will say which combination works and
 * the feature will already be running on it.
 *
 * The order is by cost, not by taste. The sink on a message-only window is wanted because it can
 * never interfere with anything; a foreground-only registration on the same window is next, and it
 * is a real degradation rather than a lesser one, since packets then stop while the window is not
 * in front, which for an invisible window means possibly never; a hidden top-level window is last
 * because it is a real window in the system's lists.
 * ============================================================================================ */
/* ==============================================================================================
 * The shim, and why the first build of this feature was inert
 *
 * Windows ships an application compatibility fix for this exact executable. Querying the shim
 * database for the game's own WMAIN.EXE matches two entries, both carrying IgnoreAltTab, and the
 * 32-bit AcGenral.dll contains the symbol NS_IgnoreAltTab::APIHook_RegisterRawInputDevices.
 * Microsoft's own description of that fix says it intercepts RegisterRawInputDevices, causes the
 * call to fail with ERROR_INVALID_PARAMETER, and thereby prevents WM_INPUT from being delivered.
 *
 * That is exactly the error the first build logged, and it explains the shape of the failure that
 * nothing else did: every variant was refused identically inside the game while a standalone probe
 * on the same machine registered all of them. The refusal is attached to the executable, not to the
 * parameters, the window, the thread or the machine.
 *
 * A shim of that era hooks the import tables of the modules in the process. Resolving the function
 * out of user32's own export table therefore reaches the real one. GetProcAddress is not used for
 * it, because the shim engine hooks that too; the export directory is walked by hand instead, which
 * nothing in the process is in a position to rewrite.
 *
 * This is not a workaround for a defect in Windows. The fix exists because the 1999 game mishandles
 * Alt-Tab, and switching raw input off is how it was made to do so. We want the modern path and we
 * are inside our own process; the compatibility fix keeps doing everything else it does.
 * ============================================================================================ */
typedef BOOL (WINAPI *register_raw_fn_t)(PCRAWINPUTDEVICE, UINT, UINT);

static register_raw_fn_t resolve_unshimmed(void)
{
    HMODULE                   user32 = GetModuleHandleA("user32.dll");
    const uint8_t            *base   = (const uint8_t *)user32;
    const IMAGE_DOS_HEADER   *dos;
    const IMAGE_NT_HEADERS32 *nt;
    const IMAGE_EXPORT_DIRECTORY *exports;
    const uint32_t           *names;
    const uint32_t           *functions;
    const uint16_t           *ordinals;
    DWORD                     directory_size;
    DWORD                     directory_rva;
    uint32_t                  i;

    if (user32 == NULL) {
        return NULL;
    }
    dos = (const IMAGE_DOS_HEADER *)base;
    if (dos->e_magic != IMAGE_DOS_SIGNATURE) {
        return NULL;
    }
    nt = (const IMAGE_NT_HEADERS32 *)(base + dos->e_lfanew);
    if (nt->Signature != IMAGE_NT_SIGNATURE) {
        return NULL;
    }
    directory_rva  = nt->OptionalHeader.DataDirectory[IMAGE_DIRECTORY_ENTRY_EXPORT].VirtualAddress;
    directory_size = nt->OptionalHeader.DataDirectory[IMAGE_DIRECTORY_ENTRY_EXPORT].Size;
    if (directory_rva == 0 || directory_size == 0) {
        return NULL;
    }
    exports   = (const IMAGE_EXPORT_DIRECTORY *)(base + directory_rva);
    names     = (const uint32_t *)(base + exports->AddressOfNames);
    functions = (const uint32_t *)(base + exports->AddressOfFunctions);
    ordinals  = (const uint16_t *)(base + exports->AddressOfNameOrdinals);

    for (i = 0; i < exports->NumberOfNames; ++i) {
        const char *name = (const char *)(base + names[i]);
        DWORD       rva;

        if (strcmp(name, "RegisterRawInputDevices") != 0) {
            continue;
        }
        rva = functions[ordinals[i]];
        /* A forwarded export points back inside the directory and is a string, not code. This one
         * is not forwarded on any build seen, but believing that without checking is how a caller
         * ends up jumping into text. */
        if (rva >= directory_rva && rva < directory_rva + directory_size) {
            return NULL;
        }
        return (register_raw_fn_t)(void *)(base + rva);
    }
    return NULL;
}

static bool register_one(DWORD flags, HWND target, const char *what)
{
    RAWINPUTDEVICE device;

    device.usUsagePage = HID_USAGE_PAGE_GENERIC_DESKTOP;
    device.usUsage     = HID_USAGE_GENERIC_MOUSE;
    device.dwFlags     = flags;
    device.hwndTarget  = target;

    if (RegisterRawInputDevices(&device, 1, (UINT)sizeof(device))) {
        log_info("raw input registered: %s (window %08X)", what, (unsigned)(uintptr_t)target);
        return true;
    }

    {
        DWORD             shimmed_error = GetLastError();
        register_raw_fn_t direct        = resolve_unshimmed();

        /* ERROR_INVALID_PARAMETER on parameters that a standalone process accepts is the signature
         * of the compatibility fix described above, so the real function is worth one attempt.
         * Anything else is a genuine refusal and is reported as one. */
        if (shimmed_error == ERROR_INVALID_PARAMETER && direct != NULL &&
            direct != RegisterRawInputDevices) {
            SetLastError(0);
            if (direct(&device, 1, (UINT)sizeof(device))) {
                log_info("raw input registered: %s, through the unshimmed function. Windows ships "
                         "an application compatibility fix for this executable which intercepts "
                         "RegisterRawInputDevices and fails it, and that is what the refusal above "
                         "was. The real function was resolved out of user32's export table and "
                         "accepted the same parameters.", what);
                return true;
            }
            log_warning("raw input refused %s with error %lu, and the unshimmed function refused "
                        "it too with error %lu", what, (unsigned long)shimmed_error,
                        (unsigned long)GetLastError());
            return false;
        }
        log_warning("raw input refused %s with error %lu", what, (unsigned long)shimmed_error);
    }
    return false;
}

static bool register_ladder(void)
{
    /* INPUTSINK and not NOLEGACY, at every rung. NOLEGACY would suppress the ordinary mouse
     * messages for the whole process, and the engine's menus, its pointer warp and its own device
     * all live on those. */
    if (register_one(RIDEV_INPUTSINK, raw_state.window,
                     "message-only window, background delivery")) {
        return true;
    }
    if (register_one(0, raw_state.window, "message-only window, foreground only")) {
        return true;
    }

    /* A real window, never shown and one pixel across. Created only if the message-only one was
     * refused, so the ordinary case leaves nothing in the system's window lists. */
    raw_state.fallback_window = CreateWindowExA(0, RAW_MOUSE_CLASS, RAW_MOUSE_CLASS, WS_POPUP,
                                                0, 0, 1, 1, NULL, NULL,
                                                GetModuleHandleA(NULL), NULL);
    if (raw_state.fallback_window == NULL) {
        log_warning("the fallback raw input window could not be created (error %lu)",
                    (unsigned long)GetLastError());
        return false;
    }
    if (register_one(RIDEV_INPUTSINK, raw_state.fallback_window,
                     "hidden top-level window, background delivery")) {
        return true;
    }
    if (register_one(0, raw_state.fallback_window, "hidden top-level window, foreground only")) {
        return true;
    }

    log_warning("every raw input registration was refused, so the mouse is read through the "
                "engine's own device. The errors above say which combination failed and how; "
                "please report them, because a standalone probe registers all four on this "
                "machine, so the refusal belongs to this process rather than to Windows.");
    return false;
}

static bool create_window_and_register(void)
{
    WNDCLASSEXA wc;

    ZeroMemory(&wc, sizeof(wc));
    wc.cbSize        = sizeof(wc);
    wc.lpfnWndProc   = raw_mouse_proc;
    wc.hInstance     = GetModuleHandleA(NULL);
    wc.lpszClassName = RAW_MOUSE_CLASS;

    /* A class that is already registered is not an error: this DLL can be loaded twice in a
     * session that reloads, and the class outlives the window. */
    if (RegisterClassExA(&wc) == 0 && GetLastError() != ERROR_CLASS_ALREADY_EXISTS) {
        log_warning("the raw input window class could not be registered (error %lu), the mouse is "
                    "read through the engine's own DirectInput device as before",
                    (unsigned long)GetLastError());
        return false;
    }

    /* HWND_MESSAGE: no screen presence, no activation, no focus, never painted. It exists only to
     * be the target of the registration below. */
    raw_state.window = CreateWindowExA(0, RAW_MOUSE_CLASS, RAW_MOUSE_CLASS, 0, 0, 0, 0, 0,
                                       HWND_MESSAGE, NULL, GetModuleHandleA(NULL), NULL);
    if (raw_state.window == NULL) {
        log_warning("the raw input message window could not be created (error %lu), the mouse is "
                    "read through the engine's own DirectInput device as before",
                    (unsigned long)GetLastError());
        return false;
    }

    return register_ladder();
}

static DWORD WINAPI raw_mouse_thread(LPVOID parameter)
{
    MSG message;

    (void)parameter;

    raw_state.registered = create_window_and_register();
    SetEvent(raw_state.ready);

    if (!raw_state.registered) {
        return 0;
    }

    /* The pump runs for the life of the process. There is no shutdown path and that is deliberate
     * rather than an omission: the only correct moment to stop would be process exit, where the
     * loader is already tearing threads down, and a teardown that races the loader is a crash on
     * the way out of a game that was working. */
    while (GetMessageA(&message, NULL, 0, 0) > 0) {
        TranslateMessage(&message);
        DispatchMessageA(&message);
    }
    return 0;
}

/* ==============================================================================================
 * The game thread
 * ============================================================================================ */
bool raw_mouse_install(void)
{
    DWORD waited;

    if (raw_state.installed) {
        return raw_state.active;
    }
    raw_state.installed = true;

    {
        LARGE_INTEGER frequency;
        QueryPerformanceFrequency(&frequency);
        raw_state.ticks_per_second = frequency.QuadPart;
    }
    /* Initialised before the thread exists, because the hook takes it on its very first packet. */
    InitializeCriticalSection(&raw_state.lock);

    raw_state.ready = CreateEventA(NULL, TRUE, FALSE, NULL);
    if (raw_state.ready == NULL) {
        log_warning("the raw input startup event could not be created, the mouse is read through "
                    "the engine's own DirectInput device as before");
        return false;
    }

    raw_state.thread = CreateThread(NULL, 0, raw_mouse_thread, NULL, 0, NULL);
    if (raw_state.thread == NULL) {
        log_warning("the raw input thread could not be started, the mouse is read through the "
                    "engine's own DirectInput device as before");
        CloseHandle(raw_state.ready);
        raw_state.ready = NULL;
        return false;
    }

    /* The thread is lifted above normal, and the reason is a scheduling one rather than a
     * throughput one. It does almost nothing per packet, one GetRawInputData into a stack buffer
     * and two interlocked adds, and then blocks again; what matters is not how fast it runs but how
     * soon after the packet arrives it runs at all. At normal priority it queues behind every other
     * runnable thread on the machine, and a packet that is accumulated late is not lost, it is
     * counted in the following frame instead of the one it belongs to. That is exactly the one
     * frame either way that the measurement calls the sampling boundary, and it widens with the
     * load on the machine rather than with anything the game is doing.
     *
     * ABOVE_NORMAL rather than HIGHEST or TIME_CRITICAL: at a thousand packets a second this thread
     * preempts the drawing thread a thousand times a second, and each of those is a context switch
     * the game pays for. Above normal is enough to win against ordinary background work, which is
     * what is being defended against, without making the input path a source of jitter in its own
     * right. A failure here is not worth refusing the feature over, so it is logged and ignored. */
    if (!SetThreadPriority(raw_state.thread, THREAD_PRIORITY_ABOVE_NORMAL)) {
        log_warning("the raw input thread stays at normal priority; under heavy load from other "
                    "processes its packets may be accumulated a frame later than they arrived");
    }

    /* Waited for rather than assumed, so that the installation log tells the truth about which
     * reader is live instead of reporting a registration that may still fail a moment later. */
    waited = WaitForSingleObject(raw_state.ready, STARTUP_TIMEOUT_MS);
    if (waited != WAIT_OBJECT_0) {
        log_warning("the raw input thread did not report within %d ms, so the mouse is read "
                    "through the engine's own DirectInput device. The thread is left running and "
                    "harmless.", STARTUP_TIMEOUT_MS);
        return false;
    }

    raw_state.active = raw_state.registered;
    return raw_state.active;
}

unsigned raw_mouse_packet_count(void)
{
    return (unsigned)InterlockedCompareExchange(&raw_state.packets, 0, 0);
}

void raw_mouse_take(raw_mouse_sample_t *out)
{
    LONG     counts  = InterlockedExchange(&raw_state.accumulated_counts, 0);
    LONG     packets = InterlockedExchange(&raw_state.packets_since_take, 0);
    LONGLONG newest;

    EnterCriticalSection(&raw_state.lock);
    newest = raw_state.newest_tick;
    LeaveCriticalSection(&raw_state.lock);

    out->packets = (unsigned)((packets > 0) ? packets : 0);
    out->span_seconds = 0.0f;
    if (out->packets > 0 && raw_state.ticks_per_second > 0) {
        if (raw_state.taken_tick != 0 && newest > raw_state.taken_tick) {
            out->span_seconds = (float)((double)(newest - raw_state.taken_tick)
                                        / (double)raw_state.ticks_per_second);
        }
        raw_state.taken_tick = newest;
    }

    if (raw_state.absolute_seen && !raw_state.warned_absolute) {
        raw_state.warned_absolute = true;
        log_warning("the mouse is reporting ABSOLUTE positions rather than relative counts, which "
                    "is what a remote desktop and the virtual mice that streaming and VR software "
                    "install do. They are differenced here so the view still turns, but the "
                    "resolution is the resolution of that virtual pointer and not of your mouse. "
                    "If the aim feels coarse, the cause is that device rather than this game.");
    }

    /* The unit, and it is read out of the engine rather than chosen.
     *
     * control_applyMouseDefaults at 0x0046586C registers exactly one binding on turn function 0:
     * addBinding(fn = 0, source = 6, flags = 0x0C), and the entry's scale field is written as
     * 0x3E99999A, which is 0.3f. Flags 0x0C is INVERT | RELATIVE. input_axis at 0x004652C3 then
     * answers scale * -raw, the centre for source 6 is provably 0 and the dead zone is 0, so the
     * engine's own reader answers exactly
     *
     *     input_axis(0) == 0.3 * -lX
     *
     * Answering in the same units makes this a drop-in for that reader: nothing downstream changes
     * and a sensitivity the player has already tuned keeps its meaning. The negation is the
     * engine's own INVERT flag on the binding, not a preference. ENGINE_AXIS_SCALE is that 0.3f,
     * and it is declared in a header rather than here because two readers now have to agree on it
     * and a load-bearing constant duplicated across two files is one that rots.
     *
     * The difference that is not cosmetic: a count here is a real device count, where the engine's
     * was a screen pixel of pointer travel. The number the player sets finally means what it
     * says. */
    out->axis = ENGINE_AXIS_SCALE * -(float)counts;
}

/* The cursor's own drain. Independent of the one above: it clears only the cells the packet path
 * adds to for this purpose, so the view turn's accumulator is untouched and both consumers see
 * every packet exactly once.
 *
 * Counts, not pixels, and both axes. The caller decides what a count is worth on screen and keeps
 * the remainder; nothing is rounded here, because rounding is what the engine already gets
 * wrong. */
void raw_mouse_take_cursor(long *out_dx, long *out_dy)
{
    LONG dx = InterlockedExchange(&raw_state.cursor_counts_x, 0);
    LONG dy = InterlockedExchange(&raw_state.cursor_counts_y, 0);

    if (out_dx != NULL) {
        *out_dx = (long)dx;
    }
    if (out_dy != NULL) {
        *out_dy = (long)dy;
    }
}

/* True once the reader is registered and has actually delivered a packet. The cursor path uses it
 * to decide whether it may take the engine's own motion away: a registration that succeeded and
 * then stayed silent would otherwise leave the player with a frozen pointer, which is worse than
 * the stepping this replaces. */
bool raw_mouse_is_delivering(void)
{
    return raw_state.active && raw_state.registered &&
           InterlockedCompareExchange(&raw_state.packets, 0, 0) > 0;
}
