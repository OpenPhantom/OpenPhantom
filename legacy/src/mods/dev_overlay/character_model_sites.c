/* character_model_sites.c: the five patterns a model swap stands on.
 *
 * Every site here is a CALL TARGET rather than a detour target, so each one is anchored on its own
 * prologue and used as a function. Nothing in this feature writes over any of the five, and that is
 * what keeps them findable: signature_find_unique matches the prologue and nothing else. The object
 * scale, the glow card release and the lightning arc release declare their prologues and go through
 * the second stage as well, because another module declares the first of them for itself.
 *
 * Not one address is written into a pattern. The three operands handed back at the end are READ out
 * of the one site that proves what each of them is for, because three builds of this engine ship in
 * one installation and four of these five sit at different addresses in the Edit Tool's recompile.
 */
#include "character_model_sites.h"

#include "common/logging.h"
#include "common/memory.h"
#include "common/patch.h"
#include "common/signature.h"

#include <stddef.h>

/* rdThing_SetModel. The three stores that open it are what make it recognisable: the type is set
 * to 1, the model goes into the second word, and the geoset override at +0x134 is put back to -1
 * before anything is allocated. */
static const uint8_t SIG_SET_MODEL[] = {
    0x55, 0x8B, 0xEC, 0x83, 0xEC, 0x0C, 0x57,    /* push ebp; mov ebp,esp; sub esp,0x0C; push edi */
    0x8B, 0x45, 0x08,                            /* mov eax,[ebp+8]      the thing              */
    0xC7, 0x00, 0x01, 0x00, 0x00, 0x00,          /* mov [eax],1          RD_THING_MODEL3         */
    0x8B, 0x4D, 0x08,
    0x8B, 0x55, 0x0C,                            /* mov edx,[ebp+0x0C]   the model               */
    0x89, 0x51, 0x04,                            /* mov [ecx+4],edx                              */
    0x8B, 0x45, 0x08,
    0xC7, 0x80, 0x34, 0x01, 0x00, 0x00,          /* mov [eax+0x134],-1   no geoset override       */
    0xFF, 0xFF, 0xFF, 0xFF
};

/* rdThing_freeArrays. The type test and the first of the four frees. The jump past the test is
 * the only operand in it. */
static const uint8_t SIG_FREE_ARRAYS[] = {
    0x55, 0x8B, 0xEC, 0x83, 0xEC, 0x08,
    0x8B, 0x45, 0x08,
    0x8B, 0x08,
    0x89, 0x4D, 0xF8,
    0x83, 0x7D, 0xF8, 0x01,                      /* cmp [ebp-8],1        the thing's type        */
    0x74, 0x05,
    0xE9, 0x00, 0x00, 0x00, 0x00,                /* jmp past the four frees                      */
    0x8B, 0x55, 0x08,
    0x83, 0x7A, 0x20, 0x00,                      /* cmp [edx+0x20],0     the node matrices       */
    0x74, 0x19
};
static const uint8_t MSK_FREE_ARRAYS[] = {
    0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF,
    0xFF, 0xFF, 0xFF,
    0xFF, 0xFF,
    0xFF, 0xFF, 0xFF,
    0xFF, 0xFF, 0xFF, 0xFF,
    0xFF, 0xFF,
    0xFF, 0x00, 0x00, 0x00, 0x00,
    0xFF, 0xFF, 0xFF,
    0xFF, 0xFF, 0xFF, 0xFF,
    0xFF, 0xFF
};
_Static_assert(sizeof(SIG_FREE_ARRAYS) == sizeof(MSK_FREE_ARRAYS),
               "the free arrays pattern and its mask are different lengths");

/* bapobj_setScale, recognised by the three stores at +0x30, +0x34 and +0x38 that give the object
 * its three scale factors. The multiplayer calls the same function for a far body's size and
 * declares its head, so this one declares it too and is found through the two stage rule: a
 * declared prologue is bytes another module may one day write over, and a pattern that needs them
 * would then find nothing. push ebp; mov ebp,esp; sub esp,0x10 ends at six, with no relative
 * operand before it. */
static const uint8_t SIG_SET_SCALE[] = {
    0x55, 0x8B, 0xEC, 0x83, 0xEC, 0x10,
    0x8B, 0x45, 0x08,
    0x89, 0x45, 0xFC,
    0x8B, 0x4D, 0xFC,
    0x8B, 0x55, 0x0C,
    0x89, 0x51, 0x30,                            /* mov [ecx+0x30],edx   scale.x                 */
    0x8B, 0x45, 0xFC,
    0x8B, 0x4D, 0x10,
    0x89, 0x48, 0x34,                            /* mov [eax+0x34],ecx   scale.y                 */
    0x8B, 0x55, 0xFC,
    0x8B, 0x45, 0x14,
    0x89, 0x42, 0x38                             /* mov [edx+0x38],eax   scale.z                 */
};

/* Plr_RebindWeaponModel. It opens on the player record, takes the mount node out of it and hides
 * that node's children before it puts the equipped weapon back. */
static const uint8_t SIG_REBIND_WEAPON[] = {
    0x55, 0x8B, 0xEC,
    0xA1, 0x00, 0x00, 0x00, 0x00,                /* mov eax,[the player record]                  */
    0x8B, 0x48, 0x40,                            /* mov ecx,[eax+0x40]   the weapon mount node   */
    0x51,
    0x8B, 0x15, 0x00, 0x00, 0x00, 0x00,
    0x8B, 0x42, 0x0C,                            /* mov eax,[edx+0x0C]   the player's body       */
    0x50,
    0xE8, 0x00, 0x00, 0x00, 0x00,                /* call the node hide                           */
    0x83, 0xC4, 0x08,
    0x8B, 0x0D, 0x00, 0x00, 0x00, 0x00,
    0x83, 0xB9, 0x84, 0x00, 0x00, 0x00, 0x00     /* cmp [ecx+0x84],0     the equipped slot        */
};
static const uint8_t MSK_REBIND_WEAPON[] = {
    0xFF, 0xFF, 0xFF,
    0xFF, 0x00, 0x00, 0x00, 0x00,
    0xFF, 0xFF, 0xFF,
    0xFF,
    0xFF, 0xFF, 0x00, 0x00, 0x00, 0x00,
    0xFF, 0xFF, 0xFF,
    0xFF,
    0xFF, 0x00, 0x00, 0x00, 0x00,
    0xFF, 0xFF, 0xFF,
    0xFF, 0xFF, 0x00, 0x00, 0x00, 0x00,
    0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF
};
_Static_assert(sizeof(SIG_REBIND_WEAPON) == sizeof(MSK_REBIND_WEAPON),
               "the rebind weapon pattern and its mask are different lengths");

/* The one site that indexes the hero asset table, and the same anchor actor_loader.c stands on.
 * Each module resolves it for itself, so neither can leave the other holding a half resolved
 * address. The pushed 'BAFS' is what makes the match certain. */
static const uint8_t SIG_HERO_ASSETS[] = {
    0xA1, 0x00, 0x00, 0x00, 0x00,                /* mov eax,[the player record]                  */
    0x8B, 0x48, 0x6C,                            /* mov ecx,[eax+0x6c]   the hero index          */
    0x8B, 0x14, 0x8D, 0x00, 0x00, 0x00, 0x00,    /* mov edx,[ecx*4+the name table]               */
    0x52,
    0x68, 0x53, 0x46, 0x41, 0x42,                /* push 'BAFS', the actor tag                   */
    0xE8, 0x00, 0x00, 0x00, 0x00                 /* call the resource loader                     */
};
static const uint8_t MSK_HERO_ASSETS[] = {
    0xFF, 0x00, 0x00, 0x00, 0x00,
    0xFF, 0xFF, 0xFF,
    0xFF, 0xFF, 0xFF, 0x00, 0x00, 0x00, 0x00,
    0xFF,
    0xFF, 0xFF, 0xFF, 0xFF, 0xFF,
    0xFF, 0x00, 0x00, 0x00, 0x00
};
_Static_assert(sizeof(SIG_HERO_ASSETS) == sizeof(MSK_HERO_ASSETS),
               "the hero assets pattern and its mask are different lengths");

/* halo_freeForThing (0x0043A154). The first thirteen bytes are a counting loop's head, and that
 * head opens nineteen functions of the image; the pattern therefore reaches past the stride of the
 * card table and the compare against the object argument, where it is one. The table's address is
 * the only operand and is masked, like every absolute address here.
 *
 *   55                push ebp
 *   8B EC             mov  ebp,esp
 *   51                push ecx
 *   C7 45 FC 00000000 mov  [ebp-4],0           the prologue ends here, eleven bytes
 *   EB 09             jmp  the test
 *   ...               i += 1
 *   83 7D FC 20       cmp  [ebp-4],0x20        thirty two cards
 *   6B C9 1C          imul ecx,ecx,0x1C        a card is 0x1C bytes
 *   8B 91 ........    mov  edx,[ecx+the table] the object a card hangs on
 *   3B 55 08          cmp  edx,[ebp+8]         the object asked about
 *   75 34             jne  the next card
 */
static const uint8_t SIG_HALO_FREE[] = {
    0x55, 0x8B, 0xEC, 0x51, 0xC7, 0x45, 0xFC, 0x00, 0x00, 0x00, 0x00,
    0xEB, 0x09,
    0x8B, 0x45, 0xFC, 0x83, 0xC0, 0x01, 0x89, 0x45, 0xFC,
    0x83, 0x7D, 0xFC, 0x20,                      /* cmp [ebp-4],0x20: 32 cards      */
    0x7D, 0x47,
    0x8B, 0x4D, 0xFC,
    0x6B, 0xC9, 0x1C,                            /* imul ecx,ecx,0x1C: the stride   */
    0x8B, 0x91, 0x00, 0x00, 0x00, 0x00,          /* mov edx,[ecx+the card table]    */
    0x3B, 0x55, 0x08,                            /* cmp edx,[ebp+8]: the object     */
    0x75, 0x34
};
static const uint8_t MSK_HALO_FREE[] = {
    0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF,
    0xFF, 0xFF,
    0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF,
    0xFF, 0xFF, 0xFF, 0xFF,
    0xFF, 0xFF,
    0xFF, 0xFF, 0xFF,
    0xFF, 0xFF, 0xFF,
    0xFF, 0xFF, 0x00, 0x00, 0x00, 0x00,
    0xFF, 0xFF, 0xFF,
    0xFF, 0xFF
};
_Static_assert(sizeof(SIG_HALO_FREE) == sizeof(MSK_HALO_FREE),
               "the card release pattern and its mask are different lengths");

/* push ebp; mov ebp,esp; push ecx; mov dword [ebp-4],0: boundaries at 1, 3, 4 and 11, and no
 * relative operand before the eleventh byte. */
#define HALO_FREE_PROLOGUE     11u
#define SET_SCALE_PROLOGUE     6u

/* fxzappo_detachThing (0x0043D4DF). A lightning arc hangs its four ends on objects, and each end
 * keeps the node index of the rig its object wore when the end was placed. The arc's draw asks
 * footstep_nodeToWorld for the end, which bounds the index by the node count of the ACTOR's model
 * (0x0043829E) and takes the joint matrix out of the worn handle (0x004382DB), so after a rebind
 * a hero's index reads behind the borrowed rig's matrices. The release is what the engine does
 * when the object is destroyed (fx_thingDestroyed, 0x00438F0A): every end on the object is let go,
 * the arc is drawn from its last points until its life runs out, and it no longer sends the object
 * its message once a second. It writes the arc table and nothing else.
 *
 * Its twin fxzappo_isThingZapped (0x0043D572) opens with the same counting loop, so the pattern
 * reaches the store that makes this one the release. The arc table is the only absolute operand
 * and is masked.
 *
 *   55                   push ebp
 *   8B EC                mov  ebp,esp
 *   83 EC 0C             sub  esp,0x0C             the prologue ends here, six bytes
 *   C7 45 F8 00000000    mov  [ebp-8],0
 *   ...                  i += 1
 *   83 7D F8 40          cmp  [ebp-8],0x40         sixty four arcs
 *   6B C9 78             imul ecx,ecx,0x78         an arc is 0x78 bytes
 *   81 C1 ........       add  ecx,the arc table
 *   ...                  an arc not in use is skipped
 *   83 7D F4 04          cmp  [ebp-0x0C],4         four ends
 *   8B 44 8A 14          mov  eax,[edx+ecx*4+0x14] the object an end hangs on
 *   3B 45 08             cmp  eax,[ebp+8]          the object asked about
 *   75 0E                jne  the next end
 *   C7 44 8A 14 00000000 mov  [edx+ecx*4+0x14],0   the end is let go
 */
static const uint8_t SIG_ARC_RELEASE[] = {
    0x55, 0x8B, 0xEC, 0x83, 0xEC, 0x0C,
    0xC7, 0x45, 0xF8, 0x00, 0x00, 0x00, 0x00,
    0xEB, 0x09,
    0x8B, 0x45, 0xF8, 0x83, 0xC0, 0x01, 0x89, 0x45, 0xF8,
    0x83, 0x7D, 0xF8, 0x40,                      /* cmp [ebp-8],0x40: 64 arcs       */
    0x7D, 0x52,
    0x8B, 0x4D, 0xF8,
    0x6B, 0xC9, 0x78,                            /* imul ecx,ecx,0x78: the stride   */
    0x81, 0xC1, 0x00, 0x00, 0x00, 0x00,          /* add ecx,the arc table           */
    0x89, 0x4D, 0xFC,
    0x8B, 0x55, 0xFC,
    0x83, 0x3A, 0x00,                            /* cmp [edx],0: the arc in use     */
    0x75, 0x02,
    0xEB, 0xD8,
    0xC7, 0x45, 0xF4, 0x00, 0x00, 0x00, 0x00,
    0xEB, 0x09,
    0x8B, 0x45, 0xF4, 0x83, 0xC0, 0x01, 0x89, 0x45, 0xF4,
    0x83, 0x7D, 0xF4, 0x04,                      /* cmp [ebp-0x0C],4: four ends     */
    0x7D, 0x1F,
    0x8B, 0x4D, 0xF4,
    0x8B, 0x55, 0xFC,
    0x8B, 0x44, 0x8A, 0x14,                      /* mov eax,[edx+ecx*4+0x14]        */
    0x3B, 0x45, 0x08,                            /* cmp eax,[ebp+8]: the object     */
    0x75, 0x0E,
    0x8B, 0x4D, 0xF4,
    0x8B, 0x55, 0xFC,
    0xC7, 0x44, 0x8A, 0x14, 0x00, 0x00, 0x00, 0x00   /* the end is let go               */
};
static const uint8_t MSK_ARC_RELEASE[] = {
    0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF,
    0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF,
    0xFF, 0xFF,
    0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF,
    0xFF, 0xFF, 0xFF, 0xFF,
    0xFF, 0xFF,
    0xFF, 0xFF, 0xFF,
    0xFF, 0xFF, 0xFF,
    0xFF, 0xFF, 0x00, 0x00, 0x00, 0x00,
    0xFF, 0xFF, 0xFF,
    0xFF, 0xFF, 0xFF,
    0xFF, 0xFF, 0xFF,
    0xFF, 0xFF,
    0xFF, 0xFF,
    0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF,
    0xFF, 0xFF,
    0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF,
    0xFF, 0xFF, 0xFF, 0xFF,
    0xFF, 0xFF,
    0xFF, 0xFF, 0xFF,
    0xFF, 0xFF, 0xFF,
    0xFF, 0xFF, 0xFF, 0xFF,
    0xFF, 0xFF, 0xFF,
    0xFF, 0xFF,
    0xFF, 0xFF, 0xFF,
    0xFF, 0xFF, 0xFF,
    0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF
};
_Static_assert(sizeof(SIG_ARC_RELEASE) == sizeof(MSK_ARC_RELEASE),
               "the arc release pattern and its mask are different lengths");
_Static_assert(sizeof(SIG_ARC_RELEASE) == 0x6Cu,
               "the arc release pattern ends on the store that lets an end go");

/* push ebp; mov ebp,esp; sub esp,0x0C: boundaries at 1, 3 and 6, no relative operand. */
#define ARC_RELEASE_PROLOGUE   6u

#define OFFSET_PLAYER_RECORD   1u    /* the cell holding the player block pointer */
#define OFFSET_NAME_TABLE      11u   /* the table of four asset names             */
#define OFFSET_RES_ALLOC_CALL  21u   /* the call whose target loads an actor      */

/* ============================================================================================ */

bool character_model_sites_resolve(character_model_sites_t *out)
{
    uintptr_t site;
    uintptr_t target = 0;
    uint32_t  record = 0;
    uint32_t  table = 0;

    out->player_record = 0;
    out->name_table = 0;
    out->set_model = NULL;
    out->free_arrays = NULL;
    out->set_scale = NULL;
    out->rebind_weapon = NULL;
    out->res_alloc = NULL;

    site = signature_find_unique(SIG_SET_MODEL, NULL, sizeof SIG_SET_MODEL);
    if (site == 0) {
        log_warning("the model bind did not resolve, so no model swap is offered");
        return false;
    }
    out->set_model = (rd_set_model_fn_t)site;

    site = signature_find_unique(SIG_FREE_ARRAYS, MSK_FREE_ARRAYS, sizeof SIG_FREE_ARRAYS);
    if (site == 0) {
        log_warning("the per node array release did not resolve, so no model swap is offered: "
                    "binding a second model without it leaks four arrays per swap and leaves the "
                    "old node count behind");
        return false;
    }
    out->free_arrays = (rd_free_arrays_fn_t)site;

    site = signature_find_detour_target(SIG_SET_SCALE, NULL, sizeof SIG_SET_SCALE,
                                        SET_SCALE_PROLOGUE);
    if (site == 0) {
        log_warning("the object scale did not resolve, so no model swap is offered");
        return false;
    }
    out->set_scale = (bap_set_scale_fn_t)site;

    site = signature_find_unique(SIG_REBIND_WEAPON, MSK_REBIND_WEAPON, sizeof SIG_REBIND_WEAPON);
    if (site == 0) {
        log_warning("the weapon remount did not resolve, so no model swap is offered: a new "
                    "skeleton leaves the weapon on a node index that no longer means the hand");
        return false;
    }
    out->rebind_weapon = (plr_rebind_weapon_fn_t)site;

    site = signature_find_unique(SIG_HERO_ASSETS, MSK_HERO_ASSETS, sizeof SIG_HERO_ASSETS);
    if (site == 0) {
        log_warning("the hero asset table did not resolve, so no model swap is offered");
        return false;
    }
    if (!memory_read_u32(site + OFFSET_PLAYER_RECORD, &record) ||
        !memory_read_u32(site + OFFSET_NAME_TABLE, &table) ||
        !patch_read_call_target(site + OFFSET_RES_ALLOC_CALL, &target)) {
        log_warning("the operands at %08X could not be read, so no model swap is offered",
                    (unsigned)site);
        return false;
    }
    out->player_record = (uintptr_t)record;
    out->name_table    = (uintptr_t)table;
    out->res_alloc     = (res_alloc_fn_t)target;
    return true;
}

arc_release_fn_t character_model_sites_arc_release(void)
{
    static bool             tried;
    static arc_release_fn_t found;
    uintptr_t               site;

    if (tried) {
        return found;
    }
    tried = true;
    site = signature_find_detour_target(SIG_ARC_RELEASE, MSK_ARC_RELEASE, sizeof SIG_ARC_RELEASE,
                                        ARC_RELEASE_PROLOGUE);
    if (site == 0u) {
        log_warning("the lightning arc release did not resolve, so a body keeps its arcs through a "
                    "rebind, and an arc's end reads a node of the old rig against the joint "
                    "matrices of the new one; the model is put on all the same");
        return NULL;
    }
    found = (arc_release_fn_t)site;
    log_info("the lightning arc release is at %08X, so a body lets go of its arcs before a rebind",
             (unsigned)site);
    return found;
}

void character_model_sites_let_go_of_arcs(uintptr_t obj)
{
    arc_release_fn_t release = character_model_sites_arc_release();

    if (release != NULL && obj != 0u) {
        release((void *)obj);
    }
}

halo_free_fn_t character_model_sites_halo_free(void)
{
    static bool           tried;
    static halo_free_fn_t found;
    uintptr_t             site;

    if (tried) {
        return found;
    }
    tried = true;
    site = signature_find_detour_target(SIG_HALO_FREE, MSK_HALO_FREE, sizeof SIG_HALO_FREE,
                                        HALO_FREE_PROLOGUE);
    if (site == 0u) {
        log_warning("the glow card release did not resolve, so no far body is dressed: a rebind "
                    "would leave the hero's cards on node indices of another rig");
        return NULL;
    }
    found = (halo_free_fn_t)site;
    log_info("the glow card release is at %08X, so a far body can be dressed",
             (unsigned)site);
    return found;
}
