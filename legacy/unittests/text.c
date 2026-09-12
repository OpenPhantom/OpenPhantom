/* text.c: the bounded formatter every label, path and log line goes through. */
#include "unittest.h"

#include "common/text.h"

#include <string.h>

int main(void)
{
    char   small[8];
    char   exact[6];
    char   untouched[4] = { 'a', 'b', 'c', 'd' };
    size_t stored;

    ut_section("a value that fits");
    stored = text_format(small, sizeof small, "%d", 42);
    ut_check(stored == 2, "the count is the characters stored, not the buffer size");
    ut_check(strcmp(small, "42") == 0, "and the text is what was asked for");

    ut_section("a value that does not fit");
    memset(small, 'x', sizeof small);
    stored = text_format(small, sizeof small, "%s", "twelve chars");
    ut_check(small[sizeof small - 1] == '\0', "the last byte is a terminator whatever was there");
    ut_check(stored == sizeof small - 1, "the count is the full buffer less the terminator");
    ut_check(strcmp(small, "twelve ") == 0, "and the head of the text survives");

    ut_section("a value that fills the buffer exactly");
    stored = text_format(exact, sizeof exact, "%s", "12345");
    ut_check(stored == 5 && exact[5] == '\0', "five characters and a terminator in six bytes");

    ut_section("nothing to write into");
    stored = text_format(untouched, 0, "%s", "anything");
    ut_check(stored == 0, "a zero size stores nothing");
    ut_check(memcmp(untouched, "abcd", 4) == 0, "and does not terminate out of range");
    ut_check(text_format(NULL, 8, "%s", "anything") == 0, "a missing buffer is survived");

    return ut_summary("text");
}
