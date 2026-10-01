/* mp_bootstrap.c: install the engine side at a moment when the engine exists.
 *
 * The DLL's entry point runs before the host's CRT start. Calling the module registry there is a
 * silent no op, and a task registered there is wiped later by the scheduler's own install arm. So
 * the engine side is installed from inside the engine's startup instead, and this file is the
 * deferral plus the three things it does once it gets there.
 *
 * What it installs is a foothold, not a feature: two module nodes and one task slot, all of which
 * do nothing but count what they receive. Nothing else in the game changes.
 *
 * ============================== The one write that is not a count =============================
 *
 * One of the two module nodes is moved from the tail of the registry to its head, and that is a
 * write into the engine's own list. It is worth the risk for a reason that is structural: every
 * per frame and per substep broadcast walks the list BACKWARDS, from the tail, so the node the
 * registry appends is the FIRST to hear everything. First is right for the frame opening and
 * harmless for the substep, and it is wrong twice: it puts us in front of the input latch, and it
 * makes the game's own interface draw over anything we draw. A node at the head is the LAST to
 * hear a backward broadcast, which is what those two want.
 *
 * The relink has to be complete or it builds a cycle, and a cycle here is not a slow list. Two
 * messages and the registry's own lookup walk FORWARD, and a forward walk into a cycle never
 * returns: the symptom is the game hanging at every level end and on quit. So the node's old
 * predecessor has to be told it is the tail now, the tail pointer has to be moved back to it, and
 * the result is walked from both ends before it is believed.
 */
#include "mp_bootstrap.h"

#include "mp_armed.h"
#include "mp_capacity.h"
#include "mp_cells.h"
#include "mp_diag.h"
#include "mp_module.h"
#include "mp_pool.h"
#include "mp_signatures.h"
#include "mp_task.h"

#include "common/detour.h"
#include "common/frame_hook.h"
#include "common/logging.h"
#include "common/memory.h"
#include "common/patch.h"

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

/* The engine's module node, at the offsets module_install writes them. Only the three fields this
 * file touches are named; the rest of the 0x24 bytes belong to the registry.
 *
 * Read out of the registry's install at 0x0046ED64, which takes the node from malloc(0x24) at
 * 0x0046ED6B and links it in this block:
 *
 *   0046EDE3  8B 0D C8 64 6D 00     mov  ecx, [0x006D64C8]     the tail
 *   0046EDE9  89 48 04              mov  [eax+4], ecx          node->pPrev = tail
 *   0046EDEF  C7 02 00 00 00 00     mov  dword [edx], 0        node->pNext = 0
 *   0046EE06  89 08                 mov  [eax], ecx            oldTail->pNext = node
 *   0046EE0B  89 15 C8 64 6D 00     mov  [0x006D64C8], edx     tail = node
 *   0046EE1D  A3 C4 64 6D 00        mov  [0x006D64C4], eax     head = node, only when empty
 *   0046EE22  6A 00 6A 00 6A 01     push 0 / push 0 / push 1
 *   0046EE2B  8B 4D F8 FF 51 10     call [node+0x10]           fn(msg = 1, arg = 0, dt = 0.0)
 *   0046EE31  83 C4 0C              add  esp, 0xC              cdecl
 *
 * So the next pointer is at +0x00, the previous at +0x04, the handler at +0x10, the head cell at
 * 0x006D64C4 and the tail cell at 0x006D64C8. The identifier at +0x08 is written as the node's
 * own address (0046ED8D: `8B 45 F8 / 8B 4D F8 / 89 48 08`) and the install returns it, which is
 * why the return value is used as a node pointer here. */
#define MODULE_NODE_NEXT   0x00u
#define MODULE_NODE_PREV   0x04u
#define MODULE_NODE_SIZE   0x24u

/* The two messages that end something, and the reason the census is printed on them rather than on
 * a timer: both of them walk the list FORWARD, so the node at the tail is the last to see them.
 * Reporting there means the numbers include the message being reported on.
 *
 * Which way each broadcast walks, from the registry's own bodies: the timed broadcast at
 * 0x0046F4A9 walks tail to head over the previous pointers and carries 0x0C, 0x0D, 0x0E, 0x11
 * and 0x15; the plain broadcast at 0x0046F3C3 walks the same way for everything except these
 * two, which it walks head to tail over the next pointers; the lookup at 0x0046F529 that every
 * targeted send goes through walks head to tail, as does the shutdown send at 0x0046EF38, while
 * the teardown at 0x0046EC0C walks tail to head. So three walkers go forward, and a forward
 * walk is what a broken relink hangs. */
#define MODULE_MSG_SHUTDOWN    0x04
#define MODULE_MSG_LEVEL_END   0x06

/* The first message of a drawn frame, and the moment the hook shapes are read. Not at install time:
 * half the mods that matter load after this one, and an answer about a process that no longer
 * exists is worse than none. */
#define MODULE_MSG_FRAME_BEGIN 0x0C
/* The end of one substep. The engine sends it once per substep from its own substep runner, and
 * that runner is its ONLY sender in the whole image, so a node that tests for this number is
 * standing at the end of a substep with no further condition. Between it and the next substep the
 * engine does two things and nothing else: it counts the substep and it adds the step to its
 * simulation time. */
#define MODULE_MSG_SUBSTEP_END 0x0E

/* task_register hands back a record pointer and reports failure as -1 rather than as null, so the
 * check this tree writes by habit would read a failure as success. The registration at
 * 0x0047563D takes the first free slot of 64, stride 0x2C, array at 0x00868740, and a slot is
 * free when its handler word at +0x14 is zero. */
#define TASK_REGISTER_FAILED ((uintptr_t)0xFFFFFFFFu)

/* The five dwords a registration copies into the front of the record it takes. Exactly five: the
 * simulation target time sits immediately behind them, and a sixth would zero the world clock.
 *
 *   004756A7  6B FF 2C              imul edi, edi, 0x2C        the stride
 *   004756AA  81 C7 40 87 86 00     add  edi, 0x00868740       the array
 *   004756B0  B9 05 00 00 00        mov  ecx, 5
 *   004756B5  BE 80 92 86 00        mov  esi, 0x00869280       the staging area
 *   004756BA  F3 A5                 rep  movsd                 five dwords into +0x00 to +0x13
 *   004756BE  89 0D 80 92 86 00     mov  [0x00869280], ecx     and the first three cleared
 *
 * 0x00869280 + 0x14 is 0x00869294, the simulation target time. The five land in front of the
 * handler at +0x14 and the handler is stored before the copy, so they cannot clobber it. */
#define TASK_STAGING_DWORDS 5u

/* No shipped level has anything like this many modules. The bound exists so that a walk over a
 * list we have just edited terminates whatever we did to it. */
#define MODULE_WALK_LIMIT 256u

typedef uintptr_t(__cdecl *module_install_fn)(void *proc, const char *name);
typedef uintptr_t(__cdecl *task_register_fn)(void *task);
typedef int32_t(__cdecl *sys_startup_fn)(void);
typedef void(__cdecl *campaign_run_fn)(void);

typedef struct mp_bootstrap_module {
    bool                  armed;
    bool                  provoke;
    bool                  has_run;
    mp_bootstrap_path_t   path;
    detour_t              startup_detour;
    detour_t              campaign_detour;
    mp_bootstrap_result_t result;
} mp_bootstrap_module_t;

static mp_bootstrap_module_t boot;

mp_bootstrap_path_t mp_bootstrap_path(void)    { return boot.path; }
bool                mp_bootstrap_has_run(void) { return boot.has_run; }

const mp_bootstrap_result_t *mp_bootstrap_result(void)
{
    return &boot.result;
}

/* ==============================================================================================
 * What the foothold does once it is in: measure, and nothing else.
 *
 * The census itself lives in mp_module.c and mp_task.c. What is here is the decision of WHEN to
 * print it, which is the one piece of policy the two of them should not own.
 * ============================================================================================ */

static void (*report_client)(const char *why);
static void (*shutdown_client)(void);
static void (*frame_begin_client)(void);

void mp_bootstrap_set_frame_begin_client(void (*client)(void))
{
    frame_begin_client = client;
}

void mp_bootstrap_set_report_client(void (*client)(const char *why))
{
    report_client = client;
}

void mp_bootstrap_set_shutdown_client(void (*client)(void))
{
    shutdown_client = client;
}

static uint32_t __cdecl tail_proc(int msg, int arg, float dt)
{
    uint32_t answer = mp_module_tail_proc(msg, arg, dt);

    if (msg == MODULE_MSG_FRAME_BEGIN) {
        mp_diag_report_hook_shapes();       /* once, and it knows it */
        /* A walk of the object pool on every drawn frame is a session's measurement, and so is
         * the report at a level end: single player pays for neither. */
        if (mp_armed_transport()) {
            mp_capacity_sample();
            if (frame_begin_client != NULL) {
                frame_begin_client();
            }
        }
    } else if (msg == MODULE_MSG_LEVEL_END && mp_armed_transport()) {
        mp_module_report("a level end");
        mp_task_report("a level end");
        mp_capacity_report("a level end");
        mp_pool_report("a level end");
        if (report_client != NULL) {
            report_client("a level end");
        }
    } else if (msg == MODULE_MSG_SHUTDOWN) {
        if (shutdown_client != NULL) {
            shutdown_client();
        }
        mp_module_report("the quit");
        mp_task_report("the quit");
        mp_capacity_report("the quit");
        mp_pool_report("the quit");
        if (report_client != NULL) {
            report_client("the quit");
        }
    }
    return answer;
}

/* The head node runs LAST of all of them, because the registry broadcasts from its tail towards
 * its head and the bootstrap relinks this node to the front. That is what makes it the right place
 * to read a finished substep from: every task has run, and so has the collision pass, which is
 * itself a listener on this same message and sits further down the list.
 *
 * The client is called rather than inlined so that the module layer keeps knowing only about
 * messages, and the bridge keeps knowing only about substeps. */
static void (*substep_end_client)(void);
static void (*module_message_client)(int msg);

void mp_bootstrap_set_substep_end_client(void (*client)(void))
{
    substep_end_client = client;
}

void mp_bootstrap_set_module_message_client(void (*client)(int msg))
{
    module_message_client = client;
}

static uint32_t __cdecl head_proc(int msg, int arg, float dt)
{
    uint32_t answer = mp_module_head_proc(msg, arg, dt);

    /* The head node is the LAST to hear a backward broadcast, so by the time this runs every
     * other module has already acted on the message. That is the right side of a level change to
     * be told about one: the banks have moved, and what is wanted here is to notice that they
     * have rather than to be warned that they are about to. */
    if (module_message_client != NULL) {
        module_message_client(msg);
    }
    if (msg == MODULE_MSG_SUBSTEP_END && substep_end_client != NULL) {
        substep_end_client();
    }
    return answer;
}

/* ==============================================================================================
 * The list edit.
 * ============================================================================================ */

static bool read_pointer(uintptr_t address, uintptr_t *out)
{
    uint32_t value = 0;

    if (!memory_try_readable(address, sizeof(value)) || !memory_read_u32(address, &value)) {
        return false;
    }
    *out = (uintptr_t)value;
    return true;
}

static bool write_pointer(uintptr_t address, uintptr_t value)
{
    return patch_write_u32(address, (uint32_t)value) == PATCH_RESULT_OK;
}

/* Both directions, each with a bound, and they have to agree. A forward walk alone would not catch
 * a backward chain that lost its way, and either kind of damage is a hang rather than a wrong
 * answer, so both are worth the twenty microseconds. */
static bool list_is_sane(uintptr_t head, uintptr_t tail, size_t *count_out)
{
    uintptr_t node;
    size_t    forward = 0;
    size_t    backward = 0;
    uintptr_t last_forward = 0;

    for (node = head; node != 0 && forward < MODULE_WALK_LIMIT; ++forward) {
        last_forward = node;
        if (!read_pointer(node + MODULE_NODE_NEXT, &node)) {
            log_error("the module list is not readable %u nodes in from the head",
                      (unsigned)forward);
            return false;
        }
    }
    if (node != 0) {
        log_error("the module list does not end within %u nodes walking forward, which is a cycle",
                  (unsigned)MODULE_WALK_LIMIT);
        return false;
    }

    for (node = tail; node != 0 && backward < MODULE_WALK_LIMIT; ++backward) {
        if (!read_pointer(node + MODULE_NODE_PREV, &node)) {
            log_error("the module list is not readable %u nodes in from the tail",
                      (unsigned)backward);
            return false;
        }
    }
    if (node != 0) {
        log_error("the module list does not end within %u nodes walking backward",
                  (unsigned)MODULE_WALK_LIMIT);
        return false;
    }

    if (forward != backward) {
        log_error("the module list has %u nodes forward and %u backward",
                  (unsigned)forward, (unsigned)backward);
        return false;
    }
    if (last_forward != tail) {
        log_error("walking forward from the head ends at %08X, but the tail pointer says %08X",
                  (unsigned)last_forward, (unsigned)tail);
        return false;
    }

    if (count_out != NULL) {
        *count_out = forward;
    }
    return true;
}

/* Move `node`, which module_install has just appended, to the front of the list.
 *
 * The order below never leaves the list in a state a walk would not survive: the node is first
 * detached completely, which leaves a shorter but correct list, and only then linked in at the
 * front. The line that detaches it from its predecessor is the one whose absence builds a cycle.
 * The design this was written from listed four of the six writes and lacked the first two: with
 * the predecessor still pointing at the node and the node now pointing at the head, a forward
 * walk loops for ever, and the tail pointer still names the node. */
static bool relink_to_head(uintptr_t node, uintptr_t head_cell, uintptr_t tail_cell)
{
    uintptr_t previous = 0;
    uintptr_t old_head = 0;

    if (!read_pointer(node + MODULE_NODE_PREV, &previous) ||
        !read_pointer(head_cell, &old_head)) {
        log_error("the module list could not be read back after the node was installed");
        return false;
    }

    if (previous == 0 || old_head == node) {
        log_info("the node is already at the head of the module list, nothing to relink");
        return true;
    }

    if (!write_pointer(previous + MODULE_NODE_NEXT, 0) ||   /* the line whose absence is a cycle */
        !write_pointer(tail_cell, previous) ||
        !write_pointer(node + MODULE_NODE_PREV, 0) ||
        !write_pointer(node + MODULE_NODE_NEXT, old_head) ||
        !write_pointer(old_head + MODULE_NODE_PREV, node) ||
        !write_pointer(head_cell, node)) {
        log_error("the module list relink failed part way through, the list may be inconsistent");
        return false;
    }

    return true;
}

/* ==============================================================================================
 * The task slot.
 * ============================================================================================ */

/* The five staged dwords are how a caller passes arguments to a task without the scheduler knowing
 * about them, and they are cleared by the registration that consumes them. A registration made at
 * any other moment therefore inherits whatever the last caller left behind, so the area is cleared
 * before it is read rather than after.
 *
 * Exactly five. The simulation target time sits immediately behind them, and a sixth dword would
 * zero the world clock. */
static void clear_task_staging(uintptr_t staging)
{
    uint32_t index;

    for (index = 0; index < TASK_STAGING_DWORDS; ++index) {
        if (patch_write_u32(staging + index * 4u, 0) != PATCH_RESULT_OK) {
            log_warning("the task staging area at %08X could not be cleared, so the record this "
                        "registration takes may inherit somebody else's arguments",
                        (unsigned)(staging + index * 4u));
            return;
        }
    }
}

static uintptr_t register_task(uintptr_t task_register_site, uintptr_t staging)
{
    task_register_fn engine_task_register = (task_register_fn)task_register_site;
    uintptr_t        record;

    if (staging != 0) {
        clear_task_staging(staging);
    } else {
        log_warning("the task staging area is unknown, so this registration may inherit somebody "
                    "else's arguments in the front of its record");
    }

    record = engine_task_register((void *)&mp_task_tick);
    if (record == 0 || record == TASK_REGISTER_FAILED) {
        log_warning("task_register refused (%08X), so there is no substep tick",
                    (unsigned)record);
        return 0;
    }
    return record;
}

/* ==============================================================================================
 * The engine side, run once from inside the engine's own startup.
 * ============================================================================================ */

static void install_engine_side(const char *how)
{
    uintptr_t         install_site;
    uintptr_t         register_site;
    uintptr_t         head_cell;
    uintptr_t         tail_cell;
    module_install_fn engine_module_install;
    uintptr_t         head_value = 0;
    uintptr_t         tail_value = 0;

    if (boot.has_run) {
        return;
    }
    boot.has_run = true;

    log_info("engine side reached through %s", how);

    /* Before either node or the task can stamp anything, and it is allowed to be zero: the census
     * then reports figures it says nobody should believe rather than silently wrong ones. */
    mp_module_set_frame_counter(mp_cells_address(MP_CELL_CLOCK_TICKS));
    mp_task_set_frame_counter(mp_cells_address(MP_CELL_CLOCK_TICKS));

    install_site  = mp_signatures_address(MP_SITE_MODULE_INSTALL);
    register_site = mp_signatures_address(MP_SITE_TASK_REGISTER);
    head_cell     = mp_cells_address(MP_CELL_MODULE_HEAD);
    tail_cell     = mp_cells_address(MP_CELL_MODULE_TAIL);

    if (install_site == 0 || head_cell == 0 || tail_cell == 0) {
        log_error("module_install or the list pointers did not resolve, so no node is installed");
        return;
    }

    engine_module_install = (module_install_fn)install_site;

    /* The tail node first. It is the ordinary case and it needs no edit: the registry appends, and
     * appended means first to hear every backward broadcast. */
    boot.result.tail_node = engine_module_install((void *)&tail_proc, "MPFirst");
    if (boot.result.tail_node == 0) {
        log_error("module_install refused the first node");
        return;
    }

    /* The head node second, then moved. Installing it after the other one means the relink has a
     * predecessor to hand back the tail to, which is the case the code is written for. */
    boot.result.head_node = engine_module_install((void *)&head_proc, "MPLast");
    if (boot.result.head_node == 0) {
        log_error("module_install refused the second node, the first one stays where it is");
        return;
    }

    if (!relink_to_head(boot.result.head_node, head_cell, tail_cell)) {
        return;
    }

    /* The deliberate fault, and only when it was asked for. A third node carrying the tail node's
     * own procedure means every broadcast enters that procedure twice with one head entry between
     * them, which is exactly the shape the census calls "never reached the head". */
    if (boot.provoke) {
        uintptr_t extra = engine_module_install((void *)&mp_module_tail_proc, "MPDouble");

        log_warning("a third node was installed on purpose at %08X to provoke the census guard; "
                    "the report must say a message never reached the head", (unsigned)extra);
    }

    if (!read_pointer(head_cell, &head_value) || !read_pointer(tail_cell, &tail_value)) {
        log_error("the module list pointers could not be read back after the relink");
        return;
    }

    boot.result.list_is_sane = list_is_sane(head_value, tail_value, &boot.result.module_count);
    if (!boot.result.list_is_sane) {
        log_error("the module list did not survive the relink, expect a hang at the next level "
                  "end");
        return;
    }
    /* In the field this read 29 nodes: the registry carries 27 engine handlers, plus these two,
     * so the walk counted the real list and not something that happened to terminate. */
    log_info("module list: %u nodes, head %08X is ours, tail %08X",
             (unsigned)boot.result.module_count, (unsigned)head_value, (unsigned)tail_value);

    if (register_site == 0) {
        log_warning("task_register did not resolve, so there is no substep tick");
        return;
    }
    boot.result.task_record = register_task(register_site, mp_cells_address(MP_CELL_TASK_STAGING));
    if (boot.result.task_record != 0) {
        /* In the field this was 0x008687F0: (0x8687F0 - 0x868740) / 0x2C is exactly 4, so the
         * four game tasks hold slots 0 to 3 and a registration made after startup lands behind
         * them without a trick. */
        log_info("task record at %08X", (unsigned)boot.result.task_record);
    }
}

/* ==============================================================================================
 * The three ways in.
 * ============================================================================================ */

/* The ordinary one. The engine's startup returns non zero on success and both of its failure exits
 * lead to shutdown, so its return value is the one clean signal that the engine is built. The
 * startup at 0x0043E613 has exactly one caller, so the detour needs no reentrancy guard; its
 * prologue is nine clean bytes with no relative operand, and it ends `B8 01 00 00 00 / 8B E5 /
 * 5D / C3`, cdecl, returning 1 on success.
 *
 * The caller above it was refused: the main at 0x0043E5E0 opens `55 8B EC E8 2B 00 00 00`, with
 * the E8 at offset 3, so the five byte window a detour overwrites ends inside the call's rel32.
 * The same holds for the frame entry and the render frame begin, which is why neither is a way
 * in. */
static int32_t __cdecl hook_sys_startup(void)
{
    sys_startup_fn original = (sys_startup_fn)boot.startup_detour.original;
    int32_t        result   = original();

    if (result != 0) {
        install_engine_side("the engine's own startup");
    }
    return result;
}

/* The first fallback, the campaign loop at 0x0043EB2A. By the time it is entered the engine is
 * built, so this one installs before calling on rather than after: it needs no return value and
 * therefore does not depend on a signature having read one correctly. */
static void __cdecl hook_campaign_run(void)
{
    campaign_run_fn original = (campaign_run_fn)boot.campaign_detour.original;

    install_engine_side("the campaign loop");
    original();
}

/* The last resort. It runs after the first frame has been presented, which is later than either of
 * the others and later than the first level load, but it is still inside a built engine. */
static void frame_callback(void)
{
    install_engine_side("the end of a rendered frame");
}

bool mp_bootstrap_arm(bool provoke_double_delivery)
{
    uintptr_t site;

    if (boot.armed) {
        return true;
    }
    boot.provoke = provoke_double_delivery;

    site = mp_signatures_address(MP_SITE_SYS_STARTUP);
    if (site != 0 &&
        detour_install(&boot.startup_detour, site, (const void *)&hook_sys_startup,
                       mp_signatures_prologue(MP_SITE_SYS_STARTUP))) {
        boot.armed = true;
        boot.path  = MP_BOOTSTRAP_SYS_STARTUP;
        log_info("armed on the engine's startup at %08X", (unsigned)site);
        return true;
    }

    site = mp_signatures_address(MP_SITE_CAMPAIGN_RUN);
    if (site != 0 &&
        detour_install(&boot.campaign_detour, site, (const void *)&hook_campaign_run,
                       mp_signatures_prologue(MP_SITE_CAMPAIGN_RUN))) {
        boot.armed = true;
        boot.path  = MP_BOOTSTRAP_CAMPAIGN_RUN;
        log_warning("the engine's startup could not be hooked, armed on the campaign loop at %08X "
                    "instead", (unsigned)site);
        return true;
    }

    if (frame_hook_add(&frame_callback)) {
        boot.armed = true;
        boot.path  = MP_BOOTSTRAP_FRAME_HOOK;
        log_warning("neither the engine's startup nor the campaign loop could be hooked, armed on "
                    "the end of a rendered frame instead");
        return true;
    }

    boot.path = MP_BOOTSTRAP_NONE;
    log_error("no way into the engine could be armed, this feature stays out of the process");
    return false;
}
