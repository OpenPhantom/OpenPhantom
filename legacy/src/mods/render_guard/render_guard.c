/* render_guard.c: the deferred face path fills two arrays without checking either bound.
 *
 * ============================== What is actually broken =======================================
 *
 * Both faults are in one engine function, the deferred face submit at 0x00487D20, and both are
 * writes with no test standing in front of them.
 *
 * First, the stack array. The function keeps 32 bytes of per vertex clip outcodes on its own stack
 * and fills one byte per vertex, with nothing comparing the vertex count against the array's
 * length. The array sits at [esp+0x18], so its last byte is [esp+0x37]. The frame is
 * `sub esp,0x28` plus four pushes, which puts the saved return address at [esp+0x38]. That is
 * outcode[32], and the thirty third vertex of a face writes it. The result is not a graphical
 * defect, it is a corrupted return whose target depends on where the vertex landed on screen,
 * which is also why it would never reproduce twice the same way.
 *
 * Thirty is the authored limit rather than an inference. The editor build of this engine still
 * carries the assert the shipping build compiles out, and its text names the number: "Can't have
 * more than 30 sided polys as special faces". Thirty is therefore the contract every caller was
 * written against, and thirty three is the mechanical failure point, so refusing at thirty one
 * refuses nothing a correct scene submits. The two vertex limits in this engine are different
 * numbers belonging to different paths: the immediate path asserts at sixty four, the size of its
 * own scratch vertex buffer. Only the deferred path has thirty.
 *
 * Second, the shared vertex pool. Every accepted face copies its vertices into one global pool and
 * advances a cursor by the vertex count. The queue's ENTRY count is tested against a ceiling on the
 * function's first instruction; the pool cursor is tested against nothing, anywhere. A scene that
 * stays under the entry ceiling can still run the cursor past the end of the pool, and what
 * follows the pool is other engine state.
 *
 * ============================== What this does ================================================
 *
 * It refuses a face that would overrun either array, by returning the same null the function
 * already returns when its queue is full. That path is neither new nor exotic: every caller
 * handles it, because a full queue is an ordinary condition in a busy scene. A refused face costs
 * one polygon, in a frame that was about to corrupt memory.
 *
 * ============================== The undefined depth comparison ================================
 *
 * The third repair here is not a bound. The engine maps a device's depth comparison capability to
 * the D3DCMPFUNC it then asks Direct3D for, one arm per capability, and it has no arm for LESS. An
 * input of exactly 0x02 falls through every branch and the accumulator comes back as it was
 * initialised, zero, which is not a member of D3DCMPFUNC. The engine hands that zero to
 * SetRenderState as the depth comparison, and what a driver makes of an undefined comparison is
 * its own business. This substitutes LESS for zero and passes every other answer through
 * untouched, so on a device that reaches any arm at all the hook is the identity.
 *
 * ============================== On the length of this file ====================================
 *
 * The two bounds themselves are in face_bounds.c, which is the one seam here that pays for itself:
 * they are pure arithmetic, they are the whole point of the DLL, and a test can only reach them
 * from outside this file, which exposes nothing but its install function. What stays behind is
 * everything that touches the engine, the ini or the log.
 *
 * The other seam available is the depth comparison repair, which is a pattern, a hook of eight
 * lines and one detour. Splitting that out buys a header, an include and a shared state pointer for
 * twenty lines of code that belong to the same Direct3D layer as everything else here, so it stays.
 * What makes the file long is the byte evidence, and that is not the part to move.
 */
#include "render_guard.h"

#include "face_bounds.h"

#include "common/detour.h"
#include "common/host_image.h"
#include "common/ini.h"
#include "common/logging.h"
#include "common/memory.h"
#include "common/signature.h"

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#define RENDER_GUARD_SECTION "render_guard"

/* The authored vertex limit of this path, from the editor build's own assert text. Refusing above
 * 30 stops two vertices short of the 33rd, which is the one whose outcode lands on the saved
 * return address. */
#define AUTHORED_MAX_VERTICES     30u
#define MAX_CONFIGURABLE_VERTICES 31u   /* 32 is the array, and its 33rd entry is the return */

/* --- 0x00487D20  the deferred face submit ---------------------------------------------------- *
 *   00487D20  A1 B0 6F 86 00        mov eax,[0x00866FB0]      the queue's entry count
 *   00487D25  83 EC 28              sub esp,0x28
 *   00487D28  D9 05 78 8D 4A 00     fld dword [0x004A8D78]
 *   00487D2E  53                    push ebx
 *   00487D2F  55                    push ebp
 *   00487D30  56                    push esi
 *   00487D31  3D 04 20 00 00        cmp eax,0x2004            the queue's entry ceiling
 *   00487D36  57                    push edi
 *   00487D37  72 0C                 jb 00487D45
 *   00487D39  DD D8                 fstp st(0)
 *   00487D3B  33 C0                 xor eax,eax               the queue-full answer, and ours
 *   00487D44  C3                    ret
 *
 * There is no frame pointer at all. The function loads its queue count from an absolute address
 * first, reserves its locals second and pushes the callee saved registers only afterwards, so the
 * first instruction boundary at or past five bytes is EIGHT, not the five or six of the usual
 * `push ebp / mov ebp,esp / sub esp,imm` opening. A detour that assumed five would cut the
 * `sub esp,0x28` in half.
 *
 * Both absolute operands are wildcarded, which is what lets one pattern serve every build: it
 * resolves at 0x00487D20 on each retail WMAIN checked and at 0x00487CC0 on the Edit Tool's
 * recompile.
 *
 * The `cmp eax,0x2004` at pattern offset 18 is the queue's own entry ceiling, and it is read out
 * of the match rather than written down here, because the pool capacity below is derived from
 * that same constant and the two must not be able to drift apart. */
static const uint8_t SIG_DEFER_FACE[] = {
    0xA1, 0x00, 0x00, 0x00, 0x00,        /* mov eax, [std3D_deferCount] */
    0x83, 0xEC, 0x28,                    /* sub esp, 0x28               */
    0xD9, 0x05, 0x00, 0x00, 0x00, 0x00,  /* fld dword [a constant]      */
    0x53, 0x55, 0x56,                    /* push ebx / ebp / esi        */
    0x3D, 0x04, 0x20, 0x00, 0x00,        /* cmp eax, 0x2004             */
    0x57,                                /* push edi                    */
    0x72                                 /* jb  (the queue-full exit)   */
};
static const uint8_t MSK_DEFER_FACE[] = {
    0xFF, 0x00, 0x00, 0x00, 0x00,
    0xFF, 0xFF, 0xFF,
    0xFF, 0xFF, 0x00, 0x00, 0x00, 0x00,
    0xFF, 0xFF, 0xFF,
    0xFF, 0xFF, 0xFF, 0xFF, 0xFF,
    0xFF,
    0xFF
};
#define DEFER_FACE_PROLOGUE       8u
#define OFFSET_ENTRY_CEILING      18u

/* --- 0x00487EEB  where the pool cursor is advanced ------------------------------------------- *
 *   00487ED7  8B 3D 80 73 86 00     mov edi,[0x00867380]
 *   00487EDD  C1 E7 05              shl edi,5                 stride 32 bytes, proven here
 *   00487EE0  D8 F9                 fdivr st(1)
 *   00487EE2  81 C7 10 4C 73 00     add edi,0x00734C10        and this is the pool's base
 *   00487EEB  8B 1D 80 73 86 00     mov ebx,[0x00867380]
 *   00487EF1  03 DE                 add ebx,esi               esi is the vertex count
 *   00487EF3  89 1D 80 73 86 00     mov [0x00867380],ebx      stored back, with no test at all
 *   004880C7  C7 05 80 73 86 00 ..  mov dword [0x00867380],0  the reset, in the queue drain
 *
 * The shift by five is what proves the pool's stride is 32 bytes. The add and the store are the
 * whole of the pool's bookkeeping: no comparison, no ceiling, no refusal anywhere on that path.
 *
 * The site is inside the same function but far past the eight bytes a detour on its entry
 * rewrites, so the two anchors cannot interfere with each other. It resolves at 0x00487EEB on
 * retail and at 0x00487E8B on the recompile. The cursor's address is read out of the matched
 * operand rather than written down. */
static const uint8_t SIG_POOL_CURSOR[] = {
    0x8B, 0x1D, 0x00, 0x00, 0x00, 0x00,  /* mov ebx, [std3D_vertPoolUsed] */
    0x03, 0xDE,                          /* add ebx, esi   (the count)    */
    0x89, 0x1D, 0x00, 0x00, 0x00, 0x00,  /* mov [std3D_vertPoolUsed], ebx */
    0xD9, 0x18                           /* fstp dword [eax]              */
};
static const uint8_t MSK_POOL_CURSOR[] = {
    0xFF, 0xFF, 0x00, 0x00, 0x00, 0x00,
    0xFF, 0xFF,
    0xFF, 0xFF, 0x00, 0x00, 0x00, 0x00,
    0xFF, 0xFF
};
#define OFFSET_POOL_CURSOR_OPERAND 2u

/* --- why the pool's capacity is a setting and not a constant ---------------------------------- *
 * The engine never names that capacity. The only number in the image it can be derived from is the
 * queue's entry ceiling above, 0x2004, the count this same function tests on its first
 * instruction. That is a derivation, not a measurement, and it is worth saying so plainly.
 *
 * A census was attempted and did not settle it. Scanning .text for dword literals landing between
 * the pool's base at 0x00734C10 and a generous end at 0x00790000 returns 74 hits at byte alignment,
 * 15 of them dword aligned and only 6 real instruction operands, so most are coincidences inside
 * instructions rather than addresses. From the other side, the nearest address above the pool's
 * base that anything else is known to use is 0x008439AC, which is 1.06 MB higher, so the pool is
 * not immediately followed by anything identified. The derived
 * capacity, 0x2004 vertices of 32 bytes each, which is 0x40080 bytes ending at 0x00774C90, is
 * consistent with that gap but not proven by it.
 *
 * So the guard defaults to the derived number and says so in the log the first time it refuses,
 * instead of dropping geometry quietly. A refusal during ordinary play is evidence that the pool
 * is larger than the derivation, not evidence that the scene is too complex, and the ini key is
 * how to say so. */

/* --- 0x0048AAF9  the capability to depth comparison mapper ------------------------------------ *
 * One arm per capability, and this is the whole table:
 *
 *     0x01 NEVER        -> 1      or  al,1
 *     0x02 LESS         -> 2      no arm at all
 *     0x04 EQUAL        -> 3      or  al,3
 *     0x08 LESSEQUAL    -> 4      or  al,4
 *     0x10 GREATER      -> 5      or  al,5
 *     0x20 NOTEQUAL     -> 6      or  al,6
 *     0x40 GREATEREQUAL -> 7      mov al,7
 *     0x80 ALWAYS       -> 8      or  al,8
 *
 * An input of exactly 0x02 falls through every branch and the function returns its accumulator as
 * it was initialised, zero. The `or` is the original's own and not a transcription slip; it is
 * only meaningful for a single bit input, since 0x08 with 0x10 would give `4 | 5`, which is 5
 * rather than two comparisons.
 *
 * That input is reachable: the device open path stores exactly 2 into the capability cell at
 * 0x00866FC8 when the device fails its comparison probe. Whether any real device takes that path
 * is not established, which is exactly why this substitutes instead of assuming either way. Any
 * Direct3D 9 device advertises GREATER, so on a translation layer the mapper reaches the 0x10 arm
 * and this hook never changes an answer.
 *
 * The pattern has to reach the SECOND arm, and those eighteen bytes are not padding. A sibling in
 * the same module opens with the identical thirty bytes, down to the first capability test, and
 * the offline pattern check failed the build on a match count of two before any of this ran:
 *
 *     0048AA64  ... 8B 55 08  83 E2 03  85 D2  74 08  8B 45 FC  0C 04  89 45 FC
 *     0048AAF9  ... 8B 55 08  83 E2 04  85 D2  74 08  8B 45 FC  0C 03  89 45 FC
 *
 * They diverge only at the mask and the contributed value of that second arm: `and edx,3` with
 * `or al,4` against `and edx,4` with `or al,3`. Extended through it the pattern is unique on every
 * retail build checked and on the recompile, where it lands at 0x0048AA99. The shorter anchor
 * looked completely reasonable while being wrong, which is the case the rule about expected match
 * counts exists for.
 *
 * No absolute operands, so the pattern is literal throughout. The prologue is eleven bytes, four
 * for the frame and seven for the zeroing of the accumulator. */
static const uint8_t SIG_CMP_CAPS_TO_FUNC[] = {
    0x55, 0x8B, 0xEC, 0x51,                          /* push ebp / mov ebp,esp / push ecx  */
    0xC7, 0x45, 0xFC, 0x00, 0x00, 0x00, 0x00,        /* mov [ebp-4], 0                     */
    0x8B, 0x45, 0x08,                                /* mov eax, [ebp+8]                   */
    0x83, 0xE0, 0x01,                                /* and eax, 1        the NEVER arm    */
    0x85, 0xC0,                                      /* test eax, eax                      */
    0x74, 0x09,                                      /* je  past it                        */
    0x8B, 0x4D, 0xFC,                                /* mov ecx, [ebp-4]                   */
    0x83, 0xC9, 0x01,                                /* or  ecx, 1                         */
    0x89, 0x4D, 0xFC,                                /* mov [ebp-4], ecx                   */
    0x8B, 0x55, 0x08,                                /* mov edx, [ebp+8]                   */
    0x83, 0xE2, 0x04,                                /* and edx, 4        the EQUAL arm    */
    0x85, 0xD2,                                      /* test edx, edx                      */
    0x74, 0x08,                                      /* je  past it                        */
    0x8B, 0x45, 0xFC,                                /* mov eax, [ebp-4]                   */
    0x0C, 0x03,                                      /* or  al, 3                          */
    0x89, 0x45, 0xFC                                 /* mov [ebp-4], eax                   */
};
#define CMP_CAPS_PROLOGUE          11u
#define D3DCMP_LESS                2u

/* Five cdecl arguments, and the return value is the queue entry or null. The engine's own
 * queue-full arm returns null from this same function, so null is an expected answer here and
 * every caller already deals with it. The hook returns what the original returned; a detour that
 * drops a return value is a silent data fault. */
typedef void *(__cdecl *defer_face_fn_t)(void *texture, uint32_t flags, void *vertices,
                                         uint32_t count, int32_t need_clip);

/* One argument, and the accumulator it returns is a dword local, so the caller receives a full
 * register even though only the low byte is ever meaningful. */
typedef uint32_t (__cdecl *cmp_caps_fn_t)(uint32_t caps);

typedef struct render_guard_state {
    bool            installed;
    detour_t        defer;
    detour_t        cmp_caps;
    defer_face_fn_t original;
    cmp_caps_fn_t   cmp_original;
    uint32_t        max_vertices;
    uint32_t        pool_capacity;
    uint32_t        refused_vertices;   /* faces refused for the outcode array */
    uint32_t        refused_pool;       /* faces refused for the vertex pool   */
    uint32_t        substituted;        /* undefined depth comparisons replaced */
    bool            said_vertices;
    bool            said_pool;
    bool            said_cmp;
    /* A PLAIN POINTER, validated once at install and deliberately not read through
     * memory_read_u32 afterwards. That helper calls memory_is_readable_range, which calls
     * VirtualQuery, and this hook runs once per submitted face, which is hundreds of times a frame
     * in a busy scene. A guard against a rare overflow must not cost a system call on every
     * ordinary face, least of all in a tree whose open question is why the picture stutters. The
     * cell is inside the host image and the image is never unmapped, so the single check at
     * install is the whole check that is needed.
     *
     * The general rule this is one instance of: memory_read_* belongs in installation code and in
     * code that runs at human rates, never in a path the engine drives per object. */
    const volatile uint32_t *pool_cursor;
} render_guard_state_t;

static render_guard_state_t guard_state;

/* Reads the cursor and asks whether this face would take it past the end. The cursor is read fresh
 * on every call rather than tracked here, because the engine zeroes it when it drains the queue
 * and this hook has no reliable way to observe that moment. The comparison itself lives in
 * face_bounds.c, where a test can reach it; a capacity of 0 is answered there and means the bound
 * is not in force. */
static bool pool_would_overflow(uint32_t count)
{
    if (guard_state.pool_cursor == NULL || guard_state.pool_capacity == 0u) {
        return false;   /* the capacity is tested BEFORE the cell is read, as it always was */
    }

    return face_bounds_pool_would_overflow(*guard_state.pool_cursor,
                                           guard_state.pool_capacity, count);
}

static void *__cdecl hook_defer_face(void *texture, uint32_t flags, void *vertices,
                                     uint32_t count, int32_t need_clip)
{
    if (face_bounds_vertex_count_refused(count, guard_state.max_vertices)) {
        ++guard_state.refused_vertices;
        if (!guard_state.said_vertices) {
            guard_state.said_vertices = true;
            log_warning("a %u vertex face reached the deferred path, which keeps a 32 byte outcode "
                        "array on its stack and would have written past it. Refused. The authored "
                        "limit is %u and the saved return address is what the 33rd vertex "
                        "overwrites, so this is reported once and only counted after that.",
                        (unsigned)count, (unsigned)guard_state.max_vertices);
        }
        return NULL;
    }

    if (pool_would_overflow(count)) {
        ++guard_state.refused_pool;
        if (!guard_state.said_pool) {
            guard_state.said_pool = true;
            log_warning("the deferred vertex pool would have overflowed at %u vertices with a "
                        "ceiling of %u. Refused. If this appears in ordinary play the ceiling is "
                        "too low rather than the scene too complex, because the engine never names "
                        "the pool's real capacity; raise PoolCapacityVertices.",
                        (unsigned)count, (unsigned)guard_state.pool_capacity);
        }
        return NULL;
    }

    return guard_state.original(texture, flags, vertices, count, need_clip);
}

/* Substitutes only where the engine's own answer is not a comparison Direct3D defines. Every value
 * the mapper produces for a capability it does have an arm for is passed through untouched. */
static uint32_t __cdecl hook_cmp_caps_to_func(uint32_t caps)
{
    uint32_t result = guard_state.cmp_original(caps);

    if ((result & 0xFFu) != 0u) {
        return result;
    }

    ++guard_state.substituted;
    if (!guard_state.said_cmp) {
        guard_state.said_cmp = true;
        log_warning("the depth comparison mapper answered 0 for capabilities %08X, and 0 is not a "
                    "value Direct3D defines. Substituted LESS. The mapper has no arm for the LESS "
                    "capability, which is the one input that falls through every branch it does "
                    "have. Reported once and only counted after that.", (unsigned)caps);
    }
    return D3DCMP_LESS;
}

/* Resolves the cursor's address out of the matched operand and validates it ONCE. Everything after
 * this is a plain load. */
static const volatile uint32_t *resolve_pool_cursor(void)
{
    uintptr_t site;
    uint32_t  cell = 0;

    site = signature_find_unique(SIG_POOL_CURSOR, MSK_POOL_CURSOR, sizeof SIG_POOL_CURSOR);
    if (site == 0) {
        return NULL;
    }
    if (!memory_read_u32(site + OFFSET_POOL_CURSOR_OPERAND, &cell) ||
        !memory_is_inside_image((uintptr_t)cell, sizeof(uint32_t)) ||
        !memory_is_readable_range((uintptr_t)cell, sizeof(uint32_t))) {
        return NULL;
    }
    return (const volatile uint32_t *)(uintptr_t)cell;
}

/* `ceiling` is the entry ceiling read out of the engine's own instruction, or 0 when that read
 * failed. It is the default for the pool bound, and an ini value overrides it, because raising the
 * bound is the documented answer to a refusal in ordinary play. */
static void load_config(uint32_t ceiling)
{
    int32_t configured;

    guard_state.max_vertices = AUTHORED_MAX_VERTICES;
    configured = ini_read_int(RENDER_GUARD_SECTION, "MaxDeferredVertices",
                              (int32_t)AUTHORED_MAX_VERTICES);
    if (configured > 0 && configured <= (int32_t)MAX_CONFIGURABLE_VERTICES) {
        guard_state.max_vertices = (uint32_t)configured;
    } else {
        log_warning("MaxDeferredVertices=%d is outside 1 to %u, which would either refuse every "
                    "face or allow the write that lands on the return address, so the authored %u "
                    "is used instead", (int)configured, (unsigned)MAX_CONFIGURABLE_VERTICES,
                    (unsigned)AUTHORED_MAX_VERTICES);
    }

    guard_state.pool_capacity = ceiling;
    configured = ini_read_int(RENDER_GUARD_SECTION, "PoolCapacityVertices", (int32_t)ceiling);
    if (configured >= 0) {
        guard_state.pool_capacity = (uint32_t)configured;
    } else {
        log_warning("PoolCapacityVertices=%d is negative, so the %u derived from the engine's own "
                    "queue ceiling is used instead", (int)configured, (unsigned)ceiling);
    }
}

/* The depth comparison repair. It shares nothing with the two bounds except the ini section and it
 * has its own key, but it is attempted only after the face hook is in place, so a build on which
 * the submit pattern does not resolve loses this repair as well. */
static void install_depth_compare_guard(void)
{
    uintptr_t site;

    if (!ini_read_bool(RENDER_GUARD_SECTION, "GuardDepthCompare", true)) {
        log_info("GuardDepthCompare=0, so a depth comparison of 0 still reaches the device");
        return;
    }

    site = signature_find_detour_target(SIG_CMP_CAPS_TO_FUNC, NULL, sizeof SIG_CMP_CAPS_TO_FUNC,
                                        CMP_CAPS_PROLOGUE);
    if (site == 0) {
        log_warning("the depth comparison mapper did not resolve, so an undefined comparison would "
                    "still reach the device");
        return;
    }
    if (!detour_install(&guard_state.cmp_caps, site, (const void *)hook_cmp_caps_to_func,
                        CMP_CAPS_PROLOGUE)) {
        log_warning("the detour on the depth comparison mapper at %08X failed", (unsigned)site);
        return;
    }

    guard_state.cmp_original = (cmp_caps_fn_t)guard_state.cmp_caps.original;
    log_info("depth comparison mapper %08X guarded: an answer of 0, which Direct3D does not define "
             "and which only the LESS capability produces, becomes LESS. Every other answer is "
             "passed through untouched.", (unsigned)site);
}

void render_guard_install(void)
{
    uintptr_t site;
    uint32_t  ceiling = 0;

    log_init("render_guard", false);   /* first, or every line below is dropped, including the
                                        * warnings that would name the problem */

    if (guard_state.installed) {
        return;
    }

    /* And this one too. common/ is a static library, so this DLL carries its own copy of the
     * host-image state and another DLL having resolved it earlier does nothing for us. Without it
     * the scanner searches an empty range and every pattern comes back with zero matches, which
     * reads in the log exactly like an unsupported executable. */
    if (!host_image_resolve()) {
        log_error("no 32-bit host image, neither bound is guarded");
        return;
    }
    if (!ini_read_bool(RENDER_GUARD_SECTION, "Enabled", true)) {
        log_info("disabled");
        return;
    }

    site = signature_find_detour_target(SIG_DEFER_FACE, MSK_DEFER_FACE, sizeof SIG_DEFER_FACE,
                                        DEFER_FACE_PROLOGUE);
    if (site == 0) {
        log_warning("the deferred face submit did not resolve, so neither bound is guarded");
        return;
    }

    /* The queue's entry ceiling is the one number in the image the pool's capacity can be derived
     * from, so it is read here rather than assumed. */
    if (!memory_read_u32(site + OFFSET_ENTRY_CEILING, &ceiling) || ceiling == 0u) {
        log_warning("the queue ceiling could not be read at %08X, so the pool bound falls back to "
                    "whatever the ini says and is off when the ini says nothing", (unsigned)site);
        ceiling = 0u;
    }

    load_config(ceiling);

    guard_state.pool_cursor = resolve_pool_cursor();
    if (guard_state.pool_cursor == NULL) {
        log_warning("the vertex pool cursor did not resolve, so only the outcode array is guarded");
        guard_state.pool_capacity = 0u;
    }

    if (!detour_install(&guard_state.defer, site, (const void *)hook_defer_face,
                        DEFER_FACE_PROLOGUE)) {
        log_warning("the detour at %08X failed, so nothing is guarded", (unsigned)site);
        return;
    }
    guard_state.original  = (defer_face_fn_t)guard_state.defer.original;
    guard_state.installed = true;

    install_depth_compare_guard();

    log_info("deferred face submit %08X guarded: at most %u vertices a face, pool cursor %08X with "
             "a ceiling of %u vertices. A face past either bound is refused the same way the "
             "engine refuses one when its queue is full. Neither test costs more than a compare "
             "and a load, so nothing here can show up as a frame cost.",
             (unsigned)site, (unsigned)guard_state.max_vertices,
             (unsigned)(uintptr_t)guard_state.pool_cursor, (unsigned)guard_state.pool_capacity);
}
