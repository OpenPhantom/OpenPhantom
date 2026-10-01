/* mp_events.h: the things a player does that are moments, not states, on the wire.
 *
 * A body's state (where it is, what it plays, what it holds) is sent every tick and the newest
 * copy wins; a shot, a force push, a sabre action or the start of a weapon change is an event,
 * and an event sent as state is lost whenever the mirror does not change between two of them. The
 * field showed exactly that: one wave for two pushes in a row, because the engine writes the
 * overlay mirror only when a clip is played, and no shots at all on the far side, because a shot
 * is a spawned object and not a property of the body.
 * Events therefore travel as reliable messages, each one exactly once, in order, and the far side
 * performs each one through the engine's own spawner or starter. Pure: the codec and the queue
 * know nothing about the engine.
 *
 * The weapon change is the one that is both. Its slot travels as state as well, but the state is
 * the COMMITTED slot at +0x84 of the body record, which the equip commit writes only when the
 * weapon change sees the fire frame marker of the draw clip, several frames into it;
 * a puppet that waited for it would begin the same clip that many frames later than the sender
 * did, and would still be drawing when the sender is done. The event carries the moment the
 * change began, and the state behind it stays as the reconciliation for an event that was lost.
 *
 * Every event carries the sender's tick, the substep it was caught in, so the far side can perform
 * it at the moment its replay of the sender reaches that tick rather than the moment it arrived,
 * and a shot carries its muzzle relative to the sender's body rather than in the world, so the
 * bolt leaves the far body's weapon wherever that body is being drawn. Both came out of one
 * playtest finding: bolts left the air in front of the puppet, because an event was performed on
 * arrival while the puppet was drawn three intervals in the past, so the bolt spawned where the
 * far player had been a hundred milliseconds ago in the sender's world and where the puppet
 * would be a hundred milliseconds later in this one. Due-ness is decided on signed differences,
 * so a wrapped counter and a reordered event both read correctly.
 *
 * A mover is the one event that is not about a body. A door, a lift or a platform is opened by
 * the player who stepped on the plate or pressed the button, and the far machine has no way to
 * learn that from any body record, so the moment travels here and the far side runs it through
 * the engine's own opener. The mover module owns the catching and the performing; what belongs
 * here is the shape it travels in.
 *
 * With more than two players a moment also says WHOSE it is. A client sends its own to the host,
 * and the host performs it and passes it on to every other client with the sender's world slot
 * written in and the tick rewritten to the host's own: the other clients see the sender's body on
 * the host's worlds, which carry the host's clock, so the sender's own count would make the moment
 * due at a time that has nothing to do with that body. A mover names nobody, because a door is not
 * a body, and is restamped all the same.
 *
 * Reliable messages share their channel with one other the session already carries, a one byte
 * world slot note, and used to share it with a four byte acknowledgement. An event is told apart
 * by its length and its tag byte together: no event has the length of either of those two, and
 * where two events share a length, the push and the weapon change, the sabre action and the
 * mover, the spawn and the pickup claim, the tag separates them.
 */
#ifndef MULTIPLAYER_MP_EVENTS_H
#define MULTIPLAYER_MP_EVENTS_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#define MP_EVENT_SHOT   0x81u   /* a projectile left the player's weapon */
#define MP_EVENT_PUSH   0x82u   /* a force push started, with the meter it spent */
#define MP_EVENT_SABRE  0x83u   /* a sabre action began or ended */
#define MP_EVENT_WEAPON 0x84u   /* a weapon change began, with the slot it is heading for */
#define MP_EVENT_MOVER  0x85u   /* a mover was opened or closed, with the mover it names */
#define MP_EVENT_SPAWN  0x87u   /* an actor was created, with the placement it was made from */
#define MP_EVENT_DESPAWN 0x88u  /* an actor left, with the REASON, which is the whole message,
                                 *  and the speed of a burst into pieces just before it */
#define MP_EVENT_SKIN   0x89u   /* a player changed which actor it wears, by NAME */
#define MP_EVENT_PICKUP 0x8Au   /* a client CLAIMS a pickup; the host decides and despawns it */
/* 0x8B and 0x8C belong to the world scratchpad, which shares this channel. */
#define MP_EVENT_USE    0x8Du   /* RETIRED: a client's use press, while a client could talk to
                                 *  people; the number stays claimed */
#define MP_EVENT_PLAYER_HIT 0x90u /* the host's world hurt a FAR player, and that player's
                                  *  own machine performs it on its own body */
#define MP_EVENT_HIT    0x8Eu   /* a client hit something the host owns; the host performs it */
#define MP_EVENT_PLAYER_SOUND 0xA8u   /* a sound a player's own body made: its death cry, a
                                       *  pickup, a key, the water, the burning ground */
/* 0x8F is the roster, mp_roster.h: not an event, and told apart by its own length there.
 *
 * The tags that are not events but are spent. Everything from here to 0x94 is taken by a
 * message that lives in the module it belongs to rather than in this codec, and each is
 * written down here because this is the file somebody reads when they need a free one:
 *
 *   0x91 the lobby line, 0x92 what the host has chosen, 0x93 the content fingerprint
 *        (mp_lobby.h), and
 *   0x94 a death, with who died and who did it (mp_death.h). It is not an event because
 *        the decision it carries is the relay's: the victim is the only side that can see
 *        its own death, and it is the side that reports it, and
 *   0x95 the round's table of points, repeated by the host (mp_score.h). It is a note of its
 *        own rather than a field of the host's setup because the setup decides whether a level
 *        LOADS and its decoder refuses the whole message on one byte out of range; a torn
 *        score line must not be able to cost the level choice.
 *
 * The next free tag is not written here any more, and that is the point. This line used to
 * name one, and it was wrong three times running: it said 0x96 after 0x96 went to the world
 * state, after 0x97 went to the taken list, and after 0x98 and 0x99 went to the savegame
 * transfer. Two messages on one tag are recognised by each other's readers, and the second one
 * then decodes somebody else's fields behind a tag test and a length test that both passed.
 * The next free tag is the one after MP_LOBBY_NOTE_BAND_LAST, and the lobby rule's unit test names
 * every tag of the band once, so a number with no row, or with two, fails there. Use that, not a
 * comment. */

/* The engine's own next and previous walk runs over slots 0 to 12, wrapping at 12 and stopping at
 * 0, and 0 is unarmed; that is the range the codec takes. A slot outside it is a torn message,
 * refused rather than handed to the setter, which indexes an ammo table with it. It is not the
 * weapon table: that has twelve rows, 0 to 11, and the equip commit indexes it with no bound, so
 * the puppet's starter guard refuses slot 12 as well. */
#define MP_EVENT_WEAPON_SLOTS 13u

/* The sabre actions and what their operand means: a swing names a row of the engine's swing table,
 * a block or a parry the overlay clip the engine chose, a disarm carries nothing. */
#define MP_SABRE_SWING  0u
#define MP_SABRE_BLOCK  1u
#define MP_SABRE_PARRY  2u
#define MP_SABRE_DISARM 3u
#define MP_SABRE_ACTION_MAX MP_SABRE_DISARM

/* The two things a mover event can say. The engine has one function for each and they are not
 * opposites: its close forces the open-hold latch rather than driving the mover shut. */
#define MP_EVENT_MOVER_CLOSE 0u
#define MP_EVENT_MOVER_OPEN  1u

/* Why an actor left, straight out of `enemy_delete`. The five are the ones the engine's own six
 * call sites pass, and the reason is not decoration: the whole world bookkeeping hangs off it.
 * A removal for reason 0 puts the placement back to respawnable, one for reason 1 marks it
 * finally dead, and a reveal list fires on the same message. A despawn without its reason would
 * leave the two machines with different campaigns rather than with different scenery.
 *
 * Reason 0xE deletes NOTHING: the actor stays in the list as a corpse, and how long it lies is
 * decided by the detail level, which is a per-machine setting. It travels so that the far side
 * knows a corpse is meant, not so that it removes anything. */
#define MP_EVENT_REMOVE_OUT_OF_RANGE 0u
#define MP_EVENT_REMOVE_DELETED      1u
#define MP_EVENT_REMOVE_HOST_RELEASE 3u
#define MP_EVENT_REMOVE_LEVEL_END    4u
#define MP_EVENT_REMOVE_LEAVE_CORPSE 0x0Eu

/* The two appearance changes, which are different things and were mistaken for each other more
 * than once while they were being reverse engineered.
 *
 * A CHARACTER change means the player embodies another actor and uses ITS clips, weapons and rig;
 * it rides hero slot 3. A MODEL change means the player keeps its own hero and only wears foreign
 * geometry, so the clips, the controls and the weapons stay its own.
 *
 * The difference matters on the wire because a clip ordinal is per model: after a character change
 * the far side's clip numbers index a different table, and after a model change they do not. */
#define MP_SKIN_CHARACTER 0u
#define MP_SKIN_MODEL     1u
#define MP_SKIN_KIND_MAX  MP_SKIN_MODEL


/* What a pickup can be. A pickup is not a special object in this engine, it is an ENMY placement
 * whose shooter class falls in this band; the player's contact handler dispatches on it.
 *
 * Two values INSIDE the band, 0x0b and 0x0c, are refused on purpose. A census of all 2250
 * placements across the eleven shipped levels finds 177 pickups and NONE in those two, and the
 * retail handler's arm for those two reaches a stack defect. The shipped data can never get there;
 * a message from the wire could, and a receiver that passed one on would turn a defect nobody can
 * reach into one anybody can send. */
#define MP_PICKUP_KIND_MIN         0x0Au
#define MP_PICKUP_KIND_MAX         0x1Bu
#define MP_PICKUP_KIND_UNPLACED_LO 0x0Bu
#define MP_PICKUP_KIND_UNPLACED_HI 0x0Cu

/* How long an actor asset name can be. The engine's own field is 32 bytes and carries the file
 * name in all 265 shipped actors, so this is the field's size and not a limit invented here. The
 * name is the engine's own identity: pr+0x00 is the hero actor the resource loader answers for
 * the hero's name table entry, its header carries that name at +0x08, and the engine compares
 * the same field when it matches a halo. No index would do: all 164 profile characters ride hero
 * slot 3, and the roster order differs between two panels of one build, one ordering the first
 * five as obiwan, quigon, panaka, queen, mace and the other as obiwan, quigon, mace, panaka,
 * queen, behind both the line order of a hand editable ini.
 *
 * The shape a name may have is not this codec's choice either. The engine gates every resource
 * request behind an 8.3 test before it touches a file: at most one dot, at most eight characters
 * before it, at most three after it, and every other character out of a fixed 63 character set of
 * the letters, the digits and the underscore. A name outside that can never name anything, so it
 * is refused here rather than carried to a layer that would only be refused later. */
#define MP_EVENT_ASSET_MAX 32u

/* The characters the engine's resource gate accepts, in its own order. A hyphen is NOT among them,
 * and that is the trap: hyphenated names do exist in this game's data, but they are clip names
 * living inside a .baf, and a clip name never passes through the resource gate. */
#define MP_EVENT_ASSET_ALPHABET \
    "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789_"

/* What the same gate allows on either side of the single dot. */
#define MP_EVENT_ASSET_STEM_MAX      8u
#define MP_EVENT_ASSET_EXTENSION_MAX 3u

/* What a player's sound was, which says which of the engine's own tables its index is read from
 * on the far side: the death cry and the burning cry have a table each by hero, the other four are
 * the level's named sounds. A death that burns sends both. For the two cries the flags byte is the
 * death's cause, so the far side can leave a scripted death silent as the engine does; for the
 * others any value of it decodes, so a later build can use it without a new version.
 *
 * The last two are not sounds but the one thing the same body wears for a while: the shield a
 * shield pickup puts round it, whose rise carries its length in whole seconds in the sound byte,
 * and whose end carries nothing. */
#define MP_PLAYER_SOUND_DEATH      0u
#define MP_PLAYER_SOUND_BURN       1u
#define MP_PLAYER_SOUND_PICKUP     2u
#define MP_PLAYER_SOUND_KEY        3u
#define MP_PLAYER_SOUND_WATER      4u
#define MP_PLAYER_SOUND_GROUND     5u
#define MP_PLAYER_SOUND_SHIELD_ON  6u
#define MP_PLAYER_SOUND_SHIELD_OFF 7u
#define MP_PLAYER_SOUND_KIND_MAX   MP_PLAYER_SOUND_SHIELD_OFF

/* How many hero slots the engine has. The name table a spawn reads from is four entries, so a
 * slot outside that range indexes past it. Every other field of this codec that indexes something
 * is bounded, and this one was not: the weapon slot, the mover mode and the removal reason are all
 * refused when they name something the engine has no entry for, and a hero slot has to be too. */
#define MP_EVENT_HERO_SLOTS 4u

/* Encoded sizes: the tag, the tick, for each of the five moments of a body (the four here and a
 * player's sound, below) the world slot of the player who did it, then the fields. A shot is a
 * kind, a body relative offset and two angles; a push is a charge; a sabre action is an action
 * and an operand; a weapon change is a slot, which gives it the length of a push and leaves the
 * tag to tell them apart; a mover is a two byte id and a mode byte and names no player, which
 * gives it the length of a sabre action and the same answer.
 *
 * The id is two bytes rather than one. The engine's own trigger front ends narrow it to a byte,
 * because a walk trigger stores four of them in a polygon and a button packs the same four into a
 * dword with 0xff as the empty marker, but that is the front end's narrowing and not the mover's:
 * the opener takes an `int` and bounds it against the world's mover count, which is an `int` too,
 * and the id this event carries is read out of the mover's own record rather than out of a
 * trigger. Two bytes cost nothing at the rate a player opens doors and leave the wire honest
 * about what the engine can address. */
#define MP_EVENT_SHOT_BYTES   23u
#define MP_EVENT_PUSH_BYTES   7u
#define MP_EVENT_SABRE_BYTES  8u
#define MP_EVENT_WEAPON_BYTES 7u
#define MP_EVENT_MOVER_BYTES  8u

/* The two actor messages. They exist because the enemy record describes an actor the far side
 * otherwise has no way to create: the activation scan that would create it sits UNDER the AI
 * suspend gate (the call at 00432C42, the gate at 00432C1D) and is one of only three callers of
 * the spawner at 00437250, so a client that has handed the level to the host never spawns
 * anything on its own. Both carry a LEVEL IDENTITY, and that is the whole reason their lengths
 * are what they are.
 *
 * A placement index means something only inside one level, and a session outlives a level load by
 * design: it survives thirty seconds of it so that a peer does not have to rejoin. An arriving
 * spawn from the level that just ended would otherwise name a placement in the level that just
 * began, and create the wrong actor rather than none. The map's digest learned this the expensive
 * way and carries the same identity for the same reason.
 *
 * The script override is two bytes because it is an index into the level's script table and the
 * spawner takes it as an int; narrowing it here would be this file inventing a limit the engine
 * does not have. */
#define MP_EVENT_SPAWN_BYTES   11u   /* tag, tick, level, index, generation, script */
#define MP_EVENT_DESPAWN_BYTES 12u   /* tag, tick, level, key (two bytes), generation, reason,
                                      * burst */

/* The appearance change. Fixed length with the name padded out, rather than a variable length
 * message, because every recogniser on this channel tells a message apart by its length together
 * with its tag; a variable one would turn that test into a range and weaken it for every other
 * message that shares the channel. Forty two bytes for something a player does a handful of
 * times in an evening is not a cost worth a weaker recogniser. Forty two is on neither ladder of
 * seven per mover, the map's state note at 12, 19, 26 and the map digest at 8, 15, 22, and the
 * campaign bank, whose length is genuinely free, is excluded by its tag alone; the unit test walks
 * every length on the channel so a later change fails there and not in the field.
 *
 * It names the WORLD SLOT it belongs to, the way the reported player hit does. Without that byte
 * an arriving change says only that somebody looks different, and a receiver has to guess which
 * body it means. That guess is right while a session holds two players and wrong as soon as it
 * holds three. */
#define MP_EVENT_SKIN_BYTES    42u   /* tag, tick, kind, hero, slot, scale, 32 name bytes */

/* The pickup claim. It names the world slot of the claimant behind the tick, the way a body's
 * moment names its player: the claim says whose it is, and the host's grant, the same message
 * back, says whom it is for, so that only that player applies it. That gives it the length of a
 * spawn, which the tag tells apart; the recogniser tests both together. */
#define MP_EVENT_PICKUP_BYTES  11u   /* tag, tick, slot, level, index, generation, kind */

/* A player's sound is one of the moments of a body, so it names the world slot of the player
 * whose body made it behind the tick, like the shot, and a host passes a client's on with that
 * slot and its own tick written in. Then what it was, the index into that table, and the flags. */
#define MP_EVENT_PLAYER_SOUND_BYTES 9u   /* tag, tick, slot, what, sound, flags */
#define MP_EVENT_MAX_BYTES    MP_EVENT_SKIN_BYTES

/* The two lengths the recogniser was written against that are not events, so a reader can name
 * them. The slot note is still on the channel; the four byte acknowledgement has moved to the
 * front of the unreliable payload and no longer arrives here, and the number stays so that no
 * event is ever given that length. */
#define MP_EVENT_FOREIGN_SLOT_NOTE_BYTES 1u
#define MP_EVENT_FOREIGN_ACK_BYTES       4u

typedef struct mp_event {
    uint8_t  kind;        /* MP_EVENT_SHOT, MP_EVENT_PUSH, MP_EVENT_SABRE or MP_EVENT_WEAPON */
    uint32_t tick;        /* the sender's substep the moment was caught in, or the substep of the
                           * host that passed it on */
    uint8_t  source_slot; /* shots, pushes, sabre actions and weapon changes: the world slot of
                           * the player who did it, which a host that passes it on writes in;
                           * a pickup claim and its grant: the claimant's */
    uint8_t  shot_kind;   /* the engine's projectile kind, shots only */
    float    origin[3];   /* shots only: the muzzle as an offset in the sender's BODY frame, the
                           * world muzzle less the body's position, turned into the body's axes */
    float    pitch;       /* degrees, shots only */
    float    yaw;         /* degrees, shots only, RELATIVE to the sender's body yaw */
    float    charge;      /* 0..1, the meter a push spent, pushes only */
    uint8_t  action;      /* sabre only, one of MP_SABRE_* */
    uint8_t  operand;     /* sabre only, the row or clip the action names */
    uint8_t  weapon_slot; /* weapon changes only, the slot the change is heading for */
    uint16_t mover_id;    /* movers only, the mover's own index in the world's mover table */
    uint8_t  mover_mode;  /* movers only, MP_EVENT_MOVER_OPEN or MP_EVENT_MOVER_CLOSE */

    /* Actor messages. The level identity is the mover count of the world the sender is in, which
     * is what the map's digest already uses and is different in all eleven shipped levels. */
    uint16_t level_id;
    uint16_t actor_index;      /* the enemy's key (mp_wire.h), stable for one actor's life: a
                                * placement index, or from 256 a copy an editor spawned. A
                                * spawn and a pickup only ever name a placement */
    uint8_t  actor_generation; /* which life, because a placement out of range respawns */
    uint16_t actor_script;     /* spawns only: the script override the spawner takes */
    uint8_t  actor_reason;     /* despawns only: one of MP_EVENT_REMOVE_* */
    uint8_t  actor_burst;      /* despawns only: the speed the actor burst into its pieces at
                                * just before, in quarters, 0 for a body that simply goes. Any
                                * byte decodes; the receiver bursts only a replica it may */

    /* Appearance. The identity is the asset NAME and not an index, because no index means the same
     * thing on two machines: all 164 profile characters ride hero slot 3, so the hero byte cannot
     * tell them apart, and the roster's own order differs between two panels of one build. */
    uint8_t  pickup_kind;      /* pickups only: the shooter class the placement carries */
    uint8_t  sound_what;       /* a player's sound only: one of MP_PLAYER_SOUND_ */
    uint8_t  sound_index;      /* and its index in the table `sound_what` names */
    uint8_t  sound_flags;
    uint8_t  skin_kind;                      /* MP_SKIN_CHARACTER or MP_SKIN_MODEL */
    uint8_t  skin_hero;                      /* the hero slot the change rides */
    uint8_t  skin_slot;                      /* the world slot whose body now looks like this */
    uint16_t skin_scale;                     /* hundredths of its own size, 0 for nobody said */
    char     skin_asset[MP_EVENT_ASSET_MAX]; /* NUL terminated, and the rest zero */
} mp_event_t;

/* A small ring: what the local player did since the last send, and what the far player did that
 * the puppet has not performed yet. Sixteen is far more than a player can do in a tick, and a full
 * ring drops the oldest and counts it rather than blocking anything. */
#define MP_EVENT_QUEUE_SLOTS 16u

typedef struct mp_event_queue {
    mp_event_t items[MP_EVENT_QUEUE_SLOTS];
    size_t     head;
    size_t     count;
    uint32_t   dropped;
} mp_event_queue_t;

void mp_event_queue_init(mp_event_queue_t *queue);
void mp_event_queue_push(mp_event_queue_t *queue, const mp_event_t *event);
bool mp_event_queue_peek(const mp_event_queue_t *queue, mp_event_t *out);
bool mp_event_queue_pop(mp_event_queue_t *queue, mp_event_t *out);

/* Returns the encoded length, 0 when the event is malformed or the buffer too small. */
size_t mp_event_encode(const mp_event_t *event, uint8_t *buffer, size_t capacity);

/* True when a reliable message of this length and first byte is an event at all; the decoder then
 * refuses one whose fields do not fit. */
bool mp_event_is_event(const uint8_t *buffer, size_t bytes);
bool mp_event_decode(const uint8_t *buffer, size_t bytes, mp_event_t *out);

/* A player's moment rewritten in place for the next leg of its way, by the host that passes it
 * on: the tick becomes the host's, and a body's moment names `source_slot`, the slot of the peer
 * it came from, which only the host can vouch for. False, with the message untouched, for a
 * message that is none of the six or does not decode, and for a slot past the snapshot's table;
 * a pickup claim is none of the six, because the host answers it rather than passing it on. The
 * six are the shot, the push, the sabre, the weapon change, a player's sound, and the mover,
 * which names no slot. */
bool mp_event_restamp(uint8_t *note, size_t bytes, uint8_t source_slot, uint32_t tick);

/* The body frame. The engine builds an object's pose from its three Euler angles (pitch about x,
 * yaw about y, roll about z, all degrees) as three columns; forward is the second column, which
 * for an upright body is (-sin yaw, cos yaw, 0), and a positive pitch raises it. The same nine
 * products serve the sender, which turns a world muzzle into the body's axes, and the puppet,
 * which turns the offset back out at its own placement, so the two can never disagree about the
 * convention. `matrix` is the three columns in order, nine floats. An independent check that does
 * not touch the pose builder: the player's own contact handler computes its knockback direction
 * from sincos(heading + 180) as (-s, c), which is the same forward with the same sign. Two
 * readings of two functions agreeing is what lets the sign travel without the configurable
 * constant an earlier draft had wanted; the unit test pins forward at yaw 0, 30 and 90 degrees,
 * the lift of a positive pitch, and the round trip with all three angles set. */
void mp_event_body_matrix(float pitch, float yaw, float roll, float matrix[9]);
void mp_event_to_local(const float matrix[9], const float world_delta[3], float local[3]);
void mp_event_to_world(const float matrix[9], const float local[3], float world_delta[3]);

/* An angle folded into 0..360, as the wire's angle quantiser expects. */
float mp_event_wrap360(float degrees);

/* The actor a far body is built from for an appearance of `kind` named `name`, worn as hero
 * `hero`. A character is the actor it names. A model is worn over the body that is there: `kept`,
 * the actor the bank was last built from, when that was for the same hero, and otherwise the
 * empty name, which the body module reads as the hero's own shipped asset. `kept` may be NULL.
 * The one place the two kinds part on the receiving side. */
const char *mp_skin_body_actor(uint8_t kind, const char *name, uint8_t hero, const char *kept,
                               uint8_t kept_hero);

#endif /* MULTIPLAYER_MP_EVENTS_H */
