/* mp_dialog.c: the codec for one spoken line and the host's pick. The header carries the
 * reasoning.
 *
 * The line is written against the wire's own writer and reader rather than by hand, because the
 * fixed point a position travels in is a decision the whole feature shares and a second spelling
 * of it would be a second thing to keep true.
 */
#include "mp_dialog.h"

#include "mp_wire.h"

#include <string.h>

size_t mp_dialog_encode(const mp_dialog_line_t *line, uint8_t *buffer, size_t capacity)
{
    mp_wire_writer_t writer;
    size_t           axis;

    if (line == NULL || buffer == NULL || capacity < MP_DIALOG_BYTES) {
        return 0u;
    }
    /* An id the receiver's own book cannot hold is refused HERE rather than sent and refused
     * there. The assert that would have caught it is compiled out of the retail build, so what a
     * receiver would actually do with it is read off the end of the book. */
    if (line->line >= MP_DIALOG_MAX_ID) {
        return 0u;
    }
    mp_wire_writer_init(&writer, buffer, capacity);
    (void)mp_wire_put_u8(&writer, (uint8_t)MP_DIALOG_TAG);
    (void)mp_wire_put_u16(&writer, line->level);
    (void)mp_wire_put_u16(&writer, line->line);
    (void)mp_wire_put_u8(&writer, line->has_position ? 1u : 0u);
    for (axis = 0; axis < 3u; ++axis) {
        /* A place that will not fit the fixed point makes the whole note refuse rather than
         * travel with a wrong anchor: a voice at the wrong end of the level is worse than a voice
         * with no place, and the receiver has a rule for no place already. */
        if (!mp_wire_put_position(&writer, line->has_position ? line->position[axis] : 0.0f)) {
            return 0u;
        }
    }
    if (writer.overflowed) {
        return 0u;
    }
    return MP_DIALOG_BYTES;
}

bool mp_dialog_is(const uint8_t *buffer, size_t bytes)
{
    return buffer != NULL && bytes == MP_DIALOG_BYTES && buffer[0] == (uint8_t)MP_DIALOG_TAG;
}

bool mp_dialog_decode(const uint8_t *buffer, size_t bytes, mp_dialog_line_t *out)
{
    mp_wire_reader_t reader;
    uint8_t          tag  = 0;
    uint8_t          flag = 0;
    size_t           axis;

    if (out == NULL || !mp_dialog_is(buffer, bytes)) {
        return false;
    }
    memset(out, 0, sizeof *out);
    mp_wire_reader_init(&reader, buffer, bytes);
    (void)mp_wire_get_u8(&reader, &tag);
    (void)mp_wire_get_u16(&reader, &out->level);
    (void)mp_wire_get_u16(&reader, &out->line);
    (void)mp_wire_get_u8(&reader, &flag);
    for (axis = 0; axis < 3u; ++axis) {
        if (!mp_wire_get_position(&reader, &out->position[axis])) {
            return false;
        }
    }
    /* The same bound as the encoder, applied again on the way in. The sender is another machine
     * and a note is bytes off a socket; a line id past the book is the one field here whose abuse
     * reads memory rather than showing the wrong words. */
    if (out->line >= MP_DIALOG_MAX_ID) {
        return false;
    }
    /* Anything but nought or one means a sender this build does not describe. */
    if (flag > 1u) {
        return false;
    }
    out->has_position = flag != 0u;
    return true;
}

/* ==============================================================================================
 * The host's pick. Short enough to write by hand rather than through the wire writer, and it
 * bounds the line id the same way the spoken line does, for the same reason: the assert behind
 * the book is compiled out of the retail build.
 * ============================================================================================ */

size_t mp_dialog_encode_pick(uint16_t level, uint16_t line, uint8_t *buffer, size_t capacity)
{
    if (buffer == NULL || capacity < MP_DIALOG_PICK_BYTES || line >= MP_DIALOG_MAX_ID) {
        return 0u;
    }
    buffer[0] = (uint8_t)MP_DIALOG_PICK_TAG;
    buffer[1] = (uint8_t)(level & 0xFFu);
    buffer[2] = (uint8_t)((level >> 8) & 0xFFu);
    buffer[3] = (uint8_t)(line & 0xFFu);
    buffer[4] = (uint8_t)((line >> 8) & 0xFFu);
    return MP_DIALOG_PICK_BYTES;
}

bool mp_dialog_is_pick(const uint8_t *buffer, size_t bytes)
{
    return buffer != NULL && bytes == MP_DIALOG_PICK_BYTES &&
           buffer[0] == (uint8_t)MP_DIALOG_PICK_TAG;
}

bool mp_dialog_decode_pick(const uint8_t *buffer, size_t bytes, uint16_t *level, uint16_t *line)
{
    uint16_t heard;

    if (!mp_dialog_is_pick(buffer, bytes)) {
        return false;
    }
    heard = (uint16_t)((uint16_t)buffer[3] | ((uint16_t)buffer[4] << 8));
    if (heard >= MP_DIALOG_MAX_ID) {
        return false;
    }
    if (level != NULL) {
        *level = (uint16_t)((uint16_t)buffer[1] | ((uint16_t)buffer[2] << 8));
    }
    if (line != NULL) {
        *line = heard;
    }
    return true;
}
