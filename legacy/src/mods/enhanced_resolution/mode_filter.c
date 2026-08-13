/* mode_filter.c: keep the engine's 64 mode slots for modes it can actually use.
 *
 * ==============================================================================================
 * THE CEILING, and why it is not the one everybody looks at
 *
 * The options screen shows what graphics_buildModeList produced, and that list is capped by this
 * DLL at MaxMenuModes. Every field report about missing resolutions gets blamed on that cap, and
 * it is almost never the cause. The real ceiling is two steps earlier, inside the enumeration
 * callback the engine hands to DirectDraw:
 *
 *   004928FC  55 8B EC 83 EC 18 56   prologue
 *   00492903  83 3D <count>, 0x40    cmp the number of recorded modes against 64
 *   0049290A  0F 83 ...              jae -> return 0 = DDENUMRET_CANCEL
 *   00492910  A1 <count>             and otherwise
 *   00492915  6B C0 54               index it into a table of 0x54-byte records
 *   00492918  05 <table>             at <table>
 *
 * Sixty four records, and the enumeration is CANCELLED once they are full. Everything the driver
 * would have offered after that never arrives.
 *
 * The callback records every bit depth it is given. It reads the pixel format flags at desc+0x4C
 * and stores kind 1 for DDPF_RGB, and the bit count from desc+0x54 (0x004929AC onward). The list
 * builder then keeps only what it can use:
 *
 *   0046C666  83 78 1C 01            kind must be 1
 *   0046C671  83 79 20 10            bit count must be exactly 16
 *
 * So on a driver that reports 8, 16 and 32 bit, two of every three recorded slots are spent on
 * modes that are thrown away later, and the 64 run out after roughly 21 usable resolutions. Which
 * 21 depends on the order the driver enumerates in, which is why the list differs from machine to
 * machine and why one user sees every resolution and the next sees a third of them.
 *
 * ==============================================================================================
 * WHAT THIS DOES
 *
 * It answers the callback itself for the modes that cannot survive the later test, and returns
 * DDENUMRET_OK without letting them consume a slot:
 *
 *   not DDPF_RGB, or a bit count other than 16   the list builder would reject it anyway
 *   a resolution already held by a USABLE record the list builder would show it twice
 *
 * Everything else is passed to the engine's own callback unchanged. Nothing is invented, no mode
 * the engine would have kept is dropped, and the enumeration is never cancelled early by us.
 *
 * "Usable" is doing real work in that second line. A record can be 16-bit RGB and still be turned
 * away by graphics_findMode, which additionally wants five bits in each channel, so a duplicate is
 * only suppressed when the record already holding that size would satisfy that test too. Without
 * it a driver that offered one size twice, narrow first and 565 second, would end up with only
 * the narrow record and a resolution that no longer resolves at all.
 *
 * The duplicate test matters more than it looks. The engine asks for the enumeration with
 * dwFlags = 0 (0x0049251C, `6A 00`), so DDEDM_REFRESHRATES is off and a well behaved driver
 * reports each resolution once per bit depth. A wrapper that ignores that flag reports it once
 * per refresh rate as well, and then a single resolution can eat a dozen slots on its own.
 *
 * ==============================================================================================
 * WHAT IT CANNOT DO
 *
 * Two ceilings sit between this and "every resolution Windows offers", and neither is ours:
 *
 *   the bit depth test itself. A wrapper that exposes no 16-bit modes at all leaves the list
 *   empty, and no filter can conjure one.
 *
 *   the video memory test at 0x0046C6AC, `width * height * 6 > free local video memory`, where
 *   the free figure comes from IDirectDraw::GetAvailableVidMem. A driver or wrapper that reports
 *   a small or fixed number silently deletes the large modes. The engine has its own switch for
 *   that, read at 0x0046B9F7: `allow_all_modes` in the `[options]` section of obi.ini zeroes the
 *   figure the test compares against, which turns the test off without any patch at all.
 */
#include "mode_filter.h"

#include "common/detour.h"
#include "common/logging.h"
#include "common/memory.h"
#include "common/signature.h"

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

/* --- 0x004928FC  the DirectDraw enumeration callback ------------------------------------------ *
 * Address free: the three operands are wildcarded and read back out of the match. The shape that
 * identifies it is "compare a counter against 64, give up if it is not below, otherwise index a
 * 0x54-byte record out of a table". Measured unique in every shipped image; in the Edit Tool's
 * recompile it sits at 0x0049289C with a different counter and a different table, which is the
 * whole reason none of the three may be written down here. */
static const uint8_t SIG_ENUM_CALLBACK[] = {
    0x55, 0x8B, 0xEC, 0x83, 0xEC, 0x18, 0x56,               /* prologue, then push esi        */
    0x83, 0x3D, 0x00, 0x00, 0x00, 0x00, 0x40,               /* cmp [count], 64                */
    0x0F, 0x83, 0x00, 0x00, 0x00, 0x00,                     /* jae -> cancel                  */
    0xA1, 0x00, 0x00, 0x00, 0x00,                           /* mov eax,[count]                */
    0x6B, 0xC0, 0x54,                                       /* imul eax,eax,0x54              */
    0x05, 0x00, 0x00, 0x00, 0x00                            /* add eax,<table>                */
};
static const uint8_t MSK_ENUM_CALLBACK[] = {
    0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF,
    0xFF, 0xFF, 0x00, 0x00, 0x00, 0x00, 0xFF,
    0xFF, 0xFF, 0x00, 0x00, 0x00, 0x00,
    0xFF, 0x00, 0x00, 0x00, 0x00,
    0xFF, 0xFF, 0xFF,
    0xFF, 0x00, 0x00, 0x00, 0x00
};
_Static_assert(sizeof(SIG_ENUM_CALLBACK) == sizeof(MSK_ENUM_CALLBACK),
               "the callback pattern and its mask are different lengths");

#define ENUM_CALLBACK_PROLOGUE 6u    /* push ebp / mov ebp,esp / sub esp,0x18 */
#define OFFSET_COUNT_OPERAND   9u
#define OFFSET_CAP_IMMEDIATE   13u
#define OFFSET_STRIDE_BYTE     27u
#define OFFSET_TABLE_OPERAND   29u

/* Read out of the match rather than written down, and then checked: a build whose table is not
 * laid out this way must not be filtered against the wrong numbers. */
#define EXPECTED_TABLE_CAP     64u
#define EXPECTED_RECORD_STRIDE 0x54u

/* Field offsets inside the record the callback fills, taken from its own stores at 0x00492929
 * onward: entry+0x08 = desc+0x0C = width, entry+0x0C = desc+0x08 = height. The three channel
 * widths at +0x24, +0x28 and +0x2C are written from the pixel format's own bit counts. */
#define RECORD_WIDTH        0x08u
#define RECORD_HEIGHT       0x0Cu
#define RECORD_RED_BITS     0x24u
#define RECORD_GREEN_BITS   0x28u
#define RECORD_BLUE_BITS    0x2Cu

/* graphics_findMode applies a test the list builder does not, and it is the reason a duplicate
 * may not be suppressed blindly:
 *
 *   0046BC5B  83 78 24 05   cmp [rec+0x24],5     ; red
 *   0046BC64  83 79 28 05   cmp [rec+0x28],5     ; green
 *   0046BC6D  83 7A 2C 05   cmp [rec+0x2C],5     ; blue
 *   0046BC7E  83 C8 FF      or eax,-1            ; below any of them: NOT FOUND
 *
 * So a record can be 16-bit RGB, survive the list builder, and still be unusable to the lookup
 * that resolves a saved resolution. If a driver offered the same size twice, once with a narrow
 * channel and once as 565, the engine would list both and this lookup would find the good one.
 * Suppressing the second as a duplicate would leave only the narrow record and turn a resolution
 * that worked into one that does not resolve at all. */
#define ENGINE_MIN_CHANNEL_BITS 5u

/* Field offsets inside the DDSURFACEDESC the driver hands in, taken from the engine's own reads:
 * 0x0049299E reads desc+0x4C for the pixel format flags, 0x004929BC reads desc+0x54 for the bit
 * count, and 0x00492929 / 0x00492935 read desc+0x0C and desc+0x08 for the two dimensions. */
#define DESC_HEIGHT       0x08u
#define DESC_WIDTH        0x0Cu
#define DESC_FORMAT_FLAGS 0x4Cu
#define DESC_BIT_COUNT    0x54u

#define DDPF_RGB          0x40u
#define ENGINE_BIT_COUNT  16u

/* DDENUMRET_OK. Returning this without calling the engine's callback is exactly "I have seen this
 * mode, carry on", which is what skipping means here. */
#define DDENUMRET_OK 1

typedef int32_t (__stdcall *enum_callback_fn_t)(void *desc, void *context);

typedef struct mode_filter_state {
    bool                installed;
    bool                active;
    detour_t            detour;

    const volatile uint32_t *count;    /* how many records the engine has taken */
    uintptr_t                table;
    uint32_t                 cap;
    uint32_t                 stride;

    uint32_t recorded;
    uint32_t skipped_format;
    uint32_t skipped_duplicate;
    bool     logged_saturated;
} mode_filter_state_t;

static mode_filter_state_t filter_state;

/* True when this width and height is already in the table AND the record that holds it is one the
 * engine's own lookup will accept. The table is the engine's own and is only ever grown by the
 * callback we sit in front of, and the counter is incremented last, after every field of a record
 * is written, so everything below the count is complete.
 *
 * The channel test is what makes "nothing the engine would have kept is dropped" true rather than
 * nearly true. A record too narrow for graphics_findMode does not suppress anything: the next
 * copy of that resolution is allowed through, which is exactly what the engine relies on.
 *
 * The reads are the faulting kind. The table was range checked once at install, so asking the
 * operating system again on every one of up to 64 records, on every mode the driver offers, would
 * be a system call per read for no answer that is not already known. */
static bool already_recorded(uint32_t width, uint32_t height)
{
    uint32_t taken = *filter_state.count;
    uint32_t index;

    if (taken > filter_state.cap) {
        return false;                  /* not a table we understand any more, do not judge */
    }
    for (index = 0; index < taken; ++index) {
        uintptr_t record = filter_state.table + (uintptr_t)index * filter_state.stride;
        uint32_t  w = 0;
        uint32_t  h = 0;
        uint32_t  red = 0;
        uint32_t  green = 0;
        uint32_t  blue = 0;

        if (!memory_try_read(record + RECORD_WIDTH, &w, sizeof w) ||
            !memory_try_read(record + RECORD_HEIGHT, &h, sizeof h)) {
            return false;
        }
        if (w != width || h != height) {
            continue;
        }
        if (!memory_try_read(record + RECORD_RED_BITS, &red, sizeof red) ||
            !memory_try_read(record + RECORD_GREEN_BITS, &green, sizeof green) ||
            !memory_try_read(record + RECORD_BLUE_BITS, &blue, sizeof blue)) {
            return false;
        }
        if (red >= ENGINE_MIN_CHANNEL_BITS && green >= ENGINE_MIN_CHANNEL_BITS &&
            blue >= ENGINE_MIN_CHANNEL_BITS) {
            return true;
        }
        /* Too narrow for the engine's own lookup, so it suppresses nothing and a later copy of
         * the same size has to be allowed through. */
        return false;
    }
    return false;
}

static int32_t __stdcall hook_enum_callback(void *desc, void *context)
{
    enum_callback_fn_t original = (enum_callback_fn_t)filter_state.detour.original;
    uint32_t flags = 0;
    uint32_t bits = 0;
    uint32_t width = 0;
    uint32_t height = 0;

    if (original == NULL) {
        return DDENUMRET_OK;            /* the un-armed instant; keep enumerating regardless */
    }
    if (!filter_state.active || desc == NULL) {
        return original(desc, context);
    }

    /* The descriptor belongs to the driver, not to this image, so every read of it is the
     * faulting kind. Anything unreadable is not ours to judge and goes to the engine. */
    if (!memory_try_read((uintptr_t)desc + DESC_FORMAT_FLAGS, &flags, sizeof flags) ||
        !memory_try_read((uintptr_t)desc + DESC_BIT_COUNT, &bits, sizeof bits) ||
        !memory_try_read((uintptr_t)desc + DESC_WIDTH, &width, sizeof width) ||
        !memory_try_read((uintptr_t)desc + DESC_HEIGHT, &height, sizeof height)) {
        return original(desc, context);
    }

    if ((flags & DDPF_RGB) == 0u || bits != ENGINE_BIT_COUNT) {
        ++filter_state.skipped_format;
        return DDENUMRET_OK;
    }
    if (already_recorded(width, height)) {
        ++filter_state.skipped_duplicate;
        return DDENUMRET_OK;
    }

    ++filter_state.recorded;
    if (!filter_state.logged_saturated && *filter_state.count + 1u >= filter_state.cap) {
        filter_state.logged_saturated = true;
        log_warning("the engine's mode table is FULL at %u records even after filtering, so the "
                    "driver is offering more than %u distinct 16-bit resolutions and the rest are "
                    "not being enumerated. The list in the options screen is complete only up to "
                    "this point.", (unsigned)filter_state.cap, (unsigned)filter_state.cap);
    }
    return original(desc, context);
}

/* ============================================================================================ */
bool mode_filter_install(bool enabled)
{
    uintptr_t site;
    uint32_t  count_address = 0;
    uint32_t  table_address = 0;
    uint8_t   cap = 0;
    uint8_t   stride = 0;

    if (filter_state.installed) {
        return filter_state.active;
    }
    filter_state.installed = true;

    if (!enabled) {
        log_info("FilterModeEnumeration=0, every bit depth the driver reports takes one of the "
                 "engine's 64 mode records, and the enumeration stops when they are gone. On a "
                 "driver that reports 8, 16 and 32 bit that is about a third of the resolutions "
                 "your monitor can do.");
        return false;
    }

    site = signature_find_detour_target(SIG_ENUM_CALLBACK, MSK_ENUM_CALLBACK,
                                        sizeof SIG_ENUM_CALLBACK, ENUM_CALLBACK_PROLOGUE);
    if (site == 0) {
        log_warning("the display mode enumeration callback did not resolve, the mode list is "
                    "whatever the driver's own enumeration order left room for");
        return false;
    }

    if (!memory_read_u32(site + OFFSET_COUNT_OPERAND, &count_address) ||
        !memory_read_u32(site + OFFSET_TABLE_OPERAND, &table_address) ||
        !memory_read_u8(site + OFFSET_CAP_IMMEDIATE, &cap) ||
        !memory_read_u8(site + OFFSET_STRIDE_BYTE, &stride)) {
        log_warning("the callback's own operands could not be read at %08X, refused",
                    (unsigned)site);
        return false;
    }
    if (cap != EXPECTED_TABLE_CAP || stride != EXPECTED_RECORD_STRIDE) {
        log_warning("the mode table reads as %u records of 0x%02X bytes, not %u of 0x%02X, so "
                    "this is not the table this filter was measured against, refused",
                    (unsigned)cap, (unsigned)stride,
                    (unsigned)EXPECTED_TABLE_CAP, (unsigned)EXPECTED_RECORD_STRIDE);
        return false;
    }
    if (!memory_is_inside_image(count_address, sizeof(uint32_t)) ||
        !memory_is_inside_image(table_address, (size_t)cap * stride)) {
        log_warning("the counter at %08X or the table at %08X lies outside the host image, "
                    "refused", (unsigned)count_address, (unsigned)table_address);
        return false;
    }

    filter_state.count  = (const volatile uint32_t *)(uintptr_t)count_address;
    filter_state.table  = (uintptr_t)table_address;
    filter_state.cap    = cap;
    filter_state.stride = stride;

    if (!detour_install(&filter_state.detour, site, (const void *)hook_enum_callback,
                        ENUM_CALLBACK_PROLOGUE)) {
        log_warning("the enumeration callback at %08X could not be detoured, the mode list is "
                    "whatever the driver's enumeration order left room for", (unsigned)site);
        return false;
    }

    filter_state.active = true;
    log_info("display mode enumeration filtered at %08X (counter %08X, table %08X, %u records of "
             "0x%02X): only 16-bit RGB modes take a record, and a resolution already in the table "
             "does not take a second one. Without this, every bit depth the driver reports spends "
             "records the mode list will reject later, and the enumeration is cancelled once the "
             "%u are gone.",
             (unsigned)site, (unsigned)count_address, (unsigned)table_address,
             (unsigned)cap, (unsigned)stride, (unsigned)cap);
    return true;
}

void mode_filter_log_summary(void)
{
    if (!filter_state.active) {
        return;
    }
    if (filter_state.recorded == 0u && filter_state.skipped_format == 0u &&
        filter_state.skipped_duplicate == 0u) {
        /* Installed, and never asked a single question. The enumeration runs once, inside graphics
         * startup, so this means it had already happened by the time this DLL was loaded. The
         * install line above says the filter is in force, and on its own it would be a success
         * line for a patch that did nothing. */
        log_warning("the mode filter saw no modes at all, so the enumeration had already run "
                    "before this DLL was loaded and the mode list is whatever it left. The "
                    "loader's early trigger is what normally puts us in front of it.");
        return;
    }
    log_info("mode enumeration: %u record(s) kept, %u skipped for bit depth or format, %u skipped "
             "as a resolution already recorded. Every one of those skips is a record the engine "
             "would otherwise have spent on a mode the options screen cannot show.",
             (unsigned)filter_state.recorded, (unsigned)filter_state.skipped_format,
             (unsigned)filter_state.skipped_duplicate);
}
