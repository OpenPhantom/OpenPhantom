/* mp_signatures_chat.c: the engine's key handler, as a byte pattern. See the header.
 *
 * Unique in every image the offline verification covers. The four address bearing fields are
 * masked: the movie cell and the console cell, which are read, the jump out and the call of the
 * default path, which are relative.
 */
#include "mp_signatures_chat.h"

#include "common/logging.h"
#include "common/signature.h"

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

/* main_keyHook from its head to the operand of its console test. */
static const uint8_t SIG_MP_CHAT_KEY_HOOK[] = {
    0x55, 0x8B, 0xEC,                                      /* push ebp; mov ebp,esp   */
    0x83, 0xEC, 0x08,                                      /* sub esp,8               */
    0xC7, 0x45, 0xFC, 0x00, 0x00, 0x00, 0x00,              /* result = 0              */
    0x83, 0x3D, 0x00, 0x00, 0x00, 0x00, 0x00,              /* cmp [movie],0           */
    0x74, 0x07,                                            /* jz  +7                  */
    0x33, 0xC0,                                            /* xor eax,eax             */
    0xE9, 0x00, 0x00, 0x00, 0x00,                          /* jmp out                 */
    0x81, 0x7D, 0x0C, 0x00, 0x01, 0x00, 0x00,              /* cmp [ebp+0x0C],0x100    */
    0x74, 0x18,                                            /* jz  +0x18               */
    0x8B, 0x45, 0x14, 0x50,                                /* push lParam             */
    0x8B, 0x4D, 0x10, 0x51,                                /* push wParam             */
    0x8B, 0x55, 0x0C, 0x52,                                /* push msg                */
    0xE8, 0x00, 0x00, 0x00, 0x00,                          /* call the default path   */
    0x83, 0xC4, 0x0C,                                      /* add esp,0x0C            */
    0x33, 0xC0,                                            /* xor eax,eax             */
    0xEB, 0x6F,                                            /* jmp out                 */
    0x83, 0x3D, 0x00, 0x00, 0x00, 0x00, 0x00               /* cmp [the console],0     */
};
static const uint8_t MSK_MP_CHAT_KEY_HOOK[] = {
    0xFF, 0xFF, 0xFF,
    0xFF, 0xFF, 0xFF,
    0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF,
    0xFF, 0xFF, 0x00, 0x00, 0x00, 0x00, 0xFF,
    0xFF, 0xFF,
    0xFF, 0xFF,
    0xFF, 0x00, 0x00, 0x00, 0x00,
    0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF,
    0xFF, 0x00,
    0xFF, 0xFF, 0xFF, 0xFF,
    0xFF, 0xFF, 0xFF, 0xFF,
    0xFF, 0xFF, 0xFF, 0xFF,
    0xFF, 0x00, 0x00, 0x00, 0x00,
    0xFF, 0xFF, 0xFF,
    0xFF, 0xFF,
    0xFF, 0x00,
    0xFF, 0xFF, 0x00, 0x00, 0x00, 0x00, 0xFF
};
/* push ebp; mov ebp,esp; sub esp,8: three instructions, none of them relative. */
#define MP_CHAT_KEY_HOOK_PROLOGUE 6u

_Static_assert(sizeof(SIG_MP_CHAT_KEY_HOOK) == 69u && sizeof(MSK_MP_CHAT_KEY_HOOK) == 69u,
               "the key handler's pattern and its mask are the length its offsets count in");
_Static_assert(MP_CHAT_KEY_HOOK_MOVIE_CELL + 4u <= sizeof(SIG_MP_CHAT_KEY_HOOK) &&
                   MP_CHAT_KEY_HOOK_MOVIE_CELL >= MP_CHAT_KEY_HOOK_PROLOGUE,
               "the movie cell lies inside the pattern and behind the bytes a hook writes");

static signature_t chat_sites[MP_CHAT_SITE_COUNT] = {
    SIGNATURE_ENTRY_DETOUR_MASKED("chat_key_hook", SIG_MP_CHAT_KEY_HOOK, MSK_MP_CHAT_KEY_HOOK,
                                  MP_CHAT_KEY_HOOK_PROLOGUE)
};

static bool   resolved_once;
static size_t resolved_count;

size_t mp_signatures_chat_resolve(void)
{
    if (resolved_once) {
        return resolved_count;
    }
    resolved_once  = true;
    resolved_count = signature_resolve_table(chat_sites, MP_CHAT_SITE_COUNT);
    log_info("%u of %u chat site(s) resolved", (unsigned)resolved_count,
             (unsigned)MP_CHAT_SITE_COUNT);
    return resolved_count;
}

uintptr_t mp_signatures_chat_address(mp_chat_site_t site)
{
    return ((size_t)site < (size_t)MP_CHAT_SITE_COUNT) ? chat_sites[site].address : 0u;
}

size_t mp_signatures_chat_prologue(mp_chat_site_t site)
{
    return ((size_t)site < (size_t)MP_CHAT_SITE_COUNT) ? chat_sites[site].detour_prologue : 0u;
}

const signature_t *mp_signatures_chat_site(mp_chat_site_t site)
{
    return ((size_t)site < (size_t)MP_CHAT_SITE_COUNT) ? &chat_sites[site] : NULL;
}
