/* power_throttling.c: see power_throttling.h. */
#include "power_throttling.h"

#include "common/logging.h"
#include "common/text.h"

#include <windows.h>

#include <stdbool.h>
#include <string.h>

/* Named here for an SDK older than Windows 11's, which lacks the timer switch. The values are the
 * ones Windows defines; a system that does not know the timer switch refuses it, which is handled
 * below rather than assumed away. */
#ifndef PROCESS_POWER_THROTTLING_IGNORE_TIMER_RESOLUTION
#define PROCESS_POWER_THROTTLING_IGNORE_TIMER_RESOLUTION 0x4
#endif

typedef BOOL (WINAPI *process_information_fn_t)(HANDLE process,
                                                PROCESS_INFORMATION_CLASS information_class,
                                                LPVOID information, DWORD size);

const char *power_throttling_state_name(unsigned long control_mask, unsigned long state_mask,
                                        unsigned long bit)
{
    if ((control_mask & bit) == 0ul) {
        return "left to Windows";
    }
    return ((state_mask & bit) != 0ul) ? "held on" : "held off";
}

/* The switches in `control`, each held off: a clear state bit under a set control bit is how a
 * process says "never throttle this". */
static bool hold_off(process_information_fn_t set_information, ULONG control)
{
    PROCESS_POWER_THROTTLING_STATE state;

    memset(&state, 0, sizeof(state));
    state.Version     = PROCESS_POWER_THROTTLING_CURRENT_VERSION;
    state.ControlMask = control;
    state.StateMask   = 0u;
    return set_information(GetCurrentProcess(), ProcessPowerThrottling, &state,
                           (DWORD)sizeof(state)) != FALSE;
}

static void apply_high_qos(process_information_fn_t set_information)
{
    DWORD refused;

    if (set_information == NULL) {
        log_warning("HighQos=1, but this Windows has no SetProcessInformation, which came with "
                    "Windows 8, so power throttling is left to it");
        return;
    }
    if (hold_off(set_information, PROCESS_POWER_THROTTLING_EXECUTION_SPEED |
                                  PROCESS_POWER_THROTTLING_IGNORE_TIMER_RESOLUTION)) {
        log_info("HighQos=1: Windows is asked never to slow this process down or to coarsen its "
                 "timers while its window is behind another. Nothing about how the game plays "
                 "changes; what goes is Windows' option to treat a game behind another window as "
                 "background work");
        return;
    }
    refused = GetLastError();

    /* Windows 10 knows the execution speed switch and not the timer one, and refuses the pair
     * whole, so the first switch is asked for on its own. */
    if (hold_off(set_information, PROCESS_POWER_THROTTLING_EXECUTION_SPEED)) {
        log_info("HighQos=1: Windows holds off slowing this process down behind another window, "
                 "and refused the timer switch with it (error %lu), which it only knows from "
                 "Windows 11 on, so the timers are left to it", (unsigned long)refused);
        return;
    }
    log_warning("HighQos=1, but Windows refused both switches (error %lu, then %lu), so power "
                "throttling is left to it", (unsigned long)refused, (unsigned long)GetLastError());
}

static void report_state(process_information_fn_t get_information)
{
    PROCESS_POWER_THROTTLING_STATE state;
    char                           speed[40];
    char                           timer[40];

    memset(&state, 0, sizeof(state));
    state.Version = PROCESS_POWER_THROTTLING_CURRENT_VERSION;

    if (get_information == NULL ||
        !get_information(GetCurrentProcess(), ProcessPowerThrottling, &state,
                         (DWORD)sizeof(state))) {
        unsigned long error = (get_information == NULL) ? (unsigned long)ERROR_PROC_NOT_FOUND
                                                        : (unsigned long)GetLastError();

        (void)text_format(speed, sizeof(speed), "not readable (error %lu)", error);
        (void)text_format(timer, sizeof(timer), "not readable (error %lu)", error);
    } else {
        (void)text_format(speed, sizeof(speed), "%s",
                          power_throttling_state_name(state.ControlMask, state.StateMask,
                                                      PROCESS_POWER_THROTTLING_EXECUTION_SPEED));
        (void)text_format(timer, sizeof(timer), "%s",
                          power_throttling_state_name(
                              state.ControlMask, state.StateMask,
                              PROCESS_POWER_THROTTLING_IGNORE_TIMER_RESOLUTION));
    }
    log_info("this process's power throttling at start: execution speed %s, timer resolution %s",
             speed, timer);
}

void power_throttling_apply(bool high_qos)
{
    HMODULE                  kernel32        = GetModuleHandleA("kernel32.dll");
    process_information_fn_t set_information = NULL;
    process_information_fn_t get_information = NULL;

    /* Looked up rather than linked, so a Windows without them loses this line and not the DLL. */
    if (kernel32 != NULL) {
        set_information = (process_information_fn_t)GetProcAddress(kernel32,
                                                                   "SetProcessInformation");
        get_information = (process_information_fn_t)GetProcAddress(kernel32,
                                                                   "GetProcessInformation");
    }

    if (high_qos) {
        apply_high_qos(set_information);
    } else {
        log_info("HighQos=0, so Windows may slow this process down and coarsen its timers while "
                 "its window is behind another");
    }
    report_state(get_information);
}
