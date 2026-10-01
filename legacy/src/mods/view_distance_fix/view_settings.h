/* view_settings.h: the settings record this DLL is driven by.
 *
 * The reader that fills it in is view_settings.c. It is a responsibility of its own: it touches
 * no engine memory, resolves no signature, and is the only part of this feature somebody looking
 * for a default has to read. The record itself is declared here because every other file in the
 * DLL reads fields out of it, and there is exactly one of them: the install sequence owns the
 * storage and hands a pointer to whoever needs it, so a key changed while the game runs is seen
 * by all of them at once.
 */
#ifndef VIEW_SETTINGS_H
#define VIEW_SETTINGS_H

#include <stdbool.h>

/* The clamps the draw distance and the fog band are held to, named because two more places hold
 * them: the table of settings a multiplayer host may decide for its clients
 * (common/host_settings_note.c), and the developer panel's rows. A test holds all three against
 * each other, since a header cannot be shared between two DLLs without making one depend on the
 * other. The reasons for each bound are in view_settings.c where they are applied. */
#define VIEW_SETTINGS_RANGE_MIN    1.0f
#define VIEW_SETTINGS_RANGE_MAX    2.5f
#define VIEW_SETTINGS_FOG_BAND_MIN 0.25f
#define VIEW_SETTINGS_FOG_BAND_MAX 1.0f

typedef struct view_distance_config {
    bool  enabled;
    float view_range_scale;
    bool  frame_backoff;
    bool  strict_view_range;
    float backoff_fps;
    int   fog_inside_cut;
    bool  fog_follow_fov;
    int   fog_implementation;   /* 0 engine untouched, 1 the per-vertex ramp, 2 per pixel */
    bool  authored_fog;
    float fog_min_end;
    float fog_band_scale;
    float level_open_seconds;
    float level_open_range;
    float fog_scale;
    float fog_settle_seconds;
    float npc_range_scale;
    float cutscene_range;
    bool  poly_depth_bias;
    bool  translucent_fog;
    bool  dither;
    float level_fade_seconds;
    bool  log_fog_band;
    float level_open_fog_start;
    float level_open_fog_end;
    bool  two_sided_severed;
    int   two_sided_max;
    bool  relocate_draw_table;
    bool  lower_cell_limit;
    bool  relocate_vertex_table;
    bool  spawn_census;
    bool  log_player_position;
    bool  range_pinned;         /* no cell watchdog: the draw distance stays at 1.0 whoever asks */
} view_distance_config_t;

/* The clamp the reader holds every value to. It is shared rather than duplicated because the fog
 * configuration applies the same two bounds to a pair of settings this reader deliberately leaves
 * alone, and two copies of a bound is how the two stop agreeing. */
float view_settings_clamp(float value, float minimum, float maximum);

/* Reads the whole record out of the ini and applies every clamp. Called once, before any site is
 * resolved, because whether a site is patched at all depends on what it finds. */
void view_settings_load(view_distance_config_t *config);

/* The handful of keys that are re-read while the game runs, on a frame counter of its own.
 * `effective_view_scale` is the number the draw distance hook multiplies by, and it is passed in
 * because a raise on disk has to reset it: the frame governor and the cell watchdog only ever
 * lower it, so without the reset a scale nobody is asking for any more would go on standing.
 *
 * In a multiplayer session a client takes the host's draw distance, fog band and authored band in
 * place of its own, from memory, and its engine_fixes.ini is not written; its own values come back
 * when the session is over. The frame governor, the cell watchdog and StrictViewRange stay this
 * machine's own: the host sets the target, the machine protects itself. */
void view_settings_poll(view_distance_config_t *config, float *effective_view_scale);

/* Writes the draw distance actually in force back to the ini, for the panel that cannot see what
 * happened to the number it wrote. Output only; nothing reads it back into the engine. While the
 * host's draw distance is in force nothing is written, because the number would be the host's and
 * not this machine's; the acknowledgement the multiplayer reads carries it instead, filed here
 * because this is where the value after the governor and the watchdog is known. */
void view_settings_publish_effective_scale(float scale);

#endif /* VIEW_SETTINGS_H */
