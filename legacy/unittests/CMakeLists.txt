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
add_unit_test(hud_layout hud_ratio_scaling hud_ratio_scaling/hud_layout.c)

# The damper, the mouse bank and the travel angle together: all three are the arithmetic that
# decides how this feature feels. free_look_math.c is here and free_look.c deliberately is not,
# because the arithmetic has no engine in it, so the test needs neither the detours nor the
# resolved sites.
add_unit_test(strafe_walk enhanced_input
    enhanced_input/strafe_walk.c
    enhanced_input/mouse_look.c
    enhanced_input/free_look_math.c
)

# The mouse step gets its own test rather than more cases in the one above, because it checks a
# different kind of claim: not "this formula is right" but "no input is ever lost", which only
# shows up over a run of drains with uneven frame times.
add_unit_test(mouse_look enhanced_input enhanced_input/mouse_look.c)

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

add_unit_test(fog_regime view_distance_fix view_distance_fix/fog_regime.c)

# The only encoder here that writes a whole instruction sequence rather than an operand, so it is
# the only one whose mistakes are invisible at run time.
add_unit_test(camera_anchor framerate_fix framerate_fix/camera_compensation.c)

# The predicate that decides whether the facing command's completion is withheld. It can be wrong
# in either direction and neither shows up at run time: too rare and the scripted scene never
# progresses, too often and a clip that is still moving is delayed.
add_unit_test(face_latch framerate_fix framerate_fix/face_latch.c)

# The one predicate that decides whether a call is let into the music DLL. Wrong in either
# direction and the failure is silent.
add_unit_test(imuse_guard imuse_fix imuse_fix/imuse_guard.c)

add_unit_test(menu_patcher "")

# The chained detours actually get installed and called through here, on a page of machine code the
# test writes itself. The property that matters cannot be seen in the struct fields: a second
# detour that copied the first one's branch would report success and then loop forever.
add_unit_test(detour "")

# The pattern matcher every patch in the project stands on.
add_unit_test(signature "")
