/* mp_wire.h: what goes on the wire, and the two readers that put it there and take it off.
 *
 * Layer 1. No engine, no address, no socket: this is arithmetic over a byte buffer and it runs in
 * the project's unit tests with nothing else in the process.
 *
 * The set of things a body sends is not invented here. The engine's own 1999 prototype settled it,
 * and the list is short on purpose: position, orientation, two flags, a weapon and hero identity,
 * the owner's health as one byte, and the animation as a PLAY STATE rather than as a pose, with the
 * playhead of each channel so the far body runs in phase. No velocity, no ground contact, no
 * inventory, no mode. Whoever chose that had full knowledge of this engine, and the reason it
 * still holds is that the receiver feeds the engine's own interpolation with it. Shots left the
 * record for the event stream, where a moment is delivered once rather than sampled.
 *
 * Three properties this module owes its callers, all of them checkable without a game:
 *
 *   OVERFLOW IS STICKY. A writer that has run out of room stays failed, and so does a reader that
 *   has run past the end. The failure that matters is not the one that reports; it is half a packet
 *   that reports success.
 *
 *   Nothing that is not a number goes out. A NaN position encoded as bits and decoded on the far
 *   side is a body at an undefined place, and every piece of arithmetic that touches it afterwards
 *   becomes NaN too. It is refused at the encoder, where there is still somebody to tell.
 *
 *   The byte order is the protocol's, not the host's. Every multi byte value is written a byte at a
 *   time, least significant first, so the wire format does not change if this is ever built for a
 *   machine that disagrees.
 */
#ifndef MULTIPLAYER_MP_WIRE_H
#define MULTIPLAYER_MP_WIRE_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

/* Bumped whenever the meaning of any field changes. Two builds that disagree here must not talk to
 * each other, and the handshake is where that is decided: the join request carries the number and
 * a host denies any other with the reason on screen. Nothing after the handshake carries it, so a
 * change to what a connected packet means needs a new number here or goes unnoticed. */
#define MP_WIRE_VERSION 35u  /* 35: a join is judged by what has to be the same and nothing
                              *     else. The join request names the game data, the damage
                              *     table out of damage.txt and the roster out of
                              *     characters.ini, and the build of the multiplayer by its
                              *     linker stamp and image size, behind the name and also when
                              *     the join comes from the lobby; the word where 34 put its
                              *     content fingerprint is zero. A refusal carries a detail
                              *     behind its reason byte: which file or which mod, as a
                              *     number from the list below and as text, and the host's own
                              *     stamp and release number, so a client can say what differs.
                              *     Game data is still reason 3, a required mod is the new
                              *     reason 7. Difficulty, detail level, the cheat cells and the
                              *     mods' ini sections count in neither the request nor the
                              *     level's content note 0x93, which now carries the same
                              *     composition as the request. And a new reliable state note
                              *     0xAB (MP_HOST_SETTINGS_TAG), from the host to every peer:
                              *     the host's world settings, the draw distance, the fog band,
                              *     dismemberment and two cheat bits, which a client plays for
                              *     the session without writing them into its own ini. A build
                              *     on 34 reads the request's statement as padding and the
                              *     detail of a refusal not at all, and would drop every 0xAB.
                              *     Behind its mods the statement lists the DLLs outside this
                              *     release that the joining side has loaded from its mods
                              *     folder, and a host refuses one its [multiplayer] AllowMods
                              *     does not name with reason 8. The list sits where the
                              *     decoder of the mods reads nothing, so the number stays:
                              *     two builds that disagree about it differ in the build of
                              *     the multiplayer, which the host refuses first.
                              * 34: the chat puts two new tags on the reliable channel: a
                              *     player's line said to its host, 0xA9, with nothing in it
                              *     but a serial and the text, and the host's line for every
                              *     player, 0xAA, with the slot and the name the host stamps
                              *     from its own table. No older message changes. The number
                              *     moves because a host on 33 refuses every 0xA9 as a note no
                              *     player says and a client on 33 drops every 0xAA without a
                              *     count, so a player would type lines nobody ever reads and
                              *     never see one come back; the handshake is where that is
                              *     refused rather than found out in a level.
                              * 33: three changes, and a build on 32 misreads all of them.
                              *     A client's payload prefix grows from four bytes to eight:
                              *     behind the newest host tick it holds come thirty one bits,
                              *     one for each tick before that one it holds as well, so the
                              *     host opens only the keys of a payload that really did not
                              *     arrive. Bit 31 is always clear. A host on 32 would read the
                              *     bits as the head of the snapshot and refuse every state.
                              *     A BODY carries the world it was sent from: the generation of
                              *     the setup note its sender acted on, four bits in the high
                              *     half of the flag byte. A reader never blends two worlds and
                              *     takes no pose of another one as a place in its own.
                              *     And the setup note 0x92 grows from 110 to 111 bytes: its last
                              *     byte is the host's difficulty plus one, nought for none, so
                              *     every client plays the host's column of the damage table.
                              * 32: nothing a player sees or hears may be missing on another
                              *     machine, and three things change shape for it at once.
                              *     The ENEMY record keeps only states: the three event fields
                              *     `limb`, `fx` and `block` and the dead reckoning that was never
                              *     read (`vel_x/y/z`, `turn_stage` and its presence bit) leave
                              *     it; `shield` grows to sixteen bits, its high byte the radius
                              *     in eighths of a unit, and five fields come in, `body` and the
                              *     four words of the hidden nodes and meshes, 21 fields in all
                              *     behind a mask of three bytes. The state word's bits 0x4000
                              *     and 0x8000, the hand and the sabre on 31, say whether the node
                              *     words and the mesh words are there. A build on 31 reads every
                              *     record against a table of other widths.
                              *     Every enemy block carries the WORLD EVENTS in front of its
                              *     records: a head of five bytes, the count and the two music
                              *     calls the host claims for that peer, and the events, each
                              *     with a number of its own and a length a later build can step
                              *     over. The block header grows from 35 to 40 bytes.
                              *     The level's state 0xA3 knows four more parts, the loops, the
                              *     fog each viewer sees (twelve bytes each, the script's index
                              *     among them), the escort and a journal; a build on 31 refuses a
                              *     note that sets one. Its fog part 0x08 changes shape without a
                              *     new bit: the fog as a state of 22 bytes, where 31 carried the
                              *     last four commands, which a build on 31 would read wrongly.
                              *     And a new tag 0xA8, a moment of a player's own body passed on
                              *     like a shot: a sound it made, or its shield going up or down,
                              *     eight kinds.
                              * 31: four new tags on the reliable channel: the scene the host
                              *     gathers everybody for, the host's note of its push blocks, a
                              *     client's wish to push one, and a push block starting to
                              *     fall. No older message
                              *     changes. The number moves because a build on 30 would go on
                              *     pushing and sinking the host's blocks by itself and gather
                              *     nobody for a scene, and the handshake is where that is
                              *     refused rather than found out in a level.
                              * 30: two things an enemy does in front of the host reach a client.
                              *     The ENEMY record gains a last field `block`, a u16 event:
                              *     a blade clang the host's cooldown let through, played again
                              *     on the replica. It is the 23rd field and the mask stays three
                              *     bytes, but a build on 29 walks 22 rows, leaves the two bytes
                              *     lying and reads every record behind it in the block from the
                              *     wrong place; a whole record always carries the field, so
                              *     that would be every block.
                              *     And the removal 0x88 grows from 11 to 12 bytes: its last byte
                              *     is the speed of a burst into pieces, in quarters, 0 for none,
                              *     so a client bursts its replica before it removes it. A build
                              *     on 29 does not recognise a 12 byte removal at all and would
                              *     keep every enemy the host removed standing.
                              * 29: the level's state travels, a new note 0xA3 from the host:
                              *     the emitter placements, the level lights and the director's
                              *     fog commands a script switched, read out of the host's
                              *     memory and matched on a client, which refuses its own
                              *     scripts those switches. A build on 28 would count the note
                              *     as unknown and go on switching its own level.
                              *     The ENEMY record changed with it, and that is the dangerous
                              *     half: the emitter field `fx` goes from u16 to u32 and the
                              *     limb field from u8 to u16, both events with a sequence
                              *     number now, and a field `shield` is appended for the
                              *     droideka's shield. A build on 28 reads the change mask
                              *     against a table with other widths and puts every field
                              *     behind `limb` in the wrong place.
                              *     This number also covers a change of 2026-09-22 that came
                              *     without one: a host describes a record whole until an
                              *     acknowledgement confirms a view, and a client refuses a
                              *     record with no base that names no position. Two builds on
                              *     28 from before and after that change passed the handshake
                              *     and then stood each other's enemies still.
                              * 28: BOTH hit messages carry the node that was struck, as an id
                              *     into a shipped table of node names rather than an index
                              *     into one model's own table, which would mean a different
                              *     joint on a swapped rig. The reported hit goes from 6 to 7
                              *     bytes and the player hit from 7 to 8, so a build on 27
                              *     drops every one of them without even counting it as torn.
                              *     Two readers get their answer back: the engine's own impact
                              *     effect for the melee code 0x21, and the dismemberment mod,
                              *     which picks the limb it severs from that cell.
                              *     This number also covers the entity spawner's half, decided
                              *     on the same day: a client removing its own copies, and the
                              *     host's cap travelling to a client. One number for both,
                              *     because two builds that each call themselves 28 would pass
                              *     the handshake and then read each other wrong, which is
                              *     worse than a refused connection.
                              *     And the BODY RECORD grew a byte with it: the locomotion
                              *     state, so a far player's footfalls can be made where they
                              *     are seen. That one is the dangerous half of this version.
                              *     A dropped message costs one message; a body record read
                              *     one byte short puts every field after it, the animation
                              *     channels and every twist, into the wrong place.
                              *     The ENEMY record gained two fields under this number as
                              *     well, the limb an actor lost and the emitter a script hung
                              *     on it. They cost nothing while they are empty, because that
                              *     record is delta coded and a field equal to its baseline is
                              *     absent from the mask, but the field ORDER moved: a build on
                              *     an older 28 reads the mask against a shorter table.
                              * 27: the roster entry carries the kind of its asset as its last
                              *     byte, so a model worn over a hero is not built as an actor
                              *     by a player who joined later or by another client of the
                              *     same host. A build on 26 takes a 57 byte entry for a torn
                              *     roster.
                              * 26: an ally's bolt, class 1 out of the AI, travels as an NPC's
                              *     bolt (0xA0), where a build on 25 refused it as torn; it was
                              *     the host player's shot before.
                              * 25: the NPC copies an editor spawns travel: a client's wish
                              *     (0xA1) and the host's entry (0xA2), the copies' records in
                              *     the enemy block with the generation of their grant, and a
                              *     deathmatch that performs a client's hit on a copy. A build
                              *     on 24 would take none of it and hit a copy's key as nothing.
                              * 24: an enemy is named by a KEY of two bytes wherever a message
                              *     names one: a removal, a reported hit and a player hit.
                              *     The placements keep 0 to 255, and 256 and up are copies
                              *     an editor spawns, for which Mos Espa, holding 255
                              *     placements, has no byte left. The three messages grow by
                              *     a byte each, so a build on 23 recognises none of them.
                              *     The same number covers the appearance event and the
                              *     roster entry, which grew by the two bytes of the body's
                              *     scale while the version stood still.
                              * 23: the host names every bolt its NPCs fire and a client fires
                              *     the same bolt; a hit by one is decided where its victim
                              *     sits, which a build on 22 would take a second time.
                              * 22: the handshake keeps no state for an address that has not
                              *     answered, and no password crosses it. The challenge carries
                              *     a COOKIE, a digest over the address, both salts and the time
                              *     under a secret only the host knows, and the host claims a
                              *     slot only for a response that echoes it; the response
                              *     carries a PROOF, a digest over both salts under the password,
                              *     where the request used to carry the password itself. A build
                              *     on 21 is refused by version before either matters.
                              * 21: a pickup claim names its claimant's world slot behind the
                              *     tick, and the host's grant names the same slot, so only the
                              *     claimant applies it; every client used to apply every grant.
                              *     The claim is a byte longer, so a build on 20 recognises none.
                              * 20: the four moments of a body, the shot, the push, the sabre
                              *     action and the weapon change, name the WORLD SLOT of the
                              *     player who did them, one byte behind the tick, and a listen
                              *     host passes a client's on to the other clients with that slot
                              *     and its own tick written in. Each of the four is a byte
                              *     longer, so a build on 19 recognises none of them and drops
                              *     every one without a word, which is why this is a version and
                              *     not a tolerated difference.
                              * 19: the savegame no longer travels as reliable messages. It has a
                              *     lane of its own (PKT_BULK), the slices go out unordered, and
                              *     the receiver answers with a bitmask of what it holds, which
                              *     is at once the request, the acknowledgement and the only thing
                              *     that may call a transfer finished. A build on 18 knows neither
                              *     the packet type nor the mask, so a client of it would wait in
                              *     its lobby for a file that never came and nothing would say so.
                              *     That silence is why this is a version bump and not a tolerated
                              *     difference: a refused handshake names the reason on screen.
                              *     The reliable channel also lost thirteen bytes of its largest
                              *     message in the same delivery, because it had been sizing
                              *     itself against a budget the carrier had already spent.
                              * 18: the setup note names the savegame the host restores (its
                              *     digest and its size), and two notes carry that file to the
                              *     clients in chunks (mp_savefile). A client on 17 would refuse
                              *     every setup note for its length and never load anything.
                              * 17: the appearance change names the WORLD SLOT it belongs to, and
                              *     the roster carries every player's asset NAME. The event on its
                              *     own served two players and nobody who joined late: an event is
                              *     an edge, and a late joiner has missed every edge that fired
                              *     before it arrived. The roster repeats, so it is what tells a
                              *     late joiner what the players already here look like.
                              *     The host's setup note grew by ten bytes in the same delivery:
                              *     nine of a RULE SET and one of a GENERATION. The rules belong
                              *     to the session rather than to the machine, so they go where a
                              *     late joiner will hear them; the generation replaces the start
                              *     bit as the thing a client acts on, because a bit that is never
                              *     cleared can only send everybody into a level once. A round's
                              *     SCORE TABLE became a note of its own, 0x95, repeated for the
                              *     same reason. No further version step: none of this had been
                              *     delivered when it was written, so there is no build in the
                              *     field that understands 17 without it.
                              * 16: fifteen peers and sixteen bodies in a snapshot, so the present
                              *     mask became two bytes; sixteen roster lines, each with a hero
                              *     byte; and the two lobby notes, team, ready and hero from a
                              *     player to the host, and game, level and the start bit from the
                              *     host back to everyone.
                              * 15: the request carries a PASSWORD in sixteen bytes behind the
                              *     name, and a host with one denies a mismatch (MP_DENY_PASSWORD).
                              * 14: a reported hit carries the IMPACT CODE and the two contact
                              *     channels. The host used to read the impact off its own
                              *     puppet a round trip later, by which time the swing was
                              *     over and the body carried 0x29, the table row whose
                              *     damage is zero in every column.
                              * 13: the join request carries the player's NAME in sixteen
                              *     bytes of its padding, and the accept answers with the
                              *     host's; a roster message (0x8F) names everyone. A byte that
                              *     was padding and now carries a name is such a change.
                              * 12: the enemy block says WHICH LEVEL it describes, in two
                              *     bytes behind the count. A receiver now creates the bodies
                              *     the block names, and a block from the level the host just
                              *     left would otherwise create the wrong ones here.
                              * 11: an enemy record ALWAYS carries its twist count, because that
                              *     count is the length of what follows it. Deltaing it let a
                              *     receiver read the wrong number of tail bytes and lose the
                              *     whole block.
                              * 10: an enemy record says whether its PLAYHEAD is there. A zero
                              *     playhead and an absent one used to be the same bits.
                              * 9: the host's payload carries an ENEMY BLOCK in front of the
                              *    snapshot, behind a two byte length. A version 8 receiver
                              *    would read that length as the snapshot's own header.
                              * 8: the join request names the game mode, in a byte that was
                              *    padding before. 7: the map's digest says which level it
                              *    describes. The version moves whenever the meaning of a
                              *    byte changes, and a byte that carried nothing and now
                              *    carries a mode is such a change.
                              * 6: a mover opening or closing travels as an event of its own,
                              *    eight bytes, tag 0x85; a build without the tag would take
                              *    it for a torn reliable message. 5: the weapon change became
                              *    an event of its own, so a build without it would show the
                              *    far weapon changing late while believing itself in step.
                              *    Neither changed a byte of the body record. 4: the shot flag
                              *    left the record, the owner's health joined it as one byte
                              *    between the hero and the animation, and the track words
                              *    became playheads with a mask that means something. */

/* Four tags handed out in one place, so that the two features using them, the scene and the push
 * blocks, cannot take one number twice. The scene's is retired with the wire number unchanged: a
 * build that still sends it differs in the build of the multiplayer, which a host refuses at the
 * handshake before any note is said. */
#define MP_SCENE_NOTE_TAG 0xA4u   /* RETIRED: the host's scene, told to every client while a
                                   * scene gathered all players; nobody sends it and nobody
                                   * reads it, and the number stays claimed */
#define MP_CRATE_NOTE_TAG 0xA5u   /* the host's note of its push blocks */
#define MP_CRATE_PUSH_TAG 0xA6u   /* a client's wish to push one */
#define MP_CRATE_FALL_TAG 0xA7u   /* a push block starting to fall, once */

/* An angle on the wire. The engine works in degrees, and a sixteenth of a degree is finer than the
 * eye resolves at any distance a body is visible from, so a whole turn fits in sixteen bits. */
#define MP_WIRE_ANGLE_SCALE 182.044444f   /* 65536 / 360 */

/* A position on the wire, in fixed point.
 *
 * The census this comment used to ask for has been done. Every world coordinate in the eleven
 * shipped levels, over 2250 enemy placements and their waypoints: x from 0.5 to 240.0, y from -2.1
 * to 176.7, z from 1.0 to 97.0. The largest magnitude anywhere is 240 units.
 *
 * The range below therefore carries about a hundred and thirty times more than any shipped level
 * uses, and narrowing it to sixteen bits would save a byte an axis on every body. That is a change
 * to what is on the wire and it is not made here as a side effect; what is recorded here is that
 * the measurement now exists and that the enemy record already acts on it. */
#define MP_WIRE_POSITION_SCALE 256.0f     /* 1/256 of a unit */
#define MP_WIRE_POSITION_LIMIT 8388607    /* what 24 bits of signed fixed point reaches */

/* An analogue axis on the wire: signed sixteen bit fixed point over [-1, 1]. An axis is not an
 * angle. Full left and full right are opposite ends of a stick, not the same heading, so it must
 * never wrap; the first command encoder sent it through the angle quantiser, and a full left turn
 * arrived at the host as a turn to the right. */
#define MP_WIRE_AXIS_SCALE 32767.0f

typedef struct mp_wire_writer {
    uint8_t *buffer;
    size_t   size;
    size_t   at;
    bool     overflowed;   /* sticky */
} mp_wire_writer_t;

typedef struct mp_wire_reader {
    const uint8_t *buffer;
    size_t         size;
    size_t         at;
    bool           overran;    /* sticky */
} mp_wire_reader_t;

/* The animation, as the 1999 prototype sent it: which clip is playing on each channel and how far
 * into it, never the joint angles. The receiver hands this to the engine's own interpolation, which
 * is why the pose itself never has to cross.
 *
 * The playhead is in frames times sixteen, because the engine's own track time is a frame count
 * at the clip's own rate and a sixteenth of a frame is finer than any blend resolves; 4095 frames
 * is the ceiling, not yet checked against the longest shipped clip, and a sender clamps rather
 * than wraps. The mask says what is actually running:
 * bit 0 the base channel is live, bit 1 the overlay channel is live (its track still belongs to
 * the ordinal sent), bit 2 the base clip's last change was a crossfade rather than a cut. */
typedef struct mp_wire_anim {
    uint16_t clip[2];        /* the ordinal on each of the two channels */
    uint16_t track[2];       /* the playhead of each channel, in frames times sixteen */
    uint32_t channel_mask;   /* the live and fade bits above */
} mp_wire_anim_t;

#define MP_WIRE_ANIM_BASE_LIVE    0x1u
#define MP_WIRE_ANIM_OVERLAY_LIVE 0x2u
#define MP_WIRE_ANIM_BASE_FADED   0x4u
#define MP_WIRE_TRACK_SCALE       16.0f
#define MP_WIRE_TRACK_MAX         65535u

/* One node turned on top of the clips: the root toward the travel direction, the chest toward the
 * aim, the torso under a hit. Degrees, -180..180; a node with both angles zero is not sent. Six is
 * more than the engine and the input fixes turn on one body at once. */
#define MP_WIRE_MAX_TWISTS 6u

typedef struct mp_wire_twist {
    uint8_t node;
    float   pitch;
    float   yaw;
} mp_wire_twist_t;

/* The world a body was sent from, in the four high bits of its flag byte: the generation of the
 * setup note its sender acted on, which is the one its level began under, cut to four bits. Two
 * worlds sixteen changes apart share a number, and no pose lives in a history that long. */
#define MP_WIRE_WORLD_MASK 0x0Fu
uint8_t mp_wire_world_of(uint8_t generation);

/* Shots do not travel in the body: a shot is a moment, and a moment sent as state is lost whenever
 * two of them fall inside one packet interval. The events module carries them. The flag bit that
 * once said "fired" stays reserved and zero on the wire. */
typedef struct mp_wire_body {
    float          position[3];
    float          orientation[3];   /* degrees */
    bool           alive;
    bool           dead;             /* not the negation of alive: a body can be neither yet */
    uint8_t        world;            /* the sender's world, 0..MP_WIRE_WORLD_MASK */
    uint8_t        weapon;
    uint8_t        hero;
    uint8_t        health;           /* the owner's health, 0..100 in play; a receiver treats
                                      * anything above 100 as 100 */
    uint8_t        loco;             /* the locomotion state the engine last asked a footfall
                                      * for, 0 for none. It travels because the engine stores
                                      * it nowhere: it is a literal at each call site, picked
                                      * by the same test that picks the clip. */
    mp_wire_anim_t anim;
    uint8_t         twist_count;
    mp_wire_twist_t twist[MP_WIRE_MAX_TWISTS];
} mp_wire_body_t;

/* How many bytes a body record takes: three positions of four, three angles of two, the flag
 * byte, four identity bytes (weapon, hero, health, locomotion state), two clip and track
 * pairs of four, the
 * channel mask, the twist count, then five bytes per twist. The minimum is a body with no twist,
 * the maximum one with six; a caller sizing a datagram uses the maximum. The unit test encodes a
 * record and compares the writer's position against these numbers, which is the only thing that
 * keeps them true: the constant once said 30 while the record was 33, nothing in the language
 * checks such a number, and a caller sizing a datagram by it would have cut three bytes off every
 * body. The same test pins the health byte as the twenty second byte, so the field order cannot
 * drift silently either. */
#define MP_WIRE_BODY_MIN_BYTES 36u
#define MP_WIRE_BODY_MAX_BYTES (MP_WIRE_BODY_MIN_BYTES + MP_WIRE_MAX_TWISTS * 5u)

void mp_wire_writer_init(mp_wire_writer_t *writer, void *buffer, size_t size);
void mp_wire_reader_init(mp_wire_reader_t *reader, const void *buffer, size_t size);

/* Every one of these returns false once the writer has overflowed, and keeps returning false. */
/* How big a body is drawn, in hundredths of the size its own asset asks for, as two messages
 * carry it: the appearance event and the repeated roster. ZERO is "nobody said", which is what a
 * build without the developer overlay's two player scales sends and what every sender before the
 * field existed sent; it reads as the body's own size.
 *
 * The bounds are the panel's own two factors with room around them, three times for the giant and
 * 0.35 for the doll. They live here, with the rule, because two messages carry the same number
 * and two rules for one number is how they come apart. */
#define MP_WIRE_SCALE_NONE 0u
#define MP_WIRE_SCALE_MIN  10u
#define MP_WIRE_SCALE_MAX  650u

/* Whether a scale is one a sender may have meant: nobody said, or inside the bounds. */
bool mp_wire_scale_is_sound(uint16_t scale);

/* The mods a join request and a refusal can name, by a number that keeps its meaning in every
 * build. The list only grows at its end and no number is ever given twice: two builds with lists
 * of different length still agree on every number both know, and a number a build does not know is
 * passed over rather than read as another mod. A refusal carries the name as text as well, so a
 * build that does not know the number can still say which mod it was. */
#define MP_WIRE_MOD_MULTIPLAYER 0u

/* The name an enemy travels under, which every message that names one shares. A PLACEMENT is
 * named by its index in the level, below 256: the largest shipped level holds 255, so a byte
 * was enough while nothing else needed a name. A COPY an editor spawns has no placement of its
 * own and is named 256 plus a number the host gives it, so the two can never be mistaken for
 * each other in any level. The copies are bounded by the engine's actor pool, 128 actors,
 * because each takes a slot there; the two bytes on the wire would carry more. */
#define MP_WIRE_KEY_COPY_BASE 256u
#define MP_WIRE_COPY_MAX      128u
#define MP_WIRE_KEY_COUNT     (MP_WIRE_KEY_COPY_BASE + MP_WIRE_COPY_MAX)
#define MP_WIRE_KEY_NONE      0xFFFFu   /* no enemy at all */

/* The one test for each kind, so that no reader spells the boundary again. */
bool mp_wire_key_is_placement(uint32_t key);
bool mp_wire_key_is_copy(uint32_t key);

bool mp_wire_put_u8 (mp_wire_writer_t *writer, uint8_t  value);
bool mp_wire_put_u16(mp_wire_writer_t *writer, uint16_t value);
bool mp_wire_put_u32(mp_wire_writer_t *writer, uint32_t value);

/* Refuses a value that is not finite, and refuses one outside the range the fixed point reaches.
 * Both are failures of the caller rather than of the wire, and both are silent if they are not. */
bool mp_wire_put_position(mp_wire_writer_t *writer, float value);
bool mp_wire_put_angle   (mp_wire_writer_t *writer, float degrees);

/* Refuses a value that is not finite. A finite value beyond full deflection is clamped rather than
 * refused, because more than full deflection means full deflection and nothing else. */
bool mp_wire_put_axis    (mp_wire_writer_t *writer, float value);

bool mp_wire_get_u8 (mp_wire_reader_t *reader, uint8_t  *value);
bool mp_wire_get_u16(mp_wire_reader_t *reader, uint16_t *value);
bool mp_wire_get_u32(mp_wire_reader_t *reader, uint32_t *value);
bool mp_wire_get_position(mp_wire_reader_t *reader, float *value);
bool mp_wire_get_angle   (mp_wire_reader_t *reader, float *degrees);
bool mp_wire_get_axis    (mp_wire_reader_t *reader, float *value);

/* The whole body record, version stamped by the caller's packet header rather than per body. */
bool mp_wire_put_body(mp_wire_writer_t *writer, const mp_wire_body_t *body);
bool mp_wire_get_body(mp_wire_reader_t *reader, mp_wire_body_t *body);

/* The engine's health is a signed dword that its own setter does not clamp, and the record's
 * byte must never wrap: a health of 300 would arrive as 44 and a negative one as a large number.
 * The clamp is here, in the wire's own arithmetic, so every sender applies the same one. */
uint8_t mp_wire_clamp_health(int32_t health);

/* A playhead in frames to the wire's sixteenths, clamped to what sixteen bits hold; a value that
 * is not a number or is negative reads as the clip's start. */
uint16_t mp_wire_track_from_frames(float frames);

/* What a round trip costs in accuracy, so a caller can decide whether it matters rather than
 * discovering it. Both are the worst case, half a step of the respective scale. */
#define MP_WIRE_POSITION_ERROR (0.5f / MP_WIRE_POSITION_SCALE)
#define MP_WIRE_ANGLE_ERROR    (0.5f / MP_WIRE_ANGLE_SCALE)
#define MP_WIRE_AXIS_ERROR     (0.5f / MP_WIRE_AXIS_SCALE)

#endif /* MULTIPLAYER_MP_WIRE_H */
