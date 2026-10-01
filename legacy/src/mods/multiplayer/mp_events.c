/* mp_events.c: the event codec and ring, and the body frame both ends of a shot use. Pure. */
#include "mp_events.h"

#include "mp_snapshot.h"
#include "mp_wire.h"

#include <math.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <string.h>

void mp_event_queue_init(mp_event_queue_t *queue)
{
    memset(queue, 0, sizeof *queue);
}

void mp_event_queue_push(mp_event_queue_t *queue, const mp_event_t *event)
{
    size_t tail;

    if (queue->count == MP_EVENT_QUEUE_SLOTS) {
        /* Drop the oldest: a stale event performed late is worse than one missed and counted. */
        queue->head = (queue->head + 1u) % MP_EVENT_QUEUE_SLOTS;
        --queue->count;
        ++queue->dropped;
    }
    tail = (queue->head + queue->count) % MP_EVENT_QUEUE_SLOTS;
    queue->items[tail] = *event;
    ++queue->count;
}

bool mp_event_queue_peek(const mp_event_queue_t *queue, mp_event_t *out)
{
    if (queue->count == 0u) {
        return false;
    }
    *out = queue->items[queue->head];
    return true;
}

bool mp_event_queue_pop(mp_event_queue_t *queue, mp_event_t *out)
{
    if (!mp_event_queue_peek(queue, out)) {
        return false;
    }
    queue->head = (queue->head + 1u) % MP_EVENT_QUEUE_SLOTS;
    --queue->count;
    return true;
}

/* A signed angle rides the wire's unsigned wrap: -30 goes out as 330 and comes back as -30. */
static float signed_angle(float degrees)
{
    return degrees > 180.0f ? degrees - 360.0f : degrees;
}

static uint8_t charge_byte(float charge)
{
    if (!(charge >= 0.0f)) {   /* also catches NaN */
        charge = 0.0f;
    }
    if (charge > 1.0f) {
        charge = 1.0f;
    }
    return (uint8_t)(charge * 255.0f + 0.5f);
}

/* Whether an asset name is one this build could have produced.
 *
 * This is not tidiness, it is the difference between a refused message and a closed game. The
 * receiver of one of these hands the name to the engine's resource loader, and that loader answers
 * a miss with a silent zero; the bind that follows asserts, and this build's assert handler is a
 * message box followed by exit. An unknown name is therefore a PROGRAM END rather than a failed
 * swap, and a stranger who can put bytes on this channel would otherwise be able to close the game
 * by naming an actor that does not exist.
 *
 * Two rules are applied, and only one of them is this file's own.
 *
 * The ENGINE'S rule is the 8.3 gate every resource request passes before a single file is touched:
 * at most one dot, at most eight characters before it, at most three after it, and every other
 * character out of the fixed 63 character alphabet above. The loader at 00472040 reduces the name
 * to its basename, runs the gate at 00474ED2, and only then looks in the resident list; the gate
 * answers no by returning nothing at all, so a name that fails it can never name a resource
 * however well the file would otherwise load. The alphabet was read out of the shipped image at
 * 004B79B8, file offset 0xB63BC, 63 bytes then a NUL, rather than taken on trust. Refusing such a
 * name here rather than downstream is free, and it means the layer that applies one of these is
 * never handed a name the engine would have thrown away anyway. The assert the miss reaches ends
 * in the handler at 00495FCC, a message box and then exit(1).
 *
 * Note that the alphabet has no hyphen. That reads like an oversight and is not: names carrying
 * one do exist in this game's data, but they are clip names inside a .baf, and a clip name never
 * reaches the resource gate. This codec used to allow the hyphen and was therefore LOOSER than the
 * engine, which is the wrong direction for a gate whose whole job is to refuse.
 *
 * Note also that a name with no dot at all passes the engine's gate at any length, because the
 * eight character limit is only tested when a dot is found. That is copied faithfully rather than
 * tightened: a rule stricter than the engine's would refuse a name the engine would have taken.
 * The field's own 32 bytes are the only length limit such a name meets, and a name whose only
 * dot is its first character passes with an empty stem for the same reason.
 *
 * The check used to accept the hyphen and up to 31 characters of an alphabet of its own, looser
 * than the engine in exactly the direction a gate must not be. The two implementations were then
 * run side by side over 299843 generated names, every string up to six characters over an
 * alphabet covering each class the rule can tell apart plus every dot position in every length
 * up to twenty, with zero disagreements outside the two departures that are this codec's own: an
 * empty name is refused here and the gate takes it, and a name longer than the field is refused
 * here where the gate never length checks a dotless name.
 *
 * THIS FILE'S rule is the encoding: terminated inside the field, not empty, and zero after the
 * terminator, so that one name has exactly one encoding and the padding cannot carry anything.
 * The same gate exists a second time in the roster codec, on purpose: the two are pure modules
 * that link independently, several test binaries link one without the other, and both unit
 * tests drive one table of border cases so a change to one and not the other fails.
 *
 * Whether the actor EXISTS is a question only the resource layer can answer, and the layer that
 * applies one of these must ask it before it acts. */
static bool asset_char_is_allowed(char c)
{
    static const char ALPHABET[] = MP_EVENT_ASSET_ALPHABET;
    size_t            i;

    for (i = 0; i < sizeof ALPHABET - 1u; ++i) {
        if (ALPHABET[i] == c) {
            return true;
        }
    }
    return false;
}

static bool asset_name_is_sound(const char *name)
{
    size_t at;
    size_t dot_at = 0;
    bool   dotted = false;
    bool   terminated = false;

    for (at = 0; at < MP_EVENT_ASSET_MAX; ++at) {
        char c = name[at];

        if (c == '\0') {
            terminated = true;
            break;
        }
        if (c == '.') {
            if (dotted || at > MP_EVENT_ASSET_STEM_MAX) {
                return false;   /* a second dot, or a stem the engine's gate would refuse */
            }
            dotted = true;
            dot_at = at;
        } else if (!asset_char_is_allowed(c)) {
            return false;
        }
    }
    if (!terminated || at == 0u) {
        return false;   /* unterminated, or empty, and an empty name reaches the same assert */
    }
    if (dotted && at - dot_at - 1u > MP_EVENT_ASSET_EXTENSION_MAX) {
        return false;   /* an extension longer than three */
    }
    for (++at; at < MP_EVENT_ASSET_MAX; ++at) {
        if (name[at] != '\0') {
            return false;   /* padding that carries something is not this build's encoding */
        }
    }
    return true;
}

/* Whether a pickup kind is one the shipped data actually places.
 *
 * The original path, for the record: the cylinder contact reaches the player's contact handler,
 * which sets bit 3 in the PICKUP body's flag word and sends the pickup's task a message; the
 * actor's handler sets its removal reason and its hit points to zero, and the enemy tick then
 * deletes it with reason 1. The bit lives in the pickup body and not in the story bank, so the
 * per hero exclusion that governs the campaign bank does not touch it; and on a client that has
 * handed the level to the host that tick does not run, so a taken pickup would stand for ever.
 * The reconstruction had the other message global as the sender of that message; it is the
 * pickup's own body.
 *
 * The band is the player contact handler's own dispatch range. The two holes in it are the point:
 * no placement in any shipped level carries them, and the handler's arm for them reaches a stack
 * defect, so the retail game can never arrive there. A message off the wire could, and passing one
 * on would make a defect nobody can reach into one anybody can send. */
static bool pickup_kind_is_placed(uint8_t kind)
{
    if (kind < MP_PICKUP_KIND_MIN || kind > MP_PICKUP_KIND_MAX) {
        return false;
    }
    return kind != MP_PICKUP_KIND_UNPLACED_LO && kind != MP_PICKUP_KIND_UNPLACED_HI;
}

/* The five the engine's own six call sites pass, and nothing else. A reason outside them would be
 * handed to a removal that switches on it, so it is refused on both sides rather than passed on. */
static bool remove_reason_is_known(uint8_t reason)
{
    return reason == MP_EVENT_REMOVE_OUT_OF_RANGE || reason == MP_EVENT_REMOVE_DELETED ||
           reason == MP_EVENT_REMOVE_HOST_RELEASE || reason == MP_EVENT_REMOVE_LEVEL_END ||
           reason == MP_EVENT_REMOVE_LEAVE_CORPSE;
}

/* The messages that name a world slot behind the tick: the moments of a body, whose player did
 * them, and the pickup claim, whose claimant it is. A mover is the map's. */
static bool carries_source(uint8_t kind)
{
    return kind == MP_EVENT_SHOT || kind == MP_EVENT_PUSH || kind == MP_EVENT_SABRE ||
           kind == MP_EVENT_WEAPON || kind == MP_EVENT_PLAYER_SOUND || kind == MP_EVENT_PICKUP;
}

/* What a host passes on to its other players: a body's moments and a mover. */
static bool is_moment(uint8_t kind)
{
    return kind == MP_EVENT_SHOT || kind == MP_EVENT_PUSH || kind == MP_EVENT_SABRE ||
           kind == MP_EVENT_WEAPON || kind == MP_EVENT_PLAYER_SOUND || kind == MP_EVENT_MOVER;
}

/* The eight things a player's sound can be. A byte past them names no table on the far side. */
static bool player_sound_is_known(uint8_t what)
{
    return what <= MP_PLAYER_SOUND_KIND_MAX;
}

size_t mp_event_encode(const mp_event_t *event, uint8_t *buffer, size_t capacity)
{
    mp_wire_writer_t w;
    int              axis;

    if (event == NULL || buffer == NULL) {
        return 0u;
    }
    if (carries_source(event->kind) && event->source_slot >= MP_SNAPSHOT_MAX_BODIES) {
        return 0u;   /* the receiver picks a far bank by it, and past the table there is none */
    }
    mp_wire_writer_init(&w, buffer, capacity);
    switch (event->kind) {
    case MP_EVENT_SHOT:
        mp_wire_put_u8(&w, MP_EVENT_SHOT);
        mp_wire_put_u32(&w, event->tick);
        mp_wire_put_u8(&w, event->source_slot);
        mp_wire_put_u8(&w, event->shot_kind);
        for (axis = 0; axis < 3; ++axis) {
            mp_wire_put_position(&w, event->origin[axis]);
        }
        mp_wire_put_angle(&w, event->pitch);
        mp_wire_put_angle(&w, event->yaw);
        break;
    case MP_EVENT_PUSH:
        mp_wire_put_u8(&w, MP_EVENT_PUSH);
        mp_wire_put_u32(&w, event->tick);
        mp_wire_put_u8(&w, event->source_slot);
        mp_wire_put_u8(&w, charge_byte(event->charge));
        break;
    case MP_EVENT_SABRE:
        if (event->action > MP_SABRE_ACTION_MAX) {
            return 0u;
        }
        mp_wire_put_u8(&w, MP_EVENT_SABRE);
        mp_wire_put_u32(&w, event->tick);
        mp_wire_put_u8(&w, event->source_slot);
        mp_wire_put_u8(&w, event->action);
        mp_wire_put_u8(&w, event->operand);
        break;
    case MP_EVENT_WEAPON:
        if (event->weapon_slot >= MP_EVENT_WEAPON_SLOTS) {
            return 0u;
        }
        mp_wire_put_u8(&w, MP_EVENT_WEAPON);
        mp_wire_put_u32(&w, event->tick);
        mp_wire_put_u8(&w, event->source_slot);
        mp_wire_put_u8(&w, event->weapon_slot);
        break;
    case MP_EVENT_PLAYER_SOUND:
        if (!player_sound_is_known(event->sound_what)) {
            return 0u;
        }
        mp_wire_put_u8(&w, MP_EVENT_PLAYER_SOUND);
        mp_wire_put_u32(&w, event->tick);
        mp_wire_put_u8(&w, event->source_slot);
        mp_wire_put_u8(&w, event->sound_what);
        mp_wire_put_u8(&w, event->sound_index);
        mp_wire_put_u8(&w, event->sound_flags);
        break;
    case MP_EVENT_MOVER:
        if (event->mover_mode > MP_EVENT_MOVER_OPEN) {
            return 0u;
        }
        mp_wire_put_u8(&w, MP_EVENT_MOVER);
        mp_wire_put_u32(&w, event->tick);
        mp_wire_put_u16(&w, event->mover_id);
        mp_wire_put_u8(&w, event->mover_mode);
        break;
    case MP_EVENT_SPAWN:
        if (!mp_wire_key_is_placement(event->actor_index)) {
            return 0u;   /* a copy is not the level's to wake */
        }
        mp_wire_put_u8(&w, MP_EVENT_SPAWN);
        mp_wire_put_u32(&w, event->tick);
        mp_wire_put_u16(&w, event->level_id);
        mp_wire_put_u8(&w, (uint8_t)event->actor_index);
        mp_wire_put_u8(&w, event->actor_generation);
        mp_wire_put_u16(&w, event->actor_script);
        break;
    case MP_EVENT_SKIN: {
        size_t i;

        if (event->skin_kind > MP_SKIN_KIND_MAX || event->skin_hero >= MP_EVENT_HERO_SLOTS ||
            event->skin_slot >= MP_SNAPSHOT_MAX_BODIES ||
            !mp_wire_scale_is_sound(event->skin_scale) ||
            !asset_name_is_sound(event->skin_asset)) {
            return 0u;
        }
        mp_wire_put_u8(&w, MP_EVENT_SKIN);
        mp_wire_put_u32(&w, event->tick);
        mp_wire_put_u8(&w, event->skin_kind);
        mp_wire_put_u8(&w, event->skin_hero);
        mp_wire_put_u8(&w, event->skin_slot);
        mp_wire_put_u16(&w, event->skin_scale);
        for (i = 0; i < MP_EVENT_ASSET_MAX; ++i) {
            mp_wire_put_u8(&w, (uint8_t)event->skin_asset[i]);
        }
        break;
    }
    case MP_EVENT_PICKUP:
        if (!pickup_kind_is_placed(event->pickup_kind) ||
            !mp_wire_key_is_placement(event->actor_index)) {
            return 0u;   /* a copy is never a pickup: its class is one of a character */
        }
        mp_wire_put_u8(&w, MP_EVENT_PICKUP);
        mp_wire_put_u32(&w, event->tick);
        mp_wire_put_u8(&w, event->source_slot);
        mp_wire_put_u16(&w, event->level_id);
        mp_wire_put_u8(&w, (uint8_t)event->actor_index);
        mp_wire_put_u8(&w, event->actor_generation);
        mp_wire_put_u8(&w, event->pickup_kind);
        break;
    case MP_EVENT_DESPAWN:
        if (!remove_reason_is_known(event->actor_reason) ||
            (!mp_wire_key_is_placement(event->actor_index) &&
             !mp_wire_key_is_copy(event->actor_index))) {
            return 0u;
        }
        mp_wire_put_u8(&w, MP_EVENT_DESPAWN);
        mp_wire_put_u32(&w, event->tick);
        mp_wire_put_u16(&w, event->level_id);
        mp_wire_put_u16(&w, event->actor_index);
        mp_wire_put_u8(&w, event->actor_generation);
        mp_wire_put_u8(&w, event->actor_reason);
        mp_wire_put_u8(&w, event->actor_burst);
        break;
    default:
        return 0u;
    }
    return w.overflowed ? 0u : w.at;
}

bool mp_event_is_event(const uint8_t *buffer, size_t bytes)
{
    if (buffer == NULL || bytes == 0u) {
        return false;
    }
    return (bytes == MP_EVENT_SHOT_BYTES && buffer[0] == MP_EVENT_SHOT) ||
           (bytes == MP_EVENT_PUSH_BYTES && buffer[0] == MP_EVENT_PUSH) ||
           (bytes == MP_EVENT_SABRE_BYTES && buffer[0] == MP_EVENT_SABRE) ||
           (bytes == MP_EVENT_WEAPON_BYTES && buffer[0] == MP_EVENT_WEAPON) ||
           (bytes == MP_EVENT_MOVER_BYTES && buffer[0] == MP_EVENT_MOVER) ||
           (bytes == MP_EVENT_SPAWN_BYTES && buffer[0] == MP_EVENT_SPAWN) ||
           (bytes == MP_EVENT_DESPAWN_BYTES && buffer[0] == MP_EVENT_DESPAWN) ||
           (bytes == MP_EVENT_SKIN_BYTES && buffer[0] == MP_EVENT_SKIN) ||
           (bytes == MP_EVENT_PICKUP_BYTES && buffer[0] == MP_EVENT_PICKUP) ||
           (bytes == MP_EVENT_PLAYER_SOUND_BYTES && buffer[0] == MP_EVENT_PLAYER_SOUND);
}

bool mp_event_decode(const uint8_t *buffer, size_t bytes, mp_event_t *out)
{
    mp_wire_reader_t r;
    uint8_t          tag = 0;
    uint8_t          byte = 0;
    int              axis;

    if (out == NULL || !mp_event_is_event(buffer, bytes)) {
        return false;
    }
    memset(out, 0, sizeof *out);
    mp_wire_reader_init(&r, buffer, bytes);
    mp_wire_get_u8(&r, &tag);
    mp_wire_get_u32(&r, &out->tick);
    out->kind = tag;
    if (carries_source(tag)) {
        mp_wire_get_u8(&r, &out->source_slot);
        if (out->source_slot >= MP_SNAPSHOT_MAX_BODIES) {
            return false;   /* it picks a far bank, and a slot past the table picks none */
        }
    }
    if (tag == MP_EVENT_SHOT) {
        mp_wire_get_u8(&r, &out->shot_kind);
        for (axis = 0; axis < 3; ++axis) {
            mp_wire_get_position(&r, &out->origin[axis]);
        }
        mp_wire_get_angle(&r, &out->pitch);
        mp_wire_get_angle(&r, &out->yaw);
        out->pitch = signed_angle(out->pitch);
    } else if (tag == MP_EVENT_PUSH) {
        mp_wire_get_u8(&r, &byte);
        out->charge = (float)byte / 255.0f;
    } else if (tag == MP_EVENT_WEAPON) {
        mp_wire_get_u8(&r, &out->weapon_slot);
        if (out->weapon_slot >= MP_EVENT_WEAPON_SLOTS) {
            return false;   /* the setter indexes an ammo table with it; a torn slot is refused */
        }
    } else if (tag == MP_EVENT_PLAYER_SOUND) {
        mp_wire_get_u8(&r, &out->sound_what);
        mp_wire_get_u8(&r, &out->sound_index);
        mp_wire_get_u8(&r, &out->sound_flags);
        if (!player_sound_is_known(out->sound_what)) {
            return false;   /* it picks the far side's table, and a torn one picks none */
        }
    } else if (tag == MP_EVENT_MOVER) {
        mp_wire_get_u16(&r, &out->mover_id);
        mp_wire_get_u8(&r, &out->mover_mode);
        if (out->mover_mode > MP_EVENT_MOVER_OPEN) {
            return false;   /* the engine has two openers and no third; a torn mode is refused */
        }
    } else if (tag == MP_EVENT_SPAWN) {
        uint8_t index = 0;

        mp_wire_get_u16(&r, &out->level_id);
        mp_wire_get_u8(&r, &index);
        out->actor_index = index;
        mp_wire_get_u8(&r, &out->actor_generation);
        mp_wire_get_u16(&r, &out->actor_script);
    } else if (tag == MP_EVENT_SKIN) {
        size_t i;

        mp_wire_get_u8(&r, &out->skin_kind);
        mp_wire_get_u8(&r, &out->skin_hero);
        mp_wire_get_u8(&r, &out->skin_slot);
        mp_wire_get_u16(&r, &out->skin_scale);
        for (i = 0; i < MP_EVENT_ASSET_MAX; ++i) {
            uint8_t byte_in = 0;

            mp_wire_get_u8(&r, &byte_in);
            out->skin_asset[i] = (char)byte_in;
        }
        if (out->skin_kind > MP_SKIN_KIND_MAX || out->skin_hero >= MP_EVENT_HERO_SLOTS ||
            out->skin_slot >= MP_SNAPSHOT_MAX_BODIES ||
            !mp_wire_scale_is_sound(out->skin_scale) ||
            !asset_name_is_sound(out->skin_asset)) {
            /* The name: see asset_name_is_sound, an unknown one ends the program. The hero slot:
             * the layer that applies this indexes a four entry name table with it. The world slot:
             * it selects a body out of a fixed table, and one past the end selects nothing that
             * exists. */
            return false;
        }
    } else if (tag == MP_EVENT_PICKUP) {
        uint8_t index = 0;

        mp_wire_get_u16(&r, &out->level_id);
        mp_wire_get_u8(&r, &index);
        out->actor_index = index;
        mp_wire_get_u8(&r, &out->actor_generation);
        mp_wire_get_u8(&r, &out->pickup_kind);
        if (!pickup_kind_is_placed(out->pickup_kind)) {
            return false;   /* see pickup_kind_is_placed: two of them reach a stack defect */
        }
    } else if (tag == MP_EVENT_DESPAWN) {
        mp_wire_get_u16(&r, &out->level_id);
        mp_wire_get_u16(&r, &out->actor_index);
        mp_wire_get_u8(&r, &out->actor_generation);
        mp_wire_get_u8(&r, &out->actor_reason);
        mp_wire_get_u8(&r, &out->actor_burst);
        if (!mp_wire_key_is_placement(out->actor_index) &&
            !mp_wire_key_is_copy(out->actor_index)) {
            return false;   /* a key that names no enemy is torn */
        }
        if (!remove_reason_is_known(out->actor_reason)) {
            /* A removal switches on it; a reason the engine has no arm for is torn. */
            return false;
        }
    } else {
        mp_wire_get_u8(&r, &out->action);
        mp_wire_get_u8(&r, &out->operand);
        if (out->action > MP_SABRE_ACTION_MAX) {
            return false;   /* an action the far side has no starter for is a torn message */
        }
    }
    return !r.overran;
}

bool mp_event_restamp(uint8_t *note, size_t bytes, uint8_t source_slot, uint32_t tick)
{
    mp_event_t       event;
    mp_wire_writer_t w;

    if (source_slot >= MP_SNAPSHOT_MAX_BODIES || !mp_event_decode(note, bytes, &event) ||
        !is_moment(event.kind)) {
        return false;
    }
    mp_wire_writer_init(&w, note + 1u, bytes - 1u);
    mp_wire_put_u32(&w, tick);
    if (carries_source(event.kind)) {
        mp_wire_put_u8(&w, source_slot);
    }
    return !w.overflowed;
}

/* Degrees in, because the engine works in degrees and every angle on the wire is one. */
static void sincos_deg(float degrees, float *s, float *c)
{
    double radians = (double)degrees * (3.14159265358979323846 / 180.0);

    *s = (float)sin(radians);
    *c = (float)cos(radians);
}

/* The engine's own order of products: the matrix its pose builder forms from (pitch, yaw, roll),
 * written as the three columns it multiplies a local vector's x, y and z by. */
void mp_event_body_matrix(float pitch, float yaw, float roll, float matrix[9])
{
    float sx, cx, sy, cy, sz, cz;

    sincos_deg(pitch, &sx, &cx);
    sincos_deg(yaw, &sy, &cy);
    sincos_deg(roll, &sz, &cz);

    matrix[0] = -sy * sz * sx + cy * cz;
    matrix[1] =  cy * sz * sx + sy * cz;
    matrix[2] = -sz * cx;

    matrix[3] = -sy * cx;
    matrix[4] =  cy * cx;
    matrix[5] =  sx;

    matrix[6] =  sy * cz * sx + cy * sz;
    matrix[7] = -sx * cy * cz + sy * sz;
    matrix[8] =  cz * cx;
}

/* The columns are orthonormal, so the inverse is the transpose: each local axis is the dot of
 * the world delta with that column. */
void mp_event_to_local(const float matrix[9], const float world_delta[3], float local[3])
{
    int axis;

    for (axis = 0; axis < 3; ++axis) {
        const float *column = &matrix[axis * 3];

        local[axis] = column[0] * world_delta[0] + column[1] * world_delta[1] +
                      column[2] * world_delta[2];
    }
}

void mp_event_to_world(const float matrix[9], const float local[3], float world_delta[3])
{
    int axis;

    for (axis = 0; axis < 3; ++axis) {
        world_delta[axis] = matrix[axis] * local[0] + matrix[3 + axis] * local[1] +
                            matrix[6 + axis] * local[2];
    }
}

float mp_event_wrap360(float degrees)
{
    float wrapped = (float)fmod((double)degrees, 360.0);

    if (wrapped < 0.0f) {
        wrapped += 360.0f;
    }
    return wrapped;
}

/* A model is worn over the hero's own body, so its clips, its weapons and the ordinals the wire
 * names stay that body's. Built out of the model's own file instead, a far body had that file's
 * clip table, sixteen for anakin.baf, and the first weapon change the far player made reached an
 * overlay clip past it: the engine's assert, and the host was gone.
 *
 * The kept actor is what makes a model change cost nothing: the body the bank already stands on
 * is the right one, and building it again as "the hero's own" would take it down and put the
 * same body back, twice for every swap and its way home. A kind this build does not know is
 * answered like a model with nothing kept, because a name nobody said was an actor is not
 * built as one. */
const char *mp_skin_body_actor(uint8_t kind, const char *name, uint8_t hero, const char *kept,
                               uint8_t kept_hero)
{
    if (kind == (uint8_t)MP_SKIN_CHARACTER) {
        return name != NULL ? name : "";
    }
    if (kind == (uint8_t)MP_SKIN_MODEL && kept != NULL && kept_hero == hero) {
        return kept;
    }
    return "";
}
