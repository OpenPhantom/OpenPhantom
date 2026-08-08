#ifndef INPUT_MENU_H
#define INPUT_MENU_H

#include <stdbool.h>

/* Two check boxes on the game's own controls screen, wired to the [enhanced_input] Strafe and FreeLook
 * keys. Both settings are live: their machinery is installed whichever way the keys read, so a box
 * only flips a gate, and neither box patches a byte when it is ticked.
 *
 * Must be called BEFORE the controls screen is opened for the first time: swmenu_build mutates the
 * authored widget array in place, exactly once, and a copy taken afterwards would carry zeroed
 * parameters. Loader time satisfies that by construction. */
void input_menu_install(void);

/* True when at least one check box is on the screen AND its state is read back. Anything less than
 * both is not installed: a box nobody reads is worse than no box. */
bool input_menu_is_installed(void);

#endif /* INPUT_MENU_H */
