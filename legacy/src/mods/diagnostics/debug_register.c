/* debug_register.c: see debug_register.h. Bit arithmetic only. */
#include "debug_register.h"

#define DR7_CONDITION_SHIFT 16u   /* slot 0's four bit field starts here */
#define DR7_FIELD_BITS      4u
#define DR7_FIELD_MASK      0xFu

static bool slot_is_valid(uint32_t slot)
{
    return slot < DEBUG_REGISTER_SLOTS;
}

static bool watch_is_valid(debug_watch_t watch)
{
    return watch == DEBUG_WATCH_WRITE || watch == DEBUG_WATCH_READ_WRITE;
}

static bool length_is_valid(debug_length_t length)
{
    return length == DEBUG_LENGTH_1 || length == DEBUG_LENGTH_2 || length == DEBUG_LENGTH_4;
}

/* The local enable bit for a slot sits at twice the slot number: 0, 2, 4, 6. The odd bits between
 * them are the global enables, which this never sets. */
static uint32_t local_enable_bit(uint32_t slot)
{
    return 1u << (slot * 2u);
}

static uint32_t field_shift(uint32_t slot)
{
    return DR7_CONDITION_SHIFT + (slot * DR7_FIELD_BITS);
}

uint32_t debug_register_arm(uint32_t dr7, uint32_t slot, debug_watch_t watch,
                            debug_length_t length)
{
    uint32_t shift;
    uint32_t field;

    if (!slot_is_valid(slot) || !watch_is_valid(watch) || !length_is_valid(length)) {
        return dr7;
    }
    shift = field_shift(slot);
    field = ((uint32_t)watch & 0x3u) | (((uint32_t)length & 0x3u) << 2u);

    dr7 &= ~(DR7_FIELD_MASK << shift);
    dr7 |= field << shift;
    dr7 |= local_enable_bit(slot);
    return dr7;
}

uint32_t debug_register_disarm(uint32_t dr7, uint32_t slot)
{
    if (!slot_is_valid(slot)) {
        return dr7;
    }
    dr7 &= ~(DR7_FIELD_MASK << field_shift(slot));
    dr7 &= ~local_enable_bit(slot);
    return dr7;
}

bool debug_register_fired(uint32_t dr6, uint32_t slot)
{
    if (!slot_is_valid(slot)) {
        return false;
    }
    return (dr6 & (1u << slot)) != 0u;
}

uint32_t debug_register_acknowledge(uint32_t dr6, uint32_t slot)
{
    if (!slot_is_valid(slot)) {
        return dr6;
    }
    return dr6 & ~(1u << slot);
}

bool debug_register_address_is_aligned(uintptr_t address, debug_length_t length)
{
    switch (length) {
    case DEBUG_LENGTH_1:
        return true;
    case DEBUG_LENGTH_2:
        return (address & 1u) == 0u;
    case DEBUG_LENGTH_4:
        return (address & 3u) == 0u;
    default:
        return false;
    }
}
