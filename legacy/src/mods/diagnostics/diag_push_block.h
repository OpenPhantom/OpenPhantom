/* diag_push_block.h: why a push block does or does not move for this machine's player.
 *
 * The question it answers is asked in single player: the player walks up to a crate, holds USE and
 * pushes, and either the crate goes or the player stands. It measures at the heads of the four
 * functions of that path and of the body test inside the push, all as chained detours, and never
 * at a call's operand, because the multiplayer repoints the push's own call and a second module on
 * the same four bytes would take the first one's target for the engine's.
 */
#ifndef DIAG_PUSH_BLOCK_H
#define DIAG_PUSH_BLOCK_H

/* 0 does nothing; 1 installs the five observers and writes the line once a second while it
 * changes. Answers how many observers stand. */
int diag_push_block_install(int push_block_level);

#endif /* DIAG_PUSH_BLOCK_H */
