/* menu_preview.c: the four animated previews on the main menu, and the resampler behind them.
 *
 * The seam: every other menu picture is scaled by converting the artwork on disk, so the canvas
 * scale never has to look at a pixel. These four are decoded at run time, so they are the one
 * place in the feature that owns pixel buffers, a filter and a per-frame cost. None of that is
 * shared with anything else here, which is why it is a file of its own; what it takes from the
 * rest is the two ratios and the widget's own blit mode.
 */
#include "menu_preview.h"

#include "menu_scale_internal.h"
#include "menu_scale_sites.h"

#include "common/logging.h"

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

/* ---------------------------------------------------------------------------------------------
 * The four animated previews on the main menu
 *
 * title_main_menu creates four 232x100 video surfaces, opens ss0.bik..ss3.bik into them as looping
 * previews, and plants each surface into widgets 1..4 through swmenu_setWidgetImage. That is the
 * ONLY call to that function in the game, so a picture widget carrying a pData is one of these four
 * and nothing else, which is what makes the test below safe.
 *
 * They are the one part of the front end the canvas scale cannot reach on its own. Every other menu
 * bitmap comes out of the archives and is whatever size the converted artwork made it, but these
 * are decoded at run time into a surface whose 232 and 100 are immediates inside title_main_menu.
 * So the scale moves the four buttons to their new places and leaves them at their authored size,
 * which reads as a bug even though nothing has failed.
 *
 * WHY THE SURFACE IS NOT SIMPLY CREATED BIGGER. Patching those two immediates is easy, and wrong.
 * Bink decodes at the video's own 232x100 whatever it is given, so a larger surface would hold a
 * small picture in the corner of a large black rectangle, and the only way to fill it would be to
 * upscale the surface in place. Three of the four clips are paused at any moment, so an in place
 * upscale would run again next frame over its own output, and again, compounding into mush inside a
 * second.
 *
 * So the engine's surface is left exactly as it is, always holding one pristine native frame, and
 * the draw is handed a larger buffer of ours filled from it. The swap is undone the moment the draw
 * returns, so no other code ever sees it.
 *
 * The one thing that does persist is the widget rectangle, because swpic_draw writes the frame's
 * size into it before blitting. That is the wanted outcome rather than a leak: the hit box then
 * matches what the reader can see, so the whole face of each button is clickable.
 *
 * WHY THE MODE IS TESTED AND NOT JUST pData. The save game screens plant thumbnails through the
 * same pData with bCompress set, and swrle_compressVBuffer FREES the pixel buffer, puts a much
 * shorter run length stream in its place, and leaves rasterInfo completely alone. So a compressed
 * surface still claims to be 232x100 raw pixels and nothing on it says otherwise: reading it as raw
 * walks off the end of the allocation, which is a crash on opening Load Game.
 *
 * The discriminator is the widget's fontIndex, which doubles as the blit mode. swpic_setWidgetImage
 * raises it to 2 or more exactly when it leaves the image raw, and swpic_blit branches on that same
 * field to choose between the run length blitter and the plain surface copy. Testing it here is the
 * engine's own test, read from the engine's own field, rather than a guess about the buffer.
 *
 * The consequence is that save game thumbnails are NOT scaled: they are compressed, so they go
 * through swrle_blit, which has no scale term. They stay at their authored size inside a scaled
 * screen. That is the shipped behaviour, not a regression.
 *
 * Nearest neighbour, deliberately. A 232x100 clip on a 4K button is a six times blow up with no
 * extra detail in it, so a smoothing filter would buy blur rather than sharpness while costing
 * arithmetic per pixel per frame on four buffers. Whole pixel replication also means each distinct
 * source row is expanded once and then copied for its repeats, which is what keeps this off the
 * frame time.
 */
#define VBUFFER_WIDTH          0x0Cu   /* these five are contiguous, and are saved and restored */
#define VBUFFER_HEIGHT         0x10u   /* as one block in the hook below                        */
#define VBUFFER_SIZE           0x14u
#define VBUFFER_BYTES_PER_LINE 0x18u
#define VBUFFER_PITCH_PIXELS   0x1Cu
#define VBUFFER_HEADER_INTS    5
#define VBUFFER_COLOR_INFO     0x20u   /* rasterInfo at +0x0C, colorInfo at +0x14 */

/* Indices into that colorInfo, in dwords. The three channel fields are consecutive, so a loop over
 * red, green and blue can index them off the first. */
#define COLOR_INFO_MODE        0        /* 1 = direct RGB, 0 = palette indexed */
#define COLOR_INFO_BPP         1
#define COLOR_INFO_RED_BITS    2        /* then green, then blue */
#define COLOR_INFO_RED_SHIFT   5        /* the LEFT shift, i.e. the channel's bit position */
#define VBUFFER_PIXELS         0x5Cu

/* Only four previews are ever live. The spare room is for the title screen being torn down and
 * rebuilt, which leaves four stale surfaces behind; past that the slot drawn longest ago is reused,
 * so this cannot grow however many times the reader leaves the front end and comes back. */
#define PREVIEW_CAPACITY 8u

/* A preview surface is 232x100. Anything outside these bounds is not one, and is left alone. */
#define PREVIEW_MAX_SOURCE_EDGE 2048
#define PREVIEW_MAX_TARGET_EDGE 8192

/* How to take a pixel apart, read from the surface rather than assumed. */
typedef struct preview_format {
    int32_t  bytes_per_pixel;
    int32_t  shift[3];            /* red, green, blue bit positions */
    uint32_t mask[3];
    bool     smooth;              /* false for palette indexed: an index cannot be interpolated */
} preview_format_t;

typedef struct preview_slot {
    const void      *frame;       /* the engine's surface, a key here and never dereferenced */
    uint8_t         *pixels;
    int32_t         *column;      /* destination x -> (source x << 8) | fraction */
    uint16_t        *scratch;     /* two horizontally resampled rows, three channels per pixel */
    size_t           pixel_bytes;
    int32_t          source_width;
    int32_t          source_height;
    int32_t          width;
    int32_t          height;
    int32_t          bytes_per_line;
    preview_format_t format;
    uint32_t         source_hash; /* of the source the buffer currently holds */
    bool             has_content;
    uint32_t         used;        /* the clock reading at its last draw, for reuse order */
} preview_slot_t;

static preview_slot_t previews[PREVIEW_CAPACITY];

static void preview_release(preview_slot_t *slot)
{
    free(slot->pixels);
    free(slot->column);
    free(slot->scratch);
    slot->pixels      = NULL;
    slot->column      = NULL;
    slot->scratch     = NULL;
    slot->frame       = NULL;
    slot->has_content = false;
}
static uint32_t       preview_clock;
static bool           warned_preview;

typedef void(__cdecl *pic_draw_fn_t)(void *widget, void *menu);

/* Finds the slot for this surface, or takes one, and makes sure its buffers are the right size.
 * Returns NULL only when memory could not be had, in which case the preview is drawn at its
 * authored size, which is what happens without this file at all. */
static preview_slot_t *preview_claim(const void *frame, int32_t source_width,
                                     int32_t source_height, int32_t width, int32_t height,
                                     const preview_format_t *format)
{
    preview_slot_t *slot = NULL;
    size_t          index;
    int32_t         bytes_per_line;
    size_t          bytes;

    for (index = 0; index < PREVIEW_CAPACITY; ++index) {
        if (previews[index].frame == frame) {
            slot = &previews[index];
            break;
        }
        if (previews[index].frame == NULL) {
            slot = &previews[index];          /* a free one, kept in case nothing matches */
        }
    }
    if (slot == NULL) {
        slot = &previews[0];                  /* all taken: reuse the one drawn longest ago */
        for (index = 1; index < PREVIEW_CAPACITY; ++index) {
            if (previews[index].used < slot->used) {
                slot = &previews[index];
            }
        }
    }

    slot->used = ++preview_clock;
    if (slot->frame == frame && slot->pixels != NULL &&
        slot->source_width == source_width && slot->source_height == source_height &&
        slot->width == width && slot->height == height &&
        slot->format.bytes_per_pixel == format->bytes_per_pixel &&
        slot->format.smooth == format->smooth) {
        return slot;                          /* the usual case, from the second frame onwards */
    }

    bytes_per_line = (width * format->bytes_per_pixel + 3) & ~3;
    bytes          = (size_t)bytes_per_line * (size_t)height;

    {
        uint8_t  *pixels = (uint8_t *)realloc(slot->pixels, bytes);
        int32_t  *column;
        uint16_t *scratch;

        if (pixels == NULL) {
            preview_release(slot);
            return NULL;
        }
        slot->pixels = pixels;

        column = (int32_t *)realloc(slot->column, (size_t)width * sizeof(int32_t));
        if (column == NULL) {
            preview_release(slot);
            return NULL;
        }
        slot->column = column;

        /* Two rows of horizontally resampled channel values, so the expensive half of a separable
         * filter runs once per SOURCE row rather than once per destination row. At six times that
         * is most of the work saved. */
        scratch = (uint16_t *)realloc(slot->scratch,
                                      (size_t)width * 3u * 2u * sizeof(uint16_t));
        if (scratch == NULL) {
            preview_release(slot);
            return NULL;
        }
        slot->scratch = scratch;
    }

    /* The source position of every destination column, as an index and an eight bit fraction.
     * Sampling from pixel CENTRES, which is what keeps the resampled picture from drifting half a
     * source pixel up and left of where the nearest neighbour version put it. */
    {
        int32_t step = (source_width << 8) / width;
        int32_t x;

        for (x = 0; x < width; ++x) {
            int32_t position = x * step + (step >> 1) - 128;
            int32_t source_x;

            if (position < 0) {
                position = 0;
            }
            source_x = position >> 8;
            if (source_x >= source_width - 1) {
                source_x = (source_width > 0) ? source_width - 1 : 0;
                position = source_x << 8;                 /* clamp, and do not read past the edge */
            }
            slot->column[x] = (source_x << 8) | (position & 0xFF);
        }
    }

    slot->frame          = frame;
    slot->pixel_bytes    = bytes;
    slot->source_width   = source_width;
    slot->source_height  = source_height;
    slot->width          = width;
    slot->height         = height;
    slot->bytes_per_line = bytes_per_line;
    slot->format         = *format;
    slot->has_content    = false;             /* the buffers are new, so the hash means nothing */
    return slot;
}

static uint32_t preview_read_pixel(const uint8_t *row, int32_t index, int32_t bytes_per_pixel)
{
    if (bytes_per_pixel == 2) {
        return *(const uint16_t *)(row + (size_t)index * 2u);
    }
    if (bytes_per_pixel == 4) {
        return *(const uint32_t *)(row + (size_t)index * 4u);
    }
    return row[(size_t)index * (size_t)bytes_per_pixel];
}

static void preview_write_pixel(uint8_t *row, int32_t index, int32_t bytes_per_pixel, uint32_t value)
{
    if (bytes_per_pixel == 2) {
        *(uint16_t *)(row + (size_t)index * 2u) = (uint16_t)value;
    } else if (bytes_per_pixel == 4) {
        *(uint32_t *)(row + (size_t)index * 4u) = value;
    } else {
        row[(size_t)index * (size_t)bytes_per_pixel] = (uint8_t)value;
    }
}

/* One source row, resampled across into three channel values per destination pixel, each carried at
 * eight extra bits of precision so the vertical blend that follows does not quantise twice. */
static void preview_expand_row(const preview_slot_t *slot, const uint8_t *source, uint16_t *out)
{
    const preview_format_t *format = &slot->format;
    int32_t                 x;

    for (x = 0; x < slot->width; ++x) {
        int32_t  packed   = slot->column[x];
        int32_t  source_x = packed >> 8;
        uint32_t fraction = (uint32_t)(packed & 0xFF);
        int32_t  next     = (source_x + 1 < slot->source_width) ? source_x + 1 : source_x;
        uint32_t left     = preview_read_pixel(source, source_x, format->bytes_per_pixel);
        uint32_t right    = preview_read_pixel(source, next,     format->bytes_per_pixel);
        int      channel;

        for (channel = 0; channel < 3; ++channel) {
            uint32_t a = (left  >> format->shift[channel]) & format->mask[channel];
            uint32_t b = (right >> format->shift[channel]) & format->mask[channel];

            out[x * 3 + channel] = (uint16_t)(a * (256u - fraction) + b * fraction);
        }
    }
}

/* Bilinear, separable, and only ever called when the source has actually changed.
 *
 * Nearest neighbour was the first version of this and it is what a six times blow-up of a 160x120
 * save thumbnail looks like: every source pixel becomes a visible 6x4 block. Bilinear costs about
 * thirty operations per destination pixel, which would be real frame time at a hundred frames a
 * second, so the caller hashes the source first and skips this entirely while the picture is
 * standing still. On the save screen that means once per row selected; on the main menu, once per
 * frame of the one clip that is playing.
 *
 * PALETTE SURFACES ARE NOT SMOOTHED. Interpolating two palette INDICES produces a third index whose
 * colour has nothing to do with either, so those fall back to whole pixel replication. No surface
 * this actually meets is palettised; the test is there so that one never comes out as confetti. */
static void preview_fill(preview_slot_t *slot, const uint8_t *source, int32_t source_bytes_per_line)
{
    const preview_format_t *format = &slot->format;
    uint16_t               *rows[2];
    int32_t                 held[2];
    int32_t                 step;
    int32_t                 y;

    if (!format->smooth) {
        int32_t previous = -1;

        for (y = 0; y < slot->height; ++y) {
            uint8_t *out = slot->pixels + (size_t)y * (size_t)slot->bytes_per_line;
            int32_t  source_y = (int32_t)(((int64_t)y * slot->source_height) / slot->height);
            const uint8_t *from;
            int32_t  x;

            if (source_y == previous) {
                memcpy(out, out - slot->bytes_per_line, (size_t)slot->bytes_per_line);
                continue;
            }
            previous = source_y;
            from     = source + (size_t)source_y * (size_t)source_bytes_per_line;
            for (x = 0; x < slot->width; ++x) {
                preview_write_pixel(out, x, format->bytes_per_pixel,
                                    preview_read_pixel(from, slot->column[x] >> 8,
                                                       format->bytes_per_pixel));
            }
        }
        return;
    }

    rows[0] = slot->scratch;
    rows[1] = slot->scratch + (size_t)slot->width * 3u;
    held[0] = -1;
    held[1] = -1;
    step    = (slot->source_height << 8) / slot->height;

    for (y = 0; y < slot->height; ++y) {
        uint8_t *out = slot->pixels + (size_t)y * (size_t)slot->bytes_per_line;
        int32_t  position = y * step + (step >> 1) - 128;
        int32_t  top;
        int32_t  bottom;
        uint32_t fraction;
        int32_t  x;
        int      i;

        if (position < 0) {
            position = 0;
        }
        top = position >> 8;
        if (top >= slot->source_height - 1) {
            top      = (slot->source_height > 0) ? slot->source_height - 1 : 0;
            position = top << 8;
        }
        fraction = (uint32_t)(position & 0xFF);
        bottom   = (top + 1 < slot->source_height) ? top + 1 : top;

        /* Keep whichever of the two scratch rows already holds a source row we still need, so a
         * vertical scale of N re-expands each source row once rather than N times. */
        for (i = 0; i < 2; ++i) {
            int32_t want = (i == 0) ? top : bottom;

            if (held[0] != want && held[1] != want) {
                int32_t victim = (held[0] == top || held[0] == bottom) ? 1 : 0;

                preview_expand_row(slot, source + (size_t)want * (size_t)source_bytes_per_line,
                                   rows[victim]);
                held[victim] = want;
            }
        }

        {
            const uint16_t *above = (held[0] == top)    ? rows[0] : rows[1];
            const uint16_t *below = (held[0] == bottom) ? rows[0] : rows[1];

            for (x = 0; x < slot->width; ++x) {
                uint32_t value = 0;
                int      channel;

                for (channel = 0; channel < 3; ++channel) {
                    uint32_t a = above[x * 3 + channel];
                    uint32_t b = below[x * 3 + channel];
                    uint32_t c = (a * (256u - fraction) + b * fraction) >> 16;

                    value |= c << format->shift[channel];
                }
                preview_write_pixel(out, x, format->bytes_per_pixel, value);
            }
        }
    }
}

/* Cheap enough to run every frame on every preview, which is the point: it is what buys the right
 * to run a real resampler at all. Four 32-bit words at a time over the source, which for the four
 * main menu clips is about 185 KiB a frame. */
static uint32_t preview_source_hash(const uint8_t *source, int32_t width, int32_t height,
                                    int32_t bytes_per_line, int32_t bytes_per_pixel)
{
    uint32_t hash = 2166136261u;
    size_t   used = (size_t)width * (size_t)bytes_per_pixel;
    int32_t  y;

    for (y = 0; y < height; ++y) {
        const uint8_t *row = source + (size_t)y * (size_t)bytes_per_line;
        size_t         i   = 0;

        while (i + 4u <= used) {
            uint32_t word;
            memcpy(&word, row + i, sizeof word);
            hash = (hash ^ word) * 16777619u;
            i += 4u;
        }
        while (i < used) {
            hash = (hash ^ row[i]) * 16777619u;
            ++i;
        }
    }
    return hash;
}

/* Returns the filled slot to draw this surface from, or NULL to draw it as the engine would. */
static preview_slot_t *preview_upscale(const char *frame)
{
    int32_t        source_width  = *(const int32_t *)(frame + VBUFFER_WIDTH);
    int32_t        source_height = *(const int32_t *)(frame + VBUFFER_HEIGHT);
    int32_t        source_stride = *(const int32_t *)(frame + VBUFFER_BYTES_PER_LINE);
    const int32_t *colour        = (const int32_t *)(frame + VBUFFER_COLOR_INFO);
    const uint8_t *pixels        = *(const uint8_t *const *)(frame + VBUFFER_PIXELS);
    preview_format_t format;
    int32_t          bpp;
    int32_t          width;
    int32_t          height;
    uint32_t         hash;
    preview_slot_t  *slot;
    int              channel;

    if (pixels == NULL) {
        return NULL;
    }
    bpp = colour[COLOR_INFO_BPP];
    if (bpp <= 0 || (bpp & 7) != 0 || bpp > 32) {
        return NULL;
    }
    format.bytes_per_pixel = bpp / 8;

    /* The channel layout is read from the surface rather than assumed, because 555 and 565 both
     * occur and the difference is the whole picture. colorMode 1 is direct RGB; 0 is palette
     * indexed, which cannot be interpolated. */
    for (channel = 0; channel < 3; ++channel) {
        int32_t bits = colour[COLOR_INFO_RED_BITS + channel];

        format.shift[channel] = colour[COLOR_INFO_RED_SHIFT + channel];
        format.mask[channel]  = (bits > 0 && bits <= 8) ? ((1u << bits) - 1u) : 0u;
        if (format.mask[channel] == 0u || format.shift[channel] < 0 ||
            format.shift[channel] > 31) {
            return NULL;
        }
    }
    format.smooth = (colour[COLOR_INFO_MODE] == 1);

    if (source_width  <= 0 || source_width  > PREVIEW_MAX_SOURCE_EDGE ||
        source_height <= 0 || source_height > PREVIEW_MAX_SOURCE_EDGE ||
        source_stride < source_width * format.bytes_per_pixel) {
        return NULL;
    }

    width  = scaled_coordinate(source_width,  scale_state.ratio_x);
    height = scaled_coordinate(source_height, scale_state.ratio_y);
    if (width <= source_width && height <= source_height) {
        return NULL;                          /* nothing to gain, so nothing is copied */
    }
    if (width > PREVIEW_MAX_TARGET_EDGE || height > PREVIEW_MAX_TARGET_EDGE) {
        return NULL;
    }

    slot = preview_claim(frame, source_width, source_height, width, height, &format);
    if (slot == NULL) {
        if (!warned_preview) {
            warned_preview = true;
            log_warning("a menu preview could not be given a %dx%d buffer, so it stays at its "
                        "authored %dx%d. Nothing else is affected",
                        (int)width, (int)height, (int)source_width, (int)source_height);
        }
        return NULL;
    }

    hash = preview_source_hash(pixels, source_width, source_height, source_stride,
                               format.bytes_per_pixel);
    if (slot->has_content && slot->source_hash == hash) {
        return slot;                          /* the picture has not moved; the buffer still holds it */
    }
    slot->source_hash = hash;
    slot->has_content = true;
    preview_fill(slot, pixels, source_stride);
    return slot;
}

void __cdecl hook_pic_draw(void *widget, void *menu)
{
    pic_draw_fn_t   original = (pic_draw_fn_t)scale_state.pic_draw_detour.original;
    char           *frame;
    int32_t         mode;
    preview_slot_t *slot;
    int32_t         saved[VBUFFER_HEADER_INTS];
    uint8_t        *saved_pixels;

    if (original == NULL) {
        return;
    }
    frame = *(char *const *)((char *)widget + WIDGET_DATA);
    mode  = *(const int32_t *)((const char *)widget + WIDGET_FONT_INDEX);
    slot  = (frame != NULL && mode >= 2) ? preview_upscale(frame) : NULL;
    if (slot == NULL) {
        original(widget, menu);
        return;
    }

    memcpy(saved, frame + VBUFFER_WIDTH, sizeof saved);
    saved_pixels = *(uint8_t **)(frame + VBUFFER_PIXELS);

    *(int32_t *)(frame + VBUFFER_WIDTH)          = slot->width;
    *(int32_t *)(frame + VBUFFER_HEIGHT)         = slot->height;
    *(int32_t *)(frame + VBUFFER_SIZE)           = (int32_t)slot->pixel_bytes;
    *(int32_t *)(frame + VBUFFER_BYTES_PER_LINE) = slot->bytes_per_line;
    *(int32_t *)(frame + VBUFFER_PITCH_PIXELS)   = slot->bytes_per_line / slot->format.bytes_per_pixel;
    *(uint8_t **)(frame + VBUFFER_PIXELS)        = slot->pixels;

    original(widget, menu);

    memcpy(frame + VBUFFER_WIDTH, saved, sizeof saved);
    *(uint8_t **)(frame + VBUFFER_PIXELS) = saved_pixels;
}
