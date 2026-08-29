/* cheats_original_actions.c: the sixteen one-shot codes from the console's dispatch function that
 * cheats_original.c's shared toggle table does not cover.
 *
 * ==============================================================================================
 * ONE ANCHOR, NOT SIXTEEN SIGNATURES
 *
 * Every one of these lives inside a single retail function, gameplay_open_cheat_console, read in
 * full by decompiling 0x0042fc90. After the eleven-entry toggle loop cheats_original.c already
 * matches, that function just chains sixteen `strcmpi` tests against the typed text, each followed
 * by whatever that code actually does - a function call, a direct write, or both.
 *
 * Rather than write and verify sixteen independent byte patterns, this resolves ONE signature for
 * the function's own prologue and reads everything else as a FIXED BYTE OFFSET from it. That is
 * sound for the same reason cheats_original.c's OFFSET_NAME_TABLE and OFFSET_FLAG_ARRAY are: the
 * whole function is one compiled unit, so a recompile that relocates it moves every instruction
 * inside it by the same amount, and every address an instruction embeds is read out of the match
 * rather than assumed - nothing here is a hardcoded absolute address.
 *
 * The prologue used as the anchor, 0x0042fc90 in retail:
 *
 *   0042fc90  55                       push ebp
 *   0042fc91  8B EC                    mov ebp,esp
 *   0042fc93  83 EC 38                 sub esp,0x38
 *   0042fc96  C7 45 D8 00000000        mov [ebp-0x28],0
 *   0042fc9d  D9 05 [addr]             fld [the frame-time scale]
 *   0042fca3  D8 0D [addr]             fmul [a second scale]
 *   0042fca9  D9 5D D4                 fstp [ebp-0x2c]
 *   0042fcac  83 3D [addr] 00          cmp [a re-entrancy flag],0
 *   0042fcb3  7E 08                    jle +8
 *
 * Every offset below was read directly off the full decompilation and disassembly of this
 * function, not estimated, and is the distance from 0x0042fc90 to the instruction in question.
 *
 * ==============================================================================================
 * THE COUNTER, WHAT IT REALLY DOES, AND WHY THE GATE MATCHES RETAIL'S OWN CAP ANYWAY
 *
 * Full health and all-weapons-full-ammo both raise DAT_00872efc, capped under the console's own
 * `< 10` guard so the two effects themselves stop giving anything past a point. The first version
 * of this file read that guard as a safe BUDGET and gated on "exactly zero" instead, on the
 * strength of what the only other reader of this cell does - FUN_00457f24, which computes the
 * player's EFFECTIVE difficulty for the run:
 *
 *   local_14 = DAT_00872fa0 - local_10;
 *   if (local_14 < 0) local_14 = 0;
 *   if (0 < DAT_00872efc) local_14 = 10;      <- ANY nonzero value, not >= 10
 *   return local_14;
 *
 * That reading is correct about the bytes and was the wrong gate anyway. The FIRST use of either
 * code already sets this counter to 2 or 5, already satisfies "0 < DAT_00872efc", and already pins
 * the hardest row for the rest of the run - and there is no code path in this file, or anywhere
 * else, that can stop that from happening while the cheat still does anything at all. A gate that
 * only allows the very first press does not prevent the pin; it just takes away the handful of
 * further uses retail itself always allowed after paying that same, already-unavoidable cost. A
 * player who wants the resource benefit more than once, the normal way to actually use either of
 * these codes, got LESS from this panel than from typing the same code into the console by hand.
 *
 * So the gate matches retail's own `< 10` exactly: cheats_original_actions_is_available() reads
 * this cell fresh every time and answers false once it stops being under ten, the same point
 * retail's own effect stops giving anything. What the counter's mere existence still guarantees,
 * regardless of any gate here, is that pressing either one even once pins the difficulty - which is
 * a cost of the effect itself, not a defect in how many times this panel lets you pay it.
 *
 * ==============================================================================================
 * TWO LABELS THIS FILE HAD WRONG THE FIRST TIME, CORRECTED FROM WHAT THE CALLEES ACTUALLY DO
 *
 * Two of the three codes too short to be catalogued as strings were named from a fan-made cheat
 * sheet before either callee had actually been read, on the assumption that whichever mystery
 * one-shot codes were left over must be whichever screenshot rows were left over. That assumption
 * was wrong: "but i feel so good" and "happy", which the guess leaned on, turned out to already be
 * TOGGLE TABLE entries cheats_original.c covers on its own, not these two at all. Decompiling the
 * real callees:
 *
 *   FUN_0042945f -> FUN_004293e8: cycles DAT_004ac538 through 1..4, copies a 12-dword row out of a
 *   table at it, writes "perf_level" to the ini and broadcasts a task event. This is the game's own
 *   graphics detail level cycler, not a force power colour.
 *
 *   FUN_00462750: `DAT_006cfdc0 = (DAT_006cfdc0 == 0)`, a plain toggle. Its only other reader,
 *   FUN_004627b6, draws a shape in colour 0xF800 - RGB565 pure red - at an icon's own position when
 *   it is set. What icon is a further question this project has not answered yet; that it draws in
 *   red at all is why the label below says "highlight" rather than claiming more.
 * ============================================================================================== */
#include "cheats_original_actions.h"

#include "common/logging.h"
#include "common/memory.h"
#include "common/patch.h"
#include "common/signature.h"

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>

/* --- the anchor: gameplay_open_cheat_console's own prologue, 37 bytes, three embedded addresses
 * and the one conditional branch masked out. */
static const uint8_t SIG_CONSOLE_FN[] = {
    0x55,                                                  /* push ebp                 */
    0x8B, 0xEC,                                            /* mov ebp,esp              */
    0x83, 0xEC, 0x38,                                      /* sub esp,0x38             */
    0xC7, 0x45, 0xD8, 0x00, 0x00, 0x00, 0x00,               /* mov [ebp-0x28],0         */
    0xD9, 0x05, 0x00, 0x00, 0x00, 0x00,                     /* fld [frame scale]        */
    0xD8, 0x0D, 0x00, 0x00, 0x00, 0x00,                     /* fmul [a second scale]    */
    0xD9, 0x5D, 0xD4,                                       /* fstp [ebp-0x2c]          */
    0x83, 0x3D, 0x00, 0x00, 0x00, 0x00, 0x00,               /* cmp [re-entrancy flag],0 */
    0x7E, 0x00                                              /* jle +N                   */
};
static const uint8_t MSK_CONSOLE_FN[] = {
    0xFF,
    0xFF, 0xFF,
    0xFF, 0xFF, 0xFF,
    0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF,
    0xFF, 0xFF, 0x00, 0x00, 0x00, 0x00,
    0xFF, 0xFF, 0x00, 0x00, 0x00, 0x00,
    0xFF, 0xFF, 0xFF,
    0xFF, 0xFF, 0x00, 0x00, 0x00, 0x00, 0xFF,
    0xFF, 0x00
};
_Static_assert(sizeof(SIG_CONSOLE_FN) == sizeof(MSK_CONSOLE_FN),
               "the console function pattern and its mask are different lengths");

/* Every offset is `instruction address - 0x0042fc90` in retail, read straight off the disassembly.
 * An "OP_" constant is where an instruction's own address-bearing operand starts, which is what
 * gets read as data; the plain offsets are where a CALL itself sits, for patch_read_call_target. */
#define OFF_KILL_CALL           0x284u   /* FUN_00459e85(0), also reused for full health's (100) */
#define OFF_SWAP_CALL           0x3D1u   /* FUN_004302aa(0..3), the four character swaps         */
#define OFF_LOWER_A_CALL        0x319u   /* FUN_00457eee(), "i stink"                             */
#define OP_DIFFICULTY_VAR       0x344u   /* DAT_00872fa0, "i really stink" / "i rule the world"   */
#define OFF_GRAPHICS_DETAIL_CALL 0x39Eu  /* FUN_0042945f(), cycles the graphics detail level      */
#define OFF_RED_HIGHLIGHT_CALL   0x44Bu  /* FUN_00462750(), toggles a red icon highlight          */
#define OP_CREDITS_VAR          0x4AFu   /* DAT_00881368, "gurshick" / "where is gurshick"        */
#define OFF_BONUS_MSG_CALL      0x47Bu   /* FUN_0043dc61(0x48, 0x40800000), "brenando"            */
#define OP_WAVERING_VAR         0x223u   /* DAT_006c4d00, "drop a beat"                           */
#define OFF_WAVERING_OFF_CALL   0x23Bu   /* FUN_00419400(4), when the toggle lands off            */
#define OFF_WAVERING_ON_CALL    0x256u   /* FUN_00419420(4), when the toggle lands on             */
#define OP_DEBUG_VAR            0x2F7u   /* DAT_00881350                                          */
#define OP_COUNTER_VAR          0x4E3u   /* DAT_00872efc, the difficulty-pinning counter          */
#define OP_AMMO_MODE_VAR        0x567u   /* DAT_0086d580, which branch "i like to cheat" takes    */
#define OFF_GIVE_AMMO_CALL      0x575u   /* FUN_00459f8f(slot, amount), six calls, one target     */
#define OP_GRAPHICS_LEVEL_VAR   0x3AAu   /* DAT_004ac538, read AFTER cycling for the message id   */

/* The message ids retail's own console passes to FUN_0043dc61 after each effect, read straight off
 * the same disassembly as everything else here. Safe to carry as literals rather than resolve: each
 * is an immediate value baked into the CALL SITE'S OWN bytes, not a movable data address - the same
 * reasoning the tech bonus case already documents. Graphics detail is the one exception, whose
 * message id is `DAT_004ac538 + 0x37` and has to be read fresh after the level actually changes. */
#define MSG_KILL_SELF        0x46
#define MSG_FULL_HEALTH      0x36
#define MSG_ALL_WEAPONS_AMMO 0x25
#define MSG_LOWER_DIFFICULTY 0x34   /* both "i stink" and "i really stink" print this same one */
#define MSG_INCREASE_DIFFICULTY 0x35
#define MSG_RED_HIGHLIGHT    0x47
#define MSG_DURATION_BITS    0x40800000   /* 4.0f's bit pattern, the seconds every message shows for */

/* String operands, for the three codes too short for the image's own string analysis to have
 * named on its own (see cheats_original.c's MAX_NAME_LENGTH note: nothing stops a SHORT string
 * being missed the other way, by never being catalogued as one at all). Read live rather than
 * guessed, the same reason nothing here is a hardcoded address. */
#define OP_DEBUG_CODE_TEXT           0x2E3u
#define OP_GRAPHICS_DETAIL_CODE_TEXT 0x38Eu
#define OP_RED_HIGHLIGHT_CODE_TEXT   0x43Bu

typedef void (__cdecl *call0_fn_t)(void);
typedef void (__cdecl *call1_fn_t)(int32_t);
typedef void (__cdecl *call2_fn_t)(int32_t, int32_t);

typedef struct action_slot {
    bool     available;
    char     label[64];
} action_slot_t;

typedef struct actions_state {
    bool     resolved;
    uintptr_t anchor;

    call1_fn_t set_health;          /* kill self (0) and full health (100) */
    call1_fn_t swap_character;      /* the four play-as codes              */
    call0_fn_t lower_difficulty_a;
    call0_fn_t cycle_graphics_detail;
    call0_fn_t toggle_red_highlight;
    call2_fn_t print_message;
    call2_fn_t give_ammo;
    call1_fn_t wavering_off;        /* both take the fixed retail argument 4, see the invoke */
    call1_fn_t wavering_on;

    volatile int32_t *difficulty_var;
    volatile int32_t *credits_var;
    volatile int32_t *wavering_var;
    volatile int32_t *debug_var;
    volatile int32_t *counter_var;   /* the difficulty-pinning counter, DAT_00872efc */
    const volatile int32_t *ammo_mode_var;
    const volatile int32_t *graphics_level_var;   /* DAT_004ac538, read after cycling for a message */

    /* -1 means none queued. See the comment above cheats_original_actions_invoke()'s character
     * swap cases for why a swap is queued rather than run immediately. */
    int32_t pending_character;

    action_slot_t slots[CHEATS_ACTION_COUNT];
} actions_state_t;

static actions_state_t st;

/* ============================================================================================ */
static bool read_call_target(uint32_t offset, void **out)
{
    uintptr_t target = 0;

    if (!patch_read_call_target(st.anchor + offset, &target)) {
        return false;
    }
    *out = (void *)target;
    return true;
}

static bool read_data_pointer(uint32_t operand_offset, volatile int32_t **out)
{
    uint32_t addr = 0;

    if (!memory_read_u32(st.anchor + operand_offset, &addr) ||
        !memory_is_inside_image(addr, sizeof(int32_t))) {
        return false;
    }
    *out = (volatile int32_t *)(uintptr_t)addr;
    return true;
}

/* A code string in this function is always short and always inside the image; the same shape
 * cheats_original.c's own name_is_plausible checks, kept small here because only three callers
 * ever need it. */
static bool read_code_text(uint32_t operand_offset, char *out, size_t out_size)
{
    uint32_t    addr = 0;
    const char *text;
    size_t      i;

    if (!memory_read_u32(st.anchor + operand_offset, &addr) || !memory_is_inside_image(addr, 1)) {
        return false;
    }
    text = (const char *)(uintptr_t)addr;
    for (i = 0; i + 1u < out_size; ++i) {
        if (!memory_is_inside_image(addr + (uint32_t)i, 1)) {
            return false;
        }
        if (text[i] == '\0') {
            break;
        }
        if (text[i] < 0x20 || text[i] > 0x7E) {
            return false;
        }
        out[i] = text[i];
    }
    out[i] = '\0';
    return i > 0;
}

static void set_label(cheats_action_id_t id, const char *text)
{
    size_t i;

    for (i = 0; i + 1u < sizeof st.slots[id].label && text[i] != '\0'; ++i) {
        st.slots[id].label[i] = text[i];
    }
    st.slots[id].label[i] = '\0';
}

/* Retail's own on-screen confirmation, the one this whole file originally left out everywhere
 * except the tech bonus message - which was the wrong call for anything else with no OTHER visible
 * feedback. A player pressing a difficulty row that changes a number nothing on screen reflects has
 * no way to tell it worked without this. Silent when the site never resolved: the effect still ran,
 * only the confirmation is missing, the same trade-off resolve_graphics_detail() already documents
 * for its own message id. */
static void print_message_if_available(int32_t message_id)
{
    if (st.print_message != NULL) {
        st.print_message(message_id, MSG_DURATION_BITS);
    }
}

/* ============================================================================================ */
static void resolve_kill_and_health(void)
{
    void *target = NULL;

    if (!read_call_target(OFF_KILL_CALL, &target)) {
        log_warning("the health-set function did not resolve, so kill self and full health stay "
                    "unavailable");
        return;
    }
    st.set_health = (call1_fn_t)target;
    st.slots[CHEATS_ACTION_KILL_SELF].available = true;
    set_label(CHEATS_ACTION_KILL_SELF, "Kill self (kill me now)");

    if (read_data_pointer(OP_COUNTER_VAR, &st.counter_var)) {
        st.slots[CHEATS_ACTION_FULL_HEALTH].available = true;
        set_label(CHEATS_ACTION_FULL_HEALTH, "Full health (heal it up)");
    } else {
        log_warning("the difficulty-pinning counter did not resolve, so full health and "
                    "all-weapons-full-ammo stay unavailable rather than run unguarded");
    }
}

static void resolve_character_swap(void)
{
    void *target = NULL;

    if (!read_call_target(OFF_SWAP_CALL, &target)) {
        log_warning("the character-swap function did not resolve, so the four play-as codes stay "
                    "unavailable");
        return;
    }
    st.swap_character = (call1_fn_t)target;
    st.slots[CHEATS_ACTION_PLAY_OBI].available = true;
    st.slots[CHEATS_ACTION_PLAY_QUIGON].available = true;
    st.slots[CHEATS_ACTION_PLAY_PANAKA].available = true;
    st.slots[CHEATS_ACTION_PLAY_QUEEN].available = true;
    set_label(CHEATS_ACTION_PLAY_OBI, "Play as Obi-Wan again (iamobi)");
    set_label(CHEATS_ACTION_PLAY_QUIGON, "Play as Qui-Gon Jinn (iamquigon)");
    set_label(CHEATS_ACTION_PLAY_PANAKA, "Play as Panaka (iampanaka)");
    set_label(CHEATS_ACTION_PLAY_QUEEN, "Play as Queen Amidala (iamqueen)");
}

static void resolve_difficulty(void)
{
    void *target = NULL;

    if (read_call_target(OFF_LOWER_A_CALL, &target)) {
        st.lower_difficulty_a = (call0_fn_t)target;
        st.slots[CHEATS_ACTION_LOWER_DIFFICULTY_A].available = true;
        set_label(CHEATS_ACTION_LOWER_DIFFICULTY_A, "Lower difficulty (i stink)");
    } else {
        log_warning("the lower-difficulty function did not resolve, that row stays unavailable");
    }

    if (read_data_pointer(OP_DIFFICULTY_VAR, &st.difficulty_var)) {
        st.slots[CHEATS_ACTION_LOWER_DIFFICULTY_B].available = true;
        st.slots[CHEATS_ACTION_INCREASE_DIFFICULTY].available = true;
        set_label(CHEATS_ACTION_LOWER_DIFFICULTY_B, "Lower difficulty (i really stink)");
        set_label(CHEATS_ACTION_INCREASE_DIFFICULTY, "Increased difficulty (i rule the world)");
    } else {
        log_warning("the difficulty variable did not resolve, two rows stay unavailable");
    }
}

/* HELD BACK AS n/a, THE SAME WAY AND FOR THE SAME REASON AS WAVERING GRAPHICS.
 *
 * The site resolves cleanly and the write runs without crashing on its own terms - but field
 * testing found triggering it from this panel, mid level, behaves badly enough to be worth not
 * offering rather than diagnosing on the spot. Retail's own code path for `gurshick` is the
 * console's, which is not this panel's: the console pumps its own frame loop and the player is
 * never suspended while it runs, so whatever the credits sequence expects to be true when it starts
 * may simply not be, here. The resolution stays in place rather than deleted, the same trade
 * resolve_wavering_graphics() makes, in case the actual precondition is ever pinned down. */
static void resolve_credits(void)
{
    if (read_data_pointer(OP_CREDITS_VAR, &st.credits_var)) {
        set_label(CHEATS_ACTION_VIEW_CREDITS, "View credits (gurshick)");
        log_info("view credits resolved but is held back as n/a: field-confirmed misbehaviour when "
                 "triggered from this panel. See the comment above resolve_credits().");
    } else {
        log_warning("the view-credits flag did not resolve, that row stays unavailable");
    }
}

/* HELD BACK AS n/a ON PURPOSE, NOT BECAUSE IT FAILED TO RESOLVE.
 *
 * The site resolves cleanly - the flag and both apply calls all read as valid, in-image addresses -
 * and it runs without crashing. What it does not do, confirmed against the running game rather than
 * assumed, is anything visible. The flag it flips (`DAT_006c4d00`) is read inside two of the
 * engine's own dense per-vertex model transform routines, deep enough that saying what it actually
 * renders as would need real work, and "resolves and runs" is not the same claim as "does something
 * a player asked for" - so it stays off rather than offering a row that ticks and, as far as this
 * project can currently show, does nothing. The resolution below is left in place rather than
 * deleted: if the visible effect is ever pinned down, restoring the row is one line. */
static void resolve_wavering_graphics(void)
{
    void *off_target = NULL;
    void *on_target = NULL;

    if (!read_data_pointer(OP_WAVERING_VAR, &st.wavering_var) ||
        !read_call_target(OFF_WAVERING_OFF_CALL, &off_target) ||
        !read_call_target(OFF_WAVERING_ON_CALL, &on_target)) {
        log_warning("wavering graphics did not fully resolve, that row stays unavailable");
        return;
    }
    st.wavering_off = (call1_fn_t)off_target;
    st.wavering_on = (call1_fn_t)on_target;
    set_label(CHEATS_ACTION_WAVERING_GRAPHICS, "Wavering graphics (drop a beat)");
    /* Deliberately not `st.slots[...].available = true`: see the comment above this function. */
    log_info("wavering graphics resolved (flag and both apply calls all valid) but is held back as "
             "n/a rather than offered: no confirmed visible effect. See the comment above "
             "resolve_wavering_graphics().");
}

/* HELD BACK AS N/A, the third row in this file that resolves and is still not offered.
 *
 * The game's own debug mode draws its frame rate readout through the same text layer this panel
 * draws through, and running it from here breaks the panel: field confirmed, by turning it on and
 * watching what happened to the overlay afterwards. The flag and its code text both resolve, so
 * this is not a resolve failure and saying so in the log would be misleading; it is a row that
 * works and must not be offered from this panel.
 *
 * Kept visible and greyed rather than deleted, for the same reason as view credits and wavering
 * graphics: a row that quietly disappears invites being added back by somebody who does not know
 * why it went. The label still shows the code, so the cheat is still discoverable to anyone who
 * wants to type it into the game's own console, which is where it works. */
static void resolve_debug_mode(void)
{
    char text[24] = { 0 };
    char label[64];

    if (!read_data_pointer(OP_DEBUG_VAR, &st.debug_var)) {
        log_warning("the debug-mode flag did not resolve, that row stays unavailable");
        return;
    }
    /* Deliberately not `st.slots[...].available = true`: see the comment above this function. */
    if (read_code_text(OP_DEBUG_CODE_TEXT, text, sizeof text)) {
        _snprintf(label, sizeof label, "Debug mode (%s)", text);
        label[sizeof label - 1] = '\0';
        set_label(CHEATS_ACTION_DEBUG_MODE, label);
    } else {
        set_label(CHEATS_ACTION_DEBUG_MODE, "Debug mode");
    }
    log_info("debug mode resolved but is held back as n/a: running it from this panel breaks the "
             "panel, field confirmed. See the comment above resolve_debug_mode().");
}

static void resolve_graphics_detail(void)
{
    void *target = NULL;
    char  text[24] = { 0 };
    char  label[64];

    if (!read_call_target(OFF_GRAPHICS_DETAIL_CALL, &target)) {
        log_warning("the graphics-detail function did not resolve, that row stays unavailable");
        return;
    }
    st.cycle_graphics_detail = (call0_fn_t)target;
    /* Best effort and non-fatal: without it the level still cycles, only the confirmation message
     * cannot say which level it landed on, the same trade the game's own pointer makes in
     * overlay_sites.c. */
    if (!read_data_pointer(OP_GRAPHICS_LEVEL_VAR, (volatile int32_t **)&st.graphics_level_var)) {
        log_warning("the graphics detail level cell did not resolve, so cycling it prints no "
                    "message but still works");
    }
    st.slots[CHEATS_ACTION_GRAPHICS_DETAIL].available = true;
    if (read_code_text(OP_GRAPHICS_DETAIL_CODE_TEXT, text, sizeof text)) {
        _snprintf(label, sizeof label, "Cycle graphics detail level, 1-4 (%s)", text);
        label[sizeof label - 1] = '\0';
        set_label(CHEATS_ACTION_GRAPHICS_DETAIL, label);
    } else {
        set_label(CHEATS_ACTION_GRAPHICS_DETAIL, "Cycle graphics detail level, 1-4");
    }
}

static void resolve_red_highlight(void)
{
    void *target = NULL;
    char  text[24] = { 0 };
    char  label[64];

    if (!read_call_target(OFF_RED_HIGHLIGHT_CALL, &target)) {
        log_warning("the red-highlight function did not resolve, that row stays unavailable");
        return;
    }
    st.toggle_red_highlight = (call0_fn_t)target;
    st.slots[CHEATS_ACTION_RED_HIGHLIGHT].available = true;
    if (read_code_text(OP_RED_HIGHLIGHT_CODE_TEXT, text, sizeof text)) {
        _snprintf(label, sizeof label, "Toggle a red icon highlight (%s)", text);
        label[sizeof label - 1] = '\0';
        set_label(CHEATS_ACTION_RED_HIGHLIGHT, label);
    } else {
        set_label(CHEATS_ACTION_RED_HIGHLIGHT, "Toggle a red icon highlight");
    }
}

static void resolve_tech_bonus(void)
{
    void *target = NULL;

    if (!read_call_target(OFF_BONUS_MSG_CALL, &target)) {
        log_warning("the message printer did not resolve, \"Tech Bonus!\" stays unavailable");
        return;
    }
    st.print_message = (call2_fn_t)target;
    st.slots[CHEATS_ACTION_TECH_BONUS].available = true;
    set_label(CHEATS_ACTION_TECH_BONUS, "\"Tech Bonus!\" message (brenando)");
}

static void resolve_all_weapons_ammo(void)
{
    void *target = NULL;

    if (!read_call_target(OFF_GIVE_AMMO_CALL, &target) ||
        !read_data_pointer(OP_AMMO_MODE_VAR, (volatile int32_t **)&st.ammo_mode_var)) {
        log_warning("all-weapons-full-ammo did not fully resolve, that row stays unavailable");
        return;
    }
    st.give_ammo = (call2_fn_t)target;
    /* Shares its availability with full health: both are gated on the SAME counter, resolved (or
     * not) in resolve_kill_and_health(). This only decides whether the SITE exists; the gate is
     * read fresh in cheats_original_actions_is_available(). */
    if (st.counter_var != NULL) {
        st.slots[CHEATS_ACTION_ALL_WEAPONS_AMMO].available = true;
        set_label(CHEATS_ACTION_ALL_WEAPONS_AMMO, "All weapons, full ammo (i like to cheat)");
    }
}

/* ============================================================================================ */
bool cheats_original_actions_resolve(void)
{
    if (st.resolved) {
        return true;
    }
    st.pending_character = -1;

    st.anchor = signature_find_unique(SIG_CONSOLE_FN, MSK_CONSOLE_FN, sizeof SIG_CONSOLE_FN);
    if (st.anchor == 0) {
        log_warning("the cheat console's own function did not resolve, so none of the one-shot "
                    "codes are offered. The eleven toggles are unaffected: they resolve on their "
                    "own, in cheats_original.c.");
        return false;
    }

    resolve_kill_and_health();
    resolve_character_swap();
    resolve_difficulty();
    resolve_credits();
    resolve_wavering_graphics();
    resolve_debug_mode();
    resolve_graphics_detail();
    resolve_red_highlight();
    resolve_tech_bonus();
    resolve_all_weapons_ammo();

    st.resolved = true;
    log_info("the cheat console's function resolved at %08X; the one-shot codes read their sites "
             "relative to it", (unsigned)st.anchor);
    return true;
}

const char *cheats_original_actions_name(cheats_action_id_t id)
{
    if ((unsigned)id >= (unsigned)CHEATS_ACTION_COUNT) {
        return NULL;
    }
    return st.slots[id].label;
}

bool cheats_original_actions_is_available(cheats_action_id_t id)
{
    int32_t counter;

    if ((unsigned)id >= (unsigned)CHEATS_ACTION_COUNT || !st.slots[id].available) {
        return false;
    }
    if (id == CHEATS_ACTION_FULL_HEALTH || id == CHEATS_ACTION_ALL_WEAPONS_AMMO) {
        /* Read fresh every time: the retail console, a save load, or this panel itself can all
         * move this cell between one paint and the next. `< 10` is the retail console's OWN gate,
         * matched rather than tightened - see the header comment at the top of this file for why a
         * stricter gate here would not buy any additional safety. The difficulty is already pinned
         * from the first use either way; the counter's only remaining job past that point is
         * deciding how many more times the RESOURCE benefit itself still works, same as retail. */
        if (st.counter_var == NULL) {
            return false;
        }
        counter = *st.counter_var;
        return counter < 10;
    }
    return true;
}

/* ============================================================================================ */
bool cheats_original_actions_invoke(cheats_action_id_t id)
{
    if (!cheats_original_actions_is_available(id)) {
        return false;
    }

    switch (id) {
    case CHEATS_ACTION_KILL_SELF:
        st.set_health(0);
        print_message_if_available(MSG_KILL_SELF);
        return true;

    case CHEATS_ACTION_FULL_HEALTH:
        st.set_health(100);
        *st.counter_var = *st.counter_var + 2;
        print_message_if_available(MSG_FULL_HEALTH);
        return true;

    case CHEATS_ACTION_ALL_WEAPONS_AMMO:
        *st.counter_var = *st.counter_var + 5;
        if (*st.ammo_mode_var == 2) {
            st.give_ammo(10, 400);
            st.give_ammo(11, 400);
        } else {
            st.give_ammo(2, 500);
            st.give_ammo(4, 5);
        }
        st.give_ammo(3, 500);
        st.give_ammo(6, 10);
        st.give_ammo(7, 10);
        st.give_ammo(5, 1);
        print_message_if_available(MSG_ALL_WEAPONS_AMMO);
        return true;

    /* QUEUED, NOT RUN HERE. The swap has a precondition read out of FUN_00447d18: a pointer in the
     * player's own state block that reads as "no active controller" while dev_overlay is holding
     * the player suspended, which is the engine's own idle state and is exactly what the panel asks
     * for on every other frame it is open. Calling the swap now can silently do nothing, with no
     * error to show for it. Queuing it instead and applying it once the panel closes and the player
     * is un-suspended again is what makes it reliable rather than "usually" reliable. Only the last
     * press before closing wins - the four are mutually exclusive anyway, so overwriting a pending
     * one rather than queuing several is the honest behaviour, not a limitation of the queue. */
    case CHEATS_ACTION_PLAY_OBI:
        st.pending_character = 0;
        return true;
    case CHEATS_ACTION_PLAY_QUIGON:
        st.pending_character = 1;
        return true;
    case CHEATS_ACTION_PLAY_PANAKA:
        st.pending_character = 2;
        return true;
    case CHEATS_ACTION_PLAY_QUEEN:
        st.pending_character = 3;
        return true;

    case CHEATS_ACTION_LOWER_DIFFICULTY_A:
        st.lower_difficulty_a();
        print_message_if_available(MSG_LOWER_DIFFICULTY);
        return true;
    case CHEATS_ACTION_LOWER_DIFFICULTY_B:
        *st.difficulty_var = 0;
        print_message_if_available(MSG_LOWER_DIFFICULTY);
        return true;
    case CHEATS_ACTION_INCREASE_DIFFICULTY:
        *st.difficulty_var = 9;
        print_message_if_available(MSG_INCREASE_DIFFICULTY);
        return true;

    case CHEATS_ACTION_VIEW_CREDITS:
        *st.credits_var = 1;
        return true;

    case CHEATS_ACTION_WAVERING_GRAPHICS:
        *st.wavering_var = *st.wavering_var ^ 1;
        if (*st.wavering_var == 0) {
            st.wavering_off(4);
        } else {
            st.wavering_on(4);
        }
        return true;

    case CHEATS_ACTION_DEBUG_MODE:
        *st.debug_var = (*st.debug_var == 0) ? 1 : 0;
        return true;

    case CHEATS_ACTION_GRAPHICS_DETAIL:
        st.cycle_graphics_detail();
        /* Retail's own message id here is not a fixed constant, it is `DAT_004ac538 + 0x37`, read
         * AFTER the call above so it names the level just cycled TO rather than the one left. */
        if (st.graphics_level_var != NULL) {
            print_message_if_available(*st.graphics_level_var + 0x37);
        }
        return true;
    case CHEATS_ACTION_RED_HIGHLIGHT:
        st.toggle_red_highlight();
        print_message_if_available(MSG_RED_HIGHLIGHT);
        return true;

    case CHEATS_ACTION_TECH_BONUS:
        /* 0x48 is the message id and 0x40800000 the raw dword the retail call site itself pushes
         * for its display-seconds argument (4.0f's bit pattern) - both are immediate values baked
         * into the CODE at this call site, not a movable data address, so unlike everything else
         * in this file they are safe to carry as literals. */
        st.print_message(0x48, 0x40800000);
        return true;

    case CHEATS_ACTION_COUNT:
    default:
        return false;
    }
}

bool cheats_original_actions_is_pending(cheats_action_id_t id)
{
    int32_t character;

    switch (id) {
    case CHEATS_ACTION_PLAY_OBI:    character = 0; break;
    case CHEATS_ACTION_PLAY_QUIGON: character = 1; break;
    case CHEATS_ACTION_PLAY_PANAKA: character = 2; break;
    case CHEATS_ACTION_PLAY_QUEEN:  character = 3; break;
    default:
        return false;   /* nothing else is ever queued */
    }
    /* Before resolve() has run, `st` is only zero-initialised: pending_character reads as 0, not
     * the -1 sentinel resolve() sets, which would otherwise make this answer true for Obi-Wan (0)
     * on an executable nothing has been resolved against at all. */
    return st.resolved && st.pending_character == character;
}

const char *cheats_original_actions_pending_label(void)
{
    if (!st.resolved) {
        return NULL;      /* see the same zero-vs-sentinel note in cheats_original_actions_is_pending() */
    }
    switch (st.pending_character) {
    case 0: return st.slots[CHEATS_ACTION_PLAY_OBI].label;
    case 1: return st.slots[CHEATS_ACTION_PLAY_QUIGON].label;
    case 2: return st.slots[CHEATS_ACTION_PLAY_PANAKA].label;
    case 3: return st.slots[CHEATS_ACTION_PLAY_QUEEN].label;
    default: return NULL;    /* nothing queued */
    }
}

void cheats_original_actions_apply_pending(void)
{
    if (st.pending_character < 0 || st.swap_character == NULL) {
        st.pending_character = -1;
        return;
    }
    st.swap_character(st.pending_character);
    st.pending_character = -1;
}

int32_t cheats_original_actions_graphics_level(void)
{
    if (st.graphics_level_var == NULL) {
        return 0;
    }
    return *st.graphics_level_var;
}

volatile int32_t *cheats_original_actions_level_status_cell(void)
{
    return st.credits_var;
}
