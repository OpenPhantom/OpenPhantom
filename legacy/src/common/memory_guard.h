/* memory_guard.h: a copy that turns a fault into a refusal even where another handler resumes it.
 *
 * memory.c used to guard its trying reads with __try around a memcpy. In the game that never ran:
 * the compatibility database of Windows gives WMAIN.EXE the IgnoreException fix with the command
 * line ACCESS_VIOLATION_READ:1, and AcGenral.dll installs it as a vectored handler at the end of
 * the chain. Every read fault in the process, in any module, is resumed behind the instruction that
 * faulted, before any __try frame is asked. A read of a bad pointer went on with whatever the
 * registers held and reported success.
 *
 * So the copy is a small stub of its own at an address this file knows, and a vectored handler of
 * its own, registered at the front of the chain, recognises a fault at exactly that instruction and
 * returns from the stub with a refusal, before the fix at the end of the chain sees anything. It is
 * the shape runtimes use for the same problem (a fetch at a known instruction, and a handler that
 * sends a fault there to a continuation), and it costs a good read what the copy costs.
 *
 * Every DLL links its own copy of this file and so installs its own handler, which recognises only
 * its own stub. Nothing is uninstalled: the loader never frees a feature DLL. Write faults are not
 * resumed by the fix, so writes keep their __try in memory.c.
 */
#ifndef COMMON_MEMORY_GUARD_H
#define COMMON_MEMORY_GUARD_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

/* Where a guarded copy faulted, and whether it was a guard page. */
typedef struct memory_guard_fault {
    uintptr_t address;
    bool      guard;
} memory_guard_fault_t;

typedef enum memory_guard_state {
    MEMORY_GUARD_UNKNOWN,    /* not started yet */
    MEMORY_GUARD_STARTING,   /* another thread is starting it */
    MEMORY_GUARD_READY,      /* the stub is the expected one and its handler is installed */
    MEMORY_GUARD_REFUSED     /* the stub's bytes differ or the handler would not install */
} memory_guard_state_t;

/* Starts the guard on the first call and says whether copies may use it now. False while it is
 * starting on another thread and for good once it was refused; the caller then takes its own
 * fallback for that call. */
bool memory_guard_ready(void);

memory_guard_state_t memory_guard_state(void);

/* Copies `size` bytes. True when every byte was copied; false when the source or the destination
 * faulted, with where in `fault`. Only after memory_guard_ready answered true. */
bool memory_guard_copy(void *destination, const void *source, size_t size,
                       memory_guard_fault_t *fault);

/* Faults this module's handler has turned back into refusals, since the process started. */
uint32_t memory_guard_redirected(void);

/* Pure, for the start and for a test: a relative jump a linker put in front of a function is
 * followed to its target, anything else is returned as it is; and whether the bytes at `code` are
 * the stub this file was built to recognise. */
const uint8_t *memory_guard_follow_jump(const uint8_t *code);
bool memory_guard_stub_matches(const uint8_t *code);

#endif /* COMMON_MEMORY_GUARD_H */
