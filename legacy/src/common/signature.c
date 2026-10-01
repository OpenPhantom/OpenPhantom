#include "signature.h"

#include "host_image.h"
#include "logging.h"

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <string.h>

static bool matches_at(const uint8_t *candidate, const uint8_t *bytes, const uint8_t *mask,
                       size_t size)
{
    size_t index;

    if (mask == NULL) {
        return memcmp(candidate, bytes, size) == 0;
    }

    for (index = 0; index < size; ++index) {
        if (mask[index] != 0 && candidate[index] != bytes[index]) {
            return false;
        }
    }
    return true;
}

size_t signature_count_in_buffer(const uint8_t *haystack, size_t haystack_size,
                                 const uint8_t *bytes, const uint8_t *mask, size_t size,
                                 size_t *offsets, size_t max_offsets)
{
    size_t last_start;
    size_t offset;
    size_t hits = 0;

    if (haystack == NULL || bytes == NULL || size == 0 || haystack_size < size) {
        return 0;
    }

    last_start = haystack_size - size;
    for (offset = 0; offset <= last_start; ++offset) {
        if (mask == NULL && haystack[offset] != bytes[0]) {
            continue;                              /* cheap reject on the common path */
        }
        if (!matches_at(haystack + offset, bytes, mask, size)) {
            continue;
        }
        if (offsets != NULL && hits < max_offsets) {
            offsets[hits] = offset;
        }
        ++hits;
    }

    return hits;
}

/* Staging for the offsets the buffer search reports, before they are turned into addresses. The
 * bound is the one the header publishes, so the two cannot drift apart. */
#define MAX_STAGED_OFFSETS SIGNATURE_MAX_REPORTED

size_t signature_count_matches(const uint8_t *bytes, const uint8_t *mask, size_t size,
                               uintptr_t *addresses, size_t max_addresses)
{
    const uint8_t *text = (const uint8_t *)host_image_text();
    size_t         offsets[MAX_STAGED_OFFSETS];
    size_t         wanted;
    size_t         hits;
    size_t         index;

    if (text == NULL) {
        return 0;
    }

    wanted = (addresses == NULL) ? 0
           : (max_addresses < MAX_STAGED_OFFSETS) ? max_addresses : MAX_STAGED_OFFSETS;

    hits = signature_count_in_buffer(text, host_image_text_size(), bytes, mask, size,
                                     offsets, wanted);

    for (index = 0; index < hits && index < wanted; ++index) {
        addresses[index] = (uintptr_t)(text + offsets[index]);
    }
    return hits;
}

uintptr_t signature_find_unique(const uint8_t *bytes, const uint8_t *mask, size_t size)
{
    uintptr_t address = 0;
    size_t    hits;

    hits = signature_count_matches(bytes, mask, size, &address, 1);
    return (hits == 1) ? address : 0;
}

#define JMP_REL32_OPCODE 0xE9u

/* How many tail hits this sifts before it gives up.
 *
 * It was eight, and eight is below what the shipped image holds for the tail of a short function.
 * The camera take a scene asks for is a load, a store and a return, and that tail stands 24 times;
 * the savegame load by name 31 times; the enemy use latch 27. At each of those exactly one
 * candidate carries the authored prologue or a branch, so the head test below decides them, but
 * the search gave up before the test ran and the site counted as unresolved. Sixty four is twice
 * the largest tail count measured over this tree's detour targets. It costs one masked compare of
 * a prologue per candidate, once, at install time, and 256 bytes of stack; a tail that stands more
 * often than that is a pattern to cut longer, not to sift further. */
#define MAX_TAIL_CANDIDATES 64

/* Stage 2 of the detour-target rule: the site may already carry somebody's `jmp rel32`, so the
 * prologue cannot be part of the search. Anchor on the tail and prove the head.
 *
 * The candidates come from the buffer search directly rather than from signature_count_matches,
 * because that one stages its addresses in an array of its own whose bound is the eight the header
 * publishes; asking it for more would leave the candidates past the eighth unwritten. */
static uintptr_t find_by_tail(const uint8_t *bytes, const uint8_t *mask, size_t size,
                              size_t prologue_size)
{
    const uint8_t *text = (const uint8_t *)host_image_text();
    size_t         offsets[MAX_TAIL_CANDIDATES];
    uintptr_t      accepted = 0;
    size_t         accepted_count = 0;
    size_t         hits;
    size_t         index;

    if (text == NULL || prologue_size == 0 || prologue_size >= size) {
        return 0;
    }

    hits = signature_count_in_buffer(text, host_image_text_size(), bytes + prologue_size,
                                     (mask != NULL) ? mask + prologue_size : NULL,
                                     size - prologue_size, offsets, MAX_TAIL_CANDIDATES);
    if (hits == 0) {
        return 0;                    /* the table's own "0 matches" line covers this */
    }
    if (hits > MAX_TAIL_CANDIDATES) {
        log_warning("  a detour target's tail matched %u times, more than the %u this can sift, "
                    "so the site is treated as unresolved", (unsigned)hits,
                    (unsigned)MAX_TAIL_CANDIDATES);
        return 0;
    }

    for (index = 0; index < hits; ++index) {
        uintptr_t start;

        /* The tail can sit at the very front of the section, where there is no room for a
         * prologue in front of it. That is a rejected candidate, not an underflow. */
        if (offsets[index] < prologue_size) {
            continue;
        }
        start = (uintptr_t)(text + offsets[index] - prologue_size);
        /* Either the prologue is still the authored one, mask honoured, or somebody has already
         * branched away from it. Anything else is a coincidental match and is discarded. */
        if (!matches_at((const uint8_t *)start, bytes, mask, prologue_size) &&
            *(const uint8_t *)start != JMP_REL32_OPCODE) {
            continue;
        }

        accepted = start;
        ++accepted_count;
    }

    if (accepted_count != 1) {
        log_warning("  a detour target's tail matched %u times and %u of those carried its "
                    "prologue or a jump, so the site is treated as unresolved", (unsigned)hits,
                    (unsigned)accepted_count);
        return 0;
    }
    return accepted;
}

uintptr_t signature_find_at(uintptr_t address, const uint8_t *bytes, const uint8_t *mask,
                            size_t size, size_t prologue_size)
{
    if (address == 0 || prologue_size == 0 || prologue_size >= size) {
        return 0;
    }
    /* Both ends: a wrong gap must not read past the section, whichever way it is wrong. */
    if (address < host_image_text() ||
        address + size > host_image_text() + host_image_text_size()) {
        return 0;
    }
    /* The tail has to be exactly right: nothing has written there, whoever detoured this. */
    if (!matches_at((const uint8_t *)(address + prologue_size), bytes + prologue_size,
                    (mask != NULL) ? mask + prologue_size : NULL, size - prologue_size)) {
        return 0;
    }
    /* And the head is either still the authored prologue or a branch away from it, which is the
     * same pair find_by_tail accepts. */
    if (!matches_at((const uint8_t *)address, bytes, mask, prologue_size) &&
        *(const uint8_t *)address != JMP_REL32_OPCODE) {
        return 0;
    }
    return address;
}

uintptr_t signature_find_detour_target(const uint8_t *bytes, const uint8_t *mask, size_t size,
                                       size_t prologue_size)
{
    uintptr_t address = signature_find_unique(bytes, mask, size);

    if (address != 0) {
        return address;
    }
    return find_by_tail(bytes, mask, size, prologue_size);
}

size_t signature_resolve_table(signature_t *table, size_t count)
{
    size_t index;
    size_t resolved = 0;

    if (table == NULL) {
        return 0;
    }

    for (index = 0; index < count; ++index) {
        uintptr_t address = 0;
        size_t    hits;

        hits = signature_count_matches(table[index].bytes, table[index].mask,
                                       table[index].size, &address, 1);

        if (hits != 1 && table[index].detour_prologue != 0) {
            address = find_by_tail(table[index].bytes, table[index].mask, table[index].size,
                                   table[index].detour_prologue);
            if (address != 0) {
                table[index].address = address;
                ++resolved;
                log_info("  site %-22s -> %08X (already detoured; found by its tail)",
                         table[index].name, (unsigned)address);
                continue;
            }
        }

        /* Last, and only for an entry that declared a neighbour: a function too short to anchor
           on once its prologue is a jump. The neighbour sits earlier in the table, so it already
           has an address. */
        if (hits != 1 && address == 0 && table[index].follows_entry != 0) {
            size_t previous = table[index].follows_entry - 1u;

            if (previous < count && table[previous].address != 0) {
                address = signature_find_at(table[previous].address + table[index].follows_gap,
                                            table[index].bytes, table[index].mask,
                                            table[index].size, table[index].detour_prologue);
                if (address != 0) {
                    table[index].address = address;
                    ++resolved;
                    log_info("  site %-22s -> %08X (too short to anchor once detoured; found "
                             "behind %s)", table[index].name, (unsigned)address,
                             table[previous].name);
                    continue;
                }
            }
        }

        if (hits == 1) {
            table[index].address = address;
            ++resolved;
            log_info("  site %-22s -> %08X", table[index].name, (unsigned)address);
        } else {
            table[index].address = 0;
            log_warning("  site %-22s -> NOT RESOLVED (%u matches), this patch is DISABLED",
                        table[index].name, (unsigned)hits);
        }
    }

    return resolved;
}

/* The window a site is judged through. Big enough for every pattern in this tree with room to
 * spare; a site declaring more than this is refused rather than read into a shorter buffer. */
#define MAX_OPERAND_WINDOW 256u

static bool window_is_inside_text(uintptr_t address, size_t size)
{
    uintptr_t text = host_image_text();

    if (text == 0 || size > host_image_text_size()) {
        return false;
    }
    return address >= text && address <= (text + host_image_text_size() - size);
}

bool signature_read_address_operand(const signature_t *site, size_t offset, uintptr_t *address)
{
    uint8_t  window[MAX_OPERAND_WINDOW];
    uint32_t operand = 0;
    size_t   span;

    if (site == NULL || address == NULL || site->bytes == NULL || site->address == 0 ||
        site->size == 0 || offset > sizeof(window)) {
        return false;
    }

    /* The pattern window is what decides whether the site is still itself. The operand does not
     * have to sit inside it: an anchor may match a head and name an address that comes later, so
     * the copy that is read has to reach whichever of the two ends further. */
    span = (offset + sizeof(operand) > site->size) ? (offset + sizeof(operand)) : site->size;
    if (span > sizeof(window) || !window_is_inside_text(site->address, span)) {
        return false;
    }

    memcpy(window, (const void *)site->address, span);
    if (matches_at(window, site->bytes, site->mask, site->size)) {
        memcpy(&operand, window + offset, sizeof(operand));
        *address = (uintptr_t)operand;
        return true;
    }

    if (!host_image_read_original(site->address, window, span)) {
        log_warning("  site %s no longer matches its own pattern and the executable on disk could "
                    "not be read, so the operand at +%u is refused",
                    site->name, (unsigned)offset);
        return false;
    }
    if (!matches_at(window, site->bytes, site->mask, site->size)) {
        log_warning("  site %s does not match its own pattern on disk either, so the operand at "
                    "+%u is refused", site->name, (unsigned)offset);
        return false;
    }

    memcpy(&operand, window + offset, sizeof(operand));
    *address = (uintptr_t)operand + (uintptr_t)host_image_relocation_delta();
    log_info("  site %s carries foreign bytes, so its operand at +%u was read from the executable "
             "on disk instead of from memory", site->name, (unsigned)offset);
    return true;
}

size_t signature_count_dword(uint32_t value)
{
    const uint8_t *text;
    size_t         text_size;
    size_t         offset;
    size_t         hits = 0;

    text      = (const uint8_t *)host_image_text();
    text_size = host_image_text_size();
    if (text == NULL || text_size < sizeof(value)) {
        return 0;
    }

    /* The operand of an instruction is not aligned to anything, so every byte offset is a
     * candidate. The dword is assembled with memcpy, which the compiler folds to one unaligned
     * load on x86 and which keeps the read inside the language. */
    for (offset = 0; offset <= text_size - sizeof(value); ++offset) {
        uint32_t candidate;

        memcpy(&candidate, text + offset, sizeof candidate);
        if (candidate == value) {
            ++hits;
        }
    }

    return hits;
}
