/* mp_cutscene_sites.h: where a script takes what a scene holds, and where it gives it back.
 *
 * Layer 2. The scene gates of mp_cutscene.c tell a script's call from the engine's own by the
 * address the call returns to, and two of the functions they hull are found through the calls a
 * script makes to them. Finding those places is reading bytes and proving them, and it is here;
 * the hulls and what they refuse stay in mp_cutscene.c. It is the seam that file took when the
 * two releases joined its five hulls.
 *
 * The script's side of a scene is two opcodes, both inline arms of the script runner, laid out
 * one behind the other. The retail image:
 *
 *   the camera dolly, opcode 0x20C
 *   00434F38  8B 45 F8 / 83 38 00 / 7C 10    the sign of its operand picks the end
 *   00434F40  8B 4D F8 / 8B 11 / 52
 *   00434F46  E8 BF 34 FE FF      call 0041840A  bapview_overrideOn     returns to 00434F4B
 *   00434F4B  83 C4 04 / EB 19
 *   00434F50  E8 CC 34 FE FF      call 00418421  bapview_overrideOff    returns to 00434F55
 *   00434F55  6A 63               push 99
 *   00434F57  E8 BC BF FF FF      call 00430F18  the lock's release     returns to 00434F5C
 *   00434F5C  83 C4 04 / 6A 00
 *   00434F61  E8 69 47 00 00      call 004396CF  the bars, told to go   returns to 00434F66
 *
 *   the lock, opcode 0x604
 *   00434F6E  8B 45 F8 / 8B 48 04 / 51
 *   00434F75  E8 55 47 00 00      call 004396CF  the bars, as its second operand says
 *   00434F7A  83 C4 04
 *   00434F7D  8B 55 F8 / 83 3A 00 / 7C 1A    the sign of its first operand picks the end
 *   00434F85  8B 45 F8 / 8B 08 / 51
 *   00434F8B  E8 7A 34 FE FF      call 0041840A  bapview_overrideOn     returns to 00434F90
 *   00434F90  83 C4 04 / 6A 05
 *   00434F95  E8 3F BF FF FF      call 00430ED9  the lock's entry       returns to 00434F9A
 *   00434F9A  83 C4 04 / EB 0F
 *   00434F9F  E8 7D 34 FE FF      call 00418421  bapview_overrideOff    returns to 00434FA4
 *   00434FA4  6A 05               push 5
 *   00434FA6  E8 6D BF FF FF      call 00430F18  the lock's release     returns to 00434FAB
 *
 * So a take is one, two or three calls, the bars, the camera and the lock, and an end is three:
 * the camera, the lock and the bars for the dolly, the bars, the camera and the lock for the lock
 * opcode. The bars have no other caller in the image than these two, so a call of the bars is a
 * script's by itself. No address above is written into the code: every one is found by a pattern
 * or read out of a call operand, and each is proved by the call in front of it.
 */
#ifndef MULTIPLAYER_MP_CUTSCENE_SITES_H
#define MULTIPLAYER_MP_CUTSCENE_SITES_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

/* The three places a script takes the camera, in the order every table here keeps. */
enum {
    MP_CUTSCENE_TAKE_DOLLY = 0,   /* the camera dolly opcode */
    MP_CUTSCENE_TAKE_LOCK,        /* the lock opcode */
    MP_CUTSCENE_TAKE_SPEAK,       /* a spoken line */
    MP_CUTSCENE_TAKES
};

/* The two places a script gives a scene back. */
enum {
    MP_CUTSCENE_END_DOLLY = 0,
    MP_CUTSCENE_END_LOCK,
    MP_CUTSCENE_ENDS
};

typedef struct mp_cutscene_takes {
    uintptr_t take_return[MP_CUTSCENE_TAKES];      /* by take, nought where it did not resolve */
    uintptr_t script_returns[MP_CUTSCENE_TAKES];   /* the resolved ones, packed for one test */
    size_t    script_return_count;
    uintptr_t camera_entry;            /* the camera take itself, nought where it is not proved */
    size_t    camera_operands;         /* the calls that named it, all agreeing */
    bool      camera_pattern_agrees;   /* and its own pattern resolved at the same address */
    bool      camera_head_branched;    /* its head was another module's branch already */
} mp_cutscene_takes_t;

/* Resolves the three script takes and, out of the calls they make, the camera take. A take that
 * does not resolve is said and left out, and its opcode then keeps the camera. The entry is
 * nought, with a line, where the calls do not prove one address or the bytes there are not the
 * function. To be called before the camera is hulled: the head is read as another module left
 * it. */
void mp_cutscene_sites_takes(mp_cutscene_takes_t *out);

typedef struct mp_cutscene_releases {
    uintptr_t lock_off_entry;                      /* the lock's release, nought where not found */
    uintptr_t lock_off_return[MP_CUTSCENE_ENDS];   /* where a script's call of it returns to */
    uintptr_t camera_off_entry;                    /* the camera's, nought where not proved */
    uintptr_t camera_off_return[MP_CUTSCENE_ENDS];
} mp_cutscene_releases_t;

/* Resolves the two releases out of the two script ends, counted from the takes already resolved.
 * The lock's release is found by its own pattern and held against the ends; the clearing of the
 * camera's override has no pattern that would find it and is the address both ends call, proved
 * by its bytes. A return address is set only for an end whose own call names the function, so an
 * end that is not what it was taken for matches nothing. Says what is missing. */
void mp_cutscene_sites_releases(const mp_cutscene_takes_t *takes, mp_cutscene_releases_t *out);

#endif /* MULTIPLAYER_MP_CUTSCENE_SITES_H */
