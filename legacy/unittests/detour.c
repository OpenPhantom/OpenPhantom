/* detour.c: the chained detours, exercised on a stand-in function instead of the engine.
 *
 * This is the one mechanism in the project that is genuinely new rather than borrowed, and until
 * now it was the only load-bearing part of `common` with no test at all. It exists because several
 * independent DLLs legitimately want the same engine function: a conventional trampoline detour
 * placed on an already-detoured target copies the first hook's `jmp` into its own trampoline and
 * builds an infinite loop that reports success.
 *
 * So the test does not check flags and call it proven. It BUILDS a chain of three hooks and CALLS
 * through it, and the order the hooks record is the evidence. A detour that reported success and
 * looped would hang here rather than pass.
 *
 * The stand-in is six bytes of machine code in a page of our own:
 *
 *     B8 2A 00 00 00    mov eax, 42
 *     C3                ret
 *
 * The first five are exactly the prologue length the detour is told to copy, so the trampoline
 * becomes "mov eax,42; jmp target+5" and lands on the ret. Using real compiler output here would
 * be a worse test: the prologue would be whatever the optimiser felt like emitting that day.
 */
#include "unittest.h"

#include "common/detour.h"

#include <windows.h>

#include <string.h>

#define ENGINE_PROLOGUE 5u
#define ENGINE_ANSWER   42

static const unsigned char ENGINE_BODY[] = { 0xB8, 0x2A, 0x00, 0x00, 0x00, 0xC3 };

typedef int (__cdecl *engine_fn_t)(void);



/* Where the hooks write down that they ran, and in which order. */
static char  trail[16];
static size_t trail_length;

static void record(char who)
{
    if (trail_length + 1 < sizeof(trail)) {
        trail[trail_length++] = who;
        trail[trail_length]   = '\0';
    }
}

static detour_t detour_a;
static detour_t detour_b;
static detour_t detour_c;

static int __cdecl hook_a(void)
{
    record('a');
    return ((engine_fn_t)detour_a.original)();
}

static int __cdecl hook_b(void)
{
    record('b');
    return ((engine_fn_t)detour_b.original)();
}

static int __cdecl hook_c(void)
{
    record('c');
    return ((engine_fn_t)detour_c.original)();
}

static unsigned char *make_engine(void)
{
    unsigned char *page = (unsigned char *)VirtualAlloc(NULL, 0x1000,
                                                        MEM_COMMIT | MEM_RESERVE,
                                                        PAGE_EXECUTE_READWRITE);
    if (page == NULL) {
        return NULL;
    }
    memcpy(page, ENGINE_BODY, sizeof(ENGINE_BODY));
    FlushInstructionCache(GetCurrentProcess(), page, 0x1000);
    return page;
}

static int call_engine(unsigned char *engine)
{
    trail_length = 0;
    trail[0]     = '\0';
    return ((engine_fn_t)engine)();
}

/* ==============================================================================================
 * The chain: three hooks on one target, installed one after another, then called.
 * ============================================================================================ */
static void test_chain_unwinds_in_reverse_install_order(void)
{
    unsigned char *engine = make_engine();
    int            answer;

    ut_section("three hooks on one function");
    ut_check(engine != NULL, "the stand-in engine function could be built");
    if (engine == NULL) {
        return;
    }

    answer = call_engine(engine);
    ut_check(answer == ENGINE_ANSWER, "undetoured, the stand-in returns its own answer");
    ut_check(strcmp(trail, "") == 0, "and no hook has run");

    ut_check(detour_install(&detour_a, (uintptr_t)engine, (const void *)hook_a, ENGINE_PROLOGUE),
          "the FIRST detour installs");
    ut_check(detour_a.installed && !detour_a.chained,
          "the first installer is not a chained one: the target was untouched");
    ut_check(detour_a.original != NULL && detour_a.original != (void *)engine,
          "its original is a fresh trampoline, not the target itself");
    ut_check(*engine == 0xE9, "and the target now begins with a jmp rel32");

    answer = call_engine(engine);
    ut_check(answer == ENGINE_ANSWER, "through one hook the answer still comes back");
    ut_check(strcmp(trail, "a") == 0, "and exactly one hook ran");

    ut_check(detour_install(&detour_b, (uintptr_t)engine, (const void *)hook_b, ENGINE_PROLOGUE),
          "the SECOND detour installs on the same function");
    ut_check(detour_b.chained, "the second installer reports that it chained");
    ut_check(detour_b.original == (void *)hook_a,
          "the point of the whole mechanism: its original is the first hook, not a copy of "
          "the prologue that would have jumped back to itself");
    ut_check(detour_b.prologue_size == 0, "a chained detour copies no bytes and says so");

    answer = call_engine(engine);
    ut_check(answer == ENGINE_ANSWER, "through two hooks the answer still comes back");
    ut_check(strcmp(trail, "ba") == 0, "and both ran, newest first");

    ut_check(detour_install(&detour_c, (uintptr_t)engine, (const void *)hook_c, ENGINE_PROLOGUE),
          "the THIRD detour installs");
    ut_check(detour_c.original == (void *)hook_b, "and links in front of the second");

    answer = call_engine(engine);
    ut_check(answer == ENGINE_ANSWER, "through three hooks the answer still comes back");
    ut_check(strcmp(trail, "cba") == 0,
          "all three ran in reverse install order, which is what proves the chain unwinds rather "
          "than looping");

    VirtualFree(engine, 0, MEM_RELEASE);
}

/* Installing the same detour_t twice must not add a second link. It is the ordinary case for a
 * feature whose install function runs again, and a duplicated link would call the whole chain
 * twice per invocation. */
static void test_second_install_of_the_same_detour_is_ignored(void)
{
    unsigned char *engine = make_engine();
    detour_t       detour;
    int            answer;

    ut_section("installing the same detour twice");
    if (engine == NULL) {
        ut_check(0, "the stand-in engine function could be built");
        return;
    }
    memset(&detour, 0, sizeof(detour));
    detour_a.original = NULL;

    ut_check(detour_install(&detour, (uintptr_t)engine, (const void *)hook_a, ENGINE_PROLOGUE),
          "the first install succeeds");
    detour_a = detour;                       /* hook_a calls through detour_a */

    ut_check(detour_install(&detour, (uintptr_t)engine, (const void *)hook_a, ENGINE_PROLOGUE),
          "the second install reports success rather than an error, because it is idempotent");

    answer = call_engine(engine);
    ut_check(answer == ENGINE_ANSWER, "the function still returns its answer");
    ut_check(strcmp(trail, "a") == 0, "and the hook ran ONCE, so no second link was added");

    VirtualFree(engine, 0, MEM_RELEASE);
}

/* ==============================================================================================
 * The refusals. Every one of these leaves the target untouched, which is the property that makes
 * a failed install safe: the feature switches itself off and the engine is exactly as it was.
 * ============================================================================================ */
static void test_bad_arguments_are_refused(void)
{
    unsigned char *engine = make_engine();
    detour_t       detour;

    ut_section("refusals");
    if (engine == NULL) {
        ut_check(0, "the stand-in engine function could be built");
        return;
    }

    memset(&detour, 0, sizeof(detour));
    ut_check(!detour_install(NULL, (uintptr_t)engine, (const void *)hook_a, ENGINE_PROLOGUE),
          "a null detour is refused");
    ut_check(!detour_install(&detour, 0, (const void *)hook_a, ENGINE_PROLOGUE),
          "a null target is refused");
    ut_check(!detour_install(&detour, (uintptr_t)engine, NULL, ENGINE_PROLOGUE),
          "a null hook is refused");

    /* Below five bytes there is no room for the branch itself; above sixteen the caller is almost
     * certainly not describing a prologue any more. */
    ut_check(!detour_install(&detour, (uintptr_t)engine, (const void *)hook_a, 4),
          "a prologue shorter than the jmp that replaces it is refused");
    ut_check(!detour_install(&detour, (uintptr_t)engine, (const void *)hook_a, 17),
          "an implausibly long prologue is refused");
    ut_check(memcmp(engine, ENGINE_BODY, sizeof(ENGINE_BODY)) == 0,
          "and after every refusal the target is byte-for-byte untouched");

    VirtualFree(engine, 0, MEM_RELEASE);
}

/* A target that begins with E9 but branches somewhere that cannot be executed means we misread the
 * branch. Keeping that address as `original` would send the engine to an unaccountable address on
 * the first call rather than failing here, so it has to be refused at install time. */
static void test_a_branch_into_unexecutable_memory_is_refused(void)
{
    unsigned char *engine = make_engine();
    unsigned char *data;
    detour_t       detour;
    unsigned int   displacement;

    ut_section("an existing branch that points at data");
    data = (unsigned char *)VirtualAlloc(NULL, 0x1000, MEM_COMMIT | MEM_RESERVE, PAGE_READWRITE);
    if (engine == NULL || data == NULL) {
        ut_check(0, "the two stand-in pages could be built");
        return;
    }

    engine[0]    = 0xE9;
    displacement = (unsigned int)((uintptr_t)data - ((uintptr_t)engine + 5));
    memcpy(engine + 1, &displacement, sizeof(displacement));

    memset(&detour, 0, sizeof(detour));
    ut_check(!detour_install(&detour, (uintptr_t)engine, (const void *)hook_a, ENGINE_PROLOGUE),
          "a branch into non-executable memory is refused rather than chained onto");
    ut_check(!detour.installed, "and the detour is left uninstalled");

    VirtualFree(data, 0, MEM_RELEASE);
    VirtualFree(engine, 0, MEM_RELEASE);
}

int main(void)
{
    test_chain_unwinds_in_reverse_install_order();
    test_second_install_of_the_same_detour_is_ignored();
    test_bad_arguments_are_refused();
    test_a_branch_into_unexecutable_memory_is_refused();

    return ut_summary("detour");
}
