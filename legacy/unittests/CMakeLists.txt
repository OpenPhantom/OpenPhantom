# Ordinary console programs, so they run without the game and ctest can drive them.
#
# Each one is unittests/<module>.c and is built against the REAL module rather than a stub of it,
# because a stub only proves the stub. unittest.c is the shared pass and fail harness.
#
# add_unit_test(<module> <feature directory under src/mods, or "" for common> [extra sources...])

set(MODS ${CMAKE_SOURCE_DIR}/src/mods)

function(add_unit_test name module_dir)
    set(sources "")
    foreach(source ${ARGN})
        list(APPEND sources ${MODS}/${source})
    endforeach()

    add_executable(${name}_test ${name}.c unittest.c ${sources})
    if(NOT module_dir STREQUAL "")
        target_include_directories(${name}_test PRIVATE ${MODS}/${module_dir})
    endif()
    target_link_libraries(${name}_test PRIVATE engine_fixes_common)
    # Each test binary gets a directory of its own. The settings file is read and written beside
    # the host executable, so every test that goes through ini.c, directly or through a module
    # that persists a row, shared one engine_fixes.ini while they all sat in one directory, and a
    # parallel ctest run had them rewriting it under each other. The multi-config generators add
    # their configuration name below this.
    set_target_properties(${name}_test PROPERTIES
        RUNTIME_OUTPUT_DIRECTORY ${CMAKE_CURRENT_BINARY_DIR}/${name})
    add_test(NAME ${name} COMMAND ${name}_test)
endfunction()


add_unit_test(fov_math   variable_fov      variable_fov/fov_math.c)

# The shared thumbstick deadzone, which two DLLs both drive and neither had a test for. The corner
# case is the point: a square range reports 1.41 at a true diagonal, and every caller reads what
# comes back as a speed.
add_unit_test(stick "")

# The bounded formatter. Truncation is the whole test: the toolchain's own call neither terminates
# nor says when it has cut.
add_unit_test(text "")

# The clamp and the finite test, header only, so the test is the only translation unit.
add_unit_test(numeric "")

# The music volume latch: the two-deep rule that decides what a provider change puts back, in the
# file the hooks feed. The hooks themselves need a patched process and are not linked.
add_unit_test(music_volume imuse_fix imuse_fix/music_volume_latch.c)
add_unit_test(sound_owner sound_lifetime_fix sound_lifetime_fix/sound_lifetime_fix.c)
add_unit_test(hud_layout hud_ratio_scaling hud_ratio_scaling/hud_layout.c)

# The damper, the mouse bank and the travel angle together: all three are the arithmetic that
# decides how this feature feels. free_look_math.c is here and free_look.c deliberately is not,
# because the arithmetic has no engine in it, so the test needs neither the detours nor the
# resolved sites.
add_unit_test(strafe_walk enhanced_input
    enhanced_input/strafe_walk.c
    enhanced_input/free_look_math.c
)

# The mouse step gets its own test rather than more cases in the one above, because it checks a
# different kind of claim: not "this formula is right" but "no input is ever lost", which only
# shows up over a run of drains with uneven frame times.
add_unit_test(mouse_look enhanced_input
    enhanced_input/mouse_look.c
    enhanced_input/frame_clock.c
    enhanced_input/input_gate.c
    enhanced_input/mouse_config.c
    enhanced_input/input_slider.c
    enhanced_input/mouse_rate.c
    enhanced_input/raw_mouse.c
    enhanced_input/input_foreground.c
    enhanced_input/menu_cursor.c
)

# The rate estimator on its own: the boundary correction that divides by what the reports
# REPRESENT rather than by how long they took to arrive.
add_unit_test(mouse_rate enhanced_input enhanced_input/mouse_rate.c)

# The per-frame view lead. What is proven here is a property of a SEQUENCE, not of one call: the
# drawn angle must advance by one frame of hand movement on every frame while the body goes on
# turning once per simulation step. It carries the first design as a regression, because that one
# adds the bank without cancelling the interpolation and turns the camera backwards once per step.
add_unit_test(view_lead enhanced_input
    enhanced_input/view_lead.c
    enhanced_input/free_look_math.c
    enhanced_input/raw_mouse.c
    enhanced_input/input_foreground.c
)

# Whether the delivery survives a SLOW MOUSE, which is the question a field test on one desk
# cannot answer: the per-frame path is at the mercy of how many device reports land in a frame.
add_unit_test(delivery_rates enhanced_input
    enhanced_input/mouse_rate.c
)

# pointer_release.c comes along because cursor_anchor asks it whether the player has taken the
# pointer before it re-centres. It is the real file rather than a stub: a stub would only prove
# that the stub returns false.
add_unit_test(cursor_anchor enhanced_resolution enhanced_resolution/cursor_anchor.c
                                                enhanced_resolution/pointer_release.c)
add_unit_test(window_fit    enhanced_resolution enhanced_resolution/window_fit.c)

# All of the window mode's policy: what shape is asked for, given a monitor and the size
# the engine is rendering. window_fit.c comes along because window_mode.c asks it for that
# size, and the rule here is to link the real module rather than a stub.
add_unit_test(window_mode   enhanced_resolution enhanced_resolution/window_mode.c
    enhanced_resolution/window_fit.c
)
add_unit_test(focus_guard   enhanced_resolution
    enhanced_resolution/focus_guard.c
    # asked every frame whether the player has taken the pointer, so the confinement can stand
    # down while they use the window frame
    enhanced_resolution/pointer_release.c
    enhanced_resolution/window_fit.c
)

# The menu cursor cage: only its arithmetic is testable without a display, and the property worth
# proving is that at 640x480 it computes exactly the constants the engine ships with. window_fit.c
# comes along because pointer_cage.c asks it for the display mode, and the DLL has one owner for
# that question.
add_unit_test(pointer_cage enhanced_resolution
    enhanced_resolution/pointer_cage.c
    enhanced_resolution/window_fit.c
)

# The island clamp: the arithmetic that decides where a menu sprite may land, and which sprites the
# clamp is defined for at all. The case worth its name reproduces the reported defect's own numbers:
# at 3840x2160 the hovered button's halo is cut exactly at island x=1600 instead of stamping the
# border. window_fit.c comes along for the same reason it does above: the hook asks it for the
# display mode.
add_unit_test(menu_art_resample enhanced_resolution enhanced_resolution/menu_art_resample.c)

# Whether the canvas a screen was authored for can be recovered by dividing, the test that decides
# whether the rectangle shadow has to keep it. The rest of the refit is engine writes and is not
# testable here; the rounding is, and the rounding is where the answer turned out to be no.
add_unit_test(menu_refit enhanced_resolution
    enhanced_resolution/menu_scale.c
    # the two halves of the same feature: menu_scale.c's hooks call into both
    enhanced_resolution/menu_scale_install.c
    enhanced_resolution/menu_scale_refit.c
    enhanced_resolution/menu_scale_3d.c
    enhanced_resolution/menu_scale_sites.c
    enhanced_resolution/menu_preview.c
    enhanced_resolution/menu_art_census.c
    enhanced_resolution/menu_art_load.c
    enhanced_resolution/menu_art_resample.c
    enhanced_resolution/menu_art_source.c
    enhanced_resolution/menu_loading_bar.c
    enhanced_resolution/menu_island_clip.c
    enhanced_resolution/pointer_cage.c
    enhanced_resolution/window_fit.c
    # the queryFont hook asks the subtitle layout whether a caller is its own line height call;
    # unresolved in a test process, so it answers false and the hook takes the menu gate
    enhanced_resolution/subtitle_scale.c
)

add_unit_test(menu_island_clip enhanced_resolution
    enhanced_resolution/menu_island_clip.c
    # the census the sprite hook reports to, the real file rather than a stub, and the site
    # table it resolves its own detours from
    enhanced_resolution/menu_art_census.c
    enhanced_resolution/menu_scale_sites.c
    enhanced_resolution/window_fit.c
)

add_unit_test(fog_regime view_distance_fix view_distance_fix/fog_regime.c
    view_distance_fix/fog_band.c
    view_distance_fix/fog_regime_install.c
    view_distance_fix/fog_trace.c
    view_distance_fix/cell_watchdog.c)

# The only encoder here that writes a whole instruction sequence rather than an operand, so it is
# the only one whose mistakes are invisible at run time.
add_unit_test(camera_anchor framerate_fix framerate_fix/camera_compensation.c)

# The predicate that decides whether the facing command's completion is withheld. It can be wrong
# in either direction and neither shows up at run time: too rare and the scripted scene never
# progresses, too often and a clip that is still moving is delayed.
# substep_counter.c comes along because face_latch.c asks it which simulation step this is rather
# than resolving the counter itself. The real file, not a stub: in a test process it resolves
# nothing and answers "no counter", which is the case the install path has to survive.
add_unit_test(face_latch framerate_fix framerate_fix/face_latch.c
    framerate_fix/substep_counter.c
)

# The verdict that separates a gameplay window from a menu window. The counter it reports on is
# shared with the menu, so before this existed the instrument printed a frame rate as a substep
# rate, and an open item was raised against the simulation on the strength of it.
# sim_clock.c comes with it: the window has to add back whatever the rebase took off the clock it
# samples, so the two files are one measurement and linking only half of them does not build.
add_unit_test(framerate_stats framerate_fix framerate_fix/framerate_stats.c
              framerate_fix/sim_clock.c framerate_fix/world_clock.c
              framerate_fix/frame_cap.c)

# What a moving door looks like between two simulation steps. Pure arithmetic, and every way it
# can be wrong is silent in review and loud on screen: a destroyed scale flickers the model, a
# guard measured on unnormalised rows either never smooths a shrunk subnode or never rejects a
# jump on a grown one, and a skewed basis shears the geometry.
add_unit_test(mover_blend framerate_fix framerate_fix/mover_blend.c)

# Whether a mover that ran off the end of its track looped or reset, judged against its own
# ordinary tick. A loop drawn as a reset is a hitch once a revolution on the fastest thing in the
# scene; a reset drawn as a loop sweeps a piston backwards across a substep.
add_unit_test(mover_wraps framerate_fix framerate_fix/mover_wraps.c)

# The side table that remembers a mover's previous pose. For weeks nothing gave a slot back, and
# the third level of a session was drawn stepped because the first two still owned the table; the
# emptying on a level opening and the aging of a slot are the two ways back, and both are pinned.
add_unit_test(mover_slots framerate_fix framerate_fix/mover_slots.c framerate_fix/mover_evenness.c)

# The instrument that judges whether a mover draws evenly. Two earlier versions of it were wrong in
# ways a play session could not reveal, and both wrong answers were believed: one measured a speed
# range rather than jitter, and one reset its history on the second of the two calls each subnode
# gets per frame and reported nothing at all from a real session.
add_unit_test(mover_evenness framerate_fix framerate_fix/mover_evenness.c)

# Which render cap to use, given a setting and a display. Three lines, and the reason they matter
# took a whole evening: nothing in the shipped stack ties produced frames to shown ones, so a cap
# below the refresh rate judders however correct the interpolation is.
add_unit_test(frame_cap framerate_fix framerate_fix/frame_cap.c)

# The one claim the simulation clock rebase stands on: taking the same amount off both clocks must
# leave their difference exactly, because that difference IS the interpolation alpha. A rebase that
# rounds would inject a fresh error into the quantity it exists to protect, and nothing on screen
# would say so.
add_unit_test(sim_clock framerate_fix framerate_fix/sim_clock.c
    # the un-clamping shares this file's one detour, so it comes along with it
    framerate_fix/world_clock.c
    # a level opening restarts the cap's fraction, and the clock going backwards is where it is read
    framerate_fix/frame_cap.c
)

# Removing the substep loop's clamp of the world clock. The clamp put a mover's sample pair on
# the frame lattice while the alpha blending it was a phase within a simulation step. Three
# attempts to compensate for that in the draw failed, so the cause is being removed instead. The
# lattice walk is the claim: one substep per call, exactly, at any frame rate.
add_unit_test(world_clock framerate_fix framerate_fix/world_clock.c)

# The one predicate that decides whether a call is let into the music DLL. Wrong in either
# direction and the failure is silent.
add_unit_test(imuse_guard imuse_fix imuse_fix/imuse_guard.c)

# Which file on disk a movie name turns into. The only part of fmv_player with an answer a test can
# check: the rest of it is a window, a decoder and a message loop. Both ways of getting it wrong
# are silent, and one of them asks the file system about a file the player never named.
add_unit_test(movie_path fmv_player fmv_player/movie_path.c)

# The patcher is common code; enhanced_input is named so the test reads controls_layout.h, the
# one set of numbers the controls screen's group is laid out from and checked against.
add_unit_test(menu_patcher enhanced_input)

# The chained detours actually get installed and called through here, on a page of machine code the
# test writes itself. The property that matters cannot be seen in the struct fields: a second
# detour that copied the first one's branch would report success and then loop forever.
# The write guards: the range check and the read back. It writes into the test process rather
# than the game, which is enough to prove a refusal refuses and a write lands.
# The guarded reads. The case that matters is the refusal: the trying form has to answer no for
# an address this process does not own rather than take the program down.
# The settings file, driven against a real one written beside the test binary. Every module
# reads through this, and the parsing underneath is the platform is own.
add_unit_test(ini "")

add_unit_test(memory "")

add_unit_test(patch "")

add_unit_test(detour "")

# The pattern matcher every patch in the project stands on.
add_unit_test(signature "")

# Everything effect_clock answers the engine with in place of a random draw. Wrong in the range
# and the engine's own scaling runs off the end; wrong in the mixing and either every flickering
# object in a room goes dark together or an effect freezes instead of being paced.
add_unit_test(substep_noise effect_clock effect_clock/substep_noise.c)

# The two bounds that are the whole point of render_guard. Both are comparisons, and every way
# they can be wrong is silent in review: too tight and authored geometry is refused, too loose
# and the write that overruns still happens.
add_unit_test(face_bounds render_guard render_guard/face_bounds.c)

# Which requested texture ceiling is accepted, clamped or refused. The engine never asks the
# device for its own maximum, so this validation is the only thing between a setting and a
# surface creation that fails.
add_unit_test(texture_size large_textures large_textures/texture_size.c)

# The overlay's model: which rows are on screen, and the search that decides it. Built against the
# real cheat sources rather than stubs, so the test also pins down what the panel looks like on an
# executable neither of them could resolve.
add_unit_test(overlay_model dev_overlay
    dev_overlay/overlay_model.c
    # the groups whose rows the model dispatches to, the same as the Utilities group below
    dev_overlay/overlay_framerate.c
    dev_overlay/overlay_window.c
    dev_overlay/fog_band_row.c
    dev_overlay/view_range_live_row.c
    dev_overlay/overlay_key_name.c
    dev_overlay/overlay_utilities.c
    dev_overlay/open_key_row.c
    dev_overlay/fog_follow_row.c
    dev_overlay/auto_range_row.c
    dev_overlay/dismemberment_row.c
    dev_overlay/fov_row.c
    dev_overlay/menu_extras_row.c
    dev_overlay/sensitivity_row.c
    dev_overlay/free_look_row.c
    dev_overlay/strafe_row.c
    dev_overlay/air_control_row.c
    dev_overlay/camera_follow_row.c
    dev_overlay/strict_range_row.c
    dev_overlay/view_range_row.c
    dev_overlay/dev_menu_size_row.c
    dev_overlay/subtitle_size_row.c
    dev_overlay/cheats_original.c
    dev_overlay/cheats_original_actions.c
    dev_overlay/cheats_openphantom.c
    # The cheats were split by responsibility when cheats_openphantom.c passed twice the size this
    # project allows. They still share one state record, so the test links them all together.
    dev_overlay/cheats_npc_damage.c
    dev_overlay/cheats_jump_boost.c
    dev_overlay/cheats_fall_consequences.c
    dev_overlay/cheats_noclip.c
    dev_overlay/cheats_free_camera.c
    dev_overlay/cheats_no_fog.c
    # Both jump boost and the free camera ask the same "is there a floor under this" question,
    # so the module that owns it has to be linked beside them. It resolves nothing in a test
    # process either, and answers UNAVAILABLE, which is the case both callers must survive.
    dev_overlay/floor_probe.c
    # The cell the player is read through. It resolves nothing in a test process, so every
    # cheat that reads the player is left unavailable, which is the case the panel has to show.
    dev_overlay/player_slot.c
    # cheats_openphantom.c calls sim_pause_hold() rather than writing the engine's pause cell itself,
    # so the owner of that cell has to be linked in beside it. Nothing here calls into it: without a
    # resolved site every entry point returns immediately, as a test process wants.
    dev_overlay/sim_pause.c
)

# The character census reads a live process, so what can be tested without the game is the
# arithmetic underneath it: the pool slot walk, the radius test, the vertical classification and
# the fixed width name field. character_scan.c holds exactly that and no more.
add_unit_test(character_scan diagnostics diagnostics/character_scan.c)

# The debug register bit packing. Getting a condition or a length wrong does not fail loudly, it
# arms a watch that never fires, which reads as "nothing writes this field". Worth pinning to the
# values the manual specifies.
add_unit_test(debug_register diagnostics diagnostics/debug_register.c)

# The one decision the ground clip fix makes. The modes it is checked against were read out of a
# live level rather than invented, and the case that must NOT change, a collision tested character
# keeping its velocity, matters as much as the case that must.
add_unit_test(move_mode ground_clip_fix ground_clip_fix/move_mode.c)

add_unit_test(creeping_mover ground_clip_fix ground_clip_fix/creeping_mover.c)

# The camera hand-back's one decision, and it is mostly about what it refuses. Being too
# permissive here does not fail loudly, it takes the camera off a cutscene or a death shot,
# which reads as the engine misbehaving rather than as this module doing it.
add_unit_test(handback_rule camera_handback_fix camera_handback_fix/handback_rule.c)

# Whether the pad stick may drive at all. It runs every substep of every session, so the
# unresolved case is the one that matters: too strict there is not a subtle fault, it is a
# player who cannot move.
add_unit_test(input_mode enhanced_input enhanced_input/input_mode.c)

# The frame-time governor's one decision. The dead zone between its two thresholds is the property
# that stops it oscillating, and it is a relationship between two settings rather than anything
# either says alone, so it is worth a test of its own.
add_unit_test(frame_governor view_distance_fix view_distance_fix/frame_governor.c)

# Whether a small push on the right stick moves the view at all. One poll of it is worth less
# than a whole mouse count, and SendInput only takes whole ones, so the fraction owed has to
# survive from one poll to the next or the stick is dead until it is pushed a long way.
add_unit_test(look_counts controller_input controller_input/look_counts.c)

# Where each drawn object was a simulation step ago. The engine keeps that beside the current
# position and blends between the two, but a platform's carry overwrites it with the current
# one, so a rider has nothing to blend. What is checked here is a property of a SEQUENCE: the
# same answer for every frame inside a step, moving on exactly once when the step does.
add_unit_test(object_track framerate_fix framerate_fix/object_track.c)

# Whether a carried character is drawn smoothly at any frame rate a player might set, rather than
# only at the one it was played at. The same arithmetic over a run of frames at nine rates, above
# and below the simulation's own 32, and once more with the frame times jittered. What it pins
# down is the invariant: the drawn moment trails the simulation by exactly one step and advances
# evenly, at every one of them.
add_unit_test(rate_independence framerate_fix framerate_fix/object_track.c)

# Naming a virtual key, which the open-key row shows and the ini round-trips.
add_unit_test(overlay_key_name dev_overlay dev_overlay/overlay_key_name.c)

# The draw distance row's own arithmetic: the clamp, and the hand written parser that exists
# because strtof would read a decimal point only where the locale agrees it is one.
add_unit_test(view_range_row dev_overlay dev_overlay/view_range_row.c)

# The field of view row, which is the only one with a slider, so its ends and its rounding are
# where a handle that cannot reach the maximum would show up.
add_unit_test(fov_row dev_overlay dev_overlay/fov_row.c dev_overlay/view_range_row.c)
