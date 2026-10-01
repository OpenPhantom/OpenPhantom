/* mp_mod_manifest_rule.h: what two machines must hold the same to play together, and nothing more.
 *
 * Layer 1, pure. A client simulates almost nothing of the shared world: the host does, and since
 * the client parks its own activation scan, its difficulty, detail level and ini settings decide
 * nothing the host sees. What a client still runs for the host is its own copy of the multiplayer,
 * which reads every message, and the two data files that give the messages their meaning: the
 * damage table out of damage.txt, and the roster out of characters.ini, which decides what a clip
 * number on the wire means. Those three are what a join is judged by. Everything else a player
 * installs or sets is theirs, with one rule on top: a DLL out of the mods folder that is not of
 * this release is let into a session only by the host's [multiplayer] AllowMods.
 *
 * So a join request states: the two data fingerprints, and for each REQUIRED mod its build, the
 * linker's time stamp and the image size. The release number cannot tell two builds apart, the
 * stamp can. The host compares the statement against its own and refuses with the file or the mod
 * that differs, so a player reads a name and a date rather than a number. Behind the mods the
 * request lists the DLLs outside this release that the joining side has loaded, and the host holds
 * them against its own list.
 *
 * A mod is named on the wire by a number from mp_wire.h that never changes meaning. Two builds of
 * different age can carry lists of different length; a number a build does not know is passed over,
 * and a refusal carries the name as text as well, so the joining side can say what it was even when
 * its own list is the shorter one.
 *
 * The table of this release's mods sits here too, with what each of them is to a session, for the
 * census that names every DLL of the process. It is written one mod at a time, like the table it
 * replaces, because it is a decision and not a listing.
 */
#ifndef MULTIPLAYER_MP_MOD_MANIFEST_RULE_H
#define MULTIPLAYER_MP_MOD_MANIFEST_RULE_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

/* ======================================== The statement ======================================= */

/* One required mod as a join request names it. */
typedef struct mp_mod_build {
    uint8_t  id;       /* MP_WIRE_MOD_* */
    uint32_t stamp;    /* the linker's time stamp out of the file header */
    uint32_t image;    /* SizeOfImage out of the optional header */
} mp_mod_build_t;

/* How many required mods one statement can name. The release has one; eight leaves room for a
 * list that grows, and the statement still fits the request's padding many times over. */
#define MP_MOD_MANIFEST_MAX_MODS 8u

/* The statement: the damage table and the roster, and the builds of the required mods this side
 * has loaded. A 0 for a data file says this side has none to show (no characters.ini beside the
 * game, or a shot table that did not resolve); it is a value like any other and differs from every
 * other, because a side without the file does not play the same game as a side with it. */
typedef struct mp_mod_manifest {
    uint32_t       damage;
    uint32_t       roster;
    uint8_t        count;
    mp_mod_build_t mods[MP_MOD_MANIFEST_MAX_MODS];
} mp_mod_manifest_t;

/* On the wire: damage u32, roster u32, count u8, then count x (id u8, stamp u32, image u32). */
#define MP_MOD_MANIFEST_HEAD_BYTES 9u
#define MP_MOD_MANIFEST_MOD_BYTES  9u
#define MP_MOD_MANIFEST_MAX_BYTES \
    (MP_MOD_MANIFEST_HEAD_BYTES + MP_MOD_MANIFEST_MAX_MODS * MP_MOD_MANIFEST_MOD_BYTES)

/* The statement's bytes, 0 when it does not fit `capacity` or names more mods than it may. */
size_t mp_mod_manifest_encode(const mp_mod_manifest_t *manifest, uint8_t *out, size_t capacity);

/* A stranger's bytes back. False for a statement that is cut short, says more mods than it carries
 * or more than MP_MOD_MANIFEST_MAX_MODS; bytes behind the last mod are a later build's and are not
 * read. */
bool mp_mod_manifest_decode(const uint8_t *bytes, size_t length, mp_mod_manifest_t *out);

/* The statement as one number, for the level's content note and the LAN announce. It folds exactly
 * what mp_mod_manifest_judge compares, with this build's list of required mods: the two data values
 * as they stand, then for every mod of the list in the list's order its build or its absence. A
 * number the list does not know is left out, as the judge passes it over. So two statements the
 * judge admits always give the same number, and the check a level makes cannot end a session the
 * join let in. 0 only for a statement that holds nothing it folds: no data and no mod of the list;
 * any other hash that lands on 0 is 1. The list of DLLs outside this release is not folded: two
 * machines with different DLLs of their own agree on the number, so the note never ends a join the
 * host's list let in. */
uint32_t mp_mod_manifest_fingerprint(const mp_mod_manifest_t *manifest);

/* ======================================== The judgement ======================================= */

/* The reasons a refusal carries, the same numbers as the session's MP_DENY_CONTENT, MP_DENY_MODS
 * and MP_DENY_FOREIGN_DLL. The session layer is above this one, so the numbers are repeated here
 * and the bridge, which includes both, asserts that they agree. */
#define MP_MOD_REFUSE_NONE      0u
#define MP_MOD_REFUSE_GAME_DATA 3u
#define MP_MOD_REFUSE_MODS      7u
#define MP_MOD_REFUSE_FOREIGN   8u

/* What differs, below the reason. Game data: which file. Mods: which side lacks it, or both have
 * it and the builds differ. The joining side is "here" when a client reads a refusal. */
#define MP_MOD_SUB_DAMAGE            1u
#define MP_MOD_SUB_ROSTER            2u
#define MP_MOD_SUB_MISSING_AT_HOST   1u
#define MP_MOD_SUB_MISSING_AT_JOINER 2u
#define MP_MOD_SUB_OTHER_BUILD       3u

/* A DLL outside this release: one the host's list does not name, more of them than the request
 * named, or a request with no readable list of them. */
#define MP_MOD_SUB_NOT_ALLOWED 1u
#define MP_MOD_SUB_NOT_NAMED   2u
#define MP_MOD_SUB_NO_LIST     3u

/* No mod, in a verdict about a data file or a list. */
#define MP_MOD_NO_MOD 0xFFu

typedef struct mp_mod_verdict {
    uint8_t  reason;       /* MP_MOD_REFUSE_*; NONE admits */
    uint8_t  sub;          /* MP_MOD_SUB_* */
    uint8_t  mod;          /* MP_WIRE_MOD_*, MP_MOD_NO_MOD for a data file, and for a DLL outside
                            * this release its place in the request's list of names */
    uint32_t host_stamp;   /* the host's build stamp, or its fingerprint of the data file */
    uint32_t host_image;   /* the host's image size; 0 for a data file */
    uint32_t joiner_stamp; /* the same of the joining side, for the host's own line */
    uint32_t joiner_image;
} mp_mod_verdict_t;

/* One required mod of a build: the number it travels under and its file. */
typedef struct mp_mod_required {
    uint8_t     id;
    const char *dll;
} mp_mod_required_t;

/* This build's list of required mods. */
const mp_mod_required_t *mp_mod_manifest_required(size_t *count);

/* The file name of a required mod by its number in `table`, NULL for a number it does not know. */
const char *mp_mod_manifest_required_name(const mp_mod_required_t *table, size_t count,
                                          uint8_t id);

/* The host's judgement of a joining side's statement against its own, `table` being the host's
 * list of required mods. The data files first, because a different roster makes every clip on the
 * wire mean something else whatever the builds are; a 0 on one side against a value on the other
 * is a difference like any other. Then every required mod the host has: missing at the joiner, or
 * another build. Then every mod the joiner names that the host's list requires and the host lacks.
 * A number the host's list does not know never changes the verdict. */
void mp_mod_manifest_judge(const mp_mod_manifest_t *host, const mp_mod_manifest_t *joiner,
                           const mp_mod_required_t *table, size_t table_count,
                           mp_mod_verdict_t *out);

/* ================================ The DLLs outside this release =============================== */

/* A whole statement, its mods and the list behind them, is never longer than this. A host takes a
 * longer one for no statement at all and admits its sender unjudged, so the limit is the room a
 * request has for a statement, and the bridge holds the two equal. */
#define MP_MOD_STATEMENT_MAX_BYTES 128u

/* Behind the last mod: form u8, judged u8, count u8, stated u8, then `stated` x (length u8 1 to
 * 31, that many bytes of printable ASCII). `count` is every DLL outside this release the side has
 * loaded from its mods folder, 255 for 255 and more; `stated` is how many of them follow by name.
 * `judged` 0 says the side has no release number of its own and judged nothing. A later form is
 * another number in the first byte, and bytes behind the last name are a later build's and are not
 * read. */
#define MP_MOD_FOREIGN_FORM       1u
#define MP_MOD_FOREIGN_HEAD_BYTES 4u
#define MP_MOD_FOREIGN_NAMES_MAX  8u
#define MP_MOD_FOREIGN_NAME_MAX   32u   /* 31 characters and the terminator */

/* How the decoder found the list. */
#define MP_MOD_FOREIGN_ABSENT    0u   /* nothing behind the mods */
#define MP_MOD_FOREIGN_SOUND     1u
#define MP_MOD_FOREIGN_MALFORMED 2u

/* The list as the decoder read it. */
typedef struct mp_mod_foreign {
    uint8_t form;     /* MP_MOD_FOREIGN_ABSENT, _SOUND or _MALFORMED */
    uint8_t judged;   /* 0: the side has no release number of its own and judged nothing */
    uint8_t count;    /* every DLL outside this release it has loaded from the mods folder */
    uint8_t stated;   /* how many of them follow by name */
    char    names[MP_MOD_FOREIGN_NAMES_MAX][MP_MOD_FOREIGN_NAME_MAX];
} mp_mod_foreign_t;

/* Writes the list into `out`, the room behind the statement's mods, and answers its length, 0 when
 * not even its head fits. `names` are the `listed` DLLs the census holds, in its order, each of any
 * length, and `count` is every one it found, which may be more. A name is stated whole or not at
 * all: one over 31 characters, one with a byte outside printable ASCII, the ninth and any that does
 * not fit the room is counted and not named. `stated` receives for each of the `listed` names
 * whether it was named, and may be NULL. */
size_t mp_mod_foreign_encode(const char *const *names, size_t listed, size_t count, bool judged,
                             uint8_t *out, size_t room, bool *stated);

/* Reads the list of a statement of `length` bytes whose mods end at `base_end`, where
 * mp_mod_manifest_decode stopped. MP_MOD_FOREIGN_ABSENT when nothing follows the mods,
 * MP_MOD_FOREIGN_MALFORMED for anything that breaks the form above, with everything else zero. The
 * mods are read either way: a broken list never makes the statement unreadable, because the judge
 * admits an unreadable statement unjudged. */
void mp_mod_foreign_decode(const uint8_t *bytes, size_t length, size_t base_end,
                           mp_mod_foreign_t *out);

/* The one rule for which DLLs outside this release a session takes, over any list of names: the
 * index of the first of the `count` names that `allow_list`, the value of [multiplayer] AllowMods,
 * does not name, or `count` when it names them all. The host asks it of its own whole list before
 * it hosts, and the judge of the names a request states. */
size_t mp_mod_foreign_first_refused(const char *const *names, size_t count,
                                    const char *allow_list);

/* The host's verdict on the list a joining side stated, against `allow_list`, in this order: no
 * readable list refuses (MP_MOD_SUB_NO_LIST); a side that judged nothing is admitted; the first
 * name the list does not name refuses (MP_MOD_SUB_NOT_ALLOWED, `mod` its place in the list); more
 * DLLs than names refuses (MP_MOD_SUB_NOT_NAMED), because the rest cannot be held against the
 * list; anything else is admitted. Stamps and sizes stay 0. */
void mp_mod_foreign_judge(const mp_mod_foreign_t *foreign, const char *allow_list,
                          mp_mod_verdict_t *out);

/* The names as a log line lists them: the first MP_MOD_FOREIGN_NAMES_MAX that fit `capacity`
 * whole, separated by a comma, then how many more there are; "none" for no names. Reads no more
 * than the first MP_MOD_FOREIGN_NAMES_MAX entries of `names`. Answers `out`. */
const char *mp_mod_foreign_list(const char *const *names, size_t count, char *out,
                                size_t capacity);

/* ==================================== The refusal's detail ==================================== */

#define MP_MOD_REFUSAL_NAME_MAX    32u
#define MP_MOD_REFUSAL_VERSION_MAX 24u

/* Behind the reason byte of a refusal: sub u8, mod u8, host stamp u32, host image u32, the name as
 * 32 bytes of text and the host's release number as 24. */
#define MP_MOD_REFUSAL_DETAIL_BYTES \
    (1u + 1u + 4u + 4u + MP_MOD_REFUSAL_NAME_MAX + MP_MOD_REFUSAL_VERSION_MAX)

/* The detail of `verdict` with `name` and `version`, cut to their fields. 0 when it does not
 * fit. */
size_t mp_mod_refusal_detail_encode(const mp_mod_verdict_t *verdict, const char *name,
                                    const char *version, uint8_t *out, size_t capacity);

/* A stranger's detail back: the host's side of `verdict` (reason and the joiner's fields are left
 * as they were), the name and the version, each cleaned to printable ASCII and terminated. False
 * for a detail cut short. */
bool mp_mod_refusal_detail_decode(const uint8_t *bytes, size_t length, mp_mod_verdict_t *verdict,
                                  char name[MP_MOD_REFUSAL_NAME_MAX],
                                  char version[MP_MOD_REFUSAL_VERSION_MAX]);

/* What a client knows about why its join was refused, for the screen that says it.
 *
 * For a mod (reason MP_MOD_REFUSE_MODS) `mod` is its file name, the versions are the release
 * numbers of the two sides and the stamps are the linkers' time stamps, which a screen shows as a
 * date. For a data file (reason MP_MOD_REFUSE_GAME_DATA) `mod` is the file's name, the versions are
 * empty and the stamps are the two sides' fingerprints of the file, which are no dates and are
 * shown as eight hex digits. A stamp of 0 on this side means this side does not have the mod.
 *
 * For a DLL outside this release (reason MP_MOD_REFUSE_FOREIGN) `mod` is its file name: under
 * MP_MOD_SUB_NOT_ALLOWED the one the host named, under MP_MOD_SUB_NOT_NAMED the first of this
 * side's own that its request could not name, under MP_MOD_SUB_NO_LIST empty. `host_version` is the
 * host's release number and `host_stamp` 0. `here_stamp` is this side's linker stamp of that DLL,
 * and `here_version` its release number only when it is another release of this project, since any
 * other DLL's own version says nothing here; both stay empty when this side's census does not know
 * the name. */
typedef struct mp_mod_refusal {
    uint8_t  reason;
    uint8_t  sub;
    char     mod[MP_MOD_REFUSAL_NAME_MAX];
    char     host_version[MP_MOD_REFUSAL_VERSION_MAX];
    char     here_version[MP_MOD_REFUSAL_VERSION_MAX];
    uint32_t host_stamp;
    uint32_t here_stamp;
} mp_mod_refusal_t;

/* ===================================== The release's mods ===================================== */

/* What a mod of this release is to a session. */
typedef enum mp_mod_class {
    MP_MOD_CLASS_REQUIRED = 0,   /* the same build on every machine, or no session */
    MP_MOD_CLASS_HOST_SETTINGS,  /* in a session the host's values count, never written here */
    MP_MOD_CLASS_HOST_ONLY,      /* changes only what the host simulates; free on a client */
    MP_MOD_CLASS_LOCAL,          /* picture, sound, window, input, diagnosis: this machine's own */
    MP_MOD_CLASS_COUNT
} mp_mod_class_t;

typedef struct mp_mod_known {
    const char    *name;      /* the directory, the DLL's stem and the ini section, one word */
    mp_mod_class_t mod_class;
    bool           partner;   /* the multiplayer works with it through a note; without it a part
                               * of the session is missing on that machine, the world is not */
    const char    *why;       /* for the census line */
} mp_mod_known_t;

const mp_mod_known_t *mp_mod_manifest_known(size_t *count);

/* The entry for a DLL's stem, compared without regard to case; NULL for a mod the table does not
 * name. */
const mp_mod_known_t *mp_mod_manifest_find_known(const char *stem);

#endif /* MULTIPLAYER_MP_MOD_MANIFEST_RULE_H */
