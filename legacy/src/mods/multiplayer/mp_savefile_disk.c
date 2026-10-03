/* mp_savefile_disk.c: the received savegame on the disk. See the header. */
#include "mp_savefile_disk.h"

#include <windows.h>

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <string.h>

/* The comparison reads the file in pieces of this size, so it needs no buffer the size of a
 * savegame. */
#define COMPARE_BLOCK_BYTES 4096u

bool mp_savefile_disk_holds(const char *path, const uint8_t *bytes, uint32_t total)
{
    uint8_t  block[COMPARE_BLOCK_BYTES];
    HANDLE   file;
    uint32_t at = 0u;
    bool     same = true;

    if (path == NULL || bytes == NULL || total == 0u) {
        return false;
    }
    /* Read sharing only: a copy of the game that is writing the file shares it with nobody, and
     * this open then fails, which is the answer "not known to be the same". */
    file = CreateFileA(path, GENERIC_READ, FILE_SHARE_READ, NULL, OPEN_EXISTING,
                       FILE_ATTRIBUTE_NORMAL, NULL);
    if (file == INVALID_HANDLE_VALUE) {
        return false;
    }
    if (GetFileSize(file, NULL) != (DWORD)total) {
        CloseHandle(file);
        return false;
    }
    while (same && at < total) {
        DWORD want = total - at < COMPARE_BLOCK_BYTES ? (DWORD)(total - at)
                                                      : (DWORD)COMPARE_BLOCK_BYTES;
        DWORD got  = 0;

        same = ReadFile(file, block, want, &got, NULL) != 0 && got == want &&
               memcmp(block, bytes + at, want) == 0;
        at += (uint32_t)want;
    }
    CloseHandle(file);
    return same;
}

bool mp_savefile_disk_write(const char *path, const uint8_t *bytes, uint32_t total,
                            uint32_t *error)
{
    HANDLE file;
    DWORD  written = 0;

    if (error != NULL) {
        *error = 0u;
    }
    if (path == NULL || bytes == NULL) {
        return false;
    }
    file = CreateFileA(path, GENERIC_WRITE, 0, NULL, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, NULL);
    if (file == INVALID_HANDLE_VALUE) {
        if (error != NULL) {
            *error = (uint32_t)GetLastError();
        }
        return false;
    }
    if (!WriteFile(file, bytes, (DWORD)total, &written, NULL) || written != (DWORD)total) {
        if (error != NULL) {
            *error = (uint32_t)GetLastError();
        }
        CloseHandle(file);
        return false;
    }
    CloseHandle(file);
    return true;
}
