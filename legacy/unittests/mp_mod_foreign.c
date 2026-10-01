/* mp_mod_foreign.c: the list of DLLs outside this release behind a statement's mods, and the one
 * rule over it.
 *
 * The list goes round, and a fuzz of 2000 lists with names up to 260 characters, bytes outside
 * ASCII and more names than fit holds the encoder to its promises: the whole statement never passes
 * its limit, only whole names are stated, the count never falls below the stated, and the flags say
 * exactly which names went. The strict decoder meets 2000 runs of random bytes and every cut of a
 * real statement and reads nothing but the list. The decoder of the mods as it stood before there
 * was a list is written out here as the reference, and it reads every new statement to the same
 * mods, so a host of that build still judges the builds first. The fingerprint does not see the
 * list. Then the rule and the judgement, case by case.
 */
#include "unittest.h"

#include "mp_mod_manifest_rule.h"
#include "mp_wire.h"

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <string.h>

#define LONG_NAME_MAX 260u

static uint32_t next_random(uint32_t *state)
{
    uint32_t x = *state;

    x ^= x << 13;
    x ^= x >> 17;
    x ^= x << 5;
    *state = x;
    return x;
}

static mp_mod_manifest_t one_mod(void)
{
    mp_mod_manifest_t m;

    memset(&m, 0, sizeof m);
    m.damage        = 0x84273DBBu;
    m.roster        = 0xE1D7140Du;
    m.count         = 1u;
    m.mods[0].id    = MP_WIRE_MOD_MULTIPLAYER;
    m.mods[0].stamp = 0x6ABB2222u;
    m.mods[0].image = 0x00A43000u;
    return m;
}

static size_t base_end(const mp_mod_manifest_t *m)
{
    return MP_MOD_MANIFEST_HEAD_BYTES + (size_t)m->count * MP_MOD_MANIFEST_MOD_BYTES;
}

/* The decoder of the mods as it stood before the list, the reference a host of that build is. */
static bool reference_decode(const uint8_t *bytes, size_t length, mp_mod_manifest_t *out)
{
    mp_wire_reader_t r;
    uint8_t          i;

    memset(out, 0, sizeof *out);
    mp_wire_reader_init(&r, bytes, length);
    mp_wire_get_u32(&r, &out->damage);
    mp_wire_get_u32(&r, &out->roster);
    mp_wire_get_u8(&r, &out->count);
    if (r.overran || out->count > MP_MOD_MANIFEST_MAX_MODS ||
        length < MP_MOD_MANIFEST_HEAD_BYTES + (size_t)out->count * MP_MOD_MANIFEST_MOD_BYTES) {
        return false;
    }
    for (i = 0; i < out->count; ++i) {
        mp_wire_get_u8(&r, &out->mods[i].id);
        mp_wire_get_u32(&r, &out->mods[i].stamp);
        mp_wire_get_u32(&r, &out->mods[i].image);
    }
    return !r.overran;
}

static bool same_manifest(const mp_mod_manifest_t *a, const mp_mod_manifest_t *b)
{
    uint8_t i;

    if (a->damage != b->damage || a->roster != b->roster || a->count != b->count) {
        return false;
    }
    for (i = 0; i < a->count; ++i) {
        if (a->mods[i].id != b->mods[i].id || a->mods[i].stamp != b->mods[i].stamp ||
            a->mods[i].image != b->mods[i].image) {
            return false;
        }
    }
    return true;
}

/* A statement: the mods, and the list of `names` behind them. Answers its length. */
static size_t statement(const mp_mod_manifest_t *m, const char *const *names, size_t listed,
                        size_t count, bool judged, uint8_t *out, bool *stated)
{
    size_t base = mp_mod_manifest_encode(m, out, MP_MOD_STATEMENT_MAX_BYTES);

    return base + mp_mod_foreign_encode(names, listed, count, judged, out + base,
                                        MP_MOD_STATEMENT_MAX_BYTES - base, stated);
}

static void check_a_list_goes_round(void)
{
    static const char *const ONE[] = { "fps_counter_hud.dll" };
    static const char *const MIXED[] = {
        "a_name_of_exactly_31_chars_.dll", "a_name_of_exactly_thirty_two.dll", "caf\xE9.dll",
        "tab\tin.dll", "", "ok.dll",
    };
    mp_mod_manifest_t m = one_mod();
    mp_mod_foreign_t  f;
    uint8_t           bytes[MP_MOD_STATEMENT_MAX_BYTES];
    bool              stated[16];
    size_t            length;

    ut_section("the list behind the mods, round and back");
    length = statement(&m, ONE, 1u, 1u, true, bytes, stated);
    ut_checkf(length == 42u, "one required mod and fps_counter_hud.dll are 42 bytes (%u)",
              (unsigned)length);
    mp_mod_foreign_decode(bytes, length, base_end(&m), &f);
    ut_check(f.form == MP_MOD_FOREIGN_SOUND && f.judged == 1u && f.count == 1u && f.stated == 1u &&
                 strcmp(f.names[0], "fps_counter_hud.dll") == 0 && stated[0],
             "and reads back as it was written, with its flag set");
    length = statement(&m, NULL, 0u, 0u, true, bytes, NULL);
    mp_mod_foreign_decode(bytes, length, base_end(&m), &f);
    ut_check(length == 22u && f.form == MP_MOD_FOREIGN_SOUND && f.count == 0u && f.judged == 1u,
             "no DLL outside this release is the four bytes of the head, 22 in all");
    length = statement(&m, NULL, 0u, 0u, false, bytes, NULL);
    mp_mod_foreign_decode(bytes, length, base_end(&m), &f);
    ut_check(f.form == MP_MOD_FOREIGN_SOUND && f.judged == 0u,
             "a build that judged nothing says so in the second byte");

    length = statement(&m, MIXED, 6u, 6u, true, bytes, stated);
    mp_mod_foreign_decode(bytes, length, base_end(&m), &f);
    ut_check(f.stated == 2u && strcmp(f.names[0], "a_name_of_exactly_31_chars_.dll") == 0 &&
                 strcmp(f.names[1], "ok.dll") == 0 && f.count == 6u,
             "a name of 31 characters is stated; one of 32, one outside ASCII, one with a tab and "
             "an empty one are counted and not named");
    ut_check(stated[0] && !stated[1] && !stated[2] && !stated[3] && !stated[4] && stated[5],
             "and the flags say exactly which were named");
    ut_check(mp_mod_foreign_encode(ONE, 1u, 1u, true, bytes, 3u, stated) == 0u,
             "a room that does not hold the head writes nothing");
    ut_check(mp_mod_foreign_encode(NULL, 2u, 2u, true, bytes, sizeof bytes, NULL) == 0u,
             "names promised and not given write nothing");
    length = mp_mod_foreign_encode(ONE, 1u, 300u, true, bytes, sizeof bytes, NULL);
    ut_check(length != 0u && bytes[2] == 0xFFu, "a count past 255 is written as 255");
    length = mp_mod_foreign_encode(ONE, 1u, 0u, true, bytes, sizeof bytes, NULL);
    ut_check(length != 0u && bytes[2] == 1u && bytes[3] == 1u,
             "and one below the names given is raised to them, "
             "so it never says fewer than it names");
}

static void check_the_room(void)
{
    static const char *const LONGEST[] = {
        "aaaaaaaaaaaaaaaaaaaaaaaaaaa.dll", "bbbbbbbbbbbbbbbbbbbbbbbbbbb.dll",
        "ccccccccccccccccccccccccccc.dll", "ddddddddddddddddddddddddddd.dll",
    };
    static const char *const NINE[] = {
        "1.dll", "2.dll", "3.dll", "4.dll", "5.dll", "6.dll", "7.dll", "8.dll", "9.dll",
    };
    mp_mod_manifest_t m = one_mod();
    mp_mod_foreign_t  f;
    uint8_t           bytes[MP_MOD_STATEMENT_MAX_BYTES];
    bool              stated[16];
    size_t            length;

    ut_section("the room a statement has");
    length = statement(&m, LONGEST, 4u, 4u, true, bytes, stated);
    mp_mod_foreign_decode(bytes, length, base_end(&m), &f);
    ut_checkf(length == 118u && f.stated == 3u && f.count == 4u && !stated[3],
              "three names of 31 characters fit behind one required mod, 118 bytes, and the "
              "fourth is counted (%u bytes, %u stated)", (unsigned)length, (unsigned)f.stated);
    length = statement(&m, NINE, 9u, 9u, true, bytes, stated);
    mp_mod_foreign_decode(bytes, length, base_end(&m), &f);
    ut_check(f.stated == MP_MOD_FOREIGN_NAMES_MAX && f.count == 9u && !stated[8],
             "no more than eight are named, however short");
}

/* A name of random length and bytes, some of them outside printable ASCII. */
static void random_name(uint32_t *state, char *out)
{
    size_t length = next_random(state) % (LONG_NAME_MAX + 1u);
    bool   plain = (next_random(state) & 3u) != 0u;
    size_t i;

    if ((next_random(state) & 1u) != 0u) {
        length %= 40u;   /* mostly names a list can state */
    }
    for (i = 0; i < length; ++i) {
        uint8_t byte = (uint8_t)next_random(state);

        out[i] = (char)(plain ? 0x21u + byte % 0x5Eu : (byte == 0u ? 1u : byte));
    }
    out[length] = '\0';
}

/* Whether the flags and the decoded names agree with what was given. */
static bool flags_agree(const char *const *names, size_t listed, const bool *stated,
                        const mp_mod_foreign_t *f)
{
    size_t i;
    size_t named = 0u;

    for (i = 0; i < listed; ++i) {
        if (!stated[i]) {
            continue;
        }
        if (named >= f->stated || strcmp(names[i], f->names[named]) != 0 ||
            strlen(names[i]) >= MP_MOD_FOREIGN_NAME_MAX) {
            return false;
        }
        ++named;
    }
    return named == f->stated;
}

static void check_the_fuzz(void)
{
    static char       store[20][LONG_NAME_MAX + 1u];
    const char       *names[20];
    uint8_t           bytes[MP_MOD_STATEMENT_MAX_BYTES];
    bool              stated[20];
    uint32_t          state = 0x5EED4321u;
    unsigned          round;
    unsigned          failed = 0u;
    unsigned          reference = 0u;

    ut_section("2000 random lists behind random mods");
    for (round = 0; round < 2000u; ++round) {
        mp_mod_manifest_t any;
        mp_mod_manifest_t back;
        mp_mod_foreign_t  f;
        size_t            listed = next_random(&state) % 21u;
        size_t            count = listed + next_random(&state) % 300u;
        size_t            length;
        size_t            i;

        memset(&any, 0, sizeof any);
        any.damage = next_random(&state);
        any.count  = (uint8_t)(next_random(&state) % (MP_MOD_MANIFEST_MAX_MODS + 1u));
        for (i = 0; i < any.count; ++i) {
            any.mods[i].id    = (uint8_t)next_random(&state);
            any.mods[i].stamp = next_random(&state);
        }
        for (i = 0; i < listed; ++i) {
            random_name(&state, store[i]);
            names[i] = store[i];
        }
        length = statement(&any, names, listed, count, true, bytes, stated);
        mp_mod_foreign_decode(bytes, length, base_end(&any), &f);
        if (length > MP_MOD_STATEMENT_MAX_BYTES || f.form != MP_MOD_FOREIGN_SOUND ||
            f.stated > f.count || f.count != (count > 255u ? 255u : count) ||
            !flags_agree(names, listed, stated, &f) ||
            !mp_mod_manifest_decode(bytes, length, &back) ||
            !same_manifest(&any, &back)) {
            ++failed;
        }
        if (!reference_decode(bytes, length, &back) || !same_manifest(&any, &back)) {
            ++reference;
        }
    }
    ut_checkf(failed == 0u, "never past the limit, only whole names, the count never below the "
              "stated, the flags right and the mods untouched (%u did not)", failed);
    ut_checkf(reference == 0u, "the decoder from before the list reads every one to the same mods "
              "(%u did not)", reference);
}

static void check_the_strict_decoder(void)
{
    static const char *const TWO[] = { "a.dll", "bb.dll" };
    mp_mod_manifest_t m = one_mod();
    mp_mod_manifest_t back;
    mp_mod_foreign_t  f;
    uint8_t           bytes[MP_MOD_STATEMENT_MAX_BYTES];
    uint8_t           bad[MP_MOD_STATEMENT_MAX_BYTES];
    size_t            length = statement(&m, TWO, 2u, 2u, true, bytes, NULL);
    size_t            end = base_end(&m);
    size_t            cut;
    uint32_t          state = 0x0BADF00Du;
    unsigned          round;
    unsigned          failed = 0u;

    ut_section("the decoder is strict and reads only the list");
    for (cut = end; cut <= length; ++cut) {
        mp_mod_foreign_decode(bytes, cut, end, &f);
        if (f.form != (cut == end ? MP_MOD_FOREIGN_ABSENT
                       : cut == length ? MP_MOD_FOREIGN_SOUND : MP_MOD_FOREIGN_MALFORMED) ||
            !mp_mod_manifest_decode(bytes, cut, &back) || !same_manifest(&m, &back)) {
            ++failed;
        }
    }
    ut_checkf(failed == 0u, "every cut is absent, broken or whole, and the mods before it always "
              "read (%u did not)", failed);

    memcpy(bad, bytes, length);
    bad[length] = 0xEEu;
    mp_mod_foreign_decode(bad, length + 1u, end, &f);
    ut_check(f.form == MP_MOD_FOREIGN_SOUND && f.stated == 2u,
             "a byte behind the last name belongs to a later form and is not read");
    mp_mod_foreign_decode(bytes, length, length + 1u, &f);
    ut_check(f.form == MP_MOD_FOREIGN_MALFORMED, "mods said to end past the statement are broken");
    {
        static const size_t  at[] = { 0u, 1u, 3u, 4u, 4u, 5u, 5u };
        static const uint8_t to[] = { 2u, 2u, 3u, 0u, 32u, 0x80u, 0x1Fu };
        size_t               i;

        failed = 0u;
        for (i = 0; i < sizeof at / sizeof at[0]; ++i) {
            memcpy(bad, bytes, length);
            bad[end + at[i]] = to[i];
            mp_mod_foreign_decode(bad, length, end, &f);
            if (f.form != MP_MOD_FOREIGN_MALFORMED || f.stated != 0u || f.names[0][0] != '\0') {
                ++failed;
            }
        }
        ut_checkf(failed == 0u, "another form, a judged byte past 1, more stated than counted, an "
                  "empty name, a name of 32 and a byte outside printable ASCII each read as broken "
                  "and empty (%u did not)", failed);
    }
    memcpy(bad, bytes, length);
    bad[end + 2u] = 9u;
    bad[end + 3u] = 9u;
    mp_mod_foreign_decode(bad, length, end, &f);
    ut_check(f.form == MP_MOD_FOREIGN_MALFORMED, "a ninth name is more than a list may state");
    failed = 0u;
    for (round = 0; round < 2000u; ++round) {
        size_t i;

        for (i = 0; i < sizeof bad; ++i) {
            bad[i] = (uint8_t)next_random(&state);
        }
        cut = next_random(&state) % (sizeof bad + 1u);
        mp_mod_foreign_decode(bad, cut, next_random(&state) % (cut + 2u), &f);
        if (f.form == MP_MOD_FOREIGN_SOUND &&
            (f.stated > f.count || f.stated > MP_MOD_FOREIGN_NAMES_MAX || f.judged > 1u)) {
            ++failed;
        }
    }
    ut_checkf(failed == 0u, "2000 random byte runs never read as a list that breaks its own form "
              "(%u did)", failed);
}

static void check_the_fingerprint(void)
{
    static const char *const ONE[] = { "fps_counter_hud.dll" };
    mp_mod_manifest_t m = one_mod();
    mp_mod_manifest_t with;
    mp_mod_manifest_t without;
    uint8_t           a[MP_MOD_STATEMENT_MAX_BYTES];
    uint8_t           b[MP_MOD_STATEMENT_MAX_BYTES];
    size_t            a_length = statement(&m, ONE, 1u, 1u, true, a, NULL);
    size_t            b_length = statement(&m, NULL, 0u, 0u, true, b, NULL);

    ut_section("the fingerprint does not see the list");
    ut_check(mp_mod_manifest_decode(a, a_length, &with) &&
                 mp_mod_manifest_decode(b, b_length, &without) &&
                 mp_mod_manifest_fingerprint(&with) == mp_mod_manifest_fingerprint(&without),
             "two machines with other DLLs of their own agree on the content note's number");
}

static void check_the_rule(void)
{
    static const char *const TWO[] = { "reshade.dll", "Fps_Counter_Hud.DLL" };
    static const char *const COMMA[] = { "x,y.dll" };

    ut_section("the one rule over a list of names");
    ut_check(mp_mod_foreign_first_refused(TWO, 2u, "") == 0u &&
                 mp_mod_foreign_first_refused(TWO, 2u, NULL) == 0u,
             "an empty list refuses the first name");
    ut_check(mp_mod_foreign_first_refused(TWO, 2u, "reshade, fps_counter_hud.dll") == 2u,
             "a list naming both, with or without .dll and in any case, refuses none");
    ut_check(mp_mod_foreign_first_refused(TWO, 2u, "RESHADE.dll") == 1u,
             "the first name the list leaves out is the one refused");
    ut_check(mp_mod_foreign_first_refused(COMMA, 1u, "x,y.dll") == 0u,
             "a file name with a comma in it can never be named");
    ut_check(mp_mod_foreign_first_refused(TWO, 0u, "") == 0u &&
                 mp_mod_foreign_first_refused(NULL, 1u, "a") == 0u,
             "no names refuse nothing, and names promised and not given refuse the first");
}

static mp_mod_foreign_t a_list(uint8_t count, uint8_t stated, bool judged)
{
    mp_mod_foreign_t f;

    memset(&f, 0, sizeof f);
    f.form   = MP_MOD_FOREIGN_SOUND;
    f.judged = judged ? 1u : 0u;
    f.count  = count;
    f.stated = stated;
    memcpy(f.names[0], "reshade.dll", sizeof "reshade.dll");
    memcpy(f.names[1], "x.dll", sizeof "x.dll");
    return f;
}

static void check_the_judgement(void)
{
    mp_mod_foreign_t f = a_list(2u, 2u, true);
    mp_mod_verdict_t v;

    ut_section("the host's verdict on the list");
    mp_mod_foreign_judge(&f, "reshade, x", &v);
    ut_check(v.reason == MP_MOD_REFUSE_NONE, "every name listed and every DLL named: admitted");
    mp_mod_foreign_judge(&f, "reshade", &v);
    ut_check(v.reason == MP_MOD_REFUSE_FOREIGN && v.sub == MP_MOD_SUB_NOT_ALLOWED && v.mod == 1u &&
                 v.host_stamp == 0u && v.host_image == 0u,
             "a name the list does not name: refused, with its place in the list, no stamps");
    f = a_list(3u, 2u, true);
    mp_mod_foreign_judge(&f, "reshade, x", &v);
    ut_check(v.reason == MP_MOD_REFUSE_FOREIGN && v.sub == MP_MOD_SUB_NOT_NAMED &&
                 v.mod == MP_MOD_NO_MOD,
             "more DLLs than named: refused, the rest cannot be held against the list");
    f = a_list(3u, 2u, true);
    mp_mod_foreign_judge(&f, "x", &v);
    ut_check(v.sub == MP_MOD_SUB_NOT_ALLOWED && v.mod == 0u,
             "a name refused comes before the count");
    f = a_list(2u, 2u, false);
    mp_mod_foreign_judge(&f, "", &v);
    ut_check(v.reason == MP_MOD_REFUSE_NONE, "a side that judged nothing is admitted");
    f = a_list(0u, 0u, true);
    mp_mod_foreign_judge(&f, "", &v);
    ut_check(v.reason == MP_MOD_REFUSE_NONE, "no DLL outside this release: admitted by any list");
    f.form = MP_MOD_FOREIGN_ABSENT;
    mp_mod_foreign_judge(&f, "anything", &v);
    ut_check(v.reason == MP_MOD_REFUSE_FOREIGN && v.sub == MP_MOD_SUB_NO_LIST,
             "no list at all: refused, a build like this one always sends one");
    f.form = MP_MOD_FOREIGN_MALFORMED;
    mp_mod_foreign_judge(&f, "anything", &v);
    ut_check(v.sub == MP_MOD_SUB_NO_LIST, "a broken list: refused the same way");
    mp_mod_foreign_judge(NULL, "", &v);
    ut_check(v.sub == MP_MOD_SUB_NO_LIST, "and no list handed in at all");
}

static void check_the_log_list(void)
{
    static const char *const TEN[] = {
        "a.dll", "b.dll", "c.dll", "d.dll", "e.dll", "f.dll", "g.dll", "h.dll", "i.dll", "j.dll",
    };
    static const char *const HUGE[] = {
        "a_name_far_longer_than_the_room_a_short_buffer_has.dll",
    };
    char out[128];

    ut_section("the names as a log line lists them");
    ut_check(strcmp(mp_mod_foreign_list(TEN, 0u, out, sizeof out), "none") == 0, "none");
    ut_check(strcmp(mp_mod_foreign_list(TEN, 2u, out, sizeof out), "a.dll, b.dll") == 0,
             "two names with a comma between");
    ut_checkf(strcmp(mp_mod_foreign_list(TEN, 10u, out, sizeof out),
                     "a.dll, b.dll, c.dll, d.dll, e.dll, f.dll, g.dll, h.dll, and 2 more") == 0,
              "the first eight, then how many more (%s)", out);
    ut_checkf(strcmp(mp_mod_foreign_list(TEN, 10u, out, 40u), "a.dll, b.dll, and 8 more") == 0,
              "whole names only, as many as the room holds (%s)", out);
    ut_checkf(strcmp(mp_mod_foreign_list(HUGE, 1u, out, 40u),
                     "1 name(s), none short enough to list") == 0,
              "a name that never fits is counted (%s)", out);
}

int main(void)
{
    check_a_list_goes_round();
    check_the_room();
    check_the_fuzz();
    check_the_strict_decoder();
    check_the_fingerprint();
    check_the_rule();
    check_the_judgement();
    check_the_log_list();
    return ut_summary("mp_mod_foreign");
}
