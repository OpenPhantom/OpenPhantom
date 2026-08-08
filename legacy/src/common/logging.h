/* logging.h: one shared log file, one prefix per DLL.
 *
 * Every module in the process appends to <game>\engine_fixes.log. The prefix is set once by
 * log_init() and is written in front of every line automatically, so a call site cannot forget it
 * and two DLLs cannot drift apart in how they spell their own name:
 *
 *     [variable_fov] hooked rdCamera_BuildProjection at 00475FFA
 *     [view_distance_fix] WARNING: gather_append did not match, no cell watchdog
 *
 * The loader calls log_init("loader", true) first and truncates; every feature DLL calls
 * log_init("<feature>", false) and appends. Each line is flushed, because the interesting case is
 * the one where the process dies immediately afterwards.
 */
#ifndef COMMON_LOGGING_H
#define COMMON_LOGGING_H

#include <stdbool.h>

/* `feature_name` must be a string literal or otherwise outlive the process. */
void log_init(const char *feature_name, bool truncate);
void log_shutdown(void);

void log_info(const char *format, ...);
void log_warning(const char *format, ...);
void log_error(const char *format, ...);

/* Full path of the log file, for messages that want to name it. Empty before log_init(). */
const char *log_path(void);

#endif /* COMMON_LOGGING_H */
