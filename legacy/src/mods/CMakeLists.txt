# One DLL per independent fix, all of them landing in mods\.
#
# add_engine_fix(<name> <sources...>) builds <name>.dll from <name>/<source>. The directory name,
# the DLL name, the ini section and the log prefix are all the same word, and that lets a
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
    add_version_resource(${name})
endfunction()


add_engine_fix(variable_fov
    dll_main.c
    variable_fov.c
    fov_math.c
    fov_menu.c
    fov_poll.c
    fov_strings.c
)

add_engine_fix(framerate_fix
    dll_main.c
    framerate_fix.c
    face_latch.c
    # the engine's simulation step count, which the facing latch and the rider blend both need and
    # neither should reconstruct for itself
    substep_counter.c
    camera_compensation.c
    camera_target.c
    draw_interpolation.c
    framerate_stats.c
    frame_cap.c
    frame_delta.c
    frame_wait.c
    power_throttling.c
    sim_clock.c
    world_clock.c
    particle_clock.c
    mover_interpolation.c
    object_interpolation.c
    rider_trace.c
    object_track.c
    mover_blend.c
    mover_evenness.c
    mover_slots.c
    mover_wraps.c
)
target_link_libraries(framerate_fix PRIVATE winmm)

# cursor_anchor.c repairs an engine assumption that only window_fit.c can break: the engine
# compares CLIENT coordinates against the SCREEN point it warps the pointer to, and those agree
# only while the window sits at screen 0,0, which is where the engine itself always left it.
add_engine_fix(enhanced_resolution
    dll_main.c
    enhanced_resolution.c
    window_fit.c
    window_mode.c
    windowed_device.c
    present_clip.c
    pointer_release.c
    window_poll.c
    fullscreen_toggle.c
    window_close.c
    cursor_anchor.c
    pointer_cage.c
    focus_guard.c
    menu_island_clip.c
    menu_scale.c
    menu_scale_sites.c
    menu_scale_install.c
    menu_scale_refit.c
    menu_scale_3d.c
    menu_preview.c
    menu_art_census.c
    menu_art_resample.c
    menu_art_load.c
    menu_loading_bar.c
    menu_art_source.c
    mode_depth.c
    mode_filter.c
    mode_table.c
    sw_blit_guard.c
    ending_resolution.c
    credits_skip.c
    subtitle_scale.c
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
    npc_range.c
    view_settings.c
    view_host_value.c
    view_range.c
    two_sided_faces.c
    cell_watchdog.c
    frame_governor.c
    draw_table.c
    vertex_table.c
    fog_regime.c
    fog_band.c
    fog_regime_install.c
    fog_owner.c
    fog_trace.c
    poly_bias.c
    translucent_fog.c
    device_dither.c
    scene_fade.c
    spawn_census.c
)

add_engine_fix(dismemberment
    dll_main.c
    dismemberment.c
    limb_mode_pick.c
    limb_nodes.c
    limb_flight.c
)

# Free look is five files. free_look.c is the body half, the on/off switch and installation;
# free_look_aim.c is the shot under free look, the three detours on the attack path;
# free_look_camera.c writes the camera's own yaw offset from a chained detour on updateCam and was
# lifted out when the pair reached 1139 lines; camera_sites.c is the patterns that find that
# camera, kept apart for the same reason player_sites.c is; free_look_math.c is the arithmetic,
# which the test links on its own.
add_engine_fix(enhanced_input
    dll_main.c
    enhanced_input.c
    input_config.c
    input_foreground.c
    input_gate.c
    input_menu.c
    input_slider.c
    input_switches.c
    camera_follow.c
    frame_clock.c
    free_look_follow.c
    input_mode.c
    input_steer.c
    pad_stick.c
    pad_axis_mute.c
    pad_run.c
    menu_captions.c
    mouse_look.c
    mouse_census.c
    mouse_config.c
    mouse_rate.c
    raw_mouse.c
    menu_cursor.c
    player_sites.c
    steer_lean.c
    steer_log.c
    strafe_walk.c
    free_look.c
    free_look_aim.c
    free_look_camera.c
    free_look_log.c
    free_look_math.c
    camera_sites.c
    camera_watch.c
    view_lead.c
)
# XInput is linked here as well as named by pad_stick.c's pragma, so a toolchain that ignores
# the pragma still links, the same way controller_input does both.
target_link_libraries(enhanced_input PRIVATE xinput)

# music_sites.c is the patterns and the cross-checks that decide whether to believe them;
# imuse_fix.c is the behaviour those cells drive. Same split as player_sites and camera_sites.
add_engine_fix(imuse_fix
    dll_main.c
    imuse_fix.c
    music_sites.c
    music_probe.c
    music_volume.c
    music_volume_latch.c
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
    dry_at_start.c
    scorch_reach.c
)

# Two independent bugs behind "the SFX slider resets to full on reload": the value saved came from
# an unreliable AIL query instead of the engine's own volume mirror, and separately the startup
# load-time apply is a guaranteed no-op because bapsound_moduleInit marks sound ready one statement
# after it applies the loaded volume. See the DLL's own header comment and README.
add_engine_fix(sfx_volume_save_fix
    dll_main.c
    sfx_volume_save_fix.c
    slider_rounding.c
)

add_engine_fix(ground_clip_fix
    dll_main.c
    ground_clip_fix.c
    move_mode.c
    creeping_mover.c
    rider_floor.c
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
    diag_world_mover_census.c
    diag_world_path_census.c
    diag_flow.c
    diag_frame.c
    diag_core_class.c
    diag_present.c
    diag_x87.c
    diag_projectiles.c
    diag_camera_owner.c
    diag_dialogue_ops.c
    diag_footsteps.c
    diag_characters.c
    character_scan.c
    debug_register.c
    diag_write_watch.c
    diag_push_block.c
)
target_link_libraries(diagnostics PRIVATE winmm psapi pdh)

# Five files, and the seams are the four different ways this feature fails. fmv_player.c is the one
# byte-evidenced detour and decides WHEN a movie is ours. vlc_locate.c answers "is there a 32-bit
# libVLC on this machine at all", which is the question about somebody's registry and is kept apart
# for the same reason player_sites.c and music_sites.c are. vlc_runtime.c loads that libVLC and
# resolves its exports, vlc_playback.c drives it. video_overlay.c owns the window it draws
# into. movie_path.c is the arithmetic, kept
# apart for the same reason fov_math.c and free_look_math.c are: it is the only part of this
# feature a test can observe without the game, and unittests/movie_path.c links it on its own.
# advapi32 (RegOpenKeyExW etc.) is already one of MSVC's own default libraries, linked here anyway
# for the same reason winmm/ole32 are explicit elsewhere in this tree: so the dependency is visible
# at a glance instead of relying on an implicit default.
# In a multiplayer session a movie belongs to the host. movie_rule.c decides, pure and tested;
# movie_session.c reads the multiplayer's gate and says what it decided; movie_wait.c is the black
# wait of a client whose movie ended first and movie_text.c its line; bink_cells.c is the retail
# player's abort cell and the watch that writes it; movie_close.c is the close box during a movie.
add_engine_fix(fmv_player
    dll_main.c
    fmv_player.c
    movie_path.c
    movie_close.c
    movie_rule.c
    movie_session.c
    movie_text.c
    movie_wait.c
    bink_cells.c
    menu_cursor_cells.c
    video_overlay.c
    sfx_mute.c
    render_curtain.c
    vlc_locate.c
    vlc_playback.c
    vlc_runtime.c
)
target_link_libraries(fmv_player PRIVATE advapi32 winmm)


# Two unbounded writes in one engine function, so one DLL and one hook. Nothing here changes
# what is drawn in a scene that stays inside the limits the artists worked to.
add_engine_fix(render_guard
    dll_main.c
    render_guard.c
    face_bounds.c
    flat_quad.c
    node_verts.c
    halo_copy.c
    halo_verts.c
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
    scene_scope.c
    idle_clip.c
    speaker_gesture.c
    speaker_rest.c
    stand_in.c
    stand_in_watch.c
)
target_link_libraries(dialogue_anim_fix PRIVATE winmm)

# A branching conversation stays open only while its script node is visited every step, and a
# script that gates the node on the actor's own voice never visits it while the voice plays; the
# text-timed pacing stamp runs out first, the box closes, and the line starts again from the top.
add_engine_fix(dialogue_menu_fix
    dll_main.c
    dialogue_menu_fix.c
)
target_link_libraries(dialogue_menu_fix PRIVATE winmm)

# A spoken line that names a camera group takes the camera and the dialogue only gives it back
# when a choice MENU was open, which an ordinary line never has. The camera then stays on that
# shot until the level is reloaded. Found in Otoh Gunga, present in every level that talks.
add_engine_fix(camera_handback_fix
    dll_main.c
    camera_handback_fix.c
    handback_rule.c
)

add_engine_fix(dev_overlay
    dll_main.c
    dev_overlay.c
    fog_band_row.c
    view_range_live_row.c
    overlay_host_value.c
    overlay_key_name.c
    overlay_utilities.c
    overlay_framerate.c
    overlay_window.c
    open_key_row.c
    chat_key_row.c
    overlay_multiplayer.c
    player_help_row.c
    fog_follow_row.c
    auto_range_row.c
    fov_row.c
    menu_extras_row.c
    sensitivity_row.c
    free_look_row.c
    strafe_row.c
    controller_mode_row.c
    overlay_controls.c
    overlay_picture.c
    overlay_fog.c
    overlay_freecam.c
    overlay_dismember.c
    overlay_menu_extras.c
    overlay_cheats.c
    overlay_row_fill.c
    overlay_reason.c
    overlay_kit.c
    overlay_number.c
    overlay_choice.c
    overlay_frame.c
    overlay_legend.c
    overlay_slider.c
    overlay_edit.c
    overlay_keys.c
    overlay_chip.c
    overlay_row_source.c
    air_control_row.c
    camera_follow_row.c
    strict_range_row.c
    view_range_row.c
    dev_menu_size_row.c
    dismemberment_row.c
    subtitle_size_row.c
    overlay_model.c
    overlay_headings.c
    overlay_draw.c
    overlay_sites.c
    overlay_layout.c
    overlay_notice.c
    overlay_input.c
    input_freeze.c
    player_slot.c
    sim_pause.c
    cheats_original.c
    cheats_original_actions.c
    cheats_openphantom.c
    cheats_npc_damage.c
    cheats_super_run.c
    cheats_jump_boost.c
    cheats_fall_consequences.c
    cheats_noclip.c
    cheats_free_camera.c
    freecam_teleport.c
    freecam_world.c
    freeze_anim_row.c
    start_level.c
    start_level_row.c
    overlay_levels.c
    npc_spawner.c
    npc_census.c
    npc_foreign.c
    npc_spawn_block.c
    npc_spawn_list.c
    npc_spawn_record.c
    npc_spawn_rules.c
    npc_spawn_sites.c
    npc_spawn_save.c
    npc_spawn_node.c
    npc_spawn_describe.c
    world_probe.c
    npc_spawn_session.c
    npc_spawn_link.c
    pad_input.c
    pad_panel.c
    panel_cage.c
    spawn_scripts.c
    actor_loader.c
    overlay_spawn.c
    session_lock.c
    local_look.c
    # The model swap: wear another actor's geometry and keep your own hero, clips and weapons.
    # The blade guard answers the one engine assert a borrowed rig can reach; keeping the
    # player's own clips and weapons is what makes it the only guard the swap needs.
    model_blade_guard.c
    overlay_modelswap.c
    character_model.c
    character_model_sites.c
    character_model_roster.c
    character_nodemap.c
    character_restmap.c
    character_bodies.c
    character_rebind.c
    character_clipcopy.c
    character_prop.c
    character_prop_blade.c
    character_prop_body.c
    character_prop_rig.c
    character_prop_sites.c
    character_prop_draw.c
    character_prop_math.c
    character_reentry.c
    character_mount.c
    character_facing.c
    character_buoyancy.c
    character_ground.c
    # The bound on the sabre card fetch and the bracket on their registration, which a rebind
    # needs whether or not render_guard is loaded beside it.
    character_cards.c
    # A far player's body wearing the model that player wears, answered through the notes.
    far_model.c
    actor_catalog.c
    cheats_no_fog.c
    floor_probe.c
    # The entity spawner's offer rule and its generated table, and its placement mode: the state,
    # the camera and the pointer's ray, the preview, the marks, and who owns the pointer.
    entity_offer.c
    entity_names.c
    npc_spawner_sites.c
    spawn_place.c
    spawn_reason.c
    spawn_keys.c
    spawn_ghost.c
    spawn_look.c
    spawn_marks.c
    spawn_banner.c
    spawn_mode.c
    world_pick.c
    world_camera.c
    input_owner.c
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
    look_counts.c
)
target_link_libraries(controller_input PRIVATE xinput)
# The panel and the free camera read the pad themselves (pad_input.c), through the same API.
target_link_libraries(dev_overlay PRIVATE xinput)

# The multiplayer: a co-operative campaign for up to four players over UDP, directly or through
# the relay, with a lobby in the game's own menu, the host's world described to the clients
# every substep, the far players as puppets driven by what arrives, and the host's savegame
# carried to a joining client. The deathmatch is built as well, and the menu does not offer it.
# The master switch, Enabled, is off in the code, so an ini without it leaves the game alone,
# and the shipped ini turns it on; with it off nothing is scanned. The switches the feature
# stands on are on in the code.
# One machine runs one game: nothing here lets a second instance start beside the first.
add_engine_fix(multiplayer
    dll_main.c
    multiplayer.c
    mp_pumps.c
    mp_bootstrap.c
    mp_module.c
    mp_task.c
    mp_diag.c
    mp_capacity.c
    mp_memory_watch.c
    mp_census_probe.c
    mp_pool.c
    mp_pool_rule.c
    mp_bank.c
    mp_bank_keep.c
    mp_bank_spawn.c
    mp_bridge.c
    mp_bridge_command.c
    mp_bridge_drain.c
    mp_bridge_drain_host.c
    mp_bridge_install.c
    mp_bridge_far.c
    mp_follow_difficulty.c
    mp_bridge_appearance.c
    mp_bridge_relay.c
    mp_trust.c
    mp_bridge_world.c
    mp_bridge_world_check.c
    mp_bridge_report.c
    mp_interp.c
    mp_timeline.c
    mp_damage.c
    mp_damage_entry.c
    mp_input.c
    mp_lifecycle.c
    mp_hero_carry.c
    mp_hero_carry_rule.c
    mp_body.c
    mp_body_passable.c
    mp_body_shot.c
    mp_body_gate.c
    mp_signatures_contact.c
    mp_body_death.c
    mp_contact_rule.c
    mp_phases.c
    mp_puppet.c
    mp_puppet_anim.c
    mp_puppet_anim_engine.c
    mp_puppet_anim_seek.c
    mp_puppet_sabre.c
    mp_puppet_shot.c
    mp_puppet_starter.c
    mp_starter_rule.c
    mp_blade.c
    mp_blade_draw.c
    mp_blade_rule.c
    mp_effects.c
    mp_sound.c
    mp_twist.c
    mp_events.c
    mp_actions.c
    mp_enemy.c
    mp_fieldset.c
    mp_settings.c
    mp_announce.c
    mp_browser.c
    mp_canvas.c
    mp_discovery.c
    mp_announcer.c
    mp_lobby.c
    mp_lobby_note_rule.c
    mp_rules.c
    mp_respawn.c
    mp_respawn_fade.c
    mp_death_music_rule.c
    mp_death_music.c
    mp_seat.c
    mp_seat_wish.c
    mp_seat_order.c
    mp_seat_rule.c
    mp_spawnpoints.c
    mp_signatures_world.c
    mp_signatures_pause.c
    mp_signatures_door.c
    mp_world_door.c
    mp_signatures_hud.c
    mp_score.c
    mp_round.c
    mp_board.c
    mp_text.c
    mp_text_pick.c
    mp_text_second.c
    mp_hud.c
    mp_reentry.c
    mp_arrival.c
    mp_player_help_rule.c
    mp_player_help.c
    mp_repair_lock.c
    mp_teleport_host.c
    mp_follow.c
    mp_world_values.c
    mp_session_over.c
    mp_install.c
    mp_arm_hulls.c
    mp_exit.c
    mp_armed.c
    mp_pause_rule.c
    mp_pause.c
    mp_relay_crypto.c
    mp_relay_wire.c
    mp_relay_inner.c
    mp_relay_noise.c
    mp_relay_keydoc.c
    mp_relay_keysource.c
    mp_relay_link.c
    mp_relay_link_leg.c
    mp_relay_seats.c
    mp_relay_socket.c
    mp_relay_transport.c
    mp_relay_listing.c
    mp_relay_list.c
    mp_relay_anchors.c
    mp_relay_keyfetch.c
    mp_placements.c
    mp_taken.c
    mp_death.c
    mp_match.c
    mp_cutscene.c
    mp_cutscene_sites.c
    mp_signatures_scene.c
    mp_scene_rule.c
    mp_scene_claim_rule.c
    mp_scene_claim.c
    mp_scene_flow.c
    mp_scene_doorless.c
    mp_scene_room.c
    mp_scene_scope.c
    mp_scene_exit.c
    mp_scene_bind.c
    mp_scene_host.c
    mp_scene_seats.c
    mp_scene_own.c
    mp_scene_hero_rule.c
    mp_scene_hero_watch.c
    mp_scene_host_report.c
    mp_scene_client.c
    mp_scene_free_rule.c
    mp_scene_free.c
    mp_movie_gate.c
    mp_host_settings_rule.c
    mp_host_settings.c
    mp_world_holds.c
    mp_levels.c
    mp_saves.c
    mp_savefile.c
    mp_savefile_disk.c
    mp_bridge_savefile.c
    mp_bridge_savefile_look.c
    mp_start.c
    mp_start_restore.c
    mp_start_wish.c
    mp_start_phase.c
    mp_settings_ini.c
    mp_menu_screen.c
    mp_menu_screens.c
    mp_menu_screens_join.c
    mp_menu_screens_host.c
    mp_menu_screens_players.c
    mp_menu_screens_lobby.c
    mp_menu_screens_band.c
    mp_menu_screens_choice.c
    mp_world.c
    mp_world_apply.c
    mp_world_digest.c
    mp_world_phase.c
    mp_world_state.c
    mp_actions_sabre.c
    mp_channel.c
    mp_channel_state.c
    mp_inbox.c
    mp_hold.c
    mp_session_hold.c
    mp_session_meter.c
    mp_budget_rule.c
    mp_state_note_rule.c
    mp_transport.c
    mp_loopback.c
    mp_session.c
    mp_session_packets.c
    mp_entropy.c
    mp_session_access.c
    mp_session_io.c
    mp_session_bulk.c
    mp_wallclock.c
    mp_wire.c
    mp_snapshot.c
    mp_snapshot_history.c
    mp_payload_prefix.c
    mp_command.c
    mp_error_smooth.c
    mp_rewind.c
    mp_clock.c
    mp_socket.c
    mp_signatures.c
    mp_signatures_enemy.c
    mp_signatures_foot.c
    mp_signatures_blade.c
    mp_signatures_puppet.c
    mp_cell_sites.c
    mp_lobby_sites.c
    mp_menu_sites.c
    mp_menu.c
    mp_cells.c
    mp_config.c
    mp_content.c
    mp_enemy_wire.c
    mp_scratch.c
    mp_enemy_bind.c
    mp_enemy_pose.c
    mp_clip_rule.c
    mp_clip_events.c
    mp_signatures_clip.c
    mp_enemy_fx.c
    mp_world_event.c
    mp_world_event_client.c
    mp_world_event_rule.c
    mp_script_sound.c
    mp_script_sound_client.c
    mp_script_sound_rule.c
    mp_signatures_script_sound.c
    mp_actor_loop.c
    mp_enemy_body.c
    mp_enemy_body_rule.c
    mp_enemy_nodes.c
    mp_director_rule.c
    mp_enemy_shield.c
    mp_enemy_shield_rule.c
    mp_enemy_limb.c
    mp_enemy_block.c
    mp_enemy_block_rule.c
    mp_enemy_burst.c
    mp_enemy_burst_rule.c
    mp_enemy_dead_watch.c
    mp_enemy_dead_watch_rule.c
    mp_enemy_dead_watch_client.c
    mp_enemy_pack.c
    mp_enemy_nodes_rule.c
    mp_enemy_limb_rule.c
    mp_enemy_blast.c
    mp_signatures_effects.c
    mp_enemy_spawn.c
    mp_enemy_relay.c
    mp_npc_shot.c
    mp_npc_shot_relay.c
    mp_npc_shot_zap.c
    mp_npc_shot_impact.c
    mp_signatures_shot_impact.c
    mp_shot_sites.c
    mp_arena.c
    mp_pickup_relay.c
    mp_player_sound.c
    mp_player_sound_play.c
    mp_player_sound_rule.c
    mp_signatures_player_sound.c
    mp_dialog.c
    mp_dialog_relay.c
    mp_signatures_dialog.c
    mp_voice.c
    mp_voice_report.c
    mp_voice_rule.c
    mp_voice_engine.c
    mp_voice_life.c
    mp_signatures_voice.c
    mp_voice_bind.c
    mp_own_body.c
    mp_quest.c
    mp_quest_relay.c
    mp_roster.c
    mp_bridge_roster.c
    mp_bridge_seats.c
    mp_bridge_lobby.c
    mp_bridge_lobby_late.c
    mp_bridge_announce.c
    mp_bridge_public.c
    mp_bridge_content.c
    mp_bridge_statement.c
    mp_mod_census.c
    mp_mod_folder.c
    mp_mod_allow.c
    mp_mod_manifest_rule.c
    mp_enemy_sync.c
    mp_enemy_sync_flush.c
    mp_enemy_queue_rule.c
    mp_enemy_sync_send.c
    mp_enemy_sync_choose.c
    mp_enemy_interest_rule.c
    mp_enemy_interest.c
    mp_enemy_sync_report.c
    mp_npc_copy_wire.c
    mp_npc_copies.c
    mp_npc_copies_client.c
    mp_npc_copies_run.c
    mp_npc_copies_run_host.c
    mp_npc_copies_run_client.c
    mp_npc_copies_bridge.c
    mp_hit_relay.c
    mp_hit_relay_death.c
    mp_knockback.c
    mp_knockback_rule.c
    mp_hit_wire.c
    mp_flash.c
    mp_reports.c
    mp_flash_rule.c
    mp_range_gate_rule.c
    mp_range_gate.c
    mp_world_anchor_rule.c
    mp_world_anchor.c
    mp_crate.c
    mp_crate_client.c
    mp_crate_host.c
    mp_crate_opener.c
    mp_crate_rule.c
    mp_crate_wire.c
    mp_signatures_crate.c
    mp_chat.c
    mp_chat_rule.c
    mp_chat_wire.c
    mp_chat_key_rule.c
    mp_chat_input.c
    mp_chat_layout.c
    mp_chat_draw.c
    mp_signatures_chat.c
    mp_stopwatch.c
    mp_cadence.c
    mp_level_switch.c
    mp_level_state.c
    mp_level_state_report.c
    mp_level_state_bind.c
    mp_level_state_count.c
    mp_level_state_rule.c
    mp_level_state_apply.c
    mp_level_state_fog.c
    mp_level_state_fog_rule.c
    mp_level_state_journal_rule.c
    mp_level_state_drawn.c
    mp_fog_viewers.c
    mp_fog_viewers_rule.c
    mp_session_now.c
    mp_footstep.c
    mp_node_map.c
    mp_hit_attacker.c
    mp_scratch_bind.c
    mp_scratch_wire.c
    mp_use_latch.c
    mp_target.c
    mp_target_rule.c
    mp_scene_watch.c
    mp_watchpost.c
    mp_udp.c
    mp_body_asset.c
    mp_body_spawn.c
    mp_body_wear.c
    mp_wear_rule.c
)

# Winsock, DELAY LOADED. A plain import would map ws2_32 into the process the moment this DLL is
# loaded, which is every launch, including the overwhelming majority where multiplayer is off. The
# section header in the ini says the feature touches nothing with the switch off, and an import
# table entry would quietly make that untrue. Delay loading means the library arrives on the first
# socket call and not before.
target_link_libraries(multiplayer PRIVATE ws2_32 bcrypt delayimp)
# The relay transport: HACL* for its handshake and record layer, Mbed TLS for the key document fetch
# (src/third_party). Both are static; nothing of them is loaded until public mode asks.
target_link_libraries(multiplayer PRIVATE mp_hacl mbedtls mbedx509 tfpsacrypto)
target_link_options(multiplayer PRIVATE /DELAYLOAD:ws2_32.dll /DELAYLOAD:bcrypt.dll)

# Stack cookies for the two programs that parse what strangers send. MSVC has /GS on by default
# and the root does not turn it off; it is named here so that no later root option can: a stack
# overrun in a decoder is the one place in this tree where that would be an exploit rather than
# a crash. MSVC takes the last of two contradicting options.
target_compile_options(multiplayer PRIVATE /GS)

# The dedicated server: NOT a mod and not a DLL, but the pure network layer plus the aggregation
# logic on a real socket, as a program with a window or, with --headless, a console. It loads no
# game and needs none. A player joins it from the ini, because the menu offers co-op only and
# cannot reach it: NetRole=2 (or OBI_NET_ROLE=client), NetAddress set to this machine's address
# and port, and GameMode=tdm. It lands next to dinput.dll in the dist folder.
add_executable(obi_dedicated
    multiplayer/server_main.c
    multiplayer/mp_roster.c
    multiplayer/mp_chat_rule.c
    multiplayer/mp_chat_wire.c
    multiplayer/mp_server.c
    multiplayer/mp_server_log.c
    multiplayer/server_config.c
    multiplayer/server_run.c
    multiplayer/server_gui.c
    multiplayer/mp_match.c
    multiplayer/mp_score.c
    multiplayer/mp_rules.c
    multiplayer/mp_lobby.c
    multiplayer/mp_death.c
    multiplayer/mp_events.c
    multiplayer/mp_trust.c
    multiplayer/mp_npc_copy_wire.c
    multiplayer/mp_enemy_wire.c
    multiplayer/mp_fieldset.c
    multiplayer/mp_session.c
    multiplayer/mp_session_packets.c
    multiplayer/mp_entropy.c
    multiplayer/mp_session_access.c
    multiplayer/mp_session_io.c
    multiplayer/mp_session_bulk.c
    multiplayer/mp_channel.c
    multiplayer/mp_channel_state.c
    multiplayer/mp_inbox.c
    multiplayer/mp_hold.c
    multiplayer/mp_session_hold.c
    multiplayer/mp_session_meter.c
    multiplayer/mp_budget_rule.c
    multiplayer/mp_state_note_rule.c
    multiplayer/mp_transport.c
    multiplayer/mp_snapshot.c
    multiplayer/mp_snapshot_history.c
    multiplayer/mp_payload_prefix.c
    multiplayer/mp_clock.c
    multiplayer/mp_wire.c
    multiplayer/mp_udp.c
    multiplayer/mp_socket.c
    multiplayer/mp_wallclock.c
)
target_include_directories(obi_dedicated PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/multiplayer)
target_link_libraries(obi_dedicated PRIVATE engine_fixes_common ws2_32 comctl32 bcrypt)
target_compile_options(obi_dedicated PRIVATE /GS)
# The window subsystem, and the console shape asks for a console rather than the other way round:
# a console program that opens a window leaves an empty black rectangle behind it for the life of
# the session. See the head of server_main.c.
set_target_properties(obi_dedicated PROPERTIES WIN32_EXECUTABLE TRUE)
set_target_properties(obi_dedicated PROPERTIES
    RUNTIME_OUTPUT_DIRECTORY ${ENGINE_FIX_OUTPUT}
    RUNTIME_OUTPUT_DIRECTORY_DEBUG ${ENGINE_FIX_OUTPUT}
    RUNTIME_OUTPUT_DIRECTORY_RELEASE ${ENGINE_FIX_OUTPUT}
)
