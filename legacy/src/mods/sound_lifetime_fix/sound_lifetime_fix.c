/* sound_lifetime_fix.c: a pinned voice must not keep a pointer into its caller's stack frame.
 *
 * THE SYMPTOM. Saving while enemy droids have blaster bolts in flight, then loading that save,
 * crashes at the end of the load. The fault is an EXECUTE at FFFFFFFF reached through the window
 * message pump, with no engine frame under it. Turning sound effects off stops it. Turning the
 * volume to zero does not, because the volume gates no branch anywhere in the engine: it is handed
 * to Miles and nothing else reads it.
 *
 * THE CAUSE. bapsound_play records the address the caller passed for its channel handle,
 *
 *     g_channel[ch].pOwnerHandle = pHandle;
 *
 * and bapsound_freeChannel writes -1 through that pointer when the voice ends,
 *
 *     if (c->pOwnerHandle != 0) { *c->pOwnerHandle = -1; c->pOwnerHandle = 0; }
 *
 * which is a sound protocol as long as the handle outlives the voice. Three call sites in the
 * projectile code pass the address of a stack local instead. shot_spawn is the one that runs on
 * every bolt fired:
 *
 *     u32 handle;
 *     if (sound_play3d(0, g_sfxName[i], &handle, &pShot->pos, sndFlags) >= 0)
 *         bapsound_pinChannel(handle, &pShot->pos);
 *
 * bapsound_pinChannel exists to detach a voice from its caller, and it does half of that job: it
 * copies the position by value so the channel stops reading the caller's vec3. It leaves
 * pOwnerHandle alone. The frame returns, the pointer does not, and when the voice ends the engine
 * writes FFFFFFFF into a frame that has already gone.
 *
 * Loading a save fires that write for every voice at once, because the load broadcasts module
 * message 6 and that reaches bapsound_removeLevelSounds, which stops all twelve channels. Measured
 * on the save that crashes: three channels carried stack owner handles at that moment, all three
 * flagged SNDF_STATIC_POS, all three blaster sounds, and every one of them wrote above the stack
 * pointer, which is to say into a frame still in use.
 *
 * THE FIX. Clear pOwnerHandle where the pin happens, which is what the pin was already trying to
 * do. Only a handle pointing into the calling thread's own stack is cleared. A channel whose owner
 * lives anywhere else keeps the protocol it was written for, so this does not have to be right
 * about call sites nobody has looked at yet.
 *
 * WHAT THIS DOES NOT SETTLE. The write is a real defect and this removes it, but the chain from the
 * poisoned slot to the faulting instruction half a second later was never traced instruction by
 * instruction. If the crash survives this, the corruption was somewhere else, and the count below
 * still says how many dangling handles were detached, which is the useful half of the answer.
 */
#include "sound_lifetime_fix.h"

#include "common/detour.h"
#include "common/host_image.h"
#include "common/ini.h"
#include "common/logging.h"
#include "common/memory.h"
#include "common/signature.h"

#include <windows.h>
#include <intrin.h>

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#define SOUND_LIFETIME_SECTION "sound_lifetime_fix"

/* --- bapsound_pinChannel 0x00417826 -----------------------------------------------------------
 *
 *   55 8B EC 51      push ebp / mov ebp,esp / push ecx
 *   8B 45 08         mov eax,[ebp+8]          the channel index, the only argument read here
 *   C1 E0 07         shl eax,7                the bank stride, 0x80
 *   05 <imm32>       add eax,&g_channel[0]    THE CHANNEL BANK. The operand is at +0x0B and is
 *                                             read out rather than written down, so a build that
 *                                             places the bank elsewhere still resolves.
 *   89 45 FC         mov [ebp-4],eax
 *   8B 4D FC         mov ecx,[ebp-4]
 *   83 79 0C 00      cmp dword [ecx+0x0C],0   c->pRef, the "slot carries a sound" test
 *   74 27            jz past the whole body
 *
 * The four operand bytes are masked out of the pattern for the reason above. The detour takes the
 * first seven bytes, which is a whole number of instructions and more than jmp rel32 needs. */
static const uint8_t SIG_PIN_CHANNEL[] = {
    0x55, 0x8B, 0xEC, 0x51, 0x8B, 0x45, 0x08, 0xC1, 0xE0, 0x07, 0x05,
    0x00, 0x00, 0x00, 0x00,
    0x89, 0x45, 0xFC, 0x8B, 0x4D, 0xFC, 0x83, 0x79, 0x0C, 0x00, 0x74, 0x27
};
static const uint8_t MASK_PIN_CHANNEL[] = {
    0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF,
    0x00, 0x00, 0x00, 0x00,
    0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF
};
#define PIN_CHANNEL_PROLOGUE  7u
#define OFFSET_CHANNEL_BANK   0x0Bu

/* The twelve voice channel bank, stride 0x80. Only the two fields this needs are named. */
#define CHANNEL_STRIDE          0x80
#define CHANNEL_COUNT             12
#define CHANNEL_FLAGS           0x10
#define CHANNEL_OWNER_HANDLE    0x78
#define CHANNEL_FLAG_STATIC_POS 0x20u

enum {
    SITE_PIN_CHANNEL,
    SITE_COUNT
};

static signature_t sites[SITE_COUNT] = {
    SIGNATURE_ENTRY_DETOUR_MASKED("bapsound_pinChannel", SIG_PIN_CHANNEL, MASK_PIN_CHANNEL,
                                  PIN_CHANNEL_PROLOGUE)
};

typedef void (__cdecl *pin_channel_fn_t)(uint32_t channel, const void *position);

static struct {
    bool      installed;
    bool      active;
    detour_t  pin;
    uint8_t  *channel_bank;
    unsigned  detached;
} pin_state;

bool sound_owner_is_on_stack(uintptr_t owner, uintptr_t limit, uintptr_t base)
{
    /* A null handle is not on any stack, and neither is one outside the extent. The extent is half
     * open at the top: base is the address one past the highest usable byte. An extent that is not
     * ordered describes no memory, so it holds nothing. */
    if (owner == 0 || limit >= base) {
        return false;
    }
    return owner >= limit && owner < base;
}

static void __cdecl hook_pin_channel(uint32_t channel, const void *position)
{
    pin_channel_fn_t original = (pin_channel_fn_t)pin_state.pin.original;
    uint8_t         *record;
    uint32_t         flags;
    void           **owner;
    uintptr_t        at;

    original(channel, position);

    if (!pin_state.active || channel >= CHANNEL_COUNT || pin_state.channel_bank == NULL) {
        return;
    }
    record = pin_state.channel_bank + channel * CHANNEL_STRIDE;
    flags  = *(const uint32_t *)(record + CHANNEL_FLAGS);

    /* The original does nothing at all when the slot carries no sound, and then there is no pin to
     * finish. SNDF_STATIC_POS is the bit it sets, so the flag answers that without a second read of
     * pRef. */
    if ((flags & CHANNEL_FLAG_STATIC_POS) == 0) {
        return;
    }
    owner = (void **)(record + CHANNEL_OWNER_HANDLE);
    at    = (uintptr_t)*owner;

    if (!sound_owner_is_on_stack(at, (uintptr_t)__readfsdword(0x08),
                                 (uintptr_t)__readfsdword(0x04))) {
        return;
    }
    *owner = NULL;
    ++pin_state.detached;

    /* The first few name themselves and then it goes quiet, because this runs on every shot fired
     * and a line per bolt would be a problem of its own. */
    if (pin_state.detached <= 4u) {
        log_info("detached a pinned voice from %08X, a local of a frame that is about to return "
                 "and would have taken FFFFFFFF when the voice ended", (unsigned)at);
    }
}

void sound_lifetime_fix_install(void)
{
    log_init("sound_lifetime_fix", false);

    if (pin_state.installed) {
        return;
    }
    if (!host_image_resolve()) {
        log_error("no 32-bit host image, nothing patched");
        return;
    }
    if (!ini_read_bool(SOUND_LIFETIME_SECTION, "Enabled", true)) {
        log_info("Enabled=0, a pinned voice keeps writing FFFFFFFF into its caller's dead frame");
        return;
    }

    pin_state.installed = true;

    if (signature_resolve_table(sites, SITE_COUNT) != SITE_COUNT) {
        log_error("bapsound_pinChannel did not resolve, nothing patched");
        return;
    }
    {
        uint32_t address = 0;

        if (!memory_read_u32(sites[SITE_PIN_CHANNEL].address + OFFSET_CHANNEL_BANK, &address) ||
            !memory_is_inside_image(address, CHANNEL_STRIDE * CHANNEL_COUNT)) {
            log_error("the channel bank operand read as %08X, which is not a bank sized range "
                      "inside the image, nothing patched", (unsigned)address);
            return;
        }
        pin_state.channel_bank = (uint8_t *)(uintptr_t)address;
    }
    if (!detour_install(&pin_state.pin, sites[SITE_PIN_CHANNEL].address,
                        (const void *)hook_pin_channel, PIN_CHANNEL_PROLOGUE)) {
        log_error("bapsound_pinChannel could not be detoured, nothing patched");
        return;
    }

    pin_state.active = true;
    log_info("armed on bapsound_pinChannel at %08X, channel bank at %08X: a pinned voice no longer "
             "keeps the address of its caller's local",
             (unsigned)sites[SITE_PIN_CHANNEL].address,
             (unsigned)(uintptr_t)pin_state.channel_bank);
}
