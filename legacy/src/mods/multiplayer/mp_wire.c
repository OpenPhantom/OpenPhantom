/* mp_wire.c: bytes onto the wire and off it again, with the two mistakes that are silent made loud.
 *
 * ==================================== The sticky failure ======================================
 *
 * Every put and every get returns a bool, and once one of them has failed the rest keep failing.
 * That is not defensive habit, it is the only way the caller's ordinary code shape stays correct.
 * A caller writes eight fields and checks once at the end; without stickiness the second field
 * failing and the third succeeding leaves a buffer that is neither the old packet nor the new one,
 * and the final check says yes.
 *
 * ================================ Why NaN is refused and not sent =============================
 *
 * A float has bit patterns that are not numbers, and they survive a round trip perfectly: encode
 * the bits, decode the bits, and the far side has a NaN. From there every sum, every comparison and
 * every interpolation involving that body produces another NaN, and the symptom appears somewhere
 * else entirely, usually as a body that has vanished or a camera that will not turn.
 *
 * The fixed point encoder cannot represent a NaN at all, so it has to do something with one. Doing
 * the arithmetic anyway gives an implementation defined integer, which is the worst of the options:
 * it looks like a position. So it is refused, at the encoder, where the caller still exists.
 *
 * ============================== The byte order is written out =================================
 *
 * Every multi byte value is assembled a byte at a time rather than copied from memory. The engine
 * only ever ran on one endianness and this file will too, and writing it out costs nothing and
 * removes the question.
 */
#include "mp_wire.h"

#include <math.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <string.h>

/* A NaN is the one value not equal to itself, and an infinity is what survives being halved. Both
 * tests are written out rather than taken from math.h, because the C runtime's isfinite is a macro
 * whose expansion differs between compilers and this has to behave identically everywhere. */
static bool is_finite_float(float value)
{
    if (value != value) {
        return false;                       /* NaN */
    }
    return !(value > 3.0e38f || value < -3.0e38f);
}

void mp_wire_writer_init(mp_wire_writer_t *writer, void *buffer, size_t size)
{
    writer->buffer     = (uint8_t *)buffer;
    writer->size       = (buffer != NULL) ? size : 0u;
    writer->at         = 0u;
    writer->overflowed = (buffer == NULL);
}

void mp_wire_reader_init(mp_wire_reader_t *reader, const void *buffer, size_t size)
{
    reader->buffer  = (const uint8_t *)buffer;
    reader->size    = (buffer != NULL) ? size : 0u;
    reader->at      = 0u;
    reader->overran = (buffer == NULL);
}

static bool room_for(mp_wire_writer_t *writer, size_t bytes)
{
    if (writer->overflowed) {
        return false;
    }
    if (writer->at + bytes > writer->size) {
        writer->overflowed = true;
        return false;
    }
    return true;
}

static bool left_for(mp_wire_reader_t *reader, size_t bytes)
{
    if (reader->overran) {
        return false;
    }
    if (reader->at + bytes > reader->size) {
        reader->overran = true;
        return false;
    }
    return true;
}

bool mp_wire_scale_is_sound(uint16_t scale)
{
    return scale == (uint16_t)MP_WIRE_SCALE_NONE ||
           (scale >= (uint16_t)MP_WIRE_SCALE_MIN && scale <= (uint16_t)MP_WIRE_SCALE_MAX);
}

bool mp_wire_key_is_placement(uint32_t key)
{
    return key < MP_WIRE_KEY_COPY_BASE;
}

bool mp_wire_key_is_copy(uint32_t key)
{
    return key >= MP_WIRE_KEY_COPY_BASE && key < MP_WIRE_KEY_COUNT;
}

bool mp_wire_put_u8(mp_wire_writer_t *writer, uint8_t value)
{
    if (!room_for(writer, 1u)) {
        return false;
    }
    writer->buffer[writer->at++] = value;
    return true;
}

bool mp_wire_put_u16(mp_wire_writer_t *writer, uint16_t value)
{
    if (!room_for(writer, 2u)) {
        return false;
    }
    writer->buffer[writer->at++] = (uint8_t)(value & 0xFFu);
    writer->buffer[writer->at++] = (uint8_t)((value >> 8) & 0xFFu);
    return true;
}

bool mp_wire_put_u32(mp_wire_writer_t *writer, uint32_t value)
{
    if (!room_for(writer, 4u)) {
        return false;
    }
    writer->buffer[writer->at++] = (uint8_t)(value & 0xFFu);
    writer->buffer[writer->at++] = (uint8_t)((value >> 8) & 0xFFu);
    writer->buffer[writer->at++] = (uint8_t)((value >> 16) & 0xFFu);
    writer->buffer[writer->at++] = (uint8_t)((value >> 24) & 0xFFu);
    return true;
}

bool mp_wire_get_u8(mp_wire_reader_t *reader, uint8_t *value)
{
    if (!left_for(reader, 1u)) {
        return false;
    }
    *value = reader->buffer[reader->at++];
    return true;
}

bool mp_wire_get_u16(mp_wire_reader_t *reader, uint16_t *value)
{
    if (!left_for(reader, 2u)) {
        return false;
    }
    *value = (uint16_t)((uint16_t)reader->buffer[reader->at] |
                        ((uint16_t)reader->buffer[reader->at + 1u] << 8));
    reader->at += 2u;
    return true;
}

bool mp_wire_get_u32(mp_wire_reader_t *reader, uint32_t *value)
{
    if (!left_for(reader, 4u)) {
        return false;
    }
    *value = (uint32_t)reader->buffer[reader->at] |
             ((uint32_t)reader->buffer[reader->at + 1u] << 8) |
             ((uint32_t)reader->buffer[reader->at + 2u] << 16) |
             ((uint32_t)reader->buffer[reader->at + 3u] << 24);
    reader->at += 4u;
    return true;
}

/* Fixed point, rounded rather than truncated: truncation biases every coordinate towards zero, and
 * a bias is the one kind of error that does not average out over a run of packets. */
static int32_t to_fixed(float value, float scale)
{
    float scaled = value * scale;

    return (int32_t)((scaled >= 0.0f) ? (scaled + 0.5f) : (scaled - 0.5f));
}

bool mp_wire_put_position(mp_wire_writer_t *writer, float value)
{
    int32_t fixed;

    if (!is_finite_float(value)) {
        writer->overflowed = true;   /* the caller's bug, and it must not go out */
        return false;
    }
    fixed = to_fixed(value, MP_WIRE_POSITION_SCALE);
    if (fixed > MP_WIRE_POSITION_LIMIT || fixed < -MP_WIRE_POSITION_LIMIT) {
        writer->overflowed = true;
        return false;
    }
    return mp_wire_put_u32(writer, (uint32_t)fixed);
}

/* The writer's range, held on the way in as well. The reader took any of the four bytes' values,
 * so a note could carry a place no writer puts on the wire, and a decoder that accepted it could
 * not say it again. Such a note is refused whole, by the sticky flag every decoder tests. */
bool mp_wire_get_position(mp_wire_reader_t *reader, float *value)
{
    uint32_t raw;
    int32_t  fixed;

    if (!mp_wire_get_u32(reader, &raw)) {
        return false;
    }
    fixed = (int32_t)raw;
    if (fixed > MP_WIRE_POSITION_LIMIT || fixed < -MP_WIRE_POSITION_LIMIT) {
        reader->overran = true;
        return false;
    }
    *value = (float)fixed / MP_WIRE_POSITION_SCALE;
    return true;
}

/* An angle wraps rather than being refused: every value of a whole turn is a legal angle, and a
 * caller handing over 730 degrees means the same thing as one handing over 10. */
bool mp_wire_put_angle(mp_wire_writer_t *writer, float degrees)
{
    float   wrapped;
    int32_t fixed;

    if (!is_finite_float(degrees)) {
        writer->overflowed = true;
        return false;
    }
    /* One remainder, not a subtraction loop: past 2^24 a float cannot step by 360 at all, and
     * the loop that used to sit here never returned for such a value. */
    wrapped = fmodf(degrees, 360.0f);
    if (wrapped < 0.0f) {
        wrapped += 360.0f;
    }
    fixed = to_fixed(wrapped, MP_WIRE_ANGLE_SCALE) & 0xFFFF;
    return mp_wire_put_u16(writer, (uint16_t)fixed);
}

bool mp_wire_get_angle(mp_wire_reader_t *reader, float *degrees)
{
    uint16_t raw;

    if (!mp_wire_get_u16(reader, &raw)) {
        return false;
    }
    *degrees = (float)raw / MP_WIRE_ANGLE_SCALE;
    return true;
}

/* The sign survives as two's complement in the sixteen bits, so the reader's cast back to int16_t
 * is what recovers it; both ends of this wire are the same compiler, which is what makes that
 * conversion of an out-of-range unsigned value dependable rather than merely usual. */
bool mp_wire_put_axis(mp_wire_writer_t *writer, float value)
{
    int32_t fixed;

    if (!is_finite_float(value)) {
        writer->overflowed = true;
        return false;
    }
    if (value > 1.0f) {
        value = 1.0f;
    } else if (value < -1.0f) {
        value = -1.0f;
    }
    fixed = to_fixed(value, MP_WIRE_AXIS_SCALE);
    return mp_wire_put_u16(writer, (uint16_t)(int16_t)fixed);
}

bool mp_wire_get_axis(mp_wire_reader_t *reader, float *value)
{
    uint16_t raw;

    if (!mp_wire_get_u16(reader, &raw)) {
        return false;
    }
    *value = (float)(int16_t)raw / MP_WIRE_AXIS_SCALE;
    return true;
}

/* The two flags share one byte. They are kept as two rather than folded into one because dead
 * is not the negation of alive: a body that has not spawned yet is neither, and a receiver that
 * infers one from the other would put such a body on the floor. Bit 0x04 once carried a shot
 * flag; it is reserved, written as zero and ignored on the way in, and so is 0x08. The high four
 * bits are the world the body was sent from. */
#define BODY_FLAG_ALIVE 0x01u
#define BODY_FLAG_DEAD  0x02u
#define BODY_WORLD_SHIFT 4u

uint8_t mp_wire_world_of(uint8_t generation)
{
    return (uint8_t)(generation & MP_WIRE_WORLD_MASK);
}

uint8_t mp_wire_clamp_health(int32_t health)
{
    if (health < 0) {
        return 0u;
    }
    if (health > 255) {
        return 255u;
    }
    return (uint8_t)health;
}

uint16_t mp_wire_track_from_frames(float frames)
{
    float scaled;

    if (!is_finite_float(frames) || frames <= 0.0f) {
        return 0u;
    }
    scaled = frames * MP_WIRE_TRACK_SCALE + 0.5f;
    if (scaled >= (float)MP_WIRE_TRACK_MAX) {
        return (uint16_t)MP_WIRE_TRACK_MAX;
    }
    return (uint16_t)scaled;
}

bool mp_wire_put_body(mp_wire_writer_t *writer, const mp_wire_body_t *body)
{
    uint8_t flags = 0u;
    size_t  index;

    /* A world the four bits cannot hold is the caller's error, and it must not go out as another
     * world. */
    if (body == NULL || body->world > MP_WIRE_WORLD_MASK) {
        writer->overflowed = true;
        return false;
    }

    flags = (uint8_t)((body->alive ? BODY_FLAG_ALIVE : 0u) |
                      (body->dead  ? BODY_FLAG_DEAD  : 0u) |
                      (uint8_t)(body->world << BODY_WORLD_SHIFT));

    for (index = 0; index < 3u; ++index) {
        (void)mp_wire_put_position(writer, body->position[index]);
    }
    for (index = 0; index < 3u; ++index) {
        (void)mp_wire_put_angle(writer, body->orientation[index]);
    }
    (void)mp_wire_put_u8(writer, flags);
    (void)mp_wire_put_u8(writer, body->weapon);
    (void)mp_wire_put_u8(writer, body->hero);
    (void)mp_wire_put_u8(writer, body->health);
    (void)mp_wire_put_u8(writer, body->loco);
    for (index = 0; index < 2u; ++index) {
        (void)mp_wire_put_u16(writer, body->anim.clip[index]);
        (void)mp_wire_put_u16(writer, body->anim.track[index]);
    }
    (void)mp_wire_put_u32(writer, body->anim.channel_mask);

    /* The twists: a count, then node, pitch and yaw each. A count past the cap is the caller's
     * error and is cut to the cap rather than refused, so a body still crosses. */
    {
        uint8_t count = body->twist_count > MP_WIRE_MAX_TWISTS ? (uint8_t)MP_WIRE_MAX_TWISTS
                                                                : body->twist_count;

        (void)mp_wire_put_u8(writer, count);
        for (index = 0; index < count; ++index) {
            (void)mp_wire_put_u8(writer, body->twist[index].node);
            (void)mp_wire_put_angle(writer, body->twist[index].pitch);
            (void)mp_wire_put_angle(writer, body->twist[index].yaw);
        }
    }

    /* One check at the end, which is what the stickiness above is for. */
    return !writer->overflowed;
}

/* A twist angle is signed on both ends and rides the wire's unsigned wrap in between. */
static float signed_angle(float degrees)
{
    return degrees > 180.0f ? degrees - 360.0f : degrees;
}

bool mp_wire_get_body(mp_wire_reader_t *reader, mp_wire_body_t *body)
{
    uint8_t flags = 0u;
    size_t  index;

    if (body == NULL) {
        reader->overran = true;
        return false;
    }
    memset(body, 0, sizeof *body);

    for (index = 0; index < 3u; ++index) {
        (void)mp_wire_get_position(reader, &body->position[index]);
    }
    for (index = 0; index < 3u; ++index) {
        (void)mp_wire_get_angle(reader, &body->orientation[index]);
    }
    (void)mp_wire_get_u8(reader, &flags);
    (void)mp_wire_get_u8(reader, &body->weapon);
    (void)mp_wire_get_u8(reader, &body->hero);
    (void)mp_wire_get_u8(reader, &body->health);
    (void)mp_wire_get_u8(reader, &body->loco);
    for (index = 0; index < 2u; ++index) {
        (void)mp_wire_get_u16(reader, &body->anim.clip[index]);
        (void)mp_wire_get_u16(reader, &body->anim.track[index]);
    }
    (void)mp_wire_get_u32(reader, &body->anim.channel_mask);

    (void)mp_wire_get_u8(reader, &body->twist_count);
    if (body->twist_count > MP_WIRE_MAX_TWISTS) {
        reader->overran = true;   /* a count the record cannot hold is a torn record */
        body->twist_count = 0;
    }
    for (index = 0; index < body->twist_count; ++index) {
        (void)mp_wire_get_u8(reader, &body->twist[index].node);
        (void)mp_wire_get_angle(reader, &body->twist[index].pitch);
        (void)mp_wire_get_angle(reader, &body->twist[index].yaw);
        body->twist[index].pitch = signed_angle(body->twist[index].pitch);
        body->twist[index].yaw   = signed_angle(body->twist[index].yaw);
    }

    body->alive = (flags & BODY_FLAG_ALIVE) != 0u;
    body->dead  = (flags & BODY_FLAG_DEAD) != 0u;
    body->world = (uint8_t)(flags >> BODY_WORLD_SHIFT);

    return !reader->overran;
}
