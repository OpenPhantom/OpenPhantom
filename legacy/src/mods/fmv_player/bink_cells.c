/* bink_cells.c: the retail Bink player's two cells, and the watch on that player. See the header.
 *
 * ==============================================================================================
 * Byte basis
 *
 * The fullscreen player at 0x004979E0 installs 0x00497E5D as the movie window's handler and runs
 * a loop that ends when its abort cell is set and a frame has been shown. The handler is a switch
 * on the message, and its case bodies write the two cells this file wants. Against the retail
 * executable, from the head of the switch at 0x00497E67:
 *
 *   00497E67  83 7D FC 20 77 11 83 7D FC 20 74 1F 83 7D FC 10 74 70 E9 85 00 00 00
 *   00497E7E  81 7D FC 02 01 00 00 74 3F 81 7D FC 00 02 00 00 74 1A EB 71
 *             the compares against 0x20, 0x10, 0x102 and 0x200, and the jumps between them
 *   00497EC6  83 3D 94 20 86 00 00           cmp [s_bAbortOnKey], 0   head+0x5F, cell at +0x61
 *   00497ECD  74 0A                          jz  +10
 *   00497ECF  C7 05 98 20 86 00 01 00 00 00  mov [s_bAbort], 1        head+0x68, cell at +0x6A
 *   00497EE9  C7 05 98 20 86 00 01 00 00 00  mov [s_bAbort], 1        head+0x82, cell at +0x84
 *
 * 0x102 is WM_CHAR, which sets the abort cell only while the key skip is set; the player writes
 * the key skip from its own flags argument and nowhere else. 0x10 is WM_CLOSE, whose case is dead
 * on this window because the game's window procedure answers WM_CLOSE before the handler is asked,
 * but its bytes are there and name the abort cell a second time.
 *
 * The 43 bytes of the switch head carry no address and match exactly once in each of the six
 * executables checked, the Edit Tool's recompile included, where the cells sit 0x50 lower. The
 * three operands lie behind the pattern, so the opcode and the immediate around each are checked
 * before an operand is read.
 * ============================================================================================== */
#include "bink_cells.h"

#include "common/logging.h"
#include "common/memory.h"
#include "common/signature.h"

#include <windows.h>

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

static const uint8_t SIG_BINK_WNDPROC_SWITCH[] = {
    0x83, 0x7D, 0xFC, 0x20,                     /* cmp [ebp-4], 0x20            */
    0x77, 0x11,                                 /* ja  +0x11                    */
    0x83, 0x7D, 0xFC, 0x20,                     /* cmp [ebp-4], 0x20            */
    0x74, 0x1F,                                 /* jz  +0x1F                    */
    0x83, 0x7D, 0xFC, 0x10,                     /* cmp [ebp-4], 0x10            */
    0x74, 0x70,                                 /* jz  +0x70                    */
    0xE9, 0x85, 0x00, 0x00, 0x00,               /* jmp +0x85                    */
    0x81, 0x7D, 0xFC, 0x02, 0x01, 0x00, 0x00,   /* cmp [ebp-4], 0x102           */
    0x74, 0x3F,                                 /* jz  +0x3F                    */
    0x81, 0x7D, 0xFC, 0x00, 0x02, 0x00, 0x00,   /* cmp [ebp-4], 0x200           */
    0x74, 0x1A,                                 /* jz  +0x1A                    */
    0xEB, 0x71                                  /* jmp +0x71                    */
};

/* The one match a build of this engine has; any other count is another build, and nothing is
 * read. */
#define BINK_SWITCH_MATCHES 1u

/* The three instructions behind the head, each with the offset of its opcode, of its operand and
 * of the byte or word that follows the operand. */
#define KEY_SKIP_CMP_AT        0x5Fu   /* 83 3D [cell] 00 */
#define KEY_SKIP_OPERAND       0x61u
#define KEY_SKIP_IMM8          0x65u
#define ABORT_ON_KEY_AT        0x68u   /* C7 05 [cell] 01 00 00 00 */
#define ABORT_ON_KEY_OPERAND   0x6Au
#define ABORT_ON_KEY_IMM32     0x6Eu
#define ABORT_ON_CLOSE_AT      0x82u   /* C7 05 [cell] 01 00 00 00 */
#define ABORT_ON_CLOSE_OPERAND 0x84u
#define ABORT_ON_CLOSE_IMM32   0x88u

/* How often the watch runs. The player's pump dispatches it at most once a round, so a shorter
 * period only adds timers that coalesce; this is the period the session's own timer runs at. */
#define BINK_WATCH_MS 30u

typedef struct bink_state {
    bool          resolved;
    int32_t      *abort;      /* s_bAbort: the loop ends once this is set and a frame is shown */
    int32_t      *key_skip;   /* s_bAbortOnKey: whether a character key sets the abort cell */

    /* The watch over one movie. */
    movie_loop_t *loop;       /* NULL between movies, and the timer then does nothing */
    bool          raised;     /* the host is done and the abort cell is written every round */
    DWORD         begin_tick;
    DWORD         raised_tick;
} bink_state_t;

static bink_state_t bink;

static bool bytes_are(uintptr_t address, const uint8_t *expected, size_t size)
{
    uint8_t actual[4];
    size_t  i;

    if (size > sizeof actual || !memory_read(address, actual, size)) {
        return false;
    }
    for (i = 0; i < size; ++i) {
        if (actual[i] != expected[i]) {
            return false;
        }
    }
    return true;
}

/* The opcode in front of an operand and what follows it, both, so the operand is only ever read
 * out of the instruction it belongs to. */
static bool instructions_are_there(uintptr_t head)
{
    static const uint8_t CMP_MEM_IMM8[] = { 0x83, 0x3D };
    static const uint8_t MOV_MEM_IMM32[] = { 0xC7, 0x05 };
    static const uint8_t ZERO[] = { 0x00 };
    static const uint8_t ONE[] = { 0x01, 0x00, 0x00, 0x00 };

    return bytes_are(head + KEY_SKIP_CMP_AT, CMP_MEM_IMM8, sizeof CMP_MEM_IMM8) &&
           bytes_are(head + KEY_SKIP_IMM8, ZERO, sizeof ZERO) &&
           bytes_are(head + ABORT_ON_KEY_AT, MOV_MEM_IMM32, sizeof MOV_MEM_IMM32) &&
           bytes_are(head + ABORT_ON_KEY_IMM32, ONE, sizeof ONE) &&
           bytes_are(head + ABORT_ON_CLOSE_AT, MOV_MEM_IMM32, sizeof MOV_MEM_IMM32) &&
           bytes_are(head + ABORT_ON_CLOSE_IMM32, ONE, sizeof ONE);
}

bool bink_cells_resolve(void)
{
    uintptr_t head = 0;
    size_t    matches;
    uint32_t  key_skip = 0;
    uint32_t  abort_on_key = 0;
    uint32_t  abort_on_close = 0;

    matches = signature_count_matches(SIG_BINK_WNDPROC_SWITCH, NULL,
                                      sizeof SIG_BINK_WNDPROC_SWITCH, &head, 1u);
    if (matches != BINK_SWITCH_MATCHES) {
        log_warning("the Bink player's cells did not resolve (the movie handler's switch matched "
                    "%u time(s), not %u), so a client's retail movie cannot be ended with the "
                    "host's; it runs to its own end and then waits", (unsigned)matches,
                    (unsigned)BINK_SWITCH_MATCHES);
        return false;
    }
    if (!instructions_are_there(head) ||
        !memory_read_image_cell(head + KEY_SKIP_OPERAND, sizeof(int32_t), &key_skip) ||
        !memory_read_image_cell(head + ABORT_ON_KEY_OPERAND, sizeof(int32_t), &abort_on_key) ||
        !memory_read_image_cell(head + ABORT_ON_CLOSE_OPERAND, sizeof(int32_t), &abort_on_close)) {
        log_warning("the Bink player's cells did not resolve (the instructions behind the switch "
                    "at %08X are not the three expected), so a client's retail movie cannot be "
                    "ended with the host's; it runs to its own end and then waits",
                    (unsigned)head);
        return false;
    }
    /* The same cell written twice, twenty six bytes apart. A disagreement means the pattern is
     * not where it was believed to be, and the answer to that is to write nothing. */
    if (abort_on_key != abort_on_close) {
        log_warning("the Bink player's cells did not resolve (the two encodings of the abort cell "
                    "disagree, %08X and %08X), so a client's retail movie cannot be ended with the "
                    "host's; it runs to its own end and then waits", (unsigned)abort_on_key,
                    (unsigned)abort_on_close);
        return false;
    }
    bink.abort    = (int32_t *)(uintptr_t)abort_on_key;
    bink.key_skip = (int32_t *)(uintptr_t)key_skip;
    bink.resolved = true;
    log_info("the Bink player's cells: abort at %08X and key skip at %08X, both encodings agree",
             (unsigned)abort_on_key, (unsigned)key_skip);
    return true;
}

/* The session let the player go while the retail movie plays: the key skip the player was handed
 * as 0 is set as the engine's own call would have set it, so a key ends the movie again. */
static void give_the_key_back(movie_loop_t *loop)
{
    movie_rule_let_go(loop);
    if (bink.resolved) {
        *bink.key_skip = 1;
        log_info("the Bink player's key skip at %08X is given back, so a key ends this movie again",
                 (unsigned)(uintptr_t)bink.key_skip);
    }
}

/* Dispatched by the player's own pump, on the game's thread, between two of its rounds. The abort
 * cell is written again every round once the host is done, because the player clears it once
 * after its first pump. */
static void CALLBACK watch_the_player(HWND window, UINT message, UINT_PTR id, DWORD time)
{
    movie_loop_t *loop = bink.loop;

    (void)window;
    (void)message;
    (void)id;
    (void)time;
    if (loop == NULL) {
        return;
    }
    ++loop->counts.thread_timers;
    if (!bink.raised && loop->poll != NULL) {
        movie_verdict_t verdict = loop->poll();

        if (verdict == MOVIE_VERDICT_HOST_DONE && bink.resolved) {
            bink.raised      = true;
            bink.raised_tick = GetTickCount();
        } else if (verdict != MOVIE_VERDICT_GO_ON && verdict != MOVIE_VERDICT_HOST_DONE) {
            give_the_key_back(loop);
        }
    }
    if (bink.raised) {
        *bink.abort = 1;
    }
}

int bink_cells_play(bink_movie_fn_t original, const char *name, int param2, int param3,
                    movie_loop_t *loop)
{
    bool     held = loop->poll != NULL;
    UINT_PTR timer;
    int      result;

    bink.loop       = loop;
    bink.raised     = false;
    bink.begin_tick = GetTickCount();
    timer = SetTimer(NULL, 0, BINK_WATCH_MS, &watch_the_player);
    if (timer == 0u) {
        log_warning("the watch on the retail player could not be set (error %lu), so \"%s\" is "
                    "neither counted nor ended with the host's", (unsigned long)GetLastError(),
                    name != NULL ? name : "");
    }
    if (held && bink.resolved) {
        log_info("\"%s\" plays through the retail Bink path as a client of the host's: the key "
                 "skip is handed 0 instead of 1, and the host's end is written into the player's "
                 "abort cell at %08X", name != NULL ? name : "",
                 (unsigned)(uintptr_t)bink.abort);
    } else if (held) {
        log_info("\"%s\" plays through the retail Bink path as a client of the host's: the key "
                 "skip is handed 0 instead of 1, and the abort cell did not resolve, so it runs to "
                 "its own end and then waits", name != NULL ? name : "");
    }

    result = original(name, held ? 0 : param2, param3);

    if (timer != 0u) {
        KillTimer(NULL, timer);
    }
    bink.loop = NULL;
    if (bink.raised) {
        loop->end = MOVIE_END_HOST;
        log_info("the host's movie is over: the Bink player's abort cell was raised after %u ms of "
                 "playback", (unsigned)(bink.raised_tick - bink.begin_tick));
    } else {
        loop->end = MOVIE_END_RETAIL;
    }
    return result;
}
