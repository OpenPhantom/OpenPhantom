/* debug_register.h: the x86 debug register bit arithmetic, with no Windows and no engine in it.
 *
 * A hardware data breakpoint is the only instrument that answers "which instruction wrote this
 * field" without guessing. Four of them exist per thread, in DR0 to DR3, and DR7 says for each
 * whether it is enabled, whether it watches reads or writes, and how wide it is. Getting any of
 * those bits wrong does not fail loudly: it arms a breakpoint on the wrong address or the wrong
 * width, and the report that comes back is then confidently wrong. So the bit packing lives here,
 * away from the thread and exception handling, where it can be tested against the values the
 * manual specifies.
 *
 * DR7, the control register:
 *
 *   bits 0,2,4,6    local enable for slots 0 to 3
 *   bits 1,3,5,7    global enable, not used here: local is what a per thread watch wants
 *   bits 16 to 31   four bit field per slot, the low two bits the condition and the high two the
 *                   length, slot 0 at bit 16 and each further slot four bits higher
 *
 * DR6, the status register, reports which slot fired in its low four bits. It is NOT cleared by
 * the processor, so a handler that does not clear it sees the same slot reported forever.
 */
#ifndef DIAGNOSTICS_DEBUG_REGISTER_H
#define DIAGNOSTICS_DEBUG_REGISTER_H

#include <stdbool.h>
#include <stdint.h>

#define DEBUG_REGISTER_SLOTS 4u

/* The condition field. Execute is deliberately absent: this file exists to watch data, and an
 * execute breakpoint needs the address to be an instruction boundary, which nothing here checks. */
typedef enum debug_watch {
    DEBUG_WATCH_WRITE = 1,      /* 01, on data writes only */
    DEBUG_WATCH_READ_WRITE = 3  /* 11, on data reads and writes */
} debug_watch_t;

/* The length field encodes 1, 2 and 4 bytes as 0, 1 and 3. The 3 is not a typo for 2: the value
 * for four bytes really is 11, and 10 means eight bytes, which does not exist on this target. */
typedef enum debug_length {
    DEBUG_LENGTH_1 = 0,
    DEBUG_LENGTH_2 = 1,
    DEBUG_LENGTH_4 = 3
} debug_length_t;

/* Builds the DR7 value that arms `slot` on top of an existing DR7, leaving the other slots alone.
 * Returns the previous value unchanged when the slot, condition or length is not one of the values
 * above, since arming a breakpoint from a number nobody checked is how a watch ends up on the
 * wrong width. */
uint32_t debug_register_arm(uint32_t dr7, uint32_t slot, debug_watch_t watch,
                            debug_length_t length);

/* Clears one slot's enable and its condition and length field. */
uint32_t debug_register_disarm(uint32_t dr7, uint32_t slot);

/* Whether DR6 says this slot is the one that fired. */
bool debug_register_fired(uint32_t dr6, uint32_t slot);

/* DR6 with this slot's report cleared. The processor never clears these, so a handler that leaves
 * them set reports a stale slot on the next unrelated debug exception. */
uint32_t debug_register_acknowledge(uint32_t dr6, uint32_t slot);

/* A watched address has to be aligned to its own width or the processor's behaviour is undefined,
 * and in practice the breakpoint simply never fires, which reads exactly like "nothing writes this
 * field". Checked rather than assumed for that reason. */
bool debug_register_address_is_aligned(uintptr_t address, debug_length_t length);

#endif /* DIAGNOSTICS_DEBUG_REGISTER_H */
