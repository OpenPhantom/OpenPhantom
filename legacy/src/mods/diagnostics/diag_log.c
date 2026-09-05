#include "diag_log.h"

#include "common/host_image.h"
#include "common/logging.h"

#include <windows.h>
#include <mmsystem.h>

#include <stdarg.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdio.h>
#include <string.h>

#define DIAG_LOG_FILE_NAME "engine_fixes_diag.log"
#define DIAG_LINE_MAX      512
#define MILLISECONDS_PER_SECOND 1000u

typedef struct diag_log_state {
    FILE  *file;
    DWORD  started_at;
    bool   also_to_main_log;

    int    max_lines_per_second;
    DWORD  bucket_second;
    int    budget;
    int    dropped;

    char   last_line[DIAG_LINE_MAX];
    int    repeat_count;
} diag_log_state_t;

static diag_log_state_t diag_state;

static void write_raw(const char *line)
{
    DWORD elapsed = timeGetTime() - diag_state.started_at;

    if (diag_state.file != NULL) {
        fprintf(diag_state.file, "[%4u.%03u] %s\n",
                elapsed / MILLISECONDS_PER_SECOND, elapsed % MILLISECONDS_PER_SECOND, line);
        fflush(diag_state.file);
    }
    if (diag_state.also_to_main_log) {
        log_info("%s", line);
    }
}

static void flush_repeat(void)
{
    char note[64];

    if (diag_state.repeat_count <= 0) {
        return;
    }
    _snprintf(note, sizeof(note), "     (previous line x%d)", diag_state.repeat_count + 1);
    note[sizeof(note) - 1] = '\0';
    diag_state.repeat_count = 0;
    write_raw(note);
}

bool diag_log_open(int max_lines_per_second, bool also_to_main_log)
{
    char       path[MAX_PATH];
    SYSTEMTIME now;

    diag_state.max_lines_per_second = (max_lines_per_second > 0) ? max_lines_per_second : 0;
    diag_state.also_to_main_log     = also_to_main_log;
    diag_state.started_at           = timeGetTime();

    _snprintf(path, sizeof(path), "%s%s", host_directory(), DIAG_LOG_FILE_NAME);
    path[sizeof(path) - 1] = '\0';

    diag_state.file = fopen(path, "w");
    if (diag_state.file == NULL) {
        log_error("could not open %s, diagnostics stay OFF", path);
        return false;
    }

    GetLocalTime(&now);
    fprintf(diag_state.file,
            "OpenPhantom engine fixes / diagnostics  %04d-%02d-%02d %02d:%02d:%02d\n"
            "Times are seconds since the diagnostics started. Every line is an OBSERVATION;\n"
            "no hook in this DLL changes the game.\n"
            "-------------------------------------------------------------------------------\n",
            now.wYear, now.wMonth, now.wDay, now.wHour, now.wMinute, now.wSecond);
    fflush(diag_state.file);

    log_info("diagnostic log -> %s", path);
    return true;
}

void diag_log_write(const char *format, ...)
{
    char    line[DIAG_LINE_MAX];
    va_list arguments;
    DWORD   second;

    if (diag_state.file == NULL && !diag_state.also_to_main_log) {
        return;
    }

    va_start(arguments, format);
    _vsnprintf(line, sizeof(line), format, arguments);
    va_end(arguments);
    line[sizeof(line) - 1] = '\0';

    /* (a) collapse identical lines, the most effective brake, and it loses nothing but the
     *     repetition itself, whose count it hands in. */
    if (strcmp(line, diag_state.last_line) == 0) {
        ++diag_state.repeat_count;
        return;
    }
    flush_repeat();
    strncpy(diag_state.last_line, line, sizeof(diag_state.last_line) - 1);
    diag_state.last_line[sizeof(diag_state.last_line) - 1] = '\0';

    /* (b) the per-second token bucket. */
    if (diag_state.max_lines_per_second > 0) {
        second = (timeGetTime() - diag_state.started_at) / MILLISECONDS_PER_SECOND;
        if (second != diag_state.bucket_second) {
            diag_state.bucket_second = second;
            diag_state.budget = diag_state.max_lines_per_second;

            if (diag_state.dropped != 0) {
                char note[96];
                _snprintf(note, sizeof(note),
                          "     ... %d lines suppressed (MaxLinesPerSecond=%d)",
                          diag_state.dropped, diag_state.max_lines_per_second);
                note[sizeof(note) - 1] = '\0';
                diag_state.dropped = 0;
                write_raw(note);
            }
        }
        if (diag_state.budget <= 0) {
            ++diag_state.dropped;
            return;
        }
        --diag_state.budget;
    }

    write_raw(line);
}
