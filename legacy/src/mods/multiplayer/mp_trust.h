/* mp_trust.h: what a host takes on a player's word, and what it checks.
 *
 * Layer 1, no engine. The listen host and the dedicated server ask it the same questions, and the
 * relays ask it which of the two games they are in.
 *
 * ==================================== The model ================================================
 *
 * Every machine is the authority for its own player. Where it stands, what it fires, what it
 * wears, what it picks up and how it dies are said by the machine it happens on, and a host acts
 * on that or passes it on. Nothing on a host could say otherwise: its far bodies are placed from
 * the players' own reports.
 *
 * In a co-op campaign that is the whole of it, with one exception. The players chose each other, so
 * the host performs a client's hit on an enemy with the impact code the client saw, grants a claim
 * on any pickup still standing, takes a client's quest bit into the story, and lets a client whose
 * content differs leave by itself.
 *
 * The exception is a hit with a wave's code, 0x22, a force wave or an energy ball. No host
 * performs that report, in any game, because it fires the same wave itself from the player's
 * puppet, and the engine throws an actor only for a contact whose sender is the wave. A report
 * performed with the puppet's body as the sender sets the push event and throws nothing; a
 * droid's script answers the event by setting its no-throw flag, and the real wave a substep later
 * is turned away. One source for each hit is the repair.
 *
 * Bolts and blades are left out of that rule on purpose, although the puppet fires and swings them
 * on the host as well. Their second contact mostly lands inside the invulnerability the engine
 * grants after a hit, and taking their reports away is a change of its own, not made here.
 *
 * In a deathmatch the players may be strangers, and a host checks what it can:
 *
 *   a hit        a client's report that it hit something the host owns is refused. The arena has
 *                emptied the level, so what is left to hit is players, and a player's damage is
 *                judged on the machine that player is on.
 *   a pickup     is granted only to a claimant whose body stands within reach of it here.
 *   a quest bit  is not a client's to claim: a deathmatch has no story.
 *   content      a client whose fingerprint differs is sent away rather than trusted to go.
 *
 * And in every game, a player speaks for itself only. It may say what a player says and never
 * what only a host says, and where a note names a slot, a death its victim and an appearance its
 * body, that slot is the sender's own. The host knows which slot a peer holds; the peer's word
 * about it settles nothing.
 *
 * Not checked, in either game: where a player says it stands, what it says it fired, and what its
 * own machine says it suffered. Those are the player's to say under this model, and checking them
 * would take a simulation on the host that this design does not have.
 */
#ifndef MULTIPLAYER_MP_TRUST_H
#define MULTIPLAYER_MP_TRUST_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

/* Whether this machine checks its clients: true in a deathmatch, false in a co-op campaign and
 * with no session. Set where the game is announced, beside the arena that empties the level for
 * the same game. */
void mp_trust_set_checking(bool checking);
bool mp_trust_checking(void);

/* The contact code a force wave and an energy ball post: the pair pass sends a sender's object
 * class as the code, and every shot of the three kinds that carry this type has it as its class. */
#define MP_TRUST_WAVE_CODE 0x22u

typedef enum mp_trust_hit_verdict {
    MP_TRUST_HIT_PERFORM = 0,   /* the host performs it */
    MP_TRUST_HIT_DEATHMATCH,    /* a deathmatch, on anything but a copy an editor spawned */
    MP_TRUST_HIT_HOST_WAVE      /* code 0x22: the host's own copy of the wave performs it */
} mp_trust_hit_verdict_t;

/* What the host does with a player's report that it hit `key` with contact code `code`. In a
 * deathmatch it performs one only on a copy an editor spawned, the one thing the arena leaves
 * standing that a player may need to hurt, because a hit's impact code is its damage and the
 * client chose it. In every game it performs none with a wave's code: the host fires that
 * player's wave itself, and the wave does what the report cannot. */
mp_trust_hit_verdict_t mp_trust_host_performs_hit(uint32_t key, uint8_t code);

/* Whether `note` is something a player may say to its host at all. What only a host says is not:
 * the roster, the setup, the score, the story, the campaign bank, the map's corrections, an actor
 * made or removed, a hit on a far player, a line of a conversation, what is gone, the slot byte,
 * and a line of chat as everybody reads it, which names who said it; a player says only its text.
 * A host takes none of them from a client, whatever the module behind the tag would make of it.
 * Judged by the first byte, which is every message's tag, so a tag added to the wire later is
 * refused here until it is named. */
bool mp_trust_player_may_say(const uint8_t *note, size_t bytes);

/* Whether a note from the peer that holds `sender_slot` names only that slot, where it names one:
 * a death its victim, an appearance the body that wears it. True for a note that names none. */
bool mp_trust_speaks_for_itself(const uint8_t *note, size_t bytes, uint8_t sender_slot);

/* A host's gate, both of the above: a note a player may say that names no slot but its sender's.
 * The listen host and the dedicated server each ask it of every note a client sends, before any
 * module reads the note or any other player is sent it. */
bool mp_trust_takes_from(const uint8_t *note, size_t bytes, uint8_t sender_slot);

/* Whether a dedicated server passes a player's note on to the other players: the moments of its
 * own body, a mover it moved, its appearance, its death and its map's digest. The rest of what a
 * player may say is addressed to a host, and the server reads of it only a death, a team and a
 * pickup on the way, and answers a line of chat with a line of its own for everybody, as a listen
 * host does. The content fingerprint above all stays: each client compares the one it
 * hears with its own and leaves on a difference, so passing one client's on could send every
 * other client away. */
bool mp_trust_passes_between_players(const uint8_t *note, size_t bytes);

#endif /* MULTIPLAYER_MP_TRUST_H */
