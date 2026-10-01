/* mp_diag.c: the shape of every site, read once from the running process.
 *
 * The reason this is worth a file rather than a log line is that it checks an assumption the whole
 * tree rests on and nothing else tests. Two separate mechanisms here believe that a hooked site
 * begins with 0xE9: the detour chaining, which reads the displacement there and keeps the previous
 * target, and the resolver's second stage, which accepts a head that is either the authored
 * prologue or an 0xE9. A module hooking by any other shape satisfies neither, and both failures
 * are quiet. The chain would copy foreign bytes into a trampoline and call them a prologue; the
 * resolver would refuse a site it could otherwise find.
 *
 * The check has to happen with everything loaded, which is why it runs from the first frame message
 * rather than at install time: at install time half the mods that matter have not been loaded yet,
 * and the answer would be about a process that no longer exists by the time anybody reads it. The
 * loader walks the mods directory case insensitively, so every mod whose name sorts after this
 * one's is loaded after it; message 0x0C is the first message of a drawn frame, and by then
 * everything is in.
 *
 * With every mod loaded, the first run came back 31 untouched, 3 branched, 0 foreign and 0
 * unreadable. The three branches were the mover tick at 00409170 and the draw-all at 00411028,
 * which other mods detour and which load before this one, and the bootstrap at 0043E613, which
 * is this feature's own; the entry point at 0049CB80 read 55 8B EC 6A FF 68, restored. Zero
 * foreign is the answer this file was written to get. A site another mod carries a pattern for
 * and registers as a detour target was NOT branched, so the set of already hooked sites is a
 * property of the configuration, not of the tree.
 *
 * It also reads the host's entry point, which is a different question with the same shape. The
 * loader writes a branch there and the stub puts the original bytes back before the engine starts,
 * so by now it must NOT be a branch. If it still is, either the restore did not happen or something
 * else has hooked the entry point since, and both are worth a line.
 */
#include "mp_diag.h"

#include "mp_signatures.h"
#include "mp_signatures_dialog.h"

#include "common/host_image.h"
#include "common/logging.h"
#include "common/memory.h"

#include <windows.h>

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

/* Eight bytes hold a jmp rel32 and the shortest prologue in the table, and a hot patch pair would
 * show inside them. The window's known limit: a scheme that leaves the first eight bytes alone
 * and edits further in reads as authored here. */
#define SHAPE_BYTES        8u
#define JMP_REL32_OPCODE   0xE9u
#define JMP_REL32_LENGTH   5u

/* Where the entry point sits in a mapped PE32, counted from the image base: e_lfanew at 0x3C of
 * the DOS header names the NT headers, and AddressOfEntryPoint sits 0x28 into those. Read from
 * the headers rather than by a signature, because the question is about whatever the loader
 * left there, not about authored bytes. */
#define DOS_LFANEW_OFFSET  0x3Cu
#define NT_ENTRY_OFFSET    0x28u

typedef struct mp_diag_state {
    bool     reported;
    uint32_t counts[MP_SHAPE_FOREIGN + 1];
} mp_diag_state_t;

static mp_diag_state_t diag;

bool mp_diag_has_reported(void) { return diag.reported; }

uint32_t mp_diag_shape_count(mp_site_shape_t shape)
{
    if ((size_t)shape > (size_t)MP_SHAPE_FOREIGN) {
        return 0u;
    }
    return diag.counts[shape];
}

/* The site's own pattern, compared against what is there now, honouring the wildcards. Only the
 * first eight bytes: past that a site may legitimately have been edited by an operand repoint, and
 * this question is about the head. */
static bool head_is_authored(const signature_t *site, const uint8_t *live)
{
    size_t index;
    size_t count = (site->size < SHAPE_BYTES) ? site->size : SHAPE_BYTES;

    for (index = 0; index < count; ++index) {
        if (site->mask != NULL && site->mask[index] == 0u) {
            continue;
        }
        if (live[index] != site->bytes[index]) {
            return false;
        }
    }
    return true;
}

static mp_site_shape_t classify(const signature_t *site, uintptr_t address, uint8_t *live)
{
    if (!memory_try_readable(address, SHAPE_BYTES) ||
        !memory_read(address, live, SHAPE_BYTES)) {
        return MP_SHAPE_UNREADABLE;
    }
    if (live[0] == JMP_REL32_OPCODE) {
        return MP_SHAPE_JUMP;
    }
    if (head_is_authored(site, live)) {
        return MP_SHAPE_AUTHORED;
    }
    return MP_SHAPE_FOREIGN;
}

/* Which module an address belongs to, as the allocation base of its mapping. A branch out of the
 * host image is the ordinary hooked case, and the reader's first question is always whether it went
 * to somebody else or to us: our own detour on the bootstrap looks exactly like a foreign one from
 * the bytes alone.
 *
 * The first version said only "in another module" for every branch that left the host image, and
 * the first run then reported this feature's own bootstrap detour that way: true in the sense the
 * test used, misleading in the sense a reader takes. Comparing allocation bases replaced it, and
 * the run after named the bootstrap as ours and the other two branches as another module's. */
static bool target_is_this_module(uintptr_t target)
{
    MEMORY_BASIC_INFORMATION theirs;
    MEMORY_BASIC_INFORMATION ours;

    if (VirtualQuery((LPCVOID)target, &theirs, sizeof theirs) != sizeof theirs) {
        return false;
    }
    if (VirtualQuery((LPCVOID)&target_is_this_module, &ours, sizeof ours) != sizeof ours) {
        return false;
    }
    return theirs.AllocationBase == ours.AllocationBase;
}

static const char *where_it_went(uintptr_t target)
{
    if (memory_is_inside_image(target, 1)) {
        return " inside the game";
    }
    return target_is_this_module(target) ? " into this mod, which is ours"
                                         : " into another module";
}

static void format_bytes(const uint8_t *bytes, char *out, size_t out_size)
{
    static const char digits[] = "0123456789ABCDEF";
    size_t            index;
    size_t            at = 0;

    for (index = 0; index < SHAPE_BYTES && at + 3u < out_size; ++index) {
        out[at++] = digits[(bytes[index] >> 4) & 0x0F];
        out[at++] = digits[bytes[index] & 0x0F];
        out[at++] = ' ';
    }
    out[(at > 0) ? at - 1 : 0] = '\0';
}

/* Where an 0xE9 at `address` lands, and whether that is still inside the game. A branch into
 * another module is the ordinary case here: it means a mod hooked the site before we looked. */
static uintptr_t jump_target(uintptr_t address, const uint8_t *live)
{
    int32_t displacement;

    displacement = (int32_t)((uint32_t)live[1] | ((uint32_t)live[2] << 8) |
                             ((uint32_t)live[3] << 16) | ((uint32_t)live[4] << 24));
    return address + JMP_REL32_LENGTH + (uintptr_t)displacement;
}

static void report_entry_point(void)
{
    uintptr_t base = host_image_base();
    uint32_t  lfanew = 0;
    uint32_t  entry_rva = 0;
    uintptr_t entry;
    uint8_t   live[SHAPE_BYTES];
    char      text[SHAPE_BYTES * 3];

    if (base == 0 ||
        !memory_read_u32(base + DOS_LFANEW_OFFSET, &lfanew) ||
        !memory_read_u32(base + lfanew + NT_ENTRY_OFFSET, &entry_rva) ||
        entry_rva == 0) {
        log_warning("  the host entry point could not be located");
        return;
    }

    entry = base + entry_rva;
    if (!memory_try_readable(entry, SHAPE_BYTES) || !memory_read(entry, live, SHAPE_BYTES)) {
        log_warning("  the host entry point at %08X is not readable", (unsigned)entry);
        return;
    }

    format_bytes(live, text, sizeof text);
    if (live[0] == JMP_REL32_OPCODE) {
        log_error("  entry point   %08X  %s  STILL A BRANCH to %08X. The loader writes one here "
                  "and its stub is supposed to put the original bytes back before the engine "
                  "starts.",
                  (unsigned)entry, text, (unsigned)jump_target(entry, live));
    } else {
        log_info("  entry point   %08X  %s  restored, as it should be", (unsigned)entry, text);
    }
}

/* One table's worth of shapes. Split out of the walk below when the conversation became a second
 * table: the sites that matter most for this report are the ones ANOTHER mod also wants, and the
 * conversation's speak entry is the clearest example in the tree, `diagnostics` hulls it too when
 * it is switched on, so this is where a run says whether the two hooks are chained or whether one
 * of them quietly owns it. */
static void report_one_table(const signature_t *sites, size_t count)
{
    size_t index;

    for (index = 0; index < count; ++index) {
        const signature_t *site = &sites[index];
        uint8_t            live[SHAPE_BYTES];
        char               text[SHAPE_BYTES * 3];
        mp_site_shape_t    shape;

        if (site->address == 0u) {
            continue;   /* the resolver already said so, and said why */
        }

        shape = classify(site, site->address, live);
        ++diag.counts[shape];

        switch (shape) {
        case MP_SHAPE_UNREADABLE:
            log_error("  %-22s %08X  not readable", site->name, (unsigned)site->address);
            break;
        case MP_SHAPE_JUMP:
            format_bytes(live, text, sizeof text);
            log_info("  %-22s %08X  %s  branch to %08X%s", site->name, (unsigned)site->address,
                     text, (unsigned)jump_target(site->address, live),
                     where_it_went(jump_target(site->address, live)));
            break;
        case MP_SHAPE_AUTHORED:
            break;      /* the ordinary case, and there are two dozen of them */
        case MP_SHAPE_FOREIGN:
        default:
            format_bytes(live, text, sizeof text);
            log_error("  %-22s %08X  %s  NEITHER the authored bytes NOR a branch. Something has "
                      "hooked this site by a shape the detour chaining does not understand.",
                      site->name, (unsigned)site->address, text);
            break;
        }
    }
}

void mp_diag_report_hook_shapes(void)
{
    size_t             count = 0;
    const signature_t *sites = mp_signatures_sites(&count);

    if (diag.reported) {
        return;
    }
    diag.reported = true;

    log_info("hook shapes at the first drawn frame, with every mod loaded:");

    report_one_table(sites, count);
    /* And the conversation's own table, which is where a second mod's hook shows up. */
    sites = mp_signatures_dialog_sites(&count);
    report_one_table(sites, count);

    report_entry_point();

    log_info("  %u untouched, %u branched, %u foreign, %u unreadable",
             (unsigned)diag.counts[MP_SHAPE_AUTHORED], (unsigned)diag.counts[MP_SHAPE_JUMP],
             (unsigned)diag.counts[MP_SHAPE_FOREIGN], (unsigned)diag.counts[MP_SHAPE_UNREADABLE]);

    if (diag.counts[MP_SHAPE_FOREIGN] != 0 || diag.counts[MP_SHAPE_UNREADABLE] != 0) {
        log_error("  the detour chaining in this tree understands one hooking shape and there is "
                  "something here it does not");
    }
}
