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
    frame_delta.c
    sim_clock.c
    particle_clock.c
    mover_interpolation.c
    mover_blend.c
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
    menu_island_clip.c
    menu_scale.c
    menu_loading_bar.c
    menu_art_source.c
    mode_filter.c
    ending_resolution.c
    credits_skip.c
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
    frame_governor.c
    draw_table.c
    vertex_table.c
    fog_regime.c
    spawn_census.c
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
    input_config.c
    input_gate.c
    input_menu.c
    input_slider.c
    mouse_look.c
    mouse_config.c
    mouse_rate.c
    raw_mouse.c
    menu_cursor.c
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
    view_lead.c
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

# Two independent bugs behind "the SFX slider resets to full on reload": the value saved came from
# an unreliable AIL query instead of the engine's own volume mirror, and separately the startup
# load-time apply is a guaranteed no-op because bapsound_moduleInit marks sound ready one statement
# after it applies the loaded volume. See the DLL's own header comment and README.
add_engine_fix(sfx_volume_save_fix
    dll_main.c
    sfx_volume_save_fix.c
)

add_engine_fix(ground_clip_fix
    dll_main.c
    ground_clip_fix.c
    move_mode.c
)

add_engine_fix(sound_lifetime_fix
    dll_main.c
    sound_lifetime_fix.c
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
    diag_frame.c
    diag_present.c
    diag_projectiles.c
    diag_characters.c
    character_scan.c
    debug_register.c
    diag_write_watch.c
)
target_link_libraries(diagnostics PRIVATE winmm psapi pdh)

# Five files, and the seams are the four different ways this feature fails. fmv_player.c is the one
# byte-evidenced detour and decides WHEN a movie is ours. vlc_locate.c answers "is there a 32-bit
# libVLC on this machine at all", which is the question about somebody's registry and is kept apart
# for the same reason player_sites.c and music_sites.c are. vlc_playback.c loads that libVLC and
# drives it. video_overlay.c owns the window it draws into. movie_path.c is the arithmetic, kept
# apart for the same reason fov_math.c and free_look_math.c are: it is the only part of this
# feature a test can observe without the game, and unittests/movie_path.c links it on its own.
# advapi32 (RegOpenKeyExW etc.) is already one of MSVC's own default libraries, linked here anyway
# for the same reason winmm/ole32 are explicit elsewhere in this tree: so the dependency is visible
# at a glance instead of relying on an implicit default.
add_engine_fix(fmv_player
    dll_main.c
    fmv_player.c
    movie_path.c
    menu_cursor_cells.c
    video_overlay.c
    sfx_mute.c
    render_curtain.c
    vlc_locate.c
    vlc_playback.c
)
target_link_libraries(fmv_player PRIVATE advapi32 winmm)


# Two unbounded writes in one engine function, so one DLL and one hook. Nothing here changes
# what is drawn in a scene that stays inside the limits the artists worked to.
add_engine_fix(render_guard
    dll_main.c
    render_guard.c
    face_bounds.c
)

# An effect with no geometry state, re-rolled once per rendered frame because that is what 1999
# meant by once in a while. It is put back on the engine's own substep clock, so it changes at
# the rate it was authored for instead of at the frame rate.
add_engine_fix(effect_clock
    dll_main.c
    effect_clock.c
    substep_noise.c
)

# Not a fix but a capability. One immediate governs both texture axes, and on the original
# artwork the patch is the identity, because no page the game ships is larger than 64 pixels.
add_engine_fix(large_textures
    dll_main.c
    large_textures.c
    texture_size.c
)


# The panel over the running game, opened with the key below Escape. Named for what it is rather than for what is
# in it: diagnostics and developer tools become groups inside the same panel, and a shipped DLL
# cannot be renamed without breaking every configuration file that mentions it. It draws with the
# engine's own two dimensional primitives, so there is no window and no second surface.
# A talking NPC's own script sets its head/face animation target once per line and only ever
# resets it as a side effect of that same actor's own NEXT line starting, never when a different
# actor's line supersedes it. Invisible whenever the previous speaker is off camera, which is most
# of the game; visible the moment a shot holds on two actors while one keeps talking. This calls
# the engine's own animation debounce/trigger function the instant a different actor's dialogue
# trigger fires, exactly what a correctly authored "Animation: idle" node would do.
add_engine_fix(dialogue_anim_fix
    dll_main.c
    dialogue_anim_fix.c
)
target_link_libraries(dialogue_anim_fix PRIVATE winmm)

add_engine_fix(dev_overlay
    dll_main.c
    dev_overlay.c
    authored_fog_row.c
    auto_range_row.c
    view_range_row.c
    dev_menu_size_row.c
    overlay_model.c
    overlay_draw.c
    overlay_sites.c
    overlay_layout.c
    overlay_input.c
    input_freeze.c
    sim_pause.c
    cheats_original.c
    cheats_original_actions.c
    cheats_openphantom.c
    cheats_npc_damage.c
    cheats_jump_boost.c
    cheats_free_camera.c
    cheats_no_fog.c
    floor_probe.c
)

# A field investigation found that Xidi, a third-party wrapper this project used to install to
# expose an Xbox-style pad to this game's own WinMM joystick calls, produces rare but large
# single-frame stalls while being actively polled. The only two things it was used for here,
# right-stick look and Start pausing, are reproduced without it: XInputGetState reads the pad
# directly, SendInput feeds the same synthetic mouse/keyboard input any external tool would. No
# wrapper DLL and no dependency on enhanced_input.
add_engine_fix(controller_input
    dll_main.c
    controller_input.c
)
target_link_libraries(controller_input PRIVATE xinput)
