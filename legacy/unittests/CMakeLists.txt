# Ordinary console programs, so they run without the game and ctest can drive them.
#
# Each one is unittests/<module>.c and is built against the REAL module rather than a stub of it,
# because a stub only proves the stub. unittest.c is the shared pass and fail harness.
#
# add_unit_test(<module> <feature directory under src/mods, or "" for common> [extra sources...])

set(MODS ${CMAKE_SOURCE_DIR}/src/mods)

# The program on its own, with no ctest entry. add_unit_test below is this plus the entry;
# overlay_dump prints rather than checks and has no entry to make.
function(build_test_program name module_dir)
    set(sources "")
    foreach(source ${ARGN})
        # A source named under unittests/ is one of this directory's own, shared between two
        # programs; everything else is a module source under src/mods.
        if(source MATCHES "^unittests/")
            string(REPLACE "unittests/" "" own ${source})
            list(APPEND sources ${CMAKE_CURRENT_SOURCE_DIR}/${own})
        else()
            list(APPEND sources ${MODS}/${source})
        endif()
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
endfunction()

function(add_unit_test name module_dir)
    build_test_program("${name}" "${module_dir}" ${ARGN})
    add_test(NAME ${name} COMMAND ${name}_test)
endfunction()


add_unit_test(fov_math   variable_fov      variable_fov/fov_math.c)
# How often the frame poll asks the settings file whether ExtraDegrees moved: the period, the first
# look and the tick count's wrap.
add_unit_test(fov_poll   variable_fov      variable_fov/fov_poll.c)

# The shared thumbstick deadzone, which two DLLs both drive and neither had a test for. The corner
# case is the point: a square range reports 1.41 at a true diagonal, and every caller reads what
# comes back as a speed.
add_unit_test(stick "")

# The bounded formatter. Truncation is the whole test: the toolchain's own call neither terminates
# nor says when it has cut.
add_unit_test(text "")

# The clamp and the finite test, header only, so the test is the only translation unit.
add_unit_test(numeric "")

# dialogue_anim_fix's rest rule, header only: whether a speaker's body may be moved, asked of the
# actor behind it, with the rule that asked the clip's name alone held beside it as the reference.
add_unit_test(dialogue_rest_rule dialogue_anim_fix)

# The music volume latch: the two-deep rule that decides what a provider change puts back, in the
# file the hooks feed. The hooks themselves need a patched process and are not linked.
add_unit_test(music_volume imuse_fix imuse_fix/music_volume_latch.c)
add_unit_test(sound_owner sound_lifetime_fix sound_lifetime_fix/sound_lifetime_fix.c)
add_unit_test(hud_layout hud_ratio_scaling hud_ratio_scaling/hud_layout.c)
# The NPC spawner's compiled scripts are data; the checks are on the data's shape, and the clip
# filling is refused without the archive, which a test process never has beside it.
add_unit_test(spawn_scripts dev_overlay dev_overlay/spawn_scripts.c dev_overlay/actor_catalog.c
    dev_overlay/entity_offer.c dev_overlay/entity_names.c)
# The same, with the installed game's archive where there is one: the clipless file refused and a
# pickup's still script, the two claims the data alone cannot show.
add_unit_test(spawn_scripts_archive dev_overlay dev_overlay/spawn_scripts.c
    dev_overlay/actor_catalog.c dev_overlay/entity_offer.c dev_overlay/entity_names.c)
# Without the game the program exits 77 and says why, and ctest shows it skipped, not passed.
set_tests_properties(spawn_scripts_archive PROPERTIES SKIP_RETURN_CODE 77)
# The panel's own savegame block, as bytes: the layout, the torn and the foreign, and the round
# trip fuzzed.
add_unit_test(npc_spawn_block dev_overlay dev_overlay/npc_spawn_block.c)
# Nodes out of an engine list for the length of one call, and back where they were.
add_unit_test(npc_spawn_list dev_overlay dev_overlay/npc_spawn_list.c)
# The record a spawned copy is raised from, settled over its source.
add_unit_test(npc_spawn_record dev_overlay dev_overlay/npc_spawn_record.c
    dev_overlay/entity_offer.c dev_overlay/entity_names.c)
# Riding a copy, reading a block, and when a loaded block raises its copies.
add_unit_test(npc_spawn_rules dev_overlay dev_overlay/npc_spawn_rules.c)
# The panel's half of the spawn contract in a session: wishes, grants, epochs, with the engine
# faked.
add_unit_test(npc_spawn_session dev_overlay dev_overlay/npc_spawn_session.c)
# The entity spawner's offer: the rule over made-up facts and over every file of the retail game,
# whose facts are generated into the table the rule reads.
add_unit_test(entity_offer dev_overlay dev_overlay/entity_offer.c dev_overlay/entity_names.c)
# What a loaded level offers as its own, counted over a level record laid out in memory.
add_unit_test(npc_census dev_overlay dev_overlay/npc_census.c dev_overlay/entity_offer.c
    dev_overlay/entity_names.c)
# The placement mode: its state, the turn, the click lock, where the pointer puts the entity.
add_unit_test(spawn_place dev_overlay dev_overlay/spawn_place.c)
# Why the entity spawner cannot be used: the one rule the group's rows, the mode and the log read.
add_unit_test(spawn_reason dev_overlay dev_overlay/spawn_reason.c)
# A group's rows written as a table: what each kind of entry draws, and above all the count, since
# a number entry is two rows and a miscount moves the slot numbers session_lock.c addresses.
add_unit_test(overlay_kit dev_overlay dev_overlay/overlay_kit.c dev_overlay/overlay_row_fill.c
    dev_overlay/overlay_number.c dev_overlay/overlay_reason.c)
# A number row's arithmetic on its own: where a fraction of a track lands, where a value sits
# along it, which fraction one press away is, where Default puts the handle, and what the end
# of a track row gives up first when the panel is narrow. Five floats in, an answer out, so
# the values can be pinned absolutely rather than by two paths agreeing with each other.
add_unit_test(overlay_number dev_overlay dev_overlay/overlay_number.c)
# The one way a dragged track reaches the settings file: who holds it, what the drawing is told,
# and which of the hand's moves get through the throttle. The model is stubbed in the test; it is
# the far side of the seam.
add_unit_test(overlay_slider dev_overlay dev_overlay/overlay_slider.c
    dev_overlay/overlay_number.c
    # the band a refused write puts up, which is the one answer all seven hands share
    dev_overlay/overlay_notice.c)
# Which rows a running session takes, over the real session note, and the one group it leaves
# alone while the multiplayer runs the NPC copies.
add_unit_test(session_lock dev_overlay dev_overlay/session_lock.c dev_overlay/overlay_reason.c)
# What the panel's footer says: the four key caps, the tally of the open tab, the session that
# replaces it, and what the line gives up when the panel is too narrow to hold all of it.
# overlay_layout.c is here because the footer's width question cannot be asked without it: how
# much room the band actually gets on a given screen at a given dev menu size is the layout's
# answer, and a test that supplies its own room can only check the rule against itself.
add_unit_test(overlay_legend dev_overlay dev_overlay/overlay_legend.c dev_overlay/session_lock.c
    dev_overlay/overlay_reason.c dev_overlay/overlay_layout.c dev_overlay/overlay_notice.c)
# The sentence the panel puts up when it turns something down, and the one rule that matters about
# it: nothing here reads a clock, because the band costs height and a band that went away by
# itself would move the row under a pointer that had not moved.
add_unit_test(overlay_notice dev_overlay dev_overlay/overlay_notice.c)
# The camera arithmetic, there and back, and which way the view matrix is read.
add_unit_test(world_pick dev_overlay dev_overlay/world_pick.c dev_overlay/character_prop_math.c
    dev_overlay/spawn_place.c)
# The preview's render handle over a pretend engine: bound, drawn, let go once, never late.
add_unit_test(spawn_ghost dev_overlay dev_overlay/spawn_ghost.c)
# Who has the pointer, the wheel and the pause; the panel and the camera stood up in the test.
add_unit_test(input_owner dev_overlay dev_overlay/input_owner.c dev_overlay/spawn_place.c)
# The placement mode's two keys and the file they live in.
add_unit_test(spawn_keys dev_overlay dev_overlay/spawn_keys.c dev_overlay/overlay_key_name.c
    dev_overlay/overlay_notice.c)
# The marks' boxes, and a scene with no screen to draw on.
add_unit_test(spawn_marks dev_overlay dev_overlay/spawn_marks.c)
# The words the placement mode puts at the top of the picture, which is the only half of it a
# test can see: the drawing needs a screen and the counters need a running mode.
add_unit_test(spawn_banner dev_overlay dev_overlay/spawn_banner.c)
# The placement mode's own frame against a stand in for the engine: the held world and its settle,
# the death, the refusal row, the right click, and two clicks in one frame.
add_unit_test(spawn_mode dev_overlay dev_overlay/spawn_mode.c dev_overlay/spawn_banner.c
    dev_overlay/spawn_place.c
    dev_overlay/spawn_reason.c
    dev_overlay/spawn_ghost.c dev_overlay/spawn_keys.c dev_overlay/overlay_key_name.c
    dev_overlay/overlay_notice.c
    dev_overlay/input_owner.c)
# The pad's deadzone and its hold that fires once; the pad itself is never polled here.
add_unit_test(pad_input dev_overlay dev_overlay/pad_input.c)
target_link_libraries(pad_input_test PRIVATE xinput)

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
    enhanced_input/mouse_census.c
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

add_unit_test(npc_range view_distance_fix view_distance_fix/npc_range.c)
add_unit_test(fog_regime view_distance_fix view_distance_fix/fog_regime.c
    view_distance_fix/fog_band.c
    view_distance_fix/fog_regime_install.c
    view_distance_fix/fog_owner.c
    view_distance_fix/fog_trace.c
    view_distance_fix/cell_watchdog.c)
# Whose fog the device shows, the band push, and the gas room frame by frame against the old tick.
add_unit_test(fog_owner view_distance_fix view_distance_fix/fog_owner.c)

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

# The sleep ahead of the engine's frame wait: the cap above which nothing sleeps, how much of a
# frame's budget is slept, the margin learned from the timer's overshoot and the histogram the
# minute's line is read from. frame_cap.c comes along because the wait asks it for the cap in force.
add_unit_test(frame_wait framerate_fix framerate_fix/frame_wait.c framerate_fix/frame_cap.c)

# How the log names one power throttling switch out of Windows' two masks.
add_unit_test(power_throttling framerate_fix framerate_fix/power_throttling.c)

# The one predicate that decides whether a call is let into the music DLL. Wrong in either
# direction and the failure is silent.
add_unit_test(imuse_guard imuse_fix imuse_fix/imuse_guard.c)

# Which file on disk a movie name turns into. The only part of fmv_player with an answer a test can
# check: the rest of it is a window, a decoder and a message loop. Both ways of getting it wrong
# are silent, and one of them asks the file system about a file the player never named.
add_unit_test(movie_path fmv_player fmv_player/movie_path.c)

# Who decides when a movie ends in a multiplayer session, pure: the role over every gate the
# multiplayer can file, the ways out of each role, and when the host's end is the host's. The
# waiting line comes along, because it is the other pure piece of the same change.
add_unit_test(movie_rule fmv_player fmv_player/movie_rule.c fmv_player/movie_text.c)

# The session side of a movie against a gate this test files itself through the real channel: the
# roles, the record it files back, the quiet over pumped turns, and the gate taken down under it.
add_unit_test(movie_session fmv_player fmv_player/movie_session.c fmv_player/movie_rule.c
    fmv_player/movie_text.c fmv_player/movie_path.c)

# The close box during a movie, on a real window's queue: put back at once without a session,
# remembered in one and passed on exactly once afterwards, as the command a click produces.
add_unit_test(movie_close fmv_player fmv_player/movie_close.c fmv_player/movie_rule.c)

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
# reads through this, and the parsing underneath is the platform is own. Each test that writes
# engine_fixes.ini holds the same ctest resource lock, so no two of them rewrite one file under
# each other, whichever directory they run in.
add_unit_test(ini "")
set_tests_properties(ini PROPERTIES RESOURCE_LOCK engine_fixes_ini)

add_unit_test(memory "")

# The guarded copy the trying reads use, against a stand-in for the compatibility fix the game runs
# with, which resumes every read fault behind the instruction before a __try is asked.
add_unit_test(memory_guard "")

add_unit_test(patch "")

add_unit_test(detour "")

# The pattern matcher every patch in the project stands on.
add_unit_test(signature "")

# Reading an address operand out of a site somebody may already have detoured: which copy of the
# bytes to believe. The test writes a branch over a function of its own so the path that reads the
# executable on disk is reached for real rather than argued about.
add_unit_test(signature_operand "")

# Where an address came from on disk. The mapping is what decides whether an operand can be
# recovered after another module has written a branch over it, and the cases that matter are the
# ones no shipped executable has: the edge where a section's mapped size runs past its stored size.
add_unit_test(host_image "")

# The note two mods leave for each other without either knowing the other is loaded. It is
# testable in one process because that is the situation it serves: both are DLLs in the same
# process, and the channel between them is the operating system rather than a symbol.
add_unit_test(shared_note "")
add_unit_test(session_note "")
add_unit_test(npc_spawn_note "")
add_unit_test(npc_spawn_note_protocol "")
# Which far body should wear which borrowed model, between the multiplayer and the overlay.
add_unit_test(model_wear_note "")
# What the movie player and the multiplayer tell each other about a movie: two records with one
# writer each, the one shape a gate is filed in, and the refusal of a record of another shape.
add_unit_test(movie_note "")
# The host's world settings for a session and each consumer's acknowledgement: no record and a
# record of another shape both mean the consumer's own value, and every name the channel accepts.
add_unit_test(host_settings_note "")
# The two buttons of the developer menu that the multiplayer carries out: the ask the overlay
# files and the answer the multiplayer files, their shape, what each side refuses, and the
# comparison that keeps a press from before a session from firing in it.
add_unit_test(player_help_note "")
# Which DLL in the mods folder is one of this release's own: the classification from strings, the
# readers on images built byte by byte and on the operating system's own DLLs.
add_unit_test(mod_identity "")
# The multiplayer's end of those records. The three rules first, pure: when a far body that wears
# a borrowed model is built again, whose rig the rotations fit, and its size; every case is one a
# plausible rule gets wrong.
add_unit_test(mp_wear_rule multiplayer multiplayer/mp_wear_rule.c)

# The guard in front of the three engine starters a puppet calls: which clip each will play, and
# whether an actor carries it, pure. Its engine half is driven over made up bodies in the
# puppet sabre test, whose family links it.
add_unit_test(mp_starter_rule multiplayer multiplayer/mp_starter_rule.c)

# What a contact with code 0x22 does to an actor and what a throw looks like on a client, pure: the
# handler's order, the first touch of a wave, a refused report met by its wave or not, and a
# record entering state 6 against the clip last written. Its engine half reads the actor on the
# host and is linked beside the hit relay, or played by the test where the relay stands alone.
add_unit_test(mp_knockback_rule multiplayer multiplayer/mp_knockback_rule.c)

# Which sabre vectors a Jedi body keeps after its spawn, what a puppet's blade does each substep
# and the four vertices a far blade is drawn from, pure: a book that only ever raises a reading,
# the hilt a reading is held against, the order of the candidates, and the setter's formula. Its
# engine half has its own test below the body wear family.
add_unit_test(mp_blade_rule multiplayer multiplayer/mp_blade_rule.c)

# Then the tick that asks for the model and follows the answer, over made up bodies and an answer
# published by hand. The family is what the tick reaches: the far player's model, the rebuild
# through the body module, the puppet's blade, rotations and effects it sets back at the first
# sight of a worn body, and the wall clock the wait before a rebuild is measured on. Every list
# below that links a caller of the tick carries it too.
set(BODY_WEAR_FAMILY
    multiplayer/mp_body_wear.c
    multiplayer/mp_wear_rule.c
    multiplayer/mp_wallclock.c
    multiplayer/mp_bridge_far.c
    multiplayer/mp_timeline.c
    multiplayer/mp_interp.c
    multiplayer/mp_snapshot.c
    multiplayer/mp_snapshot_history.c
    multiplayer/mp_twist.c
    multiplayer/mp_puppet_sabre.c
    multiplayer/mp_puppet_starter.c
    multiplayer/mp_starter_rule.c
    multiplayer/mp_puppet_anim.c
    multiplayer/mp_puppet_anim_engine.c
    multiplayer/mp_puppet_anim_seek.c
    multiplayer/mp_clip_rule.c
    multiplayer/mp_clip_events.c
    multiplayer/mp_signatures_clip.c
    multiplayer/mp_blade.c
    multiplayer/mp_blade_draw.c
    multiplayer/mp_blade_rule.c
)
add_unit_test(mp_body_wear multiplayer
    ${BODY_WEAR_FAMILY}
    multiplayer/mp_armed.c
    multiplayer/mp_events.c
    multiplayer/mp_wire.c
    multiplayer/mp_body.c
    multiplayer/mp_body_passable.c
    multiplayer/mp_body_gate.c
    multiplayer/mp_signatures_contact.c
    multiplayer/mp_body_shot.c
    multiplayer/mp_shot_sites.c
    multiplayer/mp_body_death.c
    multiplayer/mp_contact_rule.c
    multiplayer/mp_body_asset.c
    multiplayer/mp_body_spawn.c
    multiplayer/mp_effects.c
    multiplayer/mp_node_map.c
    multiplayer/mp_bank.c
    multiplayer/mp_bank_keep.c
    multiplayer/mp_bank_spawn.c
    multiplayer/mp_capacity.c
    multiplayer/mp_phases.c
    multiplayer/mp_pool.c
    multiplayer/mp_pool_rule.c
    multiplayer/mp_cells.c
    multiplayer/mp_content.c
    multiplayer/mp_signatures.c
    multiplayer/mp_signatures_enemy.c
    multiplayer/mp_signatures_foot.c
    multiplayer/mp_signatures_blade.c
    multiplayer/mp_signatures_puppet.c
    multiplayer/mp_cell_sites.c
    multiplayer/mp_menu_sites.c
    multiplayer/mp_lobby_sites.c
)

# The blade rule's engine half over blocks, actors, bodies, render handles and models made up in
# the test's own memory: the offsets it reads, which blocks a choice is held against, the two
# fields a puppet's blade is judged by, and where the chosen vectors are written. The body
# wear family comes along because the far spawn names it, and the armed cell because the player's
# own spawn asks it.
add_unit_test(mp_blade multiplayer
    ${BODY_WEAR_FAMILY}
    multiplayer/mp_armed.c
    multiplayer/mp_events.c
    multiplayer/mp_wire.c
    multiplayer/mp_body.c
    multiplayer/mp_body_passable.c
    multiplayer/mp_body_gate.c
    multiplayer/mp_signatures_contact.c
    multiplayer/mp_body_shot.c
    multiplayer/mp_shot_sites.c
    multiplayer/mp_body_death.c
    multiplayer/mp_contact_rule.c
    multiplayer/mp_body_asset.c
    multiplayer/mp_body_spawn.c
    multiplayer/mp_effects.c
    multiplayer/mp_node_map.c
    multiplayer/mp_bank.c
    multiplayer/mp_bank_keep.c
    multiplayer/mp_bank_spawn.c
    multiplayer/mp_capacity.c
    multiplayer/mp_phases.c
    multiplayer/mp_pool.c
    multiplayer/mp_pool_rule.c
    multiplayer/mp_cells.c
    multiplayer/mp_content.c
    multiplayer/mp_signatures.c
    multiplayer/mp_signatures_enemy.c
    multiplayer/mp_signatures_foot.c
    multiplayer/mp_signatures_blade.c
    multiplayer/mp_signatures_puppet.c
    multiplayer/mp_cell_sites.c
    multiplayer/mp_menu_sites.c
    multiplayer/mp_lobby_sites.c
)

# A far Jedi's blade drawn from vertices of its own, over a model, a mesh, a body and a bank block
# made up in the test's own memory: the one way to the mesh by name, the two hull bodies with a
# pretend engine call that records what the mesh named while it ran, the live check, the guard a
# pretend spawn and take down write through, and the frame witness. Nothing resolves here, so the
# hulls themselves are never placed; the family is what the module and its callers link against.
add_unit_test(mp_blade_draw multiplayer
    ${BODY_WEAR_FAMILY}
    multiplayer/mp_armed.c
    multiplayer/mp_events.c
    multiplayer/mp_wire.c
    multiplayer/mp_body.c
    multiplayer/mp_body_passable.c
    multiplayer/mp_body_gate.c
    multiplayer/mp_signatures_contact.c
    multiplayer/mp_body_shot.c
    multiplayer/mp_shot_sites.c
    multiplayer/mp_body_death.c
    multiplayer/mp_contact_rule.c
    multiplayer/mp_body_asset.c
    multiplayer/mp_body_spawn.c
    multiplayer/mp_effects.c
    multiplayer/mp_node_map.c
    multiplayer/mp_bank.c
    multiplayer/mp_bank_keep.c
    multiplayer/mp_bank_spawn.c
    multiplayer/mp_capacity.c
    multiplayer/mp_phases.c
    multiplayer/mp_pool.c
    multiplayer/mp_pool_rule.c
    multiplayer/mp_cells.c
    multiplayer/mp_content.c
    multiplayer/mp_signatures.c
    multiplayer/mp_signatures_enemy.c
    multiplayer/mp_signatures_foot.c
    multiplayer/mp_signatures_blade.c
    multiplayer/mp_signatures_puppet.c
    multiplayer/mp_cell_sites.c
    multiplayer/mp_menu_sites.c
    multiplayer/mp_lobby_sites.c
)

# What an animation ordinal means on one actor, and the chain that answers when a role is empty.
# Pure lookup with no engine in it, so the test links it alone. Every way the chain can be wrong is
# quiet in review and loud on screen: a walk and run pair that falls back to each other never
# terminates, and a missing jump that answers with the idle snaps the body to a stand in mid air.
add_unit_test(character_profile "")

# Everything effect_clock answers the engine with in place of a random draw. Wrong in the range
# and the engine's own scaling runs off the end; wrong in the mixing and either every flickering
# object in a room goes dark together or an effect freezes instead of being paced.
add_unit_test(substep_noise effect_clock effect_clock/substep_noise.c)

# The two bounds that are the whole point of render_guard. Both are comparisons, and every way
# they can be wrong is silent in review: too tight and authored geometry is refused, too loose
# and the write that overruns still happens.
add_unit_test(face_bounds render_guard render_guard/face_bounds.c)

# The halo's node vertex copy, on structures of the test's own: every arm that does not copy
# leaves twelve zeros, and a mesh longer than the buffer stops at its fourth vertex.
add_unit_test(halo_copy render_guard render_guard/halo_copy.c)

# The x87 balance's instruction replacement, on bytes of the test's own: the opcode is checked
# before anything is written, a refusal writes nothing, and the journal puts it all back.
add_unit_test(node_verts render_guard render_guard/node_verts.c)

# Which requested texture ceiling is accepted, clamped or refused. The engine never asks the
# device for its own maximum, so this validation is the only thing between a setting and a
# surface creation that fails.
add_unit_test(texture_size large_textures large_textures/texture_size.c)

# The model swap links as one. Its modules call across each other densely enough that a test of
# any one of them needs nearly all of them, so the list is named once and every test of the family
# links it whole. A test that needs only a leaf names that leaf on its own.
set(MODEL_SWAP_FAMILY
    dev_overlay/character_bodies.c
    dev_overlay/character_buoyancy.c
    dev_overlay/character_cards.c
    dev_overlay/character_clipcopy.c
    dev_overlay/character_facing.c
    dev_overlay/character_ground.c
    dev_overlay/character_model.c
    dev_overlay/character_model_roster.c
    dev_overlay/character_model_sites.c
    dev_overlay/character_mount.c
    dev_overlay/character_nodemap.c
    dev_overlay/character_prop.c
    dev_overlay/character_prop_blade.c
    dev_overlay/character_prop_body.c
    dev_overlay/character_prop_draw.c
    dev_overlay/character_prop_math.c
    dev_overlay/character_prop_rig.c
    dev_overlay/character_prop_sites.c
    dev_overlay/character_rebind.c
    dev_overlay/character_reentry.c
    dev_overlay/character_restmap.c
    dev_overlay/far_model.c
    dev_overlay/local_look.c
    dev_overlay/model_blade_guard.c
)

# The overlay's model: which rows are on screen, and the search that decides it. Built against the
# real cheat sources rather than stubs, so the test also pins down what the panel looks like on an
# executable neither of them could resolve. Two programs share the list: overlay_model walks the
# navigation, the cheats, the free camera and the folds, and overlay_groups walks the settings
# groups by position. One program held both until the tenth group put it over the size limit.
set(OVERLAY_MODEL_SOURCES
    unittests/overlay_stubs.c
    dev_overlay/overlay_model.c
    # which headings the model draws, in what order, and which sources stand under each
    dev_overlay/overlay_headings.c
    # what a running multiplayer session takes away, which the model asks per row; the
    # note it reads comes with engine_fixes_common, which every test here links
    dev_overlay/session_lock.c
    # Getting around the panel from the keyboard, and the bands it moves through. Without these
    # the program named overlay_keys would drive the model and never overlay_keys.c: left and
    # right, Home, End and Return would go unchecked, and so would the one line that keeps all
    # of them off a panel that is open but not drawn.
    dev_overlay/overlay_keys.c
    dev_overlay/overlay_layout.c
    # what the panel last turned down, which the layout asks for the band's height
    dev_overlay/overlay_notice.c
    # the groups whose rows the model dispatches to, the same as the Utilities group below
    dev_overlay/overlay_framerate.c
    dev_overlay/overlay_window.c
    dev_overlay/fog_band_row.c
    dev_overlay/view_range_live_row.c
    # what the host of a running session has set on a client, which four rows and the note under
    # the draw distance read; outside a client's session it asks nothing
    dev_overlay/overlay_host_value.c
    dev_overlay/overlay_key_name.c
    dev_overlay/overlay_utilities.c
    dev_overlay/open_key_row.c
    # the chat's key row, which the open key row also asks so the two never share a key, and the
    # multiplayer group it is drawn in
    dev_overlay/chat_key_row.c
    dev_overlay/overlay_multiplayer.c
    # the two buttons of that group, which file an ask for the multiplayer and read its answer.
    # A press asks whether the free camera keeps the panel up, which is input_owner.c's answer;
    # nothing flies in a test process, so it answers no and the stubbed close is counted
    dev_overlay/player_help_row.c
    dev_overlay/input_owner.c
    dev_overlay/fog_follow_row.c
    dev_overlay/auto_range_row.c
    dev_overlay/dismemberment_row.c
    dev_overlay/fov_row.c
    dev_overlay/menu_extras_row.c
    dev_overlay/sensitivity_row.c
    dev_overlay/free_look_row.c
    dev_overlay/strafe_row.c
    dev_overlay/controller_mode_row.c
    dev_overlay/overlay_controls.c
    # a group's rows written as a table; the three groups under it are built out of it
    dev_overlay/overlay_kit.c
    # a number row's arithmetic: fraction to value and back, one press, and Default
    dev_overlay/overlay_number.c
    # the one way a hand reaches the settings file, which the sideways keys and Default use
    dev_overlay/overlay_slider.c
    dev_overlay/overlay_picture.c
    dev_overlay/overlay_fog.c
    dev_overlay/overlay_freecam.c
    dev_overlay/overlay_dismember.c
    dev_overlay/overlay_menu_extras.c
    dev_overlay/overlay_cheats.c
    dev_overlay/overlay_row_fill.c
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
    dev_overlay/cheats_super_run.c
    dev_overlay/cheats_jump_boost.c
    dev_overlay/cheats_fall_consequences.c
    dev_overlay/cheats_noclip.c
    dev_overlay/cheats_free_camera.c
    dev_overlay/freecam_teleport.c
    dev_overlay/freecam_world.c
    # The free camera reads the pad's frame; in a test process the pad is never read, so the
    # frame stays empty and the camera takes nothing from it.
    dev_overlay/pad_input.c
    dev_overlay/freeze_anim_row.c
    # The start level: its sites resolve nothing in a test process, so its row reads
    # unavailable, which is the case the panel has to show.
    dev_overlay/start_level.c
    dev_overlay/start_level_row.c
    dev_overlay/overlay_levels.c
    # The NPC spawner: its sites resolve nothing in a test process and no level is loaded, so
    # both of its rows read unavailable and its list is empty, which is the case the panel has
    # to show.
    dev_overlay/npc_spawner.c
    dev_overlay/npc_census.c
    dev_overlay/npc_foreign.c
    dev_overlay/npc_spawn_record.c
    dev_overlay/npc_spawn_rules.c
    dev_overlay/npc_spawn_describe.c
    dev_overlay/world_probe.c
    dev_overlay/npc_spawn_session.c
    dev_overlay/npc_spawn_link.c
    dev_overlay/npc_spawn_save.c
    dev_overlay/npc_spawn_node.c
    dev_overlay/npc_spawn_sites.c
    dev_overlay/npc_spawn_list.c
    dev_overlay/npc_spawn_block.c
    dev_overlay/spawn_scripts.c
    dev_overlay/actor_loader.c
    dev_overlay/overlay_spawn.c
    dev_overlay/overlay_reason.c
    dev_overlay/overlay_edit.c
    dev_overlay/overlay_row_source.c
    # the two choice kinds: the words of a segment row, where its boxes sit and which
    # one a point falls in
    dev_overlay/overlay_choice.c
    # the model swap's group, and the swap under it: it resolves its own engine sites and
    # answers nothing in a process that has none, which is what the rows are asked here. The
    # family brings local_look.c, which the cheats also say the player's size through.
    dev_overlay/overlay_modelswap.c
    ${MODEL_SWAP_FAMILY}
    # The archive catalogue, the same: there is no big.lab beside a test process, so it reads
    # nothing and the spawner's list holds no archive entries.
    dev_overlay/actor_catalog.c
    dev_overlay/cheats_no_fog.c
    # Both jump boost and the free camera ask the same "is there a floor under this" question,
    # so the module that owns it has to be linked beside them. It resolves nothing in a test
    # process either, and answers UNAVAILABLE, which is the case both callers must survive.
    dev_overlay/floor_probe.c
    # The entity spawner's offer and the pieces of its placement mode the panel's rows and the
    # spawner reach: none of them resolves anything in a test process, so the mode reads
    # unavailable and the ghost is never bound.
    dev_overlay/entity_offer.c
    dev_overlay/entity_names.c
    dev_overlay/npc_spawner_sites.c
    dev_overlay/spawn_ghost.c
    dev_overlay/spawn_keys.c
    dev_overlay/spawn_place.c
    dev_overlay/spawn_reason.c
    # The cell the player is read through. It resolves nothing in a test process, so every
    # cheat that reads the player is left unavailable, which is the case the panel has to show.
    dev_overlay/player_slot.c
    # cheats_openphantom.c calls sim_pause_hold() rather than writing the engine's pause cell itself,
    # so the owner of that cell has to be linked in beside it. Nothing here calls into it: without a
    # resolved site every entry point returns immediately, as a test process wants.
    dev_overlay/sim_pause.c
    # The free camera holds the player's input while the world runs under it, through the same
    # holders the panel uses. Nothing here flies, so its detours are never placed.
    dev_overlay/input_freeze.c
)
add_unit_test(overlay_model dev_overlay ${OVERLAY_MODEL_SOURCES})
target_link_libraries(overlay_model_test PRIVATE xinput)
add_unit_test(overlay_groups dev_overlay ${OVERLAY_MODEL_SOURCES})
target_link_libraries(overlay_groups_test PRIVATE xinput)
set_tests_properties(overlay_groups PROPERTIES RESOURCE_LOCK engine_fixes_ini)
# Which rows a running session takes, walked through the whole panel and checked BY NAME. The
# lock decides by a group number and a slot number, and renaming, reordering or merging headings
# moves them; a slot that moved would open a hashed setting in the middle of a session, and
# nothing else in the build would notice.
# The chip's word is overlay_chip.c's, which measures through the drawing; the program stands in
# for the two measures and links the real word, because on a client the word of a taken row is the
# host's value and that is what is checked here.
add_unit_test(overlay_session dev_overlay ${OVERLAY_MODEL_SOURCES} dev_overlay/overlay_chip.c)
target_link_libraries(overlay_session_test PRIVATE xinput)
# A choice drawn as a choice: every entry of a list is the CHOICE kind, at most one of a list is
# marked, and the one choice shown whole is held to its words, the room they cost and the hit
# test that turns a pixel into one of them. Driven against the real row list, because the thing
# worth pinning is that four separate sources agree about what a list looks like.
add_unit_test(overlay_choice dev_overlay ${OVERLAY_MODEL_SOURCES})
target_link_libraries(overlay_choice_test PRIVATE xinput)
# What the panel keeps across a close, and the row the keyboard is on: two rules that are pure,
# driven against the real row list because what is interesting is that the count moves under
# them.
add_unit_test(overlay_keys dev_overlay ${OVERLAY_MODEL_SOURCES})
# Where the panel's bands sit, and what the refusal band costs the rows. The model is real rather
# than stubbed because overlay_layout.c asks it how far the list is scrolled and how many rows
# there are, and a stub of that would only prove the stub.
add_unit_test(overlay_layout dev_overlay ${OVERLAY_MODEL_SOURCES})
# Every label and every chip against the room the panel leaves for it, with both tabs and every
# fold open. A program of its own rather than part of the overlay_model test, whose walk down
# the tab it shares nothing with.
add_unit_test(overlay_width dev_overlay ${OVERLAY_MODEL_SOURCES})
target_link_libraries(overlay_width_test PRIVATE xinput)
# The panel's end of the two buttons under Multiplayer, with the multiplayer stood in for by the
# records it would file: when a button is offered and the word it carries when it is not, what a
# press files, which answer counts as the answer to the last press, how seldom the answer is
# asked for while there is none, and every sentence of the line under the buttons against the
# room it has. It links the whole list because a button asks the session lock, the free camera
# and the input freeze, and those bring the rest.
add_unit_test(player_help_row dev_overlay ${OVERLAY_MODEL_SOURCES})
target_link_libraries(player_help_row_test PRIVATE xinput)
target_link_libraries(overlay_layout_test PRIVATE xinput)
target_link_libraries(overlay_keys_test PRIVATE xinput)
# The entity spawner group with a spawner standing behind it. Every programme above links the real
# npc_spawner.c, which resolves nothing in a process with no game, so the group reads unavailable
# in all of them and the panel's one row of WORDS could never be driven: a branch taken out of
# overlay_keys_sideways() left every one of them green. This programme swaps that one file for a
# stand in inside the test and drives the usable half: the words, the row locked by a pickup, the
# word the folded heading carries, and a list entry that is offered and not chosen.
set(OVERLAY_SPAWNER_SOURCES ${OVERLAY_MODEL_SOURCES})
list(REMOVE_ITEM OVERLAY_SPAWNER_SOURCES dev_overlay/npc_spawner.c)
add_unit_test(overlay_spawner dev_overlay ${OVERLAY_SPAWNER_SOURCES})
target_link_libraries(overlay_spawner_test PRIVATE xinput)

# Every row of both tabs, printed. No ctest entry, because it checks nothing: it is run before and
# after a change to the panel and the two runs are diffed, which is the only guard the row builders
# have where nobody has written a check for a particular row.
build_test_program(overlay_dump dev_overlay ${OVERLAY_MODEL_SOURCES})
target_link_libraries(overlay_dump_test PRIVATE xinput)

# The model swap's own tests. None of them needs a game: each drives the pure half of one module
# and pins what the live half does in a process with no engine, which is to resolve nothing and
# say so.
#
# Whether two spellings name the same asset, and how many rows there are. The rows past the five
# heroes are characters.ini, found beside the configuration file and so beside the program; this
# one program is built into a directory of its own with the shipped file copied in, and every other
# program here goes on finding none.
add_unit_test(character_model_roster dev_overlay dev_overlay/character_model_roster.c)
set_target_properties(character_model_roster_test PROPERTIES
    RUNTIME_OUTPUT_DIRECTORY ${CMAKE_CURRENT_BINARY_DIR}/with_profiles)
add_custom_command(TARGET character_model_roster_test POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy_if_different
        ${CMAKE_SOURCE_DIR}/dist/characters.ini
        $<TARGET_FILE_DIR:character_model_roster_test>/characters.ini)
# Which rigs the player's clips can be translated onto, node by node.
add_unit_test(character_nodemap dev_overlay ${MODEL_SWAP_FAMILY})
# Which node a weapon name resolves to on a rig that does not carry it.
add_unit_test(character_mount dev_overlay ${MODEL_SWAP_FAMILY})
# Which body wears which pair, the one predicate every hook asks, the old single body logic run
# beside the table, the blade guard's question and the far path's answer for one bank.
add_unit_test(character_bodies dev_overlay dev_overlay/character_bodies.c)
# Each pair's private clips: its own cache and arena, never started again for a second wearer.
add_unit_test(character_clipcopy dev_overlay dev_overlay/character_clipcopy.c
    dev_overlay/character_restmap.c)
# The rebind and the six node words of each owner, on a rig in the test's own memory.
add_unit_test(character_rebind dev_overlay ${MODEL_SWAP_FAMILY})
# The far bodies' answer through the real notes, in a process where nothing of the swap resolves.
add_unit_test(far_model dev_overlay ${MODEL_SWAP_FAMILY})
# Where a borrowed weapon hangs, and the one matrix that puts it there.
add_unit_test(character_prop dev_overlay ${MODEL_SWAP_FAMILY})
# The rows the borrowed weapon hangs on, and the single body logic they replaced, run beside
# each other over a pretend engine. The site resolution is this program's own: a test process
# has no engine to find the eight entry points in, and the module will not arm without them.
# character_prop_engine.c is that pretend engine, in a file of its own so that the reference run
# and the far bodies beside it are driven over one world rather than two.
set(PROP_BODY_FAMILY ${MODEL_SWAP_FAMILY})
list(REMOVE_ITEM PROP_BODY_FAMILY dev_overlay/character_prop_sites.c)
add_unit_test(character_prop_body dev_overlay unittests/character_prop_engine.c
              ${PROP_BODY_FAMILY})
# The same world with far players' bodies on it: the rows the far path arms, the words it lends
# them, what the sphere answers for a body this pass did not draw, and what it may never do.
add_unit_test(character_prop_far dev_overlay unittests/character_prop_engine.c
              ${PROP_BODY_FAMILY})
# Which faces an actor authored as two sided, and whose rigs were never marked.
add_unit_test(character_facing dev_overlay ${MODEL_SWAP_FAMILY})
# How many vertices of a mesh may be copied into a sabre card buffer: four, whatever it claims.
add_unit_test(character_cards dev_overlay ${MODEL_SWAP_FAMILY})
# Which assets a swap may borrow, and when the group refuses.
add_unit_test(character_model dev_overlay ${MODEL_SWAP_FAMILY})
# The waterline a swimmer of another height is pinned to.
add_unit_test(character_buoyancy dev_overlay ${MODEL_SWAP_FAMILY})
# What the marks a borrowed body leaves on the floor are measured on.
add_unit_test(character_ground dev_overlay ${MODEL_SWAP_FAMILY})
# The lock a hook holds while it is inside itself.
add_unit_test(character_reentry dev_overlay dev_overlay/character_reentry.c)

# The character census reads a live process, so what can be tested without the game is the
# arithmetic underneath it: the pool slot walk, the radius test, the vertical classification and
# the fixed width name field. character_scan.c holds exactly that and no more.
add_unit_test(character_scan diagnostics diagnostics/character_scan.c)

# The debug register bit packing. Getting a condition or a length wrong does not fail loudly, it
# arms a watch that never fires, which reads as "nothing writes this field". Worth pinning to the
# values the manual specifies.
add_unit_test(debug_register diagnostics diagnostics/debug_register.c)

# Which logical processors are efficiency cores, built from Windows' list of processors and their
# efficiency classes. The frame line's count of frames begun on one is only as right as this map.
add_unit_test(diag_core_class diagnostics diagnostics/diag_core_class.c)

# When the running count of first-chance access violations goes into the log: 16, 64, 256 and 1024,
# each once, and a count that jumped past several reported once.
add_unit_test(crash_report crash_report crash_report/crash_report.c)

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

# Which draw distance, fog band and authored band the settings poll takes, a multiplayer host's or
# this machine's own, and the table a host's values are held to against this DLL's clamps and the
# developer panel's rows; the same for the dismemberment mode against the modes that DLL knows.
add_unit_test(view_host_value view_distance_fix view_distance_fix/view_host_value.c)
add_unit_test(limb_mode_pick dismemberment dismemberment/limb_mode_pick.c)

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

# The chat's key row against the multiplayer's own reader, linked in beside it: every name the row
# can write reads back through mp_board_key_code as the same key, the row's grammar answers what
# that reader answers for every string of up to three characters, and a refused key never reaches
# the file, and the key the row shows is the key mp_chat_key_pick opens the chat on. The two DLLs
# share no code, so this program is the one place the two readings meet. The reader's own
# dependencies are the list the scoreboard's test links.
add_unit_test(chat_key_row dev_overlay dev_overlay/chat_key_row.c dev_overlay/open_key_row.c
    dev_overlay/overlay_key_name.c dev_overlay/overlay_notice.c
    multiplayer/mp_board.c
    multiplayer/mp_chat_key_rule.c
    multiplayer/mp_text.c
    multiplayer/mp_text_pick.c
    multiplayer/mp_text_second.c
    multiplayer/mp_roster.c
    multiplayer/mp_score.c
    multiplayer/mp_rules.c
    multiplayer/mp_lobby.c
    multiplayer/mp_wire.c
)
target_include_directories(chat_key_row_test PRIVATE ${MODS}/multiplayer)
set_tests_properties(chat_key_row PROPERTIES RESOURCE_LOCK engine_fixes_ini)

# The draw distance row's own arithmetic: the clamp, and the hand written parser that exists
# because strtof would read a decimal point only where the locale agrees it is one.
add_unit_test(view_range_row dev_overlay dev_overlay/view_range_row.c)

# The field of view row, which is the only one with a slider, so its ends and its rounding are
# where a handle that cannot reach the maximum would show up.
add_unit_test(fov_row dev_overlay dev_overlay/fov_row.c dev_overlay/view_range_row.c)

# The shape of the multiplayer site and cell tables. What a test can prove here is not where a site
# is, which only the real executables can answer, but whether the table is capable of being right:
# no pattern may require an absolute address, an operand a caller reads has to be a wildcard in its
# own pattern, and a build where nothing resolves has to say so and hand back zero.
add_unit_test(mp_signatures multiplayer
    multiplayer/mp_signatures.c
    multiplayer/mp_signatures_crate.c
    multiplayer/mp_signatures_enemy.c
    multiplayer/mp_signatures_foot.c
    multiplayer/mp_signatures_blade.c
    multiplayer/mp_signatures_puppet.c
    multiplayer/mp_cell_sites.c
    multiplayer/mp_menu_sites.c
    multiplayer/mp_lobby_sites.c
    multiplayer/mp_cells.c
    multiplayer/mp_content.c
    multiplayer/mp_world_values.c
)

# The reserve latch's decision, and the latch itself in a process with no game. The decision's two
# off-by-ones are silent in review and expensive in play, and the no-game claim is that every ask
# comes back as a counted refusal rather than a dereference. The engine-facing modules come along
# because the latch names them; on this machine they resolve to nothing, which is the point.
add_unit_test(mp_pool multiplayer
    multiplayer/mp_pool.c
    multiplayer/mp_pool_rule.c
    multiplayer/mp_capacity.c
    multiplayer/mp_cells.c
    multiplayer/mp_content.c
    multiplayer/mp_signatures.c
    multiplayer/mp_signatures_enemy.c
    multiplayer/mp_signatures_foot.c
    multiplayer/mp_signatures_blade.c
    multiplayer/mp_signatures_puppet.c
    multiplayer/mp_cell_sites.c
    multiplayer/mp_menu_sites.c
    multiplayer/mp_lobby_sites.c
)

# What a far body's spawn takes from the player sitting here and gives back, over a byte model
# of the engine's two writers; the rule is pure, so it needs nothing else.
add_unit_test(mp_bank_keep multiplayer multiplayer/mp_bank_keep.c)

# What a player takes along when the hero under him changes in a session, over a byte model of the
# same two engine writers: the six bytes, the health and the general ammunition travel, the hero's
# own weapons stay. The window's rule comes along for the precondition both ask.
add_unit_test(mp_hero_carry_rule multiplayer
    multiplayer/mp_hero_carry_rule.c
    multiplayer/mp_bank_keep.c
)

# The digest the bank swap gate stands on, pinned to FNV-1a's published answers rather than to a
# second copy of its own arithmetic, and the bank driven with no game: every ask must come back as
# a refusal with nothing dereferenced. The engine-facing modules come along because the bank names
# them; on this machine they resolve to nothing, which is the point.
add_unit_test(mp_bank multiplayer
    multiplayer/mp_bank.c
    multiplayer/mp_bank_keep.c
    multiplayer/mp_bank_spawn.c
    multiplayer/mp_cells.c
    multiplayer/mp_content.c
    multiplayer/mp_signatures.c
    multiplayer/mp_signatures_enemy.c
    multiplayer/mp_signatures_foot.c
    multiplayer/mp_signatures_blade.c
    multiplayer/mp_signatures_puppet.c
    multiplayer/mp_cell_sites.c
    multiplayer/mp_menu_sites.c
    multiplayer/mp_lobby_sites.c
)

# A far body's window over engine data made up in memory, against the window as it was when its
# stores went through the protecting write: the same bytes left behind, and the read only page as
# the one difference. The test plays the bank's four cells itself.
add_unit_test(mp_bank_window multiplayer
    multiplayer/mp_bank.c
    multiplayer/mp_bank_keep.c
    multiplayer/mp_bank_spawn.c
)

# The input split's decision core: which action reads which command field, the bound the digital
# axes honour, and the tap-versus-hold contract whose accumulator the caller owns and whose return
# fires on exactly the release call. The bank comes along because the dispatch asks it which class
# is active; on this machine nothing resolves, which is what the refusal checks pin down.
add_unit_test(mp_input multiplayer
    multiplayer/mp_input.c
    multiplayer/mp_pause_rule.c
    multiplayer/mp_armed.c
    multiplayer/mp_bank.c
    multiplayer/mp_bank_keep.c
    multiplayer/mp_bank_spawn.c
    multiplayer/mp_cells.c
    multiplayer/mp_content.c
    multiplayer/mp_signatures.c
    multiplayer/mp_signatures_enemy.c
    multiplayer/mp_signatures_foot.c
    multiplayer/mp_signatures_blade.c
    multiplayer/mp_signatures_puppet.c
    multiplayer/mp_cell_sites.c
    multiplayer/mp_menu_sites.c
    multiplayer/mp_lobby_sites.c
)

# The dedicated server's log line for a player who left, through the log's own ring and writer.
add_unit_test(mp_server_log multiplayer multiplayer/mp_server_log.c)

# The dedicated server, end to end with no game: one server and two clients on real localhost
# sockets in this process. Both join, each is told its slot, and each sees the other player's
# body where that player put it. This is the whole dedicated-server claim, proven offline.
add_unit_test(mp_dedicated multiplayer
    multiplayer/mp_server.c
    multiplayer/mp_chat_rule.c
    multiplayer/mp_chat_wire.c
    multiplayer/mp_npc_copy_wire.c
    multiplayer/mp_enemy_wire.c
    multiplayer/mp_fieldset.c
    multiplayer/mp_server_log.c
    multiplayer/mp_match.c
    multiplayer/mp_death.c
    multiplayer/mp_events.c
    multiplayer/mp_trust.c
    multiplayer/mp_session.c
    multiplayer/mp_session_packets.c
    multiplayer/mp_entropy.c
    multiplayer/mp_session_access.c
    multiplayer/mp_roster.c
    multiplayer/mp_score.c
    multiplayer/mp_rules.c
    multiplayer/mp_lobby.c
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
)
target_link_libraries(mp_dedicated_test PRIVATE ws2_32)

# The enemy block links as one, the same way the model swap family does. Listed by hand in every
# target that reaches the block, a new enemy module would need an edit in each, and a missing one
# would surface as an unresolved symbol in whichever test happened to link it. A new module of
# the enemy block goes into this list and nowhere else in this file.
set(ENEMY_FAMILY
    multiplayer/mp_enemy_sync.c
    multiplayer/mp_enemy_sync_flush.c
    multiplayer/mp_enemy_queue_rule.c
    multiplayer/mp_cadence.c
    multiplayer/mp_stopwatch.c
    multiplayer/mp_scene_exit.c
    multiplayer/mp_enemy_sync_send.c
    multiplayer/mp_enemy_sync_choose.c
    multiplayer/mp_enemy_interest_rule.c
    multiplayer/mp_range_gate_rule.c
    multiplayer/mp_enemy_sync_report.c
    multiplayer/mp_knockback_rule.c
    multiplayer/mp_npc_copies.c
    multiplayer/mp_npc_copies_client.c
    multiplayer/mp_npc_copy_wire.c
    multiplayer/mp_enemy_pack.c
    multiplayer/mp_enemy_limb.c
    multiplayer/mp_enemy_block.c
    multiplayer/mp_enemy_block_rule.c
    multiplayer/mp_enemy_burst.c
    multiplayer/mp_enemy_burst_rule.c
    multiplayer/mp_enemy_dead_watch.c
    multiplayer/mp_enemy_dead_watch_rule.c
    multiplayer/mp_enemy_dead_watch_client.c
    multiplayer/mp_armed.c
    multiplayer/mp_enemy_fx.c
    multiplayer/mp_world_event.c
    multiplayer/mp_world_event_client.c
    multiplayer/mp_world_event_rule.c
    multiplayer/mp_actor_loop.c
    multiplayer/mp_script_sound_rule.c
    multiplayer/mp_enemy_body.c
    multiplayer/mp_enemy_body_rule.c
    multiplayer/mp_enemy_nodes.c
    multiplayer/mp_director_rule.c
    multiplayer/mp_enemy_shield.c
    multiplayer/mp_enemy_shield_rule.c
    multiplayer/mp_level_state.c
    multiplayer/mp_level_state_report.c
    multiplayer/mp_level_state_bind.c
    multiplayer/mp_level_state_count.c
    multiplayer/mp_level_state_rule.c
    multiplayer/mp_level_state_apply.c
    multiplayer/mp_level_state_fog.c
    multiplayer/mp_level_state_fog_rule.c
    multiplayer/mp_level_state_journal_rule.c
    multiplayer/mp_level_state_drawn.c
    multiplayer/mp_fog_viewers.c
    multiplayer/mp_fog_viewers_rule.c
    multiplayer/mp_scene_rule.c
    multiplayer/mp_enemy_bind.c
    multiplayer/mp_enemy_pose.c
    multiplayer/mp_census_probe.c
    multiplayer/mp_enemy_nodes_rule.c
    multiplayer/mp_enemy_limb_rule.c
    multiplayer/mp_enemy_blast.c
    multiplayer/mp_signatures_effects.c
    multiplayer/mp_clip_rule.c
    multiplayer/mp_clip_events.c
    multiplayer/mp_signatures_clip.c
    multiplayer/mp_enemy_spawn.c
    multiplayer/mp_capacity.c
    multiplayer/mp_pool.c
    multiplayer/mp_pool_rule.c
    multiplayer/mp_enemy_wire.c
    multiplayer/mp_fieldset.c
    multiplayer/mp_cells.c
    multiplayer/mp_cell_sites.c
    multiplayer/mp_menu_sites.c
    multiplayer/mp_lobby_sites.c
    multiplayer/mp_signatures.c
    multiplayer/mp_signatures_enemy.c
    multiplayer/mp_signatures_foot.c
    multiplayer/mp_signatures_blade.c
    multiplayer/mp_signatures_puppet.c
    multiplayer/mp_wire.c
    multiplayer/mp_payload_prefix.c
    multiplayer/mp_snapshot_history.c
)

# The bridge's command mapping: every wire action bit onto its engine action id, exactly, because
# a wrong pairing does not look wrong, it jumps when the far player meant to fire. The whole
# engine-facing family comes along because the bridge names it; nothing resolves here, which is
# what the refusal checks pin down.
set(BRIDGE_FAMILY
    multiplayer/mp_pause.c
    multiplayer/mp_pause_rule.c
    multiplayer/mp_signatures_pause.c
    multiplayer/mp_cutscene.c
    multiplayer/mp_cutscene_sites.c
    multiplayer/mp_signatures_scene.c
    multiplayer/mp_scene_claim_rule.c
    multiplayer/mp_scene_claim.c
    multiplayer/mp_scene_flow.c
    multiplayer/mp_scene_doorless.c
    multiplayer/mp_scene_room.c
    multiplayer/mp_scene_scope.c
    multiplayer/mp_scene_bind.c
    multiplayer/mp_scene_host.c
    multiplayer/mp_scene_seats.c
    multiplayer/mp_scene_own.c
    multiplayer/mp_scene_host_report.c
    multiplayer/mp_scene_client.c
    multiplayer/mp_scene_free_rule.c
    multiplayer/mp_scene_free.c
    multiplayer/mp_scene_hero_rule.c
    multiplayer/mp_scene_hero_watch.c
    multiplayer/mp_npc_copies_bridge.c
    multiplayer/mp_npc_copies_run.c
    multiplayer/mp_npc_copies_run_host.c
    multiplayer/mp_npc_copies_run_client.c
    multiplayer/mp_arena.c
    multiplayer/mp_bridge.c
    multiplayer/mp_bridge_command.c
    multiplayer/mp_bridge_drain.c
    multiplayer/mp_bridge_drain_host.c
    multiplayer/mp_lobby_note_rule.c
    multiplayer/mp_flash.c
    multiplayer/mp_flash_rule.c
    multiplayer/mp_range_gate.c
    multiplayer/mp_world_anchor_rule.c
    multiplayer/mp_world_anchor.c
    multiplayer/mp_stopwatch.c
    multiplayer/mp_level_switch.c
    multiplayer/mp_session_now.c
    multiplayer/mp_bridge_install.c
    multiplayer/mp_bridge_far.c
    multiplayer/mp_bridge_appearance.c
    multiplayer/mp_bridge_relay.c
    multiplayer/mp_follow.c
    multiplayer/mp_follow_difficulty.c
    multiplayer/mp_movie_gate.c
    multiplayer/mp_host_settings_rule.c
    multiplayer/mp_host_settings.c
    multiplayer/mp_world_holds.c
    # The reader of the developer menu's two buttons, which the install arms and withdraws. Its
    # repair closes the chat's open line, so the chat's input comes along with what it stands on.
    multiplayer/mp_player_help_rule.c
    multiplayer/mp_player_help.c
    multiplayer/mp_repair_lock.c
    multiplayer/mp_teleport_host.c
    multiplayer/mp_chat_input.c
    multiplayer/mp_chat_key_rule.c
    multiplayer/mp_signatures_chat.c
    multiplayer/mp_board.c
    multiplayer/mp_score.c
    multiplayer/mp_world_door.c
    multiplayer/mp_signatures_door.c
    multiplayer/mp_trust.c
    multiplayer/mp_text.c
    multiplayer/mp_text_pick.c
    multiplayer/mp_text_second.c
    multiplayer/mp_quest.c
    multiplayer/mp_quest_relay.c
    multiplayer/mp_dialog.c
    multiplayer/mp_dialog_relay.c
    multiplayer/mp_signatures_dialog.c
    multiplayer/mp_voice.c
    multiplayer/mp_voice_report.c
    multiplayer/mp_voice_rule.c
    multiplayer/mp_voice_engine.c
    multiplayer/mp_voice_life.c
    multiplayer/mp_signatures_voice.c
    multiplayer/mp_voice_bind.c
    multiplayer/mp_own_body.c
    multiplayer/mp_bridge_roster.c
    multiplayer/mp_bridge_seats.c
    multiplayer/mp_bridge_lobby.c
    multiplayer/mp_bridge_lobby_late.c
    multiplayer/mp_bridge_announce.c
    multiplayer/mp_bridge_public.c
    multiplayer/mp_bridge_content.c
    multiplayer/mp_bridge_statement.c
    multiplayer/mp_mod_census.c
    multiplayer/mp_mod_folder.c
    multiplayer/mp_mod_allow.c
    multiplayer/mp_mod_manifest_rule.c
    multiplayer/mp_world_values.c
    multiplayer/mp_footstep.c
    multiplayer/mp_bridge_world.c
    multiplayer/mp_bridge_world_check.c
    multiplayer/mp_payload_prefix.c
    multiplayer/mp_relay_crypto.c
    multiplayer/mp_relay_wire.c
    multiplayer/mp_relay_inner.c
    multiplayer/mp_relay_noise.c
    multiplayer/mp_relay_keydoc.c
    multiplayer/mp_relay_keysource.c
    multiplayer/mp_relay_anchors.c
    multiplayer/mp_relay_keyfetch.c
    multiplayer/mp_relay_link.c
    multiplayer/mp_relay_link_leg.c
    multiplayer/mp_relay_seats.c
    multiplayer/mp_relay_socket.c
    multiplayer/mp_relay_transport.c
    multiplayer/mp_relay_listing.c
    multiplayer/mp_relay_list.c
    multiplayer/mp_bridge_report.c
    multiplayer/mp_memory_watch.c
    multiplayer/mp_enemy.c
    multiplayer/mp_menu.c
    multiplayer/mp_menu_screen.c
    multiplayer/mp_menu_screens.c
    multiplayer/mp_menu_screens_join.c
    multiplayer/mp_menu_screens_host.c
    multiplayer/mp_menu_screens_players.c
    multiplayer/mp_menu_screens_lobby.c
    multiplayer/mp_menu_screens_band.c
    multiplayer/mp_menu_screens_choice.c
    multiplayer/mp_levels.c
    multiplayer/mp_saves.c
    multiplayer/mp_savefile.c
    multiplayer/mp_savefile_disk.c
    multiplayer/mp_bridge_savefile.c
    multiplayer/mp_bridge_savefile_look.c
    multiplayer/mp_start.c
    multiplayer/mp_start_restore.c
    multiplayer/mp_start_wish.c
    multiplayer/mp_start_phase.c
    multiplayer/mp_respawn.c
    multiplayer/mp_seat.c
    multiplayer/mp_seat_wish.c
    multiplayer/mp_seat_rule.c
    multiplayer/mp_spawnpoints.c
    multiplayer/mp_signatures_world.c
    multiplayer/mp_settings.c
    multiplayer/mp_settings_ini.c
    multiplayer/mp_browser.c
    multiplayer/mp_announce.c
    multiplayer/mp_announcer.c
    multiplayer/mp_discovery.c
    multiplayer/mp_lobby.c
    multiplayer/mp_rules.c
    multiplayer/mp_footstep.c
    multiplayer/mp_puppet.c
    multiplayer/mp_puppet_shot.c
    multiplayer/mp_puppet_starter.c
    multiplayer/mp_starter_rule.c
    multiplayer/mp_blade.c
    multiplayer/mp_blade_draw.c
    multiplayer/mp_blade_rule.c
    multiplayer/mp_sound.c
    multiplayer/mp_script_sound.c
    multiplayer/mp_script_sound_client.c
    multiplayer/mp_signatures_script_sound.c
    multiplayer/mp_puppet_anim.c
    multiplayer/mp_puppet_anim_engine.c
    multiplayer/mp_puppet_anim_seek.c
    multiplayer/mp_clip_rule.c
    multiplayer/mp_clip_events.c
    multiplayer/mp_signatures_clip.c
    multiplayer/mp_puppet_sabre.c
    multiplayer/mp_effects.c
    multiplayer/mp_node_map.c
    multiplayer/mp_twist.c
    multiplayer/mp_events.c
    multiplayer/mp_world.c
    multiplayer/mp_world_apply.c
    multiplayer/mp_world_digest.c
    multiplayer/mp_world_phase.c
    multiplayer/mp_world_state.c
    multiplayer/mp_crate.c
    multiplayer/mp_crate_client.c
    multiplayer/mp_crate_host.c
    multiplayer/mp_crate_opener.c
    multiplayer/mp_crate_rule.c
    multiplayer/mp_crate_wire.c
    multiplayer/mp_signatures_crate.c
    multiplayer/mp_chat.c
    multiplayer/mp_chat_rule.c
    multiplayer/mp_chat_wire.c
    multiplayer/mp_actions.c
    multiplayer/mp_actions_sabre.c
    multiplayer/mp_body.c
    multiplayer/mp_body_passable.c
    multiplayer/mp_body_gate.c
    multiplayer/mp_signatures_contact.c
    multiplayer/mp_body_shot.c
    multiplayer/mp_shot_sites.c
    multiplayer/mp_body_death.c
    multiplayer/mp_contact_rule.c
    multiplayer/mp_body_asset.c
    multiplayer/mp_body_spawn.c
    multiplayer/mp_body_wear.c
    multiplayer/mp_wear_rule.c
    multiplayer/mp_bank.c
    multiplayer/mp_bank_keep.c
    multiplayer/mp_bank_spawn.c
    multiplayer/mp_damage.c
    multiplayer/mp_damage_entry.c
    multiplayer/mp_player_sound.c
    multiplayer/mp_player_sound_rule.c
    multiplayer/mp_signatures_player_sound.c
    multiplayer/mp_player_sound_play.c
    multiplayer/mp_input.c
    multiplayer/mp_phases.c
    multiplayer/mp_content.c
    multiplayer/mp_scratch.c
    multiplayer/mp_scratch_bind.c
    multiplayer/mp_quest.c
    multiplayer/mp_scratch_wire.c
    ${ENEMY_FAMILY}
    multiplayer/mp_enemy_relay.c
    multiplayer/mp_npc_shot.c
    multiplayer/mp_npc_shot_relay.c
    multiplayer/mp_npc_shot_zap.c
    multiplayer/mp_npc_shot_impact.c
    multiplayer/mp_signatures_shot_impact.c
    multiplayer/mp_pickup_relay.c
    multiplayer/mp_taken.c
    multiplayer/mp_placements.c
    multiplayer/mp_use_latch.c
    multiplayer/mp_hit_relay.c
    multiplayer/mp_hit_relay_death.c
    multiplayer/mp_knockback.c
    multiplayer/mp_task.c
    multiplayer/mp_hit_wire.c
    multiplayer/mp_node_map.c
    multiplayer/mp_hit_attacker.c
    multiplayer/mp_death.c
    multiplayer/mp_target.c
    multiplayer/mp_target_rule.c
    multiplayer/mp_session.c
    multiplayer/mp_session_packets.c
    multiplayer/mp_entropy.c
    multiplayer/mp_session_access.c
    multiplayer/mp_roster.c
    multiplayer/mp_session_io.c
    multiplayer/mp_session_bulk.c
    multiplayer/mp_loopback.c
    multiplayer/mp_transport.c
    multiplayer/mp_channel.c
    multiplayer/mp_channel_state.c
    multiplayer/mp_inbox.c
    multiplayer/mp_hold.c
    multiplayer/mp_session_hold.c
    multiplayer/mp_session_meter.c
    multiplayer/mp_budget_rule.c
    multiplayer/mp_state_note_rule.c
    multiplayer/mp_snapshot.c
    multiplayer/mp_snapshot_history.c
    multiplayer/mp_timeline.c
    multiplayer/mp_interp.c
    multiplayer/mp_wallclock.c
    multiplayer/mp_command.c
    multiplayer/mp_udp.c
    multiplayer/mp_socket.c
)

add_unit_test(mp_bridge multiplayer
    ${BRIDGE_FAMILY}
)

# The client's acknowledgement and its bits through the real receiving path on both ends, host
# payloads built in the room the host's send has; and the dedicated server, whose world a client's
# bridge has to be able to read, over the same loopback and through the client's own receive.
add_unit_test(mp_bridge_acks multiplayer
    ${BRIDGE_FAMILY}
    multiplayer/mp_server.c
    multiplayer/mp_server_log.c
    multiplayer/mp_match.c
    multiplayer/mp_score.c
    multiplayer/mp_clock.c
)
target_link_libraries(mp_bridge_acks_test PRIVATE ws2_32)

# The report of the guarded reads and writes against the log it writes, which the test keeps: the
# arming and its self-test, one level's counts and the next level's nought, and every table full
# with no line past the log's room.
add_unit_test(mp_memory_watch multiplayer
    ${BRIDGE_FAMILY}
)
target_link_libraries(mp_memory_watch_test PRIVATE ws2_32 mp_hacl mbedtls mbedx509 tfpsacrypto
                      bcrypt)

# The two ends of the savegame transfer, driven against a far end the test plays by hand. The lane
# and the codec are proven in mp_session; this is the module that runs the protocol in the game,
# the per peer record on the host and the client's decision about when to stop talking, where a
# defect is silent in play and shows only when a test compiles the file.
add_unit_test(mp_bridge_savefile multiplayer
    ${BRIDGE_FAMILY}
)

# The lobby's state across the doors into a session and out of it, against the real module: a
# lobby line said before the handshake, a fingerprint ready before anybody came, a second session
# begun alone, a new player on a slot, and the one door out.
add_unit_test(mp_bridge_lobby multiplayer
    ${BRIDGE_FAMILY}
)

# A client whose lobby opens on a session that already runs, against the real lobby and the real
# drain: the start held for this player's ready and measured, a start taken while not ready
# counted, a late join told apart from a start in front of the lobby and from a world change, and
# the lobby taking the lobby's notes and dropping the level's.
add_unit_test(mp_bridge_lobby_late multiplayer
    ${BRIDGE_FAMILY}
)
target_link_libraries(mp_bridge_lobby_late_test PRIVATE ws2_32)

# The lobby screen's side of the same join: the frame takes a start only through the one function
# that asks for this player's ready, driven as the frame drives it against the real lobby and
# drain, with the band beside it; a lost host and an error line said ahead of the running
# session's sentences, and a start in front of a ready lobby taken on its first frame.
add_unit_test(mp_menu_screens_lobby multiplayer
    ${BRIDGE_FAMILY}
)
target_link_libraries(mp_menu_screens_lobby_test PRIVATE ws2_32)

# The band of a join refused for the game data, a required mod or a DLL outside this release,
# against the host's real judge over the loopback: the sentence that names the file, cut at the
# name's end in all five languages, and the rows of the player list with each side's value; a
# refusal whose detail names nothing known keeps the reason's word, and a connected client's band
# its ordinary line.
add_unit_test(mp_menu_screens_band multiplayer
    ${BRIDGE_FAMILY}
)
target_link_libraries(mp_menu_screens_band_test PRIVATE ws2_32)

# A listen host greets a client when it enters the host's world, not when it connects: one
# greeting per connection and world, none for another world's state, one for every player at a
# world change with no bank started over, an entry between substeps greeted in the next one, and
# a late client that leaves before entering counted. Real localhost sockets, the host read through
# the bridge's own drain.
add_unit_test(mp_bridge_entry multiplayer
    ${BRIDGE_FAMILY}
)
target_link_libraries(mp_bridge_entry_test PRIVATE ws2_32)
target_link_libraries(mp_bridge_test PRIVATE ws2_32)
target_link_libraries(mp_bridge_savefile_test PRIVATE ws2_32)
target_link_libraries(mp_bridge_lobby_test PRIVATE ws2_32)

# The host's world settings from the lobby's own send to the record a client's mods read, and every
# way out of a session reading running = false, against the real lobby and the real drain.
add_unit_test(mp_host_settings multiplayer
    ${BRIDGE_FAMILY}
)
target_link_libraries(mp_host_settings_test PRIVATE ws2_32)

# The host changes the world and its clients go with it: which of the host's level begins is a
# world change, and what a client does with one, frame by frame.
add_unit_test(mp_follow multiplayer
    ${BRIDGE_FAMILY}
)
target_link_libraries(mp_follow_test PRIVATE ws2_32)

# Every player of a session plays the host's difficulty: written at a client's level begin, again
# after a savegame's restore, held while a level runs, and never in single player, while an exit
# waits or when the host says none. The session and the cell are the test's.
add_unit_test(mp_follow_difficulty multiplayer
    multiplayer/mp_follow_difficulty.c
)

# The five languages the game shipped in, and how the mods that draw their own text choose one.
add_unit_test(language "")

# The multiplayer's own texts: every row in every language ASCII, converting like its English
# text and fitting its width in the font it is drawn in.
add_unit_test(mp_text multiplayer
    multiplayer/mp_text.c
    multiplayer/mp_text_pick.c
    multiplayer/mp_text_second.c
)

# An NPC's bolt on the wire: the codec, which kinds travel, when a held bolt fires and the
# memory that tells a travelling bolt's hit from any other.
add_unit_test(mp_npc_shot multiplayer
    multiplayer/mp_npc_shot.c
    multiplayer/mp_enemy_pack.c
    multiplayer/mp_wire.c
    # whose a shot is, the rule the shot hull asks before a bolt is told as anybody's
    multiplayer/mp_shot_sites.c
)

# The NPC copies an editor spawns: the two messages, the host's table of keys with the engine
# pools' own rule beside it, a client's table, and both tables run together against a model of two
# machines and the channel between them.
add_unit_test(mp_npc_copy_wire multiplayer
    multiplayer/mp_npc_copy_wire.c
    multiplayer/mp_enemy_wire.c
    multiplayer/mp_fieldset.c
    multiplayer/mp_wire.c
)
add_unit_test(mp_npc_copies_host multiplayer
    multiplayer/mp_npc_copies.c
    multiplayer/mp_npc_copy_wire.c
    multiplayer/mp_pool_rule.c
    multiplayer/mp_enemy_wire.c
    multiplayer/mp_fieldset.c
    multiplayer/mp_wire.c
)
add_unit_test(mp_npc_copies_client multiplayer
    multiplayer/mp_npc_copies_client.c
    multiplayer/mp_npc_copy_wire.c
    multiplayer/mp_enemy_wire.c
    multiplayer/mp_fieldset.c
    multiplayer/mp_wire.c
)
add_unit_test(mp_npc_copies_run multiplayer
    multiplayer/mp_npc_copies.c
    multiplayer/mp_npc_copies_client.c
    multiplayer/mp_npc_copies_run.c
    multiplayer/mp_npc_copies_run_host.c
    multiplayer/mp_npc_copies_run_client.c
    multiplayer/mp_npc_copy_wire.c
    multiplayer/mp_pool_rule.c
    multiplayer/mp_enemy_wire.c
    multiplayer/mp_fieldset.c
    multiplayer/mp_wire.c
)
add_unit_test(mp_npc_copies_sim multiplayer
    multiplayer/mp_npc_copies.c
    multiplayer/mp_npc_copies_client.c
    multiplayer/mp_npc_copies_run.c
    multiplayer/mp_npc_copies_run_host.c
    multiplayer/mp_npc_copies_run_client.c
    multiplayer/mp_npc_copy_wire.c
    multiplayer/mp_pool_rule.c
    multiplayer/mp_enemy_wire.c
    multiplayer/mp_fieldset.c
    multiplayer/mp_wire.c
)
target_sources(mp_npc_copies_sim_test PRIVATE mp_npc_copies_model.c mp_npc_copies_model_panel.c)

# The same two machines with the panel's own session rules as both overlays, in place of the
# model's: whatever the model counts as never allowed stays at zero with the real panel answering
# the real multiplayer.
add_unit_test(npc_spawn_session_sim multiplayer
    dev_overlay/npc_spawn_session.c
    multiplayer/mp_npc_copies.c
    multiplayer/mp_npc_copies_client.c
    multiplayer/mp_npc_copies_run.c
    multiplayer/mp_npc_copies_run_host.c
    multiplayer/mp_npc_copies_run_client.c
    multiplayer/mp_npc_copy_wire.c
    multiplayer/mp_pool_rule.c
    multiplayer/mp_enemy_wire.c
    multiplayer/mp_fieldset.c
    multiplayer/mp_wire.c
)
target_sources(npc_spawn_session_sim_test PRIVATE mp_npc_copies_model.c
    mp_npc_copies_model_panel.c)
target_include_directories(npc_spawn_session_sim_test PRIVATE ${MODS}/dev_overlay)

# What a host takes on a player's word: the tags a player may send and a dedicated server passes
# on, the slot a note may name, and a listen host driven through its own drain refusing what only
# a host says and, in a deathmatch, sending away a client whose content differs.
add_unit_test(mp_trust multiplayer
    ${BRIDGE_FAMILY}
)
target_link_libraries(mp_trust_test PRIVATE ws2_32)

# The moments of a body as the four questions a host and a server ask of each: is it an event, may
# a player say it, is it passed on between players, and is it restamped with its sender's slot.
# And a player's sound, 0xA8, as a codec.
add_unit_test(mp_events_moments multiplayer
    ${BRIDGE_FAMILY}
)
target_link_libraries(mp_events_moments_test PRIVATE ws2_32)

# Every decoder above the network layer fed what no honest peer sends, as mp_fuzz does for the
# layer below: a note a codec accepts it says again and alike, the campaign bank is never
# written outside itself, slices and masks lie inside their notes, a host payload splits or
# refuses, and the hit relay claims only its own three messages.
add_unit_test(mp_fuzz_notes multiplayer
    ${BRIDGE_FAMILY}
    multiplayer/mp_score.c
)
target_link_libraries(mp_fuzz_notes_test PRIVATE ws2_32)

# A listen host tells every peer its own world slot, keyed on the connection: two clients, a
# restart from the same address, a leave and the next player on the freed index, on real
# localhost sockets with the host driven through the bridge's own drain.
add_unit_test(mp_bridge_seats multiplayer
    ${BRIDGE_FAMILY}
)
target_link_libraries(mp_bridge_seats_test PRIVATE ws2_32)

# A listen host takes every client's own state into the bank of its slot, and a newcomer, a
# restart included, starts only its own bank over; two clients on localhost, the host read
# through the bridge's own drain.
add_unit_test(mp_bridge_far multiplayer
    ${BRIDGE_FAMILY}
)
target_link_libraries(mp_bridge_far_test PRIVATE ws2_32)

# A client shows every other player the host's world carries, each in the far bank its slot is
# seated in, and gives a bank up when its player has been gone for a second; over the loopback,
# with the host's worlds built by hand.
add_unit_test(mp_bridge_client_far multiplayer
    ${BRIDGE_FAMILY}
)
target_link_libraries(mp_bridge_client_far_test PRIVATE ws2_32)

# The level's state as the module runs it: a note kept and never applied between substeps, the
# host's fog list, the switches refused only when this side can match the host, and every way out
# of a level or a session leaving nothing of it behind.
add_unit_test(mp_level_state multiplayer
    ${BRIDGE_FAMILY}
)
target_link_libraries(mp_level_state_test PRIVATE ws2_32)

# The chat as the module runs it, the far side played by hand: a listen host and two clients on
# real localhost sockets, the host read through the bridge's own drain, every say becoming a line
# for every player with the sayer's slot and name, a line from a client refused, and a client's
# own half through the drain of a client; the one exit forgets it all.
add_unit_test(mp_chat multiplayer
    ${BRIDGE_FAMILY}
)
target_link_libraries(mp_chat_test PRIVATE ws2_32)

# The bridge family carries the relay's transport, and with it the crypto and the TLS library.
foreach(bridge_test mp_bridge mp_bridge_acks mp_bridge_savefile mp_bridge_lobby mp_follow mp_trust
        mp_fuzz_notes mp_chat mp_host_settings mp_bridge_lobby_late mp_bridge_entry
        mp_menu_screens_lobby mp_menu_screens_band
        mp_level_state mp_events_moments
        mp_bridge_seats mp_bridge_far mp_bridge_client_far)
    target_link_libraries(${bridge_test}_test PRIVATE mp_hacl mbedtls mbedx509 tfpsacrypto bcrypt)
endforeach()

# The event codec and ring, pure: a shot or a push that is mistaken for a slot note or an
# acknowledgement moves a player or fires a gun nobody fired, so the recogniser is driven over
# the other two messages' shapes.
add_unit_test(mp_events multiplayer
    multiplayer/mp_events.c
    multiplayer/mp_wire.c
)

# A player's own sounds and shield, the decisions alone: who may send, what a death sends, which
# table the far side reads and with which flags, when a moment is too old, how long a shield lasts.
add_unit_test(mp_player_sound_rule multiplayer
    multiplayer/mp_player_sound_rule.c
)

# And both halves around them, the real modules against an engine the test plays: the repointed
# calls, the hulls' notes, the voice entry with its start gate, the burst and the shield pool; and
# every kind of the moment passed on by a host. The machine around them is answered by the test.
add_unit_test(mp_player_sound multiplayer
    multiplayer/mp_player_sound.c
    multiplayer/mp_player_sound_play.c
    multiplayer/mp_player_sound_rule.c
    multiplayer/mp_events.c
    multiplayer/mp_wire.c
)

# The enemy key and the three messages that carry it: a reported hit, a player hit and a
# removal. A key written as one byte would arrive as another enemy.
add_unit_test(mp_hit_wire multiplayer
    multiplayer/mp_hit_wire.c
    multiplayer/mp_node_map.c
    multiplayer/mp_events.c
    multiplayer/mp_wire.c
)

# The moves of a puppet's track over a far body made up in memory: a seed inside the window left
# at frame 0, a sender behind held with the engine's own bit and released, and a hold another body
# must not write through. Nothing resolves here, so the fallbacks run.
add_unit_test(mp_puppet_anim_seek multiplayer
    multiplayer/mp_puppet_anim.c
    multiplayer/mp_puppet_anim_engine.c
    multiplayer/mp_puppet_anim_seek.c
    multiplayer/mp_clip_rule.c
    multiplayer/mp_clip_events.c
    multiplayer/mp_signatures_clip.c
    multiplayer/mp_cells.c
    multiplayer/mp_content.c
    multiplayer/mp_signatures.c
    multiplayer/mp_signatures_enemy.c
    multiplayer/mp_signatures_foot.c
    multiplayer/mp_signatures_blade.c
    multiplayer/mp_signatures_puppet.c
    multiplayer/mp_cell_sites.c
    multiplayer/mp_menu_sites.c
    multiplayer/mp_lobby_sites.c
)

# The same moves forward through an engine the test plays: a track advance that honours the
# engine's own hold bit and answers the frames it was handed either way, and a dispatcher that
# records its span. The puppet module's three answers are the test's.
add_unit_test(mp_puppet_anim_seek_engine multiplayer
    multiplayer/mp_puppet_anim_seek.c
    multiplayer/mp_clip_rule.c
    multiplayer/mp_clip_events.c
)

# The puppet's animation decisions, pure over plain numbers: a change of ordinal starts with the
# mode the fade bit says and the seed the wire's head says, a head that merely wrapped on a looping
# clip is never a restart, a swing clip is only ever recorded, the track guard is the overlay
# player's own search, and an event's due-ness reads its tick difference signed. The engine half
# comes along because the file names it; nothing resolves here, which the last section pins.
add_unit_test(mp_puppet_anim multiplayer
    multiplayer/mp_puppet_anim.c
    multiplayer/mp_puppet_anim_engine.c
    multiplayer/mp_puppet_anim_seek.c
    multiplayer/mp_clip_rule.c
    multiplayer/mp_clip_events.c
    multiplayer/mp_signatures_clip.c
    multiplayer/mp_cells.c
    multiplayer/mp_content.c
    multiplayer/mp_signatures.c
    multiplayer/mp_signatures_enemy.c
    multiplayer/mp_signatures_foot.c
    multiplayer/mp_signatures_blade.c
    multiplayer/mp_signatures_puppet.c
    multiplayer/mp_cell_sites.c
    multiplayer/mp_menu_sites.c
    multiplayer/mp_lobby_sites.c
)

# What the puppet's node rotations are DRAWN as between two substeps, pure: the two ends of the
# pair, a collapsed pair answering the same at every weight, the shortest arc across the half turn,
# and a rotation that ends running out to zero rather than dropping there. The bank, the cells and
# the interpolator come along because the module names them; nothing resolves here, which is what
# the last section pins. The far blades' frame witness comes along because the world draw's hull
# calls it.
add_unit_test(mp_twist multiplayer
    multiplayer/mp_twist.c
    multiplayer/mp_blade_draw.c
    multiplayer/mp_blade_rule.c
    multiplayer/mp_bank.c
    multiplayer/mp_bank_keep.c
    multiplayer/mp_bank_spawn.c
    multiplayer/mp_interp.c
    multiplayer/mp_timeline.c
    multiplayer/mp_snapshot.c
    multiplayer/mp_snapshot_history.c
    multiplayer/mp_wire.c
    multiplayer/mp_cells.c
    multiplayer/mp_content.c
    multiplayer/mp_signatures.c
    multiplayer/mp_signatures_enemy.c
    multiplayer/mp_signatures_foot.c
    multiplayer/mp_signatures_blade.c
    multiplayer/mp_signatures_puppet.c
    multiplayer/mp_cell_sites.c
    multiplayer/mp_menu_sites.c
    multiplayer/mp_lobby_sites.c
)

# The puppet's sabre decisions, pure: which stores a block and a parry make and a swing does
# not, which operands are refused (a row past the table, a clip outside the block and parry
# ranges), whether a block takes the aux slot or waits for a weapon change to finish, and when
# the fallback disarms a blade whose disarm event never came. The animation module comes along
# for the track guard and the clip bound, and the body wear family because a swing asks whether
# its body wears a borrowed model; nothing resolves here, which the section on the engine half
# pins.
add_unit_test(mp_puppet_sabre multiplayer
    ${BODY_WEAR_FAMILY}
    multiplayer/mp_armed.c
    multiplayer/mp_events.c
    multiplayer/mp_wire.c
    multiplayer/mp_body.c
    multiplayer/mp_body_passable.c
    multiplayer/mp_body_gate.c
    multiplayer/mp_signatures_contact.c
    multiplayer/mp_body_shot.c
    multiplayer/mp_shot_sites.c
    multiplayer/mp_body_death.c
    multiplayer/mp_contact_rule.c
    multiplayer/mp_body_asset.c
    multiplayer/mp_body_spawn.c
    multiplayer/mp_effects.c
    multiplayer/mp_node_map.c
    multiplayer/mp_bank.c
    multiplayer/mp_bank_keep.c
    multiplayer/mp_bank_spawn.c
    multiplayer/mp_capacity.c
    multiplayer/mp_phases.c
    multiplayer/mp_pool.c
    multiplayer/mp_pool_rule.c
    multiplayer/mp_cells.c
    multiplayer/mp_content.c
    multiplayer/mp_signatures.c
    multiplayer/mp_signatures_enemy.c
    multiplayer/mp_signatures_foot.c
    multiplayer/mp_signatures_blade.c
    multiplayer/mp_signatures_puppet.c
    multiplayer/mp_cell_sites.c
    multiplayer/mp_menu_sites.c
    multiplayer/mp_lobby_sites.c
)

# The map's own message and the measurement built on it, pure: the digest's round trip and its
# refusals, the cap, the recogniser driven over every event and over both foreign messages on the
# channel, the mover event's own codec, and the deviation with the one type whose pose wraps. The
# engine half comes along because the file names it; nothing resolves here, which is what the last
# section pins.
add_unit_test(mp_world multiplayer
    multiplayer/mp_world.c
    multiplayer/mp_crate_opener.c
    multiplayer/mp_crate_rule.c
    multiplayer/mp_armed.c
    multiplayer/mp_world_apply.c
    multiplayer/mp_world_digest.c
    multiplayer/mp_world_phase.c
    multiplayer/mp_world_state.c
    multiplayer/mp_scratch.c
    multiplayer/mp_scratch_bind.c
    multiplayer/mp_quest.c
    multiplayer/mp_events.c
    multiplayer/mp_wire.c
    multiplayer/mp_puppet_anim.c
    multiplayer/mp_puppet_anim_engine.c
    multiplayer/mp_puppet_anim_seek.c
    multiplayer/mp_clip_rule.c
    multiplayer/mp_clip_events.c
    multiplayer/mp_signatures_clip.c
    multiplayer/mp_cells.c
    multiplayer/mp_content.c
    multiplayer/mp_signatures.c
    multiplayer/mp_signatures_enemy.c
    multiplayer/mp_signatures_foot.c
    multiplayer/mp_signatures_blade.c
    multiplayer/mp_signatures_puppet.c
    multiplayer/mp_cell_sites.c
    multiplayer/mp_menu_sites.c
    multiplayer/mp_lobby_sites.c
)

# The host's description of the movers that are away from the position the level authored, pure:
# the three decisions read off the engine's own integrator arm by arm, the note's round trip and
# its refusals, the length family against every other message on the reliable channel, and the
# generation guard that keeps a note from before a level change out of the map after it.
add_unit_test(mp_world_state multiplayer
    multiplayer/mp_world_state.c
    multiplayer/mp_world_digest.c
    multiplayer/mp_wire.c
)

# The phase controller for the map's free runners, pure over a handful of numbers: the short way
# round the ring and its sign, a dead band that is the grain the two machines sample to, a debt
# paid off in bites strictly smaller than a substep and in a bounded number of them, a mover
# nobody integrates collecting nothing, and a pose that jumps further than the engine's own frame
# time cap allows dropping the debt instead of paying it into a world that is gone.
add_unit_test(mp_world_phase multiplayer
    multiplayer/mp_world_phase.c
)

# The table driven wire record, pure: a full record round trips, a body that did not move costs
# the mask alone, one moved field costs the mask plus that field, a value too wide for its row is
# refused before a byte is written, and a truncated record is refused rather than read past.
add_unit_test(mp_fieldset multiplayer
    multiplayer/mp_fieldset.c
)

# What the menu produces, pure: what counts as an address while it is being typed, an endpoint read
# and written back, and a remembered list that moves a repeat to its head and forgets its oldest
# rather than growing. The address parser is hand written, so the checks are aimed at what the
# library parsers accept and should not.
#
# The decisions that follow from the intention are here as well, and they are here rather than in
# the screen so that they can be checked with no game in the process: how many may sit in a session
# of each game, which control rows a lobby has for each side and each game, which game a client is
# actually showing, and whether everybody has said they are ready.
add_unit_test(mp_settings multiplayer
    multiplayer/mp_settings.c
    multiplayer/mp_lobby.c
    multiplayer/mp_rules.c

    multiplayer/mp_wire.c)

# The impact effects a replicated blade owes its own body, pure: the b flag is the whole fork, a
# blade code and a body code name different voice groups, the module's own cooldown stands in for
# the one the engine writes into a record this feature does not own, and a code outside both sets
# answers nothing rather than flashing at a point nobody chose. The engine half comes along
# because the file names it; nothing resolves here, which the last section pins.
add_unit_test(mp_effects multiplayer
    multiplayer/mp_effects.c
    multiplayer/mp_node_map.c
    ${BODY_WEAR_FAMILY}
    multiplayer/mp_armed.c
    multiplayer/mp_events.c
    multiplayer/mp_wire.c
    multiplayer/mp_body.c
    multiplayer/mp_body_passable.c
    multiplayer/mp_body_gate.c
    multiplayer/mp_signatures_contact.c
    multiplayer/mp_body_shot.c
    multiplayer/mp_shot_sites.c
    multiplayer/mp_body_death.c
    multiplayer/mp_contact_rule.c
    multiplayer/mp_body_asset.c
    multiplayer/mp_body_spawn.c
    multiplayer/mp_bank.c
    multiplayer/mp_bank_keep.c
    multiplayer/mp_bank_spawn.c
    multiplayer/mp_capacity.c
    multiplayer/mp_phases.c
    multiplayer/mp_pool.c
    multiplayer/mp_pool_rule.c
    multiplayer/mp_cells.c
    multiplayer/mp_content.c
    multiplayer/mp_signatures.c
    multiplayer/mp_signatures_enemy.c
    multiplayer/mp_signatures_foot.c
    multiplayer/mp_signatures_blade.c
    multiplayer/mp_signatures_puppet.c
    multiplayer/mp_cell_sites.c
    multiplayer/mp_menu_sites.c
    multiplayer/mp_lobby_sites.c
)

# What a host says about itself to a room that has not asked: every field over the wire, everything
# a stranger can claim that has to be refused, and the two claims that must NOT be refused because
# a server this build cannot join still has to be shown as one.
add_unit_test(mp_announce multiplayer
    multiplayer/mp_announce.c
    multiplayer/mp_wire.c
)

# The session list a player picks from, driven over its own clock: one endpoint is one row whatever
# it is heard from, a lost datagram does not remove a row, a favourite never leaves, the order does
# not move while nothing changed, and the selection follows the row rather than the position.
add_unit_test(mp_browser multiplayer
    multiplayer/mp_browser.c
    multiplayer/mp_announce.c
    multiplayer/mp_settings.c
    multiplayer/mp_wire.c

    multiplayer/mp_rules.c
    multiplayer/mp_lobby.c
    # the statement's fingerprint, which a host announces and the join screen judges the rows by
    multiplayer/mp_mod_manifest_rule.c)

# The list a canvas widget draws, over its edges: rows fit the window the way the pixels say, the
# selection drags the window by exactly one row, the wheel moves the window and not the selection,
# a list that shrinks leaves the keyboard on a row that exists, and the thumb is the window over
# the list.
add_unit_test(mp_canvas multiplayer
    multiplayer/mp_canvas.c
)

# The announce socket over the loopback: a listener on a test port and a sender addressing the
# machine itself are the whole path minus the routing. Two listeners share the port, a broadcast
# is at least accepted by the stack, and a closed socket refuses everything.
add_unit_test(mp_discovery multiplayer
    multiplayer/mp_discovery.c
    multiplayer/mp_announce.c
    multiplayer/mp_socket.c
    multiplayer/mp_wire.c
)

# The host's announcer over its own clock: silent until configured and enabled, one announce a
# cycle and not one more, the socket up only once there is something to say, a refused claim
# counted rather than retried at once.
add_unit_test(mp_announcer multiplayer
    multiplayer/mp_announcer.c
    multiplayer/mp_discovery.c
    multiplayer/mp_announce.c
    multiplayer/mp_socket.c
    multiplayer/mp_wire.c
)
target_link_libraries(mp_announcer_test PRIVATE ws2_32)

# Where a body may be seated, pure: the ring each slot starts in, what a floor reading makes of a
# candidate, a body in the way, the order the standing players are tried in, the clock that ends
# a search which finds nothing, and the authored point a co-operative fallback takes.
add_unit_test(mp_seat_rule multiplayer multiplayer/mp_seat_rule.c)

# And the search on the engine side: what a ground reading means, and a wish that ends even where
# nothing can be probed. The engine-facing family comes along because the module names it; nothing
# resolves in a test process, and the last section pins that every entry point refuses.
add_unit_test(mp_seat multiplayer
    multiplayer/mp_seat.c
    multiplayer/mp_seat_wish.c
    multiplayer/mp_seat_rule.c
    multiplayer/mp_spawnpoints.c
    multiplayer/mp_placements.c
    multiplayer/mp_signatures_world.c
    multiplayer/mp_cells.c
    multiplayer/mp_cell_sites.c
    multiplayer/mp_menu_sites.c
    multiplayer/mp_lobby_sites.c
    multiplayer/mp_signatures.c
    multiplayer/mp_signatures_enemy.c
    multiplayer/mp_signatures_foot.c
    multiplayer/mp_signatures_blade.c
    multiplayer/mp_signatures_puppet.c
)

# Where a dead player comes back, and the ground he comes back on. The engine has probes for
# both questions and they answer in the opposite direction from what their names suggest:
# zero means walkable and zero means clear. Getting either backwards puts a player inside a
# wall, so both are driven here in both directions.
add_unit_test(mp_respawn multiplayer
    multiplayer/mp_respawn.c
    multiplayer/mp_seat.c
    multiplayer/mp_seat_wish.c
    multiplayer/mp_seat_rule.c
    multiplayer/mp_spawnpoints.c
    multiplayer/mp_placements.c
    multiplayer/mp_signatures_world.c
    multiplayer/mp_cells.c
    multiplayer/mp_cell_sites.c
    multiplayer/mp_menu_sites.c
    multiplayer/mp_lobby_sites.c
    multiplayer/mp_signatures.c
    multiplayer/mp_signatures_enemy.c
    multiplayer/mp_signatures_foot.c
    multiplayer/mp_signatures_blade.c
    multiplayer/mp_signatures_puppet.c
)

# Which of a level's authored standing positions an arena can put somebody on. The thinning
# is order dependent, so seeding the level start can LOWER the count rather than raise it,
# and the weakest shipped level keeps six points. Both facts are pinned here.
add_unit_test(mp_spawnpoints multiplayer
    multiplayer/mp_spawnpoints.c
    multiplayer/mp_seat_rule.c
    multiplayer/mp_placements.c
    multiplayer/mp_cells.c
    multiplayer/mp_cell_sites.c
    multiplayer/mp_menu_sites.c
    multiplayer/mp_lobby_sites.c
    multiplayer/mp_signatures.c
    multiplayer/mp_signatures_enemy.c
    multiplayer/mp_signatures_foot.c
    multiplayer/mp_signatures_blade.c
    multiplayer/mp_signatures_puppet.c
)

# The seven rules of a session, as a value. Every one of them is a number that has to
# survive a wire and a bound in both directions, and the two penalties travel biased so
# that the range check is the same comparison on both sides rather than a signed byte.
add_unit_test(mp_rules multiplayer
    multiplayer/mp_rules.c
    multiplayer/mp_lobby.c
    multiplayer/mp_wire.c
)

# Which of the two rule sets a death falls under, and whether the switch that lets a death leave
# the level running may go on at all. The four decisions are pure; the rest of the list is what
# the module names, and none of it resolves in a test process, which the last section pins.
#
# The damage module comes along because the switch is its own: the file that decides may not hand
# the decision to a caller who might forget to apply it.
add_unit_test(mp_reentry multiplayer
    multiplayer/mp_reentry.c
    ${BODY_WEAR_FAMILY}
    multiplayer/mp_respawn.c
    multiplayer/mp_seat.c
    multiplayer/mp_seat_wish.c
    multiplayer/mp_seat_rule.c
    multiplayer/mp_spawnpoints.c
    multiplayer/mp_placements.c
    multiplayer/mp_signatures_world.c
    multiplayer/mp_rules.c
    multiplayer/mp_lobby.c
    multiplayer/mp_damage.c
    multiplayer/mp_damage_entry.c
    multiplayer/mp_player_sound.c
    multiplayer/mp_player_sound_rule.c
    multiplayer/mp_signatures_player_sound.c
    multiplayer/mp_hit_relay.c
    multiplayer/mp_hit_relay_death.c
    multiplayer/mp_knockback.c
    multiplayer/mp_task.c
    multiplayer/mp_hit_wire.c
    multiplayer/mp_node_map.c
    multiplayer/mp_hit_attacker.c
    multiplayer/mp_death.c
    multiplayer/mp_trust.c
    multiplayer/mp_events.c
    multiplayer/mp_target.c
    multiplayer/mp_target_rule.c
    ${ENEMY_FAMILY}
    multiplayer/mp_body.c
    multiplayer/mp_body_passable.c
    multiplayer/mp_body_gate.c
    multiplayer/mp_signatures_contact.c
    multiplayer/mp_body_shot.c
    multiplayer/mp_shot_sites.c
    multiplayer/mp_body_death.c
    multiplayer/mp_contact_rule.c
    multiplayer/mp_body_asset.c
    multiplayer/mp_body_spawn.c
    multiplayer/mp_effects.c
    multiplayer/mp_node_map.c
    multiplayer/mp_bank.c
    multiplayer/mp_bank_keep.c
    multiplayer/mp_bank_spawn.c
    multiplayer/mp_phases.c
    multiplayer/mp_content.c
)

# The rule set, the re-entry, the seat search, the spawn points and the arrival together, against a
# small world the test plays: the three probes, the cells, the engine's own re-entry, the wire and
# the placement are fields and functions of the test file, so none of the engine side is linked.
# What it pins is the way the modules hand a player to each other: who decides when nobody stands,
# where a seat may be taken, and how long an arrival waits.
add_unit_test(mp_seat_world multiplayer
    multiplayer/mp_arrival.c
    multiplayer/mp_seat_order.c
    multiplayer/mp_reentry.c
    multiplayer/mp_respawn.c
    multiplayer/mp_seat.c
    multiplayer/mp_seat_wish.c
    multiplayer/mp_seat_rule.c
    multiplayer/mp_spawnpoints.c
)
target_sources(mp_seat_world_test PRIVATE mp_seat_order_no_session.c)

# Two clients arriving beside the host at once, through the real seat search, its wish and the
# order that reads the roster: the lower slots' seats foreseen and kept clear, the slots from the
# roster rather than the banks, the two levels of a field run, and the watch on a seat handed out.
# The probes, the cells, the roster and the far banks are the test's own.
add_unit_test(mp_seat_order multiplayer
    multiplayer/mp_seat_order.c
    multiplayer/mp_seat.c
    multiplayer/mp_seat_wish.c
    multiplayer/mp_seat_rule.c
    multiplayer/mp_spawnpoints.c
)
target_sources(mp_seat_order_test PRIVATE mp_seat_little_world.c)

# The one ring loop of the seat search, through the real search on the same little world: the
# slot's order of directions pinned against the formula it always had, for every slot and every
# set of blocked directions, and the reading of the floor under the anchor it hands back.
add_unit_test(mp_seat_loop multiplayer
    multiplayer/mp_seat.c
    multiplayer/mp_seat_wish.c
    multiplayer/mp_seat_rule.c
    multiplayer/mp_spawnpoints.c
)
target_sources(mp_seat_loop_test PRIVATE mp_seat_little_world.c)

# A fan the host reports every substep, and one death for it: the dispatcher and its gate, the
# attribution, the death hull with its one entry per life, the rule set, the re-entry and the seat
# search, against an engine the test plays, the task scheduler's delivery and a host a few substeps
# late included. The rest of what the body module names is answered by the test.
add_unit_test(mp_body_gate multiplayer
    multiplayer/mp_body.c
    multiplayer/mp_body_passable.c
    multiplayer/mp_body_gate.c
    multiplayer/mp_body_death.c
    multiplayer/mp_contact_rule.c
    multiplayer/mp_damage.c
    multiplayer/mp_damage_entry.c
    multiplayer/mp_player_sound.c
    multiplayer/mp_player_sound_rule.c
    multiplayer/mp_signatures_player_sound.c
    multiplayer/mp_reentry.c
    multiplayer/mp_respawn.c
    multiplayer/mp_seat.c
    multiplayer/mp_seat_wish.c
    multiplayer/mp_seat_rule.c
    multiplayer/mp_spawnpoints.c
)
target_sources(mp_body_gate_test PRIVATE mp_body_gate_neighbours.c)


# Who is winning, and when the round is over. Pure arithmetic over a clock that is the
# host's substep count rather than a wall clock, because two machines have two zero
# points and simulated time falls behind a wall clock under load.
add_unit_test(mp_score multiplayer
    multiplayer/mp_score.c
    multiplayer/mp_rules.c
    multiplayer/mp_lobby.c
    multiplayer/mp_wire.c
)

# What the live scoreboard decides before it draws: whether it is on screen, which rows it shows
# and in what order, how wide each column has to be, and what a clock and a result read as. The
# widths are held against the font's own advance table, and the space is the case that matters:
# it carries the pen five units and has no glyph rectangle at all, so the engine's own string
# measurement answers one for it and a column sized that way is short by four per space.
add_unit_test(mp_board multiplayer
    multiplayer/mp_board.c
    multiplayer/mp_text.c
    multiplayer/mp_text_pick.c
    multiplayer/mp_text_second.c
    multiplayer/mp_roster.c
    multiplayer/mp_score.c
    multiplayer/mp_rules.c
    multiplayer/mp_lobby.c
    multiplayer/mp_wire.c
)

# The two notes a lobby speaks, pure: a player's four bytes and the host's setup, and everything
# that is not one.
add_unit_test(mp_lobby multiplayer
    multiplayer/mp_lobby.c
    multiplayer/mp_rules.c
    multiplayer/mp_wire.c
)

# What a client in its lobby takes of the reliable notes and what it drops, pure: every tag of the
# wire with its class, and a census of the band, so a tag added without a row fails here.
add_unit_test(mp_lobby_note_rule multiplayer
    multiplayer/mp_lobby_note_rule.c
)

# Which pickups are gone, absolutely rather than as a change. The codec only: what it is FOR is
# a client that joined an hour in and would otherwise heal itself on medipacks the host used up
# before it connected, and that half is in the relay.
add_unit_test(mp_taken multiplayer
    multiplayer/mp_taken.c
)

# The thirty-four story bits every player shares: the band's edges against the hero bits either
# side of them, the splice that must not spill, and the two notes. The edge cases are the whole
# point: a splice one bit wide in the wrong direction hands one player another player's key
# and nothing anywhere says so.
add_unit_test(mp_quest multiplayer
    multiplayer/mp_quest.c
)

# One spoken line on the wire. Small surface, and two of its refusals are the only thing between
# a stranger's bytes and a read off the end of the dialogue book.
add_unit_test(mp_dialog multiplayer
    multiplayer/mp_dialog.c
    multiplayer/mp_wire.c
)

# Whether this machine sees and hears a spoken line: the one rule for host and client, its edges,
# the scene, the place handed to the engine, the reach read out of two immediates, and the verdict
# held for the line on show. Pure.
add_unit_test(mp_voice_rule multiplayer
    multiplayer/mp_voice_rule.c
)

# Where the judgement reaches the engine: the reply's call, bound first and alone, against a window
# of the test's own, so that it has to resolve the voice's sites before it reads them.
add_unit_test(mp_voice_bind multiplayer
    multiplayer/mp_voice_bind.c
    multiplayer/mp_voice_rule.c
)

# The judgement carried out: the hulls and the speak entry's two halves against cells, a field
# setter, a six byte dialogue module and a bank of twelve channels of the test's own, with the
# session read by the tree's one reading of the setup note. Two programs share the stand-in: one for
# the session, the scene and the resting values, one for the hearing radius and the engine's answer.
set(VOICE_FAMILY
    multiplayer/mp_voice.c
    multiplayer/mp_voice_report.c
    multiplayer/mp_voice_rule.c
    multiplayer/mp_voice_engine.c
    multiplayer/mp_voice_life.c
    multiplayer/mp_session_now.c
    multiplayer/mp_scene_rule.c
    multiplayer/mp_armed.c
    multiplayer/mp_stopwatch.c
)
# Where this machine's own body stands, over a hero block and an object the test lays out, in each
# of the five module states: the send and the judgement of a spoken line take one answer.
add_unit_test(mp_own_body multiplayer
    multiplayer/mp_own_body.c
)

add_unit_test(mp_voice multiplayer ${VOICE_FAMILY})
target_sources(mp_voice_test PRIVATE mp_voice_stand_in.c)
add_unit_test(mp_voice_heard multiplayer ${VOICE_FAMILY})
target_sources(mp_voice_heard_test PRIVATE mp_voice_stand_in.c)

# The dedicated server's match authority: which side a joiner goes on, whether a swap is allowed,
# when a round ends and what happens next. All of it is what a listen host gets from a lobby
# screen and a running game, and what a server with neither has to decide for itself.
add_unit_test(mp_match multiplayer
    multiplayer/mp_match.c
    multiplayer/mp_score.c
    multiplayer/mp_rules.c
    multiplayer/mp_lobby.c
    multiplayer/mp_death.c
    multiplayer/mp_wire.c
    multiplayer/mp_clock.c
)

# What a savegame says about itself: the three fields a lobby needs out of its header, and every
# shape of file that is not one. The parser is pure, so the test writes the bytes itself.
add_unit_test(mp_saves multiplayer
    multiplayer/mp_saves.c
    multiplayer/mp_text.c
    multiplayer/mp_text_pick.c
    multiplayer/mp_text_second.c
)

# The reason a file is no choice, kept apart: the received savegame on the disk, whether it is
# there already and putting it there, against real files, a neighbour that holds the file the
# way a write does and one that reads it the way the engine's restore does.
add_unit_test(mp_savefile_disk multiplayer
    multiplayer/mp_savefile_disk.c
)

# What the music of a player is owed after his own death in a level that goes on: the bed he
# had put back half a second after he stands, the death piece taken out of the sequence latch,
# and every case in which a newer word stands and nothing is put over it.
add_unit_test(mp_death_music_rule multiplayer
    multiplayer/mp_death_music_rule.c
)

# A savegame cut into notes for the reliable channel and put back together on the other side:
# the digest that names a file, the chunk whose fields are held against each other, and the
# assembly that is complete when the bytes hash to their name and not when the count says so.
add_unit_test(mp_savefile multiplayer
    multiplayer/mp_savefile.c
    multiplayer/mp_wire.c
)

# Which levels this installation can offer: the game's own eleven and anything else lying in the
# level folder. The two pure halves are the whole of the awkwardness, the name a file gets when
# nobody named it and the join that puts this machine's root in front, and the engine-side halves
# are checked for refusing, because nothing resolves in a test process.
add_unit_test(mp_levels multiplayer
    multiplayer/mp_levels.c
    multiplayer/mp_lobby.c
    multiplayer/mp_rules.c
    multiplayer/mp_wire.c
    multiplayer/mp_cells.c
    multiplayer/mp_content.c
    multiplayer/mp_signatures.c
    multiplayer/mp_signatures_enemy.c
    multiplayer/mp_signatures_foot.c
    multiplayer/mp_signatures_blade.c
    multiplayer/mp_signatures_puppet.c
    multiplayer/mp_cell_sites.c
    multiplayer/mp_menu_sites.c
    multiplayer/mp_lobby_sites.c
)

# The three decisions the lobby's engine side makes on its own: what a tick does with a wish that
# is waiting for two engine gates, the hero clamp that stands between a number off the wire and a
# table the engine indexes raw, and whether a pose another machine computed is a finite one. It
# also holds the teleport's enumerator against its own row, because the site table is positional
# and a row inserted one place out resolves under its neighbour's name in silence. The
# engine-facing family comes along because the module names it; nothing resolves in a test process,
# and the last section is there to prove every entry point refuses rather than dereferences.
add_unit_test(mp_start multiplayer
    multiplayer/mp_start.c
    multiplayer/mp_start_restore.c
    multiplayer/mp_start_wish.c
    multiplayer/mp_start_phase.c
    multiplayer/mp_respawn.c
    multiplayer/mp_seat.c
    multiplayer/mp_seat_wish.c
    multiplayer/mp_seat_rule.c
    multiplayer/mp_spawnpoints.c
    multiplayer/mp_placements.c
    multiplayer/mp_signatures_world.c
    multiplayer/mp_levels.c
    multiplayer/mp_saves.c
    multiplayer/mp_text.c
    multiplayer/mp_text_pick.c
    multiplayer/mp_text_second.c
    multiplayer/mp_lobby.c
    multiplayer/mp_rules.c
    multiplayer/mp_wire.c
    multiplayer/mp_cells.c
    multiplayer/mp_content.c
    multiplayer/mp_signatures.c
    multiplayer/mp_signatures_enemy.c
    multiplayer/mp_signatures_foot.c
    multiplayer/mp_signatures_blade.c
    multiplayer/mp_signatures_puppet.c
    multiplayer/mp_cell_sites.c
    multiplayer/mp_menu_sites.c
    multiplayer/mp_lobby_sites.c
)

# Where a client's body stands when the level it entered is the host's saved game. Three
# decisions: whether a run wants the offset at all, whether a resolved far pose is one to
# stand beside, and what one tick does with an armed one. The four bridge answers are stubbed
# in the test rather than linked, because they are exactly what the test has to control and
# the real ones would drag sixty files in to describe a session that does not exist. The seat
# search and the placement are the real ones, and the last section pins that both refuse here.
add_unit_test(mp_arrival multiplayer
    multiplayer/mp_arrival.c
    multiplayer/mp_seat_order.c
    multiplayer/mp_respawn.c
    multiplayer/mp_seat.c
    multiplayer/mp_seat_wish.c
    multiplayer/mp_seat_rule.c
    multiplayer/mp_spawnpoints.c
    multiplayer/mp_placements.c
    multiplayer/mp_start.c
    multiplayer/mp_start_restore.c
    multiplayer/mp_start_wish.c
    multiplayer/mp_start_phase.c
    multiplayer/mp_signatures_world.c
    multiplayer/mp_levels.c
    multiplayer/mp_saves.c
    multiplayer/mp_text.c
    multiplayer/mp_text_pick.c
    multiplayer/mp_text_second.c
    multiplayer/mp_lobby.c
    multiplayer/mp_rules.c
    multiplayer/mp_wire.c
    multiplayer/mp_cells.c
    multiplayer/mp_content.c
    multiplayer/mp_signatures.c
    multiplayer/mp_signatures_enemy.c
    multiplayer/mp_signatures_foot.c
    multiplayer/mp_signatures_blade.c
    multiplayer/mp_signatures_puppet.c
    multiplayer/mp_cell_sites.c
    multiplayer/mp_menu_sites.c
    multiplayer/mp_lobby_sites.c
)
target_sources(mp_arrival_test PRIVATE mp_seat_order_no_session.c)

# Where a sound is placed for a body this machine does not listen from, pure: which calls at the
# funnel may be re-pointed, both flags the copy has to carry, the shape of the record it copies, and
# a window inside a window putting back the anchor it replaced rather than clearing it. The engine
# half comes along because the file names it; nothing resolves here, which the last section pins.
# The enemy block family comes with it, because the clang hands its cooldown's verdict to the enemy
# module, and that module asks the sending half whether this side describes its enemies.
add_unit_test(mp_sound multiplayer
    multiplayer/mp_sound.c
    ${ENEMY_FAMILY}
)

# The damage module's provocation schedule over its edges, its three way death route, and the
# install refusing with nothing resolved. The body family comes along because the module names it;
# on this machine every site resolves to nothing, which is what the refusal checks pin down.
#
# The hit relay and the enemy binding are here because the death message is the relay's: it lives
# in mp_hit_relay_death.c and arrives through the relay's distributor. Who died and who did it is
# the same subject as who hit whom, and both are decided from the same two owner pointers. The
# relay's own files and those after them, to mp_target_rule.c, are the whole of what that include
# chain pulls in; the count of the contacts with code 0x22 and the substep task come with the relay.
add_unit_test(mp_damage multiplayer
    multiplayer/mp_damage.c
    multiplayer/mp_damage_entry.c
    multiplayer/mp_player_sound.c
    multiplayer/mp_player_sound_rule.c
    multiplayer/mp_signatures_player_sound.c
    multiplayer/mp_respawn.c
    multiplayer/mp_seat.c
    multiplayer/mp_seat_wish.c
    multiplayer/mp_seat_rule.c
    multiplayer/mp_spawnpoints.c
    multiplayer/mp_placements.c
    multiplayer/mp_signatures_world.c
    ${BODY_WEAR_FAMILY}
    multiplayer/mp_hit_relay.c
    multiplayer/mp_hit_relay_death.c
    multiplayer/mp_knockback.c
    multiplayer/mp_task.c
    multiplayer/mp_hit_wire.c
    multiplayer/mp_node_map.c
    multiplayer/mp_hit_attacker.c
    multiplayer/mp_death.c
    multiplayer/mp_trust.c
    multiplayer/mp_events.c
    multiplayer/mp_target.c
    multiplayer/mp_target_rule.c
    ${ENEMY_FAMILY}
    multiplayer/mp_body.c
    multiplayer/mp_body_passable.c
    multiplayer/mp_body_gate.c
    multiplayer/mp_signatures_contact.c
    multiplayer/mp_body_shot.c
    multiplayer/mp_shot_sites.c
    multiplayer/mp_body_death.c
    multiplayer/mp_contact_rule.c
    multiplayer/mp_body_asset.c
    multiplayer/mp_body_spawn.c
    multiplayer/mp_effects.c
    multiplayer/mp_node_map.c
    multiplayer/mp_bank.c
    multiplayer/mp_bank_keep.c
    multiplayer/mp_bank_spawn.c
    multiplayer/mp_phases.c
    multiplayer/mp_content.c
)

# The body module's co-op classes and its refusals with no game: the four classes must be
# distinct and clear of the enemy and ally values, and every entry point must refuse cleanly when
# nothing resolved. The engine choreography itself is proven in game, not here.
add_unit_test(mp_body multiplayer
    multiplayer/mp_body.c
    multiplayer/mp_body_passable.c
    multiplayer/mp_body_gate.c
    multiplayer/mp_signatures_contact.c
    multiplayer/mp_body_shot.c
    ${BODY_WEAR_FAMILY}
    multiplayer/mp_lobby.c
    multiplayer/mp_rules.c
    multiplayer/mp_armed.c
    multiplayer/mp_events.c
    multiplayer/mp_wire.c
    multiplayer/mp_shot_sites.c
    multiplayer/mp_body_death.c
    multiplayer/mp_contact_rule.c
    multiplayer/mp_body_asset.c
    multiplayer/mp_body_spawn.c
    multiplayer/mp_effects.c
    multiplayer/mp_node_map.c
    multiplayer/mp_bank.c
    multiplayer/mp_bank_keep.c
    multiplayer/mp_bank_spawn.c
    multiplayer/mp_capacity.c
    multiplayer/mp_phases.c
    multiplayer/mp_pool.c
    multiplayer/mp_pool_rule.c
    multiplayer/mp_cells.c
    multiplayer/mp_content.c
    multiplayer/mp_signatures.c
    multiplayer/mp_signatures_enemy.c
    multiplayer/mp_signatures_foot.c
    multiplayer/mp_signatures_blade.c
    multiplayer/mp_signatures_puppet.c
    multiplayer/mp_cell_sites.c
    multiplayer/mp_menu_sites.c
    multiplayer/mp_lobby_sites.c
)

# The sabre sender in a process with no game: a null listener installs nothing, a real one finds
# no site and stands no hull, and every counter stays at zero. The four hulls themselves are
# engine choreography and are proven in game. The bank comes along because the hulls ask it which
# class is active.
add_unit_test(mp_actions_sabre multiplayer
    multiplayer/mp_actions_sabre.c
    multiplayer/mp_armed.c
    multiplayer/mp_bank.c
    multiplayer/mp_bank_keep.c
    multiplayer/mp_bank_spawn.c
    multiplayer/mp_cells.c
    multiplayer/mp_content.c
    multiplayer/mp_signatures.c
    multiplayer/mp_signatures_enemy.c
    multiplayer/mp_signatures_foot.c
    multiplayer/mp_signatures_blade.c
    multiplayer/mp_signatures_puppet.c
    multiplayer/mp_cell_sites.c
    multiplayer/mp_menu_sites.c
    multiplayer/mp_lobby_sites.c
)

# The replicated phase loop over a local record: run, skip, a mode-owned function in a default's
# place, the pin that lets a short descriptor cover thirteen phases, the pin decided on the word
# AFTER the call when a phase swaps the descriptor, the mode-changed flag ending the walk and being
# cleared, the terminator ending it without clearing, a NULL plan entry withholding a default, and
# the install refusing with no game.
add_unit_test(mp_phases multiplayer
    multiplayer/mp_phases.c
    multiplayer/mp_bank.c
    multiplayer/mp_bank_keep.c
    multiplayer/mp_bank_spawn.c
    multiplayer/mp_cells.c
    multiplayer/mp_content.c
    multiplayer/mp_signatures.c
    multiplayer/mp_signatures_enemy.c
    multiplayer/mp_signatures_foot.c
    multiplayer/mp_signatures_blade.c
    multiplayer/mp_signatures_puppet.c
    multiplayer/mp_cell_sites.c
    multiplayer/mp_menu_sites.c
    multiplayer/mp_lobby_sites.c
)

# The snapshot ring by tick: store and fetch, and the wraparound check that keeps a slot reused a
# ring later from returning the old tick, plus the interpolation bracket that refuses a pair with a
# missing end. Newest stays right across the tick counter's own wrap.
add_unit_test(mp_snapshot_history multiplayer
    multiplayer/mp_snapshot_history.c
    multiplayer/mp_snapshot.c
    multiplayer/mp_wire.c
)

# The world snapshot's delta encoding: a full round trip, a delta that carries only what changed
# and pulls the rest from the baseline, a body that leaves, and the refusals that keep a lossy link
# from desyncing the two sides (a wrong baseline, a slot unchanged from a body the baseline lacks,
# a truncated packet).
add_unit_test(mp_snapshot multiplayer
    multiplayer/mp_snapshot.c
    multiplayer/mp_wire.c
)

# What a payload opens with: the client's newest tick and the bits of the ticks before it, derived
# from a history, and the host's enemy length. Round tripped both ways over random input, because a
# decoder that takes what the encoder refuses lets a stranger name a tick no history holds.
add_unit_test(mp_payload_prefix multiplayer
    multiplayer/mp_payload_prefix.c
    multiplayer/mp_snapshot_history.c
    multiplayer/mp_snapshot.c
    multiplayer/mp_wire.c
)

# The character roster as one number: the same answer twice, and zero only for no roster at all.
add_unit_test(mp_content multiplayer
    multiplayer/mp_content.c
)

# What two machines must hold the same to play together, pure: the statement's codec round trip and
# a fuzz over it, every case of the judgement, a refusal between two lists of required mods of
# different length, the refusal's detail against a stranger's bytes, and the release's mods.
add_unit_test(mp_mod_manifest_rule multiplayer
    multiplayer/mp_mod_manifest_rule.c
    multiplayer/mp_wire.c
)

# The list of DLLs outside this release behind a statement's mods, pure: the codec round trip and a
# fuzz with names up to 260 characters, the strict decoder against random bytes and every cut, the
# decoder from before the list as the reference, and the one rule and the judgement, case by case.
add_unit_test(mp_mod_foreign multiplayer
    multiplayer/mp_mod_manifest_rule.c
    multiplayer/mp_wire.c
)

# The census against the process's real module list. A DLL built below for nothing else is copied
# into a folder of the test's own, loaded and renamed while loaded, and the census still finds it;
# whether this machine may host with [multiplayer] AllowMods empty and naming it; where a file lies
# against the folder, written every way a player may write it. The second program takes the census
# from the folder, which does not see the renamed DLL. Both write the ini beside the program.
add_library(mp_mod_census_probe MODULE mp_mod_census_probe.c)
add_unit_test(mp_mod_census multiplayer
    multiplayer/mp_mod_census.c
    multiplayer/mp_mod_folder.c
    multiplayer/mp_mod_allow.c
    multiplayer/mp_mod_manifest_rule.c
    multiplayer/mp_wire.c
)
add_unit_test(mp_mod_census_folder multiplayer
    multiplayer/mp_mod_census.c
    multiplayer/mp_mod_folder.c
    multiplayer/mp_mod_manifest_rule.c
    multiplayer/mp_wire.c
)
foreach(census_test mp_mod_census mp_mod_census_folder)
    target_sources(${census_test}_test PRIVATE mp_mod_census_fixture.c)
    target_compile_definitions(${census_test}_test PRIVATE
        "MP_MOD_CENSUS_PROBE=\"$<TARGET_FILE:mp_mod_census_probe>\"")
    add_dependencies(${census_test}_test mp_mod_census_probe)
    set_tests_properties(${census_test} PROPERTIES RESOURCE_LOCK engine_fixes_ini)
endforeach()

# This side's statement, made once per process and handed to each transport: the real module
# against stand-ins for the shot table, the roster, the census, the sessions and the log. Two
# programs, because a process makes its statement once: the transports in one, the ini path that
# learns from its 32nd substep in the other.
add_unit_test(mp_bridge_statement multiplayer
    multiplayer/mp_bridge_statement.c
    multiplayer/mp_mod_manifest_rule.c
    multiplayer/mp_wire.c
)
target_sources(mp_bridge_statement_test PRIVATE mp_bridge_statement_stand_in.c)
add_unit_test(mp_bridge_statement_ini multiplayer
    multiplayer/mp_bridge_statement.c
    multiplayer/mp_mod_manifest_rule.c
    multiplayer/mp_wire.c
)
target_sources(mp_bridge_statement_ini_test PRIVATE mp_bridge_statement_stand_in.c)

# The join check against the real modules: two sessions over the loopback, the host holding the
# bridge's own judge, a client stating another build of the multiplayer, other game data or DLLs
# outside this release, and the record the lobby's screen reads afterwards. It writes the host's
# [multiplayer] AllowMods into the ini beside the program.
add_unit_test(mp_bridge_content multiplayer
    ${BRIDGE_FAMILY}
)
set_tests_properties(mp_bridge_content PROPERTIES RESOURCE_LOCK engine_fixes_ini)
target_link_libraries(mp_bridge_content_test PRIVATE ws2_32 mp_hacl mbedtls mbedx509 tfpsacrypto
                      bcrypt)

add_unit_test(mp_enemy_wire multiplayer
    multiplayer/mp_enemy_wire.c
    multiplayer/mp_fieldset.c
)

# The level's state as a note and the decisions a client makes about it: the codec against the
# largest note and the channel's own bound, random bytes that must never read past the end, the
# emitter placements' four values, the fog's numbers and snaps, the throttle and the order.
add_unit_test(mp_level_state_rule multiplayer
    multiplayer/mp_level_state_rule.c
    multiplayer/mp_director_rule.c
    multiplayer/mp_wire.c
)

# The director's fog followed from its commands: which take effect, a ramp running out and the
# level's own fog coming back, the colour and the fog bit kept through later edges where the old
# window of four lost them, and the commands that bring another engine to a state.
add_unit_test(mp_level_state_fog_rule multiplayer
    multiplayer/mp_level_state_fog_rule.c
    multiplayer/mp_level_state_rule.c
    multiplayer/mp_director_rule.c
    multiplayer/mp_wire.c
)

# The level's journal: the host's window of sixteen, the numbers across the wrap, what a client
# plays after a jump and on a first note, and the whole note round trip at the channel's own bound.
add_unit_test(mp_level_state_journal_rule multiplayer
    multiplayer/mp_level_state_journal_rule.c
    multiplayer/mp_level_state_fog_rule.c
    multiplayer/mp_level_state_rule.c
    multiplayer/mp_director_rule.c
    multiplayer/mp_wire.c
)

# The fog of a room: the table of fog scripts, the fingerprint and the binding over a copy of the
# gas room's script, and one viewer through the four transitions.
add_unit_test(mp_fog_viewers_rule multiplayer
    multiplayer/mp_fog_viewers_rule.c
    multiplayer/mp_level_state_fog_rule.c
    multiplayer/mp_level_state_rule.c
    multiplayer/mp_director_rule.c
    multiplayer/mp_wire.c
)

# The same on a host's engine, which the test plays: a world with the gas room's script laid out as
# the engine keeps it, the pool, a census in the order a substep takes it, the hero block and a
# director that records what it is handed. The viewers' walk and the director's fog hand run.
add_unit_test(mp_fog_viewers multiplayer
    multiplayer/mp_fog_viewers.c
    multiplayer/mp_fog_viewers_rule.c
    multiplayer/mp_level_state_fog.c
    multiplayer/mp_level_state_fog_rule.c
    multiplayer/mp_level_state_rule.c
    multiplayer/mp_director_rule.c
    multiplayer/mp_enemy_wire.c
    multiplayer/mp_fieldset.c
    multiplayer/mp_wire.c
)

# A droideka's shield as one byte, and the director commands that bring a replica to it.
add_unit_test(mp_enemy_shield_rule multiplayer
    multiplayer/mp_enemy_shield_rule.c
)

# The dead that stood on the host, measured: a fall only from above zero, every exit of a run, the
# 12 s limit at its boundary, a life that bursts, and a corpse another hand stood up.
add_unit_test(mp_enemy_dead_watch_rule multiplayer
    multiplayer/mp_enemy_dead_watch_rule.c
)

# The host's half of it over actors and bodies made up in memory, and its report read back after
# the enemy table's reset, in the order a level end runs them. The log is kept in memory.
add_unit_test(mp_enemy_dead_watch multiplayer
    ${ENEMY_FAMILY}
)

# How an enemy's body ends, carried in its record: the field packed and read back, the write
# decision in every case, the class a solid body gets back, the flag bits left to other writers,
# the start of a clip counted on the host, and the engine half on a replica made up in memory, the
# death that dissolves with the host's class already gone first.
add_unit_test(mp_enemy_body multiplayer
    ${ENEMY_FAMILY}
)

# The census's read of a puppet against the read it was before the run time reads stopped asking
# the system first: the old reading code word for word as the reference, over a body made up in two
# pages of which the second may not be read, one case per branch of the old code.
add_unit_test(mp_enemy_pose multiplayer
    ${ENEMY_FAMILY}
)

# The census's walk of the pool and its read of one actor against the walk and the read they were,
# over 22 actors made up in memory and a pool broken both ways the walk answers. The test plays the
# engine's cell table itself, so the family comes along without the module that resolves it.
set(ENEMY_FAMILY_WITHOUT_CELLS ${ENEMY_FAMILY})
list(REMOVE_ITEM ENEMY_FAMILY_WITHOUT_CELLS multiplayer/mp_cells.c)
add_unit_test(mp_enemy_census multiplayer
    ${ENEMY_FAMILY_WITHOUT_CELLS}
)

# A client's flush over three replicas made up in memory, the cell table played the same way: two
# records of one replica between two flushes written in two, both pairs of a parked replica closed
# in a substep without a write, never the player's own body and never a slot that is not the
# actor, never across a life, every way out emptying what was held, and the watchers reading the
# record written rather than the newest taken.
add_unit_test(mp_enemy_sync_flush multiplayer
    ${ENEMY_FAMILY_WITHOUT_CELLS}
)

# The two newest records of one replica, pure and driven the way the enemy table drives it: one
# kept behind the other only while the body lacks it and the life is the same, a third dropping
# the oldest, a new life keeping nothing, and a clear leaving the queue as a fresh one.
add_unit_test(mp_enemy_queue_rule multiplayer
    multiplayer/mp_enemy_queue_rule.c
    multiplayer/mp_enemy_wire.c
    multiplayer/mp_fieldset.c
)

# Where a body's clip stands against the sender's, pure: the distance around the loop against the
# puppet's old one as the reference, the window at both substep lengths, and the span of events a
# move passes, which has to be the dispatcher's.
add_unit_test(mp_clip_rule multiplayer
    multiplayer/mp_clip_rule.c
)

add_unit_test(mp_enemy_pack multiplayer
    multiplayer/mp_enemy_pack.c
)

# The hidden nodes and meshes in the engine's saved form, the entries a receiver writes, and the
# sabre's own two bits, the rule the masks replaced, as the reference.
add_unit_test(mp_enemy_nodes_rule multiplayer
    multiplayer/mp_enemy_nodes_rule.c
)

# The engine half of the masks on an actor, body, thing and model made up in memory.
add_unit_test(mp_enemy_nodes multiplayer
    ${ENEMY_FAMILY}
)

# A limb as the number its world event carries, the old number as the reference, and when a client
# throws the piece: never the root, never past the model, and not while the task table is nearly
# full.
add_unit_test(mp_enemy_limb_rule multiplayer
    multiplayer/mp_enemy_limb_rule.c
    multiplayer/mp_enemy_pack.c
)

add_unit_test(mp_scratch multiplayer
    multiplayer/mp_scratch.c
    multiplayer/mp_wire.c
)

# The module census: which walk goes which way, a walk inside a walk, and the faults it keeps.
add_unit_test(mp_module multiplayer
    multiplayer/mp_module.c
)

# The enemy block. It runs with no engine behind it, which is exactly the case a client is in for
# every enemy the host has and it does not.
add_unit_test(mp_enemy_sync multiplayer
    ${ENEMY_FAMILY}
)

# No partial description without a confirmed base: the whole records a view gets until an
# acknowledgement confirms it, the keys that confirmation opens again, the view given up when the
# acknowledgements stop, and the receiver that refuses a record with nothing to be read against.
add_unit_test(mp_enemy_sync_base multiplayer
    ${ENEMY_FAMILY}
)

# The interest rule alone: the engine's two radii asked of a peer's player, what may not wait,
# the priority accumulator, and the wake radius scaled the way view_distance_fix scales it,
# held against that module's own arithmetic.
add_unit_test(mp_enemy_interest multiplayer
    multiplayer/mp_enemy_interest_rule.c
    multiplayer/mp_range_gate_rule.c
    view_distance_fix/npc_range.c
)

# The same rule inside the block: the bitmap untouched, the far keys held back, the silent peer,
# the question for the floor, and the round robin with no world to read.
add_unit_test(mp_enemy_sync_interest multiplayer
    ${ENEMY_FAMILY}
)

# The floor under the enemy block raised to the records that may not wait, against the budget's
# split with a real channel and the reserve of four players; the fixed floor kept as the reference.
add_unit_test(mp_enemy_sync_floor multiplayer
    multiplayer/mp_budget_rule.c
    multiplayer/mp_channel.c
    multiplayer/mp_channel_state.c
    ${ENEMY_FAMILY}
)

# The copies' part of the enemy block, with the sending half handed its census by hand.
add_unit_test(mp_enemy_sync_copies multiplayer
    ${ENEMY_FAMILY}
)

# The removals with their reason, pickup claims, and the table of lives a removal was sent for.
# No engine: a client with no level open and no actor, and the messages have to be refused
# correctly rather than performed on nothing.
add_unit_test(mp_enemy_relay multiplayer
    multiplayer/mp_enemy_relay.c
    multiplayer/mp_pickup_relay.c
    multiplayer/mp_player_sound.c
    multiplayer/mp_player_sound_rule.c
    multiplayer/mp_signatures_player_sound.c
    multiplayer/mp_bank.c
    multiplayer/mp_bank_keep.c
    multiplayer/mp_bank_spawn.c
    multiplayer/mp_content.c
    multiplayer/mp_trust.c
    multiplayer/mp_death.c
    multiplayer/mp_taken.c
    multiplayer/mp_placements.c
    ${ENEMY_FAMILY}
    multiplayer/mp_events.c
)

# An NPC's blade clang: the cooldown's verdict, the world event it becomes, and one clang per event
# on a replica. The host half runs for real, so the enemy block family comes along; the test does
# by hand only what the census does, which is to say the sending half is describing.
add_unit_test(mp_enemy_block multiplayer
    ${ENEMY_FAMILY}
)

# The world events end to end: posted on a host, settled by the census, carried in every block to
# a peer until an acknowledgement proves it, read and taken once on a client, performed on its
# replica or waited for. Four peers with their own acknowledgements, a sound by earshot and an
# explosion by interest, the music a block says, and the single place rule the events replaced as
# the reference for one event in a window. The world, the census and the log kept in memory are
# the stand-in both world event tests share.
add_unit_test(mp_world_event multiplayer
    ${ENEMY_FAMILY}
)
target_sources(mp_world_event_test PRIVATE mp_world_event_stand_in.c)

# The same host at its limits: a full part in the real payload room, a full substep's rank, the
# events no census settles, the reset, and an actor the census passed over, read back out of the
# host's report in the stand-in's log.
add_unit_test(mp_world_event_limits multiplayer
    ${ENEMY_FAMILY}
)
target_sources(mp_world_event_limits_test PRIVATE mp_world_event_stand_in.c)

# A client's half through the enemy block's own apply: a block refused behind its part takes none
# of its events, mutated blocks the same, and the one question every script output of a client
# asks, against the two it replaced.
add_unit_test(mp_world_event_client multiplayer
    ${ENEMY_FAMILY}
)

# The events' codec and decisions alone: a round trip over random events, the numbers across the
# wrap, the memory against a burst, what a client does without a replica, who is concerned.
add_unit_test(mp_world_event_rule multiplayer
    multiplayer/mp_world_event_rule.c
    multiplayer/mp_range_gate_rule.c
)

# The scripts' sounds as decisions: music before loop before once, the edge and the hold against
# the old rule of one event per call, whom music is meant for, the one predicate for a client's
# script outputs, single player untouched, and one loop per replica.
add_unit_test(mp_script_sound_rule multiplayer
    multiplayer/mp_script_sound_rule.c
)

# The loops of the actors' scripts, from the host's note to a client's replica against an engine
# the test stands in for, and their part of the level's state written and read back.
add_unit_test(mp_actor_loop multiplayer
    ${ENEMY_FAMILY}
)

# The scripts' sounds on the engine, both halves, over an engine the test plays: the opcode's call
# written into this process's image and repointed by the install, the play routine, the funnel, a
# level of sound call records, a bank of channels and the director. The loops run for real; the
# world events, the census, the session and the targets are the test's.
add_unit_test(mp_script_sound multiplayer
    multiplayer/mp_script_sound.c
    multiplayer/mp_script_sound_client.c
    multiplayer/mp_script_sound_rule.c
    multiplayer/mp_actor_loop.c
    multiplayer/mp_director_rule.c
    multiplayer/mp_enemy_wire.c
    multiplayer/mp_fieldset.c
    multiplayer/mp_level_state_rule.c
    multiplayer/mp_wire.c
)

# The sound opcode's call and the two routines around it: the call ends its pattern with the
# operand masked, no pattern requires an address, every operand read out is masked, the detour
# heads are whole instructions inside their patterns, and a process with no game resolves nothing.
add_unit_test(mp_signatures_script_sound multiplayer
    multiplayer/mp_signatures_script_sound.c
)

# The director's twenty commands by class, against the two lists the class table replaced.
add_unit_test(mp_director_rule multiplayer
    multiplayer/mp_director_rule.c
    multiplayer/mp_level_state_rule.c
    multiplayer/mp_wire.c
)

# An enemy that bursts into its pieces: the speed in quarters, the ring a burst waits in for its
# removal, the gates named, the client's decision against the one it replaced, the twelve byte
# removal, and the pieces nobody's. The same family, because the decision the relay calls maps the
# binding's slot.
add_unit_test(mp_enemy_burst multiplayer
    multiplayer/mp_events.c
    multiplayer/mp_shot_sites.c
    ${ENEMY_FAMILY}
)

# The two decisions a scene gate makes: whether a put-back of the hero may go through, and which
# return addresses are a script asking for the camera. Both are pure, and the first is driven over
# every sequence of grabs and put-backs six long with the old behaviour beside it as the
# reference.
add_unit_test(mp_scene_rule multiplayer
    multiplayer/mp_scene_rule.c
)

# Which player a scene's script meant, the rule a host measures a scene's trigger by. It lives in
# the scene rule's file and is tested apart, because it is a subject of its own.
add_unit_test(mp_scene_trigger multiplayer
    multiplayer/mp_scene_rule.c
)

# The single questions a scene asks: whether a scene runs, whether a player may be moved, who
# asked for a respawn, where a hero's spawn came from and what a warp did to the quest bits.
add_unit_test(mp_scene_move multiplayer
    multiplayer/mp_scene_rule.c
)

# The watch on the hero a scene hosts: when a walk counts as stuck, which target is the real one
# across a detour, and when the hero is put onto it. Pure.
add_unit_test(mp_scene_hero_rule multiplayer
    multiplayer/mp_scene_hero_rule.c
)

# A scene of the host's: the host's scene and one player's seat, each walked over every sequence
# of its inputs up to six long.
add_unit_test(mp_scene_flow multiplayer
    multiplayer/mp_scene_flow.c
)

# What a scene of the host's waits for, and where each wait ends: every far player gone, a host
# dead or where it cannot be moved, a host with no place, and a seat with a bound of its own.
add_unit_test(mp_scene_flow_wait multiplayer
    multiplayer/mp_scene_flow.c
)

# The host at the place of a scene a far player set off: the hold that falls the moment he stands
# there, the rule of the place, and a gathering's seat that is tried again and taken the hard way
# rather than given up, with every short sequence of its inputs giving every fade back.
add_unit_test(mp_scene_hold multiplayer
    multiplayer/mp_scene_flow.c
    multiplayer/mp_scene_room.c
)

# A scene that runs on the host with no door heard: when the host takes it over and how its flow
# stands then; and the input a menu leaves over a fallen lock.
add_unit_test(mp_scene_doorless multiplayer
    multiplayer/mp_scene_doorless.c
    multiplayer/mp_scene_flow.c
    multiplayer/mp_scene_rule.c
)

# Whose a script's doors are on the host, pure: whose a run is over every input against the rule
# written plainly, the three marks of a taker bit by bit, the doors a far player's run was refused
# in their order, the latch and a far player's lock that is no scene of the host's, the placements
# woken for a far player, and a refused release owed to the host.
add_unit_test(mp_scene_claim_rule multiplayer
    multiplayer/mp_scene_claim_rule.c
    multiplayer/mp_scene_rule.c
)

# What of a scene's hold on a player is given back: the plan out of one look, over every value
# the look can carry, for a client that is never held and for a player who asks for himself.
add_unit_test(mp_scene_free_rule multiplayer
    multiplayer/mp_scene_free_rule.c
)

# Where the two releases of a scene live: the lock's held against the two script ends, and the
# clearing of the camera's override read out of them, against the retail image's own bytes.
add_unit_test(mp_scene_free_ends multiplayer
    multiplayer/mp_scene_free_rule.c
)

# The host's input while he is brought to the place of a scene: when it is held, and every way
# out of a scene letting it go, walked from a held input.
add_unit_test(mp_scene_input multiplayer
    multiplayer/mp_scene_flow.c
)

# The place of a scene: whether a body stands at it, whether the host stands there already, and
# the way a body at it faces.
add_unit_test(mp_scene_room multiplayer
    multiplayer/mp_scene_room.c
)

# Which calls the target resolver's hull leaves to the engine: the conversation menu's facing test,
# by its return address, and nobody else. Pure, and held against the hull before it.
add_unit_test(mp_target_rule multiplayer
    multiplayer/mp_target_rule.c
)

# Which placements a team deathmatch buries. The rule is pure and is driven here over the classes
# and the flag words the eleven levels actually author; the engine-side half is checked for
# refusing, because in a test process no cell and no site resolves.
add_unit_test(mp_arena multiplayer
    multiplayer/mp_arena.c
    multiplayer/mp_wire.c
    multiplayer/mp_placements.c
    multiplayer/mp_cells.c
    multiplayer/mp_cell_sites.c
    multiplayer/mp_menu_sites.c
    multiplayer/mp_lobby_sites.c
    multiplayer/mp_signatures.c
    multiplayer/mp_signatures_enemy.c
    multiplayer/mp_signatures_foot.c
    multiplayer/mp_signatures_blade.c
    multiplayer/mp_signatures_puppet.c
)

# The campaign bank for a player who enters the host's world mid game: the real wire on both ends
# one after the other, the engine's two banks played by the test. A zero the host has held since
# the level began reaches the newcomer only through the arrival's sweep, and a message from before
# the arrival is refused.
add_unit_test(mp_scratch_wire_arrival multiplayer
    multiplayer/mp_scratch_wire.c
    multiplayer/mp_scratch.c
    multiplayer/mp_wire.c
)

# The campaign on the reliable channel. It runs with no engine behind it, which is the case a
# machine is in while it sits in a menu with a session already open.
add_unit_test(mp_scratch_wire multiplayer
    multiplayer/mp_scratch_wire.c
    multiplayer/mp_scratch.c
    multiplayer/mp_scratch_bind.c
    multiplayer/mp_quest.c
    multiplayer/mp_cells.c
    multiplayer/mp_cell_sites.c
    multiplayer/mp_signatures.c
    multiplayer/mp_signatures_enemy.c
    multiplayer/mp_signatures_foot.c
    multiplayer/mp_signatures_blade.c
    multiplayer/mp_signatures_puppet.c
    multiplayer/mp_menu_sites.c
    multiplayer/mp_lobby_sites.c
    multiplayer/mp_events.c
    multiplayer/mp_wire.c
)

# Two sessions meeting over the loopback: the salt handshake completes across packet loss, both
# sides agree on the connection id, reliable messages cross both ways, and a client with no host
# gives up rather than trying forever. This is the join proven end to end without a socket.
add_unit_test(mp_session multiplayer
    multiplayer/mp_session.c
    multiplayer/mp_session_packets.c
    multiplayer/mp_entropy.c
    multiplayer/mp_session_access.c
    multiplayer/mp_roster.c
    multiplayer/mp_session_io.c
    multiplayer/mp_session_bulk.c
    multiplayer/mp_loopback.c
    multiplayer/mp_transport.c
    multiplayer/mp_channel.c
    multiplayer/mp_channel_state.c
    multiplayer/mp_inbox.c
    multiplayer/mp_hold.c
    multiplayer/mp_session_hold.c
    multiplayer/mp_session_meter.c
    multiplayer/mp_budget_rule.c
    multiplayer/mp_state_note_rule.c
    multiplayer/mp_wire.c
    # The bulk lane is driven end to end here, with the savegame codec as the block that rides it:
    # a lane proven with bytes nobody parses proves less than one carrying the real thing.
    multiplayer/mp_savefile.c
)

# The same session over a mailbox the test drives packet by packet: the cadence and every gate.
add_unit_test(mp_session_mailbox multiplayer
    multiplayer/mp_session.c
    multiplayer/mp_session_packets.c
    multiplayer/mp_entropy.c
    multiplayer/mp_session_access.c
    multiplayer/mp_roster.c
    multiplayer/mp_session_io.c
    multiplayer/mp_session_bulk.c
    multiplayer/mp_loopback.c
    multiplayer/mp_transport.c
    multiplayer/mp_channel.c
    multiplayer/mp_channel_state.c
    multiplayer/mp_inbox.c
    multiplayer/mp_hold.c
    multiplayer/mp_session_hold.c
    multiplayer/mp_session_meter.c
    multiplayer/mp_budget_rule.c
    multiplayer/mp_state_note_rule.c
    multiplayer/mp_wire.c
)

# One host and three clients over a network the test drives packet by packet: what a host does
# for one peer at the expense of another, which the two-ended harnesses above cannot show. The
# harness, mp_peers_net.c, is shared by the holds' tests and those of the state notes, the
# addressed hit and the overflow packet.
set(PEERS_SESSION
    multiplayer/mp_session.c
    multiplayer/mp_session_packets.c
    multiplayer/mp_entropy.c
    multiplayer/mp_session_access.c
    multiplayer/mp_roster.c
    multiplayer/mp_session_io.c
    multiplayer/mp_session_bulk.c
    multiplayer/mp_loopback.c
    multiplayer/mp_transport.c
    multiplayer/mp_channel.c
    multiplayer/mp_channel_state.c
    multiplayer/mp_inbox.c
    multiplayer/mp_hold.c
    multiplayer/mp_session_hold.c
    multiplayer/mp_session_meter.c
    multiplayer/mp_budget_rule.c
    multiplayer/mp_state_note_rule.c
    multiplayer/mp_wire.c
)
add_unit_test(mp_session_peers multiplayer ${PEERS_SESSION})
target_sources(mp_session_peers_test PRIVATE mp_peers_net.c)
add_unit_test(mp_session_peers_state multiplayer ${PEERS_SESSION})
# The crate note is built by its own encoder, as its sender builds it.
target_sources(mp_session_peers_state_test PRIVATE mp_peers_net.c
    ${MODS}/multiplayer/mp_crate_wire.c)

# The lag-compensation rewind: a stored tick is exact, between two it interpolates, a request older
# than the window is answered at the edge rather than refused, a future one at the newest, and a
# tick never stored refuses rather than inventing a past. The clamp is the point: a shot can never
# rewind arbitrarily far back.
add_unit_test(mp_rewind multiplayer multiplayer/mp_rewind.c)

# The prediction-correction smoothing: a correction is invisible the frame it lands, the offset
# decays toward zero and actually reaches it, a large error catches up faster than a small one, and
# a correction too large to hide snaps with no offset. Every check is a property of a sequence,
# because one step of an exponential proves nothing.
add_unit_test(mp_error_smooth multiplayer multiplayer/mp_error_smooth.c)

# The client-to-host input stream: a redundant packet round-trips, the sink applies each command
# once however many packets repeat it and never replays a stale one that arrives late, and the
# deserialiser refuses a packet that claims more than fits, a truncated one, and an axis out of
# range. The mirror of mp_snapshot, the other direction of the wire.
add_unit_test(mp_command multiplayer
    multiplayer/mp_command.c
    multiplayer/mp_wire.c
)

# The whole network layer end to end: a host builds a world every tick, delta encodes it against
# the baseline the client acknowledged, and sends it as the unreliable payload; the client decodes,
# stores, and acknowledges; the link drops a fifth. The property is the Quake-3 one, a lost snapshot
# healed by the next, so the client's world stays in sync without a retransmission. This is the run
# that proves the session, the snapshot codec, the history ring and the ack loop are one working
# thing before any of it touches the engine.
add_unit_test(mp_netsync multiplayer
    multiplayer/mp_session.c
    multiplayer/mp_session_packets.c
    multiplayer/mp_entropy.c
    multiplayer/mp_session_access.c
    multiplayer/mp_roster.c
    multiplayer/mp_session_io.c
    multiplayer/mp_session_bulk.c
    multiplayer/mp_loopback.c
    multiplayer/mp_transport.c
    multiplayer/mp_channel.c
    multiplayer/mp_channel_state.c
    multiplayer/mp_inbox.c
    multiplayer/mp_hold.c
    multiplayer/mp_session_hold.c
    multiplayer/mp_session_meter.c
    multiplayer/mp_budget_rule.c
    multiplayer/mp_state_note_rule.c
    multiplayer/mp_snapshot.c
    multiplayer/mp_snapshot_history.c
    multiplayer/mp_wire.c
)

# The in-memory network and the whole reliability chain over it. The emulator does what it says,
# and then two channels exchange fifty reliable messages across a link that drops, duplicates,
# reorders and delays, with every message arriving once and in order. This is the self test to
# pass before the first real socket: a session that connects and transfers nothing while the
# protocol reports success is what it exists to catch.
add_unit_test(mp_loopback multiplayer
    multiplayer/mp_loopback.c
    multiplayer/mp_transport.c
    multiplayer/mp_channel.c
    multiplayer/mp_channel_state.c
)

# The inbox alone: order across the end of its ring, no torn message, a stall counted once.
add_unit_test(mp_inbox multiplayer multiplayer/mp_inbox.c)
# The hold alone: order across the end of its ring, dead entries never handed out, its limits.
add_unit_test(mp_hold multiplayer multiplayer/mp_hold.c)
# A state note as the newest copy of its kind: replaced in place, shrunk in flight, left out
# unchanged, waiting behind a shrunk copy, and the question before a send agreeing with the send.
add_unit_test(mp_channel_state multiplayer multiplayer/mp_channel.c
    multiplayer/mp_channel_state.c)
# The shares of one packet, and a digest that finds its seat when the enemy block gives way.
add_unit_test(mp_budget_rule multiplayer multiplayer/mp_budget_rule.c multiplayer/mp_channel.c
    multiplayer/mp_channel_state.c)
# What the channel says is due is what its build then seats; the overflow packet and its rate.
add_unit_test(mp_channel_budget multiplayer multiplayer/mp_channel.c
    multiplayer/mp_channel_state.c multiplayer/mp_budget_rule.c)
# What a session sends a second and what a payload packet weighs, for the report's peer lines.
add_unit_test(mp_session_meter multiplayer multiplayer/mp_session_meter.c)
# Which nine notes are states, and the key without the clock; the crate note and the host's
# settings from their own encoders, so the clock left out is where those encoders put it.
add_unit_test(mp_state_note_rule multiplayer multiplayer/mp_state_note_rule.c
    multiplayer/mp_crate_wire.c multiplayer/mp_wire.c
    multiplayer/mp_host_settings_rule.c)
add_unit_test(mp_flash multiplayer multiplayer/mp_flash_rule.c)
# Every way a transport goes up arms the detonation flash and the way down puts the engine's arm
# back, seen in a shot table of the test's own: the real install, flash rule, record of the
# standing transport and menu settings, against stand-ins for the cells, the sessions, the
# socket, the relay and the halves the install binds. The reader of the developer menu's two
# buttons is armed and withdrawn beside them, counted by a stand-in.
add_unit_test(mp_bridge_install multiplayer
    multiplayer/mp_bridge_install.c
    multiplayer/mp_flash.c
    multiplayer/mp_flash_rule.c
    multiplayer/mp_armed.c
    multiplayer/mp_settings.c
    multiplayer/mp_rules.c
    multiplayer/mp_lobby.c
    multiplayer/mp_wire.c
)
# The two buttons of the developer menu as rules, the reader and the teleport: the mark that keeps
# a press of before a session from firing, every row of the teleport's refusals in its order, and
# the search that takes only a place beside the host.
add_unit_test(mp_player_help_rule multiplayer
    multiplayer/mp_player_help_rule.c
)
# The same rules, the repair: what a press asks on each role, what counts as nothing to do, which
# input hold lost its owner, when the host's scene latches and what the answer says, with the real
# plan of a scene's release under it and the real holders.
add_unit_test(mp_player_help_repair multiplayer
    multiplayer/mp_player_help_rule.c
    multiplayer/mp_scene_free_rule.c
    multiplayer/mp_armed.c
)
# The reader of the two buttons as it ships, against the two records in one process: the test
# files an ask as the overlay does and reads the answer as the panel does, and the repair and the
# teleport are stand-ins that answer what they are told to. A press of before a session never
# carried out, nobody told to listen before a frame has run, the newest press answered, and an
# open answer ended on the way out.
add_unit_test(mp_player_help multiplayer
    multiplayer/mp_player_help.c
    multiplayer/mp_player_help_rule.c
)
# The note of the host's world settings, both directions of its bounds against the table, and
# what the host says of its own ini, read the way each mod reads it.
add_unit_test(mp_host_settings_rule multiplayer multiplayer/mp_host_settings_rule.c)
# The cheat and 60fps cells a session holds, and every way they come back, a host's goodbye among
# them, through the real reading of the session against stand-ins for what it reads and three cells
# of this test's own.
add_unit_test(mp_world_holds multiplayer multiplayer/mp_world_holds.c
    multiplayer/mp_session_now.c multiplayer/mp_scene_rule.c)
# Whether this machine plays in a running session: one answer for the host's settings, the world
# holds and the hero carry, as a table over every kind of machine and every way a session looks.
add_unit_test(mp_session_now multiplayer multiplayer/mp_session_now.c multiplayer/mp_scene_rule.c)

# Which player an enemy is measured against. The engine asks about the local one; in a
# session the answer has to be the nearest of all of them.
add_unit_test(mp_range_gate_rule multiplayer multiplayer/mp_range_gate_rule.c)

# Which body a host's world is woken and kept by while its own player is dead.
add_unit_test(mp_world_anchor_rule multiplayer
    multiplayer/mp_world_anchor_rule.c
    multiplayer/mp_seat_rule.c
)

# What each delivery path does with the verdict on a contact between players.
add_unit_test(mp_contact_rule multiplayer multiplayer/mp_contact_rule.c)

# Where the host's substep goes: the buckets, the minutes and the rows of the report, through the
# report itself; the durations read a clock and are only counted.
add_unit_test(mp_stopwatch multiplayer multiplayer/mp_stopwatch.c)

# When the host's world leaves the host and how its records reach a client's replicas: counters
# only, each read through the line the report prints. The queue of two records comes along, so
# the case of six records over five substeps is told by the lines as the flush tells them.
add_unit_test(mp_cadence multiplayer
    multiplayer/mp_cadence.c
    multiplayer/mp_enemy_queue_rule.c
    multiplayer/mp_stopwatch.c
    multiplayer/mp_enemy_wire.c
    multiplayer/mp_fieldset.c
)
add_unit_test(mp_hit_attacker multiplayer multiplayer/mp_hit_attacker.c)

# The one way out of a session, as rules: when the transport comes down, when the title screen
# means a session has nothing left to be, and what a player is told there.
add_unit_test(mp_exit multiplayer multiplayer/mp_exit.c)

# The pause menu of a session against the engine's own pause as the reference: the cells the one
# exit leaves for every answer, every way out ending in the same state, the ids the readers hold,
# the reasons the menu closes itself for and when that is given up, and the session note said
# where the transport cell is set.
add_unit_test(mp_pause multiplayer multiplayer/mp_pause_rule.c multiplayer/mp_armed.c)

# A hit the host saw on a far player, addressed by the relay itself to that player's machine. The
# relay is the real one and so is its codec; the engine's contact globals, the handler it hulls and
# the modules around it are played by the test, as a host with three clients.
add_unit_test(mp_hit_relay_addressed multiplayer
    multiplayer/mp_hit_relay.c
    multiplayer/mp_hit_relay_death.c
    multiplayer/mp_hit_wire.c
    multiplayer/mp_hit_attacker.c
    multiplayer/mp_node_map.c
    multiplayer/mp_death.c
    multiplayer/mp_armed.c
    multiplayer/mp_wire.c
)

# The one write the session's pause menu makes: the key hook's call, repointed while a transport
# stands and put back when it comes down, each only after the call proves what it lands on. Played
# against call sites in the test's own image; the binding's other readers answer "not resolved".
add_unit_test(mp_pause_arm multiplayer
    multiplayer/mp_pause.c
    multiplayer/mp_pause_rule.c
    multiplayer/mp_armed.c
)

# The primitives of the relay protocol against their published vectors, the same self-test public
# mode runs, and the nonce, the associated data and the bounds the protocol builds on them.
add_unit_test(mp_relay_crypto multiplayer
    multiplayer/mp_relay_crypto.c
    multiplayer/mp_entropy.c
)
target_link_libraries(mp_relay_crypto_test PRIVATE mp_hacl bcrypt)

# The relay protocol's messages, handshake and legs, pure, against the relay's own vectors,
# generated into unittests/mp_relay_vectors.h.
set(RELAY_FAMILY
    multiplayer/mp_relay_wire.c
    multiplayer/mp_relay_inner.c
    multiplayer/mp_relay_noise.c
    multiplayer/mp_relay_crypto.c
    multiplayer/mp_entropy.c
)
add_unit_test(mp_relay_wire multiplayer ${RELAY_FAMILY})
target_link_libraries(mp_relay_wire_test PRIVATE mp_hacl bcrypt)
add_unit_test(mp_relay_noise multiplayer ${RELAY_FAMILY})
target_link_libraries(mp_relay_noise_test PRIVATE mp_hacl bcrypt)

# The relay's key document: the documents read, the ones refused and the damaged ones.
add_unit_test(mp_relay_keydoc multiplayer multiplayer/mp_relay_keydoc.c)

# The two certificates the key fetch trusts, ISRG Root YE and X2, against the SHA-256 of each.
add_unit_test(mp_relay_anchors multiplayer
    multiplayer/mp_relay_anchors.c
    multiplayer/mp_relay_crypto.c
    multiplayer/mp_entropy.c
)
target_link_libraries(mp_relay_anchors_test PRIVATE mp_hacl bcrypt)

# The rules of the key the document names: an hour in hand, seven days kept, one refetch a
# minute and three a start, the line kept on disk and the answer of the fetch.
add_unit_test(mp_relay_keysource multiplayer
    multiplayer/mp_relay_keysource.c
    multiplayer/mp_relay_keydoc.c
)

# The link to the relay against a relay of the tests' own, which is first proven against the
# relay's recorded handshakes: the attempts and every refusal, a host's standing leg, a player's.
foreach(link_test mp_relay_link mp_relay_link_keep mp_relay_link_member)
    add_unit_test(${link_test} multiplayer
        ${RELAY_FAMILY}
        multiplayer/mp_relay_link.c
        multiplayer/mp_relay_link_leg.c
    )
    target_sources(${link_test}_test PRIVATE mp_relay_fake.c)
    target_link_libraries(${link_test}_test PRIVATE mp_hacl bcrypt)
endforeach()

# The players a relay host seats, as the endpoints its session keys them by.
add_unit_test(mp_relay_seats multiplayer multiplayer/mp_relay_seats.c)

# The relay's two sockets on the loopback: only the relay is heard.
add_unit_test(mp_relay_socket multiplayer
    multiplayer/mp_relay_socket.c
    multiplayer/mp_socket.c
)
target_link_libraries(mp_relay_socket_test PRIVATE ws2_32)

# A host and a player through a relay of the tests' own on the loopback: the real transports,
# with the fetch and the clocks handed in.
add_unit_test(mp_relay_transport multiplayer
    ${RELAY_FAMILY}
    multiplayer/mp_relay_link.c
    multiplayer/mp_relay_link_leg.c
    multiplayer/mp_relay_seats.c
    multiplayer/mp_relay_socket.c
    multiplayer/mp_relay_transport.c
    multiplayer/mp_relay_keysource.c
    multiplayer/mp_relay_keydoc.c
    multiplayer/mp_relay_keyfetch.c
    multiplayer/mp_relay_anchors.c
    multiplayer/mp_socket.c
    multiplayer/mp_wallclock.c
)
target_sources(mp_relay_transport_test PRIVATE mp_relay_fake.c)
target_link_libraries(mp_relay_transport_test PRIVATE
    mp_hacl mbedtls mbedx509 tfpsacrypto ws2_32 bcrypt)

# The public list for the join screen: the rounds and pages on a clock of the test's own, then the
# sockets against a relay on the loopback that hands out one page.
add_unit_test(mp_relay_listing multiplayer
    ${RELAY_FAMILY}
    multiplayer/mp_relay_link.c
    multiplayer/mp_relay_link_leg.c
    multiplayer/mp_relay_listing.c
    multiplayer/mp_announce.c
    multiplayer/mp_wire.c
)
target_sources(mp_relay_listing_test PRIVATE mp_relay_fake.c)
target_link_libraries(mp_relay_listing_test PRIVATE mp_hacl bcrypt)
add_unit_test(mp_relay_list multiplayer
    ${RELAY_FAMILY}
    multiplayer/mp_relay_link.c
    multiplayer/mp_relay_link_leg.c
    multiplayer/mp_relay_listing.c
    multiplayer/mp_relay_list.c
    multiplayer/mp_relay_socket.c
    multiplayer/mp_relay_keyfetch.c
    multiplayer/mp_relay_keysource.c
    multiplayer/mp_relay_keydoc.c
    multiplayer/mp_relay_anchors.c
    multiplayer/mp_announce.c
    multiplayer/mp_wire.c
    multiplayer/mp_socket.c
)
target_sources(mp_relay_list_test PRIVATE mp_relay_fake.c)
target_link_libraries(mp_relay_list_test PRIVATE
    mp_hacl mbedtls mbedx509 tfpsacrypto ws2_32 bcrypt)

# A receiver that pumps between substeps and reads no note, over the loopback: no packet is
# refused for the order window and no payload is lost.
add_unit_test(mp_session_inbox multiplayer
    multiplayer/mp_session.c
    multiplayer/mp_session_packets.c
    multiplayer/mp_entropy.c
    multiplayer/mp_session_access.c
    multiplayer/mp_roster.c
    multiplayer/mp_session_io.c
    multiplayer/mp_session_bulk.c
    multiplayer/mp_loopback.c
    multiplayer/mp_transport.c
    multiplayer/mp_channel.c
    multiplayer/mp_channel_state.c
    multiplayer/mp_inbox.c
    multiplayer/mp_hold.c
    multiplayer/mp_session_hold.c
    multiplayer/mp_session_meter.c
    multiplayer/mp_budget_rule.c
    multiplayer/mp_state_note_rule.c
    multiplayer/mp_wire.c
)

# The roster codec and the name rule it shares with the handshake.
add_unit_test(mp_roster multiplayer multiplayer/mp_roster.c multiplayer/mp_wire.c)

# The reliability layer, driven with the test playing the network. What has to hold is the
# refusals: a message over the budget is refused rather than fragmented, and a packet whose
# messages overrun the ordering window is not acknowledged rather than acknowledged with a message
# silently dropped, which would be a permanent loss.
add_unit_test(mp_channel multiplayer multiplayer/mp_channel.c multiplayer/mp_channel_state.c)

# The sender's half of the window rule: a burst after a lost packet costs the next packet nothing,
# and what is held back still arrives, in order.
add_unit_test(mp_channel_window multiplayer multiplayer/mp_channel.c
    multiplayer/mp_channel_state.c)

# Every decoder fed what no honest peer sends: random bytes, valid packets after bit flips and
# truncations, and handshake packets forged by someone who knows the format but not the salts. The
# properties are the negative ones: no read past a buffer, no half applied snapshot, no stranger
# reaching connected, no small packet answered with a larger one, no forged response keeping a slot
# alive, no forged denial ending a join.
add_unit_test(mp_fuzz multiplayer
    multiplayer/mp_session.c
    multiplayer/mp_session_packets.c
    multiplayer/mp_entropy.c
    multiplayer/mp_session_access.c
    multiplayer/mp_roster.c
    multiplayer/mp_session_io.c
    multiplayer/mp_session_bulk.c
    multiplayer/mp_transport.c
    multiplayer/mp_channel.c
    multiplayer/mp_channel_state.c
    multiplayer/mp_inbox.c
    multiplayer/mp_hold.c
    multiplayer/mp_session_hold.c
    multiplayer/mp_session_meter.c
    multiplayer/mp_budget_rule.c
    multiplayer/mp_state_note_rule.c
    multiplayer/mp_snapshot.c
    multiplayer/mp_command.c
    multiplayer/mp_events.c
    multiplayer/mp_wire.c
)

# The network clock over its edges: tick and millisecond wraparound mid-blend, loss and reorder,
# estimate drift over tens of thousands of snapshots, underrun refusal instead of extrapolation,
# and the pace decision's hysteresis. No engine and no socket in the process, which is the point
# of layer 1.
add_unit_test(mp_clock multiplayer multiplayer/mp_clock.c)

# The render timeline: the warm-up, one tick per substep on a steady stream, a lost sample
# invisible, ten minutes of rate drift absorbed by slews alone, a sender stall halting the render
# and resuming with one resync, a receiver stall rebasing once, a reordered arrival ignored, and
# a streak inside the cooldown not counting. No engine, no clock.
add_unit_test(mp_timeline multiplayer multiplayer/mp_timeline.c)

# The far body sampled at the render moment: the exact sample at a whole tick, the blend at
# phase four of eight, a lost sample bracketed, twists blended with a node on one side only, the
# discrete state always the earlier sample's, and an underrun counted rather than invented. No
# engine, and no clock: the timeline hands it the render moment.
add_unit_test(mp_interp multiplayer
    multiplayer/mp_interp.c
    multiplayer/mp_timeline.c
    multiplayer/mp_snapshot.c
    multiplayer/mp_snapshot_history.c
    multiplayer/mp_wire.c
)

# Two real UDP sockets on localhost in one process: strict address parsing, a non-blocking recv
# that returns at once on an empty socket, a packet crossing and being answered by the endpoint the
# host learned from the datagram, and an oversized datagram dropped rather than torn. Skips cleanly
# on a machine with no Winsock, like the socket lifetime test below.
add_unit_test(mp_udp multiplayer
    multiplayer/mp_udp.c
    multiplayer/mp_socket.c
    multiplayer/mp_transport.c
)
target_link_libraries(mp_udp_test PRIVATE ws2_32)
target_link_libraries(mp_discovery_test PRIVATE ws2_32)

# The Winsock lifetime, driven for real. The point is not only the reference count: with nothing
# calling into the module the linker drops it and the import disappears, so a build that merely
# compiles the socket header proves nothing about whether the library links. Calling it from here
# is what makes that answer real, whatever the transports above it happen to call.
add_unit_test(mp_socket multiplayer multiplayer/mp_socket.c)
target_link_libraries(mp_socket_test PRIVATE ws2_32)

# The packet encoding, driven over its edges. What has to hold is not that a plausible position
# survives a round trip but that the buffer's last byte is usable and its next one is not, that a
# value which cannot be represented is refused rather than wrapped, and that a writer which has
# already failed does not start succeeding again halfway through a record. That last one is what the
# whole calling style rests on: every caller writes a run of fields and checks once at the end.
add_unit_test(mp_wire multiplayer multiplayer/mp_wire.c)

# The three push block messages, whole or refused whole, fuzzed there and back, in buffers of the
# size one reliable message really has.
add_unit_test(mp_crate_wire multiplayer
    multiplayer/mp_crate_wire.c
    multiplayer/mp_wire.c
)

# The chat's say and line, whole or refused whole: every length, every byte value in a text, every
# slot, and random bytes decoded against a guard page so a read past the message stops the test.
add_unit_test(mp_chat_wire multiplayer
    multiplayer/mp_chat_wire.c
    multiplayer/mp_chat_rule.c
    multiplayer/mp_roster.c
    multiplayer/mp_wire.c
)

# What a typed byte becomes on two code pages, the trim, the bucket over its burst, its refill and
# the wrap of the clock, and the host's rule with the buffer its callers give it.
add_unit_test(mp_chat_rule multiplayer
    multiplayer/mp_chat_rule.c
    multiplayer/mp_chat_wire.c
    multiplayer/mp_roster.c
    multiplayer/mp_wire.c
)

# The chat's key table row by row, the opening key's own character, a held key, and the choice of
# the key against the names the developer menu's row writes. The scoreboard's key grammar comes
# along, because the chat key is read with it.
add_unit_test(mp_chat_key_rule multiplayer
    multiplayer/mp_chat_key_rule.c
    multiplayer/mp_board.c
    multiplayer/mp_text.c
    multiplayer/mp_text_pick.c
    multiplayer/mp_text_second.c
    multiplayer/mp_roster.c
    multiplayer/mp_score.c
    multiplayer/mp_rules.c
    multiplayer/mp_lobby.c
    multiplayer/mp_wire.c
)

# The rows of the chat box: the wrap, the six from the bottom, the fade, and the input row that
# never grows wider than the box.
add_unit_test(mp_chat_layout multiplayer
    multiplayer/mp_chat_layout.c
)

# The chat's input line as it ships: every way out of an open chat walked and the same fields read
# after each, the hold a closing key keeps until its own release, the opening press against the
# handlers further in, the input split and the transport. The engine, the settings, the clock and
# the one call that says a line are played by the test; the key table, the typing rule, the holders
# and the session note are the real ones. The module arms once a process, so the arming without
# the movie cell is a second run of the same program.
add_unit_test(mp_chat_input multiplayer
    multiplayer/mp_chat_input.c
    multiplayer/mp_chat_key_rule.c
    multiplayer/mp_chat_rule.c
    multiplayer/mp_chat_wire.c
    multiplayer/mp_armed.c
    multiplayer/mp_board.c
    multiplayer/mp_text.c
    multiplayer/mp_text_pick.c
    multiplayer/mp_text_second.c
    multiplayer/mp_roster.c
    multiplayer/mp_score.c
    multiplayer/mp_rules.c
    multiplayer/mp_lobby.c
    multiplayer/mp_wire.c
)
target_sources(mp_chat_input_test PRIVATE mp_chat_input_stand_in.c)
add_test(NAME mp_chat_input_no_movie_cell COMMAND mp_chat_input_test no-movie-cell)

# What the chat box hands the renderer in a frame of six full rows, string by string and glyph by
# glyph, with and without the box behind them. The surface and the chat's lines are played here.
add_unit_test(mp_chat_draw multiplayer
    multiplayer/mp_chat_draw.c
    multiplayer/mp_chat_layout.c
)

# Every decision about a push block that needs no engine: who holds it, what an index off the wire
# names, the host's step, the client's table, the answers, the note's cadence.
add_unit_test(mp_crate_rule multiplayer
    multiplayer/mp_crate_rule.c
    multiplayer/mp_crate_wire.c
    multiplayer/mp_wire.c
)

# Both push block machines as they ship, each driven by the other side played by hand through the
# codec, over a small world of the test's own behind the same table of engine calls.
add_unit_test(mp_crate_play multiplayer
    multiplayer/mp_crate_host.c
    multiplayer/mp_crate_client.c
    multiplayer/mp_crate_rule.c
    multiplayer/mp_crate_wire.c
    multiplayer/mp_wire.c
)

# The one monotonic clock the session and the channel run on: zero at the first call, never
# seen to go backwards, and a real ten millisecond wait measured as roughly ten milliseconds,
# which the tick count's sixteen millisecond steps could not promise.
add_unit_test(mp_wallclock multiplayer multiplayer/mp_wallclock.c)
