/* The received savegame on the disk: whether it is there already, and putting it there.
 *
 * Against real files in the test's working folder. The case this module exists for is two copies
 * of the game in one folder that are both sent the same file: the second finds it and does not
 * write, and a file held by a neighbour that shares it with nobody is "not known to be the same"
 * to the comparison and an error with its code to the write.
 */
#include "unittest.h"

#include "mp_savefile_disk.h"

#include <windows.h>

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <string.h>

#define TEST_FILE    "mp_savefile_disk_test.sav"
#define NO_SUCH_FILE "mp_savefile_disk_test_absent.sav"
#define NO_SUCH_PATH "mp_savefile_disk_no_such_folder\\file.sav"

/* Longer than the block the comparison reads in, and not a multiple of it, so a difference in the
 * last short block is a case. */
#define FILE_BYTES 9001u

static uint8_t bytes[FILE_BYTES];
static uint8_t other[FILE_BYTES];

static void fill(void)
{
    uint32_t i;

    for (i = 0u; i < FILE_BYTES; ++i) {
        bytes[i] = (uint8_t)(i * 7u + 3u);
    }
    memcpy(other, bytes, sizeof other);
}

static void check_write_and_compare(void)
{
    uint32_t error = 77u;

    (void)DeleteFileA(TEST_FILE);

    ut_section("a file that is not there holds nothing");
    ut_check(!mp_savefile_disk_holds(TEST_FILE, bytes, FILE_BYTES), "no file, no match");

    ut_section("what was written is what the comparison finds");
    ut_check(mp_savefile_disk_write(TEST_FILE, bytes, FILE_BYTES, &error) && error == 0u,
             "the write succeeds and leaves the error at nought");
    ut_check(mp_savefile_disk_holds(TEST_FILE, bytes, FILE_BYTES),
             "the same bytes are found, so a second copy of the game need not write them again");

    ut_section("and anything else is not found");
    other[0] ^= 0x01u;
    ut_check(!mp_savefile_disk_holds(TEST_FILE, other, FILE_BYTES), "a first byte that differs");
    other[0] ^= 0x01u;
    other[FILE_BYTES - 1u] ^= 0x80u;
    ut_check(!mp_savefile_disk_holds(TEST_FILE, other, FILE_BYTES),
             "a last byte that differs, in the short block at the end");
    other[FILE_BYTES - 1u] ^= 0x80u;
    other[4096u] ^= 0x10u;
    ut_check(!mp_savefile_disk_holds(TEST_FILE, other, FILE_BYTES),
             "the first byte of the second block");
    other[4096u] ^= 0x10u;
    ut_check(mp_savefile_disk_holds(TEST_FILE, other, FILE_BYTES),
             "and with every difference taken back it is found again");
    ut_check(!mp_savefile_disk_holds(TEST_FILE, bytes, FILE_BYTES - 1u),
             "a file one byte longer than what would be written is another file");
    ut_check(!mp_savefile_disk_holds(TEST_FILE, bytes, 0u), "nothing to write is never found");
    ut_check(!mp_savefile_disk_holds(NULL, bytes, FILE_BYTES) &&
                 !mp_savefile_disk_holds(TEST_FILE, NULL, FILE_BYTES),
             "nor is no name, or no bytes");

    ut_section("a write replaces the file whole");
    ut_check(mp_savefile_disk_write(TEST_FILE, bytes, 100u, &error) && error == 0u,
             "a shorter file over a longer one");
    ut_check(mp_savefile_disk_holds(TEST_FILE, bytes, 100u) &&
                 !mp_savefile_disk_holds(TEST_FILE, bytes, FILE_BYTES),
             "leaves the shorter one and nothing of the longer");
}

static void check_a_file_somebody_holds(void)
{
    HANDLE   neighbour;
    uint32_t error = 0u;

    ut_section("a neighbour that is writing the file shares it with nobody");
    ut_check(mp_savefile_disk_write(TEST_FILE, bytes, FILE_BYTES, &error), "the file is there");
    neighbour = CreateFileA(TEST_FILE, GENERIC_WRITE, 0, NULL, OPEN_EXISTING,
                            FILE_ATTRIBUTE_NORMAL, NULL);
    ut_check(neighbour != INVALID_HANDLE_VALUE, "and a second handle holds it the way a write does");
    if (neighbour == INVALID_HANDLE_VALUE) {
        return;
    }
    ut_check(!mp_savefile_disk_holds(TEST_FILE, bytes, FILE_BYTES),
             "the comparison cannot open it, which is the answer: not known to be the same");
    ut_check(!mp_savefile_disk_write(TEST_FILE, bytes, FILE_BYTES, &error) &&
                 error == (uint32_t)ERROR_SHARING_VIOLATION,
             "and the write is refused with the sharing violation as its code");
    CloseHandle(neighbour);
    ut_check(mp_savefile_disk_holds(TEST_FILE, bytes, FILE_BYTES),
             "once the neighbour lets go, the file it held is found as it was");

    ut_section("a reader beside it does not stand in the comparison's way");
    neighbour = CreateFileA(TEST_FILE, GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_WRITE, NULL,
                            OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, NULL);
    ut_check(neighbour != INVALID_HANDLE_VALUE, "a handle opened the way the engine's restore does");
    if (neighbour == INVALID_HANDLE_VALUE) {
        return;
    }
    ut_check(mp_savefile_disk_holds(TEST_FILE, bytes, FILE_BYTES),
             "the comparison reads beside a restore");
    ut_check(!mp_savefile_disk_write(TEST_FILE, bytes, FILE_BYTES, &error) &&
                 error == (uint32_t)ERROR_SHARING_VIOLATION,
             "and the write, which shares with nobody, is refused while the restore reads: a file "
             "is never cut short under it");
    CloseHandle(neighbour);
}

static void check_what_cannot_be_written(void)
{
    uint32_t error = 0u;

    ut_section("a write that cannot begin says why");
    ut_check(!mp_savefile_disk_write(NO_SUCH_PATH, bytes, FILE_BYTES, &error) &&
                 error == (uint32_t)ERROR_PATH_NOT_FOUND,
             "a folder that does not exist is the system's own code");
    error = 77u;
    ut_check(!mp_savefile_disk_write(NULL, bytes, FILE_BYTES, &error) && error == 0u,
             "no name is refused before the system is asked, with the error at nought");
    ut_check(!mp_savefile_disk_write(TEST_FILE, NULL, FILE_BYTES, NULL), "and so are no bytes");
    ut_check(!mp_savefile_disk_holds(NO_SUCH_FILE, bytes, FILE_BYTES),
             "a name with no file behind it holds nothing");
}

int main(void)
{
    fill();
    check_write_and_compare();
    check_a_file_somebody_holds();
    check_what_cannot_be_written();
    (void)DeleteFileA(TEST_FILE);
    return ut_summary("mp_savefile_disk");
}
