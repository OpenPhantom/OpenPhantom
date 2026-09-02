/* credits_skip.h: a skip key for the end credits, which the engine gives no way out of.
 *
 * ==============================================================================================
 * Why there is no way out today
 *
 * credits_screen at 0x004470F8 is a closed loop and it reads no input at all. It never calls
 * swmenu_takeNavCode, which is the engine's own "did the player press something" question, so no
 * key reaches it by design. The loop is:
 *
 *     while (exitCode < 0) {
 *         if (bAtEnd == 0) { ease, scroll one row, read the next line of each column }
 *         sys_frame();
 *         if (bAtEnd != 0) {
 *             hold += g_frameDelta;
 *             bapMusicSetSequence(2000, ...);        // the fade, re-issued every frame
 *             if (hold > 4.0f) exitCode = 0;
 *         }
 *     }
 *     swmenu_close(); closeText(left); closeText(right); ... return exitCode;
 *
 * exitCode changes in exactly one place, and only when the text has run out. On a finished game
 * that leaves the player with no way to leave the credits but to kill the process.
 *
 * ==============================================================================================
 * How this skips, and why it does NOT write exitCode
 *
 * The engine's own ending is the thing worth reaching, not avoiding: it blanks the rows, latches
 * bAtEnd, fades the music over four seconds, then closes the screen and both text files through
 * swmenu_close and closeText. Forcing exitCode from outside would jump into the middle of that and
 * skip nothing of the cleanup but all of the grace.
 *
 * So this tells the engine the text has run out instead. credits_readLine is a leaf at 0x004476EB
 * and its answer is what drives the ending: the loop reads `if (credits_readLine(right, ...) == 0)
 * bEofRight++`, and one scroll step later bEofRight sends it into the arm that sets bAtEnd. Answer
 * 0 and the game ends its own credits, by its own path, releasing its own resources.
 *
 * THE COST IS THAT IT IS NOT INSTANT. Two scroll steps to latch the end, then the authored four
 * second music fade. That is the game's own bow-out and it is the right shape for credits; a hard
 * cut would need the exitCode write this deliberately avoids.
 *
 * ==============================================================================================
 * The key
 *
 * Escape, read with GetAsyncKeyState from the per frame tick. That one test also covers a
 * controller: controller_input.c synthesises Escape through SendInput for its Start button, so a
 * pad skips the credits without this file knowing a pad exists.
 *
 * A key ALREADY HELD when the credits open does not skip them. The closing cutscene runs
 * immediately before and is itself skippable with the same key, so the player's finger is very
 * likely still down; the state is seeded on entry and only a fresh press counts.
 */
#ifndef CREDITS_SKIP_H
#define CREDITS_SKIP_H

#include <stdbool.h>

/* `enabled` false declines and says so, so a reader can tell "switched off" from "did not
 * resolve". Needs the per frame tick; without it the feature declines rather than half installing,
 * because a skip key that is only polled once a scroll step would need holding down. */
void credits_skip_install(bool enabled);

#endif /* CREDITS_SKIP_H */
