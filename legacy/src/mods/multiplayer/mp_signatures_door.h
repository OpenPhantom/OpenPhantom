/* mp_signatures_door.h: the two engine functions a client must not walk through alone. */
#ifndef MP_SIGNATURES_DOOR_H
#define MP_SIGNATURES_DOOR_H

#include <stddef.h>
#include <stdint.h>

typedef enum mp_door_site {
    MP_DOOR_SITE_LOAD_FROM_SCREEN,  /* 0x00451936  broadcast 6, then restore a slot by index */
    MP_DOOR_SITE_DIRECTOR,          /* 0x00429880  the script director's extra functions */
    MP_DOOR_SITE_COUNT
} mp_door_site_t;

/* Resolves once. The prologue of each site is declared with its pattern, because both are hulled
 * by this feature. */
size_t mp_signatures_door_resolve(void);

uintptr_t mp_signatures_door_address(mp_door_site_t site);

size_t mp_signatures_door_prologue(mp_door_site_t site);

#endif /* MP_SIGNATURES_DOOR_H */
