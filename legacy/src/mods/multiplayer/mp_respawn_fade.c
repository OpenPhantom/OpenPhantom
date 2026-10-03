/* mp_respawn_fade.c: the fade the engine's re-entry waits on, started again when a closing menu
 * has taken it away. See the header.
 *
 * What the engine does, in the retail image. The player's task, in its state 4, pushes one second
 * and calls the tint's opaque start, then stores 3 as its state; in state 3 it compares the tint's
 * expired cell with 1 on every tick and spawns only when they are equal. The tint's draw sets that
 * cell inside the block that begins with a test of the tint's active cell, so a tint that was
 * stopped never sets it. The menu's close calls the tint's stop when its last screen goes.
 *
 * The tint's active cell is named by the engine three times in the two routines this file reads:
 *
 *     fxfade_startTintOpaque, the routine the scene's module already calls, thirty two bytes in:
 *     004393F0  E8 7B FF FF FF                 call fxfade_startTint
 *
 *     fxfade_startTint:
 *     00439373  C7 05 78 4E 6C 00 01 00 00 00  mov [active],1       operand at +0x05
 *     0043937D  C7 05 7C 4E 6C 00 00 00 00 00  mov [expired],0      operand at +0x0F
 *
 *     fxfade_stopTint, the routine that follows the opaque start, forty two bytes after its head:
 *     004393FD  83 3D 78 4E 6C 00 00           cmp [active],0       operand at +0x05
 *     00439408  C7 05 78 4E 6C 00 00 00 00 00  mov [active],0       operand at +0x10
 *
 * The expired cell in the start has to be the one the player's task waits on, which is what makes
 * the routine the tint's start. Of the three namings of the active cell two have to be readable
 * and every readable one has to name the same cell. Another module's branch over the head of
 * either routine pads its prologue and so hides the naming inside it, one in the start and one in
 * the stop; the third is past any prologue, and two are still left.
 */
#include "mp_respawn_fade.h"

#include "mp_cells.h"
#include "mp_respawn.h"
#include "mp_scene_bind.h"

#include "common/logging.h"
#include "common/memory.h"
#include "common/patch.h"

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <string.h>

/* The engine's own fade of a re-entry, one second: the player's task pushes 0x3F800000 for it. */
#define RESPAWN_FADE_SECONDS 1.0f

/* Where the opaque start calls the start, and how long it is: the stop begins behind it. */
#define SHELL_CALL_OFFSET 0x20u
#define SHELL_BYTES       0x2Au

/* The start: `mov [active],1` and `mov [expired],0` behind its three byte head. */
#define START_ACTIVE_OPCODE  0x03u
#define START_ACTIVE_OPERAND 0x05u
#define START_ACTIVE_VALUE   0x09u
#define START_DONE_OPCODE    0x0Du
#define START_DONE_OPERAND   0x0Fu
#define START_DONE_VALUE     0x13u
#define START_BYTES          0x17u

/* The stop: `cmp [active],0`, two short jumps, `mov [active],0`. */
#define STOP_TEST_OPCODE   0x03u
#define STOP_TEST_OPERAND  0x05u
#define STOP_TEST_VALUE    0x09u
#define STOP_CLEAR_OPCODE  0x0Eu
#define STOP_CLEAR_OPERAND 0x10u
#define STOP_CLEAR_VALUE   0x14u
#define STOP_BYTES         0x18u

#define OPCODE_MOV_MEM_IMM32_A 0xC7u
#define OPCODE_MOV_MEM_IMM32_B 0x05u
#define OPCODE_CMP_MEM_IMM8_A  0x83u
#define OPCODE_CMP_MEM_IMM8_B  0x3Du

/* The player module's state with no player in the world: the engine's despawn stores it, and a
 * level that ends leaves it there. The re-entry itself goes from its wait to running inside one
 * tick, so a look between two frames never reads this state on the way. */
#define MODULE_OFF 0u

/* A line for each of the first restarts and landings of a process; the report counts them all. */
#define FADE_LINES_MAX 8u

typedef struct fade_state {
    bool      tried;
    uintptr_t done_cell;      /* reads 1 once the tint has run its time */
    uintptr_t active_cell;    /* reads nought once the tint was stopped; nought here: not bound */
    uint32_t  namings;        /* how many of the three namings read, and agreed */

    bool      restarted;      /* a fade was started again and its re-entry has not landed yet */
    uint32_t  cut_short;      /* restarts whose level ended before the body stood */
    uint32_t  waits;          /* frames the player module was seen waiting on a fade */
    uint32_t  unread;         /* of those, frames a cell of the tint did not read */
    uint32_t  restarts;
    uint32_t  landed;         /* restarts after which the module ran again */
    uint32_t  lines;
} fade_state_t;

static fade_state_t fade;

static uint32_t u32_at(const uint8_t *bytes, size_t offset)
{
    uint32_t value;

    memcpy(&value, bytes + offset, sizeof value);
    return value;
}

static bool is_mov_to_cell(const uint8_t *bytes, size_t opcode)
{
    return bytes[opcode] == OPCODE_MOV_MEM_IMM32_A && bytes[opcode + 1u] == OPCODE_MOV_MEM_IMM32_B;
}

/* Proves the active cell out of the start and the stop. NULL when it is proved and stored, and
 * otherwise the reason it is not. */
static const char *prove_the_active_cell(uintptr_t shell, uintptr_t done_cell)
{
    uint8_t   start_bytes[START_BYTES];
    uint8_t   stop_bytes[STOP_BYTES];
    uint32_t  named[3];
    size_t    count = 0u;
    size_t    i;
    uintptr_t start = 0u;

    if (!patch_read_call_target(shell + SHELL_CALL_OFFSET, &start) ||
        !memory_read(start, start_bytes, sizeof start_bytes)) {
        return "the tint's opaque start does not call a start that reads";
    }
    if (!is_mov_to_cell(start_bytes, START_DONE_OPCODE) ||
        u32_at(start_bytes, START_DONE_OPERAND) != (uint32_t)done_cell ||
        u32_at(start_bytes, START_DONE_VALUE) != 0u) {
        return "the routine the opaque start calls does not clear the cell the re-entry waits on";
    }
    if (is_mov_to_cell(start_bytes, START_ACTIVE_OPCODE) &&
        u32_at(start_bytes, START_ACTIVE_VALUE) == 1u) {
        named[count++] = u32_at(start_bytes, START_ACTIVE_OPERAND);
    }
    if (memory_read(shell + SHELL_BYTES, stop_bytes, sizeof stop_bytes)) {
        if (stop_bytes[STOP_TEST_OPCODE] == OPCODE_CMP_MEM_IMM8_A &&
            stop_bytes[STOP_TEST_OPCODE + 1u] == OPCODE_CMP_MEM_IMM8_B &&
            stop_bytes[STOP_TEST_VALUE] == 0u) {
            named[count++] = u32_at(stop_bytes, STOP_TEST_OPERAND);
        }
        if (is_mov_to_cell(stop_bytes, STOP_CLEAR_OPCODE) &&
            u32_at(stop_bytes, STOP_CLEAR_VALUE) == 0u) {
            named[count++] = u32_at(stop_bytes, STOP_CLEAR_OPERAND);
        }
    }
    if (count < 2u) {
        return "fewer than two of the three places that name the tint's active cell read";
    }
    for (i = 1u; i < count; ++i) {
        if (named[i] != named[0]) {
            return "the places that name the tint's active cell do not name one cell";
        }
    }
    if (named[0] == (uint32_t)done_cell ||
        !memory_is_inside_image((uintptr_t)named[0], sizeof(uint32_t)) ||
        !memory_is_readable_range((uintptr_t)named[0], sizeof(uint32_t))) {
        return "the cell named as the tint's active one is not a cell of the image of its own";
    }
    fade.active_cell = (uintptr_t)named[0];
    fade.namings     = (uint32_t)count;
    return NULL;
}

/* Bound once, at the first look after the scene's module has the fade. Until it has, this is
 * asked again; a fade that never resolves there is said there. */
static bool bound(void)
{
    uintptr_t   shell = 0u;
    uintptr_t   done  = 0u;
    const char *why;

    if (fade.tried) {
        return fade.active_cell != 0u;
    }
    if (!mp_scene_bind_fade_sites(&shell, &done)) {
        return false;
    }
    fade.tried     = true;
    fade.done_cell = done;
    why = prove_the_active_cell(shell, done);
    if (why != NULL) {
        log_warning("the engine's re-entry cannot be given its fade back: %s. A re-entry whose "
                    "fade a closing menu stops waits for an end that is not reported, as it did "
                    "before", why);
        return false;
    }
    log_info("the engine's re-entry keeps its fade: the tint's active cell is %08X, named by %u "
             "of the three places that agree, beside its expired cell %08X; a re-entry whose "
             "fade a closing menu stopped is given it again",
             (unsigned)fade.active_cell, (unsigned)fade.namings, (unsigned)fade.done_cell);
    return true;
}

void mp_respawn_fade_tick(void)
{
    uint32_t state  = 0u;
    uint32_t done   = 0u;
    uint32_t active = 0u;

    if (!bound() || !mp_scene_bind_module_state(&state)) {
        return;
    }
    if (fade.restarted && state == MP_HERO_MODULE_RUNNING) {
        fade.restarted = false;
        ++fade.landed;
        if (fade.lines < FADE_LINES_MAX) {
            ++fade.lines;
            log_info("the re-entry whose fade was started again has landed: the player module "
                     "runs");
        }
    } else if (fade.restarted && state == MODULE_OFF) {
        /* The level ended under the re-entry. The first frame of the next level reads a
         * running module, and that is no landing of this one. */
        fade.restarted = false;
        ++fade.cut_short;
    }
    /* The cheap question first: on almost every frame the module is not waiting on a fade. */
    if (state != MP_RESPAWN_MODULE_FADING) {
        return;
    }
    ++fade.waits;
    if (!memory_try_read_u32(fade.done_cell, &done) ||
        !memory_try_read_u32(fade.active_cell, &active)) {
        ++fade.unread;
        return;
    }
    if (!mp_respawn_fade_is_lost(state, done == 1u, active != 0u)) {
        return;
    }
    mp_scene_bind_fade_out(RESPAWN_FADE_SECONDS);
    fade.restarted = true;
    ++fade.restarts;
    if (fade.lines < FADE_LINES_MAX) {
        ++fade.lines;
        log_info("the fade the engine's re-entry waits on was stopped under it, as a menu's close "
                 "stops it, and is started again: one second to black, the way the player "
                 "module's own state starts it (restart %u)", (unsigned)fade.restarts);
    }
}

void mp_respawn_fade_report(void)
{
    if (!fade.tried) {
        log_info("  the re-entry's fade: never asked about; the scene's module did not bind the "
                 "fade, or no frame of a session ran");
        return;
    }
    log_info("  the re-entry's fade: %s; %u frame(s) the player module waited on a fade, %u of "
             "them with a cell of the tint unread; started again %u time(s) after it was stopped "
             "under the wait, %u of those re-entries landed and %u ended with their level%s",
             fade.active_cell != 0u ? "bound" : "NOT bound, the tint's active cell did not prove",
             (unsigned)fade.waits, (unsigned)fade.unread, (unsigned)fade.restarts,
             (unsigned)fade.landed, (unsigned)fade.cut_short,
             fade.restarted ? "; one is still under way at the report" : "");
}
