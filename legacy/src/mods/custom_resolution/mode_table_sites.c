/* mode_table_sites.c: the four signatures custom_resolution.c needs, resolved once at install.
 *
 * See custom_resolution.c's own header comment for the full mechanism these sites belong to; this
 * file is only the byte evidence and the addresses it yields.
 */
#include "mode_table_sites.h"

#include "common/logging.h"
#include "common/memory.h"
#include "common/signature.h"

#include <stddef.h>
#include <stdint.h>

/* --- 0x0046C592  graphics_buildModeList (function start) --------------------------------------
 * Thirty three bytes, disassembled directly, not decompiled text:
 *
 *   0046c592  55                       push ebp
 *   0046c593  8B EC                    mov ebp,esp
 *   0046c595  83 EC 14                 sub esp,0x14                       <- prologue ends here,
 *                                                                            six bytes, a clean
 *                                                                            instruction boundary
 *   0046c598  56                       push esi
 *   0046c599  57                       push edi
 *   0046c59a  83 3D F4 77 4B 00 00     cmp dword ptr [0x004b77f4],0       <- the device index
 *   0046c5a1  7D 05                    jge +5
 *   0046c5a3  E9 B6 01 00 00           jmp 0x0046c75e
 *   0046c5a8  A1 F4 77 4B 00           mov eax,[0x004b77f4]
 *   0046c5ad  8B 0D 14 20 86 00        mov ecx,[0x00862014]               <- the raw record count
 *
 * The two distinct literal globals this reaches (the device-index cell and the raw record count)
 * are what make thirty three bytes unique rather than the six-byte prologue alone, which several
 * neighbouring MSVC functions share. Six is still what gets relocated into the trampoline: the
 * remaining twenty seven bytes exist for the search, not for the detour. */
static const uint8_t SIG_BUILD_MODE_LIST[] = {
    0x55, 0x8B, 0xEC, 0x83, 0xEC, 0x14, 0x56, 0x57,
    0x83, 0x3D, 0xF4, 0x77, 0x4B, 0x00, 0x00,
    0x7D, 0x05,
    0xE9, 0xB6, 0x01, 0x00, 0x00,
    0xA1, 0xF4, 0x77, 0x4B, 0x00,
    0x8B, 0x0D, 0x14, 0x20, 0x86, 0x00
};

/* --- 0x0046BE3D  graphics_setResolution (function start), and 0x0046C6D9  the 4:3 gate --------
 * Both copied verbatim from enhanced_resolution's own enhanced_resolution.c, byte for byte: two
 * independent DLLs resolving the same site is exactly what common/detour.h's chaining exists for,
 * and matching bytes rather than reusing a pointer is what keeps this DLL independent of whether
 * enhanced_resolution.dll is even installed (see cheats_openphantom.c's own SIG_CAMERA_VIEW for
 * the same reasoning applied to a different pair of features). If enhanced_resolution.dll has
 * already patched the aspect gate, this DLL's own attempt below finds the new bytes instead of
 * the expected old ones and declines with a log line - the gate is already lifted either way, so
 * that is a correct outcome, not a failure needing a special case. */
static const uint8_t SIG_SET_RESOLUTION[] = {
    0x55, 0x8B, 0xEC, 0x83, 0xEC, 0x5C, 0x83, 0x3D, 0x60, 0x63, 0x6D, 0x00, 0x00,
    0x75, 0x07, 0x33, 0xC0, 0xE9, 0x49, 0x01
};
#define SET_RESOLUTION_PROLOGUE_SIZE 6u

static const uint8_t SIG_ASPECT_GATE[] = {
    0x81, 0x7D, 0xF4, 0x00, 0x05, 0x00, 0x00, 0x75, 0x09,
    0x81, 0x7D, 0xEC, 0x00, 0x04, 0x00, 0x00, 0x74, 0x22
};
const uint8_t MODE_TABLE_ASPECT_GATE_PATCH[2] = { 0xEB, 0x32 };

/* --- 0x004928FC  the DirectDraw enumeration callback, read only, never detoured here -----------
 * Copied verbatim from enhanced_resolution's own mode_filter.c, which already measured this
 * unique in every shipped image and already extracts exactly the two addresses this file also
 * needs: the raw table's own base and its live record count. Resolved with
 * signature_find_detour_target() rather than signature_find_unique() ON PURPOSE, even though
 * nothing here installs a detour on it: if mode_filter.c's OWN detour is already sitting on this
 * site (FilterModeEnumeration=1 is its default), a plain literal search would find a `jmp` where
 * it expects the real prologue and fail outright. signature_find_detour_target() recognises that
 * shape and still resolves the site correctly, and the operand offsets read below are all well
 * past the six bytes any detour here would ever overwrite, so reading them is safe regardless of
 * whether mode_filter.c got here first. */
static const uint8_t SIG_ENUM_CALLBACK[] = {
    0x55, 0x8B, 0xEC, 0x83, 0xEC, 0x18, 0x56,
    0x83, 0x3D, 0x00, 0x00, 0x00, 0x00, 0x40,
    0x0F, 0x83, 0x00, 0x00, 0x00, 0x00,
    0xA1, 0x00, 0x00, 0x00, 0x00,
    0x6B, 0xC0, 0x54,
    0x05, 0x00, 0x00, 0x00, 0x00
};
static const uint8_t MSK_ENUM_CALLBACK[] = {
    0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF,
    0xFF, 0xFF, 0x00, 0x00, 0x00, 0x00, 0xFF,
    0xFF, 0xFF, 0x00, 0x00, 0x00, 0x00,
    0xFF, 0x00, 0x00, 0x00, 0x00,
    0xFF, 0xFF, 0xFF,
    0xFF, 0x00, 0x00, 0x00, 0x00
};
#define ENUM_CALLBACK_PROLOGUE 6u
#define OFFSET_COUNT_OPERAND   9u
#define OFFSET_CAP_IMMEDIATE   13u
#define OFFSET_STRIDE_BYTE     27u
#define OFFSET_TABLE_OPERAND   29u
#define EXPECTED_TABLE_CAP     64u
#define EXPECTED_RECORD_STRIDE 0x54u

static void resolve_mode_table(mode_table_sites_t *out)
{
    uintptr_t site;
    uint32_t  table_address = 0;
    uint32_t  count_address = 0;
    uint8_t   cap = 0;
    uint8_t   stride = 0;

    site = signature_find_detour_target(SIG_ENUM_CALLBACK, MSK_ENUM_CALLBACK,
                                        sizeof SIG_ENUM_CALLBACK, ENUM_CALLBACK_PROLOGUE);
    if (site == 0) {
        log_warning("the display mode enumeration callback did not resolve, no custom resolution "
                    "can be added");
        return;
    }
    if (!memory_read_u32(site + OFFSET_COUNT_OPERAND, &count_address) ||
        !memory_read_u32(site + OFFSET_TABLE_OPERAND, &table_address) ||
        !memory_read_u8(site + OFFSET_CAP_IMMEDIATE, &cap) ||
        !memory_read_u8(site + OFFSET_STRIDE_BYTE, &stride)) {
        log_warning("the callback's own operands could not be read at %08X, refused",
                    (unsigned)site);
        return;
    }
    if (cap != EXPECTED_TABLE_CAP || stride != EXPECTED_RECORD_STRIDE) {
        log_warning("the mode table reads as %u records of 0x%02X bytes, not %u of 0x%02X, so "
                    "this is not the table this file was measured against, refused",
                    (unsigned)cap, (unsigned)stride, (unsigned)EXPECTED_TABLE_CAP,
                    (unsigned)EXPECTED_RECORD_STRIDE);
        return;
    }
    if (!memory_is_inside_image(count_address, sizeof(uint32_t)) ||
        !memory_is_inside_image(table_address, (size_t)cap * stride)) {
        log_warning("the counter at %08X or the table at %08X lies outside the host image, "
                    "refused", (unsigned)count_address, (unsigned)table_address);
        return;
    }

    out->table_address  = (uintptr_t)table_address;
    out->count_address  = (uintptr_t)count_address;
    out->table_resolved = true;
    log_info("mode table resolved: %u records of 0x%02X bytes at %08X, count at %08X",
             (unsigned)cap, (unsigned)stride, (unsigned)table_address, (unsigned)count_address);
}

void mode_table_sites_resolve(mode_table_sites_t *out)
{
    out->build_mode_list_site = 0;
    out->set_resolution_site  = 0;
    out->aspect_gate_site     = 0;
    out->table_address        = 0;
    out->count_address        = 0;
    out->table_resolved       = false;

    resolve_mode_table(out);

    out->build_mode_list_site =
        signature_find_detour_target(SIG_BUILD_MODE_LIST, NULL, sizeof SIG_BUILD_MODE_LIST,
                                     BUILD_MODE_LIST_PROLOGUE_SIZE);
    if (out->build_mode_list_site == 0) {
        log_warning("graphics_buildModeList did not resolve, the custom resolution cannot be "
                    "added to the mode table at all");
    }

    out->set_resolution_site =
        signature_find_detour_target(SIG_SET_RESOLUTION, NULL, sizeof SIG_SET_RESOLUTION,
                                     SET_RESOLUTION_PROLOGUE_SIZE);
    if (out->set_resolution_site == 0) {
        log_warning("graphics_setResolution did not resolve - the custom resolution will still "
                    "be offered in the options screen if it made it into the mode table, but it "
                    "will not be applied automatically at startup");
    }

    out->aspect_gate_site = signature_find_unique(SIG_ASPECT_GATE, NULL, sizeof SIG_ASPECT_GATE);
    if (out->aspect_gate_site == 0) {
        log_info("the 4:3 aspect gate did not resolve as expected - either an unsupported build, "
                 "or another mod (enhanced_resolution's own WidescreenModes) already lifted it, "
                 "which is harmless either way. A non-4:3 custom resolution may still not appear "
                 "in the options screen's own list if that is the reason");
    }
}
