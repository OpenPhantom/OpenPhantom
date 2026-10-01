/* mp_body_asset.h: the actor asset a far body wears, and the proof that wearing it is safe.
 *
 * A far body used to be spawned as the hero the LOCAL player is, and the reason written at the
 * spawn was that this is the one template proven loaded. That reason was never about the hero; it
 * was about the proof. This module is the proof, so the reason falls.
 *
 * ============================ Why a name has to be proved first ===============================
 *
 * player_spawnHero loads the hero's actor with res_Alloc and hands the result straight to
 * bapobj_bindActor without testing it. res_Alloc answers a name it cannot find with zero and
 * touches no counter; bindActor stores the null, runs the assert service at slot +0x18 of the
 * service record, and that service shows a message box and ends the process. It has no return in
 * front of the dereference either, so even a service that came back would fault on the next line.
 *
 * An unknown asset name is therefore not a failed spawn, it is the end of the program, and the
 * name arrives from another machine. Nothing may reach the spawn that has not been asked for
 * here first.
 *
 * ============================== What the proof leaves behind ==================================
 *
 * A load that finds something raises the resource node's use count, and only res_Free lowers it.
 * The reference this module takes is handed back after the bind has run, never before: at that
 * point the spawn has taken its own, so the count cannot pass through zero, which is where the
 * resource layer's sweep is allowed to free the node under a body that is playing it.
 *
 * A load that finds nothing raises no counter, so a refused name leaves no debt.
 *
 * ================================ The borrowed name slot ======================================
 *
 * The spawn reads the name out of the four entry hero table, indexed by the hero it is given, and
 * from nowhere else. So a body wears a foreign asset by pointing one entry of that table at a
 * name of ours for the length of one call and putting back whatever was there. What was there is
 * read live rather than remembered at installation, because another fix owns the same table while
 * the local player rides a character of its own, and a restore from an old snapshot would take
 * that character away from him.
 */
#ifndef MULTIPLAYER_MP_BODY_ASSET_H
#define MULTIPLAYER_MP_BODY_ASSET_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

/* How many entries the hero name table has, which is also the number of heroes the spawn knows. */
#define MP_BODY_ASSET_SLOTS 4

/* Which slot a foreign asset rides, and it is not a free choice. The spawn branches into the
 * jedi arm for slots 0 and 1, so those two run the sabre capture over a model that may carry no
 * blade. Slot 2 is worse than it looks: it carries an unarmed melee mode whose entry gate is the
 * hero index being exactly that one, and that mode asks the base channel for two clips in the
 * seventies which a small actor does not have. Slot 3 has no mode of its own and is out of reach
 * of the jedi arm, so the question disappears there instead of being survived. */
#define MP_BODY_ASSET_FOREIGN_SLOT 3

/* What asking for a name answered. Separate values rather than a bool because the caller logs
 * them differently: a name the resource layer does not know is the far side sending something
 * this installation does not have, while an asset with no clips is a file that is there and
 * cannot carry a body. */
typedef enum mp_body_asset_verdict {
    MP_BODY_ASSET_OK,
    MP_BODY_ASSET_NO_ANCHORS,   /* the site did not resolve, so nothing can be proved at all */
    MP_BODY_ASSET_NOT_FOUND,    /* res_Alloc answered zero; handing this on would end the game */
    MP_BODY_ASSET_NO_CLIPS      /* loaded, and its clip table cannot carry the spawn's own clip */
} mp_body_asset_verdict_t;

/* Resolves the hero name table, the resource loader and the release. Optional in the sense the
 * rest of the feature is: without it every far body keeps the local hero and the log says so
 * once. The release alone missing is not a refusal, only a line: the proof still works, and what
 * is lost is the ability to hand its reference back. */
bool mp_body_asset_install(void);
bool mp_body_asset_ready(void);

/* Ask the resource layer for `name` before the spawn does, and answer whether a body may be built
 * out of it. On MP_BODY_ASSET_OK `asset_out` holds what the loader returned, which is the same
 * pointer the spawn will store in the block, so the caller can measure the two against each other
 * afterwards.
 *
 * A reference is outstanding after this call on every verdict except MP_BODY_ASSET_NOT_FOUND and
 * MP_BODY_ASSET_NO_ANCHORS, and the caller hands it back with mp_body_asset_release_probe once
 * the attempt has ended, whether it ended in a spawn or in a refusal. */
mp_body_asset_verdict_t mp_body_asset_probe(const char *name, void **asset_out);
void mp_body_asset_release_probe(void);
const char *mp_body_asset_verdict_text(mp_body_asset_verdict_t verdict);

/* Point hero slot `slot` at `name` and remember what the slot held. One borrow at a time; false
 * when the slot is out of range, when a borrow is already open, or when either access refused.
 * The caller runs exactly one spawn between the two and nothing else, because between them the
 * table says something the rest of the game must not read. */
bool mp_body_asset_lend_name(int32_t slot, const char *name);
void mp_body_asset_return_name(int32_t slot);
bool mp_body_asset_borrow_open(void);

/* The name hero slot `slot` names right now, cut at its first zero byte and lower cased. This is
 * how "the far player wants the asset this slot already carries" is answered without a table of
 * shipped names that another fix could have made untrue. */
bool mp_body_asset_slot_name(int32_t slot, char *out, size_t bytes);

/* The name a loaded actor carries in its own header, by the same two rules. */
bool mp_body_asset_name_of(uint32_t actor, char *out, size_t bytes);

/* THE MEASUREMENT, and the whole path rests on it.
 *
 * That a spawn driven through a bank window reads the borrowed name at all is a conclusion drawn
 * from two mechanisms that were each proved on their own: the spawn reads the name through the
 * player pointer, and inside a window that pointer names the bank. Nobody has ever run a spawn
 * with a foreign name in the slot, so the conclusion has never been observed. This compares what
 * the bank's block ended up holding against what the proof handed back, and says so once when
 * they differ. Without it the path is an inference wearing the clothes of a fact. */
void mp_body_asset_note_bound(size_t index, uint32_t bound_actor, const void *probed);

void mp_body_asset_report(void);

#endif /* MULTIPLAYER_MP_BODY_ASSET_H */
