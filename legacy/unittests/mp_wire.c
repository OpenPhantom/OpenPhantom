/* mp_wire.c: the packet encoding, driven over its edges rather than its middle.
 *
 * Everything interesting about an encoder is at its boundaries. A round trip of a plausible
 * position proves almost nothing; what has to hold is that the buffer's last byte is usable and its
 * next one is not, that a value which cannot be represented is refused instead of wrapped, and that
 * a writer which has already failed does not start succeeding again halfway through a record.
 *
 * The last one is the property the whole calling style rests on. Every caller writes a run of
 * fields and checks once at the end, so a failure that does not stick produces a buffer holding
 * neither the old packet nor the new one, with a final check that says yes.
 */
#include "unittest.h"

#include "mp_wire.h"

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <string.h>

/* A NaN and an infinity built by arithmetic rather than by a header macro, so the test does not
 * depend on how this compiler spells them. The volatile keeps the optimiser from folding the
 * division at compile time, where dividing by zero is not allowed to happen. */
static float make_nan(void)
{
    volatile float zero = 0.0f;
    return zero / zero;
}

static float make_inf(void)
{
    volatile float zero = 0.0f;
    return 1.0f / zero;
}

static void check_integers(void)
{
    uint8_t          buffer[16];
    mp_wire_writer_t writer;
    mp_wire_reader_t reader;
    uint8_t          eight = 0;
    uint16_t         sixteen = 0;
    uint32_t         thirtytwo = 0;

    ut_section("integers, at their extremes");

    mp_wire_writer_init(&writer, buffer, sizeof buffer);
    ut_check(mp_wire_put_u8(&writer, 0xFFu), "a byte of all ones goes in");
    ut_check(mp_wire_put_u16(&writer, 0xFFFFu), "so does a word");
    ut_check(mp_wire_put_u32(&writer, 0xFFFFFFFFu), "and a dword");
    ut_check(mp_wire_put_u8(&writer, 0u), "and a zero");

    mp_wire_reader_init(&reader, buffer, writer.at);
    ut_check(mp_wire_get_u8(&reader, &eight) && eight == 0xFFu, "the byte comes back");
    ut_check(mp_wire_get_u16(&reader, &sixteen) && sixteen == 0xFFFFu, "the word comes back");
    ut_check(mp_wire_get_u32(&reader, &thirtytwo) && thirtytwo == 0xFFFFFFFFu,
             "the dword comes back");
    ut_check(mp_wire_get_u8(&reader, &eight) && eight == 0u, "and the zero");

    ut_check(!mp_wire_get_u8(&reader, &eight), "one byte past the end is refused");
    ut_check(reader.overran, "and the reader stays overrun");

    ut_section("the byte order is the protocol's");

    mp_wire_writer_init(&writer, buffer, sizeof buffer);
    (void)mp_wire_put_u32(&writer, 0x11223344u);
    ut_check(buffer[0] == 0x44u && buffer[1] == 0x33u && buffer[2] == 0x22u && buffer[3] == 0x11u,
             "a dword is least significant byte first, whatever the host does");
}

static void check_the_last_byte(void)
{
    uint8_t          buffer[4];
    mp_wire_writer_t writer;

    ut_section("the buffer's last byte is usable and its next one is not");

    mp_wire_writer_init(&writer, buffer, 4u);
    ut_check(mp_wire_put_u32(&writer, 0x12345678u), "a dword fits a four byte buffer exactly");
    ut_check(!writer.overflowed, "and does not overflow it");
    ut_check(!mp_wire_put_u8(&writer, 1u), "one more byte does not fit");
    ut_check(writer.overflowed, "and the writer says so");

    /* Cleared first, or the check reads what the previous case left in the same stack buffer and
     * fails for a reason that has nothing to do with the encoder. */
    memset(buffer, 0, sizeof buffer);
    mp_wire_writer_init(&writer, buffer, 3u);
    ut_check(!mp_wire_put_u32(&writer, 0x12345678u), "a dword does not fit three bytes");
    ut_check(buffer[0] == 0u && buffer[1] == 0u && buffer[2] == 0u,
             "and nothing of it was written, rather than as much as fitted");

    ut_section("a failure sticks");

    mp_wire_writer_init(&writer, buffer, 2u);
    ut_check(!mp_wire_put_u32(&writer, 1u), "the dword is refused");
    ut_check(!mp_wire_put_u8(&writer, 1u),
             "and a byte that WOULD have fitted is refused too, because a caller writes a run of "
             "fields and checks once at the end");
}

static void check_positions(void)
{
    uint8_t          buffer[64];
    mp_wire_writer_t writer;
    mp_wire_reader_t reader;
    static const float CASES[] = { 0.0f, 1.0f, -1.0f, 0.5f, -0.5f, 1023.75f, -1023.75f,
                                   0.00390625f, 32767.5f, -32767.5f };
    size_t index;
    float  back = 0.0f;

    ut_section("positions round trip inside their stated error");

    for (index = 0; index < sizeof CASES / sizeof CASES[0]; ++index) {
        mp_wire_writer_init(&writer, buffer, sizeof buffer);
        ut_checkf(mp_wire_put_position(&writer, CASES[index]), "%f goes out", (double)CASES[index]);
        mp_wire_reader_init(&reader, buffer, writer.at);
        ut_check(mp_wire_get_position(&reader, &back), "and comes back");
        ut_near(back, CASES[index], MP_WIRE_POSITION_ERROR, "within half a step of the scale");
    }

    ut_section("a position that cannot be represented is refused, not wrapped");

    mp_wire_writer_init(&writer, buffer, sizeof buffer);
    ut_check(!mp_wire_put_position(&writer, 1.0e9f), "far outside the range is refused");
    ut_check(writer.overflowed,
             "and the writer is failed, so the rest of the record cannot go out");

    mp_wire_writer_init(&writer, buffer, sizeof buffer);
    ut_check(!mp_wire_put_position(&writer, -1.0e9f), "and so is the other direction");

    ut_section("a position no writer puts on the wire is refused on the way in");

    mp_wire_writer_init(&writer, buffer, sizeof buffer);
    (void)mp_wire_put_u32(&writer, (uint32_t)MP_WIRE_POSITION_LIMIT);
    (void)mp_wire_put_u32(&writer, (uint32_t)(MP_WIRE_POSITION_LIMIT + 1));
    (void)mp_wire_put_u32(&writer, (uint32_t)(-(int32_t)MP_WIRE_POSITION_LIMIT - 1));
    mp_wire_reader_init(&reader, buffer, writer.at);
    ut_check(mp_wire_get_position(&reader, &back) &&
                 back == (float)MP_WIRE_POSITION_LIMIT / MP_WIRE_POSITION_SCALE,
             "the writer's last value reads back");
    ut_check(!mp_wire_get_position(&reader, &back) && reader.overran,
             "one past it is refused, and the reader is failed with it");
    mp_wire_reader_init(&reader, buffer + 8, 4u);
    ut_check(!mp_wire_get_position(&reader, &back) && reader.overran,
             "and so is one past the other end");

    ut_section("nothing that is not a number goes out");

    mp_wire_writer_init(&writer, buffer, sizeof buffer);
    ut_check(!mp_wire_put_position(&writer, make_nan()),
             "a NaN position is refused at the encoder, where there is still somebody to tell");
    ut_check(writer.overflowed, "and it fails the whole record");

    mp_wire_writer_init(&writer, buffer, sizeof buffer);
    ut_check(!mp_wire_put_position(&writer, make_inf()), "an infinity likewise");

    mp_wire_writer_init(&writer, buffer, sizeof buffer);
    ut_check(!mp_wire_put_angle(&writer, make_nan()), "and a NaN angle");
}

static void check_angles(void)
{
    uint8_t          buffer[16];
    mp_wire_writer_t writer;
    mp_wire_reader_t reader;
    static const float CASES[] = { 0.0f, 1.0f, 90.0f, 179.9f, 180.0f, 270.0f, 359.9f };
    size_t index;
    float  back = 0.0f;

    ut_section("angles round trip, and wrap rather than being refused");

    for (index = 0; index < sizeof CASES / sizeof CASES[0]; ++index) {
        mp_wire_writer_init(&writer, buffer, sizeof buffer);
        ut_checkf(mp_wire_put_angle(&writer, CASES[index]), "%f degrees goes out",
                  (double)CASES[index]);
        mp_wire_reader_init(&reader, buffer, writer.at);
        ut_check(mp_wire_get_angle(&reader, &back), "and comes back");
        ut_near(back, CASES[index], MP_WIRE_ANGLE_ERROR, "within half a step");
    }

    mp_wire_writer_init(&writer, buffer, sizeof buffer);
    (void)mp_wire_put_angle(&writer, 370.0f);
    mp_wire_reader_init(&reader, buffer, writer.at);
    (void)mp_wire_get_angle(&reader, &back);
    ut_near(back, 10.0f, MP_WIRE_ANGLE_ERROR, "370 degrees is 10 degrees");

    mp_wire_writer_init(&writer, buffer, sizeof buffer);
    (void)mp_wire_put_angle(&writer, -90.0f);
    mp_wire_reader_init(&reader, buffer, writer.at);
    (void)mp_wire_get_angle(&reader, &back);
    ut_near(back, 270.0f, MP_WIRE_ANGLE_ERROR, "and minus 90 is 270");

    /* 360 has to land on 0 rather than on the top of the range, or a body facing north jitters
     * between two encodings on consecutive packets. */
    mp_wire_writer_init(&writer, buffer, sizeof buffer);
    (void)mp_wire_put_angle(&writer, 360.0f);
    mp_wire_reader_init(&reader, buffer, writer.at);
    (void)mp_wire_get_angle(&reader, &back);
    ut_near(back, 0.0f, MP_WIRE_ANGLE_ERROR, "and 360 is 0, not the top of the range");

    /* A billion degrees is a corrupt or hostile value, and it must come out as an angle in finite
     * time rather than hang the pump; 1e9 mod 360 is 280. */
    mp_wire_writer_init(&writer, buffer, sizeof buffer);
    ut_check(mp_wire_put_angle(&writer, 1.0e9f), "a billion degrees returns at all");
    mp_wire_reader_init(&reader, buffer, writer.at);
    (void)mp_wire_get_angle(&reader, &back);
    ut_near(back, 280.0f, MP_WIRE_ANGLE_ERROR, "and wraps to 280");

    mp_wire_writer_init(&writer, buffer, sizeof buffer);
    ut_check(mp_wire_put_angle(&writer, -1.0e9f), "so does minus a billion");
    mp_wire_reader_init(&reader, buffer, writer.at);
    (void)mp_wire_get_angle(&reader, &back);
    ut_near(back, 80.0f, MP_WIRE_ANGLE_ERROR, "and wraps to 80");
}

static void check_axes(void)
{
    static const float CASES[] = { -1.0f, -0.5f, -0.001f, 0.0f, 0.001f, 0.5f, 1.0f };
    uint8_t          buffer[8];
    mp_wire_writer_t writer;
    mp_wire_reader_t reader;
    size_t           index;
    float            back = 0.0f;

    ut_section("axes are signed, and clamp rather than wrap");

    for (index = 0; index < sizeof CASES / sizeof CASES[0]; ++index) {
        mp_wire_writer_init(&writer, buffer, sizeof buffer);
        ut_checkf(mp_wire_put_axis(&writer, CASES[index]), "%f goes out", (double)CASES[index]);
        ut_check(writer.at == 2u, "in two bytes");
        mp_wire_reader_init(&reader, buffer, writer.at);
        ut_check(mp_wire_get_axis(&reader, &back), "and comes back");
        ut_near(back, CASES[index], MP_WIRE_AXIS_ERROR, "within half a step, sign included");
    }

    mp_wire_writer_init(&writer, buffer, sizeof buffer);
    ut_check(mp_wire_put_axis(&writer, -3.0f), "beyond full deflection still goes out");
    mp_wire_reader_init(&reader, buffer, writer.at);
    (void)mp_wire_get_axis(&reader, &back);
    ut_check(back == -1.0f, "as full deflection, the same way");

    mp_wire_writer_init(&writer, buffer, sizeof buffer);
    ut_check(!mp_wire_put_axis(&writer, make_nan()), "not a number is refused");
    ut_check(writer.overflowed, "and the writer is failed for good");
}

static void check_body(void)
{
    uint8_t          buffer[MP_WIRE_BODY_MAX_BYTES];
    mp_wire_writer_t writer;
    mp_wire_reader_t reader;
    mp_wire_body_t   sent;
    mp_wire_body_t   got;

    ut_section("a whole body record");

    memset(&sent, 0, sizeof sent);
    sent.position[0] = 12.25f;
    sent.position[1] = -3.5f;
    sent.position[2] = 0.125f;
    sent.orientation[0] = 45.0f;
    sent.orientation[1] = 200.0f;
    sent.orientation[2] = 359.0f;
    sent.alive  = true;
    sent.dead   = false;
    sent.weapon = 7;
    sent.hero   = 3;
    sent.health = 77;
    sent.anim.clip[0]  = 0x1234;
    sent.anim.clip[1]  = 0;
    sent.anim.track[0] = 250;
    sent.anim.track[1] = 9;
    sent.anim.channel_mask = 0xDEADBEEFu;

    mp_wire_writer_init(&writer, buffer, sizeof buffer);
    ut_check(mp_wire_put_body(&writer, &sent), "the record goes out");
    ut_check(writer.at == MP_WIRE_BODY_MIN_BYTES,
             "and a body with no twist is exactly the minimum the header promises");

    mp_wire_reader_init(&reader, buffer, writer.at);
    ut_check(mp_wire_get_body(&reader, &got), "and comes back");

    ut_near(got.position[0], sent.position[0], MP_WIRE_POSITION_ERROR, "x survives");
    ut_near(got.position[1], sent.position[1], MP_WIRE_POSITION_ERROR, "y survives");
    ut_near(got.position[2], sent.position[2], MP_WIRE_POSITION_ERROR, "z survives");
    ut_near(got.orientation[0], sent.orientation[0], MP_WIRE_ANGLE_ERROR, "pitch survives");
    ut_near(got.orientation[1], sent.orientation[1], MP_WIRE_ANGLE_ERROR, "yaw survives");
    ut_near(got.orientation[2], sent.orientation[2], MP_WIRE_ANGLE_ERROR, "roll survives");
    ut_check(got.alive && !got.dead, "the two flags survive independently");
    ut_check(got.weapon == 7 && got.hero == 3, "the identities survive");
    ut_check(got.health == 77, "the health byte rides between the hero and the animation");
    ut_check(buffer[21] == 77, "at the twenty-second byte, after position, angles, flags, "
                               "weapon and hero");
    ut_check(got.anim.clip[0] == 0x1234 && got.anim.track[0] == 250,
             "the animation is a clip and a time, never a pose");
    ut_check(got.anim.channel_mask == 0xDEADBEEFu, "and the channel mask survives");
    ut_check(got.twist_count == 0u, "and no twist was invented");

    ut_section("the node twists ride along, signed");

    sent.twist_count    = 2;
    sent.twist[0].node  = 0;
    sent.twist[0].pitch = 0.0f;
    sent.twist[0].yaw   = -35.5f;
    sent.twist[1].node  = 7;
    sent.twist[1].pitch = 12.0f;
    sent.twist[1].yaw   = 170.0f;
    mp_wire_writer_init(&writer, buffer, sizeof buffer);
    ut_check(mp_wire_put_body(&writer, &sent), "a body with two twists goes out");
    ut_check(writer.at == MP_WIRE_BODY_MIN_BYTES + 2u * 5u, "at five bytes per twist");
    mp_wire_reader_init(&reader, buffer, writer.at);
    ut_check(mp_wire_get_body(&reader, &got) && got.twist_count == 2u, "and comes back with both");
    ut_check(got.twist[0].node == 0 && got.twist[1].node == 7, "on the right nodes");
    ut_near(got.twist[0].yaw, -35.5f, MP_WIRE_ANGLE_ERROR, "a negative yaw comes back negative");
    ut_near(got.twist[1].pitch, 12.0f, MP_WIRE_ANGLE_ERROR, "a positive pitch survives");
    ut_near(got.twist[1].yaw, 170.0f, MP_WIRE_ANGLE_ERROR, "and a yaw below 180 stays positive");

    sent.twist_count = MP_WIRE_MAX_TWISTS + 3u;
    mp_wire_writer_init(&writer, buffer, sizeof buffer);
    ut_check(mp_wire_put_body(&writer, &sent) && writer.at == MP_WIRE_BODY_MAX_BYTES,
             "a count past the cap is cut to the cap, and the maximum is the header's maximum");

    buffer[MP_WIRE_BODY_MIN_BYTES - 1u] = (uint8_t)(MP_WIRE_MAX_TWISTS + 1u);
    mp_wire_reader_init(&reader, buffer, writer.at);
    ut_check(!mp_wire_get_body(&reader, &got), "a count the record cannot hold is refused");

    ut_section("dead is not the negation of alive");

    memset(&sent, 0, sizeof sent);
    mp_wire_writer_init(&writer, buffer, sizeof buffer);
    (void)mp_wire_put_body(&writer, &sent);
    mp_wire_reader_init(&reader, buffer, writer.at);
    (void)mp_wire_get_body(&reader, &got);
    ut_check(!got.alive && !got.dead,
             "a body that has not spawned is neither, and a receiver inferring one from the other "
             "would put it on the floor");

    ut_section("a truncated record is refused whole");

    mp_wire_writer_init(&writer, buffer, MP_WIRE_BODY_MIN_BYTES - 1u);
    ut_check(!mp_wire_put_body(&writer, &sent), "one byte short and the record does not go out");

    mp_wire_reader_init(&reader, buffer, MP_WIRE_BODY_MIN_BYTES - 1u);
    ut_check(!mp_wire_get_body(&reader, &got), "and a short one does not come in");
}

/* The flag byte sits behind the three positions and the three angles. */
#define BODY_FLAG_AT 18u

static void check_the_world_bits(void)
{
    uint8_t          buffer[MP_WIRE_BODY_MAX_BYTES];
    uint8_t          again[MP_WIRE_BODY_MAX_BYTES];
    mp_wire_writer_t writer;
    mp_wire_reader_t reader;
    mp_wire_body_t   sent;
    mp_wire_body_t   got;
    unsigned         world;
    unsigned         flags;
    bool             every_world = true;
    bool             every_byte  = true;

    ut_section("a body carries the world it was sent from, four bits of its flag byte");
    ut_check(mp_wire_world_of(1u) == 1u && mp_wire_world_of(17u) == 1u &&
                 mp_wire_world_of(0xFFu) == MP_WIRE_WORLD_MASK,
             "a generation is cut to its low four bits");
    memset(&sent, 0, sizeof sent);
    sent.alive = true;
    for (world = 0; world <= MP_WIRE_WORLD_MASK; ++world) {
        sent.world = (uint8_t)world;
        mp_wire_writer_init(&writer, buffer, sizeof buffer);
        mp_wire_reader_init(&reader, buffer, sizeof buffer);
        if (!mp_wire_put_body(&writer, &sent) || buffer[BODY_FLAG_AT] != (0x01u | (world << 4)) ||
            !mp_wire_get_body(&reader, &got) || got.world != world || !got.alive || got.dead) {
            every_world = false;
        }
    }
    ut_check(every_world, "every world from 0 to 15 goes out in the high four bits and comes back "
                          "beside the two flags");
    ut_check(writer.at == MP_WIRE_BODY_MIN_BYTES, "and the record did not grow");
    sent.world = (uint8_t)(MP_WIRE_WORLD_MASK + 1u);
    mp_wire_writer_init(&writer, buffer, sizeof buffer);
    ut_check(!mp_wire_put_body(&writer, &sent) && writer.overflowed,
             "a world the four bits cannot hold is refused, not sent as another world");

    ut_section("round trip over every flag byte: 0x04 and 0x08 are read past, the rest survive");
    sent.world = 0u;
    for (flags = 0; flags < 256u; ++flags) {
        mp_wire_writer_init(&writer, buffer, sizeof buffer);
        (void)mp_wire_put_body(&writer, &sent);
        buffer[BODY_FLAG_AT] = (uint8_t)flags;
        mp_wire_reader_init(&reader, buffer, writer.at);
        mp_wire_writer_init(&writer, again, sizeof again);
        if (!mp_wire_get_body(&reader, &got) || !mp_wire_put_body(&writer, &got) ||
            again[BODY_FLAG_AT] != (uint8_t)(flags & ~0x0Cu) ||
            memcmp(again, buffer, BODY_FLAG_AT) != 0) {
            every_byte = false;
        }
    }
    ut_check(every_byte, "all 256 flag bytes decode, and each says again what it said, the two "
                         "reserved bits cleared");
}

static void check_health_and_track(void)
{
    ut_section("health is clamped to the byte, never wrapped");

    ut_check(mp_wire_clamp_health(0) == 0u, "zero is zero");
    ut_check(mp_wire_clamp_health(100) == 100u, "full health is itself");
    ut_check(mp_wire_clamp_health(255) == 255u, "the byte's top is itself");
    ut_check(mp_wire_clamp_health(300) == 255u, "a value the engine's setter let past 100 stops "
                                                "at the byte's top instead of wrapping to 44");
    ut_check(mp_wire_clamp_health(-5) == 0u, "and a negative one is zero, not a large number");

    ut_section("a playhead in frames becomes sixteenths, clamped");

    ut_check(mp_wire_track_from_frames(0.0f) == 0u, "frame zero is zero");
    ut_check(mp_wire_track_from_frames(1.0f) == 16u, "one frame is sixteen");
    ut_check(mp_wire_track_from_frames(12.5f) == 200u, "twelve and a half frames is two hundred");
    ut_check(mp_wire_track_from_frames(4095.0f) == 65520u, "the stated ceiling fits");
    ut_check(mp_wire_track_from_frames(5000.0f) == 65535u, "past the ceiling it clamps");
    ut_check(mp_wire_track_from_frames(-3.0f) == 0u, "a negative playhead is the start");
    ut_check(mp_wire_track_from_frames(make_nan()) == 0u, "and so is one that is not a number");
}

static void check_null_buffers(void)
{
    mp_wire_writer_t writer;
    mp_wire_reader_t reader;
    uint8_t          eight = 0;

    ut_section("a buffer that is not there");

    mp_wire_writer_init(&writer, NULL, 16u);
    ut_check(writer.overflowed, "a writer over no buffer starts failed");
    ut_check(!mp_wire_put_u8(&writer, 1u), "and refuses everything");

    mp_wire_reader_init(&reader, NULL, 16u);
    ut_check(reader.overran, "and a reader likewise");
    ut_check(!mp_wire_get_u8(&reader, &eight), "and reads nothing");
}

int main(void)
{
    check_integers();
    check_the_last_byte();
    check_positions();
    check_angles();
    check_axes();
    check_body();
    check_the_world_bits();
    check_health_and_track();
    check_null_buffers();

    return ut_summary("multiplayer wire");
}
