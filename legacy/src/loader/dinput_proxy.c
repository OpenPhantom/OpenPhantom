#include "dinput_proxy.h"

#include "mod_loader.h"

#include "common/host_image.h"
#include "common/ini.h"
#include "common/logging.h"

#include <windows.h>

#include <stdbool.h>
#include <stddef.h>
#include <stdio.h>
#include <string.h>

/* Only DirectInputCreateA is used by this game. The other six exist so that anything else in the
 * process that binds to a dinput.dll still finds what it expects. */
#pragma comment(linker, "/EXPORT:DirectInputCreateA=_proxy_direct_input_create_a@16")
#pragma comment(linker, "/EXPORT:DirectInputCreateW=_proxy_direct_input_create_w@16")
#pragma comment(linker, "/EXPORT:DirectInputCreateEx=_proxy_direct_input_create_ex@20")
#pragma comment(linker, "/EXPORT:DllCanUnloadNow=_proxy_dll_can_unload_now@0,PRIVATE")
#pragma comment(linker, "/EXPORT:DllGetClassObject=_proxy_dll_get_class_object@12,PRIVATE")
#pragma comment(linker, "/EXPORT:DllRegisterServer=_proxy_dll_register_server@0,PRIVATE")
#pragma comment(linker, "/EXPORT:DllUnregisterServer=_proxy_dll_unregister_server@0,PRIVATE")

#define LOADER_SECTION       "loader"
#define CHAIN_FALLBACK_NAME  "dinput_orig.dll"
#define CHAIN_SYSTEM_NAME    "dinput.dll"
#define PROXY_E_FAIL         ((HRESULT)0x80004005L)

typedef HRESULT (WINAPI *direct_input_create_fn_t)(HINSTANCE, DWORD, void **, void *);
typedef HRESULT (WINAPI *direct_input_create_ex_fn_t)(HINSTANCE, DWORD, const void *, void **,
                                                      void *);
typedef HRESULT (WINAPI *no_argument_fn_t)(void);
typedef HRESULT (WINAPI *get_class_object_fn_t)(const void *, const void *, void **);

static HMODULE chain_module;
static bool    chain_attempted;

static bool try_load_chain(const char *path, const char *how)
{
    if (path == NULL || path[0] == '\0') {
        return false;
    }
    if (GetFileAttributesA(path) == INVALID_FILE_ATTRIBUTES) {
        return false;
    }

    chain_module = LoadLibraryA(path);
    if (chain_module == NULL) {
        log_warning("chain: %s exists but LoadLibrary failed (%lu): %s",
                    how, (unsigned long)GetLastError(), path);
        return false;
    }

    log_info("chain: forwarding dinput exports to %s (%s)", path, how);
    return true;
}

static void build_game_relative(const char *name, char *buffer, size_t buffer_size)
{
    _snprintf(buffer, buffer_size, "%s%s", host_directory(), name);
    buffer[buffer_size - 1] = '\0';
}

void dinput_proxy_open_chain(void)
{
    char configured[MAX_PATH];
    char candidate[MAX_PATH];
    UINT length;

    if (chain_attempted) {
        return;
    }
    chain_attempted = true;

    /* 1. an explicit ChainDll, absolute or relative to the game folder */
    if (ini_read_string(LOADER_SECTION, "ChainDll", "", configured, sizeof(configured))
        && configured[0] != '\0') {
        if (strchr(configured, ':') != NULL || configured[0] == '\\') {
            if (try_load_chain(configured, "ChainDll")) {
                return;
            }
        } else {
            build_game_relative(configured, candidate, sizeof(candidate));
            if (try_load_chain(candidate, "ChainDll")) {
                return;
            }
        }
        log_warning("chain: ChainDll=%s could not be loaded, falling back", configured);
    }

    /* 2. the conventional renamed original next to the game */
    build_game_relative(CHAIN_FALLBACK_NAME, candidate, sizeof(candidate));
    if (try_load_chain(candidate, "renamed original")) {
        return;
    }

    /* 3. the system dinput */
    length = GetSystemDirectoryA(candidate, MAX_PATH);
    if (length == 0 || length > MAX_PATH - sizeof(CHAIN_SYSTEM_NAME) - 2) {
        log_error("chain: the system directory could not be determined, dinput calls will fail");
        return;
    }
    _snprintf(candidate + length, MAX_PATH - length, "\\%s", CHAIN_SYSTEM_NAME);
    candidate[MAX_PATH - 1] = '\0';
    if (try_load_chain(candidate, "system")) {
        return;
    }

    log_error("chain: no dinput.dll could be loaded. Input creation will return E_FAIL. "
              "If the game folder used to hold another dinput.dll, rename it to %s.",
              CHAIN_FALLBACK_NAME);
}

static FARPROC chain_procedure(const char *name)
{
    dinput_proxy_open_chain();
    if (chain_module == NULL) {
        return NULL;
    }
    return GetProcAddress(chain_module, name);
}

/* ---------------------------------------------------------------------------------------------
 * The fallback trigger. The mods are normally loaded before this, by early_trigger.c at the
 * host's entry point: graphics startup runs before input startup, and several patches have to be
 * in place before it. mod_loader_run_once() is idempotent, so this call costs nothing when the
 * early trigger worked and saves the run when it did not.
 * ------------------------------------------------------------------------------------------- */
HRESULT WINAPI proxy_direct_input_create_a(HINSTANCE instance, DWORD version, void **out_interface,
                                           void *outer)
{
    direct_input_create_fn_t forward;

    mod_loader_run_once();

    forward = (direct_input_create_fn_t)chain_procedure("DirectInputCreateA");
    return (forward != NULL) ? forward(instance, version, out_interface, outer) : PROXY_E_FAIL;
}

HRESULT WINAPI proxy_direct_input_create_w(HINSTANCE instance, DWORD version, void **out_interface,
                                           void *outer)
{
    direct_input_create_fn_t forward;

    forward = (direct_input_create_fn_t)chain_procedure("DirectInputCreateW");
    return (forward != NULL) ? forward(instance, version, out_interface, outer) : PROXY_E_FAIL;
}

HRESULT WINAPI proxy_direct_input_create_ex(HINSTANCE instance, DWORD version,
                                            const void *interface_id, void **out_interface,
                                            void *outer)
{
    direct_input_create_ex_fn_t forward;

    forward = (direct_input_create_ex_fn_t)chain_procedure("DirectInputCreateEx");
    return (forward != NULL)
         ? forward(instance, version, interface_id, out_interface, outer)
         : PROXY_E_FAIL;
}

HRESULT WINAPI proxy_dll_can_unload_now(void)
{
    no_argument_fn_t forward = (no_argument_fn_t)chain_procedure("DllCanUnloadNow");
    return (forward != NULL) ? forward() : PROXY_E_FAIL;
}

HRESULT WINAPI proxy_dll_get_class_object(const void *class_id, const void *interface_id,
                                          void **out_interface)
{
    get_class_object_fn_t forward;

    forward = (get_class_object_fn_t)chain_procedure("DllGetClassObject");
    return (forward != NULL) ? forward(class_id, interface_id, out_interface) : PROXY_E_FAIL;
}

HRESULT WINAPI proxy_dll_register_server(void)
{
    no_argument_fn_t forward = (no_argument_fn_t)chain_procedure("DllRegisterServer");
    return (forward != NULL) ? forward() : PROXY_E_FAIL;
}

HRESULT WINAPI proxy_dll_unregister_server(void)
{
    no_argument_fn_t forward = (no_argument_fn_t)chain_procedure("DllUnregisterServer");
    return (forward != NULL) ? forward() : PROXY_E_FAIL;
}
