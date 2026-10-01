/* mp_bank.h: the far player banks, and the swap that makes the engine tick one of them.
 *
 * The retail engine addresses its player through one pointer, `pr`, 1668 times across 110
 * functions, and that pointer has no writer anywhere in the shipped code: it is initialised in the
 * image and never stored to again. That is the whole opportunity. Whoever writes it first owns it,
 * and pointing it at another block makes every one of those 110 functions operate on another
 * player without a single patched code site.
 *
 * A bank is a private copy of the hero block, the active status record and the hold accumulator.
 * Bank 0 is the engine's own player; banks 1 to MP_BANK_FAR_MAX are the far players' bodies on
 * this machine, and at most one of them is ACTIVE at a time: swapped in through the two pointers,
 * or ticked through a content swap of the block at its absolute address. The swap is two pointer
 * stores plus one saved accumulator, and the provocation runs a full cycle of bank 1 inside every
 * substep tick and requires the digest of bank 0 to come back bit-identical.
 *
 * The swap window is our own task tick. The task dispatcher between its call into us and our
 * return touches neither pointer cell (byte census over the whole scheduler cluster, in all three
 * shipped builds), and there are no threads, so nothing engine-side can ever observe the
 * pointers swapped.
 *
 * ================================ One bank became three ========================================
 *
 * The first form of this file held exactly one far bank, because one far player was the whole
 * design. Four players are three far bodies, so the bank is a field now, addressed by index, and
 * the names that said "second" call index 1: they stay until their last caller has learned to
 * say which bank it means, and the behaviour for two players is what it was.
 */
#ifndef MULTIPLAYER_MP_BANK_H
#define MULTIPLAYER_MP_BANK_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

/* The hero block is 235 dwords, measured from the rep stosd that clears it; the status record
 * stride is measured from the imul in the one function that computes record addresses. */
#define MP_BANK_HERO_BLOCK_BYTES 0x3ACu
#define MP_BANK_STATUS_BYTES     0x4Cu

/* How many far banks there are: four players, three of them far. Indices run 1..MP_BANK_FAR_MAX;
 * 0 is the engine's own player and never a bank of ours. */
#define MP_BANK_FAR_MAX 3u

/* The collision class of a bank's body: 1 for bank 0, the retail value, and 5, 6, 7 for banks 1,
 * 2, 3, chosen because they appear in no level's actor records: a census of the 2250 placements
 * in the eleven shipped levels finds classes 0 to 4, 8, 9 and the pickup bands from 10 up, and
 * nothing in between. */
#define MP_BANK_CLASS_FIRST 5

/* FNV-1a over raw bytes, the digest form that tells WHERE two states differ nothing, but whether
 * they differ everything. Public so the test can pin it to known answers. */
uint32_t mp_bank_fnv1a(uint32_t seed, const void *bytes, size_t size);
#define MP_BANK_FNV_SEED 2166136261u

/* The two pure rules: which indices name a far bank, and which class a bank's body carries. */
bool    mp_bank_index_ok(size_t index);
int32_t mp_bank_class_of(size_t index);

/* Resolves the four cells the bank stands on and verifies, once, that both pointer cells are
 * writable and that `pr` currently aims at the hero block the size constant belongs to. Refuses
 * with a log line otherwise; nothing later works after a refusal, and every later call says so
 * quietly instead of crashing. */
bool mp_bank_install(void);

bool mp_bank_ready(void);

/* A store into the engine's data from inside a window: the swap, the loan, the tick and the far
 * body's placement. Guarded, never through the protecting write, and counted, so the report says
 * how many landed and how many were refused. The installation alone keeps the protecting write. */
bool mp_bank_window_write(uintptr_t at, const void *data, size_t size);
bool mp_bank_window_write_u32(uintptr_t at, uint32_t value);
bool mp_bank_window_write_f32(uintptr_t at, float value);
void mp_bank_window_report(void);

/* The digest of bank 0 as the engine sees it right now: hero block, active status record, the
 * hold accumulator, and the two pointer values themselves. False when a part of that is not
 * readable, which is an answer and not an accident. */
bool mp_bank_digest(uint32_t *digest_out);

/* Point the engine at bank `index`, which is made a fresh copy of bank 0 on every swap in. The
 * caller owns the window: nothing engine-side may run between these two, which inside our task
 * tick is a property of the dispatcher rather than a promise. Refused while any bank is active. */
bool mp_bank_swap_in_at(size_t index);
bool mp_bank_swap_out(void);

/* The same pointer swap WITHOUT the fresh copy: the bank's own persistent block and status are
 * what the pointers aim at. This is the contact route's window, where the engine's own handler
 * must read and write that body's life rather than a copy of bank 0's; refreshing here would
 * overwrite that life. Ends through the ordinary swap out. */
bool mp_bank_swap_in_persistent_at(size_t index);

/* The spawn's window: the refreshing swap, plus a snapshot of what the engine's hero spawn
 * writes into cells this player owns outside the block, the six inventory and key bytes, the
 * four hero records and the current player index. The ordinary swap out gives back every one
 * of them that moved, so it must come after the spawn's last engine writer, the set_active of
 * the local hero. Refused, and counted, unless the status pointer names `local_hero`'s record
 * and the current player index names that hero, or while status_setActivePlayer cannot be
 * proven to name the cells kept. */
bool mp_bank_swap_in_for_spawn(size_t index, int32_t local_hero);

bool mp_bank_is_swapped(void);

/* The bank that is active, swapped in or ticking, or 0 when none is. */
size_t mp_bank_active(void);

/* A bank body's health, the first dword of its banked status record. Zero with no bank. */
int32_t mp_bank_health_at(size_t index);

/* Reads a range of a bank's persistent block, for the snapshot the host builds. Current whenever
 * no window is open; refused past the block's end, for an index that is no bank, or with none
 * installed. */
bool mp_bank_read_at(size_t index, size_t offset, void *out, size_t size);

/* Where bank `index`'s persistent block lives, 0 for an index that is no bank. The address holds
 * for the life of the process, which is what lets another DLL be told it once per body; what it
 * holds means something only once the bank installed. */
uintptr_t mp_bank_block_at(size_t index);

/* Writes a range of a bank's persistent block, for the one correction the spawn needs (its blade
 * vectors). Refused while the pointers are swapped to a bank, inside that bank's own window
 * (where the block sits in the engine's record and is read back over the copy), past the block's
 * end, or with no bank. */
bool mp_bank_write_at(size_t index, size_t offset, const void *data, size_t size);

/* Writes one dword of a bank's status record, for the health that arrives on the wire. Never
 * through an engine setter: those write the local player's HUD cells as well. The write lands
 * wherever the record's content is at the moment: in the bank's own copy, which is the live
 * record while the pointers are swapped to it; or in the engine's record while that bank's tick
 * has placed the bank's content there, where a write to the copy would be overwritten by the
 * read-back at the end of the tick. Refused with no bank, outside the record, off a dword
 * boundary, for another bank's tick, or during a tick that did not place the status. */
bool mp_bank_status_write_at(size_t index, size_t offset, uint32_t value);

/* The bounds half of the writer, pure: a dword offset inside the status record. */
bool mp_bank_status_offset_ok(size_t offset);

/* The collision class of the bank currently active, `mp_bank_class_of(mp_bank_active())`. This is
 * what a shot fired inside a swap window, or inside a bank's tick, must carry so the pair filter
 * lets it hit other players and rejects self fire. Bank 0 answers 1. */
int32_t mp_bank_active_class(void);

/* The block loan, for the five lifecycle functions that address the hero block absolutely. A call
 * landing there while a bank is swapped in would read half its player through the pointer and
 * half through the absolute address, two different players at once. The loan makes both views
 * agree for one call: bank 0's block steps aside, the active bank's bytes take its place at the
 * absolute address, the player pointer follows, and afterwards everything returns with the
 * callee's changes carried into the bank.
 *
 * begin answers false when no bank is swapped in, which is the ordinary case and not a failure;
 * end with that answer is a no-op. The caller brackets exactly one engine call between the two. */
bool mp_bank_lend_block_begin(void);
void mp_bank_lend_block_end(bool lent);

/* Tick a bank's persistent block through the player pipeline. The bank's block holds that body
 * after its spawn, and this installs it at the absolute hero block address for one call to the
 * phase runner, then reads the pipeline's changes back into the block and restores bank 0. No
 * player pointer write is needed: unswapped, the pointer already aims at the hero block, so
 * swapping the block's CONTENT is enough for the pipeline to tick the bank's body.
 *
 * The disassembly of every phase shows they address the player only through that pointer, so the
 * runner moves, collides and commits the body's own position. The hold accumulator, one dword
 * past the block, is banked with it: the input phase writes it, so the body keeps its own value
 * and bank 0's comes back. Refused while a bank is swapped in or another is ticking, because
 * then the hero block holds bank 0 and the content swap would install the body over the wrong
 * one; the tick and the swap provocation are mutually exclusive by configuration. run_phases is
 * the resolved phase runner, a cdecl void(void). Returns false on a memory fault; the first fault
 * is logged with the step it hit, so the log says whether bank 0 was already restored. */
bool mp_bank_run_at(size_t index, void (*run_phases)(void));

/* ================================ The names that mean bank 1 ===================================
 * Every caller that still says "second" means the first far bank. These call index 1 and change
 * nothing; they go when their last caller has learned the index. */
bool    mp_bank_swap_in(void);
bool    mp_bank_swap_in_persistent(void);
int32_t mp_bank_second_health(void);
bool    mp_bank_second_write(size_t offset, const void *data, size_t size);
bool    mp_bank_run_second(void (*run_phases)(void));

/* One full provocation cycle: digest, swap bank 1 in, verify both cells read back as ours, swap
 * out, verify both cells read back byte-identical to what was saved, digest again, compare. Wired
 * as the task's tick client while ProvokeBankSwap is on, so it runs once per substep inside a
 * level. Mismatches are counted and the first differing byte is named. */
void mp_bank_provoke_tick(void);

void mp_bank_report(const char *why);

#endif /* MULTIPLAYER_MP_BANK_H */
