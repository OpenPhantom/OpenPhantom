/* memory_guard.c: the guarded copy. See the header for why it exists. */
#include "common/memory_guard.h"

#include <windows.h>

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <string.h>

/* The stub, exactly as the start checks it:
 *
 *     push esi; push edi; mov edi,[esp+12]; mov esi,[esp+16]; mov ecx,[esp+20];
 *     rep movsb; pop edi; pop esi; mov eax,1; ret
 *
 * `rep movsb` is the one instruction that touches memory the caller named, so a fault inside the
 * stub is a fault there, at offset 14, with the stack as the two pushes left it: the saved EDI at
 * [esp], ESI at [esp+4], the return address at [esp+8] and the fault record at [esp+24]. */
static const uint8_t STUB_BYTES[] = {
    0x56, 0x57, 0x8B, 0x7C, 0x24, 0x0C, 0x8B, 0x74, 0x24, 0x10, 0x8B, 0x4C,
    0x24, 0x14, 0xF3, 0xA4, 0x5F, 0x5E, 0xB8, 0x01, 0x00, 0x00, 0x00, 0xC3,
};

#define STUB_FAULT_OFFSET 14u

/* The parameters are read by the assembly through the stack, which the compiler cannot see; the
 * casts keep the warning about unreferenced parameters away and emit no code in a naked body. */
static __declspec(naked) int __cdecl guarded_copy(void *destination, const void *source,
                                                  size_t size, memory_guard_fault_t *fault)
{
    (void)destination;
    (void)source;
    (void)size;
    (void)fault;
    __asm {
        push esi
        push edi
        mov  edi, [esp + 12]
        mov  esi, [esp + 16]
        mov  ecx, [esp + 20]
        rep  movsb
        pop  edi
        pop  esi
        mov  eax, 1
        ret
    }
}

static struct {
    volatile LONG state;       /* memory_guard_state_t */
    uintptr_t     fault_pc;    /* published before the handler is installed */
    volatile LONG redirected;
} guard;

const uint8_t *memory_guard_follow_jump(const uint8_t *code)
{
    int32_t relative;

    if (code == NULL || code[0] != 0xE9u) {
        return code;
    }
    memcpy(&relative, code + 1, sizeof relative);
    return code + 5 + relative;
}

bool memory_guard_stub_matches(const uint8_t *code)
{
    return code != NULL && memcmp(code, STUB_BYTES, sizeof STUB_BYTES) == 0;
}

/* Only a fault at the stub's copy, and only the three ways a copy of memory the process does not
 * hold can end. The stub's own epilogue is replayed onto the context, with a result of nought, and
 * the fault record the caller handed over is filled in; everything else goes on down the chain. */
static LONG CALLBACK guard_handler(EXCEPTION_POINTERS *pointers)
{
    const EXCEPTION_RECORD *record;
    CONTEXT                *context;
    uintptr_t               esp;
    memory_guard_fault_t   *fault;

    if (pointers == NULL || pointers->ExceptionRecord == NULL || pointers->ContextRecord == NULL) {
        return EXCEPTION_CONTINUE_SEARCH;
    }
    record  = pointers->ExceptionRecord;
    context = pointers->ContextRecord;
    if (record->ExceptionCode != EXCEPTION_ACCESS_VIOLATION &&
        record->ExceptionCode != EXCEPTION_IN_PAGE_ERROR &&
        record->ExceptionCode != EXCEPTION_GUARD_PAGE) {
        return EXCEPTION_CONTINUE_SEARCH;
    }
    if ((uintptr_t)context->Eip != guard.fault_pc) {
        return EXCEPTION_CONTINUE_SEARCH;
    }
    esp   = (uintptr_t)context->Esp;
    fault = *(memory_guard_fault_t **)(esp + 24u);
    if (fault != NULL) {
        fault->address = record->NumberParameters >= 2u
                             ? (uintptr_t)record->ExceptionInformation[1]
                             : 0u;
        fault->guard   = record->ExceptionCode == EXCEPTION_GUARD_PAGE;
    }
    context->Edi = *(const DWORD *)(esp);
    context->Esi = *(const DWORD *)(esp + 4u);
    context->Eip = *(const DWORD *)(esp + 8u);
    context->Esp = (DWORD)(esp + 12u);
    context->Eax = 0u;
    (void)InterlockedIncrement(&guard.redirected);
    return EXCEPTION_CONTINUE_EXECUTION;
}

/* The first caller starts it. One that arrives while another thread is starting it is told no for
 * that call and takes its fallback, rather than waiting or copying without a handler. */
static bool start(void)
{
    const uint8_t *code;
    LONG           seen;

    seen = InterlockedCompareExchange(&guard.state, (LONG)MEMORY_GUARD_STARTING,
                                      (LONG)MEMORY_GUARD_UNKNOWN);
    if (seen == (LONG)MEMORY_GUARD_READY) {
        return true;
    }
    if (seen != (LONG)MEMORY_GUARD_UNKNOWN) {
        return false;
    }
    code = memory_guard_follow_jump((const uint8_t *)(uintptr_t)&guarded_copy);
    if (!memory_guard_stub_matches(code)) {
        (void)InterlockedExchange(&guard.state, (LONG)MEMORY_GUARD_REFUSED);
        return false;
    }
    guard.fault_pc = (uintptr_t)code + STUB_FAULT_OFFSET;
    MemoryBarrier();
    if (AddVectoredExceptionHandler(1, guard_handler) == NULL) {
        (void)InterlockedExchange(&guard.state, (LONG)MEMORY_GUARD_REFUSED);
        return false;
    }
    (void)InterlockedExchange(&guard.state, (LONG)MEMORY_GUARD_READY);
    return true;
}

bool memory_guard_ready(void)
{
    if (guard.state == (LONG)MEMORY_GUARD_READY) {
        return true;
    }
    return start();
}

memory_guard_state_t memory_guard_state(void)
{
    return (memory_guard_state_t)guard.state;
}

bool memory_guard_copy(void *destination, const void *source, size_t size,
                       memory_guard_fault_t *fault)
{
    memory_guard_fault_t unused = { 0u, false };

    return guarded_copy(destination, source, size, fault != NULL ? fault : &unused) != 0;
}

uint32_t memory_guard_redirected(void)
{
    return (uint32_t)guard.redirected;
}
