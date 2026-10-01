/* mp_npc_shot.h: an NPC's bolt, as the host tells its clients, and the rules for it.
 *
 * The host's NPCs are parked replicas on a client and fire nothing there, so without this message
 * a client sees none of their bolts while the hits of those bolts still reach it. The host names
 * each bolt one of its NPCs fires: the kind, the class it was fired with, the muzzle in world
 * coordinates and the two angles. A client's engine fires the same bolt once its replay of the
 * host has reached the tick the message carries.
 *
 * Pure: the codec, which kinds travel, when a held bolt is due, and a small memory of the bolts a
 * machine has sent or fired as a copy and of the sub shots their impacts made, which is how the
 * two hit reports tell those bolts apart. */
#ifndef MP_NPC_SHOT_H
#define MP_NPC_SHOT_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#define MP_NPC_SHOT_TAG   0xA0u
/* tag, host tick, level, kind, class, three positions of four bytes, two angles of two */
#define MP_NPC_SHOT_BYTES 25u

/* The engine's projectile table has 38 rows; a kind past it names no bolt. */
#define MP_NPC_SHOT_KINDS 38u

typedef struct mp_npc_shot {
    uint32_t tick;          /* the host's substep the bolt was caught in */
    uint16_t level;         /* the level identity, the mover count of the host's world */
    uint8_t  kind;          /* the engine's projectile kind */
    uint8_t  shooter_class; /* the class it was fired with, which decides what it may hit */
    float    muzzle[3];     /* where the bolt left the weapon, world coordinates */
    float    pitch;         /* degrees */
    float    yaw;           /* degrees */
} mp_npc_shot_t;

/* Returns the encoded length, 0 when the buffer is too small. */
size_t mp_npc_shot_encode(const mp_npc_shot_t *shot, uint8_t *buffer, size_t capacity);
bool   mp_npc_shot_is(const uint8_t *note, size_t bytes);
/* Refuses a class of 0, the engine's own effects, and a kind past the table. Class 1 is an ally's
 * bolt: a player's never travels here, it goes as that player's own event. */
bool   mp_npc_shot_decode(const uint8_t *note, size_t bytes, mp_npc_shot_t *out);


/* Whether a bolt of this kind travels.
 *
 * A kind with no handler in the projectile table is a plain bolt and flies alike on both machines.
 * Of the kinds WITH a handler, all but one travel as well: the grenade, the rocket, the tank
 * shell, the player's rocket, the force wave in both its forms and the energy ball. Their area
 * rings are posted from the shot's own object, which is the object the sender remembers, so the
 * guard that keeps a hit from hurting twice sees them.
 *
 * The THERMAL DETONATOR travels only where `sub_shots_tied` says the machine ties a sub shot to
 * the bolt whose impact made it. It hurts in two ways and the guard has to see both: its own
 * object, whose side the engine clears at birth so that it hurts everyone including the thrower,
 * and the fireball its impact spawns with class 0, which posts a ring every substep for most of
 * its life. The memory takes the side a bolt carries once the engine has made it, which is the
 * zero, and the fireball inherits the entry of the detonator whose impact made it. Without that
 * tie the host would mirror the fireball's hits AND the copy's fireball would hurt the client's
 * own player: double damage, on the heaviest damage line in the table.
 *
 * The PLAYER ZAP does not fly and does not collide. Its handler names whichever player is alive on
 * the machine it runs on and posts a contact straight at them, with no distance and no line of
 * sight. A copy on a client therefore hurts THAT client, wherever they stand, and it goes around
 * the contact dispatcher the guard hangs on. What is wanted from it is the pair of arcs it draws,
 * and those belong in an effect rebuilt the way a replicated blade's are, not in a copy of the
 * shot.
 *
 * Three more cannot travel at all, and the rule is not what stops them: the fireball and the
 * bubble are spawned with shooter class 0, which the sending hull refuses before this rule is
 * ever asked, and the area blast returns no object, so the listener is never called for it. The
 * line for the blast stays because it is the only bolt against such a message arriving from the
 * WIRE: a decoder takes any kind under the table's length, and this rule is where it is stopped.
 */
#define MP_NPC_SHOT_THERMAL 10u

bool mp_npc_shot_kind_travels(uint32_t kind, bool has_handler, bool sub_shots_tied);

/* The two kinds an explosive's impact spawns beneath itself, always with shooter class 0: the
 * ring a rocket leaves and the fireball a thermal detonator leaves. Nothing else in the engine
 * spawns either. The ring's class is 0 and it has no handler and no blast light, so it never
 * sends a contact; the fireball's blast light posts a ring from its own object every substep. */
#define MP_NPC_SHOT_EXPLOSION_RING 0x1Du
#define MP_NPC_SHOT_FIREBALL       0x1Eu

/* The player zap, which does not travel as a bolt (above). Its two arcs do, as a world event at
 * the actor that fired it, naming the world slot of the player the host's handler named.
 *
 * The arcs are not quite cosmetic: once a second the engine's arc tick posts a contact to the
 * first thing an arc hangs on whose class is 1, and class 1 is the player of the machine the arc
 * runs on. A replayed arc may therefore never hang on a body of class 1. The ends a client may use
 * are the shooter's replica and the far body of the player the host named; a far body carries its
 * bank's class, 5 to 7, and a replica an NPC's. The rule refuses an end of class 1 all the same,
 * and when the named player is this machine's own the arcs hang on the shooter alone. */
#define MP_NPC_SHOT_PLAYER_ZAP 24u

uint16_t mp_npc_shot_zap_pack(uint8_t victim_slot);
bool     mp_npc_shot_zap_unpack(uint16_t packed, uint8_t *victim_slot);

typedef enum mp_npc_shot_zap_ends {
    MP_NPC_SHOT_ZAP_BOTH = 0,        /* from the shooter to the named player's body */
    MP_NPC_SHOT_ZAP_SHOOTER_ALONE,   /* on the shooter only */
    MP_NPC_SHOT_ZAP_NONE             /* nowhere: the shooter is no body a replayed arc may use */
} mp_npc_shot_zap_ends_t;

/* Where a client hangs the arcs. `victim_here` says the named player has a far body on this
 * machine, whose class is `victim_class`. */
mp_npc_shot_zap_ends_t mp_npc_shot_zap_ends(int32_t shooter_class, bool victim_here,
                                            int32_t victim_class);

/* When a held bolt is fired on a client, against the tick its replay of the host has reached. */
typedef enum mp_npc_shot_due {
    MP_NPC_SHOT_WAIT,   /* the replay is not there yet */
    MP_NPC_SHOT_FIRE,   /* fire it now */
    MP_NPC_SHOT_STALE   /* the replay is past it by more than the late margin */
} mp_npc_shot_due_t;

#define MP_NPC_SHOT_LATE_TICKS  16u   /* half a second */
#define MP_NPC_SHOT_AHEAD_TICKS 32u   /* a replay a second behind has lost its place */

mp_npc_shot_due_t mp_npc_shot_due(uint32_t shot_tick, uint32_t render_tick, bool render_known);

/* The bolts a machine has sent (a host) or fired as the host's copy (a client), and the sub shots
 * their impacts made, by the object the engine made for each, with the side it carried once the
 * engine had made it, the kind the engine made and the tick. A bolt older than the lifetime is
 * forgotten, and a hit it lands after that is reported the way every hit was before.
 *
 * The side is read after the spawn and not taken from the class the bolt was fired with, because
 * the two differ for exactly one kind: the thermal detonator's spawn arm clears it to 0. For every
 * other kind the spawn writes the class there and nothing changes it before the hull reads it.
 *
 * The ring has to hold every entry young enough to answer, or a bolt still flying is pushed out
 * and its hits are mirrored again while the copy hurts as well. A detonator's fuse is four seconds.
 * The relay asserts the size against its send budget; the entries are kept in the order they were
 * made, so every search stops at the first one past the lifetime and costs what is young, not
 * what the ring can hold. */
#define MP_NPC_SHOT_MEMORY   4096u
#define MP_NPC_SHOT_LIFETIME 160u   /* five seconds of substeps */

typedef struct mp_npc_shot_memory {
    uint32_t object[MP_NPC_SHOT_MEMORY];
    uint32_t tick[MP_NPC_SHOT_MEMORY];
    uint8_t  side[MP_NPC_SHOT_MEMORY];
    uint8_t  kind[MP_NPC_SHOT_MEMORY];
    size_t   next;
} mp_npc_shot_memory_t;

void mp_npc_shot_memory_clear(mp_npc_shot_memory_t *memory);

/* Whether a bolt whose object reads `side` once the engine has made it may be remembered. Not a
 * side of 1, which is also every player's: a player's own shot that the engine puts on the old
 * object of such a bolt within the lifetime would read as the NPC's, and its hits would go
 * unreported. An ally's bolt is told by the shot owners instead. An ally's thermal detonator
 * reads 0 and is remembered like an enemy's, because the side no longer says whose it is. */
bool mp_npc_shot_rememberable(uint32_t side);

/* True when the entry it wrote over was still young enough to answer, which a ring of the right
 * size never does; the caller counts it. `tick` must not run backwards between two calls. */
bool mp_npc_shot_remember(mp_npc_shot_memory_t *memory, uint32_t object, uint8_t side,
                          uint8_t kind, uint32_t tick);

/* Drops every young entry for `object`, and says how many there were. The engine hands an object
 * out again once the shot that flew as it is freed; told of every new shot, the memory drops the
 * old bolt there, so a new shot with the same kind and side is never read as the NPC's. That
 * matters for the side of 0, which a player's own thermal detonator carries as well. An entry past
 * the lifetime answers nothing and is left where it is. */
size_t mp_npc_shot_forget(mp_npc_shot_memory_t *memory, uint32_t object, uint32_t tick_now);

/* True when `object` is a remembered bolt, young enough, of the kind remembered, whose side still
 * reads what it read when the engine made it. A sabre that turns a bolt back gives it the
 * deflector's class, and a bolt a player turned back is that player's hit. `kind_now` is the kind
 * of the shot the object flies as now; an object that is no shot any more has none, and any value
 * past the table stands for that. */
bool mp_npc_shot_still_npcs(const mp_npc_shot_memory_t *memory, uint32_t object,
                            uint32_t side_now, uint32_t kind_now, uint32_t tick_now);

/* What a shot the engine has just made is to the memory, as the hull on its constructor sees it.
 * A sub shot is spawned inside the impact of its parent, and always with class 0. */
typedef enum mp_npc_shot_sub {
    MP_NPC_SHOT_SUB_NONE = 0,     /* not a sub shot */
    MP_NPC_SHOT_SUB_TIED,         /* made by the impact of a bolt the memory holds: its child */
    MP_NPC_SHOT_SUB_OF_ANOTHER,   /* made by the impact of a bolt this memory does not hold */
    MP_NPC_SHOT_SUB_ORPHAN        /* a kind only an impact makes, made outside every impact */
} mp_npc_shot_sub_t;

mp_npc_shot_sub_t mp_npc_shot_sub_verdict(bool in_impact, bool parent_is_npcs,
                                          int32_t shooter_class, uint32_t kind);

#endif
