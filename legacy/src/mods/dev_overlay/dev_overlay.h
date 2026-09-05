/* dev_overlay.h: a panel over the running game, opened with the key below Escape.
 *
 * It holds the cheats today and it is named for what it is rather than for what is in it: the
 * diagnostics and the developer tools that come later are groups inside this panel, not a second
 * one, and renaming a shipped DLL would break every configuration file that mentions it.
 *
 * Nothing here draws into a window of its own. The panel is drawn with the engine's own two
 * dimensional primitives, inside the frame the game has already built, which is why it composites
 * properly, survives a full screen mode and cannot take the focus away from the game.
 *
 * The parts are separate on purpose, and each one can be read without the others. overlay_model
 * owns what is shown and is the only half that can be tested without the game. overlay_layout turns
 * a measured text height into bands. overlay_draw turns those into shapes and text, through the
 * entry points overlay_sites resolves. overlay_utilities describes the rows that configure this
 * patch rather than the game. overlay_input owns the detour that opens the panel, input_freeze
 * holds the player while it is up and sim_pause holds the world. The cheat files own their own
 * engine sites and know nothing about any of the rest.
 */
#ifndef DEV_OVERLAY_H
#define DEV_OVERLAY_H

void dev_overlay_install(void);

#endif /* DEV_OVERLAY_H */
