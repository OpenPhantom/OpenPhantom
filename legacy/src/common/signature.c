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
#define MAX_TAIL_CANDIDATES 8

/* Stage 2 of the detour-target rule: the site may already carry somebody's `jmp rel32`, so the
 * prologue cannot be part of the search. Anchor on the tail and prove the head. */
static uintptr_t find_by_tail(const uint8_t *bytes, const uint8_t *mask, size_t size,
                              size_t prologue_size)
{
    uintptr_t candidates[MAX_TAIL_CANDIDATES];
    uintptr_t accepted = 0;
    size_t    accepted_count = 0;
    size_t    hits;
    size_t    index;

    if (prologue_size == 0 || prologue_size >= size) {
        return 0;
    }

    hits = signature_count_matches(bytes + prologue_size,
                                   (mask != NULL) ? mask + prologue_size : NULL,
                                   size - prologue_size, candidates, MAX_TAIL_CANDIDATES);
    if (hits == 0 || hits > MAX_TAIL_CANDIDATES) {
        return 0;
    }

    for (index = 0; index < hits; ++index) {
        uintptr_t start = candidates[index] - prologue_size;

        if (start < host_image_text()) {
            continue;
        }
        /* Either the prologue is still the authored one, or somebody has already branched away
         * from it. Anything else is a coincidental match and is discarded. */
        if (memcmp((const void *)start, bytes, prologue_size) != 0 &&
            *(const uint8_t *)start != JMP_REL32_OPCODE) {
            continue;
        }

        accepted = start;
        ++accepted_count;
    }

    return (accepted_count == 1) ? accepted : 0;
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

    /* Unaligned dword reads are legal on x86 and are what this census needs: the operand of an
     * instruction is not aligned to anything. */
    for (offset = 0; offset <= text_size - sizeof(value); ++offset) {
        if (*(const uint32_t *)(text + offset) == value) {
            ++hits;
        }
    }

    return hits;
}
