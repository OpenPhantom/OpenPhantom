/* multi_instance.c: the engine's single-instance guard, and a class name of its own per copy.
 *
 * ==============================================================================================
 * The guard
 *
 * wkernel_createWindow (retail 0x00498F24) opens with:
 *
 *     0x00498F2A  6A 00               push 0
 *     0x00498F2C  68 D8 A5 4B 00      push "wKernel"
 *     0x00498F31  FF 15 68 16 8C 00   call [FindWindowA]
 *     0x00498F37  85 C0               test eax,eax
 *     0x00498F39  74 1E               je   create
 *     0x00498F3B  68 00 20 04 00      push 0x42000
 *     0x00498F40  68 C8 A5 4B 00      push "wKernel error"
 *     0x00498F45  68 AC A5 4B 00      push "Multiple instance detected"
 *     0x00498F4A  6A 00               push 0
 *     0x00498F4C  FF 15 80 16 8C 00   call [MessageBoxA]
 *     0x00498F52  33 C0               xor  eax,eax
 *
 * The class string lives in .data and is the only copy: RegisterClassA (through the WNDCLASS the
 * function fills at 0x00498F5F) and CreateWindowExA (0x00499013) push the same address. Changing
 * one character of it therefore renames the class for all three calls at once, and the guard of a
 * second copy finds nothing to refuse. No code byte is touched, so no other module's pattern over
 * this function is disturbed.
 *
 * The pattern starts at the `push 0`, six bytes in: multiplayer.dll detours the function's
 * prologue, and whichever of the two loads first, the bytes from here on are the engine's own.
 *
 * ==============================================================================================
 * Which copy renames
 *
 * Only a copy that starts while another one's window already stands. The first copy keeps the
 * engine's own name, so a single game on a machine runs exactly as it always has. A later copy
 * takes the first of wKerne2 .. wKerne9 that no window carries yet, which covers a four player
 * session with room to spare. The decision is made when this module installs, before WinMain,
 * against the windows that stand at that moment; a launcher that waits for each window before
 * starting the next copy makes it deterministic.
 *
 * Nothing in this project looks the game's window up by class: the multiplayer bridge finds it as
 * the visible window of the game's thread for exactly this reason.
 */
#include "multi_instance.h"

#include "joystick_select.h"

#include "common/host_image.h"
#include "common/ini.h"
#include "common/logging.h"
#include "common/patch.h"
#include "common/signature.h"

#include <windows.h>

#include <stdint.h>
#include <string.h>

#define MULTI_SECTION       "multi_instance"
#define CLASS_OPERAND       3u
#define CLASS_NAME_MAX      64u

static const uint8_t SIG_INSTANCE_GUARD[42] = {
    0x6A, 0x00, 0x68, 0xD8, 0xA5, 0x4B, 0x00, 0xFF, 0x15, 0x68, 0x16, 0x8C,
    0x00, 0x85, 0xC0, 0x74, 0x1E, 0x68, 0x00, 0x20, 0x04, 0x00, 0x68, 0xC8,
    0xA5, 0x4B, 0x00, 0x68, 0xAC, 0xA5, 0x4B, 0x00, 0x6A, 0x00, 0xFF, 0x15,
    0x80, 0x16, 0x8C, 0x00, 0x33, 0xC0
};
static const uint8_t MSK_INSTANCE_GUARD[42] = {
    0xFF, 0xFF, 0xFF, 0x00, 0x00, 0x00, 0x00, 0xFF, 0xFF, 0x00, 0x00, 0x00,
    0x00, 0xFF, 0xFF, 0xFF, 0x00, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0x00,
    0x00, 0x00, 0x00, 0xFF, 0x00, 0x00, 0x00, 0x00, 0xFF, 0xFF, 0xFF, 0xFF,
    0x00, 0x00, 0x00, 0x00, 0xFF, 0xFF
};

static bool installed;

/* The class name the guard pushes, read out of its operand and checked to lie inside the image
 * and to end within CLASS_NAME_MAX bytes before anything trusts it. */
static char *find_class_name(void)
{
    uintptr_t site = signature_find_unique(SIG_INSTANCE_GUARD, MSK_INSTANCE_GUARD,
                                           sizeof SIG_INSTANCE_GUARD);
    uintptr_t name;

    if (site == 0) {
        log_warning("the single-instance guard was not found exactly once, nothing changed");
        return NULL;
    }
    memcpy(&name, (const void *)(site + CLASS_OPERAND), sizeof name);
    if (name < host_image_base() || name + CLASS_NAME_MAX > host_image_end() ||
        memchr((const void *)name, '\0', CLASS_NAME_MAX) == NULL) {
        log_warning("the guard at %08X names %08X, which is no string of this image, nothing "
                    "changed", (unsigned)site, (unsigned)name);
        return NULL;
    }
    return (char *)name;
}

void multi_instance_install(void)
{
    char   candidate[CLASS_NAME_MAX];
    char  *class_name;
    size_t last;
    char   digit;

    log_init("multi_instance", false);

    if (installed) {
        return;
    }
    if (!host_image_resolve()) {
        log_error("no 32-bit host image, nothing patched");
        return;
    }
    if (!ini_read_bool(MULTI_SECTION, "Enabled", true)) {
        log_info("Enabled=0, a second copy on this machine is refused by the game as before");
        return;
    }
    installed = true;

    joystick_select_install(ini_read_int(MULTI_SECTION, "Joystick", -1));

    class_name = find_class_name();
    if (class_name == NULL) {
        return;
    }
    if (FindWindowA(class_name, NULL) == NULL) {
        log_info("the first copy on this machine: the window class keeps its name '%s'",
                 class_name);
        return;
    }

    last = strlen(class_name) - 1u;
    memcpy(candidate, class_name, last + 2u);
    for (digit = '2'; digit <= '9'; ++digit) {
        candidate[last] = digit;
        if (FindWindowA(candidate, NULL) != NULL) {
            continue;
        }
        if (patch_write_bytes((uintptr_t)(class_name + last), &digit, 1u) != PATCH_RESULT_OK) {
            log_error("could not rename the window class, the game will refuse this copy");
            return;
        }
        log_info("another copy is running: this one's window class is '%s', so the game's own "
                 "guard lets it start", class_name);
        return;
    }
    log_warning("eight copies are already running, the game will refuse this one");
}
