/* What a savegame says about itself. The parser is pure, so the test writes the header bytes
 * itself and never needs a save on disk, which also means it pins the OFFSETS, which is the part
 * that was derived from evidence rather than from a document.
 */
#include "unittest.h"

#include "mp_saves.h"

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <string.h>

static void build_header(uint8_t *header, uint8_t slot, uint8_t level, const char *name)
{
    memset(header, 0, MP_SAVES_HEADER_BYTES);
    memcpy(header, MP_SAVES_MAGIC, MP_SAVES_MAGIC_BYTES);
    header[MP_SAVES_OFF_SLOT]  = slot;
    header[MP_SAVES_OFF_LEVEL] = level;
    if (name != NULL) {
        size_t length = strlen(name);

        if (length > MP_SAVES_HEADER_BYTES - MP_SAVES_OFF_NAME - 1u) {
            length = MP_SAVES_HEADER_BYTES - MP_SAVES_OFF_NAME - 1u;
        }
        memcpy(header + MP_SAVES_OFF_NAME, name, length);
    }
}

/* Whether the bytes are a save of a level of the table, the one answer a list of saves wants. */
static bool parses(const uint8_t *bytes, size_t length, mp_save_t *out)
{
    return mp_saves_judge_header(bytes, length, out) == MP_SAVES_LOOK_SAVE;
}

static void check_the_three_fields(void)
{
    uint8_t   header[MP_SAVES_HEADER_BYTES];
    mp_save_t save;

    ut_section("the three fields a lobby needs");
    build_header(header, 3u, 3u, "LEVEL4 - GARDEN");
    ut_check(parses(header, sizeof header, &save), "a shipped snapshot parses");
    ut_check(save.slot == 3u, "the byte in front of the index is kept, for a name");
    ut_check(save.level_index == 3u, "and the level index is the field a lobby needs");
    ut_check(strcmp(save.name, "LEVEL4 - GARDEN") == 0, "the name is read as plain text");

    ut_section("the two are different fields, which one file alone could not show");
    build_header(header, 18u, 2u, "Otoh Gunga:18");
    ut_check(parses(header, sizeof header, &save) && save.slot == 18u &&
                 save.level_index == 2u,
             "a player's own save reads another number there than its level");

    ut_section("a name the font cannot draw loses the bytes it cannot draw");
    build_header(header, 4u, 1u, "Der Sumpf");
    header[MP_SAVES_OFF_NAME + 3u] = 0x81u;
    ut_check(parses(header, sizeof header, &save) &&
                 strcmp(save.name, "DerSumpf") == 0,
             "and the rest of the name survives");

    ut_section("a save with no name is still a save");
    build_header(header, 7u, 5u, NULL);
    ut_check(parses(header, sizeof header, &save) && save.name[0] != '\0',
             "it is given one, so a list never draws an empty row");
}

static void check_what_is_not_a_save(void)
{
    uint8_t   header[MP_SAVES_HEADER_BYTES];
    mp_save_t save;

    ut_section("what the parser refuses");
    build_header(header, 1u, 1u, "x");
    ut_check(!parses(header, MP_SAVES_HEADER_BYTES - 1u, &save),
             "a file too short to hold the header");
    ut_check(!parses(NULL, sizeof header, &save), "nothing");
    ut_check(!parses(header, sizeof header, NULL), "nowhere to put the answer");

    build_header(header, 1u, 1u, "x");
    header[0] = 'X';
    ut_check(!parses(header, sizeof header, &save),
             "a file that does not open with the magic");

    build_header(header, 1u, 11u, "x");
    ut_check(!parses(header, sizeof header, &save),
             "a level index past the eleven the table holds: it names no level, so nobody could "
             "be told which one to load");

    build_header(header, 1u, 1u, "x");
    header[MP_SAVES_OFF_LEVEL + 1u] = 1u;   /* 0x0101, not a byte */
    ut_check(!parses(header, sizeof header, &save),
             "a level index that does not fit a byte is a header this build does not understand");

}

/* The word in front of the level index. The engine builds the file's tag on its stack and writes
 * the name, the length and the version, so the two words behind them hold what lay there. The
 * shipped snapshots carry their slot number in the second, a save written in this installation
 * carries an address in the first and nought in the second. A parser that refused a word it did
 * not like there would refuse a savegame for the stack it was written under. */
static void check_the_word_the_engine_never_writes(void)
{
    static const uint8_t AN_ADDRESS[4] = { 0x70u, 0x5Cu, 0xE3u, 0x71u };
    uint8_t              header[MP_SAVES_HEADER_BYTES];
    mp_save_t            save;

    ut_section("the two words behind the tag are the stack's, and decide nothing");
    build_header(header, 0u, 2u, "Otoh Gunga:12");
    memcpy(header + 0x10u, AN_ADDRESS, sizeof AN_ADDRESS);
    ut_check(mp_saves_judge_header(header, sizeof header, &save) == MP_SAVES_LOOK_SAVE &&
                 save.level_index == 2u,
             "an address in the first and nought in the second, as slot twelve was saved: a "
             "save of the level its index names");
    memcpy(header + MP_SAVES_OFF_SLOT, AN_ADDRESS, sizeof AN_ADDRESS);
    ut_check(mp_saves_judge_header(header, sizeof header, &save) == MP_SAVES_LOOK_SAVE &&
                 save.level_index == 2u && strcmp(save.name, "Otoh Gunga:12") == 0,
             "an address in the second as well: still that save, with its level and its name");
    memset(header + 0x10u, 0xFF, 8u);
    ut_check(mp_saves_judge_header(header, sizeof header, &save) == MP_SAVES_LOOK_SAVE,
             "and every bit of both set");
}

/* The same bytes, asked for the reason. A lobby sent a savegame used to hear one "no" for a file
 * that did not open and for a save of a level outside the table, and began the level fresh on
 * either: a client whose file another process still held began the host's level from its start
 * while everybody else restored it. */
static void check_the_reason_a_file_is_no_choice(void)
{
    uint8_t   header[MP_SAVES_HEADER_BYTES];
    mp_save_t save;

    ut_section("a save of a level of the table is a save, with its fields");
    build_header(header, 12u, 2u, "Otoh Gunga");
    memset(&save, 0, sizeof save);
    ut_check(mp_saves_judge_header(header, sizeof header, &save) == MP_SAVES_LOOK_SAVE &&
                 save.slot == 12u && save.level_index == 2u,
             "the judgement and the parser read the same three fields");

    ut_section("a save of a level loaded by its path is a save that names no level");
    build_header(header, 12u, 0u, "x");
    memset(header + MP_SAVES_OFF_LEVEL, 0xFF, 4u);   /* no row's index, as the engine saves it */
    ut_check(mp_saves_judge_header(header, sizeof header, &save) == MP_SAVES_LOOK_NO_LEVEL,
             "minus one in the index: the file is a savegame, and its level is not in the table");
    build_header(header, 1u, 11u, "x");
    ut_check(mp_saves_judge_header(header, sizeof header, &save) == MP_SAVES_LOOK_NO_LEVEL,
             "and so is an index past the eleven rows");

    ut_section("what is no savegame at all is said apart from that");
    build_header(header, 1u, 1u, "x");
    header[0] = 'X';
    ut_check(mp_saves_judge_header(header, sizeof header, &save) == MP_SAVES_LOOK_NOT_A_SAVE,
             "another magic");
    build_header(header, 1u, 1u, "x");
    ut_check(mp_saves_judge_header(header, MP_SAVES_HEADER_BYTES - 1u, &save) ==
                 MP_SAVES_LOOK_NOT_A_SAVE,
             "fewer bytes than a header");
    ut_check(mp_saves_judge_header(NULL, sizeof header, &save) == MP_SAVES_LOOK_NOT_A_SAVE,
             "no bytes");
    ut_check(mp_saves_judge_header(header, sizeof header, NULL) == MP_SAVES_LOOK_NOT_A_SAVE,
             "and nowhere to put the answer");

    ut_section("the magic is asked before the level, so rubbish is never a level to begin fresh");
    memset(header, 0xFF, sizeof header);
    ut_check(mp_saves_judge_header(header, sizeof header, &save) == MP_SAVES_LOOK_NOT_A_SAVE,
             "a block of set bits reads minus one where the index is, and is still no savegame");

    ut_section("a file that does not open is neither: it says nothing about the level");
    ut_check(mp_saves_look("save\\NO_SUCH_FILE_OF_THE_TEST.SAV", &save) == MP_SAVES_LOOK_UNREADABLE,
             "a name with no file behind it is unreadable, not a level to begin fresh");
    ut_check(mp_saves_look(NULL, &save) == MP_SAVES_LOOK_UNREADABLE, "and so is no name");
    ut_check(mp_saves_look("save\\NO_SUCH_FILE_OF_THE_TEST.SAV", NULL) ==
                 MP_SAVES_LOOK_UNREADABLE,
             "or nowhere to put the answer");
}

static void check_the_scan_without_saves(void)
{
    ut_section("a folder with no saves in it");
    (void)mp_saves_scan();   /* the test process has no save folder; it must not mind */
    ut_check(mp_saves_at(mp_saves_count()) == NULL, "one past the end answers nothing");
    ut_check(mp_saves_find("save\\Zanzi03.sav") == NULL || mp_saves_count() != 0u,
             "and a name is only found when something was found");
    ut_check(mp_saves_find(NULL) == NULL, "nothing is never found");
}

int main(void)
{
    check_the_three_fields();
    check_what_is_not_a_save();
    check_the_word_the_engine_never_writes();
    check_the_reason_a_file_is_no_choice();
    check_the_scan_without_saves();
    return ut_summary("mp_saves");
}
