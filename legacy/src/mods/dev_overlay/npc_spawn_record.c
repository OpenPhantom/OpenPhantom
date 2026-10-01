/* npc_spawn_record.c: see npc_spawn_record.h. */
#include "npc_spawn_record.h"

#include "entity_offer.h"
#include "npc_census.h"
#include "spawn_scripts.h"

#include <stdbool.h>
#include <stdint.h>
#include <string.h>

_Static_assert(NPC_SPAWN_RECORD_ROUTE_NODE + 0x10u == NPC_SPAWN_RECORD_BYTES,
               "a route of exactly one node");
_Static_assert(NPC_SPAWN_RECORD_BYTES >= PLACE_SIZE,
               "a copy's record holds the placement it copies");

#define CLASS_KILLABLE     2   /* the one class the contact handler hands damage to */
#define CLASS_ALLY         1   /* the player's own side: what a helper is raised as, so the other
                                * helpers are not its enemies (target kind 2 is the nearest
                                * class 2) and the enemies' fire goes past it */
#define CLASS_TRIPOD_GUN   4   /* the class the Use key mounts (Plr_GroundActions asks
                                * bapobj_findObject for class 4 ahead of the player) */

/* A shooter's reload floor for the explosive shot kinds, seconds: the engine rearms after
 * rand() * the record's fire interval and fires no sooner than half a second after, so a copy
 * of a placement the level paced by other means (the bazooka man, a small interval and a script
 * that waits between shots) fired rockets at a blaster's rate (played 2026-09-16). A bolt of
 * any kind keeps the record's cadence, as the droid fighter's own script fires every tick it
 * can; a rocket, a tank shell, a grenade, a thermal detonator, an energy ball or a fireball
 * (the engine's own shot handler table) waits up to four seconds. */
#define RELOAD_HEAVY       4.0f

static bool shot_kind_is_heavy(uint16_t kind)
{
    return kind == 5 || kind == 8 || kind == 9 || kind == 10 || kind == 12 || kind == 23 ||
           kind == 30;
}

/* A flyer, a record whose move mode is 2 (a hover, the gunboat and the probe droid) or 4 to 6
 * (pitching toward where it goes, the STAP, the birds and the fish), has its height tracked
 * toward its move destination's z (move_trackZ). Mode 3 is not a flyer: the levels place walkers
 * with no collision on it, trees, power-ups and the droid fighter, and a copy of the fighter hung
 * in the air until it was told apart (played 2026-09-16). */
bool npc_spawn_record_flies(const uint8_t *record)
{
    int32_t mode;

    memcpy(&mode, record + PLACE_MOVE_MODE, sizeof mode);
    return mode == 2 || (mode >= 4 && mode <= 6);
}

/* Mode 0 is every script's first mode; the source's starting mode was its own script's. Every
 * copy is class 2, whatever its source was: the contact handler (enemy_onContact, 0x00436A68
 * region) hands a hit to enemy_receiveDamage for class 2 and for no other, so a class 3 townsman
 * or a class 1 story stand-in could not be killed, and being killed is the one thing a spawned
 * copy should reliably do. A source with no hit points gets a few. Three exceptions: the tripod is
 * class 4, the one class the Use key mounts, so the player can take it over as they take over the
 * level's own; a pickup is the class the retail levels give it (entity_offer.h), since that class
 * is the contact code the player's pickup reads and a class 2 medpack would be a thing to shoot at;
 * and a helper is class 1, see CLASS_ALLY. A pickup's class wins over a helper's: the behaviour of
 * a pickup is ignored and its script stands still (spawn_scripts.c). A shooter fires the record's
 * first weapon,
 * so a droid with a bazooka fires its rockets and a guard his bolts, as the level's own do
 * (played 2026-09-16: a bazooka droid shooting bolts when the script named the bolt outright); a
 * source whose first weapon slot is empty gets the blaster, so the shot kind is never 0, and the
 * reload is raised to the weapon's floor, see RELOAD_HEAVY. A melee copy keeps its source's
 * interval, which gates its swings. And for every copy: the scan's own flag bit set, since a
 * level kind's source may be one a script raised; no removal by distance, since the copy's record
 * is in no directory the scan would bring it back from; no reveals; a route of one node. */
void npc_spawn_record_settle(uint8_t *record, const npc_spawn_desc_t *desc, const float *position,
                             float facing, bool shoots)
{
    uint32_t zero  = 0;
    uint32_t one   = 1;
    uint32_t flags = 0;
    int32_t  pickup = entity_offer_pickup_class(desc->file);
    int32_t  klass = (_stricmp(desc->file, NPC_SPAWN_TRIPOD_FILE) == 0) ? CLASS_TRIPOD_GUN
                   : (pickup != 0)                                      ? pickup
                   : (desc->behaviour == SPAWN_BEHAVIOUR_HELP)          ? CLASS_ALLY
                                                                        : CLASS_KILLABLE;
    int32_t  hit_points;

    memcpy(record + PLACE_POSITION, position, 3u * sizeof(float));
    memcpy(record + PLACE_START_YAW, &facing, sizeof facing);
    memcpy(record + PLACE_SPAWN_STATE, &zero, sizeof zero);
    memcpy(record + PLACE_LIVE_ACTOR, &zero, sizeof zero);
    memcpy(record + PLACE_REVEAL_COUNT, &zero, sizeof zero);
    memcpy(record + PLACE_START_MODE, &zero, sizeof zero);
    memcpy(record + PLACE_CLASS, &klass, sizeof klass);
    memcpy(&flags, record + PLACE_FLAGS, sizeof flags);
    flags |= PLACE_FLAG_SCANNED;
    memcpy(record + PLACE_FLAGS, &flags, sizeof flags);
    memcpy(record + PLACE_DESPAWN_RANGE, &zero, sizeof zero);
    memset(record + NPC_SPAWN_RECORD_REVEALS, 0,
           NPC_SPAWN_RECORD_ROUTE - NPC_SPAWN_RECORD_REVEALS);
    memcpy(record + NPC_SPAWN_RECORD_ROUTE, &one, sizeof one);
    memcpy(record + NPC_SPAWN_RECORD_ROUTE_NODE, position, 3u * sizeof(float));
    memcpy(record + NPC_SPAWN_RECORD_ROUTE_NODE + 3u * sizeof(float), &zero, sizeof zero);
    memcpy(&hit_points, record + PLACE_HIT_POINTS, sizeof hit_points);
    if (hit_points <= 0) {
        hit_points = PLACE_HIT_POINTS_LEAST;
        memcpy(record + PLACE_HIT_POINTS, &hit_points, sizeof hit_points);
    }
    if (shoots) {
        uint16_t weapon;
        float    reload;
        float    floor;

        memcpy(&weapon, record + PLACE_WEAPON_KINDS, sizeof weapon);
        if (weapon == 0) {
            weapon = PLACE_SHOT_KIND_BLASTER;
            memcpy(record + PLACE_WEAPON_KINDS, &weapon, sizeof weapon);
        }
        memcpy(&reload, record + PLACE_FIRE_INTERVAL, sizeof reload);
        floor = shot_kind_is_heavy(weapon) ? RELOAD_HEAVY : 0.0f;
        if (reload < floor) {
            memcpy(record + PLACE_FIRE_INTERVAL, &floor, sizeof floor);
        }
    }
}
