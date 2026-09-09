#include "ini.h"

#include "host_image.h"

#include <windows.h>

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define INI_FILE_NAME "engine_fixes.ini"
#define INI_VALUE_MAX 128

static char ini_file_path[MAX_PATH];

const char *ini_path(void)
{
    if (ini_file_path[0] == '\0') {
        _snprintf(ini_file_path, sizeof(ini_file_path), "%s%s", host_directory(), INI_FILE_NAME);
        ini_file_path[sizeof(ini_file_path) - 1] = '\0';
    }
    return ini_file_path;
}

int32_t ini_read_int(const char *section, const char *key, int32_t default_value)
{
    return (int32_t)GetPrivateProfileIntA(section, key, (INT)default_value, ini_path());
}

bool ini_read_bool(const char *section, const char *key, bool default_value)
{
    return ini_read_int(section, key, default_value ? 1 : 0) != 0;
}

float ini_read_float(const char *section, const char *key, float default_value)
{
    char written_default[INI_VALUE_MAX];
    char value[INI_VALUE_MAX];

    _snprintf(written_default, sizeof(written_default), "%.6f", (double)default_value);
    written_default[sizeof(written_default) - 1] = '\0';

    GetPrivateProfileStringA(section, key, written_default, value, (DWORD)sizeof(value),
                             ini_path());
    value[sizeof(value) - 1] = '\0';

    return (float)atof(value);
}

bool ini_read_string(const char *section, const char *key, const char *default_value,
                     char *buffer, size_t buffer_size)
{
    DWORD copied;

    if (buffer == NULL || buffer_size == 0) {
        return false;
    }

    /* ABSENT AND EMPTY are different, and the platform call cannot tell them apart on its
       own. It answers with the number of characters it copied, and it copies the default when
       the key is missing, so a non-empty default always came back looking present. Every
       caller in this project happens to pass an empty default, where a count of zero means
       absent by luck; one passes a real one, and its absent branch could never run.

       So the question is asked with a default no settings file can hold, a value carrying
       control characters. Its arrival means the key was not there, and the caller's own
       default is copied in afterwards. */
    static const char ABSENT[] = "\001\002absent\002\001";

    if (buffer_size > sizeof ABSENT) {
        (void)GetPrivateProfileStringA(section, key, ABSENT, buffer, (DWORD)buffer_size,
                                       ini_path());
        buffer[buffer_size - 1] = '\0';

        if (strcmp(buffer, ABSENT) == 0) {
            strncpy(buffer, (default_value != NULL) ? default_value : "", buffer_size - 1);
            buffer[buffer_size - 1] = '\0';
            return false;
        }
        return true;
    }

    /* Too small to hold the sentinel, so the question cannot be put that way. The count is the
       only signal left, and it is right whenever the default is empty. */
    copied = GetPrivateProfileStringA(section, key, (default_value != NULL) ? default_value : "",
                                      buffer, (DWORD)buffer_size, ini_path());
    buffer[buffer_size - 1] = '\0';

    return copied != 0;
}

bool ini_write_float(const char *section, const char *key, float value, int decimal_places)
{
    char format[16];
    char text[INI_VALUE_MAX];

    if (decimal_places < 0) {
        decimal_places = 0;
    }
    if (decimal_places > 6) {
        decimal_places = 6;
    }

    _snprintf(format, sizeof(format), "%%.%df", decimal_places);
    format[sizeof(format) - 1] = '\0';

    _snprintf(text, sizeof(text), format, (double)value);
    text[sizeof(text) - 1] = '\0';

    return WritePrivateProfileStringA(section, key, text, ini_path()) != 0;
}

bool ini_write_int(const char *section, const char *key, int32_t value)
{
    char text[INI_VALUE_MAX];

    _snprintf(text, sizeof(text), "%d", (int)value);
    text[sizeof(text) - 1] = '\0';

    return WritePrivateProfileStringA(section, key, text, ini_path()) != 0;
}

uint64_t ini_generation(void)
{
    WIN32_FILE_ATTRIBUTE_DATA attributes;

    if (!GetFileAttributesExA(ini_path(), GetFileExInfoStandard, &attributes)) {
        return 0u;                             /* unreadable reads as unchanged; see the header */
    }
    /* The two halves are one FILETIME, which is a 64-bit count of 100 nanosecond ticks. Joined
     * here rather than compared as a structure so a caller can hold it in one variable and compare
     * it with one test. */
    return ((uint64_t)attributes.ftLastWriteTime.dwHighDateTime << 32) |
           (uint64_t)attributes.ftLastWriteTime.dwLowDateTime;
}
