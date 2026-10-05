/* mp_mouse_latch.h: the mouse buttons the engine leaves down as a level begins.
 *
 * Layer 2. The engine keeps one cell for every control that says "down", set by a press and
 * cleared by a release, and the player's jump is read as a level: as long as a control bound to
 * jump reads down, the body takes off again a fifth of a second after every landing.
 *
 * As a level begins the engine flushes its two devices. The keyboard half releases every key.
 * The mouse half reads the device's state and is meant to bring the four mouse buttons in line
 * with it, and it has two faults in the shipped bytes: it does not look at whether the device
 * answered, and all four passes of its loop write the one cell of the second mouse button, which
 * the engine's defaults bind to jump. The device is opened for the foreground only. So a window
 * that is not in front as its level begins leaves that cell at whatever its stack held, and when
 * that is not nought the body hops without end and the button itself does nothing, because no
 * press or release of a device that was not acquired arrives. A menu opened and closed cures it:
 * both change the input mode, which clears every cell.
 *
 * In a session that window is a client's, waiting in its lobby behind another window while the
 * host starts a fresh level. Alone the game is in front when its level begins. So the session
 * releases the four mouse button cells itself as a level begins, after the engine's flush, which
 * is what the keyboard half does for its keys: a button that is really held then has to be
 * pressed again. The node of this feature that hears the module messages stands at the head of
 * the engine's list and a level's beginning is broadcast from the tail, so it is told last.
 */
#ifndef MULTIPLAYER_MP_MOUSE_LATCH_H
#define MULTIPLAYER_MP_MOUSE_LATCH_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

/* The engine's table of down cells: one 32 bit cell a control. The keys by their scan code from
 * nought, the joystick's buttons from 0x100, its hat from 0x120 and the four mouse buttons last. */
#define MP_MOUSE_LATCH_CONTROLS 0x128u
#define MP_MOUSE_LATCH_FIRST    0x124u
#define MP_MOUSE_LATCH_BUTTONS  4u

/* The second mouse button: jump by the engine's defaults, and the one cell its flush writes. */
#define MP_MOUSE_LATCH_JUMP     0x125u

/* Finds the table, out of the operand of the button reader's last load. False with a line when
 * it does not resolve; a level's beginning then releases nothing. Idempotent. */
bool mp_mouse_latch_install(void);

/* A level of a session has begun and the engine's own flush has run. */
void mp_mouse_latch_level_begins(void);

void mp_mouse_latch_report(void);

/* The four mouse button cells of `table` released. Answers which of them read down, one bit a
 * button from bit 0; only those are written. `read` says whether the cells read at all. */
uint32_t mp_mouse_latch_release(uintptr_t table, bool *read);

/* Which of the released buttons nobody was pressing: down in the table and up on the device,
 * `pressed` being one bit a button the same way. Those are the flush's own, or a release that
 * was lost. */
uint32_t mp_mouse_latch_not_pressed(uint32_t were_down, uint32_t pressed);

#endif /* MULTIPLAYER_MP_MOUSE_LATCH_H */
