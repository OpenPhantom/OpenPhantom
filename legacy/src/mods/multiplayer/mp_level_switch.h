/* mp_level_switch.h: the three script arms that switch something the level owns, hulled.
 *
 * Layer 2. A client parks every replica the host lists, `enemy_tickAll` skips a parked actor
 * before anything else, so no script of a parked actor runs, and nothing it switches on the level
 * happens there. Three script commands are involved:
 *
 *   emitter_set  0x0042E346  opcode 0x212, the steam of the gas room among 63 in the eleven levels
 *   light_set    0x0042E2F9  opcodes 0x207, 0x208 and 0x210, 49 of them
 *   sound_set    0x0042E37B  opcode 0x213, exactly one
 *
 * The host's level says what they switched (mp_level_state), and a client of a started session
 * refuses its own scripts the same three arms, so there is one writer. An actor the host does not
 * steer runs its script on a client: a replica let go, or one the client's own activation scan
 * woke where that scan still runs, since mp_world_anchor holds it in a started session. Without
 * the refusal the note and that script would heal against each other and the steam would flicker.
 *
 * Every entry is counted, apart by where it came from (mp_level_state_count), and the refusal is
 * the level state's one answer: a side that cannot match the host refuses nothing.
 */
#ifndef MULTIPLAYER_MP_LEVEL_SWITCH_H
#define MULTIPLAYER_MP_LEVEL_SWITCH_H

#include <stdbool.h>

/* Resolves the three arms and hulls them, then binds the level's state. False when any of the three
 * does not resolve, and then none is installed and nothing is refused. */
bool mp_level_switch_install(void);

/* Which side this is, for the name the report gives it. It decides nothing. */
void mp_level_switch_set_host(bool host);

void mp_level_switch_report(void);

#endif /* MULTIPLAYER_MP_LEVEL_SWITCH_H */
