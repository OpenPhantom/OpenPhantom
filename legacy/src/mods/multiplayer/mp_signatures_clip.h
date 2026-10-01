/* mp_signatures_clip.h: the engine's dispatcher of a clip's own events, as a byte pattern. */
#ifndef MULTIPLAYER_MP_SIGNATURES_CLIP_H
#define MULTIPLAYER_MP_SIGNATURES_CLIP_H

#include <stdint.h>

typedef enum mp_clip_site {
    MP_CLIP_SITE_DISPATCH_EVENTS,   /* 0x00411897  fires a track's clip events over a span */
    MP_CLIP_SITE_COUNT
} mp_clip_site_t;

/* The address, resolved on the first call; 0 when the pattern matched nothing. */
uintptr_t mp_signatures_clip_address(mp_clip_site_t site);

#endif /* MULTIPLAYER_MP_SIGNATURES_CLIP_H */
