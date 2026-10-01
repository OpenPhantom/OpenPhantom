/* mp_config.h: the ini keys and the gates between them, read once.
 *
 * The seam multiplayer.c's own size note has been naming: what is here decides nothing and
 * installs nothing, it only turns a file on disk into the value the installer then acts on. The
 * installer and the watchpost stayed behind with the state they report.
 *
 * The gates are the reason this is worth having in one place. Several of these keys are not
 * independent: a network role needs the input split, a role and the in-process bridge cannot both
 * drive the wire, and a role turns the second body into a puppet of the far machine rather than
 * something this one ticks. Every one of those is settled HERE, before anything is installed, and
 * says so in the log, so a run that quietly does less than the ini asked for says which key it
 * dropped and why.
 */
#ifndef MULTIPLAYER_MP_CONFIG_H
#define MULTIPLAYER_MP_CONFIG_H

#include "multiplayer.h"

/* Reads every key and applies the gates between them. Called before anything is scanned, so a
 * machine with the feature off pays nothing and, more to the point, is touched by nothing. */
void mp_config_read(multiplayer_config_t *config);

/* A session over the wire makes every far body a PUPPET of the state its own machine sends, so
 * the pipeline tick that would simulate it here is turned off.
 *
 * It is called from both ways a session is armed, the ini and the menu, and that is the whole
 * point of it being a function. The test used to sit beside the ini's role field and nowhere
 * else, and the menu writes that field minutes later, at the moment it binds the socket. So for
 * as long as a session could only be armed from the ini the rule held, and from the day the lobby
 * shipped every session started out of it ticked its far bodies through the player pipeline with
 * an empty command: the pipeline committed the body's own physics over the position the wire had
 * just placed it at, and each player watched the other stand still while every counter in the run
 * report said the state had arrived and the body had been placed.
 *
 * Answers whether it changed anything, so a second call is quiet. */
bool mp_config_far_bodies_are_puppets(multiplayer_config_t *config);

/* Whether the enemies are one world or two, and the answer is the TRANSPORT, never the game.
 *
 * Both games want one world. A deathmatch wants it because the arena has emptied the map and the
 * two sides have to agree about what is left of it; a campaign wants it because a co-operative
 * level is the level its authors wrote, and two machines walking two private copies of it is not
 * co-operation. The host describes and the client applies in either one, and the removals are the
 * other half of the same mechanism rather than an option beside it: a client that only lets go of
 * what the presence bitmap dropped keeps a body the host has already freed, on a placement its own
 * activation scan can wake again.
 *
 * `pass_over_a_socket` is the loopback's exception and it is not a limitation to be lifted. One
 * process being both sides would run the describing half and the applying half over the SAME
 * actors: nothing would ever look newly alive, no respawn would be seen, and the applying half
 * would park the actors the describing half is reading.
 *
 * It is here, beside the puppet rule, because it is the same shape of thing and was got wrong in
 * the same way. Keying it on the game mode on 2026-09-07 switched enemy replication off for every
 * co-operative session, and nothing flagged it: the last good field run, on 2026-09-06, sent 92451
 * records and applied 6279 blocks, and every run after it read `sent 0 | applied 0` while every
 * check stayed green. */
void mp_config_enemies_are_one_world(bool pass_over_a_socket);

/* The provocations that spawn a second body with no session: the death provocation and the
 * synthetic spin. Single player itself spawns none. */
bool mp_config_body_provocation(const multiplayer_config_t *config);

/* Every switch that asks for this feature's substep work with no session: the body provocations,
 * the bank swap, and the measurement that stops the AI on this instance. Nothing else runs in
 * single player. */
bool mp_config_local_provocation(const multiplayer_config_t *config);

#endif /* MULTIPLAYER_MP_CONFIG_H */
