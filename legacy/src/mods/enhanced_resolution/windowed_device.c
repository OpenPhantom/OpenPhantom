#include "windowed_device.h"

#include "common/frame_hook.h"
#include "common/logging.h"
#include "common/memory.h"
#include "common/patch.h"
#include "common/signature.h"

#include <stdint.h>

/* --- 0x00492C43  stdDisplay_ddSetMode(param, pMode, palette) --------------------------------
 *
 *   55                             push ebp
 *   8B EC                          mov  ebp,esp
 *   81 EC 28 04 00 00              sub  esp,0x428
 *   53 56 57                       push ebx/esi/edi
 *   C7 85 EC FB FF FF 11 00 00 00  mov  [ebp-0x414],0x11      <- the byte this file writes
 *
 * Address free, no wildcards, and unique in all three shipped builds at this length, checked
 * offline. The cooperative flags are the last four bytes of the pattern rather than something
 * found by walking forward from the entry, so the byte being written is inside the byte range that
 * was matched.
 *
 * The function is NOT detoured. One byte inside it is rewritten and the engine's own code then
 * does everything else, which is the smallest change that reaches the cause. It also leaves the
 * prologue untouched, so anything that does want to detour this function still can.
 *
 * The courtesy is one directional, and that is worth saying rather than leaving to be discovered.
 * This pattern begins at the entry and is not declared as a detour target, so a DLL that detours
 * this function BEFORE this one installs replaces the first five bytes with a jump and the pattern
 * then matches zero times. The feature switches itself off with a message that reads like an
 * unsupported executable. Nothing in this tree touches 0x00492C43 today; if anything ever does,
 * this entry has to be anchored past the prologue instead. */
static const uint8_t SIG_DD_SET_MODE[] = {
    0x55,
    0x8B, 0xEC,
    0x81, 0xEC, 0x28, 0x04, 0x00, 0x00,
    0x53, 0x56, 0x57,
    0xC7, 0x85, 0xEC, 0xFB, 0xFF, 0xFF, 0x11, 0x00, 0x00, 0x00
};

/* The 0x11 is the nineteenth byte of that pattern. Written as an offset into the pattern rather
 * than as an address, so the two cannot drift apart. */
#define COOPERATIVE_FLAGS_OFFSET 18u

/* Named for what they are rather than after the DDSCL_ constants they encode. DDSCL_NORMAL is a
 * real macro in the Windows SDK's ddraw.h, and defining it here would become a C4005 redefinition,
 * and therefore an error under /W4 /WX, the moment anything in this DLL's include chain pulled that
 * header in. Nothing does today, which is exactly the sort of thing that changes quietly. */
#define COOP_FLAGS_EXCLUSIVE 0x11u   /* DDSCL_FULLSCREEN|DDSCL_EXCLUSIVE, what the engine ships */
#define COOP_FLAGS_NORMAL    0x08u   /* DDSCL_NORMAL, what this writes                          */

/* --- the two surface record stores inside the same function -------------------------------
 *
 *   C7 05 <slot> <record+0x64>     mov dword [slot], record+0x64
 *
 * Read only to REPORT, never to write. The engine keeps two sizes for each surface and only one of
 * them is ever true: the DDSURFACEDESC is refilled from the real surface by GetSurfaceDesc, while
 * the record's own width and height are copied from the mode table once and never refreshed. Every
 * draw uses the second. If a windowed device hands back a surface that is not the mode's size, the
 * picture is written into a corner of it at the right stride and the rest is whatever was there,
 * and nothing in the engine notices. Printing both is the only way to tell that apart from a
 * present that is not reaching the window at all. */
static const uint8_t SIG_RECORD_STORE_OPCODE[] = { 0xC7, 0x05 };

/* The descriptor begins two dwords past the cell the store names, and inside it dwHeight is at
 * +0x08 and dwWidth at +0x0C. So from the stored value V: height at V+0x10, width at V+0x14. */
#define DESC_FROM_SLOT      0x08u
#define DESC_OFF_HEIGHT     0x08u
#define DESC_OFF_WIDTH      0x0Cu

enum {
    SITE_DD_SET_MODE,
    SITE_COUNT
};

static signature_t sites[SITE_COUNT] = {
    SIGNATURE_ENTRY("dd_set_mode", SIG_DD_SET_MODE)
};

static uint32_t *primary_desc;
static uint32_t *back_desc;
static bool      surfaces_reported;

/* Reports what the device actually produced, once, a few frames in so the device exists. */
static void report_surfaces(void)
{
    if (surfaces_reported || primary_desc == NULL || back_desc == NULL) {
        return;
    }
    if (!memory_is_readable_range((uintptr_t)primary_desc, 0x20u) ||
        !memory_is_readable_range((uintptr_t)back_desc, 0x20u)) {
        return;
    }
    surfaces_reported = true;
    log_info("windowed device surfaces: primary %ux%u, back buffer %ux%u, both read back from the "
             "surfaces themselves. Compare these against the resolution the game is set to. If "
             "they are the desktop's size rather than the game's, the picture is being drawn into "
             "a corner of a larger surface and everything outside it is whatever was there, "
             "because the engine draws from a size it copied out of the mode table and never "
             "refreshes.",
             (unsigned)primary_desc[DESC_OFF_WIDTH / 4], (unsigned)primary_desc[DESC_OFF_HEIGHT / 4],
             (unsigned)back_desc[DESC_OFF_WIDTH / 4], (unsigned)back_desc[DESC_OFF_HEIGHT / 4]);
}

static void resolve_surface_records(uintptr_t function)
{
    const size_t span = 0x400u;
    size_t       found = 0;
    size_t       offset;

    for (offset = 0; offset + 10u <= span; offset++) {
        uintptr_t at = function + offset;
        uint32_t  value;

        if (!memory_is_readable_range(at, 10u) ||
            *(const uint8_t *)at != SIG_RECORD_STORE_OPCODE[0] ||
            *(const uint8_t *)(at + 1) != SIG_RECORD_STORE_OPCODE[1]) {
            continue;
        }
        value = *(const uint32_t *)(at + 6);
        if (!memory_is_readable_range((uintptr_t)value, DESC_FROM_SLOT + 0x20u)) {
            continue;
        }
        if (found == 0) {
            primary_desc = (uint32_t *)(uintptr_t)(value + DESC_FROM_SLOT);
        } else if (found == 1) {
            back_desc = (uint32_t *)(uintptr_t)(value + DESC_FROM_SLOT);
        }
        found++;
    }
    if (found != 2) {
        primary_desc = NULL;
        back_desc = NULL;
        log_warning("the two surface records could not be located, so the sizes the device really "
                    "produced will not be reported");
    }
}

bool windowed_device_install(const windowed_device_config_t *config)
{
    static const uint8_t expect_exclusive[1] = { COOP_FLAGS_EXCLUSIVE };
    static const uint8_t expect_normal[1]    = { COOP_FLAGS_NORMAL };
    uintptr_t site;
    uintptr_t flags_at;

    if (config == NULL) {
        return false;
    }
    if (!config->enabled) {
        log_info("WindowedPresent=0, the device is built the way the engine builds it, owning the "
                 "display. That is the shipped default, and it is why switching away costs a "
                 "device reset and a re-upload of every texture.");
        return false;
    }

    signature_resolve_table(sites, SITE_COUNT);
    site = sites[SITE_DD_SET_MODE].address;
    if (site == 0) {
        log_warning("stdDisplay_ddSetMode did not resolve, so the device keeps the engine's own "
                    "exclusive cooperative level");
        return false;
    }

    flags_at = site + COOPERATIVE_FLAGS_OFFSET;

    /* Read back before writing. The pattern makes this redundant on a first install and it is kept
     * because it is what makes the write idempotent: a second install finds 0x08 rather than 0x11
     * and declines instead of writing again. */
    if (!patch_validate_bytes(flags_at, expect_exclusive, sizeof expect_exclusive)) {
        if (patch_validate_bytes(flags_at, expect_normal, sizeof expect_normal)) {
            log_info("the cooperative level at %08X is already DDSCL_NORMAL, so this is a second "
                     "install and there is nothing to do", (unsigned)flags_at);
            return true;
        }
        log_warning("the cooperative flags at %08X are neither the engine's 0x11 nor our 0x08, so "
                    "something else has written them and this leaves them alone",
                    (unsigned)flags_at);
        return false;
    }

    if (patch_write_u8(flags_at, COOP_FLAGS_NORMAL) != PATCH_RESULT_OK) {
        log_warning("the cooperative level could not be written at %08X, so the device keeps the "
                    "engine's exclusive one", (unsigned)flags_at);
        return false;
    }

    resolve_surface_records(site);
    if (primary_desc != NULL && !frame_hook_add(report_surfaces)) {
        log_warning("the frame hook was refused, so the surface sizes will not be reported");
    }

    log_info("windowed device armed: the cooperative flags at %08X are 0x08 instead of 0x11, so "
             "the engine asks for DDSCL_NORMAL and the `or ah,8` below it still adds "
             "DDSCL_FPUSETUP. Everything else is the engine's own: it still sets the display mode, "
             "still creates a PRIMARYSURFACE|3DDEVICE|COMPLEX|FLIP primary with a back buffer, "
             "still takes its render target from GetAttachedSurface, and still presents with Flip. "
             "Alt-tab should cost nothing now, because there is no exclusive mode to lose.",
             (unsigned)flags_at);
    return true;
}
