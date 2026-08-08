/* decal_fix.c: give the decal back the depth advantage the device no longer grants it.
 *
 * ============================== What is actually broken =======================================
 *
 * A decal in this engine is NOT a quad floating above the ground. It is the ground polygon itself,
 * submitted a second time with a different texture: bapvrt_drawPolyDecals copies eight dwords per
 * vertex VERBATIM out of the array the world pass already transformed and replaces exactly the UV
 * pair (+0x18/+0x1C) and one byte of the diffuse alpha. Byte proof, 0x0041C81A onwards:
 *
 *     0041C81A  mov ecx, 8
 *     0041C824  rep movsd                 <- eight dwords, unaltered
 *     0041C829  mov [eax+0x18], ecx       <- u
 *     0041C82F  mov [eax+0x1C], ecx       <- v
 *     0041C83B  add eax, 0x20             <- stride 32 bytes
 *
 * So the second draw lands at EXACTLY the depth of the first. What lets it win is one render state.
 * bapvrt_drawPolyDecals sets the state word 0x0010AE40 (0x0041C667), whose bit 0x00100000 the state
 * applier turns into a device call at 0x0048825B:
 *
 *     0048825B  test dword [esp+0x14], 0x100000
 *     00488274  push 1
 *     00488276  push 0x2F                 <- D3DRENDERSTATE_ZBIAS
 *     00488279  call [ecx+0x58]           <- IDirect3DDevice3::SetRenderState
 *
 * The state word carries ZTEST (0x0800) and NOT ZWRITE (0x1000): the decal tests depth and never
 * writes it, so the ZBIAS is the whole of its claim to the pixel.
 *
 * D3DRENDERSTATE_ZBIAS does not exist in DIRECT3D 9. It was replaced by D3DRS_DEPTHBIAS, a float in
 * normalised depth rather than an integer 0..16. Any DirectDraw7-to-Direct3D9 translation has to
 * invent the conversion, and the result is not something this engine can influence: measured on
 * dxwrapper with Dd7to9, every decal is stamped, keeps its material, keeps its texture page, is
 * accepted by the cel selector and submitted, and never appears. The same session on a wrapper
 * that passes D3D7 through to the system implementation shows all of them.
 *
 * ============================== What this does instead ========================================
 *
 * It stops asking the device for a favour and moves the geometry. The vertices are PRE-TRANSFORMED
 * (XYZRHW|DIFFUSE|SPECULAR|TEX1, the 0x1C4 literal in every draw call), so their z is already the
 * device-space depth in [0,1], exactly the quantity ZBIAS was meant to shift. Subtracting a small
 * constant from it is what ZBIAS did, done one layer earlier and on our side of the wrapper.
 *
 * The site is exclusive, which is the whole reason this is safe. An `E8 rel32` sweep of the entire
 * .text finds 0x00487F40 has EXACTLY ONE caller, 0x0041C87D, inside bapvrt_drawPolyDecals. Nothing
 * else in the game reaches this function, so the hook needs no render-state test and can never
 * touch world geometry, sprites, the HUD or the front end. That is the same shape as the fog fix:
 * one site that belongs to one feature.
 *
 * It returns a value and the prototype says so. 0x00487F40 answers 0 or 1 (`ret` at 0x00487F65
 * carrying 0x00487D20's result, `mov eax,1` at 0x00487F77). The one caller does not read it today,
 * but a detour that drops a return value is a silent data fault; this project has already paid
 * for that lesson once, in view_distance_fix's rdThing_Draw hook.
 */
#include "decal_fix.h"

#include "common/detour.h"
#include "common/host_image.h"
#include "common/ini.h"
#include "common/logging.h"
#include "common/memory.h"
#include "common/patch.h"
#include "common/signature.h"

#include <windows.h>

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#define DECAL_FIX_SECTION "decal_fix"

/* --- 0x00487F40  the decal fan submit -------------------------------------------------------- *
 *   00487F40  8B 44 24 14      mov eax,[esp+0x14]
 *   00487F44  8B 4C 24 10      mov ecx,[esp+0x10]      <- eight bytes, a clean boundary
 * Six cdecl arguments (`add esp,0x18` at the call site): stage, render state, vertices, count,
 * clipped, sorted record. Argument three is the caller's own stack buffer, 0x0041C875 forms it as
 * `lea ecx,[esp+0x2C]`, which is `[esp+0x24]` before the two pushes that precede it, i.e. the very
 * array the copy loop above fills. */
static const uint8_t SIG_DECAL_SUBMIT[] = {
    0x8B, 0x44, 0x24, 0x14, 0x8B, 0x4C, 0x24, 0x10, 0x8B, 0x54, 0x24, 0x0C,
    0x50, 0x8B, 0x44, 0x24, 0x0C, 0x51, 0x8B, 0x4C
};
#define DECAL_SUBMIT_PROLOGUE 8u

/* --- 0x00487672  which way is "nearer"? ------------------------------------------------------ *
 * This engine does not always use a less-THAN DEPTH TEST, and getting that wrong makes every
 * value of DepthBias useless; it pushes the decal AWAY instead of forward. The compare function
 * is chosen from a device capability at run time:
 *
 *     00487672  mov eax,[0x85596C]        ; the device record
 *     00487677  mov ecx,[eax+0x24]
 *     0048767A  and cl, 0x10              ; can this device do GREATER?
 *     0048767D  neg cl / sbb ecx,ecx
 *     00487681  and ecx, 0x0E
 *     00487684  add ecx, 2                ; -> 0x10 when it can, otherwise 2
 *     00487687  mov [0x866FC8], ecx
 *
 * [0x866FC8] then goes through the caps-bit-to-D3DCMP mapper at 0x0048AAF9, whose whole table is
 * 0x01->1 NEVER, 0x04->3 EQUAL, 0x08->4 LESSEQUAL, 0x10->5 GREATER, 0x20->6, 0x40->7, 0x80->8. So
 * the mask 0x10 is D3DCMP_GREATER: a REVERSED depth test, in which nearer means a LARGER z.
 *
 * Any Direct3D 9 device advertises GREATER, so on a translation layer this is the branch that runs,
 * which is exactly why this DLL's first release changed nothing at any magnitude. The direction
 * is read per call rather than latched at install: at the host entry point the graphics are not up
 * yet and the cell is still zero. The engine reads it late for the same reason, which is why the
 * third sort key of its deferred draw list is direction-dependent rather than fixed. */
static const uint8_t SIG_ZFUNC_SELECT[] = {
    0xA1, 0x6C, 0x59, 0x85, 0x00, 0x8B, 0x48, 0x24, 0x80, 0xE1, 0x10, 0xF6,
    0xD9, 0x1B, 0xC9, 0x83, 0xE1, 0x0E, 0x83, 0xC1, 0x02, 0x89, 0x0D, 0xC8,
    0x6F, 0x86, 0x00
};
#define OFFSET_ZFUNC_MASK_OPERAND 23u
#define ZFUNC_MASK_GREATER      0x10u   /* D3DCMP_GREATER: nearer is a LARGER z */

/* --- 0x0048825B  the ZBIAS branch, and the actual fix ---------------------------------------- *
 * This is the only device state in the whole image that is unique to decals. A census of all 79
 * `call [reg+0x58]` (IDirect3DDevice3::SetRenderState) sites finds render state 47 at exactly two,
 * 0x00488279 and 0x00488285, and both are gated on the decal bit 0x00100000:
 *
 *     0048825B  test dword [esp+0x14], 0x100000   ; the DELTA carries the decal bit
 *     00488263  je   0x488288
 *     00488265  mov  eax,[0x6FC984]
 *     0048826A  test ebx, 0x100000                ; the FLAGS carry it
 *     00488270  74 0C   je 0x48827E               ; <-- the one byte this patch changes
 *     00488272  push 1 / push 0x2F / call [ecx+0x58]     ; ZBIAS = 1
 *     0048827E  push 0 / push 0x2F / call [edx+0x58]     ; ZBIAS = 0
 *
 * And the engine does not need the bias. The decal's vertices are a bit-identical copy of the host
 * polygon's (0x0041C824 `rep movsd`, only u/v and the diffuse alpha byte replaced), so its depth
 * equals the host's EXACTLY, and the deferred flush relaxes the compare for precisely that:
 *
 *     00488007  cmp eax,2 / je 0x48801E
 *     00488012  mov [0x866FC8], 0x40      ; -> mapper 0x48AAF9 -> 7 = D3DCMP_GREATEREQUAL
 *     0048801E  mov [0x866FC8], 8         ; -> 4 = D3DCMP_LESSEQUAL
 *
 * The decal passes ON EQUALITY. ZBIAS=1 is belt and braces for hardware where that equality was not
 * exact. D3DRENDERSTATE_ZBIAS has no Direct3D 9 equivalent (D3D8's 47 became D3D9's float
 * D3DRS_DEPTHBIAS 195; 47 is unassigned), so a translation layer must invent one, and the
 * conventional conversion assumes a LESS-style buffer and SUBTRACTS. Under this engine's reversed
 * depth that turns "equal" into "strictly less" and kills every decal pixel everywhere, while
 * nothing else in the game changes, because nothing else ever sets ZBIAS. It also swamps a
 * per-vertex nudge of any sane size, which is why DepthBias alone did nothing.
 *
 * Forcing the ZBIAS=0 arm restores exactly the configuration the equality contract needs. */
static const uint8_t SIG_ZBIAS_BRANCH[] = {
    0xF7, 0x44, 0x24, 0x14, 0x00, 0x00, 0x10, 0x00, 0x74, 0x23, 0xA1, 0x84,
    0xC9, 0x6F, 0x00, 0xF7, 0xC3, 0x00, 0x00, 0x10, 0x00, 0x74, 0x0C
};
#define OFFSET_ZBIAS_BRANCH_OPCODE 21u
#define ZBIAS_BRANCH_JE            0x74u   /* je  0x48827E, take ZBIAS=1 when the bit is set */
#define ZBIAS_BRANCH_JMP           0xEBu   /* jmp 0x48827E, always take ZBIAS=0 */

enum {
    SITE_DECAL_SUBMIT,
    SITE_ZFUNC_SELECT,
    SITE_ZBIAS_BRANCH,
    SITE_COUNT
};

/* SIGNATURE_ENTRY_DETOUR, not SIGNATURE_ENTRY: this site IS a detour target, so the resolver has
 * to be told how many leading bytes a detour overwrites. Nothing else hooks it today, but a pattern
 * that starts with the prologue stops matching the moment anything does, and the symptom is a
 * "site did not resolve" warning that looks like an unsupported build. See common/signature.h. */
static signature_t sites[SITE_COUNT] = {
    SIGNATURE_ENTRY_DETOUR("decal_submit", SIG_DECAL_SUBMIT, DECAL_SUBMIT_PROLOGUE),
    SIGNATURE_ENTRY("zfunc_select", SIG_ZFUNC_SELECT),
    SIGNATURE_ENTRY("zbias_branch", SIG_ZBIAS_BRANCH)
};

#define VERTEX_STRIDE_FLOATS  8u    /* 0x20 bytes, proven by `add eax,0x20` at 0x0041C83B */
#define VERTEX_Z_INDEX        2u    /* x, y, Z, rhw, diffuse, specular, u, v */
#define MAX_VERTICES          4u    /* the engine's own buffer is f32[4][8]; a polygon is 3 or 4 */

#define DEPTH_BIAS_DEFAULT    0.0f
#define DEPTH_BIAS_MAX        0.01f
#define SUBMIT_REPORT_INTERVAL 2000u   /* a handful of lines a session, not a flood */

typedef int32_t (__cdecl *decal_submit_fn_t)(void *stage, uint32_t render_state, float *vertices,
                                             uint32_t count, int32_t clipped, void *sorted);

typedef struct decal_fix_state {
    bool            installed;
    bool            enabled;
    bool            neutralise_zbias;
    bool            zbias_neutralised;
    float           depth_bias;
    detour_t        submit;
    uint32_t        state_clear;     /* bits removed from the decal render state word */
    uint32_t        state_set;       /* bits added to it */
    const uint32_t *zfunc_mask;      /* NULL = the direction could not be read */
    bool            reported_direction;
    bool            warned_about_count;
    uint32_t        submits;         /* decal fans handed to the deferred queue */
    uint32_t        refused;         /* ... of which std3D_deferFace would not take */
} decal_fix_state_t;

static decal_fix_state_t decal_state;

static void neutralise_zbias(void);

static void load_config(void)
{
    decal_state.enabled    = ini_read_bool(DECAL_FIX_SECTION, "Enabled", true);
    decal_state.neutralise_zbias =
        ini_read_bool(DECAL_FIX_SECTION, "NeutraliseZBias", true);
    /* DEFAULT 0. The geometric nudge was this DLL's first theory and it is NOT the fix: the engine
     * relies on its decal being at EXACTLY the host's depth and relaxes the compare to
     * GREATEREQUAL for it. Left as a knob for the case where a layer still cannot reproduce an
     * exact float equality, but off unless someone has a reason. */
    decal_state.depth_bias = ini_read_float(DECAL_FIX_SECTION, "DepthBias", DEPTH_BIAS_DEFAULT);

    /* The decal's render state word, bit by bit, from the ini. This is an INSTRUMENT, not a
     * feature: the word 0x0010AE40 asks for at least two things Direct3D 9 removed. ZBIAS
     * (bit 0x00100000, rs 47) and TEXTUREMAPBLEND=DECALALPHA (bit 0x00000400, rs 21), and a
     * translation layer may honour, drop or mistranslate either. Which one costs us the decal can
     * be settled in single runs instead of one rebuild per suspect. Both default to 0, so the word
     * reaches the engine exactly as it left bapvrt_drawPolyDecals. */
    decal_state.state_clear = (uint32_t)ini_read_int(DECAL_FIX_SECTION, "StateClear", 0);
    decal_state.state_set   = (uint32_t)ini_read_int(DECAL_FIX_SECTION, "StateSet", 0);

    if (decal_state.depth_bias < 0.0f) {
        decal_state.depth_bias = 0.0f;
    } else if (decal_state.depth_bias > DEPTH_BIAS_MAX) {
        log_warning("DepthBias=%.6f is above the %.2f ceiling and would pull decals "
                    "through the geometry in front of them, clamped",
                    (double)decal_state.depth_bias, (double)DEPTH_BIAS_MAX);
        decal_state.depth_bias = DEPTH_BIAS_MAX;
    }
}

/* Turn the conditional at 0x00488270 into an unconditional jump, so the decal arm always issues
 * ZBIAS = 0 instead of ZBIAS = 1. ONE byte, and the byte that is there is checked first: if it is
 * not the `je` we expect, nothing is written. */
static void neutralise_zbias(void)
{
    uintptr_t     site = sites[SITE_ZBIAS_BRANCH].address;
    uintptr_t     opcode;
    const uint8_t was = ZBIAS_BRANCH_JE;
    const uint8_t now = ZBIAS_BRANCH_JMP;

    if (!decal_state.neutralise_zbias) {
        log_info("NeutraliseZBias=0, the engine keeps asking the device for D3DRENDERSTATE_ZBIAS, "
                 "which Direct3D 9 does not have. Expect no decals on a Dd7to9 layer.");
        return;
    }
    if (site == 0) {
        log_warning("the ZBIAS branch did not resolve, decals keep depending on a render state "
                    "Direct3D 9 removed, and this is the one that matters");
        return;
    }

    opcode = site + OFFSET_ZBIAS_BRANCH_OPCODE;
    if (!patch_validate_bytes(opcode, &was, sizeof was)) {
        log_warning("the byte at %08X is not the expected `je` - refused, nothing written",
                    (unsigned)opcode);
        return;
    }
    if (patch_write_bytes(opcode, &now, sizeof now) != PATCH_RESULT_OK) {
        log_error("could not write the ZBIAS branch at %08X - nothing is changed",
                  (unsigned)opcode);
        return;
    }

    decal_state.zbias_neutralised = true;
    log_info("ZBIAS neutralised at %08X (je -> jmp): the decal arm now issues "
             "SetRenderState(D3DRENDERSTATE_ZBIAS, 0) instead of 1. That state has no Direct3D 9 "
             "equivalent, and a translation layer that synthesises a SUBTRACTING depth bias for it "
             "turns the engine's exact-equality decal test into a strict less-than and loses every "
             "decal. The engine does not need the bias: the decal's vertices are a bit-identical "
             "copy of the host polygon's, and the deferred flush relaxes the compare to "
             "GREATEREQUAL at 00488012 precisely so that equality passes.",
             (unsigned)opcode);
}

/* Read the address of the compare-function cell out of the selector's own store operand, with the
 * image check that makes the read safe. No address is written down here. */
static void resolve_zfunc_mask(void)
{
    uintptr_t site = sites[SITE_ZFUNC_SELECT].address;
    uint32_t  address = 0;

    decal_state.zfunc_mask = NULL;
    if (site == 0) {
        return;
    }
    if (!memory_read_u32(site + OFFSET_ZFUNC_MASK_OPERAND, &address)) {
        return;
    }
    if (!memory_is_inside_image(address, sizeof(uint32_t))) {
        return;
    }
    decal_state.zfunc_mask = (const uint32_t *)(uintptr_t)address;
}

/* Which way is "towards the camera" in this session. Read from the engine's own choice rather than
 * assumed, because both answers occur and the wrong one is not a smaller effect; it is the exact
 * opposite one, at every magnitude. */
static bool depth_test_is_reversed(void)
{
    uint32_t mask = 0;

    if (decal_state.zfunc_mask == NULL) {
        return false;   /* unknown: assume the conventional LESS, and say so at install */
    }
    if (!memory_read_u32((uintptr_t)decal_state.zfunc_mask, &mask)) {
        return false;
    }
    return (mask & ZFUNC_MASK_GREATER) != 0u;
}

/* Pull every vertex of this fan towards the camera by the configured amount. The vertices are
 * pre-transformed, so z is device depth in [0,1]; the clamps keep a decal on geometry that already
 * sits against a clip plane from leaving that range, which some drivers reject outright rather
 * than clamping themselves. */
static void bias_towards_camera(float *vertices, uint32_t count, bool reversed)
{
    uint32_t i;

    for (i = 0; i < count; ++i) {
        float *z = &vertices[i * VERTEX_STRIDE_FLOATS + VERTEX_Z_INDEX];

        if (reversed) {
            float biased = *z + decal_state.depth_bias;   /* GREATER: nearer is LARGER */
            *z = (biased > 1.0f) ? 1.0f : biased;
        } else {
            float biased = *z - decal_state.depth_bias;   /* LESS: nearer is SMALLER */
            *z = (biased < 0.0f) ? 0.0f : biased;
        }
    }
}

static int32_t __cdecl hook_decal_submit(void *stage, uint32_t render_state, float *vertices,
                                         uint32_t count, int32_t clipped, void *sorted)
{
    decal_submit_fn_t original = (decal_submit_fn_t)decal_state.submit.original;
    uint32_t          state = (render_state & ~decal_state.state_clear) | decal_state.state_set;
    int32_t           result;

    /* Everything that reaches this function is a decal, the site has one caller. The only reasons
     * to leave the geometry alone are a switched-off feature, a zero bias, or a fan that is not the
     * shape the engine's own buffer can hold, which would mean we are wrong about the arguments. */
    if (decal_state.enabled && decal_state.depth_bias > 0.0f && vertices != NULL &&
        count > 0u && count <= MAX_VERTICES &&
        memory_is_readable_range((uintptr_t)vertices,
                                 count * VERTEX_STRIDE_FLOATS * sizeof(float))) {
        bool reversed = depth_test_is_reversed();

        if (!decal_state.reported_direction) {
            uint32_t v;

            decal_state.reported_direction = true;
            log_info("the device gave the engine a %s depth test, so a decal is pulled forward by "
                     "%+.5f in device depth",
                     reversed ? "REVERSED (D3DCMP_GREATER)" : "conventional (D3DCMP_LESS)",
                     reversed ? (double)decal_state.depth_bias : -(double)decal_state.depth_bias);
            log_info("decal render state %08X -> %08X (StateClear=%08X StateSet=%08X). Bit 0x400 "
                     "is TEXTUREMAPBLEND=DECALALPHA and bit 0x100000 is ZBIAS; Direct3D 9 removed "
                     "BOTH, and this is where to try them one at a time",
                     (unsigned)render_state, (unsigned)state,
                     (unsigned)decal_state.state_clear, (unsigned)decal_state.state_set);

            /* ONE-SHOT CENSUS, and it is the reason this DLL stopped guessing. Every magnitude
             * argument here rests on z being device depth in [0,1]; that is an ASSUMPTION about a
             * value the engine computes itself, and it has never been read. Print the first fan
             * verbatim so the bias can be chosen against real numbers instead of a convention. */
            for (v = 0; v < count; ++v) {
                const float *p = &vertices[v * VERTEX_STRIDE_FLOATS];

                log_info("  first fan vertex %u: x=%.3f y=%.3f  z=%.9f  rhw=%.9f  (view depth "
                         "1/rhw = %.4f)", (unsigned)v, (double)p[0], (double)p[1], (double)p[2],
                         (double)p[3], (p[3] != 0.0f) ? (double)(1.0f / p[3]) : 0.0);
            }
        }
        bias_towards_camera(vertices, count, reversed);
    } else if (count > MAX_VERTICES && !decal_state.warned_about_count) {
        decal_state.warned_about_count = true;
        log_warning("a decal fan arrived with %u vertices, more than the engine's own "
                    "buffer holds, the geometry is left untouched and decals stay invisible. "
                    "The argument reading in this DLL is wrong; do not raise the limit to hide it.",
                    (unsigned)count);
    }

    result = original(stage, state, vertices, count, clipped, sorted);

    /* The function answers the question we have been asking. 0x00487F40 returns 0 when
     * std3D_deferFace (0x00487D20) refuses the face, the deferred pool is full, or the entry was
     * rejected, and in that case std3D_heapPush is never reached and the decal is dropped without
     * a word. It returns 1 only after the entry is on the heap. Counting that is the difference
     * between "the decal was submitted" and "the decal was accepted", and nothing upstream of here
     * can tell the two apart. */
    ++decal_state.submits;
    if (result == 0) {
        ++decal_state.refused;
    }
    if (decal_state.submits == 1u || (decal_state.submits % SUBMIT_REPORT_INTERVAL) == 0u) {
        log_info("deferred queue: %u decal fans submitted, %u REFUSED by std3D_deferFace "
                 "(clipped=%d, sorted record %s)",
                 (unsigned)decal_state.submits, (unsigned)decal_state.refused, (int)clipped,
                 (sorted != NULL) ? "present" : "NULL");
    }
    return result;
}

void decal_fix_install(void)
{
    if (decal_state.installed) {
        return;
    }
    decal_state.installed = true;

    log_init("decal_fix", false);   /* BEFORE anything that logs, without it every line is lost,
                                     * including the ones that report a refusal */

    /* And this one too, for the same reason: common/ is a STATIC library, so every DLL carries its
     * own copy of both the logging state and the host-image state. Another DLL having resolved the
     * image four slots earlier does nothing for us. Without it host_image_text() is 0, the scanner
     * searches an empty range, and the site reports "0 matches", which reads in the log exactly
     * like an unsupported executable and sent this DLL's first release chasing a phantom. */
    if (!host_image_resolve()) {
        log_error("no 32-bit host image, decals keep depending on D3DRENDERSTATE_ZBIAS");
        return;
    }

    load_config();
    if (!decal_state.enabled) {
        log_info("disabled");
        return;
    }

    signature_resolve_table(sites, SITE_COUNT);
    if (sites[SITE_DECAL_SUBMIT].address == 0) {
        log_warning("the decal fan submit did not resolve, decals keep depending on "
                    "D3DRENDERSTATE_ZBIAS and stay invisible on a Direct3D 9 translation layer");
        return;
    }

    if (!detour_install(&decal_state.submit, sites[SITE_DECAL_SUBMIT].address,
                        (const void *)hook_decal_submit, DECAL_SUBMIT_PROLOGUE)) {
        log_error("the detour at %08X failed, nothing is changed",
                  (unsigned)sites[SITE_DECAL_SUBMIT].address);
        return;
    }

    neutralise_zbias();
    resolve_zfunc_mask();
    if (decal_state.zfunc_mask == NULL) {
        log_warning("the depth compare function could not be located, assuming the conventional "
                    "LESS test. If decals stay invisible, that assumption is the first suspect: "
                    "this engine takes D3DCMP_GREATER whenever the device offers it, and then the "
                    "bias has to be applied in the OTHER direction");
    }

    log_info("decals are pulled %.5f towards the camera in device depth at %08X "
             "(one caller: bapvrt_drawPolyDecals). D3DRENDERSTATE_ZBIAS, which is what the engine "
             "asks for and what Direct3D 9 removed, is no longer what decides whether a scorch "
             "mark or a ground shadow survives the depth test.",
             (double)decal_state.depth_bias, (unsigned)sites[SITE_DECAL_SUBMIT].address);
}
