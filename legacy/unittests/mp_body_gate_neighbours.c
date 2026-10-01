/* mp_body_gate_neighbours.c: the modules around the fan test's real ones, answered as a machine
 * with one far body, no ticked bank and nothing to draw; and the log, kept rather than written, so
 * a check in mp_body_gate.c can count the lines since a mark that say two things at once. */
#include "mp_armed.h"
#include "mp_bank.h"
#include "mp_body.h"
#include "mp_body_asset.h"
#include "mp_body_internal.h"
#include "mp_effects.h"
#include "mp_phases.h"
#include "mp_rules.h"

#include "common/logging.h"
#include "common/text.h"

#include <stdarg.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

/* ---- the log --------------------------------------------------------------------------------- */

#define LINES_KEPT 512u
#define LINE_BYTES 1024u

static char   kept[LINES_KEPT][LINE_BYTES];
static size_t kept_count;
static size_t mark;

static void keep(const char *level, const char *format, va_list arguments)
{
    char *line = kept[kept_count % LINES_KEPT];

    (void)text_vformat(line, LINE_BYTES, format, arguments);
    ++kept_count;
    printf("      [%s] %s\n", level, line);
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
    keep("info", format, arguments);
    va_end(arguments);
}

void log_warning(const char *format, ...)
{
    va_list arguments;

    va_start(arguments, format);
    keep("warning", format, arguments);
    va_end(arguments);
}

void log_error(const char *format, ...)
{
    va_list arguments;

    va_start(arguments, format);
    keep("error", format, arguments);
    va_end(arguments);
}

void gate_log_mark(void)
{
    mark = kept_count;
}

size_t gate_log_count(const char *first, const char *second)
{
    size_t found = 0u;
    size_t at = kept_count > LINES_KEPT && mark < kept_count - LINES_KEPT ? kept_count - LINES_KEPT
                                                                         : mark;

    for (; at < kept_count; ++at) {
        const char *line = kept[at % LINES_KEPT];

        if (strstr(line, first) != NULL && (second == NULL || strstr(line, second) != NULL)) {
            ++found;
        }
    }
    return found;
}

/* ---- the rule set and the session ------------------------------------------------------------ */

uint32_t mp_rules_respawn_substeps(const mp_rules_t *rules)
{
    (void)rules;
    return 0u;
}

bool mp_armed_transport(void)
{
    return true;
}

/* ---- the banks: bank 0 is the one active, and none is ever ticked ---------------------------- */

bool mp_bank_index_ok(size_t index)
{
    return index >= 1u && index <= MP_BANK_FAR_MAX;
}

int32_t mp_bank_class_of(size_t index)
{
    return index == 0u ? 1 : (int32_t)(4u + index);
}

size_t mp_bank_active(void)
{
    return 0u;
}

int32_t mp_bank_active_class(void)
{
    return 1;
}

bool mp_bank_swap_in_persistent_at(size_t index)
{
    (void)index;
    return false;
}

bool mp_bank_swap_out(void)
{
    return true;
}

int32_t mp_bank_health_at(size_t index)
{
    (void)index;
    return 100;
}

bool mp_bank_run_at(size_t index, void (*run_phases)(void))
{
    (void)index;
    (void)run_phases;
    return false;
}

bool mp_bank_run_second(void (*run_phases)(void))
{
    (void)run_phases;
    return false;
}

int32_t mp_bank_second_health(void)
{
    return 100;
}

/* ---- the phase loop, the effects, the shot hull, the assets and the spawn: none is here ------ */

bool mp_phases_install(void)
{
    return false;
}

void __cdecl mp_phases_run_second(void)
{
}

bool mp_phases_dead_at(size_t index)
{
    (void)index;
    return false;
}

void mp_phases_note_second_revived(void)
{
}

uint32_t mp_phases_second_faults(void)
{
    return 0u;
}

void mp_effects_note_contact(size_t bank, uint32_t receiver_object)
{
    (void)bank;
    (void)receiver_object;
}

void mp_effects_note_hurt(size_t bank)
{
    (void)bank;
}

bool mp_body_shot_install(uintptr_t site, size_t prologue)
{
    (void)site;
    (void)prologue;
    return true;
}

uint32_t mp_body_shot_side_faults(void)
{
    return 0u;
}

void mp_body_shot_report(void)
{
}

bool mp_body_asset_install(void)
{
    return true;
}

bool mp_body_asset_ready(void)
{
    return true;
}

void mp_body_asset_report(void)
{
}

void mp_body_spawn_at(size_t index)
{
    (void)index;
}
