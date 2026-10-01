/* mp_mod_manifest_rule.c: what two machines must hold the same to play together.
 *
 * The statement's codec round trip and a fuzz over it, the judgement in every case it has, a
 * refusal between two builds whose lists of required mods have different lengths, the refusal's
 * detail against a stranger's bytes, and the table of the release's mods against the tree's naming
 * law. The fingerprint that replaced the old one is held against the old composition, written out
 * here as the reference: what the old one refused two machines for, the new one does not see. And
 * the one property that keeps the join and the level's content note a single judgement: a pair the
 * judge admits never has two fingerprints.
 */
#include "unittest.h"

#include "mp_mod_manifest_rule.h"
#include "mp_wire.h"

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <string.h>

/* ===================================== The statement ====================================== */

static mp_mod_manifest_t a_manifest(uint32_t damage, uint32_t roster, uint32_t stamp)
{
    mp_mod_manifest_t m;

    memset(&m, 0, sizeof m);
    m.damage        = damage;
    m.roster        = roster;
    m.count         = 1u;
    m.mods[0].id    = MP_WIRE_MOD_MULTIPLAYER;
    m.mods[0].stamp = stamp;
    m.mods[0].image = 0x00A43000u;
    return m;
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

static uint32_t next_random(uint32_t *state)
{
    uint32_t x = *state;

    x ^= x << 13;
    x ^= x >> 17;
    x ^= x << 5;
    *state = x;
    return x;
}

static void check_the_codec(void)
{
    mp_mod_manifest_t m = a_manifest(0x84273DBBu, 0xE1D7140Du, 0x6ABB2222u);
    mp_mod_manifest_t back;
    uint8_t           bytes[MP_MOD_MANIFEST_MAX_BYTES];
    size_t            length;
    size_t            cut;
    uint32_t          state = 0x5EED1234u;
    unsigned          round;
    unsigned          failed = 0u;

    ut_section("the statement on the wire");

    length = mp_mod_manifest_encode(&m, bytes, sizeof bytes);
    ut_checkf(length == MP_MOD_MANIFEST_HEAD_BYTES + MP_MOD_MANIFEST_MOD_BYTES,
              "one required mod is nine bytes of head and nine of mod (%u)", (unsigned)length);
    ut_check(mp_mod_manifest_decode(bytes, length, &back) && same_manifest(&m, &back),
             "and reads back as it was written");
    for (cut = 0; cut < length; ++cut) {
        if (mp_mod_manifest_decode(bytes, cut, &back)) {
            ++failed;
        }
    }
    ut_checkf(failed == 0u, "every statement cut short is refused (%u taken)", failed);
    ut_check(mp_mod_manifest_encode(&m, bytes, length - 1u) == 0u,
             "a buffer one byte too small writes nothing");
    bytes[8] = (uint8_t)(MP_MOD_MANIFEST_MAX_MODS + 1u);
    ut_check(!mp_mod_manifest_decode(bytes, sizeof bytes, &back),
             "a count past the most a statement may name is refused");
    m.count = (uint8_t)(MP_MOD_MANIFEST_MAX_MODS + 1u);
    ut_check(mp_mod_manifest_encode(&m, bytes, sizeof bytes) == 0u, "and never written");

    failed = 0u;
    for (round = 0; round < 2000u; ++round) {
        mp_mod_manifest_t any;
        uint8_t           i;

        memset(&any, 0, sizeof any);
        any.damage = next_random(&state);
        any.roster = next_random(&state);
        any.count  = (uint8_t)(next_random(&state) % (MP_MOD_MANIFEST_MAX_MODS + 1u));
        for (i = 0; i < any.count; ++i) {
            any.mods[i].id    = (uint8_t)next_random(&state);
            any.mods[i].stamp = next_random(&state);
            any.mods[i].image = next_random(&state);
        }
        length = mp_mod_manifest_encode(&any, bytes, sizeof bytes);
        if (length == 0u || !mp_mod_manifest_decode(bytes, length, &back) ||
            !same_manifest(&any, &back)) {
            ++failed;
        }
    }
    ut_checkf(failed == 0u, "2000 random statements go round unchanged (%u did not)", failed);

    failed = 0u;
    for (round = 0; round < 2000u; ++round) {
        size_t i;

        for (i = 0; i < sizeof bytes; ++i) {
            bytes[i] = (uint8_t)next_random(&state);
        }
        length = next_random(&state) % (sizeof bytes + 1u);
        if (mp_mod_manifest_decode(bytes, length, &back) &&
            (back.count > MP_MOD_MANIFEST_MAX_MODS ||
             MP_MOD_MANIFEST_HEAD_BYTES + (size_t)back.count * MP_MOD_MANIFEST_MOD_BYTES >
                 length)) {
            ++failed;
        }
    }
    ut_checkf(failed == 0u, "2000 random byte runs never decode to more than they carry (%u did)",
              failed);
}

/* The fingerprint up to wire 34, written out as the reference: the damage table's rows, then the
 * difficulty, the two cheat cells and the detail level, each folded in as a dword. */
static uint32_t old_fingerprint(uint32_t damage_rows_hash, uint32_t difficulty, uint32_t happy,
                                uint32_t evil, uint32_t detail)
{
    uint32_t hash = damage_rows_hash;
    uint32_t values[4];
    unsigned v;
    unsigned byte;

    values[0] = difficulty;
    values[1] = happy;
    values[2] = evil;
    values[3] = detail;
    for (v = 0; v < 4u; ++v) {
        for (byte = 0; byte < 4u; ++byte) {
            hash ^= (values[v] >> (8u * byte)) & 0xFFu;
            hash *= 16777619u;
        }
    }
    return hash;
}

static void check_the_fingerprint(void)
{
    mp_mod_manifest_t m = a_manifest(0x84273DBBu, 0xE1D7140Du, 0x6ABB2222u);
    mp_mod_manifest_t other;
    mp_mod_manifest_t empty;
    uint32_t          base = mp_mod_manifest_fingerprint(&m);

    ut_section("the statement as one number, for the level's content note");

    ut_check(base != 0u && mp_mod_manifest_fingerprint(&m) == base, "stable and never zero");
    other = m;
    other.damage ^= 1u;
    ut_check(mp_mod_manifest_fingerprint(&other) != base, "another damage table is another number");
    other = m;
    other.roster ^= 1u;
    ut_check(mp_mod_manifest_fingerprint(&other) != base, "another roster is another number");
    other = m;
    other.mods[0].stamp ^= 1u;
    ut_check(mp_mod_manifest_fingerprint(&other) != base,
             "another build of the multiplayer is another number");
    memset(&empty, 0, sizeof empty);
    ut_check(mp_mod_manifest_fingerprint(&empty) == 0u && mp_mod_manifest_fingerprint(NULL) == 0u,
             "a statement of nothing is zero, which nobody compares");

    /* Two machines with the same files and builds and different settings: the old number told them
     * apart, and refused the join; the statement has no field for any of it. */
    ut_check(old_fingerprint(0x1234u, 5u, 0u, 0u, 3u) != old_fingerprint(0x1234u, 4u, 0u, 0u, 3u) &&
                 old_fingerprint(0x1234u, 5u, 0u, 0u, 3u) !=
                     old_fingerprint(0x1234u, 5u, 1u, 0u, 3u) &&
                 old_fingerprint(0x1234u, 5u, 0u, 0u, 3u) !=
                     old_fingerprint(0x1234u, 5u, 0u, 0u, 2u),
             "the old number differed for another difficulty, a cheat or another detail level");
    other = m;
    ut_check(mp_mod_manifest_fingerprint(&other) == base,
             "the statement of two such machines is the same number");
}

/* ===================================== The judgement ====================================== */

static const mp_mod_required_t OLD_LIST[] = {
    { MP_WIRE_MOD_MULTIPLAYER, "multiplayer.dll" },
};

static const mp_mod_required_t LONGER_LIST[] = {
    { MP_WIRE_MOD_MULTIPLAYER, "multiplayer.dll" },
    { 1u, "later_required.dll" },
};

static mp_mod_verdict_t judge(const mp_mod_manifest_t *host, const mp_mod_manifest_t *joiner)
{
    size_t                   count = 0;
    const mp_mod_required_t *table = mp_mod_manifest_required(&count);
    mp_mod_verdict_t         verdict;

    mp_mod_manifest_judge(host, joiner, table, count, &verdict);
    return verdict;
}

static void check_the_judgement(void)
{
    mp_mod_manifest_t host = a_manifest(0x84273DBBu, 0xE1D7140Du, 0x6ABB2222u);
    mp_mod_manifest_t joiner;
    mp_mod_verdict_t  v;

    ut_section("the host's judgement, every case");

    joiner = host;
    v = judge(&host, &joiner);
    ut_check(v.reason == MP_MOD_REFUSE_NONE, "the same files and the same build are admitted");

    joiner = host;
    joiner.damage ^= 0x10u;
    v = judge(&host, &joiner);
    ut_check(v.reason == MP_MOD_REFUSE_GAME_DATA && v.sub == MP_MOD_SUB_DAMAGE &&
                 v.mod == MP_MOD_NO_MOD && v.host_stamp == host.damage &&
                 v.joiner_stamp == joiner.damage,
             "another damage table is refused as game data, damage.txt, with both numbers");

    joiner = host;
    joiner.roster ^= 0x10u;
    v = judge(&host, &joiner);
    ut_check(v.reason == MP_MOD_REFUSE_GAME_DATA && v.sub == MP_MOD_SUB_ROSTER,
             "another roster is refused as game data, characters.ini");

    joiner = host;
    joiner.damage ^= 0x10u;
    joiner.mods[0].stamp ^= 1u;
    v = judge(&host, &joiner);
    ut_check(v.reason == MP_MOD_REFUSE_GAME_DATA,
             "the data files are judged before the builds");

    /* A 0 is a side without the file, and a side without characters.ini does not play the game a
     * side with it plays: the join is refused with the file's name. */
    joiner = host;
    joiner.roster = 0u;
    v = judge(&host, &joiner);
    ut_checkf(v.reason == MP_MOD_REFUSE_GAME_DATA && v.sub == MP_MOD_SUB_ROSTER &&
                  v.host_stamp == 0xE1D7140Du && v.joiner_stamp == 0u,
              "a joiner without characters.ini is refused as game data, characters.ini (reason %u)",
              (unsigned)v.reason);
    v = judge(&joiner, &host);
    ut_check(v.reason == MP_MOD_REFUSE_GAME_DATA && v.sub == MP_MOD_SUB_ROSTER,
             "and so is a joiner with it at a host without it");
    joiner = host;
    joiner.damage = 0u;
    v = judge(&host, &joiner);
    ut_check(v.reason == MP_MOD_REFUSE_GAME_DATA && v.sub == MP_MOD_SUB_DAMAGE,
             "a damage table the joiner could not read is a difference as well");
    joiner = host;
    joiner.damage = 0u;
    joiner.roster = 0u;
    host.damage   = 0u;
    host.roster   = 0u;
    ut_check(judge(&host, &joiner).reason == MP_MOD_REFUSE_NONE,
             "two sides that both have none of either are the same");
    host = a_manifest(0x84273DBBu, 0xE1D7140Du, 0x6ABB2222u);

    joiner = host;
    joiner.mods[0].stamp = 0x6ABB0000u;
    v = judge(&host, &joiner);
    ut_check(v.reason == MP_MOD_REFUSE_MODS && v.sub == MP_MOD_SUB_OTHER_BUILD &&
                 v.mod == MP_WIRE_MOD_MULTIPLAYER && v.host_stamp == 0x6ABB2222u &&
                 v.joiner_stamp == 0x6ABB0000u && v.host_image == v.joiner_image,
             "another build of the multiplayer is refused as a mod, with both stamps");

    joiner = host;
    joiner.mods[0].image += 0x1000u;
    v = judge(&host, &joiner);
    ut_check(v.reason == MP_MOD_REFUSE_MODS && v.sub == MP_MOD_SUB_OTHER_BUILD,
             "the same stamp over another image size is another build too");

    joiner = host;
    joiner.count = 0u;
    v = judge(&host, &joiner);
    ut_check(v.reason == MP_MOD_REFUSE_MODS && v.sub == MP_MOD_SUB_MISSING_AT_JOINER &&
                 v.mod == MP_WIRE_MOD_MULTIPLAYER,
             "a statement without the multiplayer is refused as missing at the joiner");

    joiner = host;
    joiner.count = 2u;
    joiner.mods[1].id    = 0xC7u;
    joiner.mods[1].stamp = 1u;
    v = judge(&host, &joiner);
    ut_check(v.reason == MP_MOD_REFUSE_NONE,
             "a mod whose number this build does not know never changes the verdict");
}

/* A statement one build of this tree could make: each data value sometimes 0, the multiplayer
 * sometimes absent, and sometimes a number this build's list does not know behind it. */
static mp_mod_manifest_t random_statement(uint32_t *state)
{
    mp_mod_manifest_t m;

    memset(&m, 0, sizeof m);
    m.damage = (next_random(state) % 4u) == 0u ? 0u : next_random(state);
    m.roster = (next_random(state) % 4u) == 0u ? 0u : next_random(state);
    if ((next_random(state) % 5u) != 0u) {
        m.mods[m.count].id    = MP_WIRE_MOD_MULTIPLAYER;
        m.mods[m.count].stamp = next_random(state);
        m.mods[m.count].image = next_random(state);
        ++m.count;
    }
    if ((next_random(state) % 3u) == 0u) {
        m.mods[m.count].id    = (uint8_t)(0x80u + next_random(state) % 0x70u);
        m.mods[m.count].stamp = next_random(state);
        m.mods[m.count].image = next_random(state);
        ++m.count;
    }
    return m;
}

/* The joiner a host meets: the same statement, or the same with exactly one side's data value
 * zeroed, another build, the multiplayer missing, or an extra number neither list knows. */
static mp_mod_manifest_t a_joiner_for(const mp_mod_manifest_t *host, uint32_t *state)
{
    mp_mod_manifest_t joiner = *host;

    switch (next_random(state) % 6u) {
    case 0u:
        joiner.damage = joiner.damage != 0u ? 0u : next_random(state) | 1u;
        break;
    case 1u:
        joiner.roster = joiner.roster != 0u ? 0u : next_random(state) | 1u;
        break;
    case 2u:
        if (joiner.count != 0u) {
            joiner.mods[0].stamp ^= 1u;
        }
        break;
    case 3u:
        if (joiner.count < MP_MOD_MANIFEST_MAX_MODS) {
            joiner.mods[joiner.count].id    = 0xF0u;
            joiner.mods[joiner.count].stamp = next_random(state);
            ++joiner.count;
        }
        break;
    case 4u:
        joiner.count = 0u;
        break;
    default:
        break;
    }
    return joiner;
}

/* The join and the level's content note must be one judgement: a pair the judge admits must never
 * be told apart by the fingerprint the note carries, or the session a join let in ends a moment
 * later for a difference nobody was refused for. */
static void check_admitted_pairs_share_a_fingerprint(void)
{
    uint32_t state = 0xC117CA22u;
    unsigned round;
    unsigned admitted = 0u;
    unsigned refused_for_a_zero = 0u;
    unsigned told_apart = 0u;

    ut_section("every pair the judge admits carries one fingerprint");

    for (round = 0; round < 2000u; ++round) {
        mp_mod_manifest_t host = random_statement(&state);
        mp_mod_manifest_t joiner = a_joiner_for(&host, &state);
        mp_mod_verdict_t  v = judge(&host, &joiner);

        if (v.reason == MP_MOD_REFUSE_NONE) {
            ++admitted;
            if (mp_mod_manifest_fingerprint(&host) != mp_mod_manifest_fingerprint(&joiner)) {
                ++told_apart;
            }
        } else if (v.reason == MP_MOD_REFUSE_GAME_DATA &&
                   (v.host_stamp == 0u || v.joiner_stamp == 0u)) {
            ++refused_for_a_zero;
        }
    }
    ut_checkf(told_apart == 0u,
              "no admitted pair has two fingerprints (%u of %u admitted did)", told_apart,
              admitted);
    ut_checkf(admitted > 200u && refused_for_a_zero > 100u,
              "and the run is not vacuous: %u admitted, %u refused for a 0 on one side", admitted,
              refused_for_a_zero);
}

/* Two builds of different age: the host's list names a second required mod the joiner's older
 * list does not know. The refusal has to name it all the same, by text, on the joining side. */
static void check_two_lists_of_different_length(void)
{
    mp_mod_manifest_t host = a_manifest(0x84273DBBu, 0xE1D7140Du, 0x6ABB2222u);
    mp_mod_manifest_t joiner = host;
    mp_mod_manifest_t older_host = host;
    mp_mod_verdict_t  v;
    mp_mod_verdict_t  read;
    uint8_t           detail[MP_MOD_REFUSAL_DETAIL_BYTES];
    char              name[MP_MOD_REFUSAL_NAME_MAX];
    char              version[MP_MOD_REFUSAL_VERSION_MAX];
    size_t            length;
    const char       *host_name;

    ut_section("a refusal between two lists of different length names the right mod");

    host.count         = 2u;
    host.mods[1].id    = 1u;
    host.mods[1].stamp = 0x6ABB3333u;
    host.mods[1].image = 0x00020000u;
    mp_mod_manifest_judge(&host, &joiner, LONGER_LIST, 2u, &v);
    ut_check(v.reason == MP_MOD_REFUSE_MODS && v.sub == MP_MOD_SUB_MISSING_AT_JOINER &&
                 v.mod == 1u,
             "the newer host refuses the older joiner for the mod only its own list names");
    host_name = mp_mod_manifest_required_name(LONGER_LIST, 2u, v.mod);
    length = mp_mod_refusal_detail_encode(&v, host_name, "0.4.5", detail, sizeof detail);
    ut_check(length == MP_MOD_REFUSAL_DETAIL_BYTES, "the detail is written whole");
    memset(&read, 0, sizeof read);
    ut_check(mp_mod_refusal_detail_decode(detail, length, &read, name, version) &&
                 read.mod == 1u && read.sub == MP_MOD_SUB_MISSING_AT_JOINER &&
                 read.host_stamp == 0x6ABB3333u,
             "the joiner reads the detail back");
    ut_check(mp_mod_manifest_required_name(OLD_LIST, 1u, read.mod) == NULL &&
                 strcmp(name, "later_required.dll") == 0 && strcmp(version, "0.4.5") == 0,
             "its own list does not know the number, and the text names the mod and the release");

    joiner = host;
    mp_mod_manifest_judge(&older_host, &joiner, OLD_LIST, 1u, &v);
    ut_check(v.reason == MP_MOD_REFUSE_NONE,
             "the older host admits the newer joiner: a number its list does not know is passed "
             "over");
    mp_mod_manifest_judge(&older_host, &joiner, LONGER_LIST, 2u, &v);
    ut_check(v.reason == MP_MOD_REFUSE_MODS && v.sub == MP_MOD_SUB_MISSING_AT_HOST && v.mod == 1u &&
                 v.joiner_stamp == 0x6ABB3333u,
             "and a host whose list requires it but has not got it says it is missing at the host");
}

static void check_the_detail(void)
{
    mp_mod_verdict_t v;
    mp_mod_verdict_t read;
    uint8_t          detail[MP_MOD_REFUSAL_DETAIL_BYTES];
    char             name[MP_MOD_REFUSAL_NAME_MAX];
    char             version[MP_MOD_REFUSAL_VERSION_MAX];
    size_t           length;
    size_t           i;

    ut_section("a refusal's detail against a stranger's bytes");

    memset(&v, 0, sizeof v);
    v.reason = MP_MOD_REFUSE_GAME_DATA;
    v.sub    = MP_MOD_SUB_ROSTER;
    v.mod    = MP_MOD_NO_MOD;
    v.host_stamp = 0xE1D7140Du;
    length = mp_mod_refusal_detail_encode(&v, "characters.ini", "", detail, sizeof detail);
    ut_check(mp_mod_refusal_detail_decode(detail, length, &read, name, version) &&
                 read.sub == MP_MOD_SUB_ROSTER && read.mod == MP_MOD_NO_MOD &&
                 read.host_stamp == 0xE1D7140Du && strcmp(name, "characters.ini") == 0 &&
                 version[0] == '\0',
             "a data file's refusal goes round with its name and no version");
    ut_check(!mp_mod_refusal_detail_decode(detail, length - 1u, &read, name, version),
             "a detail cut short is refused");
    ut_check(mp_mod_refusal_detail_encode(&v, "a", "b", detail, sizeof detail - 1u) == 0u,
             "and one that does not fit is not written");

    length = mp_mod_refusal_detail_encode(
        &v, "a_name_far_longer_than_thirty_one_characters.dll", "0.4.4 and a long tail to it",
        detail, sizeof detail);
    ut_check(mp_mod_refusal_detail_decode(detail, length, &read, name, version) &&
                 strlen(name) == MP_MOD_REFUSAL_NAME_MAX - 1u &&
                 strlen(version) == MP_MOD_REFUSAL_VERSION_MAX - 1u,
             "a name and a version longer than their fields are cut, and terminated");

    for (i = 0; i < sizeof detail; ++i) {
        detail[i] = (uint8_t)(0x80u + i);
    }
    ut_check(mp_mod_refusal_detail_decode(detail, sizeof detail, &read, name, version) &&
                 name[MP_MOD_REFUSAL_NAME_MAX - 1u] == '\0' &&
                 version[MP_MOD_REFUSAL_VERSION_MAX - 1u] == '\0',
             "bytes a stranger wrote come back terminated");
    for (i = 0; name[i] != '\0'; ++i) {
        ut_checkf(name[i] >= 0x20 && name[i] < 0x7F, "and printable, character %u",
                  (unsigned)i);
    }
}

/* ===================================== The release's mods ================================= */

static void check_the_table(void)
{
    size_t                   count = 0;
    const mp_mod_known_t    *known = mp_mod_manifest_known(&count);
    size_t                   required_count = 0;
    const mp_mod_required_t *required = mp_mod_manifest_required(&required_count);
    size_t                   required_in_table = 0;
    size_t                   i;
    size_t                   j;

    ut_section("the table of the release's mods is a decision per mod");

    ut_checkf(count == 25u, "it names the release's 25 mods (%u)", (unsigned)count);
    for (i = 0; i < count; ++i) {
        const char *c;
        bool        one_word = known[i].name != NULL && known[i].name[0] != '\0';

        for (c = known[i].name; one_word && *c != '\0'; ++c) {
            one_word = (*c >= 'a' && *c <= 'z') || (*c >= '0' && *c <= '9') || *c == '_';
        }
        ut_checkf(one_word, "entry %u is one lower case word, the directory, the DLL and the "
                  "section (%s)", (unsigned)i, known[i].name != NULL ? known[i].name : "(null)");
        ut_checkf(known[i].why != NULL && known[i].why[0] != '\0' &&
                      (unsigned)known[i].mod_class < (unsigned)MP_MOD_CLASS_COUNT,
                  "entry %u has a class and a reason", (unsigned)i);
        for (j = 0; j < i; ++j) {
            ut_checkf(strcmp(known[i].name, known[j].name) != 0, "entry %u is not a repeat of %u",
                      (unsigned)i, (unsigned)j);
        }
        if (known[i].mod_class == MP_MOD_CLASS_REQUIRED) {
            ++required_in_table;
        }
    }
    ut_check(required_in_table == required_count,
             "every required mod of the table is on the list the join states, and no other");
    for (i = 0; i < required_count; ++i) {
        char stem[64];
        size_t length = strlen(required[i].dll);

        ut_check(length > 4u && length < sizeof stem, "a required mod names its file");
        memcpy(stem, required[i].dll, length - 4u);
        stem[length - 4u] = '\0';
        ut_checkf(mp_mod_manifest_find_known(stem) != NULL &&
                      mp_mod_manifest_find_known(stem)->mod_class == MP_MOD_CLASS_REQUIRED,
                  "%s stands in the table as required", required[i].dll);
    }
    ut_check(mp_mod_manifest_find_known("MultiPlayer") != NULL &&
                 mp_mod_manifest_find_known("reshade") == NULL &&
                 mp_mod_manifest_find_known(NULL) == NULL,
             "a stem is found without regard to case, and a stranger's is not");
}

int main(void)
{
    check_the_codec();
    check_the_fingerprint();
    check_the_judgement();
    check_admitted_pairs_share_a_fingerprint();
    check_two_lists_of_different_length();
    check_the_detail();
    check_the_table();

    return ut_summary("mp_mod_manifest_rule");
}
