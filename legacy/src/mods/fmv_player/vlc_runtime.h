/* vlc_runtime.h: is libVLC loaded, and what are its entry points.
 *
 * Split out of vlc_playback.c, which was doing two jobs that change for different reasons.
 * This half moves when libVLC's packaging or its export names move. The other half, playing one
 * file in one window, moves when the game's window handling or the skip keys do.
 *
 * The api table below is the seam. Everything behind it answers "can libVLC be called at all";
 * everything on the far side of it calls libVLC to play something.
 */
#ifndef FMV_PLAYER_VLC_RUNTIME_H
#define FMV_PLAYER_VLC_RUNTIME_H

#include <windows.h>

#include <stdbool.h>

/* ============================================================================================
 * The hand-declared slice of libvlc's C API. Opaque handles, never dereferenced here, only handed
 * back to libvlc's own functions, plus __cdecl function-pointer typedefs matching libVLC's own
 * declarations, which use plain C linkage with no stdcall or WINAPI decoration anywhere.
 * ============================================================================================ */
typedef struct libvlc_instance_t libvlc_instance_t;
typedef struct libvlc_media_t libvlc_media_t;
typedef struct libvlc_media_player_t libvlc_media_player_t;

typedef libvlc_instance_t *(__cdecl *libvlc_new_fn)(int argc, const char *const *argv);
typedef void (__cdecl *libvlc_release_fn)(libvlc_instance_t *instance);
typedef libvlc_media_t *(__cdecl *libvlc_media_new_path_fn)(libvlc_instance_t *instance,
                                                            const char *path);
typedef void (__cdecl *libvlc_media_release_fn)(libvlc_media_t *media);
typedef libvlc_media_player_t *(__cdecl *libvlc_media_player_new_from_media_fn)(libvlc_media_t *media);
typedef void (__cdecl *libvlc_media_player_release_fn)(libvlc_media_player_t *player);
typedef void (__cdecl *libvlc_media_player_set_hwnd_fn)(libvlc_media_player_t *player,
                                                         void *drawable);
typedef int (__cdecl *libvlc_media_player_play_fn)(libvlc_media_player_t *player);
typedef void (__cdecl *libvlc_media_player_stop_fn)(libvlc_media_player_t *player);
typedef int (__cdecl *libvlc_media_player_is_playing_fn)(libvlc_media_player_t *player);
/* Takes a string like "16:9", or NULL to go back to the source's own shape. */
typedef void (__cdecl *libvlc_video_set_aspect_ratio_fn)(libvlc_media_player_t *player,
                                                          const char *aspect);

typedef struct vlc_api {
    HMODULE                               core_module;
    HMODULE                               module;
    libvlc_new_fn                         new_instance;
    libvlc_release_fn                     release;
    libvlc_media_new_path_fn              media_new_path;
    libvlc_media_release_fn               media_release;
    libvlc_media_player_new_from_media_fn player_new_from_media;
    libvlc_media_player_release_fn        player_release;
    libvlc_media_player_set_hwnd_fn       player_set_hwnd;
    libvlc_media_player_play_fn           player_play;
    libvlc_media_player_stop_fn           player_stop;
    libvlc_media_player_is_playing_fn     player_is_playing;
    libvlc_video_set_aspect_ratio_fn      video_set_aspect_ratio;   /* optional, see resolve */
    libvlc_instance_t                    *instance;
    bool                                  plugin_path_set;
} vlc_api_t;

/* The resolved entry points. Never NULL itself, so a caller tests the field it is about to use;
 * `instance` is NULL when libVLC never loaded. */
const vlc_api_t *vlc_runtime_api(void);

#endif /* FMV_PLAYER_VLC_RUNTIME_H */
