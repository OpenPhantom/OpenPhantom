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
    add_test(NAME ${name} COMMAND ${name}_test)
endfunction()


add_unit_test(fov_math   variable_fov      variable_fov/fov_math.c)
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
    enhanced_input/input_gate.c
    enhanced_input/mouse_config.c
    enhanced_input/input_slider.c
    enhanced_input/mouse_rate.c
    enhanced_input/raw_mouse.c
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
)

# Whether the delivery survives a SLOW MOUSE, which is the question a field test on one desk
# cannot answer: the per-frame path is at the mercy of how many device reports land in a frame.
add_unit_test(delivery_rates enhanced_input
    enhanced_input/mouse_rate.c
)

add_unit_test(cursor_anchor enhanced_resolution enhanced_resolution/cursor_anchor.c)
add_unit_test(window_fit    enhanced_resolution enhanced_resolution/window_fit.c)
add_unit_test(focus_guard   enhanced_resolution
    enhanced_resolution/focus_guard.c
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
add_unit_test(menu_island_clip enhanced_resolution
    enhanced_resolution/menu_island_clip.c
    enhanced_resolution/window_fit.c
)

add_unit_test(fog_regime view_distance_fix view_distance_fix/fog_regime.c)

# The only encoder here that writes a whole instruction sequence rather than an operand, so it is
# the only one whose mistakes are invisible at run time.
add_unit_test(camera_anchor framerate_fix framerate_fix/camera_compensation.c)

# The predicate that decides whether the facing command's completion is withheld. It can be wrong
# in either direction and neither shows up at run time: too rare and the scripted scene never
# progresses, too often and a clip that is still moving is delayed.
add_unit_test(face_latch framerate_fix framerate_fix/face_latch.c)

# The verdict that separates a gameplay window from a menu window. The counter it reports on is
# shared with the menu, so before this existed the instrument printed a frame rate as a substep
# rate, and an open item was raised against the simulation on the strength of it.
add_unit_test(framerate_stats framerate_fix framerate_fix/framerate_stats.c)

# What a moving door looks like between two simulation steps. Pure arithmetic, and every way it
# can be wrong is silent in review and loud on screen: a destroyed scale flickers the model, a
# guard measured on unnormalised rows either never smooths a shrunk subnode or never rejects a
# jump on a grown one, and a skewed basis shears the geometry.
add_unit_test(mover_blend framerate_fix framerate_fix/mover_blend.c)

# The one claim the simulation clock rebase stands on: taking the same amount off both clocks must
# leave their difference exactly, because that difference IS the interpolation alpha. A rebase that
# rounds would inject a fresh error into the quantity it exists to protect, and nothing on screen
# would say so.
add_unit_test(sim_clock framerate_fix framerate_fix/sim_clock.c)

# The one predicate that decides whether a call is let into the music DLL. Wrong in either
# direction and the failure is silent.
add_unit_test(imuse_guard imuse_fix imuse_fix/imuse_guard.c)

# Which file on disk a movie name turns into. The only part of fmv_player with an answer a test can
# check: the rest of it is a window, a decoder and a message loop. Both ways of getting it wrong
# are silent, and one of them asks the file system about a file the player never named.
add_unit_test(movie_path fmv_player fmv_player/movie_path.c)

add_unit_test(menu_patcher "")

# The chained detours actually get installed and called through here, on a page of machine code the
# test writes itself. The property that matters cannot be seen in the struct fields: a second
# detour that copied the first one's branch would report success and then loop forever.
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
    dev_overlay/view_range_row.c
    dev_overlay/dev_menu_size_row.c
    dev_overlay/cheats_original.c
    dev_overlay/cheats_original_actions.c
    dev_overlay/cheats_openphantom.c
    # The cheats were split by responsibility when cheats_openphantom.c passed twice the size this
    # project allows. They still share one state record, so the test links all four together.
    dev_overlay/cheats_npc_damage.c
    dev_overlay/cheats_jump_boost.c
    dev_overlay/cheats_free_camera.c
    dev_overlay/cheats_no_fog.c
    # Both jump boost and the free camera ask the same "is there a floor under this" question,
    # so the module that owns it has to be linked beside them. It resolves nothing in a test
    # process either, and answers UNAVAILABLE, which is the case both callers must survive.
    dev_overlay/floor_probe.c
    # cheats_openphantom.c calls sim_pause_hold() rather than writing the engine's pause cell itself,
    # so the owner of that cell has to be linked in beside it. Nothing here calls into it: without a
    # resolved site every entry point returns immediately, which is exactly what a test process wants.
    dev_overlay/sim_pause.c
)

# The character census reads a live process, so what can be tested without the game is the
# arithmetic underneath it: the pool slot walk, the radius test, the vertical classification and
# the fixed width name field. character_scan.c holds exactly that and nothing else.
add_unit_test(character_scan diagnostics diagnostics/character_scan.c)

# The debug register bit packing. Getting a condition or a length wrong does not fail loudly, it
# arms a watch that never fires, which reads as "nothing writes this field". Worth pinning to the
# values the manual specifies.
add_unit_test(debug_register diagnostics diagnostics/debug_register.c)

# The one decision the ground clip fix makes. The modes it is checked against were read out of a
# live level rather than invented, and the case that must NOT change, a collision tested character
# keeping its velocity, matters as much as the case that must.
add_unit_test(move_mode ground_clip_fix ground_clip_fix/move_mode.c)

# The draw distance row's own arithmetic: the clamp, and the hand written parser that exists
# because strtof would read a decimal point only where the locale agrees it is one.
add_unit_test(view_range_row dev_overlay dev_overlay/view_range_row.c)
