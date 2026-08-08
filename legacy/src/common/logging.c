#include "logging.h"

#include "host_image.h"

#include <windows.h>

#include <stdarg.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdio.h>
#include <string.h>

#define LOG_FILE_NAME     "engine_fixes.log"
#define LOG_PREVIOUS_NAME "engine_fixes.prev.log"
#define LOG_LINE_MAX  1024

/* ==============================================================================================
 * WHY THIS USES WriteFile AND NOT fopen("a").
 *
 * Eleven modules in one process write to this one file. With eleven CRT streams in append mode
 * each stream keeps its own file position, and two flushes that land in the same instant overwrite
 * each other; one observed line lost its first 46 characters that way, taking the address of a
 * hooked function with it.
 *
 * A handle opened with FILE_APPEND_DATA (and WITHOUT FILE_WRITE_DATA) makes every WriteFile an
 * atomic append at the current end of file, which is exactly the guarantee needed here. The whole
 * line is therefore formatted into one buffer and written in one call.
 * ============================================================================================ */
typedef struct log_state {
    HANDLE      file;
    const char *feature_name;
    char        path[MAX_PATH];
} log_state_t;

static log_state_t log_state;

void log_init(const char *feature_name, bool truncate)
{
    SYSTEMTIME now;
    DWORD      rotate_error = 0;

    if (log_state.file != NULL && log_state.file != INVALID_HANDLE_VALUE) {
        return;
    }

    log_state.feature_name = (feature_name != NULL) ? feature_name : "?";

    _snprintf(log_state.path, sizeof(log_state.path), "%s%s", host_directory(), LOG_FILE_NAME);
    log_state.path[sizeof(log_state.path) - 1] = '\0';

    /* The truncation is a separate open, and that is not a detail. FILE_APPEND_DATA only means
     * "append" when FILE_WRITE_DATA is ABSENT; with both, the handle keeps an ordinary file
     * pointer. The first attempt gave the loader's handle both, so the loader wrote at its own
     * position while the feature DLLs appended at the end, and the loader's next line overwrote
     * what they had just written. crash_report, crt_copy_fix and diagnostics lost every line they
     * logged, silently, because they happen to install first. */
    /* The previous run is kept, and that is not a nicety either. The way this project is used is:
     * play, quit, then read the log. Truncating on every start means a single accidental restart,
     * or a launcher that starts the game twice, erases the session that is being investigated,
     * and the file that is left describes a run in which nothing happened. That has already cost
     * one round of diagnosis on a log whose whole session was four lines long.
     *
     * One generation is enough: the run before last is never the interesting one. */
    if (truncate) {
        HANDLE reset;
        char   previous[MAX_PATH];

        _snprintf(previous, sizeof(previous), "%s%s", host_directory(), LOG_PREVIOUS_NAME);
        previous[sizeof(previous) - 1] = '\0';
        DeleteFileA(previous);
        if (!MoveFileA(log_state.path, previous)) {
            /* Nothing to move on the very first run; anything else is worth knowing, because it
             * means the previous session's log was lost rather than kept. */
            DWORD error = GetLastError();
            if (error != ERROR_FILE_NOT_FOUND && error != ERROR_PATH_NOT_FOUND) {
                rotate_error = error;
            }
        }

        reset = CreateFileA(log_state.path, GENERIC_WRITE,
                            FILE_SHARE_READ | FILE_SHARE_WRITE, NULL,
                            CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, NULL);
        if (reset != INVALID_HANDLE_VALUE) {
            CloseHandle(reset);
        }
    }

    log_state.file = CreateFileA(log_state.path, FILE_APPEND_DATA,
                                 FILE_SHARE_READ | FILE_SHARE_WRITE, NULL,
                                 OPEN_ALWAYS, FILE_ATTRIBUTE_NORMAL, NULL);
    if (log_state.file == INVALID_HANDLE_VALUE) {
        log_state.file = NULL;
        return;
    }

    if (truncate) {
        char   header[256];
        int    length;
        DWORD  written;

        GetLocalTime(&now);
        length = _snprintf(header, sizeof(header),
                           "OpenPhantom engine fixes  %04d-%02d-%02d %02d:%02d:%02d\r\n"
                           "-----------------------------------------------------\r\n",
                           now.wYear, now.wMonth, now.wDay, now.wHour, now.wMinute, now.wSecond);
        if (length > 0) {
            WriteFile(log_state.file, header, (DWORD)length, &written, NULL);
        }

        /* Reported only now, because there was nowhere to report it before the handle existed. */
        if (rotate_error != 0) {
            log_warning("the previous log could not be kept (error %lu), it was overwritten",
                        (unsigned long)rotate_error);
        }
    }
}

void log_shutdown(void)
{
    if (log_state.file == NULL) {
        return;
    }
    CloseHandle(log_state.file);
    log_state.file = NULL;
}

static void write_line(const char *severity, const char *format, va_list arguments)
{
    char  line[LOG_LINE_MAX];
    int   length;
    DWORD written;

    if (log_state.file == NULL) {
        return;
    }

    length = _snprintf(line, sizeof(line) - 3, "[%s] %s", log_state.feature_name, severity);
    if (length < 0) {
        length = (int)sizeof(line) - 3;
    }

    {
        int body = _vsnprintf(line + length, sizeof(line) - 3 - (size_t)length, format, arguments);
        if (body < 0) {
            body = (int)(sizeof(line) - 3 - (size_t)length);
        }
        length += body;
    }

    line[length++] = '\r';
    line[length++] = '\n';

    /* One call, one atomic append: the line cannot interleave with another module's. */
    WriteFile(log_state.file, line, (DWORD)length, &written, NULL);
}

void log_info(const char *format, ...)
{
    va_list arguments;
    va_start(arguments, format);
    write_line("", format, arguments);
    va_end(arguments);
}

void log_warning(const char *format, ...)
{
    va_list arguments;
    va_start(arguments, format);
    write_line("WARNING: ", format, arguments);
    va_end(arguments);
}

void log_error(const char *format, ...)
{
    va_list arguments;
    va_start(arguments, format);
    write_line("ERROR: ", format, arguments);
    va_end(arguments);
}

const char *log_path(void)
{
    return log_state.path;
}
