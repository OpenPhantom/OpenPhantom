/* diag_dialogue_ops.h: which script opcode asked for a line.
 *
 * Dialog_SpeakSingle is reached from two opcodes, 0x504 "Statement" (a bark, said and left) and
 * 0x500 "Dialog Box" (the branching menu, visited every step while it is open), and the say line
 * in the log cannot tell them apart. Two observers on the opcode handlers name the actor and the
 * opcode in front of each say, once per distinct (actor, opcode, line) until it changes, so the
 * shape of a conversation that starts twice can be read off the log: which opcode started it each
 * time, and whether the menu was being visited in between.
 */
#ifndef DIAG_DIALOGUE_OPS_H
#define DIAG_DIALOGUE_OPS_H

/* Part of Dialogue. Returns the number of observers installed. */
int diag_dialogue_ops_install(int dialogue_level);

#endif /* DIAG_DIALOGUE_OPS_H */
