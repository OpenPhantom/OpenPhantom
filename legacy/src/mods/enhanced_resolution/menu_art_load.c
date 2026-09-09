#include "menu_art_load.h"

#include "menu_art_resample.h"

#include "common/host_image.h"
#include "common/ini.h"
#include "common/logging.h"
#include "common/memory.h"
#include "common/patch.h"
#include "common/signature.h"

#include <stdint.h>
#include <stdio.h>

#include <windows.h>

/* --- 0x0045F8FB, inside the BBMP resource handler's LOAD arm --------------------------------
 *
 *   8B 45 F8    mov eax,[ebp-0x08]     the tBitmap just filled in
 *   8B 48 78    mov ecx,[eax+0x78]     its frame array
 *   8B 11       mov edx,[ecx]          frame 0, the surface ConvertTo16 made
 *   52          push edx               the argument
 *   E8 <rel32>  call swrle_compressVBuffer
 *
 * Nine bytes, no relative displacement in any of them, and unique unmasked in both shipped
 * WMAIN.EXE builds. The call this brackets is the one being redirected, so the pattern names the
 * site by what it does rather than by where it is. */
static const uint8_t SIG_LOAD_COMPRESS[] = {
    0x8B, 0x45, 0xF8, 0x8B, 0x48, 0x78, 0x8B, 0x11, 0x52
};
#define LOAD_COMPRESS_CALL_OFFSET 9u

/* --- 0x00495290  mem_alloc(size) ------------------------------------------------------------
 *
 *   55 8B EC          push ebp / mov ebp,esp
 *   83 EC 10          sub  esp,0x10
 *   A1 <cell>         mov  eax,[a counter]
 *   89 45 FC          mov  [ebp-4],eax
 *   8B 4D 08          mov  ecx,[ebp+8]       the size asked for
 *   83 C1 10          add  ecx,0x10          the header this file exists to respect
 *   51 E8             push ecx / call malloc
 *
 * The `add ecx,0x10` is the distinguishing part and is also the evidence for the header. */
static const uint8_t SIG_MEM_ALLOC[] = {
    0x55, 0x8B, 0xEC, 0x83, 0xEC, 0x10,
    0xA1, 0x00, 0x00, 0x00, 0x00,
    0x89, 0x45, 0xFC, 0x8B, 0x4D, 0x08, 0x83, 0xC1, 0x10, 0x51, 0xE8
};
static const uint8_t MSK_MEM_ALLOC[] = {
    0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF,
    0xFF, 0x00, 0x00, 0x00, 0x00,
    0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF
};
_Static_assert(sizeof(SIG_MEM_ALLOC) == sizeof(MSK_MEM_ALLOC),
               "the allocator pattern and its mask are different lengths");

/* --- 0x00495452  mem_free(block) ------------------------------------------------------------
 *
 * Thirty bytes, and it needs every one of them. The first twenty four are shared with a second
 * function twenty nine bytes earlier, which reads the same header and returns a field out of it, so
 * a shorter pattern matches twice and resolves to nothing. They part at the twenty eighth byte,
 * where this one begins `cmp dword [eax+8],0` and the other returns. */
static const uint8_t SIG_MEM_FREE[] = {
    0x55, 0x8B, 0xEC, 0x83, 0xEC, 0x08, 0x8B, 0x45, 0x08, 0x83, 0xE8, 0x10,
    0x89, 0x45, 0xF8, 0x8B, 0x4D, 0xF8, 0x8B, 0x51, 0x0C, 0x89, 0x55, 0xFC,
    0x8B, 0x45, 0xF8, 0x83, 0x78, 0x08
};

/* The surface header, the same fields menu_preview.c saves and restores around its own swap. */
#define VBUFFER_WIDTH          0x0Cu
#define VBUFFER_HEIGHT         0x10u
#define VBUFFER_SIZE           0x14u
#define VBUFFER_BYTES_PER_LINE 0x18u
#define VBUFFER_PITCH_PIXELS   0x1Cu
#define VBUFFER_BITS_PER_PIXEL 0x24u
#define VBUFFER_PIXELS         0x5Cu

/* The run length format carries a run in twelve bits, so a row wider than this cannot be encoded.
 * The encoder does not clamp: it writes `run & 0xFFF` while advancing by the whole run, so a wider
 * row silently desynchronises the stream rather than failing. This is the same ceiling
 * MENU_SCALE_MAX_RATIO already imposes on the canvas, restated here because this is the code that
 * would produce the offending row. */
#define RLE_MAX_ROW_PIXELS 4095

enum {
    SITE_LOAD_COMPRESS,
    SITE_MEM_ALLOC,
    SITE_MEM_FREE,
    SITE_COUNT
};

static signature_t sites[SITE_COUNT] = {
    SIGNATURE_ENTRY("BBMP load compress call", SIG_LOAD_COMPRESS),
    SIGNATURE_ENTRY_MASKED("mem_alloc", SIG_MEM_ALLOC, MSK_MEM_ALLOC),
    SIGNATURE_ENTRY("mem_free", SIG_MEM_FREE)
};

typedef void(__cdecl *compress_fn_t)(void *vbuffer);
typedef void *(__cdecl *mem_alloc_fn_t)(uint32_t size);
typedef void(__cdecl *mem_free_fn_t)(void *block);

static struct {
    compress_fn_t  original_compress;
    mem_alloc_fn_t engine_alloc;
    mem_free_fn_t  engine_free;
    float          ratio_x;
    float          ratio_y;
    uint32_t       resampled;
    uint32_t       declined;
    bool           armed;
    bool           reported;
} load_state;

/* True when this surface can be replaced, with the reason logged once for the first refusal of each
 * kind rather than per picture. A menu screen holds a few dozen bitmaps and a line each would bury
 * the log for a condition that is the same every time. */
static bool wanted(int32_t width, int32_t height, int32_t bits,
                   int32_t *out_width, int32_t *out_height)
{
    if (bits != 16) {
        return false;              /* the compressor and the blitter are both 16 bit only */
    }
    *out_width  = menu_art_resample_scaled(width, load_state.ratio_x);
    *out_height = menu_art_resample_scaled(height, load_state.ratio_y);

    if (*out_width <= width && *out_height <= height) {
        return false;              /* already the size it should be, which is a converted set */
    }
    if (*out_width > RLE_MAX_ROW_PIXELS) {
        return false;
    }
    return true;
}

/* The redirected call. Everything it does is undone by doing nothing: a refusal at any point leaves
 * the surface exactly as the engine built it and the original compressor still runs on it. */
static void __cdecl resample_then_compress(void *vbuffer)
{
    char     *surface = (char *)vbuffer;
    int32_t   width;
    int32_t   height;
    int32_t   bits;
    int32_t   new_width  = 0;
    int32_t   new_height = 0;
    uint16_t *pixels;
    uint16_t *replacement;

    if (!load_state.armed || surface == NULL ||
        !memory_is_readable_range((uintptr_t)surface, VBUFFER_PIXELS + sizeof(void *))) {
        goto compress;
    }

    width  = *(const int32_t *)(surface + VBUFFER_WIDTH);
    height = *(const int32_t *)(surface + VBUFFER_HEIGHT);
    bits   = *(const int32_t *)(surface + VBUFFER_BITS_PER_PIXEL);
    pixels = *(uint16_t **)(surface + VBUFFER_PIXELS);

    if (pixels == NULL || !wanted(width, height, bits, &new_width, &new_height)) {
        load_state.declined++;
        goto compress;
    }

    replacement = (uint16_t *)load_state.engine_alloc(
        (uint32_t)new_width * (uint32_t)new_height * sizeof(uint16_t));
    if (replacement == NULL) {
        load_state.declined++;
        goto compress;
    }

    if (!menu_art_resample_16(pixels, width, height,
                              *(const int32_t *)(surface + VBUFFER_PITCH_PIXELS),
                              replacement, new_width, new_height, new_width)) {
        load_state.engine_free(replacement);
        load_state.declined++;
        goto compress;
    }

    /* The header is rewritten to describe the new pixels BEFORE the old ones are released, so there
     * is no moment at which the surface points at freed memory. stdDisplay_createVBuffer pads no
     * rows, so the pitch is the width and the byte count is the product. */
    *(int32_t *)(surface + VBUFFER_WIDTH)          = new_width;
    *(int32_t *)(surface + VBUFFER_HEIGHT)         = new_height;
    *(int32_t *)(surface + VBUFFER_SIZE)           = new_width * new_height * 2;
    *(int32_t *)(surface + VBUFFER_BYTES_PER_LINE) = new_width * 2;
    *(int32_t *)(surface + VBUFFER_PITCH_PIXELS)   = new_width;
    *(uint16_t **)(surface + VBUFFER_PIXELS)       = replacement;
    load_state.engine_free(pixels);

    load_state.resampled++;
    if (!load_state.reported) {
        load_state.reported = true;
        log_info("menu artwork is being replicated as it loads: the first was %dx%d and is now "
                 "%dx%d. Whole pixels only, so every pixel drawn is one that was already in the "
                 "picture, keeping an exactly black pixel transparent to this engine "
                 "rather than turning it into a halo or a hole.",
                 (int)width, (int)height, (int)new_width, (int)new_height);
    }

compress:
    if (load_state.original_compress != NULL) {
        load_state.original_compress(vbuffer);
    }
}

/* The authored canvas every menu is laid out on, and the size the artwork on disk is authored at.
 * Named here rather than taken from menu_scale.h so this file does not depend on the scale it
 * serves; the two are checked against each other by the static assert below. */
#define AUTHORED_WIDTH  640
#define AUTHORED_HEIGHT 480

bool menu_art_load_display_ratio(float *out_ratio_x, float *out_ratio_y)
{
    char        path[MAX_PATH];
    const char *directory = host_directory();
    int32_t     width;
    int32_t     height;

    if (out_ratio_x == NULL || out_ratio_y == NULL || directory == NULL) {
        return false;
    }
    if (_snprintf(path, sizeof path, "%s\\obi.ini", directory) < 0) {
        return false;
    }
    path[sizeof path - 1] = 0;
    if (GetFileAttributesA(path) == INVALID_FILE_ATTRIBUTES) {
        return false;
    }

    width  = (int32_t)GetPrivateProfileIntA("options", "screen_width", 0, path);
    height = (int32_t)GetPrivateProfileIntA("options", "screen_height", 0, path);
    if (width <= AUTHORED_WIDTH || height <= AUTHORED_HEIGHT) {
        return false;              /* nothing readable, or a screen the canvas already fills */
    }

    *out_ratio_x = (float)width  / (float)AUTHORED_WIDTH;
    *out_ratio_y = (float)height / (float)AUTHORED_HEIGHT;
    return true;
}

bool menu_art_load_install(float ratio_x, float ratio_y)
{
    uintptr_t call_at;
    uintptr_t original = 0;

    signature_resolve_table(sites, SITE_COUNT);
    if (sites[SITE_LOAD_COMPRESS].address == 0 || sites[SITE_MEM_ALLOC].address == 0 ||
        sites[SITE_MEM_FREE].address == 0) {
        log_warning("the menu bitmap load path did not resolve, so artwork keeps the size it has "
                    "on disk and the menus need a converted set to fill the canvas");
        return false;
    }

    call_at = sites[SITE_LOAD_COMPRESS].address + LOAD_COMPRESS_CALL_OFFSET;
    if (!patch_read_call_target(call_at, &original) || original == 0) {
        log_warning("the call at %08X is not the one expected, so menu artwork is left alone",
                    (unsigned)call_at);
        return false;
    }

    load_state.original_compress = (compress_fn_t)original;
    load_state.engine_alloc      = (mem_alloc_fn_t)sites[SITE_MEM_ALLOC].address;
    load_state.engine_free       = (mem_free_fn_t)sites[SITE_MEM_FREE].address;
    load_state.ratio_x           = ratio_x;
    load_state.ratio_y           = ratio_y;
    load_state.armed             = true;

    if (patch_redirect_call(call_at, (const void *)resample_then_compress) != PATCH_RESULT_OK) {
        load_state.armed = false;
        log_warning("the menu bitmap load call could not be redirected, so artwork keeps its size "
                    "on disk");
        return false;
    }

    if (!(ratio_x > 1.0f) && !(ratio_y > 1.0f)) {
        log_info("the menu canvas is the artwork's own size, so nothing is replicated as it loads "
                 "yet. The redirect at %08X is armed all the same, so that a canvas which grows "
                 "later has somewhere to say so", (unsigned)call_at);
        return true;
    }

    log_info("menu artwork will be replicated to %.3f by %.3f as it loads, at the call at %08X. "
             "That is one of two callers of the compressor and the other one is the save game "
             "thumbnail, which must keep its own size, so the call is redirected rather than the "
             "function replaced. Buffers come from the engine's own allocator at %08X, because the "
             "compressor frees what it is handed and that allocator carries a header in front of "
             "every block.",
             (double)ratio_x, (double)ratio_y, (unsigned)call_at,
             (unsigned)sites[SITE_MEM_ALLOC].address);
    return true;
}

void menu_art_load_set_ratio(float ratio_x, float ratio_y)
{
    if (!load_state.armed) {
        return;
    }
    load_state.ratio_x = ratio_x;
    load_state.ratio_y = ratio_y;

    /* Said again for the new size, because the first one is the line a reader looks for when the
     * artwork comes out the wrong size and it would otherwise name a ratio no longer in use. */
    load_state.reported = false;
}
