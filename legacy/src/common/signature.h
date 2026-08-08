/* signature.h: find engine code by what it IS, never by where it was.
 *
 * ==============================================================================================
 * The design rule of this whole project: signatures, never addresses.
 *
 * Three builds of this engine ship inside one installation, all 829,952 bytes:
 * BIN\WMAIN.EXE, wmain.exe (10,417 bytes different) and obi.exe, which is a RECOMPILE, 60.4 %
 * of its .text differs and its code section is 0x60 shorter. The frame limiter sits at 0x475B82
 * in WMAIN and at 0x475B22 in obi.exe. An address table would have written silently into a
 * different function.
 *
 * Uniqueness is a requirement, not a nicety. A pattern that matches zero times or more than once
 * disables exactly that one patch and says so in the log. It never guesses.
 *
 * Some anchors are deliberately NOT unique; three identical append blocks, for instance. For
 * those the key is the triple (address-free pattern, expected match count, address read out of
 * the operand). signature_count_matches() serves that case; a different count means a different
 * build and the patch declines.
 *
 * Where a pattern would otherwise embed an absolute address, prefer the address-free form and
 * READ the address out of the matched operand. That keeps the anchor working under forced ASLR
 * and after another patch has already edited a nearby immediate.
 */
#ifndef COMMON_SIGNATURE_H
#define COMMON_SIGNATURE_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

typedef struct signature {
    const char    *name;
    const uint8_t *bytes;
    const uint8_t *mask;     /* NULL = every byte must match; else 0xFF = must, 0x00 = wildcard */
    size_t         size;
    size_t         detour_prologue;  /* see below; 0 = this site is not a detour target */
    uintptr_t      address;  /* filled in by signature_resolve_table; 0 = unresolved */
} signature_t;

/* Convenience initialisers for a feature's own table. */
#define SIGNATURE_ENTRY(name_text, array) \
    { (name_text), (array), NULL, sizeof(array), 0, 0 }
#define SIGNATURE_ENTRY_MASKED(name_text, array, mask_array) \
    { (name_text), (array), (mask_array), sizeof(array), 0, 0 }

/* ==============================================================================================
 * A pattern for a detour target must not depend on the bytes the detour overwrites.
 *
 * This rule was learned the expensive way. render_frameEnd is wanted by four DLLs and
 * rdCamera_BuildProjection by two. The first DLL to install replaces the function's PROLOGUE with
 * `jmp rel32`, and every later DLL then scanned for a pattern that STARTS with that prologue,
 * found zero matches, and switched itself off with a "site did not resolve" warning. The chaining
 * in common/detour.c was correct and never got the chance to run: the camera compensation, the
 * animation clock, the cell watchdog and the window follow-up were all silently absent.
 *
 * `detour_prologue` is the number of leading pattern bytes the detour will overwrite. When it is
 * non-zero the resolver works in two stages:
 *
 *   1. the whole pattern, exactly one match  -> the site is untouched, use it (the normal case);
 *   2. otherwise the pattern WITHOUT its first `detour_prologue` bytes, and a candidate is only
 *      accepted when the bytes in front of it are either the expected prologue or begin with 0xE9.
 *      Exactly one candidate must survive.
 *
 * Stage 2 is deliberately not just "drop the prologue and take any hit": in the retail image the
 * bare tail of render_frameEnd matches twice, and the second match is not even an instruction
 * boundary. The prologue test is what keeps the anchor honest.
 * ============================================================================================ */
#define SIGNATURE_ENTRY_DETOUR(name_text, array, prologue) \
    { (name_text), (array), NULL, sizeof(array), (prologue), 0 }

/* The same, for a detour target whose pattern has to reach past an absolute address to be unique.
 * The plain form above passes NULL for the mask, so every placeholder byte in the pattern is
 * required to match literally, a site declared that way with wildcards in it resolves to zero
 * matches and switches itself off, and the only symptom is a warning line. */
#define SIGNATURE_ENTRY_DETOUR_MASKED(name_text, array, mask_array, prologue) \
    { (name_text), (array), (mask_array), sizeof(array), (prologue), 0 }

/* The standalone form of the same two-stage rule, for a site that is not part of a table. */
uintptr_t signature_find_detour_target(const uint8_t *bytes, const uint8_t *mask, size_t size,
                                       size_t prologue_size);

/* Resolves every entry, logging one line each. Returns how many resolved uniquely. */
size_t signature_resolve_table(signature_t *table, size_t count);

/* 0 when the pattern does not match exactly once. */
uintptr_t signature_find_unique(const uint8_t *bytes, const uint8_t *mask, size_t size);

/* Counts ALL matches; it does not stop early, because "3" must be distinguishable from "300".
 *
 * Writes up to `max_addresses` hit addresses into `addresses`, or up to SIGNATURE_MAX_REPORTED,
 * whichever is smaller. The RETURN VALUE is always the true count; only the list is bounded.
 * Every decision in this project is made on the count, so the bound has never mattered, but a
 * caller that asks for more addresses than that and then trusts the contract would read
 * uninitialised memory. Ask for no more than SIGNATURE_MAX_REPORTED. */
#define SIGNATURE_MAX_REPORTED 8u
size_t signature_count_matches(const uint8_t *bytes, const uint8_t *mask, size_t size,
                               uintptr_t *addresses, size_t max_addresses);

/* The same search over a caller-supplied buffer, which is what the one above is built on.
 *
 * It is separate so that the matcher can be checked without a game: every patch in this project
 * stands on this loop, and driving it through the host's own code section means the needle has to
 * be real machine code and the expected answers change with the build. Given a buffer, the
 * wildcard handling, the overlap counting and the "count them all rather than stop at two"
 * property are ordinary arithmetic with fixed answers.
 *
 * Writes up to `max_offsets` hit offsets into `offsets`; the RETURN VALUE keeps counting past
 * that, which is the property the callers rely on. */
size_t signature_count_in_buffer(const uint8_t *haystack, size_t haystack_size,
                                 const uint8_t *bytes, const uint8_t *mask, size_t size,
                                 size_t *offsets, size_t max_offsets);

/* How often a 32-bit word occurs in .text, at any alignment. Used as a completeness census:
 * "are all N references to this table still pointing at it, or has somebody moved some already". */
size_t signature_count_dword(uint32_t value);

#endif /* COMMON_SIGNATURE_H */
