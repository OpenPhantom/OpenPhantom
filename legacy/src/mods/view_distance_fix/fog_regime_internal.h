/* fog_regime_internal.h: the record the fog regime keeps, shared by the two files that own it.
 *
 * WHY THIS EXISTS, AND WHY IT IS NOT A WIDENING. fog_regime.c was past the hard limit and its own
 * section banners named four jobs. Two of them are gone from it now: the arithmetic to fog_band.c,
 * which needed nothing from here, and the install pass to fog_regime_install.c, which needs all of
 * it. Ten of the install pass's thirteen functions write this record.
 *
 * So the state is shared rather than hidden, and that is honest about what the fog regime is: one
 * machine, not two. The split is between a pass that runs ONCE and resolves every site, and the
 * frame by frame work that runs for the rest of the session. That is a real boundary in time and
 * in purpose even though both halves write the same record.
 *
 * Nothing outside those two files includes this. The module's contract is fog_regime.h.
 */
#ifndef VIEW_DISTANCE_FIX_FOG_REGIME_INTERNAL_H
#define VIEW_DISTANCE_FIX_FOG_REGIME_INTERNAL_H

#include "fog_regime.h"

#include "common/detour.h"

#include <stdbool.h>
#include <stdint.h>

typedef void (__cdecl *apply_fog_fn_t)(void *level);

typedef struct fog_regime_state {
    bool                installed;
    fog_regime_config_t config;

    detour_t            apply_detour;
    void * volatile    *level_pointer;   /* [g_level], read out of bapdraw_setFrameState */
    const volatile float *frame_delta;   /* the engine's own seconds-per-frame */

    /* The level we were handed, and the band it was loaded with. EVERY target is computed from
     * `authored`; nothing here ever reads the live field back as an input, which is what stops a
     * repeated apply from squaring its own effect. */
    void               *level;
    void               *level_without_fog;   /* reported once, not once per effects restore */
    fog_regime_band_t   authored;
    fog_regime_band_t   written;         /* what we last put into the record */
    fog_regime_band_t   current;         /* the eased value; `written` mirrors it */
    int32_t             level_view_distance;
    float               open_left;         /* seconds of the opening window still to run */

    float               horizontal_fov_degrees;
    float               reference_cut;
    float               live_cut;
    bool                cut_observed;
    bool                cut_first_seen;    /* the first real observation after a level snap */
    float               settled_live_cut;  /* the live cut, eased; see FOG_CUT_SETTLE_SECONDS */

    /* The address of the engine's device-record pointer, taken out of the capability query's own
     * `mov eax,[abs32]` before that query is patched over. Zero when it could not be read. */
    uintptr_t           device_record_ptr;
    bool                caps_reported;

    /* Where the engine keeps its IDirect3DDevice3 pointer, read out of the state commit's own
     * `mov ecx,[abs32]`. Zero when that could not be read. */
    uintptr_t           device_ptr_addr;
    uint32_t            device_caps;

    /* What install_vertex_fog wrote, kept so that the pixel path can hand the engine back its own
     * table-fog branch. Reverting is the whole reason these are stored. */
    bool                vertex_fog_installed;
    uintptr_t           query_entry;
    uintptr_t           applier_push;
    uintptr_t           commit_push;
    uint8_t             saved_query[3];

    bool                pixel_fog_active;
    fog_regime_band_t   device_band;       /* what FOGSTART and FOGEND were last told */
    bool                inside_apply;      /* true while our own applyLevelFog detour is running */

    const void         *projection_device;   /* the device the matrix was last given to */

    bool                tick_active;
    bool                span_refused;    /* the "end came out below start" complaint, logged once */
} fog_regime_state_t;

/* The one record. Defined in fog_regime.c. */
extern fog_regime_state_t fog_state;

/* The four functions that call across the cut, and the only four.
 *
 * The install pass hooks baplight_applyLevelFog, whose handler runs on every level load and so
 * belongs with the per level work rather than with the pass that installed it. The per level work
 * reports the device's fog capabilities once, which is install time knowledge. Naming both here
 * is cheaper than moving any of them to the wrong side. */
void __cdecl hook_apply_fog(void *level);
void report_device_fog_caps(void);
void consider_pixel_fog(uint32_t caps);
void push_band_to_device(void);

#endif /* VIEW_DISTANCE_FIX_FOG_REGIME_INTERNAL_H */
