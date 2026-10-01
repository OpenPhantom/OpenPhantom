/* mp_state_note_rule.h: which reliable notes describe a state, and what one says without its clock.
 *
 * Layer 1. Ten of the notes on the reliable channel are not events but a whole state, repeated:
 * the roster, the setup, the map digest, the map note, the level state, the shared story, the
 * blackboard, the host's scene, the host's whole crate note and the host's world settings. A
 * younger copy replaces an older one completely, and a lost older copy is worthless once the
 * younger arrives. That is what Tribes calls "most recent state" and Halo: Reach "state data",
 * and it is what the channel may treat
 * differently from an event: a copy not yet sent is overwritten in place, a copy in flight shrinks
 * to nothing, and an unchanged repeat is left out while one is still on its way.
 *
 * The campaign bank (0x8B) is NOT one of them, although it looks like one. It travels as a
 * difference against the sender's mirror of what it has sent, and the mirror takes a note over as
 * soon as it goes out; a note shrunk to nothing on its way would be missing on the far side for
 * good while the mirror says it is there.
 *
 * The crate note counts only when it is whole. A change note carries just the blocks that changed,
 * so it is an event under the same tag: a whole note may not overwrite it, and it may not overwrite
 * a whole note.
 *
 * The kind is the tag, and a younger copy replaces an older one of its kind. The key is what the
 * channel asks before it leaves a copy out as a repeat of one on its way: what a note says with its
 * clock left out, the sender's tick in the digest, the map note and the level state, the round
 * trips in the roster, the scene's age. Those change on every copy, so a comparison with them would
 * call every repeat a change, which is the defect the roster had once already. The whole crate
 * note keeps its tick in the key, because a change note between two equal whole notes makes the
 * second one news (mp_state_note_rule.c, is_clock). The host's world settings carry no clock at
 * all and are keyed whole.
 */
#ifndef MULTIPLAYER_MP_STATE_NOTE_RULE_H
#define MULTIPLAYER_MP_STATE_NOTE_RULE_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#define MP_STATE_NOTE_KINDS 10u

/* Whether `note` is one of the ten states; if so its kind, which is its tag, and its key. */
bool mp_state_note_classify(const uint8_t *note, size_t bytes, uint8_t *kind, uint32_t *key);

/* The kind at `index`, 0 past the tenth, for a report that walks them in a fixed order. */
uint8_t mp_state_note_kind_at(size_t index);

/* Words for the report: "the roster", "the setup" and so on; "a state" for an unknown kind. */
const char *mp_state_note_name(uint8_t kind);

#endif /* MULTIPLAYER_MP_STATE_NOTE_RULE_H */
