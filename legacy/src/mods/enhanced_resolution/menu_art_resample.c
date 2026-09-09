#include "menu_art_resample.h"

#include <string.h>

int32_t menu_art_resample_source_index(int32_t dest_index, int32_t dest_extent,
                                       int32_t source_extent)
{
    int32_t index;

    if (dest_extent <= 0 || source_extent <= 0) {
        return 0;
    }
    if (dest_index < 0) {
        dest_index = 0;
    }

    /* The centre of destination pixel n is at (2n + 1) / (2 * dest) of the way across, and the
     * source pixel under it is that fraction of the source extent. Done as one multiply and one
     * divide so no rounding happens before the division. */
    index = (int32_t)(((2 * dest_index + 1) * source_extent) / (2 * dest_extent));

    /* The last destination pixel of an exact ratio lands exactly on the extent, one past the end.
     * Clamping rather than refusing, because the alternative is a special case at one edge of every
     * picture for a value that is off by one. */
    if (index >= source_extent) {
        index = source_extent - 1;
    }
    return index;
}

int32_t menu_art_resample_scaled(int32_t value, float ratio)
{
    float scaled;

    if (!(ratio > 0.0f)) {
        return value;
    }
    scaled = (float)value * ratio;

    /* Away from zero at the half, which is what Python's round() does not do and what
     * the retired artwork converter's int(v * ratio + 0.5) does. The converter is the reference
     * because its output is what a player who ran it already has on disk. */
    return (scaled >= 0.0f) ? (int32_t)(scaled + 0.5f) : -(int32_t)(-scaled + 0.5f);
}

bool menu_art_resample_16(const uint16_t *source, int32_t source_width, int32_t source_height,
                          int32_t source_pitch_pixels,
                          uint16_t *dest, int32_t dest_width, int32_t dest_height,
                          int32_t dest_pitch_pixels)
{
    int32_t         y;
    int32_t         previous_source_row = -1;
    const uint16_t *previous_dest_row   = NULL;

    if (source == NULL || dest == NULL ||
        source_width <= 0 || source_height <= 0 || dest_width <= 0 || dest_height <= 0 ||
        source_pitch_pixels < source_width || dest_pitch_pixels < dest_width) {
        return false;
    }

    /* Refused rather than clamped: see the header. Shrinking drops whole source pixels, so a one
     * pixel border comes out dashed rather than thinner, and a caller that wanted that has asked
     * for something this cannot do well. */
    if (dest_width < source_width || dest_height < source_height) {
        return false;
    }

    for (y = 0; y < dest_height; ++y) {
        int32_t   source_row = menu_art_resample_source_index(y, dest_height, source_height);
        uint16_t *dest_row   = dest + (size_t)y * (size_t)dest_pitch_pixels;

        /* The row above came from the same source row, so it is already the answer. At six times
         * this is five rows in six, and it is most of the reason a 4K background costs about a
         * millisecond rather than several. */
        if (source_row == previous_source_row && previous_dest_row != NULL) {
            memcpy(dest_row, previous_dest_row, (size_t)dest_width * sizeof(uint16_t));
            continue;
        }

        {
            const uint16_t *source_row_pixels =
                source + (size_t)source_row * (size_t)source_pitch_pixels;
            int32_t x;

            for (x = 0; x < dest_width; ++x) {
                dest_row[x] =
                    source_row_pixels[menu_art_resample_source_index(x, dest_width, source_width)];
            }
        }

        previous_source_row = source_row;
        previous_dest_row   = dest_row;
    }
    return true;
}
