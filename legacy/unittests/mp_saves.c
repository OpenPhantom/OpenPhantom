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

static void check_the_three_fields(void)
{
    uint8_t   header[MP_SAVES_HEADER_BYTES];
    mp_save_t save;

    ut_section("the three fields a lobby needs");
    build_header(header, 3u, 3u, "LEVEL4 - GARDEN");
    ut_check(mp_saves_parse_header(header, sizeof header, &save), "a shipped snapshot parses");
    ut_check(save.slot == 3u, "the slot is its own number");
    ut_check(save.level_index == 3u, "and the level index is the field beside it");
    ut_check(strcmp(save.name, "LEVEL4 - GARDEN") == 0, "the name is read as plain text");

    ut_section("the two are different fields, which one file alone could not show");
    build_header(header, 18u, 2u, "Otoh Gunga:18");
    ut_check(mp_saves_parse_header(header, sizeof header, &save) && save.slot == 18u &&
                 save.level_index == 2u,
             "a player's own save separates the slot from the level it plays in");

    ut_section("a name the font cannot draw loses the bytes it cannot draw");
    build_header(header, 4u, 1u, "Der Sumpf");
    header[MP_SAVES_OFF_NAME + 3u] = 0x81u;
    ut_check(mp_saves_parse_header(header, sizeof header, &save) &&
                 strcmp(save.name, "DerSumpf") == 0,
             "and the rest of the name survives");

    ut_section("a save with no name is still a save");
    build_header(header, 7u, 5u, NULL);
    ut_check(mp_saves_parse_header(header, sizeof header, &save) && save.name[0] != '\0',
             "it is given one, so a list never draws an empty row");
}

static void check_what_is_not_a_save(void)
{
    uint8_t   header[MP_SAVES_HEADER_BYTES];
    mp_save_t save;

    ut_section("what the parser refuses");
    build_header(header, 1u, 1u, "x");
    ut_check(!mp_saves_parse_header(header, MP_SAVES_HEADER_BYTES - 1u, &save),
             "a file too short to hold the header");
    ut_check(!mp_saves_parse_header(NULL, sizeof header, &save), "nothing");
    ut_check(!mp_saves_parse_header(header, sizeof header, NULL), "nowhere to put the answer");

    build_header(header, 1u, 1u, "x");
    header[0] = 'X';
    ut_check(!mp_saves_parse_header(header, sizeof header, &save),
             "a file that does not open with the magic");

    build_header(header, 1u, 11u, "x");
    ut_check(!mp_saves_parse_header(header, sizeof header, &save),
             "a level index past the eleven the table holds: it names no level, so nobody could "
             "be told which one to load");

    build_header(header, 1u, 1u, "x");
    header[MP_SAVES_OFF_LEVEL + 1u] = 1u;   /* 0x0101, not a byte */
    ut_check(!mp_saves_parse_header(header, sizeof header, &save),
             "a level index that does not fit a byte is a header this build does not understand");

    build_header(header, 1u, 1u, "x");
    header[MP_SAVES_OFF_SLOT + 2u] = 9u;
    ut_check(!mp_saves_parse_header(header, sizeof header, &save), "and so is such a slot");
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
    check_the_scan_without_saves();
    return ut_summary("mp_saves");
}
