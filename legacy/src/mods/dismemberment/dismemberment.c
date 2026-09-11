/* dismemberment.c: which node the blade really hit, and a type error that made the feature useless.
 *
 * ==============================================================================================
 * The feature is not new. It is complete in the retail binary and it is live:
 *
 *   bapobj_detachNode 0x41433B   allocates its OWN bapObj for the piece, binds the same asset,
 *                                hides everything outside the subtree through
 *                                bapobj_hideMeshesBelow, plays clip usage 6 with a crossfade,
 *                                sets pNodeHidden[n]=1 on the BODY and returns the throw
 *                                direction nodeMat[0].t, nodeMat[n].t.
 *   enemy_detachPiece 0x42E900   initial impulse into the mailbox, then task_register(0x42F64C)
 *                                = candy_stuntTick, the flight.
 *   enemy_receiveDamage 0x433803 already calls it at 0x4338AB.
 *
 * It has two defects. This file repairs the first and goes around the second.
 *
 * DEFECT 1. THE WRONG NODE.
 *   0x433879 asks bapobj_hitNodeSpheresVsCylinder(victim->pBody, attacker). The probe uses
 *   attacker->pos (+0x18) = the player's FEET and attacker->cylinderRadius (+0xB8) = the whole
 *   body cylinder. It therefore picks the node nearest the ATTACKER'S BODY AXIS, not the one the
 *   blade touched. On a head strike the leg flies off.
 *   The right value is already in g_msgContactNode [0x869258] at that moment, written at
 *   0x41216F from the blade sphere (node `sabreblad01`, r = 0.15..0.25 u), nine bytes before the
 *   handler call.
 *
 * DEFECT 2. the gate is practically shut.
 *   `stateFlags & 4` = "limbs severable" comes verbatim from enmyRecord+0x00. NO opcode can set
 *   it: the jump table at 0x435409 only knows 0x20/0x80/0x1000/0x8000000. A census over all 2250
 *   ENMY records of the eleven levels: SEVEN carry it (FEDSHIP #16/#19/#27/#28/#39, QUEEN
 *   #157/#158, and the last two are breakable TREES, not enemies).
 *
 *   Simply opening that gate would NOT be a cosmetic change. 0x4338B3 doubles the damage in the
 *   SAME branch, and `partBonus` is, by census, set exactly on the three saber codes
 *   0x3B/0x3C/0x3D, an open gate would therefore double every saber hit against every NPC
 *   (8/12/15 -> 16/24/30). So the gate is NOT touched; we act at the DEATH gate instead.
 *
 * ==============================================================================================
 * The six safety gates, all byte-addressable:
 *
 *  1. MAILBOX DISCRIMINATOR  msgA==1 && msgB==0. bapobj_collidePairs sends four shapes: cylinder
 *     (0,0), blade-vs-blade (1,1), a NULL post (0,1) and the NODE post (1,0). Only the last one
 *     writes g_msgContactNode immediately beforehand. Without this gate a parry frame would pick
 *     up the node of the previous pair.
 *  2. IDENTITY  msgSelf==attacker && msgOther==victimBody. enemy_receiveDamage has SEVEN callers;
 *     for those outside a fresh post the mailbox holds stale values.
 *  3. BOUND  0 < n < model3->numNodes. MANDATORY: bapobj_detachNode checks NOTHING. 0x4143B4
 *     computes `imul n,0xB4` and reads node+0x48 unchecked; 0x4144BE writes unchecked into
 *     pNodeHidden[n]. The garbage index also lands in partIo, and `if (partIo & 8) health = 0`,
 *     an out-of-range index has roughly a 50 % chance of instant death.
 *  4. NOT ALREADY OFF  pNodeHidden[n] == 0. detachNode does not check this and would sever the
 *     same node twice: two flying objects, the second showing a hidden mesh.
 *  5. BODY-PART MASK  node+0x48 & 0x6E rather than the constant `part > 4`. The constant is tuned
 *     to baronsec-shaped skeletons (0..4 = dummy01/hips/waistdum/waist/chest); on tc14.baf the
 *     chest is node 8 and would be released. The mask is the AUTHORED statement:
 *     0x2 lArm, 0x4 rArm, 0x8 head, 0x20 lLeg, 0x40 rLeg; 0x1 = generic, 0x10 = hips.
 *     But only TEN of 265 rigs carry a mask at all, so there is a documented fallback below.
 *  6. THE DROID CASE  a node with no mesh anywhere in its subtree cannot be severed at all.
 *
 * INDEX SPACE: settled. A census over 265 unique actor .baf files says node+0x44 (matrix slot) ==
 * the ordinal in 3251/3251 nodes. g_msgContactNode and the return value of 0x412E22 live in the
 * SAME space (both are pNode->+0x44), so the substitution is type-safe.
 *
 * SIZE NOTE. Over the 600 line mark, under the 900 hard limit. Most of the excess is the byte
 * evidence above rather than code. The six gates cannot be reviewed without it, and it has to
 * stand at the site.
 */
#include "dismemberment.h"

#include "limb_flight.h"

#include "common/detour.h"
#include "common/frame_hook.h"
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

#define DISMEMBERMENT_SECTION "dismemberment"

/* --- 0x0043386E  enemy_receiveDamage: the node probe of the limb severing -------------------- *
 *   8B 4D 0C           mov ecx,[ebp+0x0C]        ; attacker (bapObj*)
 *   51                 push ecx
 *   8B 55 08           mov edx,[ebp+8]           ; victim (character*)
 *   8B 42 34           mov eax,[edx+0x34]        ; victim->pBody
 *   50                 push eax
 *   E8 <rel32>         call 0x412E22             ; <- at +0x0B
 *
 * The original CALL must not be skipped: 0x412E22 rebuilds the pose first (0x412E7C, behind the
 * poseStamp gate) and bapobj_detachNode afterwards reads nodeMat[0].t, nodeMat[n].t as the throw
 * direction. We always call it and replace only the RETURN VALUE. */
static const uint8_t SIG_SEVER_PROBE[] = {
    0x8B, 0x4D, 0x0C, 0x51, 0x8B, 0x55, 0x08, 0x8B, 0x42, 0x34, 0x50
};
#define OFFSET_PROBE_CALL 0x0Bu
/* enemy_detachPiece is the target of the call at probe + 0x3D (retail 0x4338AB). */
#define OFFSET_DETACH_PIECE_CALL 0x3Du

/* --- 0x0043707D  enemy_onContact: the DEATH gate --------------------------------------------- *
 *   8B 4D FC           mov ecx,[ebp-4]           ; victim (character*)
 *   8B 51 14           mov edx,[ecx+0x14]        ; stateFlags
 *   81 E2 00000100     and edx,0x10000           ; "the script owns the death"
 *   85 D2 / 75 0A      test/jne
 *   8B 45 FC / C7 40 20 0B000000                 ; state = kEnemy_Death (11)
 *
 * The site is only reached when health <= 0 (0x437070 `cmp [eax+0x38],0` / `jg`), i.e. exactly on
 * the lethal hit. Prologue 6 bytes (3+3), clean boundary. This 26-byte pattern holds NO rel32. */
static const uint8_t SIG_DEATH_GATE[] = {
    0x8B, 0x4D, 0xFC, 0x8B, 0x51, 0x14, 0x81, 0xE2, 0x00, 0x00, 0x01, 0x00,
    0x85, 0xD2, 0x75, 0x0A, 0x8B, 0x45, 0xFC, 0xC7, 0x40, 0x20, 0x0B, 0x00, 0x00, 0x00
};
#define DEATH_GATE_PROLOGUE_SIZE 6u

/* --- 0x00414C99  bapobj_sendMessage: the out-of-line filler of the mailbox ------------------- *
 *   55 8B EC              push ebp / mov ebp,esp
 *   8B 45 08  A3 <self>   mov eax,[ebp+8]    / mov [g_msgSelf],eax      operand at +0x07
 *   8B 4D 0C  89 0D <oth> mov ecx,[ebp+0xC]  / mov [g_msgOther],ecx     operand at +0x10
 *   8B 55 10  89 15 <cod> mov edx,[ebp+0x10] / mov [g_msgCode],edx      operand at +0x19
 *   8B 45 14  A3 <a>      mov eax,[ebp+0x14] / mov [g_msgA],eax         operand at +0x21
 *   8B 4D 18  89 0D <b>   mov ecx,[ebp+0x18] / mov [g_msgB],ecx         operand at +0x2A
 *   8B 55 08  8B 42 0C A3 the sender's own +0xC, into the impact slot this never reads
 *
 * Resolved, never detoured. Eleven other sites in the image post a message with an inline copy of
 * these stores and every one names the same cells; the gates below read five of them, so the
 * cells are read out of this function's operands rather than written down. The retail image keeps
 * them contiguous from 0x869240, and that was an observation about one build, not a property of
 * the engine. */
static const uint8_t SIG_SEND_MESSAGE[] = {
    0x55, 0x8B, 0xEC,
    0x8B, 0x45, 0x08, 0xA3, 0x00, 0x00, 0x00, 0x00,
    0x8B, 0x4D, 0x0C, 0x89, 0x0D, 0x00, 0x00, 0x00, 0x00,
    0x8B, 0x55, 0x10, 0x89, 0x15, 0x00, 0x00, 0x00, 0x00,
    0x8B, 0x45, 0x14, 0xA3, 0x00, 0x00, 0x00, 0x00,
    0x8B, 0x4D, 0x18, 0x89, 0x0D, 0x00, 0x00, 0x00, 0x00,
    0x8B, 0x55, 0x08, 0x8B, 0x42, 0x0C, 0xA3
};
static const uint8_t MSK_SEND_MESSAGE[] = {
    0xFF, 0xFF, 0xFF,
    0xFF, 0xFF, 0xFF, 0xFF, 0x00, 0x00, 0x00, 0x00,
    0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0x00, 0x00, 0x00, 0x00,
    0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0x00, 0x00, 0x00, 0x00,
    0xFF, 0xFF, 0xFF, 0xFF, 0x00, 0x00, 0x00, 0x00,
    0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0x00, 0x00, 0x00, 0x00,
    0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF
};
_Static_assert(sizeof SIG_SEND_MESSAGE == sizeof MSK_SEND_MESSAGE,
               "the send message pattern and its mask are different lengths");
#define SEND_MESSAGE_SELF_OPERAND  0x07u
#define SEND_MESSAGE_OTHER_OPERAND 0x10u
#define SEND_MESSAGE_CODE_OPERAND  0x19u
#define SEND_MESSAGE_A_OPERAND     0x21u
#define SEND_MESSAGE_B_OPERAND     0x2Au

/* --- 0x004121CF and 0x0041216C  bapobj_collidePairs: the two NODE posts ---------------------- *
 *   8B 55 FC              mov edx,[ebp-4]              the node the probe answered
 *   89 15 <node>          mov [g_msgContactNode],edx   operand at +0x05
 *   6A 00 6A 01           push 0 / push 1              b = 0, a = 1, the (1,0) shape of gate 1
 *   ...                   the pair's own code, other, self, and the call to sendMessage
 *
 * The block runs twice, once for each direction of the pair with the registers the compiler
 * chose for that copy, and the two operands have to name one cell. That agreement is the check
 * that the pattern found the post and not another store to the same register. */
static const uint8_t SIG_NODE_POST_FIRST[] = {
    0x8B, 0x4D, 0xFC, 0x89, 0x0D, 0x00, 0x00, 0x00, 0x00, 0x6A, 0x00, 0x6A, 0x01,
    0x8B, 0x55, 0xEC, 0x8B, 0x82, 0xAC, 0x00, 0x00, 0x00, 0x50,
    0x8B, 0x4D, 0xE8, 0x51, 0x8B, 0x55, 0xEC, 0x52, 0xE8
};
static const uint8_t SIG_NODE_POST_SECOND[] = {
    0x8B, 0x55, 0xFC, 0x89, 0x15, 0x00, 0x00, 0x00, 0x00, 0x6A, 0x00, 0x6A, 0x01,
    0x8B, 0x45, 0xE8, 0x8B, 0x88, 0xAC, 0x00, 0x00, 0x00, 0x51,
    0x8B, 0x55, 0xEC, 0x52, 0x8B, 0x45, 0xE8, 0x50, 0xE8
};
static const uint8_t MSK_NODE_POST[] = {
    0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0x00, 0x00, 0x00, 0x00, 0xFF, 0xFF, 0xFF, 0xFF,
    0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF,
    0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF
};
_Static_assert(sizeof SIG_NODE_POST_FIRST == sizeof MSK_NODE_POST &&
               sizeof SIG_NODE_POST_SECOND == sizeof MSK_NODE_POST,
               "the node post patterns and their mask are different lengths");
#define NODE_POST_OPERAND 0x05u

/* --- 0x00414436  bapobj_detachNode: THE TYPE ERROR ------------------------------------------- *
 *   8B 45 EC              mov  eax,[ebp-0x14]        ; keep = a NODE ordinal
 *   50                    push eax                   ; arg4
 *   8B 4D F8 8B 51 58     mov  edx,[model3+0x58]     ; the node list
 *   52                    push edx                   ; arg3
 *   8B 45 F8 50           push model3                ; arg2
 *   8B 4D F0 8B 91 9C..   push pieceObj->pThing      ; arg1
 *   E8 <rel32>            call bapobj_hideMeshesBelow 0x414BD7
 *
 * And over there 0x414BE0/0x414BE3 compares `node->meshIdx` (node+0x4C) against exactly that
 * `keep`:
 *
 *   8B 48 4C   mov ecx,[eax+0x4C]      ; a MESH index
 *   3B 4D 14   cmp ecx,[ebp+0x14]      ; against a NODE index
 *
 * Two different numbering spaces. Measured over 265 unique actor models and 2986 severable nodes:
 *
 *     the right piece      55   1.8 %
 *     a foreign subtree  2624  87.9 %
 *     nothing at all      307  10.3 %
 *
 * That is the cause of BOTH complaints: "does not work on all NPCs" is the 10.3 %, and the
 * apparent see-through is the 87.9 %, a torso with a foreign piece missing.
 *
 * The shortcut is wrong. `meshIdx = nodeIndex - 1` holds in only 78.8 % of cases (-2 in 17.6 %,
 * 0 in 1.9 %, down to -5), and exring.baf does carry a mesh on node 0. We READ node+0x4C.
 *
 * And the bolt belongs here, not only in the blade-node gate. enemy_detachPiece has TWO callers:
 * 0x4338AB (the damage path, through our gate) and the FSM opcode arm 0x4351B1, which passes
 * `operand - 1` and bypasses the gate entirely. A script can therefore hand in a -1.
 *
 * One thunk on 0x414436 suffices: hideMeshesBelow has exactly two callers, this one and its own
 * recursion. The recursion then compares consistently against the same mesh index, and that index
 * is unique within each model (0 duplicates across all 265 models). */
static const uint8_t SIG_DETACH_HIDE_CALL[] = {
    0x8B, 0x45, 0xEC, 0x50,
    0x8B, 0x4D, 0xF8, 0x8B, 0x51, 0x58, 0x52,
    0x8B, 0x45, 0xF8, 0x50,
    0x8B, 0x4D, 0xF0, 0x8B, 0x91, 0x9C, 0x00, 0x00, 0x00, 0x52,
    0xE8
};
#define OFFSET_HIDE_CALL 0x19u   /* 26 pattern bytes; the E8 is the last one */

enum {
    SITE_SEVER_PROBE,
    SITE_DEATH_GATE,
    SITE_DETACH_HIDE_CALL,
    SITE_SEND_MESSAGE,
    SITE_NODE_POST_FIRST,
    SITE_NODE_POST_SECOND,
    SITE_COUNT
};

static signature_t sites[SITE_COUNT] = {
    SIGNATURE_ENTRY("sever_probe",      SIG_SEVER_PROBE),
    /* A detour target, and declared as one: with a plain pattern the first DLL to detour the gate
     * would have left this one searching for a prologue that is now a jump. */
    SIGNATURE_ENTRY_DETOUR("death_gate", SIG_DEATH_GATE, DEATH_GATE_PROLOGUE_SIZE),
    SIGNATURE_ENTRY("detach_hide_call", SIG_DETACH_HIDE_CALL),
    SIGNATURE_ENTRY_MASKED("send_message",     SIG_SEND_MESSAGE,     MSK_SEND_MESSAGE),
    SIGNATURE_ENTRY_MASKED("node_post_first",  SIG_NODE_POST_FIRST,  MSK_NODE_POST),
    SIGNATURE_ENTRY_MASKED("node_post_second", SIG_NODE_POST_SECOND, MSK_NODE_POST)
};

/* --- bapObj / rdThing / rdModel3 / character offsets ----------------------------------------- */
#define OBJECT_THING       0x9C   /* bapobj_drawAll writes [obj+0x9C]+0x15C */
#define THING_MODEL3       0x04
#define THING_NODE_HIDDEN  0x28
#define MODEL3_NODE_COUNT  0x54   /* proven by the copy loop 0x414456: cmp edx,[ecx+0x54] */
#define MODEL3_NODES       0x58
#define NODE_STRIDE        0xB4
#define NODE_TYPE          0x48   /* the body-part mask */
#define NODE_MESH_INDEX    0x4C   /* < 0 = this node carries no mesh */
#define NODE_CHILD_COUNT   0x54
#define NODE_FIRST_CHILD   0x58
#define NODE_NEXT_SIBLING  0x5C
#define CHARACTER_BODY     0x34   /* 0x433872: mov eax,[edx+0x34] */

#define LIMB_MASK 0x6Eu           /* arms, head, legs, never 0x1 (generic), never 0x10 (hips) */
#define ENGINE_TORSO_NODE_LIMIT 4 /* the engine's own `part > 4` rule at 0x43389D */

#define MAX_SUBTREE_DEPTH    12
#define MAX_PLAUSIBLE_KIDS  128u
#define MAX_PLAUSIBLE_NODES 4096u

#define MESSAGE_CODE_SABER 0x25

typedef int32_t (__cdecl *probe_fn_t)(void *victim_body, void *attacker);
typedef int32_t (__cdecl *detach_piece_fn_t)(void *victim, uint32_t part);
typedef void    (__cdecl *hide_meshes_fn_t)(void *piece_thing, void *model3, uint8_t *nodes,
                                            int keep);

typedef struct dismemberment_state {
    bool              installed;
    limb_config_t     config;

    detour_t          death_gate_detour;
    probe_fn_t        engine_probe;
    detach_piece_fn_t engine_detach_piece;
    hide_meshes_fn_t  engine_hide_meshes;

    volatile const int32_t *message_self;
    volatile const int32_t *message_other;
    volatile const int32_t *message_code;
    volatile const int32_t *message_a;
    volatile const int32_t *message_b;
    volatile const int32_t *message_node;

    /* Does this model carry a body-part mask at all? Cached per model pointer, otherwise the
     * scan walks every node on every hit. */
    const char       *mask_model;
    bool              mask_answer;

    uint32_t          sever_count;
    int               hide_log_count;
} dismemberment_state_t;

static dismemberment_state_t limb_state;

const limb_config_t *limb_config(void)
{
    return &limb_state.config;
}

/* ============================================================================================ */
static float clamp_float(float value, float minimum, float maximum)
{
    if (!(value >= minimum)) {
        return minimum;
    }
    if (value > maximum) {
        return maximum;
    }
    return value;
}

static void load_config(void)
{
    limb_config_t *config = &limb_state.config;
    int            mode;

    /* OFF is the default here and in the shipped ini, and the two have to agree: a reader whose
     * file predates this key would otherwise get the feature the file does not mention. Severing a
     * limb on the killing blow changes how the game plays rather than repairing it, and this
     * project's rule for that class is a switch with a default that leaves the game alone. */
    mode = ini_read_int(DISMEMBERMENT_SECTION, "Mode", (int)LIMB_MODE_OFF);
    if (mode < (int)LIMB_MODE_OFF || mode > (int)LIMB_MODE_ON_DEATH) {
        mode = (int)LIMB_MODE_OFF;
    }
    config->mode = (limb_mode_t)mode;

    config->spin_scale     = clamp_float(ini_read_float(DISMEMBERMENT_SECTION, "SpinScale", 0.35f),
                                         0.0f, 2.0f);
    config->gravity_scale  = clamp_float(ini_read_float(DISMEMBERMENT_SECTION, "GravityScale",
                                                        0.40f), 0.1f, 2.0f);
    config->settle_seconds = clamp_float(ini_read_float(DISMEMBERMENT_SECTION, "SettleSeconds",
                                                        1.20f), 0.0f, 5.0f);
    config->settle_damping = clamp_float(ini_read_float(DISMEMBERMENT_SECTION, "SettleDamping",
                                                        0.80f), 0.1f, 1.0f);
    config->yaw_scale      = clamp_float(ini_read_float(DISMEMBERMENT_SECTION, "YawScale", 0.12f),
                                         0.0f, 2.0f);
    config->diagnostics    = ini_read_bool(DISMEMBERMENT_SECTION, "Diagnostics", false);
}

/* ============================================================================================ */
static bool node_pointer_is_plausible(const void *pointer)
{
    return pointer != NULL && ((uintptr_t)pointer & 3u) == 0 && (uintptr_t)pointer > 0x10000u;
}

/* 30 shipped nodes carry NO mesh (tusken/tathum1/handmaid rthigh+lthigh, nimoid rlegdum, tank
 * waist, ...). For those there is no `keep` that hideMeshesBelow could match, and with no mesh
 * there is nothing to show either. Rather than passing the raw node index through (the old, wrong
 * behaviour), we look for the FIRST mesh IN THE SUBTREE of that node: the topmost visible piece
 * of exactly this limb. If there is none, the node is unusable and is rejected. */
int32_t limb_first_mesh_in_subtree(const uint8_t *node, int depth)
{
    uint32_t       child_count;
    uint32_t       index;
    const uint8_t *child;
    int32_t        mesh;

    if (!node_pointer_is_plausible(node) || depth > MAX_SUBTREE_DEPTH) {
        return -1;
    }
    if (!memory_is_readable_range((uintptr_t)node, NODE_STRIDE)) {
        return -1;
    }

    mesh = *(const int32_t *)(node + NODE_MESH_INDEX);
    if (mesh >= 0) {
        return mesh;
    }

    child_count = *(const uint32_t *)(node + NODE_CHILD_COUNT);
    if (child_count > MAX_PLAUSIBLE_KIDS) {
        return -1;
    }

    child = *(const uint8_t * const *)(node + NODE_FIRST_CHILD);
    for (index = 0; index < child_count && node_pointer_is_plausible(child); ++index) {
        mesh = limb_first_mesh_in_subtree(child, depth + 1);
        if (mesh >= 0) {
            return mesh;
        }
        child = *(const uint8_t * const *)(child + NODE_NEXT_SIBLING);
    }

    return -1;
}

static bool model_has_part_mask(const char *model, const char *nodes, uint32_t node_count)
{
    uint32_t index;

    if (limb_state.mask_model == model) {
        return limb_state.mask_answer;
    }

    limb_state.mask_answer = false;
    for (index = 0; index < node_count; ++index) {
        if ((*(const uint32_t *)(nodes + index * NODE_STRIDE + NODE_TYPE) & ~1u) != 0) {
            limb_state.mask_answer = true;
            break;
        }
    }
    limb_state.mask_model = model;
    return limb_state.mask_answer;
}

/* Gate 5. the authored mask, with a fallback.
 *
 * The census over 265 actor .baf files says: only ten rigs carry a body-part mask at all (anakin,
 * baron, baronsec, obiwan, quigon, queen, panaka, pitdroid, sithmrc2, jawagun); on every other
 * model EVERY node reads 0x1 = generic. A pure `type & 0x6E` gate therefore refuses on those
 * outright, so decapitation did not happen on all enemies.
 *
 * Hence two stages, and the ORDER is the statement:
 *   1. if the model carries a real mask ANYWHERE, that mask is the truth, a node without a limb
 *      bit is not severed, however large its index;
 *   2. if it carries none, the decision falls back to the engine's own rule `part > 4`. That is
 *      tuned to baronsec-shaped skeletons and protects the torso column there. On a foreign rig
 *      it is a heuristic, but it is THE ENGINE'S heuristic, not ours. */
static bool part_mask_allows(uint32_t type, const char *model, const char *nodes,
                             uint32_t node_count, int32_t node_index)
{
    if ((type & LIMB_MASK) != 0) {
        return true;
    }
    if (model_has_part_mask(model, nodes, node_count)) {
        return false;                              /* the model knows better */
    }
    return node_index > ENGINE_TORSO_NODE_LIMIT;   /* the engine's own bound */
}

/* Answers with the node the blade REALLY hit, or 0 when any of the gates is shut. 0 is the safe
 * value: the engine tests `> 0` at both use sites. */
static int32_t blade_node(void *victim_body, void *attacker)
{
    int32_t     node_index;
    const char *thing;
    const char *model;
    const char *nodes;
    uint32_t    node_count;
    uint32_t    type;

    if (limb_state.message_node == NULL || victim_body == NULL) {
        return 0;
    }

    node_index = *limb_state.message_node;
    if (node_index <= 0) {
        return 0;
    }
    if (*limb_state.message_a != 1 || *limb_state.message_b != 0) {
        return 0;                                  /* gate 1: the node post */
    }
    if ((void *)(uintptr_t)*limb_state.message_self  != attacker ||
        (void *)(uintptr_t)*limb_state.message_other != victim_body) {
        return 0;                                  /* gate 2: the same message */
    }

    if (!memory_read((uintptr_t)((const char *)victim_body + OBJECT_THING), &thing,
                     sizeof(thing)) || thing == NULL) {
        return 0;
    }
    if (!memory_read((uintptr_t)(thing + THING_MODEL3), &model, sizeof(model)) || model == NULL) {
        return 0;
    }
    if (!memory_read((uintptr_t)(model + MODEL3_NODE_COUNT), &node_count, sizeof(node_count)) ||
        node_count > MAX_PLAUSIBLE_NODES) {
        return 0;
    }
    if ((uint32_t)node_index >= node_count) {
        return 0;                                  /* gate 3: the bound. MANDATORY */
    }

    {
        /* pNodeHidden can be NULL: rdThing_SetModel sets thing+0x04 = model3 BEFORE it allocates,
         * and returns 0 on an allocation failure. A thing with a valid pModel3 and a NULL
         * pNodeHidden is therefore constructible. */
        const uint32_t *hidden;

        if (!memory_read((uintptr_t)(thing + THING_NODE_HIDDEN), &hidden, sizeof(hidden)) ||
            hidden == NULL ||
            !memory_is_readable_range((uintptr_t)hidden, node_count * sizeof(uint32_t))) {
            return 0;
        }
        if (hidden[node_index] != 0) {
            return 0;                              /* gate 4: not already off */
        }
    }

    if (!memory_read((uintptr_t)(model + MODEL3_NODES), &nodes, sizeof(nodes)) || nodes == NULL) {
        return 0;
    }
    if (!memory_is_readable_range((uintptr_t)nodes, node_count * NODE_STRIDE)) {
        return 0;
    }
    type = *(const uint32_t *)(nodes + (uint32_t)node_index * NODE_STRIDE + NODE_TYPE);

    if (!part_mask_allows(type, model, nodes, node_count, node_index)) {
        return 0;                                  /* gate 5 */
    }

    if (limb_first_mesh_in_subtree((const uint8_t *)nodes + (uint32_t)node_index * NODE_STRIDE, 0)
        < 0) {
        log_info("node %d carries no mesh, in itself or in its subtree, no severing",
                 (int)node_index);
        return 0;                                  /* gate 6: the droid case */
    }

    return node_index;
}

/* ============================================================================================
 * PATCH 1, the node probe. Replaces ONLY the return value.
 * ============================================================================================ */
static int32_t __cdecl hook_sever_probe(void *victim_body, void *attacker)
{
    /* Always call the original: its side effects (bapmap_eulerToMatrixT and, behind the poseStamp
     * gate, rdPuppet_buildJointMatrices) are what bapobj_detachNode's throw direction rests on. */
    int32_t original_node = limb_state.engine_probe(victim_body, attacker);
    int32_t blade;

    if (limb_state.config.mode == LIMB_MODE_OFF) {
        return original_node;
    }

    blade = blade_node(victim_body, attacker);
    if (blade > 0) {
        return blade;
    }
    return original_node;
}

/* ============================================================================================
 * PATCH 2, the death gate. 0x43707D is reached ONLY when health <= 0, i.e. exactly on the
 * lethal hit. This gives "only the last hit severs" a place, without touching the authored gate
 * and without touching the damage doubling.
 *
 * Context here, byte-proven: [ebp-0x04] victim (character*), [ebp-0x0C] attacker,
 * [ebp-0x14] impactCode, [ebp-0x1C] msgCode. thing_disarmContact (0x437102) comes AFTER and only
 * touches the victim's weapon, no ordering conflict.
 * ============================================================================================ */
static void __cdecl on_death_gate(char *frame_pointer)
{
    void   *victim;
    void   *attacker;
    void   *victim_body;
    int32_t node_index;

    if (limb_state.config.mode < LIMB_MODE_ON_DEATH ||
        limb_state.engine_detach_piece == NULL) {
        return;
    }

    victim   = *(void **)(frame_pointer - 0x04);
    attacker = *(void **)(frame_pointer - 0x0C);
    if (victim == NULL || attacker == NULL) {
        return;
    }

    /* `code == 0x25` alone is NOT enough: the 0x25 arm calls enemy_receiveDamage without a msgA
     * test, and 0x25 also comes out of the blade-versus-blade post (1,1). A lethal parry hit would
     * otherwise run with a stale node. blade_node() checks (1,0) and the identity as well. */
    if (*limb_state.message_code != MESSAGE_CODE_SABER) {
        return;
    }

    victim_body = *(void **)((char *)victim + CHARACTER_BODY);
    node_index = blade_node(victim_body, attacker);
    if (node_index <= 0) {
        return;
    }

    ++limb_state.sever_count;
    log_info("killing blow -> node %d severed (#%u)", (int)node_index,
             (unsigned)limb_state.sever_count);
    limb_state.engine_detach_piece(victim, (uint32_t)node_index);
}

static void *death_gate_trampoline;

/* NAKED. Saves the GP registers, the flags AND the complete x87 state. This cuts into the middle
 * of enemy_onContact, so the engine holds live floats in the eight deep x87 register stack when we
 * take control, and on_death_gate below is free to use the FPU. Without fnsave/frstor a handler
 * that pushes past the eighth register does not fault, it marks the register indefinite, and the
 * engine carries on with a NaN where a coordinate was. ebp still holds enemy_onContact's frame
 * pointer after the save: neither pushad nor the sub touches it. The other naked detours in this
 * project save the same state for the same reason.
 */
static void __declspec(naked) hook_death_gate(void)
{
    __asm {
        pushad
        pushfd
        sub    esp, 112
        fnsave [esp]
        push ebp                 /* enemy_onContact's frame pointer */
        call on_death_gate
        add  esp, 4
        frstor [esp]
        add    esp, 112
        popfd
        popad
        jmp  dword ptr [death_gate_trampoline]
    }
}

/* ============================================================================================
 * PATCH 3, the mesh-index translation
 * ============================================================================================ */
static void __cdecl hook_hide_meshes(void *piece_thing, void *model3, uint8_t *nodes, int keep)
{
    int     original_keep = keep;
    int32_t raw_mesh = -1;

    /* Off means the engine's own index. This hook stays installed whatever the setting is, because
     * a detour cannot be taken out again, and the engine severs seven authored pieces of its own
     * that would otherwise keep arriving here translated while the feature reads off. */
    if (limb_state.config.mode != LIMB_MODE_OFF && model3 != NULL && nodes != NULL) {
        uint32_t node_count = *(const uint32_t *)((const char *)model3 + MODEL3_NODE_COUNT);

        if (node_count <= MAX_PLAUSIBLE_NODES && keep >= 0 && (uint32_t)keep < node_count) {
            const uint8_t *node = nodes + (uint32_t)keep * NODE_STRIDE;
            int32_t        mesh = *(const int32_t *)(node + NODE_MESH_INDEX);

            raw_mesh = mesh;
            if (mesh < 0) {
                mesh = limb_first_mesh_in_subtree(node, 0);   /* the droid case */
            }
            if (mesh >= 0 && (uint32_t)mesh < node_count) {
                keep = mesh;
            }
        }
    }

    if (limb_state.hide_log_count < 6) {
        ++limb_state.hide_log_count;
        /* `keep == original` does NOT automatically mean "failed": in roughly 2 % of nodes the
         * node happens to carry exactly the mesh number of its own index. An earlier version of
         * this message confused the two and reported success as failure. */
        log_info("node %d -> mesh %d%s", original_keep, keep,
                 (keep != original_keep) ? " (translated)"
               : (raw_mesh == original_keep)
                     ? " (node and mesh number are identical here, correct, no translation "
                       "needed)"
                     : " (UNCHANGED: neither an own mesh nor one in the subtree)");
    }

    limb_state.engine_hide_meshes(piece_thing, model3, nodes, keep);
}

/* ============================================================================================ */
/* One mailbox cell out of an operand, checked to lie in the image. */
static bool read_cell(uintptr_t site, uint32_t operand, volatile const int32_t **out)
{
    uint32_t address = 0;

    if (!memory_read_u32(site + operand, &address) ||
        !memory_is_inside_image(address, sizeof(int32_t))) {
        return false;
    }
    *out = (volatile const int32_t *)(uintptr_t)address;
    return true;
}

/* The six cells the gates read, out of the two functions that write them. Resolved before
 * anything is patched, and the feature stays off without them: the gates are what make a sever
 * land on the limb that was hit, and reading them from the wrong cells would be worse than not
 * reading them at all. */
static bool resolve_message_mailbox(void)
{
    uintptr_t               send  = sites[SITE_SEND_MESSAGE].address;
    uintptr_t               first = sites[SITE_NODE_POST_FIRST].address;
    uintptr_t               second = sites[SITE_NODE_POST_SECOND].address;
    volatile const int32_t *node_again = NULL;

    if (send == 0 || first == 0 || second == 0) {
        log_error("the message mailbox could not be located (%s, %s, %s), feature OFF",
                  (send != 0) ? "sendMessage found" : "sendMessage NOT found",
                  (first != 0) ? "first node post found" : "first node post NOT found",
                  (second != 0) ? "second node post found" : "second node post NOT found");
        return false;
    }
    if (!read_cell(send, SEND_MESSAGE_SELF_OPERAND,  &limb_state.message_self) ||
        !read_cell(send, SEND_MESSAGE_OTHER_OPERAND, &limb_state.message_other) ||
        !read_cell(send, SEND_MESSAGE_CODE_OPERAND,  &limb_state.message_code) ||
        !read_cell(send, SEND_MESSAGE_A_OPERAND,     &limb_state.message_a) ||
        !read_cell(send, SEND_MESSAGE_B_OPERAND,     &limb_state.message_b) ||
        !read_cell(first, NODE_POST_OPERAND,         &limb_state.message_node) ||
        !read_cell(second, NODE_POST_OPERAND,        &node_again)) {
        log_error("a mailbox operand at %08X or %08X names a cell outside the image, feature OFF",
                  (unsigned)send, (unsigned)first);
        return false;
    }
    if (node_again != limb_state.message_node) {
        log_error("the two node posts at %08X and %08X write different cells, %08X and %08X, so "
                  "this is not the collidePairs expected, feature OFF",
                  (unsigned)first, (unsigned)second, (unsigned)(uintptr_t)limb_state.message_node,
                  (unsigned)(uintptr_t)node_again);
        return false;
    }

    log_info("mailbox self=%08X other=%08X code=%08X a=%08X b=%08X node=%08X, read out of "
             "sendMessage at %08X and the node posts at %08X and %08X",
             (unsigned)(uintptr_t)limb_state.message_self,
             (unsigned)(uintptr_t)limb_state.message_other,
             (unsigned)(uintptr_t)limb_state.message_code,
             (unsigned)(uintptr_t)limb_state.message_a,
             (unsigned)(uintptr_t)limb_state.message_b,
             (unsigned)(uintptr_t)limb_state.message_node,
             (unsigned)send, (unsigned)first, (unsigned)second);
    return true;
}

static void install_probe(void)
{
    uintptr_t site = sites[SITE_SEVER_PROBE].address;
    uintptr_t call_site;
    uintptr_t target;

    if (site == 0) {
        log_warning("sever_probe did not resolve, the wrong node keeps being severed");
        return;
    }

    call_site = site + OFFSET_PROBE_CALL;
    if (!patch_read_call_target(call_site, &target)) {
        log_warning("no usable E8 at %08X, refused", (unsigned)call_site);
        return;
    }

    limb_state.engine_probe = (probe_fn_t)target;
    if (patch_redirect_call(call_site, (const void *)hook_sever_probe) == PATCH_RESULT_OK) {
        log_info("node probe at %08X redirected (the original %08X still runs)",
                 (unsigned)call_site, (unsigned)target);
    } else {
        limb_state.engine_probe = NULL;
        log_error("the probe at %08X could not be written", (unsigned)call_site);
    }
}

static void install_death_gate(void)
{
    uintptr_t probe_site = sites[SITE_SEVER_PROBE].address;
    uintptr_t gate_site  = sites[SITE_DEATH_GATE].address;
    uintptr_t target;

    if (probe_site == 0 ||
        !patch_read_call_target(probe_site + OFFSET_DETACH_PIECE_CALL, &target)) {
        log_warning("enemy_detachPiece did not resolve, death-gate mode OFF, node correction "
                    "stays active");
        return;
    }
    limb_state.engine_detach_piece = (detach_piece_fn_t)target;
    log_info("enemy_detachPiece = %08X", (unsigned)target);

    if (gate_site == 0) {
        log_warning("death_gate did not resolve, death-gate mode OFF");
        return;
    }

    if (detour_install(&limb_state.death_gate_detour, gate_site,
                       (const void *)hook_death_gate, DEATH_GATE_PROLOGUE_SIZE)) {
        death_gate_trampoline = limb_state.death_gate_detour.original;
        log_info("death gate hooked at %08X: on the lethal saber hit the node the blade touched "
                 "is severed", (unsigned)gate_site);
    } else {
        log_error("the death-gate detour at %08X failed", (unsigned)gate_site);
    }
}

static void install_mesh_index_fix(void)
{
    uintptr_t site = sites[SITE_DETACH_HIDE_CALL].address;
    uintptr_t call_site;
    uintptr_t target;

    if (site == 0) {
        log_warning("detach_hide_call did not resolve, the severed piece stays the WRONG subtree "
                    "(87.9 %% of nodes)");
        return;
    }

    call_site = site + OFFSET_HIDE_CALL;
    if (!patch_read_call_target(call_site, &target)) {
        log_warning("no usable E8 at %08X, refused", (unsigned)call_site);
        return;
    }

    limb_state.engine_hide_meshes = (hide_meshes_fn_t)target;
    if (patch_redirect_call(call_site, (const void *)hook_hide_meshes) == PATCH_RESULT_OK) {
        log_info("mesh index translation active (call %08X -> thunk, original %08X). The severed "
                 "piece is now the node the blade hit, it used to be a foreign subtree in "
                 "87.9 %% of cases and nothing at all in 10.3 %%.",
                 (unsigned)call_site, (unsigned)target);
    } else {
        limb_state.engine_hide_meshes = NULL;
        log_error("the call at %08X is not writable", (unsigned)call_site);
    }
}

/* The setting is re-read about once a second so the panel's row takes effect while the game runs.
 * Reading a file every frame would be absurd for a value that changes when somebody presses a key,
 * and the flight constants are only written when the answer actually changes. */
#define POLL_INTERVAL_FRAMES 60u

static void poll_mode(void)
{
    static uint32_t countdown;
    int             mode;

    if (countdown != 0u) {
        countdown--;
        return;
    }
    countdown = POLL_INTERVAL_FRAMES;

    mode = ini_read_int(DISMEMBERMENT_SECTION, "Mode", (int)limb_state.config.mode);
    if (mode < (int)LIMB_MODE_OFF || mode > (int)LIMB_MODE_ON_DEATH ||
        mode == (int)limb_state.config.mode) {
        return;
    }

    limb_state.config.mode = (limb_mode_t)mode;

    /* Said before the flight constants are moved, so the log reads in the order the reader thinks
     * in: the setting changed, and here is what changed because of it. */
    log_info("Mode is now %d, read from the settings file while the game runs", mode);
    limb_flight_set_active(mode != (int)LIMB_MODE_OFF);
}

void dismemberment_install(void)
{
    log_init("dismemberment", false);

    if (limb_state.installed) {
        return;
    }
    if (!host_image_resolve()) {
        log_error("no 32-bit host image, nothing patched");
        return;
    }

    load_config();
    signature_resolve_table(sites, SITE_COUNT);
    if (!resolve_message_mailbox()) {
        return;
    }
    limb_state.installed = true;

    /* Everything is installed whatever the setting says, and the setting is then obeyed at run
     * time. That is not the shape this file had, and the reason for the change is that the
     * developer panel can now turn this on and off while the game runs: a detour has no uninstall
     * here, so a feature that only arms itself when its setting was on at startup can never be
     * switched on later.
     *
     * Every hook and both flight constants therefore ask the mode rather than assuming it, and
     * every one of them returns the engine's own answer when the mode is off. The engine severs
     * seven authored pieces by itself, so off has to mean the shipped game rather than merely
     * "we stop adding more". */
    install_probe();
    install_mesh_index_fix();
    install_death_gate();
    limb_flight_install();

    limb_flight_set_active(limb_state.config.mode != LIMB_MODE_OFF);
    log_info("Mode=%d at startup. The developer panel's Utilities page has a row that writes this "
             "key, so a choice made in game is the one the next run starts with.",
             (int)limb_state.config.mode);

    if (!frame_hook_add(poll_mode)) {
        log_warning("render_frameEnd could not be hooked, so a change made in the panel will not "
                    "be picked up until the next start. Everything the setting selects still "
                    "works; only the live switch is lost.");
    }
}
