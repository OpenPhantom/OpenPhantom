#include "logging.h"

#include "host_image.h"
#include "text.h"
#include "common/version.h"

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
 * Why this uses WriteFile and not fopen("a").
 *
 * Every module in this project writes to this one file, and they are all in one process. Given a
 * CRT stream each in append mode, every stream keeps its own file position, and two flushes that
 * land in the same instant overwrite each other; one observed line lost its first 46 characters
 * that way, taking the address of a hooked function with it.
 *
 * A handle opened with FILE_APPEND_DATA (and WITHOUT FILE_WRITE_DATA) makes every WriteFile an
 * atomic append at the current end of file. The whole line is therefore formatted into one buffer
 * and written in one call.
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

    text_format(log_state.path, sizeof(log_state.path), "%s%s", host_directory(), LOG_FILE_NAME);

    /* The truncation is a separate open. FILE_APPEND_DATA only means "append" when
     * FILE_WRITE_DATA is ABSENT; with both, the handle keeps an ordinary file pointer. The first
     * attempt gave the loader's handle both, so the loader wrote at its own position while the
     * feature DLLs appended at the end, and the loader's next line overwrote what they had just
     * written. crash_report, crt_copy_fix and diagnostics lost every line they logged, silently,
     * because they happen to install first. */
    /* The previous run is kept. The way this project is used is:
     * play, quit, then read the log. Truncating on every start means a single accidental restart,
     * or a launcher that starts the game twice, erases the session that is being investigated,
     * and the file that is left describes a run in which nothing happened. That has already cost
     * one round of diagnosis on a log whose whole session was four lines long.
     *
     * One generation is enough: the run before last is never the interesting one. */
    if (truncate) {
        HANDLE reset;
        char   previous[MAX_PATH];

        text_format(previous, sizeof(previous), "%s%s", host_directory(), LOG_PREVIOUS_NAME);
        DeleteFileA(previous);
        if (!MoveFileA(log_state.path, previous)) {
            /* Nothing to move on the very first run; any other error means the previous
             * session's log was lost rather than kept. */
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
        size_t length;
        DWORD  written;

        GetLocalTime(&now);
        length = text_format(header, sizeof(header),
                             "OpenPhantom engine fixes %s  %04d-%02d-%02d %02d:%02d:%02d\r\n"
                             "-----------------------------------------------------\r\n",
                             OPENPHANTOM_VERSION,
                             now.wYear, now.wMonth, now.wDay, now.wHour, now.wMinute, now.wSecond);
        WriteFile(log_state.file, header, (DWORD)length, &written, NULL);

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
    char   line[LOG_LINE_MAX];
    size_t length;
    DWORD  written;

    if (log_state.file == NULL) {
        return;
    }

    /* Two bytes are kept back for the line ending. A body longer than the room left is cut, and
     * the cut is marked so a truncated line cannot be read as a complete one. */
    length  = text_format(line, sizeof(line) - 2, "[%s] %s", log_state.feature_name, severity);
    length += text_vformat(line + length, sizeof(line) - 2 - length, format, arguments);
    if (length == sizeof(line) - 3) {
        memcpy(line + length - 3, "...", 3);
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
