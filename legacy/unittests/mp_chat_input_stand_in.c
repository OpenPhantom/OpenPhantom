/* unittests/mp_chat_input_stand_in.c: the engine and the modules the chat's input line runs
 * against. See the header. */
#include "mp_chat_input_stand_in.h"

#include "mp_cells.h"
#include "mp_input.h"
#include "mp_signatures_chat.h"
#include "mp_signatures_pause.h"
#include "mp_wallclock.h"

#include "common/detour.h"
#include "common/ini.h"
#include "common/logging.h"
#include "common/signature.h"
#include "common/text.h"

#include <stdarg.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

stand_in_t engine;

/* ==============================================================================================
 * The key handler, the cells, and the modules the chat asks.
 * ============================================================================================ */

/* The handlers further in: the developer menu's and the engine's own. */
static int32_t __cdecl further_in(uint32_t window, int32_t message, int32_t wparam,
                                  uint32_t lparam)
{
    (void)window;
    (void)wparam;
    (void)lparam;
    ++engine.further_in_calls;
    engine.further_in_message = (uint32_t)message;
    return engine.further_in_answer;
}

bool detour_install(detour_t *detour, uintptr_t target, const void *hook, size_t prologue_size)
{
    memset(detour, 0, sizeof *detour);
    detour->target        = target;
    detour->original      = (void *)(uintptr_t)&further_in;
    detour->prologue_size = prologue_size;
    detour->installed     = true;
    engine.hook           = (stand_in_key_hook_t)(uintptr_t)hook;
    engine.prologue       = prologue_size;
    return true;
}

static signature_t key_handler = { "chat_key_hook", NULL, NULL, 0u, 6u, STAND_IN_KEY_HANDLER };

size_t mp_signatures_chat_resolve(void)
{
    return 1u;
}

uintptr_t mp_signatures_chat_address(mp_chat_site_t site)
{
    return site == MP_CHAT_SITE_KEY_HOOK ? STAND_IN_KEY_HANDLER : 0u;
}

size_t mp_signatures_chat_prologue(mp_chat_site_t site)
{
    return site == MP_CHAT_SITE_KEY_HOOK ? key_handler.detour_prologue : 0u;
}

const signature_t *mp_signatures_chat_site(mp_chat_site_t site)
{
    return site == MP_CHAT_SITE_KEY_HOOK ? &key_handler : NULL;
}

/* The operand of the handler's first test, which names the movie cell. */
bool signature_read_address_operand(const signature_t *site, size_t offset, uintptr_t *address)
{
    if (engine.refuse_operand || site != &key_handler || offset != MP_CHAT_KEY_HOOK_MOVIE_CELL) {
        return false;
    }
    *address = (uintptr_t)&engine.movie;
    return true;
}

uintptr_t mp_cells_address(mp_cell_t cell)
{
    switch (cell) {
    case MP_CELL_LEVEL_OUTCOME: return (uintptr_t)&engine.outcome;
    case MP_CELL_CURRENT_MENU:  return (uintptr_t)&engine.menu;
    default:                    return 0u;
    }
}

size_t mp_signatures_pause_resolve(void)
{
    return 1u;
}

uintptr_t mp_signatures_pause_cell(mp_pause_cell_t cell)
{
    return cell == MP_PAUSE_CELL_SIM_GATE ? (uintptr_t)&engine.gate : 0u;
}

bool ini_read_string(const char *section, const char *key, const char *default_value,
                     char *buffer, size_t buffer_size)
{
    const char *value = NULL;

    (void)section;
    if (strcmp(key, "ChatKey") == 0) {
        value = engine.chat_key;
    } else if (strcmp(key, "ScoreboardKey") == 0) {
        value = engine.board_key;
    }
    text_format(buffer, buffer_size, "%s", value != NULL ? value : default_value);
    return value != NULL;
}

bool mp_input_installed(void)
{
    return engine.input_split;
}

uint32_t mp_wallclock_ms(void)
{
    return engine.now_ms;
}

mp_chat_say_result_t mp_chat_say(const char *text, size_t length, uint32_t now_ms)
{
    const size_t kept = length < MP_CHAT_TEXT_MAX ? length : MP_CHAT_TEXT_MAX;

    (void)now_ms;
    ++engine.says;
    memcpy(engine.said, text, kept);
    engine.said[kept] = '\0';
    return engine.say;
}

/* ==============================================================================================
 * The log, kept in the stand-in. A line past the last place overwrites the oldest.
 * ============================================================================================ */

#define REPORT_HEAD "the chat keys: the key is "

/* The report's line into its numbers: the key's name, then every number after it in order. */
static void read_the_report(const char *line)
{
    counts_t   *c    = &engine.report;
    const char *name = line + strlen(REPORT_HEAD);
    const char *at   = strstr(line, " (ChatKey)");
    size_t      i    = 0;

    memset(c, 0, sizeof *c);
    if (at == NULL || (size_t)(at - name) >= sizeof c->key) {
        return;
    }
    memcpy(c->key, name, (size_t)(at - name));
    while (*at != '\0' && i < C_COUNT) {
        if (*at >= '0' && *at <= '9') {
            char *end;

            c->value[i++] = strtoul(at, &end, 10);
            at            = end;
        } else {
            ++at;
        }
    }
    c->read = i == C_COUNT;
}

static void keep(const char *format, va_list arguments)
{
    char *line = engine.log[engine.log_count % STAND_IN_LINES];

    (void)text_vformat(line, STAND_IN_LINE_BYTES, format, arguments);
    ++engine.log_count;
    if (strncmp(line, REPORT_HEAD, strlen(REPORT_HEAD)) == 0) {
        read_the_report(line);
    }
}

void log_init(const char *feature_name, bool truncate)
{
    (void)feature_name;
    (void)truncate;
}

void log_shutdown(void)
{
}

void log_info(const char *format, ...)
{
    va_list arguments;

    va_start(arguments, format);
    keep(format, arguments);
    va_end(arguments);
}

void log_warning(const char *format, ...)
{
    va_list arguments;

    va_start(arguments, format);
    keep(format, arguments);
    va_end(arguments);
}

void log_error(const char *format, ...)
{
    va_list arguments;

    va_start(arguments, format);
    keep(format, arguments);
    va_end(arguments);
}

const char *log_path(void)
{
    return "";
}
