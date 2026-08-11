# One DLL per independent fix, all of them landing in mods\.
#
# add_engine_fix(<name> <sources...>) builds <name>.dll from <name>/<source>. The directory name,
# the DLL name, the ini section and the log prefix are all the same word, which is what lets a
# player match a line in the log to a file on disk without a lookup table.

function(add_engine_fix name)
    set(sources "")
    foreach(source ${ARGN})
        list(APPEND sources ${name}/${source})
    endforeach()

    add_library(${name} SHARED ${sources})
    target_link_libraries(${name} PRIVATE engine_fixes_common)
    target_include_directories(${name} PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/${name})
    set_target_properties(${name} PROPERTIES
        PREFIX ""
        RUNTIME_OUTPUT_DIRECTORY ${ENGINE_FIX_OUTPUT}/mods
        RUNTIME_OUTPUT_DIRECTORY_DEBUG ${ENGINE_FIX_OUTPUT}/mods
        RUNTIME_OUTPUT_DIRECTORY_RELEASE ${ENGINE_FIX_OUTPUT}/mods
    )
endfunction()


add_engine_fix(variable_fov
    dll_main.c
    variable_fov.c
    fov_math.c
    fov_menu.c
    fov_strings.c
)

add_engine_fix(framerate_fix
    dll_main.c
    framerate_fix.c
    face_latch.c
    camera_compensation.c
    draw_interpolation.c
    framerate_stats.c
)
target_link_libraries(framerate_fix PRIVATE winmm)

# cursor_anchor.c repairs an engine assumption that only window_fit.c can break: the engine
# compares CLIENT coordinates against the SCREEN point it warps the pointer to, and those agree
# only while the window sits at screen 0,0, which is where the engine itself always left it.
add_engine_fix(enhanced_resolution
    dll_main.c
    enhanced_resolution.c
    window_fit.c
    cursor_anchor.c
    pointer_cage.c
    focus_guard.c
)

add_engine_fix(hud_ratio_scaling
    dll_main.c
    hud_ratio_scaling.c
    hud_layout.c
)

# fog_regime.c is the fog: which of the engine's two fog regimes runs, and the coupling that keeps
# the band inside the cut edge as the field of view widens.
add_engine_fix(view_distance_fix
    dll_main.c
    view_distance_fix.c
    cell_watchdog.c
    draw_table.c
    fog_regime.c
)

add_engine_fix(dismemberment
    dll_main.c
    dismemberment.c
    limb_flight.c
)

# Free look is four files. free_look.c is the body half, the on/off switch and installation;
# free_look_camera.c writes the camera's own yaw offset from a chained detour on updateCam and was
# lifted out when the pair reached 1139 lines; camera_sites.c is the patterns that find that
# camera, kept apart for the same reason player_sites.c is; free_look_math.c is the arithmetic,
# which the test links on its own.
add_engine_fix(enhanced_input
    dll_main.c
    enhanced_input.c
    input_menu.c
    input_slider.c
    mouse_look.c
    player_sites.c
    steer_lean.c
    steer_log.c
    strafe_walk.c
    free_look.c
    free_look_camera.c
    free_look_log.c
    free_look_math.c
    camera_sites.c
    camera_watch.c
)

# music_sites.c is the patterns and the cross-checks that decide whether to believe them;
# imuse_fix.c is the behaviour those cells drive. Same split as player_sites and camera_sites.
add_engine_fix(imuse_fix
    dll_main.c
    imuse_fix.c
    music_sites.c
    music_probe.c
    imuse_sites.c
    imuse_guard.c
)

add_engine_fix(crash_report
    dll_main.c
    crash_report.c
)

add_engine_fix(crt_copy_fix
    dll_main.c
    crt_copy_fix.c
)

add_engine_fix(decal_fix
    dll_main.c
    decal_fix.c
)

# Two independent bugs behind "the SFX slider resets to full on reload": the value saved came
# from an unreliable AIL query instead of the engine's own volume mirror, and separately the
# startup load-time apply is a guaranteed no-op because sfx_sound_init marks sound ready one
# statement after it applies the loaded volume. See the DLL's own header comment and README.
add_engine_fix(sfx_volume_save_fix
    dll_main.c
    sfx_volume_save_fix.c
)

add_engine_fix(diagnostics
    dll_main.c
    diagnostics.c
    diag_log.c
    diag_names.c
    diag_install.c
    diag_audio.c
    diag_world.c
    diag_flow.c
)
target_link_libraries(diagnostics PRIVATE winmm)

# video_overlay.c finds the game's own window and plays into a borderless popup owned by it through
# a 32-bit libVLC install found and loaded dynamically at runtime (vlc_playback.c - no libVLC SDK
# linked here at all, on purpose, see its own header comment); fmv_player.c is the one
# byte-evidenced detour that decides when to. This used to be Media Foundation/MFPlay running as a
# separate process (fmv_player_host.exe, no longer in this tree); see video_overlay.c's header
# comment for the full chronology of why both of those changed. advapi32 (RegOpenKeyExW etc., used
# to find a non-default VLC install) is already one of MSVC's own default libraries, linked here
# anyway for the same reason winmm/ole32 are explicit elsewhere in this tree: so the dependency is
# visible at a glance instead of relying on an implicit default.
add_engine_fix(fmv_player
    dll_main.c
    fmv_player.c
    video_overlay.c
    vlc_playback.c
)
target_link_libraries(fmv_player PRIVATE advapi32)
