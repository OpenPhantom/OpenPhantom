#include "menu_art_census.h"

#include "menu_scale_sites.h"

#include "common/detour.h"
#include "common/ini.h"
#include "common/logging.h"
#include "common/memory.h"
#include "common/signature.h"

#include <stddef.h>

/* The surface header fields this reads. The same five menu_preview.c saves and restores around its
 * own swap, named again here rather than shared, because a census that reached into another file's
 * private offsets would make that file harder to change than it is. */
#define VBUFFER_WIDTH          0x0Cu
#define VBUFFER_HEIGHT         0x10u
#define VBUFFER_BYTES_PER_LINE 0x18u
#define VBUFFER_BITS_PER_PIXEL 0x24u
#define VBUFFER_PIXELS         0x5Cu

/* Sixty four distinct shapes. A menu screen has a few dozen widgets and most screens share most of
 * their furniture, so a walk through the whole front end has never needed more; the count of shapes
 * that did not fit is reported rather than silently dropped, so a full table cannot look like a
 * complete answer. */
#define CENSUS_MAX 64u

typedef struct census_entry {
    int32_t  mode;
    int32_t  width;
    int32_t  height;
    int32_t  bits_per_pixel;
    uint32_t seen;
} census_entry_t;

static struct {
    census_entry_t entries[CENSUS_MAX];
    uint32_t       count;
    uint32_t       overflowed;
    uint32_t       screens;
    uint32_t       widgets_without_a_picture;
    uint32_t       not_a_surface;
    uint32_t       sprites;
    uint32_t       sprite_shapes;
    int32_t        sprite_widest;
    int32_t        sprite_tallest;
    bool           armed;
} census;

typedef void(__cdecl *pic_draw_fn_t)(void *widget, void *menu);
typedef int32_t(__cdecl *menu_open_fn_t)(void *menu);

static detour_t pic_draw_detour;
static detour_t menu_open_detour;

static void __cdecl census_pic_draw(void *widget, void *menu)
{
    pic_draw_fn_t original = (pic_draw_fn_t)pic_draw_detour.original;

    if (widget != NULL) {
        menu_art_census_note(widget, *(void *const *)((char *)widget + WIDGET_DATA));
    }
    if (original != NULL) {
        original(widget, menu);
    }
}

static int32_t __cdecl census_menu_open(void *menu)
{
    menu_open_fn_t original = (menu_open_fn_t)menu_open_detour.original;

    menu_art_census_note_screen();
    return (original != NULL) ? original(menu) : 0;
}

bool menu_art_census_install(void)
{
    /* Reads its own key, the way the other self-contained pieces in this DLL do, because
     * enhanced_resolution.c is at its size limit and a setting only one file ever looks at does
     * not need to travel through the file that reads every other setting. */
    if (!ini_read_bool("enhanced_resolution", "LogMenuArt", false)) {
        return false;
    }

    /* The site table belongs to the menu scale and is resolved by it. Resolving it again is
     * idempotent and costs one scan, and it is what lets this run on an installation where the
     * scale itself never installs. */
    signature_resolve_table(menu_scale_sites, SITE_COUNT);
    if (menu_scale_sites[SITE_PIC_DRAW].address == 0 ||
        menu_scale_sites[SITE_MENU_OPEN].address == 0) {
        log_warning("LogMenuArt=1, but the menu draw sites did not resolve, so nothing can be "
                    "counted");
        return false;
    }
    if (!detour_install(&pic_draw_detour, menu_scale_sites[SITE_PIC_DRAW].address,
                        (const void *)census_pic_draw, PIC_DRAW_PROLOGUE) ||
        !detour_install(&menu_open_detour, menu_scale_sites[SITE_MENU_OPEN].address,
                        (const void *)census_menu_open, MENU_OPEN_PROLOGUE)) {
        log_warning("LogMenuArt=1, but the menu draw sites could not be detoured, so nothing can "
                    "be counted");
        return false;
    }

    census.armed = true;
    log_info("LogMenuArt=1: every distinct menu picture will be reported once, with the blit path "
             "it takes, and a summary follows the eighth screen. This counts what PICTURE WIDGETS "
             "draw and nothing else, so a screen whose reported shapes do not add up to what is on "
             "it is itself the finding. A diagnostic, so it ships off.");
    return true;
}

static void report(void);

/* texture_drawSprite(texture, xL, xR, yT, yB, colour, fill). The destination edges are FLOATS and
 * carry no relationship to the texture's own size, so this path already stretches whatever it is
 * given: a quad is a quad. That is the opposite of the widget blit, which copies one source pixel
 * to one destination pixel and is the reason the artwork is upscaled on disk.
 *
 * Which of the two carries the menu's actual furniture is the question the widget census could not
 * answer, having found only the two shapes already known about across eight screens while five
 * thousand widgets drew no picture at all. Counting the quads answers it from the other side: a
 * menu built from sprites shows hundreds a screen, and one that merely garnishes a blitted picture
 * with a halo or two shows a handful. */
void menu_art_census_note_sprite(float left, float right, float top, float bottom)
{
    int32_t width  = (int32_t)(right - left);
    int32_t height = (int32_t)(bottom - top);

    if (!census.armed) {
        return;
    }
    census.sprites++;
    if (width > census.sprite_widest) {
        census.sprite_widest = width;
    }
    if (height > census.sprite_tallest) {
        census.sprite_tallest = height;
    }
}

/* Eight screens. The front end, the options page and its two or three children, a pause page and
 * the load screen is about that, so by then a walk has seen the furniture. Reported once: a
 * summary that repeated would bury the per-shape lines above it, which are the actual answer. */
#define REPORT_AFTER_SCREENS 8u

void menu_art_census_note_screen(void)
{
    if (!census.armed) {
        return;
    }
    census.screens++;
    if (census.screens == REPORT_AFTER_SCREENS) {
        report();
    }
}

/* Compressed or raw, in the engine's own words rather than ours: swpic_blit reads this same field
 * to choose between the run length blitter and the plain surface copy. */
static const char *path_of(int32_t mode)
{
    return (mode >= 2) ? "raw, plain surface copy" : "compressed, swrle_blit";
}

void menu_art_census_note(const void *widget, const void *frame)
{
    int32_t  mode;
    int32_t  width;
    int32_t  height;
    int32_t  bits;
    uint32_t at;

    if (!census.armed || widget == NULL) {
        return;
    }
    if (frame == NULL) {
        census.widgets_without_a_picture++;
        return;
    }
    if (!memory_is_readable_range((uintptr_t)frame, VBUFFER_PIXELS + sizeof(void *))) {
        return;
    }

    mode   = *(const int32_t *)((const char *)widget + WIDGET_FONT_INDEX);
    width  = *(const int32_t *)((const char *)frame + VBUFFER_WIDTH);
    height = *(const int32_t *)((const char *)frame + VBUFFER_HEIGHT);
    bits   = *(const int32_t *)((const char *)frame + VBUFFER_BITS_PER_PIXEL);

    /* A pointer being READABLE is not a pointer being a surface, and taking the first for the
     * second put "1065353216x1065353216 at 414518224 bits" in a log. That width is 0x3F800000,
     * which is 1.0f: the field a widget keeps a picture in holds other things on other widget
     * kinds, and this had read a structure of floats. So the numbers are checked for being
     * plausible ones before they are believed. */
    if (width <= 0 || height <= 0 || width > 16384 || height > 16384 ||
        (bits != 8 && bits != 16 && bits != 24 && bits != 32)) {
        census.not_a_surface++;
        return;
    }

    for (at = 0; at < census.count; ++at) {
        if (census.entries[at].mode == mode && census.entries[at].width == width &&
            census.entries[at].height == height) {
            census.entries[at].seen++;
            return;                                   /* seen before, and already reported */
        }
    }
    if (census.count >= CENSUS_MAX) {
        census.overflowed++;
        return;
    }

    census.entries[census.count].mode           = mode;
    census.entries[census.count].width          = width;
    census.entries[census.count].height         = height;
    census.entries[census.count].bits_per_pixel = bits;
    census.entries[census.count].seen           = 1u;
    census.count++;

    log_info("menu picture %dx%d at %d bits, blit mode %d: %s",
             (int)width, (int)height, (int)bits, (int)mode, path_of(mode));
}

static void report(void)
{
    uint32_t at;
    uint32_t raw = 0;
    uint32_t compressed = 0;

    if (!census.armed) {
        return;
    }
    if (census.count == 0u) {
        log_warning("the menu art census saw no pictures at all across %u screens. Either no menu "
                    "was opened, or menu artwork does not reach the screen through picture "
                    "widgets, which is itself the answer this was measuring for.",
                    (unsigned)census.screens);
        return;
    }

    log_info("menu art census, %u screens opened, %u distinct picture shapes:",
             (unsigned)census.screens, (unsigned)census.count);
    for (at = 0; at < census.count; ++at) {
        const census_entry_t *e = &census.entries[at];

        if (e->mode >= 2) {
            raw++;
        } else {
            compressed++;
        }
        log_info("  %5dx%-5d %2d bits  mode %d  drawn %u times  %s",
                 (int)e->width, (int)e->height, (int)e->bits_per_pixel, (int)e->mode,
                 (unsigned)e->seen, path_of(e->mode));
    }
    log_info("  %u shapes take the raw path, which is the one a draw time upscaler can already "
             "reach, and %u take the compressed path, which has no scale term in it. %u widgets "
             "carried no picture at all, and %u held something in that field that was not a "
             "surface.",
             (unsigned)raw, (unsigned)compressed,
             (unsigned)census.widgets_without_a_picture, (unsigned)census.not_a_surface);
    log_info("  textured sprites drawn: %u, the largest %dx%d. Those are quads with float edges, so "
             "that path scales whatever it is handed and needs no bigger source to fill a bigger "
             "canvas. A large number here against the handful of widget pictures above means the "
             "menu's furniture is drawn as quads, and the artwork is converted on disk for "
             "sharpness rather than for geometry.",
             (unsigned)census.sprites, (int)census.sprite_widest, (int)census.sprite_tallest);
    if (census.overflowed != 0u) {
        log_warning("  and %u further shapes did not fit the table, so this list is not the whole "
                    "of what was drawn", (unsigned)census.overflowed);
    }
}
