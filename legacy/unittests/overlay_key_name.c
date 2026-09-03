/* overlay_key_name: the two directions between a virtual key code and a name a person can type.
 *
 * The reading direction is the one that earns a test. It parses a settings file that a player edits
 * by hand, so every shape it does not recognise has to be refused rather than turned into some
 * other key: a name that silently became the wrong code would move the panel's hotkey to a key
 * nobody pressed, and the only symptom is that the menu stops opening. That was already reported
 * once from a test build, which is why names exist at all.
 *
 * The bare number is the other reason. OpenKey has always been a virtual key code and every ini
 * written before names existed holds one, so a digit string has to keep meaning exactly what it
 * used to mean. The cases below pin that alongside the ranges.
 */
#include "overlay_key_name.h"

#include "unittest.h"

#include <windows.h>

#include <string.h>

static void test_naming_a_code(void)
{
    char text[16];

    ut_section("turning a code into something a person reads");

    overlay_key_name('A', text, sizeof text);
    ut_check(strcmp(text, "A") == 0, "a letter is its own name");
    overlay_key_name('5', text, sizeof text);
    ut_check(strcmp(text, "5") == 0, "and so is a digit from the number row");

    overlay_key_name(VK_F1, text, sizeof text);
    ut_check(strcmp(text, "F1") == 0, "the first function key");
    overlay_key_name(VK_F12, text, sizeof text);
    ut_check(strcmp(text, "F12") == 0, "and the last one a normal keyboard carries");
    overlay_key_name(VK_F24, text, sizeof text);
    ut_check(strcmp(text, "F24") == 0,
             "F24 is named too, because the reading direction accepts it and a code that can be "
             "set but not displayed reads as a broken panel");

    overlay_key_name(VK_NUMPAD0, text, sizeof text);
    ut_check(strcmp(text, "Num0") == 0, "a numpad digit is distinguished from the number row");
    overlay_key_name(VK_ADD, text, sizeof text);
    ut_check(strcmp(text, "Num+") == 0, "and so are the numpad operators");

    overlay_key_name(VK_OEM_3, text, sizeof text);
    ut_check(strcmp(text, "Backtick") == 0, "the key below Escape on a British or American layout");
    overlay_key_name(VK_ESCAPE, text, sizeof text);
    ut_check(strcmp(text, "Esc") == 0, "and a named key uses the short form the panel has room for");

    overlay_key_name(VK_CONTROL, text, sizeof text);
    ut_check(strcmp(text, "Ctrl") == 0, "and the modifiers have short names of their own");

    overlay_key_name(0xF0, text, sizeof text);
    ut_check(strcmp(text, "240") == 0,
             "a code with no name falls back to its DECIMAL value, which is the form the reading "
             "direction accepts, so what the panel shows can always be typed back into the file");

    text[0] = 'z';
    overlay_key_name('A', text, 0u);
    ut_check(text[0] == 'z', "a buffer of no length is left alone rather than written past");
}

static void test_reading_a_name(void)
{
    int32_t vk = -1;

    ut_section("turning what was typed into a code");

    ut_check(overlay_key_from_name("F6", &vk) && vk == VK_F6, "the shipped default by name");
    ut_check(overlay_key_from_name("f6", &vk) && vk == VK_F6, "case does not matter");
    ut_check(overlay_key_from_name(" Num Pad + ", &vk) && vk == VK_ADD,
             "spaces are dropped, so a name typed with them still reads");
    ut_check(overlay_key_from_name("NUM_PLUS", &vk) && vk == VK_ADD,
             "and so are underscores, which is how a player who expects a config file writes it");

    ut_check(overlay_key_from_name("F1", &vk) && vk == VK_F1, "the bottom of the function range");
    ut_check(overlay_key_from_name("F24", &vk) && vk == VK_F24, "and the top of it");
    ut_check(!overlay_key_from_name("F0", &vk), "F0 is not a key and is refused");
    ut_check(!overlay_key_from_name("F25", &vk), "one past the top is refused, not clamped");

    ut_check(overlay_key_from_name("numpad0", &vk) && vk == VK_NUMPAD0, "the numpad digits");
    ut_check(overlay_key_from_name("num9", &vk) && vk == VK_NUMPAD9, "in both spellings");
    ut_check(!overlay_key_from_name("numpad", &vk), "the prefix on its own names nothing");

    ut_check(overlay_key_from_name("Q", &vk) && vk == 'Q', "a single letter is its own code");
    ut_check(overlay_key_from_name("backtick", &vk) && vk == VK_OEM_3, "and the aliases resolve");
    ut_check(overlay_key_from_name("tilde", &vk) && vk == VK_OEM_3,
             "including the several names people give the same key");
    ut_check(overlay_key_from_name("default", &vk) && vk == 0,
             "the word for the shipped answer reads as 0, which is what the code treats as unset");
    ut_check(overlay_key_from_name("ctrl", &vk) && vk == VK_CONTROL,
             "the modifiers read back, because a name the panel can show and the file cannot take "
             "is a name that moves the hotkey to the default the next time the game starts");
}

static void test_the_number_that_was_always_allowed(void)
{
    int32_t vk = -1;

    ut_section("the bare virtual key code every older ini holds");

    ut_check(overlay_key_from_name("117", &vk) && vk == VK_F6,
             "a decimal code still means what it meant before names existed");
    ut_check(overlay_key_from_name("0", &vk) && vk == 0, "zero is a value, not a failure");
    ut_check(overlay_key_from_name("255", &vk) && vk == 255, "the largest code there is");
    ut_check(!overlay_key_from_name("256", &vk),
             "one past it is refused rather than truncated into a key that does exist");
    ut_check(!overlay_key_from_name("12a", &vk),
             "a number with rubbish after it is refused, because guessing which half was meant is "
             "how a hotkey silently moves");

    /* The one place the two directions disagree, pinned so that a later change to either has to
     * come past this and decide on purpose. */
    ut_check(overlay_key_from_name("5", &vk) && vk == 5,
             "a single digit is the virtual key code 5, NOT the 5 key on the number row, because "
             "an older ini holding a bare code has to keep meaning what it meant");
    ut_check(overlay_key_from_name("53", &vk) && vk == '5',
             "the 5 key is reached by its code, 53, which is what the panel cannot show without "
             "giving up the obvious label");
    ut_check(overlay_key_from_name("8", &vk) && vk == VK_BACK,
             "and 8 stays Backspace rather than becoming the 8 key, which is the compatibility the "
             "number row is sacrificed for");
}

static void test_what_must_be_refused(void)
{
    int32_t vk = -1;

    ut_section("text that names no key");

    ut_check(!overlay_key_from_name("", &vk), "an empty setting is not a key");
    ut_check(!overlay_key_from_name("   ", &vk),
             "and neither is one that is empty once the spaces come out");
    ut_check(!overlay_key_from_name("banana", &vk), "an unknown word is refused");
    ut_check(!overlay_key_from_name(NULL, &vk), "a missing string is refused rather than read");
    ut_check(!overlay_key_from_name("F6", NULL), "and so is a missing destination");

    vk = 0x1234;
    (void)overlay_key_from_name("banana", &vk);
    ut_check(vk == 0x1234,
             "a refused name leaves the caller's value alone, so a bad ini keeps the default "
             "rather than landing on whatever the parser reached last");
}

static void test_the_round_trip(void)
{
    static const int32_t CODES[] = {
        'A', 'Z', VK_F1, VK_F12, VK_F24, VK_NUMPAD0, VK_NUMPAD9,
        VK_ADD, VK_SUBTRACT, VK_MULTIPLY, VK_DIVIDE, VK_DECIMAL, VK_OEM_3,
        VK_SPACE, VK_TAB, VK_RETURN, VK_ESCAPE, VK_INSERT, VK_DELETE, VK_HOME, VK_END,
        VK_CONTROL, VK_SHIFT, VK_MENU, 0xF0
    };
    size_t i;

    ut_section("reading the panel and typing it back");

    for (i = 0; i < sizeof CODES / sizeof CODES[0]; ++i) {
        char    text[16];
        int32_t back = -1;

        overlay_key_name(CODES[i], text, sizeof text);
        ut_checkf(overlay_key_from_name(text, &back) && back == CODES[i],
                  "the name shown for %02X reads back as the same code", (unsigned)CODES[i]);
    }
}

int main(void)
{
    test_naming_a_code();
    test_reading_a_name();
    test_the_number_that_was_always_allowed();
    test_what_must_be_refused();
    test_the_round_trip();

    return ut_summary("overlay_key_name");
}
