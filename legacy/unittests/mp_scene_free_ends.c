/* Where the two releases of a scene live, read out of the two script ends.
 *
 * A script gives a scene back at two places, the end of the camera dolly opcode and the end of the
 * lock opcode, and both are the same twelve bytes: a call of the clearing of the camera's
 * override, a push of the level, a call of the lock's release. The lock's release is found by its
 * own pattern and only held against these ends. The clearing of the camera's override has no
 * pattern of its own, because it is fifteen bytes of which another module's hull takes thirteen,
 * so the ends are the only place it is read from, and a reading that is wrong by one byte would
 * call into the middle of something.
 *
 * The bytes are the retail image's:
 *
 *   00434F50  E8 CC 34 FE FF   call 00418421      the dolly opcode's end
 *   00434F55  6A 63            push 99
 *   00434F57  E8 BC BF FF FF   call 00430F18      returns to 00434F5C
 *
 *   00434F9F  E8 7D 34 FE FF   call 00418421      the lock opcode's end
 *   00434FA4  6A 05            push 5
 *   00434FA6  E8 6D BF FF FF   call 00430F18      returns to 00434FAB
 */
#include "unittest.h"

#include "mp_scene_free_rule.h"

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <string.h>

#define RETAIL_RELEASE    ((uintptr_t)0x00430F18u)
#define RETAIL_CAMERA_OFF ((uintptr_t)0x00418421u)
#define RETAIL_LOCK_ENTER ((uintptr_t)0x00430ED9u)

static void retail_ends(mp_scene_free_end_t ends[MP_SCENE_FREE_ENDS])
{
    static const uint8_t DOLLY[MP_SCENE_FREE_END_BYTES] = {
        0xE8, 0xCC, 0x34, 0xFE, 0xFF, 0x6A, 0x63, 0xE8, 0xBC, 0xBF, 0xFF, 0xFF
    };
    static const uint8_t LOCK[MP_SCENE_FREE_END_BYTES] = {
        0xE8, 0x7D, 0x34, 0xFE, 0xFF, 0x6A, 0x05, 0xE8, 0x6D, 0xBF, 0xFF, 0xFF
    };

    memset(ends, 0, MP_SCENE_FREE_ENDS * sizeof ends[0]);
    ends[0].read           = true;
    ends[0].return_address = (uintptr_t)0x00434F5Cu;
    memcpy(ends[0].bytes, DOLLY, sizeof DOLLY);
    ends[1].read           = true;
    ends[1].return_address = (uintptr_t)0x00434FABu;
    memcpy(ends[1].bytes, LOCK, sizeof LOCK);
}

static void check_the_retail_ends(void)
{
    mp_scene_free_end_t  ends[MP_SCENE_FREE_ENDS];
    mp_scene_free_ends_t said;

    ut_section("the two script ends of the retail image");
    retail_ends(ends);
    mp_scene_free_read_ends(RETAIL_RELEASE, ends, &said);
    ut_check(said.release_holds && said.witnesses == 2u && said.disagreeing == 0u,
             "both ends call the lock's release five bytes in front of their return");
    ut_checkf(said.camera_off == RETAIL_CAMERA_OFF,
              "and twelve bytes in front of it both call %08X, the clearing of the camera",
              (unsigned)said.camera_off);
}

static void check_an_end_that_proves_nothing(void)
{
    mp_scene_free_end_t  ends[MP_SCENE_FREE_ENDS];
    mp_scene_free_ends_t said;

    ut_section("an end that proves nothing leaves the release, and takes the camera");
    retail_ends(ends);
    ends[0].read = false;
    mp_scene_free_read_ends(RETAIL_RELEASE, ends, &said);
    ut_check(said.release_holds && said.witnesses == 1u && said.camera_off == 0u,
             "one end unread: the release holds on the other, the camera is not proven by one");
    retail_ends(ends);
    ends[1].bytes[7] = 0x90u;
    mp_scene_free_read_ends(RETAIL_RELEASE, ends, &said);
    ut_check(said.release_holds && said.witnesses == 1u && said.camera_off == 0u,
             "one end not ending in a call: the same");
    retail_ends(ends);
    ends[0].read = false;
    ends[1].read = false;
    mp_scene_free_read_ends(RETAIL_RELEASE, ends, &said);
    ut_check(said.release_holds && said.witnesses == 0u && said.camera_off == 0u,
             "neither end read: the release stands on its pattern alone, with no camera");
    retail_ends(ends);
    ends[1].return_address = (uintptr_t)MP_SCENE_FREE_END_BYTES;
    mp_scene_free_read_ends(RETAIL_RELEASE, ends, &said);
    ut_check(said.release_holds && said.witnesses == 1u && said.camera_off == 0u,
             "an end with no twelve bytes in front of it is no end");
}

static void check_an_end_that_disagrees(void)
{
    mp_scene_free_end_t  ends[MP_SCENE_FREE_ENDS];
    mp_scene_free_ends_t said;

    ut_section("an end that calls another address refuses the release");
    retail_ends(ends);
    ends[1].bytes[8] = 0x2Eu;   /* 00434FAB + FFFFBF2E is 00430ED9, the lock's entry */
    mp_scene_free_read_ends(RETAIL_RELEASE, ends, &said);
    ut_checkf(!said.release_holds && said.disagreeing == RETAIL_LOCK_ENTER &&
                  said.camera_off == 0u,
              "the lock's end calling %08X where the release should be: nothing is to be "
              "called, the lock's or the camera's", (unsigned)said.disagreeing);
    retail_ends(ends);
    mp_scene_free_read_ends(RETAIL_LOCK_ENTER, ends, &said);
    ut_check(!said.release_holds && said.disagreeing == RETAIL_RELEASE &&
                 said.camera_off == 0u,
             "and a pattern that resolved at the wrong function is refused by both ends");
}

static void check_the_camera(void)
{
    mp_scene_free_end_t  ends[MP_SCENE_FREE_ENDS];
    mp_scene_free_ends_t said;

    ut_section("the camera needs both ends, the push in its place, and one address");
    retail_ends(ends);
    ends[0].bytes[5] = 0x68u;
    mp_scene_free_read_ends(RETAIL_RELEASE, ends, &said);
    ut_check(said.release_holds && said.witnesses == 2u && said.camera_off == 0u,
             "no push of a byte seven bytes in front of the return: not that end, no camera");
    retail_ends(ends);
    ends[0].bytes[0] = 0xE9u;
    mp_scene_free_read_ends(RETAIL_RELEASE, ends, &said);
    ut_check(said.release_holds && said.camera_off == 0u,
             "a jump where the camera's call should be: no camera");
    retail_ends(ends);
    ends[1].bytes[1] = 0x7Eu;
    mp_scene_free_read_ends(RETAIL_RELEASE, ends, &said);
    ut_check(said.release_holds && said.camera_off == 0u,
             "the two ends calling two addresses a byte apart: no camera");
    retail_ends(ends);
    ends[0].bytes[1] = 0xC3u;   /* 00434F55 + FFFFBFC3 is 00430F18, the release itself */
    ends[0].bytes[2] = 0xBFu;
    ends[0].bytes[3] = 0xFFu;
    ends[1].bytes[1] = 0x74u;   /* 00434FA4 + FFFFBF74 is the same */
    ends[1].bytes[2] = 0xBFu;
    ends[1].bytes[3] = 0xFFu;
    mp_scene_free_read_ends(RETAIL_RELEASE, ends, &said);
    ut_check(said.release_holds && said.witnesses == 2u && said.camera_off == 0u,
             "both ends calling the release itself twelve bytes back: that is no camera");
}

static void check_what_is_not_there(void)
{
    mp_scene_free_end_t  ends[MP_SCENE_FREE_ENDS];
    mp_scene_free_ends_t said;

    ut_section("what is not there");
    retail_ends(ends);
    mp_scene_free_read_ends(0u, ends, &said);
    ut_check(!said.release_holds && said.witnesses == 0u && said.camera_off == 0u,
             "no release found: nothing holds");
    mp_scene_free_read_ends(RETAIL_RELEASE, NULL, &said);
    ut_check(!said.release_holds && said.camera_off == 0u, "no ends: nothing holds");
    memset(&said, 0x5A, sizeof said);
    mp_scene_free_read_ends(RETAIL_RELEASE, ends, NULL);
    ut_check(said.witnesses == 0x5A5A5A5Au, "nowhere to answer: nothing is written");
}

int main(void)
{
    check_the_retail_ends();
    check_an_end_that_proves_nothing();
    check_an_end_that_disagrees();
    check_the_camera();
    check_what_is_not_there();
    return ut_summary("the two releases of a scene, out of the script ends");
}
