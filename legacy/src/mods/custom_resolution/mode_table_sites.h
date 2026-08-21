/* mode_table_sites.h: the four signatures, and the cross-checks that decide whether to believe
 * them. Kept apart from custom_resolution.c for the same reason player_sites.c and camera_sites.c
 * are kept apart from the files that use what they resolve: this is "where is it", not "what to
 * do with it".
 */
#ifndef MODE_TABLE_SITES_H
#define MODE_TABLE_SITES_H

#include <stdbool.h>
#include <stdint.h>

/* Prologue sizes for the two detour targets above, needed by custom_resolution.c's own
 * detour_install() calls; kept here rather than re-declared there so the number that was measured
 * against the signature and the number handed to the detour can never drift apart. */
#define BUILD_MODE_LIST_PROLOGUE_SIZE 6u
#define SET_RESOLUTION_PROLOGUE_SIZE  6u

/* The aspect gate's own patch: `jmp +0x32` lands exactly where the engine's own accept branch
 * does, identical to enhanced_resolution's own ASPECT_GATE_PATCH. */
extern const uint8_t MODE_TABLE_ASPECT_GATE_PATCH[2];

typedef struct mode_table_sites {
    uintptr_t build_mode_list_site;   /* 0 = unresolved */
    uintptr_t set_resolution_site;    /* 0 = unresolved */
    uintptr_t aspect_gate_site;       /* 0 = unresolved */

    uintptr_t table_address;          /* the raw record table's own base                        */
    uintptr_t count_address;          /* its live record count                                  */
    bool      table_resolved;         /* true only once table_address/count_address are trusted */
} mode_table_sites_t;

/* Resolves every site independently and logs its own outcome for each; a failure in one does not
 * stop the others from being attempted. See mode_table_sites.c for the byte evidence. */
void mode_table_sites_resolve(mode_table_sites_t *out);

#endif /* MODE_TABLE_SITES_H */
