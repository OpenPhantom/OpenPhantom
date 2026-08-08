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
