/* controller_input.h: the right stick looks around, Start pauses, and nothing else about a
 * controller is touched.
 *
 * A field investigation this session (three separate machines, three different ways of exposing an
 * Xbox-style pad to this game) found that Xidi, a third-party wrapper this project used to install
 * to make the pad answer the game's own WinMM joystick calls, produces rare but large single-frame
 * stalls (measured up to 419 ms) whenever it is actively being polled. Turning Xidi off removed the
 * stalls in every controlled comparison. The two things it was actually being used for on this
 * project were the right stick driving the camera and Start opening the pause menu; both are
 * reproduced here without Xidi, without WinMM, and without the game's own joystick reading at all.
 *
 * WHY THIS DOES NOT GO THROUGH THE GAME'S OWN JOYSTICK CODE. The game's controller surface is
 * exactly three WinMM calls (joyGetNumDevs, joyGetPosEx, joyGetDevCapsA), modelling one physical
 * stick with up to 32 buttons. There is no second-stick concept in it at all, which is why Xidi's
 * own working configuration for this game did not route the right stick or Start through that
 * surface either: it mapped StickRightX to a synthesized mouse axis and ButtonStart to a
 * synthesized Escape keypress. This DLL does the same two things, directly.
 *
 * WHY SENDINPUT RATHER THAN A DIRECT HOOK. enhanced_input.dll's own raw mouse reader
 * (raw_mouse.c) accepts a WM_INPUT relative mouse report on nothing more than its type field; it
 * has no way to tell a real device from an injected one, and neither does the RAWMOUSE structure
 * Windows hands it. SendInput-synthesized movement reaches it exactly like a real mouse would, with
 * no dependency between this DLL and enhanced_input.dll at all, which is what this project's own
 * "feature DLLs never depend on each other at run time" rule asks for. The pause key is confirmed
 * directly from this session's own decompile of gameplay_wndproc_hotkey_handler (0x0043F681): Escape
 * is the sole route into gameplay_open_pause_menu (0x0043FAB5), and the engine's own state gating
 * already prevents a double-toggle, so nothing here needs to track menu state itself.
 *
 * WHY A DEDICATED THREAD RATHER THAN common/frame_hook.h. Every other per-frame need in this tree
 * uses frame_hook, and the first build of this feature did too. Look worked immediately; skipping a
 * playing movie with Start never did. fmv_player's own movie playback runs a dedicated message-pump
 * loop that does not call sys_frame/render_frameEnd at all for the whole duration, so frame_hook's
 * site never fires during a movie and this DLL never got a chance to run. A real keyboard Escape
 * press still worked during a movie, because fmv_player's own skip check reads global OS keyboard
 * state, independent of which loop the game's thread happens to be parked in. A dedicated
 * background thread gives this DLL that same independence, at the cost of being the one feature in
 * this tree that is not driven by the game's own render loop; see controller_input.c's own header
 * for the full account.
 *
 * Produces: controller_input.dll
 */
#ifndef CONTROLLER_INPUT_H
#define CONTROLLER_INPUT_H

void controller_input_install(void);

#endif /* CONTROLLER_INPUT_H */
