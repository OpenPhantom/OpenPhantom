/* actor_loader.c: see actor_loader.h.
 *
 * Two calls in the hero code, retail WMAIN.EXE:
 *
 *   player_spawnHero, 0x00447ECF, loading the hero's file:
 *   00447ECF  A1 <pr>                  mov eax,[pr]
 *   00447ED4  8B 48 6C                 mov ecx,[eax+0x6C]           heroIndex
 *   00447ED7  8B 14 8D <table>         mov edx,[ecx*4+g_heroBafName]
 *   00447EDE  52                       push edx                     the name
 *   00447EDF  68 53 46 41 42           push 0x42414653              the tag, "SFAB"
 *   00447EE4  E8 <rel32>               call res_Alloc               0x00472040
 *   00447EE9  83 C4 08                 add esp,8
 *
 *   player_despawn, 0x00448262, giving it back:
 *   00448262  8B 0D <pr>               mov ecx,[pr]
 *   00448268  8B 11                    mov edx,[ecx]                pHeroActor
 *   0044826A  52                       push edx
 *   0044826B  E8 <rel32>               call res_Free                0x0047221F
 *   00448270  83 C4 04                 add esp,4
 *   00448273  A1 <pr>                  mov eax,[pr]
 *   00448278  C7 00 00 00 00 00        mov [eax],0                  pHeroActor = NULL
 *   0044827E  8B 0D <pr>               mov ecx,[pr]
 *   00448284  C7 41 0C 00 00 00 00     mov [ecx+0xC],0              hActor = NULL
 *
 * Both are cdecl: res_Alloc(tag, name) answers the loaded file or NULL; res_Free(file). The
 * cross check is the player block: the same cell has to be named by both patterns.
 */
#include "actor_loader.h"

#include "common/logging.h"
#include "common/memory.h"
#include "common/signature.h"
#include "common/text.h"

#include <stddef.h>
#include <string.h>

static const uint8_t SIG_ALLOC[] = {
    0xA1, 0x00, 0x00, 0x00, 0x00,                 /* mov eax,[pr]                       */
    0x8B, 0x48, 0x6C,                             /* mov ecx,[eax+0x6C]                 */
    0x8B, 0x14, 0x8D, 0x00, 0x00, 0x00, 0x00,     /* mov edx,[ecx*4+g_heroBafName]      */
    0x52,                                         /* push edx                           */
    0x68, 0x53, 0x46, 0x41, 0x42,                 /* push 'BAFS'                        */
    0xE8, 0x00, 0x00, 0x00, 0x00,                 /* call res_Alloc                     */
    0x83, 0xC4, 0x08                              /* add esp,8                          */
};
static const uint8_t MSK_ALLOC[] = {
    0xFF, 0x00, 0x00, 0x00, 0x00,
    0xFF, 0xFF, 0xFF,
    0xFF, 0xFF, 0xFF, 0x00, 0x00, 0x00, 0x00,
    0xFF,
    0xFF, 0xFF, 0xFF, 0xFF, 0xFF,
    0xFF, 0x00, 0x00, 0x00, 0x00,
    0xFF, 0xFF, 0xFF
};
_Static_assert(sizeof SIG_ALLOC == sizeof MSK_ALLOC, "mask length");
#define ALLOC_PLAYER_OFFSET 1u
#define ALLOC_CALL_OFFSET   22u   /* the rel32 after the E8 at 21; read from 21 the target came
                                   * out as 02E5D6D1 and the loader never installed (2026-09-16) */
#define ALLOC_NEXT_OFFSET   26u

static const uint8_t SIG_FREE[] = {
    0x8B, 0x0D, 0x00, 0x00, 0x00, 0x00,           /* mov ecx,[pr]                       */
    0x8B, 0x11,                                   /* mov edx,[ecx]                      */
    0x52,                                         /* push edx                           */
    0xE8, 0x00, 0x00, 0x00, 0x00,                 /* call res_Free                      */
    0x83, 0xC4, 0x04,                             /* add esp,4                          */
    0xA1, 0x00, 0x00, 0x00, 0x00,                 /* mov eax,[pr]                       */
    0xC7, 0x00, 0x00, 0x00, 0x00, 0x00,           /* mov [eax],0                        */
    0x8B, 0x0D, 0x00, 0x00, 0x00, 0x00,           /* mov ecx,[pr]                       */
    0xC7, 0x41, 0x0C, 0x00, 0x00, 0x00, 0x00      /* mov [ecx+0xC],0                    */
};
static const uint8_t MSK_FREE[] = {
    0xFF, 0xFF, 0x00, 0x00, 0x00, 0x00,
    0xFF, 0xFF,
    0xFF,
    0xFF, 0x00, 0x00, 0x00, 0x00,
    0xFF, 0xFF, 0xFF,
    0xFF, 0x00, 0x00, 0x00, 0x00,
    0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF,
    0xFF, 0xFF, 0x00, 0x00, 0x00, 0x00,
    0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF
};
_Static_assert(sizeof SIG_FREE == sizeof MSK_FREE, "mask length");
#define FREE_PLAYER_OFFSET 2u
#define FREE_CALL_OFFSET   10u
#define FREE_NEXT_OFFSET   14u

/* The bytes 53 46 41 42 the engine pushes, read as the dword the actor loader is registered
 * under at start-up (res_registerType(0x42414653)). The other byte order is no type at all, and
 * res_Alloc then answers the file's raw bytes, which the spawn bound as an actor and wrote
 * through (played 2026-09-16, a Watto that took the level with him). */
#define TAG_BAFS 0x42414653u
#define LOADED_MAX 32u
#define NAME_MAX   25u

/* What a loaded actor looks like, so a body is never bound to anything else: the engine's
 * actor loader (bapactor_readActor) carves the whole runtime footprint out of one block
 * whose size is at +0xA8, and the model at +0xE0 and the clip table at +0xE4 point into it; the
 * model's node count at +0x54 is what rdThing_SetModel sizes its arrays by. A file's raw bytes
 * carry the same header but file offsets where the pointers should be; the wrong tag handed
 * those back once (played 2026-09-16, a Watto that took the level with him). */
#define ACTOR_RUNTIME_SIZE 0xA8u
#define ACTOR_MODEL        0xE0u
#define ACTOR_CLIPS        0xE4u
#define MODEL_NODE_COUNT   0x54u
#define NODES_MOST         400u   /* the tools' own plausibility bound; the heroes have 48 */

static bool inside(uintptr_t at, uintptr_t block, uint32_t size)
{
    return at >= block && at < block + size;
}

static bool looks_like_actor(const void *file, char *why, size_t why_size)
{
    uintptr_t block = (uintptr_t)file;
    uint32_t  size  = 0;
    uint32_t  model = 0;
    uint32_t  clips = 0;
    uint32_t  nodes = 0;

    if (!memory_try_read(block + ACTOR_RUNTIME_SIZE, &size, sizeof size) ||
        !memory_try_read(block + ACTOR_MODEL, &model, sizeof model) ||
        !memory_try_read(block + ACTOR_CLIPS, &clips, sizeof clips)) {
        text_format(why, why_size, "its header cannot be read");
        return false;
    }
    if (size < 0x100u || !inside(model, block, size) || !inside(clips, block, size)) {
        text_format(why, why_size, "the model (%08X) or the clips (%08X) lie outside its %u "
                    "byte block", model, clips, size);
        return false;
    }
    if (!memory_try_read((uintptr_t)model + MODEL_NODE_COUNT, &nodes, sizeof nodes) ||
        nodes == 0 || nodes > NODES_MOST) {
        text_format(why, why_size, "the model has %u nodes", nodes);
        return false;
    }
    return true;
}

typedef void *(__cdecl *res_alloc_fn_t)(uint32_t tag, const char *name);
typedef void (__cdecl *res_free_fn_t)(void *resource);

static struct {
    res_alloc_fn_t alloc;
    res_free_fn_t  release;
    struct {
        char  name[NAME_MAX];
        void *file;
    } loaded[LOADED_MAX];
    uint32_t count;
} st;

static uintptr_t call_target(uintptr_t call_at, uintptr_t next)
{
    uint32_t rel32 = 0;

    if (!memory_read_u32(call_at, &rel32)) {
        return 0;
    }
    return next + rel32;
}

bool actor_loader_install(void)
{
    uintptr_t alloc_site = signature_find_unique(SIG_ALLOC, MSK_ALLOC, sizeof SIG_ALLOC);
    uintptr_t free_site  = signature_find_unique(SIG_FREE, MSK_FREE, sizeof SIG_FREE);
    uint32_t  player_a = 0;
    uint32_t  player_b = 0;
    uintptr_t alloc;
    uintptr_t release;

    if (alloc_site == 0 || free_site == 0) {
        log_warning("actor loader: the hero code's load (%08X) or release (%08X) did not resolve, "
                    "so only the level's own actor files can be spawned", (unsigned)alloc_site,
                    (unsigned)free_site);
        return false;
    }
    if (!memory_read_u32(alloc_site + ALLOC_PLAYER_OFFSET, &player_a) ||
        !memory_read_u32(free_site + FREE_PLAYER_OFFSET, &player_b) || player_a != player_b) {
        log_warning("actor loader: the two sites name different player blocks (%08X, %08X), so "
                    "only the level's own actor files can be spawned", (unsigned)player_a,
                    (unsigned)player_b);
        return false;
    }
    alloc   = call_target(alloc_site + ALLOC_CALL_OFFSET, alloc_site + ALLOC_NEXT_OFFSET);
    release = call_target(free_site + FREE_CALL_OFFSET, free_site + FREE_NEXT_OFFSET);
    if (!memory_is_inside_image(alloc, 16u) || !memory_is_inside_image(release, 16u)) {
        log_warning("actor loader: a call lands outside the image (%08X, %08X)", (unsigned)alloc,
                    (unsigned)release);
        return false;
    }
    st.alloc   = (res_alloc_fn_t)alloc;
    st.release = (res_free_fn_t)release;
    log_info("actor loader: res_Alloc at %08X and res_Free at %08X, the hero code's own; an "
             "actor file the level did not list is loaded through them for the spawner",
             (unsigned)alloc, (unsigned)release);
    return true;
}

bool actor_loader_is_available(void)
{
    return st.alloc != NULL;
}

void *actor_loader_get(const char *name)
{
    uint32_t i;
    void    *file;

    if (st.alloc == NULL || name == NULL || strlen(name) >= NAME_MAX) {
        return NULL;
    }
    for (i = 0; i < st.count; ++i) {
        if (_stricmp(st.loaded[i].name, name) == 0) {
            return st.loaded[i].file;
        }
    }
    if (st.count == LOADED_MAX) {
        log_warning("actor loader: %u files are loaded already, the most this keeps; %s is not",
                    LOADED_MAX, name);
        return NULL;
    }
    file = st.alloc(TAG_BAFS, name);
    if (file == NULL) {
        log_warning("actor loader: the engine found no actor file named %s", name);
        return NULL;
    }
    {
        char why[120];

        if (!looks_like_actor(file, why, sizeof why)) {
            log_warning("actor loader: what the engine answered for %s at %08X is not an actor "
                        "(%s); given back, nothing spawned", name, (unsigned)(uintptr_t)file,
                        why);
            st.release(file);
            return NULL;
        }
    }
    text_format(st.loaded[st.count].name, NAME_MAX, "%s", name);
    st.loaded[st.count].file = file;
    st.count++;
    log_info("actor loader: %s loaded at %08X, kept until the level changes", name,
             (unsigned)(uintptr_t)file);
    return file;
}

void actor_loader_release_all(void)
{
    uint32_t i;

    if (st.release != NULL) {
        for (i = 0; i < st.count; ++i) {
            st.release(st.loaded[i].file);
        }
    }
    if (st.count != 0) {
        log_info("actor loader: %u loaded actor files given back", st.count);
    }
    st.count = 0;
}
