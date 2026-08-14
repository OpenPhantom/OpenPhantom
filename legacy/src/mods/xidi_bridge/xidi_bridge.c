/* xidi_bridge.c: the game asks WinMM about the joystick, and Windows will not let it ask ours.
 *
 * ==============================================================================================
 * THE DEFECT
 *
 * The controller wrapper is a stand-in for winmm.dll placed beside the executable. That is the
 * ordinary way to wrap a system library: for a static import the loader searches the directory the
 * application was loaded from before it searches the system directory, so the copy in the game
 * folder wins.
 *
 * For this executable it does not. The module list of the running game reads:
 *
 *     winmm.dll     C:\WINDOWS\SYSTEM32\WINMM.dll
 *     Xidi.32.dll   not loaded
 *     DDRAW.dll     <game>\DDRAW.dll
 *     DINPUT.dll    <game>\DINPUT.dll
 *     DSOUND.dll    <game>\DSOUND.dll
 *     mss32.dll     <game>\mss32.dll
 *     binkw32.dll   <game>\binkw32.dll
 *     iMUSE.DLL     <game>\iMUSE.DLL
 *
 * Every other contested name comes out of the game folder. That one does not, and because the
 * wrapper is never mapped, the wrapper's own core library is never loaded either. The game then
 * asks a WinMM that knows nothing about XInput, gets no device, and reports that no controller is
 * connected.
 *
 * It is not the ordinary search order. A probe executable that imports the same three functions,
 * built and run from that same folder, loads the game folder's winmm.dll and is answered by the
 * wrapper. It is specific to this executable. Ruled out one at a time, each by a control run:
 *
 *   - the file name            a probe renamed to the game's own executable name still gets ours
 *   - the DPI compatibility layer, applied to this executable and inherited through __COMPAT_LAYER
 *   - the PE version fields    a probe rewritten to required OS 4.0 and subsystem 4.0 still gets ours
 *   - bound imports            the executable has no bound import directory
 *   - a manifest               it has none, and there is no external manifest beside it
 *   - the KnownDLLs list       winmm.dll is not on it, which the probe above also demonstrates
 *   - the graphics wrapper     a probe that imports ddraw, so that the wrapper starts up first and
 *                              loads winmm itself, is still answered by ours
 *   - our own libraries        the executable's import of winmm is resolved before any DllMain runs
 *   - DotLocal redirection     neither the file form nor the directory form of <exe>.local changes it
 *
 * The compatibility engine is in the process: apphelp.dll and AcGenral.DLL are both mapped, and a
 * compatibility fix for this executable is independently visible elsewhere, where it intercepts
 * RegisterRawInputDevices and fails it. Which entry of that database redirects the name is not
 * proven here, and this fix does not need it to be: it sidesteps the name instead.
 *
 * ==============================================================================================
 * THE FIX
 *
 * The wrapper is installed under a name nothing redirects and loaded from here by full path. Then
 * three pointers in the executable's import address table are rewritten to the wrapper's own
 * exports:
 *
 *     joyGetNumDevs      how many joystick slots exist
 *     joyGetPosEx        axes, buttons and hat
 *     joyGetDevCapsA     the device's ranges and capabilities
 *
 * Those three are the whole joystick surface the executable imports. The other six names it takes
 * from WinMM are four aux entry points, mciSendCommandA and timeGetTime, none of which a controller
 * wrapper has any business answering.
 *
 * Rewriting three slots rather than replacing the library is also the better arrangement in its own
 * right. Miles takes 35 names out of WinMM, iMUSE six and Bink one; replacing the library puts a
 * wrapper in front of the entire audio engine, and every one of those 43 calls then depends on it
 * forwarding faithfully. This way they keep reaching the system library directly and the wrapper
 * answers exactly the three questions it exists for.
 *
 * ==============================================================================================
 * WHY THIS IS SAFE TO REPEAT
 *
 * Each slot is validated before it is written: the address currently in it must lie inside the
 * module that is answering to winmm.dll at that moment. After a successful run it holds an address
 * inside the wrapper instead, so a second run finds a value outside that range and declines. The
 * same check refuses to fight anything else that has already redirected these imports.
 *
 * If the game did reach a winmm.dll from its own folder after all, on a machine where the
 * redirection does not happen, nothing is rewritten. That case is recognised by the path of the
 * loaded module rather than guessed at.
 */
#include "xidi_bridge.h"

#include "wrapper_path.h"

#include "common/host_image.h"
#include "common/ini.h"
#include "common/logging.h"
#include "common/patch.h"

#include <windows.h>

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <string.h>

#define XIDI_SECTION       "xidi_bridge"
#define SYSTEM_LIBRARY     "winmm.dll"
#define DEFAULT_LIBRARY    "xidi_winmm.dll"

/* Every offset below is a 32-bit import table in a 32-bit process. */
_Static_assert(sizeof(void *) == 4, "xidi_bridge assumes a 32-bit host");
_Static_assert(sizeof(IMAGE_THUNK_DATA32) == 4, "Unexpected import thunk size");

static const char *const JOYSTICK_IMPORTS[] = {
    "joyGetNumDevs",
    "joyGetPosEx",
    "joyGetDevCapsA"
};

#define JOYSTICK_IMPORT_COUNT (sizeof(JOYSTICK_IMPORTS) / sizeof(JOYSTICK_IMPORTS[0]))

typedef struct module_range {
    uintptr_t base;
    uintptr_t end;
} module_range_t;

/* Address range of a mapped module, read out of its own PE header. */
static bool module_extent(HMODULE module, module_range_t *out_range)
{
    const IMAGE_DOS_HEADER *dos_header = (const IMAGE_DOS_HEADER *)module;
    const IMAGE_NT_HEADERS *nt_headers;

    if (module == NULL || dos_header->e_magic != IMAGE_DOS_SIGNATURE) {
        return false;
    }
    nt_headers = (const IMAGE_NT_HEADERS *)((const uint8_t *)module + dos_header->e_lfanew);
    if (nt_headers->Signature != IMAGE_NT_SIGNATURE) {
        return false;
    }

    out_range->base = (uintptr_t)module;
    out_range->end  = out_range->base + nt_headers->OptionalHeader.SizeOfImage;
    return true;
}

/* The import descriptor for `dll_name`, or NULL. The name in the table is whatever the linker
 * wrote in 1999, so the comparison is case insensitive. */
static const IMAGE_IMPORT_DESCRIPTOR *find_import_descriptor(const char *dll_name)
{
    uintptr_t                      base = host_image_base();
    const IMAGE_DOS_HEADER        *dos_header = (const IMAGE_DOS_HEADER *)base;
    const IMAGE_NT_HEADERS        *nt_headers;
    const IMAGE_DATA_DIRECTORY    *directory;
    const IMAGE_IMPORT_DESCRIPTOR *descriptor;

    if (base == 0) {
        return NULL;
    }
    nt_headers = (const IMAGE_NT_HEADERS *)(base + dos_header->e_lfanew);
    directory = &nt_headers->OptionalHeader.DataDirectory[IMAGE_DIRECTORY_ENTRY_IMPORT];
    if (directory->VirtualAddress == 0 || directory->Size == 0) {
        return NULL;
    }

    descriptor = (const IMAGE_IMPORT_DESCRIPTOR *)(base + directory->VirtualAddress);
    for (; descriptor->Name != 0; ++descriptor) {
        const char *name = (const char *)(base + descriptor->Name);
        if (_stricmp(name, dll_name) == 0) {
            return descriptor;
        }
    }
    return NULL;
}

/* Rewrites the one address table slot belonging to `function_name`.
 *
 * The name table and the address table are two parallel arrays: the first still holds the names
 * the linker wrote, the second held them too until the loader overwrote each entry with the
 * resolved address. Walking them together is what turns a name into the slot to write. An
 * executable whose name table was stripped cannot be walked this way, and this refuses rather than
 * counting slots and hoping. */
static bool rewrite_slot(const IMAGE_IMPORT_DESCRIPTOR *descriptor, const char *function_name,
                         const module_range_t *expected_range, FARPROC replacement)
{
    uintptr_t                 base = host_image_base();
    const IMAGE_THUNK_DATA32 *name_thunk;
    IMAGE_THUNK_DATA32       *address_thunk;

    if (descriptor->OriginalFirstThunk == 0) {
        log_error("the import name table is absent, %s cannot be located by name", function_name);
        return false;
    }

    name_thunk    = (const IMAGE_THUNK_DATA32 *)(base + descriptor->OriginalFirstThunk);
    address_thunk = (IMAGE_THUNK_DATA32 *)(base + descriptor->FirstThunk);

    for (; name_thunk->u1.AddressOfData != 0; ++name_thunk, ++address_thunk) {
        const IMAGE_IMPORT_BY_NAME *imported;
        uintptr_t                   current;

        /* An import by ordinal carries no name, so it can never be the one we want. */
        if (IMAGE_SNAP_BY_ORDINAL32(name_thunk->u1.Ordinal)) {
            continue;
        }

        imported = (const IMAGE_IMPORT_BY_NAME *)(base + name_thunk->u1.AddressOfData);
        if (strcmp((const char *)imported->Name, function_name) != 0) {
            continue;
        }

        current = (uintptr_t)address_thunk->u1.Function;
        if (current < expected_range->base || current >= expected_range->end) {
            log_warning("%s points at %08X, which is outside the library answering to %s "
                        "(%08X-%08X). Something else redirected it, so it is left alone.",
                        function_name, (unsigned)current, SYSTEM_LIBRARY,
                        (unsigned)expected_range->base, (unsigned)expected_range->end);
            return false;
        }

        if (patch_write_pointer32((uintptr_t)&address_thunk->u1.Function,
                                  (const void *)(uintptr_t)replacement) != PATCH_RESULT_OK) {
            log_error("the address table slot for %s could not be written", function_name);
            return false;
        }

        log_info("%s: %08X -> %08X", function_name, (unsigned)current, (unsigned)(uintptr_t)replacement);
        return true;
    }

    log_warning("%s is not imported from %s by this build, nothing to redirect",
                function_name, SYSTEM_LIBRARY);
    return false;
}

/* Loads the wrapper from the game folder by full path.
 *
 * The full path is the point. A bare name would be resolved by the same search this fix exists to
 * work around, and would also let a file somewhere else on the search path answer instead. */
static HMODULE load_wrapper(const char *file_name)
{
    char    path[MAX_PATH];
    HMODULE module;

    if (!wrapper_path_join(host_directory(), file_name, path, sizeof(path))) {
        log_error("the path to %s does not fit in MAX_PATH", file_name);
        return NULL;
    }

    module = LoadLibraryA(path);
    if (module == NULL) {
        log_warning("%s is not there or could not be loaded (error %lu). Controller support is "
                    "not installed, everything else is unaffected.", path, GetLastError());
        return NULL;
    }

    log_info("loaded %s", path);
    return module;
}

static void bridge_joystick_imports(void)
{
    char                           library[MAX_PATH];
    char                           system_path[MAX_PATH];
    HMODULE                        system_module;
    HMODULE                        wrapper;
    module_range_t                 system_range;
    const IMAGE_IMPORT_DESCRIPTOR *descriptor;
    unsigned                       redirected = 0;
    size_t                         index;

    /* Whatever is answering to the system name right now is what the address table points into,
     * and its path is what says whether this fix has anything to do. */
    system_module = GetModuleHandleA(SYSTEM_LIBRARY);
    if (system_module == NULL) {
        log_warning("%s is not loaded in this process, so there is nothing to redirect",
                    SYSTEM_LIBRARY);
        return;
    }
    if (GetModuleFileNameA(system_module, system_path, MAX_PATH) == 0) {
        log_error("the path of the loaded %s could not be read", SYSTEM_LIBRARY);
        return;
    }
    system_path[MAX_PATH - 1] = '\0';

    if (wrapper_path_inside(host_directory(), system_path)) {
        log_info("%s already came from the game folder, so the imports need no help", system_path);
        return;
    }
    if (!module_extent(system_module, &system_range)) {
        log_error("%s does not look like a mapped image", system_path);
        return;
    }
    log_info("the game was given %s, which knows nothing about XInput", system_path);

    ini_read_string(XIDI_SECTION, "Library", DEFAULT_LIBRARY, library, sizeof(library));
    wrapper = load_wrapper(library);
    if (wrapper == NULL) {
        return;
    }

    descriptor = find_import_descriptor(SYSTEM_LIBRARY);
    if (descriptor == NULL) {
        log_error("this build does not import %s at all, nothing was changed", SYSTEM_LIBRARY);
        return;
    }

    for (index = 0; index < JOYSTICK_IMPORT_COUNT; ++index) {
        FARPROC replacement = GetProcAddress(wrapper, JOYSTICK_IMPORTS[index]);

        if (replacement == NULL) {
            log_error("%s does not export %s, so it is not a WinMM stand-in",
                      library, JOYSTICK_IMPORTS[index]);
            continue;
        }
        if (rewrite_slot(descriptor, JOYSTICK_IMPORTS[index], &system_range, replacement)) {
            ++redirected;
        }
    }

    if (redirected != JOYSTICK_IMPORT_COUNT) {
        log_warning("%u of %u joystick imports were redirected. The game reads all three and gives "
                    "up on the first that fails, so controller support stays off.",
                    redirected, (unsigned)JOYSTICK_IMPORT_COUNT);
        return;
    }

    /* Asking the question the game asks first, so the log distinguishes a fix that installed from
     * one that installed and works. This is also what loads the wrapper's own core library, which
     * it does on the first joystick call rather than when it is mapped. */
    {
        typedef UINT(WINAPI * joy_get_num_devs_fn)(void);
        joy_get_num_devs_fn num_devs =
            (joy_get_num_devs_fn)GetProcAddress(wrapper, "joyGetNumDevs");

        log_info("all three joystick imports now go to %s, which reports %u joystick slots",
                 library, num_devs != NULL ? num_devs() : 0u);
    }
}

void xidi_bridge_install(void)
{
    static bool installed;

    log_init("xidi_bridge", false);

    if (installed) {
        return;
    }
    if (!host_image_resolve()) {
        log_error("no 32-bit host image, nothing redirected");
        return;
    }
    if (!ini_read_bool(XIDI_SECTION, "Enabled", true)) {
        log_info("Enabled=0, the joystick imports keep going to the system library");
        return;
    }

    installed = true;
    bridge_joystick_imports();
}
