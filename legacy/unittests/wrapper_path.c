/* The two path questions xidi_bridge asks before it rewrites anything.
 *
 * Both can be wrong without saying so. If "is this library already ours" answers yes when it is
 * not, the fix declines and controller support silently stays off; if it answers no when it is,
 * the wrapper is loaded a second time under another name. And a joined path that is truncated
 * rather than refused names some other file, which LoadLibrary is then perfectly happy to fail on
 * for a reason that has nothing to do with the real one.
 */
#include "unittest.h"

#include "wrapper_path.h"

#include <string.h>

static void check_inside(void)
{
    ut_section("which directory a loaded library came out of");

    ut_check(wrapper_path_inside("C:\\Game", "C:\\Game\\winmm.dll"),
             "a file directly in the directory is inside it");
    ut_check(wrapper_path_inside("C:\\Game\\", "C:\\Game\\winmm.dll"),
             "a trailing separator on the directory asks the same question");
    ut_check(wrapper_path_inside("C:\\Game", "C:\\Game\\mods\\xidi_bridge.dll"),
             "a file in a subdirectory is inside it too");

    ut_check(!wrapper_path_inside("C:\\Game", "C:\\Gamedata\\winmm.dll"),
             "a sibling whose name merely starts the same way is NOT inside it");
    ut_check(!wrapper_path_inside("C:\\Game\\", "C:\\Gamedata\\winmm.dll"),
             "and the trailing separator does not change that answer either");
    ut_check(!wrapper_path_inside("C:\\Game", "C:\\WINDOWS\\SYSTEM32\\WINMM.dll"),
             "the system directory is not the game directory, which is the whole point");

    ut_check(wrapper_path_inside("c:\\game", "C:\\GAME\\WINMM.DLL"),
             "case is ignored, because the loader spells a path however the file system recorded it");
    ut_check(wrapper_path_inside("C:/Game", "C:\\Game\\winmm.dll"),
             "a forward slash and a backslash separate the same two names");

    ut_check(!wrapper_path_inside("C:\\Game", "C:\\Game"),
             "the directory itself is not a file inside it");
    ut_check(!wrapper_path_inside("C:\\Game\\Longer", "C:\\Game"),
             "a path shorter than the directory cannot be inside it");

    ut_check(!wrapper_path_inside(NULL, "C:\\Game\\winmm.dll"), "a missing directory is refused");
    ut_check(!wrapper_path_inside("C:\\Game", NULL), "a missing path is refused");
    ut_check(!wrapper_path_inside("", "C:\\Game\\winmm.dll"), "an empty directory is refused");
    ut_check(!wrapper_path_inside("C:\\Game", ""), "an empty path is refused");
    ut_check(!wrapper_path_inside("\\", "\\winmm.dll"),
             "a directory that is nothing but separators is refused rather than matching everything");
}

static void check_join(void)
{
    char buffer[32];

    ut_section("building the path to the wrapper");

    ut_check(wrapper_path_join("C:\\Game", "xidi_winmm.dll", buffer, sizeof(buffer)) &&
             strcmp(buffer, "C:\\Game\\xidi_winmm.dll") == 0,
             "a directory without a separator gets one inserted");

    ut_check(wrapper_path_join("C:\\Game\\", "xidi_winmm.dll", buffer, sizeof(buffer)) &&
             strcmp(buffer, "C:\\Game\\xidi_winmm.dll") == 0,
             "a directory that already ends in one does not get a second");

    ut_check(wrapper_path_join("C:/Game/", "x.dll", buffer, sizeof(buffer)) &&
             strcmp(buffer, "C:/Game/x.dll") == 0,
             "a trailing forward slash counts as the separator and no second one is added");

    /* Windows accepts either separator anywhere in a path, so the mixed result is a real path and
     * not a near miss. It is stated here because the obvious expectation is the other one. */
    ut_check(wrapper_path_join("C:/Game", "x.dll", buffer, sizeof(buffer)) &&
             strcmp(buffer, "C:/Game\\x.dll") == 0,
             "a directory written with forward slashes still gets a backslash appended");

    /* "C:\\Game\\x.dll" is 13 characters, so 14 bytes is the exact fit and 13 is one short. */
    ut_check(wrapper_path_join("C:\\Game", "x.dll", buffer, 14) &&
             strcmp(buffer, "C:\\Game\\x.dll") == 0,
             "a buffer of exactly the right size is accepted");

    buffer[0] = 'x';
    ut_check(!wrapper_path_join("C:\\Game", "x.dll", buffer, 13),
             "one byte short is refused rather than truncated");
    ut_check(buffer[0] == '\0',
             "and the buffer is emptied, so a caller that ignores the answer gets no path at all");

    ut_check(!wrapper_path_join(NULL, "x.dll", buffer, sizeof(buffer)),
             "a missing directory is refused");
    ut_check(!wrapper_path_join("C:\\Game", NULL, buffer, sizeof(buffer)),
             "a missing file name is refused");
    ut_check(!wrapper_path_join("C:\\Game", "", buffer, sizeof(buffer)),
             "an empty file name is refused, because it would name the directory");
    ut_check(!wrapper_path_join("C:\\Game", "x.dll", NULL, sizeof(buffer)),
             "a missing buffer is refused rather than written to");
    ut_check(!wrapper_path_join("C:\\Game", "x.dll", buffer, 0),
             "a buffer of no size is refused, and nothing is written into it");
}

/* The one case both functions have to agree on, because the feature chains them: the wrapper it
 * builds a path to must count as being inside the folder it was built from. */
static void check_agreement(void)
{
    char buffer[64];

    ut_section("the two together");

    ut_check(wrapper_path_join("C:\\Game", "xidi_winmm.dll", buffer, sizeof(buffer)) &&
             wrapper_path_inside("C:\\Game", buffer),
             "the path built for the wrapper is inside the directory it was built from");

    ut_check(wrapper_path_join("C:\\Game\\", "xidi_winmm.dll", buffer, sizeof(buffer)) &&
             wrapper_path_inside("C:\\Game\\", buffer),
             "and the same holds when the directory carries a trailing separator");
}

int main(void)
{
    check_inside();
    check_join();
    check_agreement();

    return ut_summary("wrapper path");
}
