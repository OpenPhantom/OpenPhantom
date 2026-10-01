/* fog_owner.h: whose fog the device shows, the level's or the effects', and the one way this
 * module puts its band there.
 *
 * The device's fog has two writers while a level runs. This module eases a band and pushes it.
 * The engine's effects set a colour and a band of their own, the gas room's pale green among them,
 * and walk the band substep by substep while a ramp runs. The push used to go through the level's
 * fog apply, which writes the colour out of the level record as well as the band, so every push
 * while a room was green put the level's colour back, and the green was gone in the first frame
 * this module's band moved. The field of view and the cut edge move that band, so it happened on
 * every machine sooner or later, in single player too.
 *
 * The owner is the colour. Only the colour setter writes the device's three colour cells: the
 * level's fog apply writes the level's own colour, the effects write theirs, and no shipped level
 * has the gas room's colour as its own. So while the device's colour is not the level's, the
 * effects hold the fog and this module pushes nothing. The level's fog apply ends that: the
 * effects' restore calls it, and so does every level load.
 *
 * One gap the colour cannot see. The gas room sets its band a substep before its colour, so for
 * that one substep the device carries the effects' band under the level's colour. A band on the
 * device that is not the one this module last put there, or the one the level's fog apply last
 * gave it, is therefore honoured for one engine substep. If no colour follows, this module's band
 * goes over it as before. That covers the room's start and every way back in. A script that moves
 * only the band, as the ones opening GUNGA and RACE do, keeps it for that substep.
 *
 * The push writes the two band cells and commits the render state around them, the pattern the
 * engine's own fog ramp uses: the fog enable bit off and committed, the band, then the flags as
 * they were and committed again. It never writes the colour.
 */
#ifndef VIEW_DISTANCE_FIX_FOG_OWNER_H
#define VIEW_DISTANCE_FIX_FOG_OWNER_H

#include "fog_regime.h"

#include <stdbool.h>
#include <stdint.h>

/* The three engine functions the band push calls, read out of the level's fog apply. The flags
 * setter takes one dword and returns through the full render state commit; the range setter takes
 * the two floats as their bits, as the level's fog apply hands them over with a plain mov. */
typedef uint32_t (__cdecl *fog_get_render_flags_fn_t)(void);
typedef void     (__cdecl *fog_set_render_flags_fn_t)(uint32_t flags);
typedef void     (__cdecl *fog_set_range_fn_t)(uint32_t start_bits, uint32_t end_bits);

typedef struct fog_device_writes {
    fog_get_render_flags_fn_t get_render_flags;
    fog_set_render_flags_fn_t set_render_flags;
    fog_set_range_fn_t        set_fog_range;
} fog_device_writes_t;

/* The level's fog apply, the push used when the three functions above did not bind. */
typedef void (__cdecl *fog_apply_fn_t)(void *level);

typedef enum fog_holder {
    FOG_HELD_BY_LEVEL = 0,    /* the band on the device is this module's to push */
    FOG_HELD_BY_EFFECTS,      /* the effects' colour is on the device */
    FOG_HELD_FOR_A_SUBSTEP    /* a band the effects set without their colour, for one substep */
} fog_holder_t;

/* What one look at the engine found. `pushed` is the band this module last put on the device, or
 * the one the level's fog apply last gave it from the record. */
typedef struct fog_owner_view {
    bool              colour_read;
    uint32_t          device_rgb;
    uint32_t          level_rgb;
    bool              cells_read;
    fog_regime_band_t cells;
    fog_regime_band_t pushed;
    bool              substep_read;
    uint32_t          substep;
} fog_owner_view_t;

/* The one substep a band the effects set is honoured for. */
typedef struct fog_owner_grace {
    bool              running;
    fog_regime_band_t band;
    uint32_t          since;
} fog_owner_grace_t;

/* ==============================================================================================
 * Pure. No engine memory; this is what the unit test drives.
 * ============================================================================================ */

/* The device colour is not the level's: the effects hold the fog. Both are packed 0xRRGGBB. */
bool fog_owner_effects_hold(uint32_t device_rgb, uint32_t level_rgb);

/* Who holds the device now. Moves the grace on, and sets `*grace_began` when one starts here. */
fog_holder_t fog_owner_judge(const fog_owner_view_t *view, fog_owner_grace_t *grace,
                             bool *grace_began);

/* Whether a frame has nothing to write: the band has not moved, and either the device is not this
 * module's to write or it already shows the band. */
bool fog_owner_band_settled(const fog_regime_band_t *current, const fog_regime_band_t *written,
                            bool pixel_fog, fog_holder_t holder, bool device_shows_current);

/* The band alone, committed: flags without the fog bit, the band, the flags as they were. */
void fog_owner_push_band(const fog_device_writes_t *writes, const fog_regime_band_t *band);

/* ==============================================================================================
 * The engine side.
 * ============================================================================================ */

/* Reads the band and flag setters and the colour cells out of the level's fog apply at
 * `apply_level_fog`, and finds the engine's substep counter. Once; says what bound. */
void fog_owner_bind(uintptr_t apply_level_fog);

/* One frame's look: who holds the device, counted, with a line on each change of hands. `current`
 * is this module's eased band, for that line. */
fog_holder_t fog_owner_observe(const void *level, const fog_regime_band_t *pushed,
                               const fog_regime_band_t *current);

/* Whether the device shows `band`: its cells when they are read, `pushed` otherwise. */
bool fog_owner_device_shows(const fog_regime_band_t *band, const fog_regime_band_t *pushed);

/* Puts `band`, the one the record holds, on the device when the device is this module's to write.
 * The caller has checked that per pixel fog runs and that the device exists. True when it was
 * written. */
bool fog_owner_push(void *level, const fog_regime_band_t *band, const fog_regime_band_t *pushed,
                    fog_apply_fn_t apply);

/* A level adopted and a level left; the second writes the level's line. */
void fog_owner_level_begins(void);
void fog_owner_level_ends(void);

#endif /* VIEW_DISTANCE_FIX_FOG_OWNER_H */
