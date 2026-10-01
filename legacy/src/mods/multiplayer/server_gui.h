/* server_gui.h: the one entry point of the window.
 *
 * It is a header with a single declaration because the window is one file and nothing else has
 * any business inside it: everything the window needs from the server it takes through
 * server_run.h, and everything the server needs from the window is nothing.
 */
#ifndef MULTIPLAYER_SERVER_GUI_H
#define MULTIPLAYER_SERVER_GUI_H

/* Opens the window, loads the settings, opens the log and runs the message loop until the window
 * is closed. Stops the server and closes the log on the way out. Returns a process exit code. */
int server_gui_run(void);

#endif /* MULTIPLAYER_SERVER_GUI_H */
