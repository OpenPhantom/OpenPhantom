/* diag_core_class.c: see diag_core_class.h. */
#include "diag_core_class.h"

#include "common/logging.h"

#include <windows.h>

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <string.h>

/* One entry per logical processor the map can hold. */
#define MAX_ENTRIES (DIAG_CORE_GROUPS * 64u)

/* GetSystemCpuSetInformation came with Windows 10, so it is looked up rather than linked: an older
 * system loses this count and keeps the DLL. */
typedef BOOL (WINAPI *cpu_set_information_fn_t)(PSYSTEM_CPU_SET_INFORMATION information,
                                                ULONG buffer_length, PULONG returned_length,
                                                HANDLE process, ULONG flags);

static diag_core_map_t core_map;
static bool            core_map_live;

void diag_core_map_build(diag_core_map_t *map, const diag_core_entry_t *entries, unsigned count)
{
    uint8_t  top = 0u;
    unsigned index;

    memset(map, 0, sizeof(*map));
    if (entries == NULL) {
        return;
    }

    for (index = 0u; index < count; ++index) {
        if (entries[index].efficiency_class > top) {
            top = entries[index].efficiency_class;
        }
    }
    for (index = 0u; index < count; ++index) {
        const diag_core_entry_t *entry = &entries[index];

        ++map->processor_count;
        if (entry->efficiency_class >= top) {
            continue;          /* the top class, or the only one there is */
        }
        if (entry->group >= DIAG_CORE_GROUPS || entry->number >= 64u) {
            continue;          /* outside the map, and so never called an efficiency core */
        }
        map->efficient[entry->group] |= (uint64_t)1u << entry->number;
        ++map->efficient_count;
    }
}

bool diag_core_map_is_efficient(const diag_core_map_t *map, uint16_t group, uint8_t number)
{
    if (map == NULL || group >= DIAG_CORE_GROUPS || number >= 64u) {
        return false;
    }
    return ((map->efficient[group] >> number) & 1u) != 0u;
}

/* Walks the records Windows returned. Each carries its own size, so a later Windows that makes the
 * record longer is walked correctly; a record of another type is skipped. */
static unsigned read_entries(const uint8_t *buffer, ULONG length, diag_core_entry_t *entries)
{
    unsigned count  = 0u;
    ULONG    offset = 0u;

    while (offset + (ULONG)offsetof(SYSTEM_CPU_SET_INFORMATION, CpuSet) < length &&
           count < MAX_ENTRIES) {
        const SYSTEM_CPU_SET_INFORMATION *record =
            (const SYSTEM_CPU_SET_INFORMATION *)(buffer + offset);

        if (record->Size == 0u || offset + record->Size > length) {
            break;
        }
        if (record->Type == CpuSetInformation) {
            entries[count].group            = record->CpuSet.Group;
            entries[count].number           = record->CpuSet.LogicalProcessorIndex;
            entries[count].efficiency_class = record->CpuSet.EfficiencyClass;
            ++count;
        }
        offset += record->Size;
    }
    return count;
}

bool diag_core_class_init(void)
{
    static diag_core_entry_t entries[MAX_ENTRIES];
    HMODULE                  kernel32 = GetModuleHandleA("kernel32.dll");
    cpu_set_information_fn_t query    = NULL;
    ULONG                    length   = 0u;
    uint8_t                 *buffer;
    unsigned                 count;

    if (kernel32 != NULL) {
        query = (cpu_set_information_fn_t)GetProcAddress(kernel32, "GetSystemCpuSetInformation");
    }
    if (query == NULL) {
        log_warning("this Windows gives no list of its processors' efficiency classes, which came "
                    "with Windows 10, so the frame line counts no frame as begun on an efficiency "
                    "core");
        return false;
    }

    /* The first call only asks for the size and fails by design. */
    (void)query(NULL, 0u, &length, GetCurrentProcess(), 0u);
    buffer = (length > 0u) ? (uint8_t *)HeapAlloc(GetProcessHeap(), 0u, length) : NULL;
    if (buffer == NULL || !query((PSYSTEM_CPU_SET_INFORMATION)buffer, length, &length,
                                 GetCurrentProcess(), 0u)) {
        log_warning("the list of this machine's processors could not be read (error %lu), so the "
                    "frame line counts no frame as begun on an efficiency core",
                    (unsigned long)GetLastError());
        if (buffer != NULL) {
            (void)HeapFree(GetProcessHeap(), 0u, buffer);
        }
        return false;
    }
    count = read_entries(buffer, length, entries);
    (void)HeapFree(GetProcessHeap(), 0u, buffer);

    diag_core_map_build(&core_map, entries, count);
    core_map_live = true;
    log_info("the frame line counts the frames that began on an efficiency core, read on the game "
             "thread every frame: %u of this machine's %u logical processor(s) are in a lower "
             "efficiency class than the rest",
             core_map.efficient_count, core_map.processor_count);
    return true;
}

bool diag_core_class_efficient_now(void)
{
    PROCESSOR_NUMBER processor;

    /* A machine with no efficiency cores answers without asking where the thread is. */
    if (!core_map_live || core_map.efficient_count == 0u) {
        return false;
    }
    GetCurrentProcessorNumberEx(&processor);
    return diag_core_map_is_efficient(&core_map, processor.Group, processor.Number);
}
