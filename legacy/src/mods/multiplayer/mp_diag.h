/* mp_diag.h: what the sites this feature stands on actually look like in the running process.
 *
 * Every detour in this tree rests on one assumption that nothing has ever checked: that a site
 * somebody else has hooked begins with 0xE9. The chaining reads the displacement there and keeps
 * the old target; the resolver's second stage accepts a head that is either the authored prologue
 * or an 0xE9. A module that hooks by any other shape, a hot patch pair, a push and return, a
 * breakpoint, satisfies neither, and the failure is silent in both places: the chain would treat
 * foreign bytes as a prologue to copy, and the resolver would refuse a site that is perfectly
 * findable.
 *
 * So this reads the first eight bytes at every resolved site once, with the full set of mods
 * loaded, and says which of the three shapes each one is. It writes nothing.
 */
#ifndef MULTIPLAYER_MP_DIAG_H
#define MULTIPLAYER_MP_DIAG_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

typedef enum mp_site_shape {
    MP_SHAPE_UNREADABLE,   /* the eight bytes could not be read at all */
    MP_SHAPE_AUTHORED,     /* still the bytes the pattern was cut from */
    MP_SHAPE_JUMP,         /* 0xE9, which is what this tree's chaining understands */
    MP_SHAPE_FOREIGN       /* neither, and that is the finding this exists for */
} mp_site_shape_t;

/* Reads and reports every site once. Further calls do nothing, because the answer is a property of
 * the load order and the load order is settled by the time the first frame is drawn. */
void mp_diag_report_hook_shapes(void);

bool mp_diag_has_reported(void);

/* How many sites came back in each shape, after the report has run. */
uint32_t mp_diag_shape_count(mp_site_shape_t shape);

#endif /* MULTIPLAYER_MP_DIAG_H */
