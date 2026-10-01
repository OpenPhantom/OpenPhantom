/* common/mod_identity.h: whether a DLL in the mods folder is one of this release's own mods, and
 * which build of it.
 *
 * Every mod of a release carries a version resource with the project's name as its company, its own
 * directory name as its internal name and the release's number as its file version. That is what
 * tells an own mod from anything else in the folder: a file name can be copied, and a mod entry
 * point is exported by any DLL written for the loader, a test helper included. The resource is a
 * guard against accident, not against intent, and it does not need to be more.
 *
 * The release number is the same for every build of a release, so it cannot tell two builds apart.
 * The linker's time stamp in the file header can, together with the size of the image, and that is
 * what "the same build" means where two machines have to run identical code.
 *
 * The classification is pure and takes strings, so a test can prove every case without a file; the
 * reader below fills the strings from a module already loaded, with no file access.
 */
#ifndef COMMON_MOD_IDENTITY_H
#define COMMON_MOD_IDENTITY_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

/* The company name every mod of this project carries in its version resource. */
#define MOD_IDENTITY_COMPANY "OpenPhantom"

#define MOD_IDENTITY_NAME_MAX    32u
#define MOD_IDENTITY_VERSION_MAX 24u

typedef enum mod_identity_kind {
    MOD_IDENTITY_UNREADABLE = 0,   /* no file, no module, or a header that does not parse */
    MOD_IDENTITY_FOREIGN,          /* no version resource, or one of another company */
    MOD_IDENTITY_MISNAMED,         /* this project's, but its internal name is not its file name */
    MOD_IDENTITY_OTHER_RELEASE,    /* this project's and correctly named, another release number */
    MOD_IDENTITY_OWN               /* this project's, correctly named, this release */
} mod_identity_kind_t;

typedef struct mod_identity {
    mod_identity_kind_t kind;
    char     internal_name[MOD_IDENTITY_NAME_MAX];   /* empty when there is no resource */
    char     file_version[MOD_IDENTITY_VERSION_MAX]; /* as the resource spells it, "1.0.0" */
    uint32_t time_stamp;                             /* the linker's, from the file header */
    uint32_t image_size;                             /* SizeOfImage, from the optional header */
} mod_identity_t;

/* Pure. `file_stem` is the file name without its folder and without ".dll", compared without regard
 * to case; `own_version` is the release number of whoever asks, out of its own resource. Any NULL
 * or empty company counts as foreign. An empty or NULL `own_version` never compares equal: a mod
 * that is right in every other way is MOD_IDENTITY_OTHER_RELEASE, and a caller that asks without a
 * number has to know that. */
mod_identity_kind_t mod_identity_classify(const char *company, const char *internal_name,
                                          const char *file_stem, const char *file_version,
                                          const char *own_version);

/* Reads a module already loaded in this process from its mapped image, with no file access, and
 * classifies it against `own_version`. `module` is an HMODULE; it is passed as a pointer so this
 * header stays free of windows.h. False when its headers are not a PE image's, and `out->kind` is
 * then MOD_IDENTITY_UNREADABLE. */
bool mod_identity_of_module(const void *module, const char *file_stem, const char *own_version,
                            mod_identity_t *out);

/* The release number of the module that contains `address_inside`, read from its own version
 * resource into `out`. False, with `out` empty, for a build that carries no resource. This is how
 * a DLL learns what "this release" is without a build-time constant. */
bool mod_identity_own_version(const void *address_inside, char *out, size_t capacity);

/* Whether `file_name`, a DLL's name in the mods folder with ".dll", stands in `list`, the value of
 * [multiplayer] AllowMods: names separated by commas, each compared without regard to case with the
 * blanks and tabs around it ignored, and each naming the file or its stem, so "my_mod" and
 * "my_mod.dll" both name the same file. An empty or NULL list names nothing, and a file name with
 * a comma in it is never named. The one rule for that key, for a host before it hosts and for its
 * judge of a join. */
bool mod_identity_name_listed(const char *list, const char *file_name);

#endif /* COMMON_MOD_IDENTITY_H */
