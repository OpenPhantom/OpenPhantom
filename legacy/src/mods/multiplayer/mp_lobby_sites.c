/* mp_lobby_sites.c: the patterns, and the reason each is cut where it is cut. See the header.
 *
 * Generated against retail WMAIN.EXE and checked for uniqueness before it was written; every
 * pattern below matches exactly once in the image. Masked bytes are call displacements and the
 * address operands the cells are read out of, a distance is not a fact about this build, and an
 * address written into a pattern is the thing the pattern exists to avoid.
 */
#include "mp_lobby_sites.h"

#include <stdint.h>

/* One round of the campaign loop, from the store that clears the restore flag, over the two moves
 * that put the start level into the current level, to the call that shows the title menu.
 * Everything a lobby needs to choose WHICH level the next round loads is named here: the start
 * index the round copies, and the index it copies into.
 *
 * It ends on the call to the title menu because that is what makes the pattern unmistakable, the
 * two moves on their own are an idiom, the pair followed by a broadcast and this call is one place
 * in the image. The two call displacements are masked; a displacement is a distance, and a
 * detour installed by anything else would change it.
 *
 * It BEGINS on `mov dword [g_restorePending], 0` because the cell it clears is what lets a session
 * end itself: writing
 * 1 there and 3 into the game mode is how the engine leaves a level for the title screen, and it
 * is the pair the engine's own "leave level" menu choice writes. Ten bytes were added in front on
 * 2026-09-07 and every offset below moved by ten with them; the immediate zero is kept unmasked
 * because it is what says this is the clearing store at the top of the round and not one of the
 * other writes to the same cell. In the retail image the pattern begins at 0043EBD6 and matches
 * once; the restore flag's operand sits at +0x02, the start level's at +0x0B and the level
 * index's at +0x10. The `push 0x17; push 0` pair at +0x14 reads backwards as 0x006A176A, which
 * is inside the image range, so the signature test allowlists that offset: the guard was right
 * to ask, and the bytes are two whole instructions, not an operand. */
const uint8_t SIG_MP_CAMPAIGN_ROUND[50] = {
    0xC7, 0x05, 0x40, 0x13, 0x88, 0x00, 0x00, 0x00, 0x00, 0x00,
    0xA1, 0xDC, 0xCF, 0x6C, 0x00, 0xA3, 0x6C, 0x13, 0x88, 0x00, 0x6A, 0x17,
    0x6A, 0x00, 0xE8, 0xD0, 0x07, 0x03, 0x00, 0x83, 0xC4, 0x08, 0xC7, 0x05,
    0xD8, 0xCF, 0x6C, 0x00, 0x01, 0x00, 0x00, 0x00, 0xE8, 0x10, 0x17, 0x00,
    0x00, 0x89, 0x45, 0xFC
};
const uint8_t MSK_MP_CAMPAIGN_ROUND[50] = {
    0xFF, 0xFF, 0x00, 0x00, 0x00, 0x00, 0xFF, 0xFF, 0xFF, 0xFF,
    0xFF, 0x00, 0x00, 0x00, 0x00, 0xFF, 0x00, 0x00, 0x00, 0x00, 0xFF, 0xFF,
    0xFF, 0xFF, 0xFF, 0x00, 0x00, 0x00, 0x00, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF,
    0x00, 0x00, 0x00, 0x00, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0x00, 0x00, 0x00,
    0x00, 0xFF, 0xFF, 0xFF
};

/* The branch that loads a level, and the richest single site in this feature: the flag that says
 * "the name is a path, take it verbatim", the level index it sets to -1 for that case, the buffer
 * the path is read out of, the same index again on the other arm, and the base of the game's own
 * level table with the imul that proves its stride is twelve.
 *
 * Five cells out of one run, two of them the same cell twice, so the recipe checks itself. At
 * 0043EC2A in the retail image, matching once: the load by name flag at +0x02 out of
 * `cmp dword [0x6CD008],1`, the level index at +0x0B out of `mov dword [0x88136C],-1`, the load
 * name buffer at +0x14 out of `push 0x881374`, the level index again at +0x2A out of
 * `mov edx,[0x88136C]`, and the level table at +0x33 out of `mov eax,[edx+0x4AE388]`, with the
 * `imul edx,0xC` between the last two. */
const uint8_t SIG_MP_CAMPAIGN_LOAD[56] = {
    0x83, 0x3D, 0x08, 0xD0, 0x6C, 0x00, 0x01, 0x75, 0x1F, 0xC7, 0x05, 0x6C,
    0x13, 0x88, 0x00, 0xFF, 0xFF, 0xFF, 0xFF, 0x68, 0x74, 0x13, 0x88, 0x00,
    0xE8, 0xC3, 0x0A, 0x00, 0x00, 0x83, 0xC4, 0x04, 0x89, 0x85, 0x74, 0xFF,
    0xFF, 0xFF, 0xEB, 0x58, 0x8B, 0x15, 0x6C, 0x13, 0x88, 0x00, 0x6B, 0xD2,
    0x0C, 0x8B, 0x82, 0x88, 0xE3, 0x4A, 0x00, 0x50
};
const uint8_t MSK_MP_CAMPAIGN_LOAD[56] = {
    0xFF, 0xFF, 0x00, 0x00, 0x00, 0x00, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0x00,
    0x00, 0x00, 0x00, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0x00, 0x00, 0x00, 0x00,
    0xFF, 0x00, 0x00, 0x00, 0x00, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF,
    0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0x00, 0x00, 0x00, 0x00, 0xFF, 0xFF,
    0xFF, 0xFF, 0xFF, 0x00, 0x00, 0x00, 0x00, 0xFF
};

/* Where the round hands over to the level: the fade call, then the two writes that clear the
 * front-end flag and set the game mode to RUNNING. The mode cell is the one that says whether a
 * level is on, and writing 1 into it is how the game leaves one.
 *
 * The two writes on their own match four times in the image, a pair of stores to a global is an
 * idiom, not a place, so the pattern reaches back over the fade to the push that carries its
 * constant. At 0043ED50 in the retail image, matching once; the front end flag's operand sits at
 * +0x11 and the game mode's at +0x1B.
 *
 * The constant itself is masked, and that is not tidiness. view_distance_fix writes the fade
 * duration in seconds over those four bytes whenever LevelFadeSeconds asks for anything but
 * the four the engine ships, and its default asks for 0.4. It loads after this module, so the
 * first field run of the two together resolved the site and then reported it as carrying
 * bytes that were neither the authored ones nor a branch. Nothing had been hooked; a number
 * had been changed. Load the two the other way round, which nothing here guarantees, and the
 * site would not have resolved at all and the lobby would have lost both operands with one
 * warning line. The push opcode, the mode, the call and both stores still pin it.
 *
 * It is NOT the bootstrap's fallback site on the entry of the same function at 0043EB2A,
 * 0x226 bytes earlier: the first cut of this pattern reused that site's enum entry and
 * silently hung the game mode cell on the wrong site, which is why the two carry different
 * names. */
const uint8_t SIG_MP_LEVEL_HANDOVER[35] = {
    0x68, 0x00, 0x00, 0x80, 0x40, 0x6A, 0x01, 0xE8, 0x74, 0xA6, 0xFF, 0xFF,
    0x83, 0xC4, 0x18, 0xC7, 0x05, 0xD8, 0xCF, 0x6C, 0x00, 0x00, 0x00, 0x00,
    0x00, 0xC7, 0x05, 0x68, 0x13, 0x88, 0x00, 0x02, 0x00, 0x00, 0x00
};
const uint8_t MSK_MP_LEVEL_HANDOVER[35] = {
    0xFF, 0x00, 0x00, 0x00, 0x00, 0xFF, 0xFF, 0xFF, 0x00, 0x00, 0x00, 0x00,
    0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0x00, 0x00, 0x00, 0x00, 0xFF, 0xFF, 0xFF,
    0xFF, 0xFF, 0xFF, 0x00, 0x00, 0x00, 0x00, 0xFF, 0xFF, 0xFF, 0xFF
};

/* The prologue of the path builder, whose first move loads the data root: the string the game
 * reads out of the registry ("CD Path") with "\gamedata\" appended. Every relative level path is
 * resolved against it, so a lobby that sends "level\swamp.b3d" over the wire lets each machine
 * prefix its own installation. At 0043F93C in the retail image, matching once, with the data
 * root's operand at +0x07 out of `mov edi,0x881480`. */
const uint8_t SIG_MP_PATH_PREFIX[11] = {
    0x55, 0x8B, 0xEC, 0x53, 0x56, 0x57, 0xBF, 0x80, 0x14, 0x88, 0x00
};
const uint8_t MSK_MP_PATH_PREFIX[11] = {
    0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0x00, 0x00, 0x00, 0x00
};

/* The hero swap, the same call the shipped cheats "iamobi" and "iamquigon" make. It
 * takes an index into the hero table and no more: a client applies its choice with it once the
 * level is up. A co-op host never calls it, because the level prescribes the host's hero; a
 * deathmatch host picks a hero in the lobby and applies it the same way. At
 * 004302AA in the retail image, matching once. The index is nowhere clamped inside it, so the
 * caller is the only bound it gets. */
const uint8_t SIG_MP_HERO_SWAP[24] = {
    0x55, 0x8B, 0xEC, 0x83, 0xEC, 0x10, 0xE8, 0x63, 0x7A, 0x01, 0x00, 0x89,
    0x45, 0xFC, 0x83, 0x7D, 0xFC, 0x00, 0x74, 0x2E, 0x8B, 0x45, 0xFC, 0x83
};
const uint8_t MSK_MP_HERO_SWAP[24] = {
    0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0x00, 0x00, 0x00, 0x00, 0xFF,
    0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF
};

/* Restores a savegame by file name. The shipped load screen reaches it through a pair that asks
 * which row is selected; a lobby that already knows the name calls it directly. At 0045158F in
 * the retail image, matching once.
 *
 * It carries a prologue like every other head here, but on this one stage two cannot help.
 * What is left after the prologue is `push imm32; call rel32; add esp,4; push imm32` with
 * every operand masked, which is an idiom rather than a place: it matches 31 times in the
 * retail image, and the tail search refuses anything over eight candidates. If a module is
 * ever seen branching over this head, the pattern has to grow past the second push first. */
const uint8_t SIG_MP_SAVE_LOAD_NAMED[24] = {
    0x55, 0x8B, 0xEC, 0x83, 0xEC, 0x30, 0x68, 0x00, 0x15, 0x88, 0x00, 0xE8,
    0x05, 0x15, 0x02, 0x00, 0x83, 0xC4, 0x04, 0x68, 0x30, 0xA0, 0x4A, 0x00
};
const uint8_t MSK_MP_SAVE_LOAD_NAMED[24] = {
    0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0x00, 0x00, 0x00, 0x00, 0xFF,
    0x00, 0x00, 0x00, 0x00, 0xFF, 0xFF, 0xFF, 0xFF, 0x00, 0x00, 0x00, 0x00
};

/* The shipped load-game screen, as a DETOUR target rather than a call: when the lobby has already
 * chosen a savegame, the detour restores it and answers 0, which is what the title menu reads as
 * "a game was loaded", so the campaign round skips its own level load and runs what the save
 * restored. Without the detour the shipped screen would appear on top of the lobby. At 00440826
 * in the retail image, matching once; the prologue the detour copies is the nine bytes of
 * `push ebp; mov ebp,esp; sub esp,0xDC`, the first instruction boundary past the five a jump
 * needs. */
const uint8_t SIG_MP_LOAD_GAME_SCREEN[32] = {
    0x55, 0x8B, 0xEC, 0x81, 0xEC, 0xDC, 0x00, 0x00, 0x00, 0x56, 0x57, 0xC7,
    0x45, 0xB8, 0x00, 0x00, 0x00, 0x00, 0xC7, 0x85, 0x58, 0xFF, 0xFF, 0xFF,
    0xFF, 0xFF, 0xFF, 0xFF, 0xC7, 0x85, 0x5C, 0xFF
};
const uint8_t MSK_MP_LOAD_GAME_SCREEN[32] = {
    0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF,
    0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF,
    0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF
};

