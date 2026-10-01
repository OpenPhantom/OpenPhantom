/* mp_text_second.c: the second table of the multiplayer's own texts, every id from
 * MP_TEXT_SECOND_TABLE_FIRST to the count. See the header.
 *
 * The first table, mp_text.c, stood near the size limit and is nothing but rows; a seam by screen
 * would part rows the test measures together. So the table is split by id: a new text goes at the
 * end of the enum and lands here, and mp_text_row asks the table an id belongs to. The first rows
 * here are the ones that stood last in mp_text.c, the two words of the window's caption and the
 * chat's prompt, moved so that this table and the switch in front of it are read from the day they
 * exist and not from the day the first new text arrives.
 *
 * Every cell is printable ASCII, for the reason mp_text.c gives: the menu fonts' width tables stop
 * at 0x7F.
 */
#include "mp_text.h"

#include <stddef.h>
#include <stdint.h>

/* Indexed from the marker, so a row stands at its id less MP_TEXT_SECOND_TABLE_FIRST. */
static const mp_text_row_t ROWS[MP_TEXT_COUNT - MP_TEXT_SECOND_TABLE_FIRST] = {
    /* What the window's caption says this machine is, during a session. */
    [MP_TEXT_CAPTION_HOST - MP_TEXT_SECOND_TABLE_FIRST] = {
        { "HOST", "HOST", "HOTE", "HOST", "ANFITRION" },
        MP_TEXT_FONT_NONE, 0u },
    [MP_TEXT_CAPTION_CLIENT - MP_TEXT_SECOND_TABLE_FIRST] = {
        { "CLIENT", "CLIENT", "CLIENT", "CLIENT", "CLIENTE" },
        MP_TEXT_FONT_NONE, 0u },

    /* The chat's input row begins with it. */
    [MP_TEXT_CHAT_SAY - MP_TEXT_SECOND_TABLE_FIRST] = {
        { "Say:", "Sagen:", "Dire :", "Di':", "Decir:" }, MP_TEXT_FONT_NONE, 0u },

    /* The band of a client whose lobby opened on a session that already runs. Its start waits for
     * this player's ready, and the band says so; the word for ready is the one the player table
     * shows in its column in each language. Both are drawn whole in the band's 400 pixels. */
    [MP_TEXT_BAND_RUNNING_PICK - MP_TEXT_SECOND_TABLE_FIRST] = {
        { "The session is on. Pick a hero, then READY.",
          "Die Sitzung laeuft. Held waehlen, dann BEREIT.",
          "Session en cours. Choisir un heros, puis PRET.",
          "Sessione in corso. Scegli l'eroe, poi PRONTO.",
          "Sesion en curso. Elige un heroe, luego LISTO." },
        MP_TEXT_FONT_SYSFONT, 400u },
    [MP_TEXT_BAND_RUNNING_READY - MP_TEXT_SECOND_TABLE_FIRST] = {
        { "Ready, here we go.",
          "Bereit, es geht los.",
          "Pret, c'est parti.",
          "Pronto, si parte.",
          "Listo, alla vamos." },
        MP_TEXT_FONT_SYSFONT, 400u },

    /* A join the host refused for a required mod and could not say which. A host of this build
     * always says; an older one, or a server in between, may not. */
    [MP_TEXT_DENY_MODS - MP_TEXT_SECOND_TABLE_FIRST] = {
        { "A required mod differs from the host's",
          "Ein noetiger Mod weicht vom Host ab",
          "Un mod requis differe de celui de l'hote",
          "Una mod richiesta differisce dall'host",
          "Un mod necesario difiere del anfitrion" },
        MP_TEXT_FONT_SYSFONT, 400u },

    /* What differs, on the band, with the file's name in it: a mod or a data file the host has in
     * another build or with other contents, a mod the host lacks, a mod this side lacks. The name
     * is the file's own and is never translated. The longest a name is today,
     * enhanced_resolution.dll, fits the band's 400 pixels in every language. */
    [MP_TEXT_REFUSED_OTHER - MP_TEXT_SECOND_TABLE_FIRST] = {
        { "The host has another %s",
          "Beim Host ist %s anders",
          "L'hote a un autre %s",
          "L'host ha un altro %s",
          "El anfitrion tiene otro %s" },
        MP_TEXT_FONT_SYSFONT, 400u },
    [MP_TEXT_REFUSED_MISSING_AT_HOST - MP_TEXT_SECOND_TABLE_FIRST] = {
        { "The host does not have %s",
          "Beim Host fehlt %s",
          "L'hote n'a pas %s",
          "Sull'host manca %s",
          "Al anfitrion le falta %s" },
        MP_TEXT_FONT_SYSFONT, 400u },
    [MP_TEXT_REFUSED_MISSING_HERE - MP_TEXT_SECOND_TABLE_FIRST] = {
        { "Missing here: %s",
          "Hier fehlt %s",
          "Il manque ici %s",
          "Qui manca %s",
          "Aqui falta %s" },
        MP_TEXT_FONT_SYSFONT, 400u },

    /* The two rows of the player list under a refusal: each side's release and build date, or the
     * two fingerprints of a data file, or the word for a side that lacks the mod. The values are
     * numbers and dates and are the same in every language. */
    [MP_TEXT_REFUSED_HOST_ROW - MP_TEXT_SECOND_TABLE_FIRST] = {
        { "Host: %s", "Host: %s", "Hote : %s", "Host: %s", "Anfitrion: %s" },
        MP_TEXT_FONT_COURIER, 288u },
    [MP_TEXT_REFUSED_HERE_ROW - MP_TEXT_SECOND_TABLE_FIRST] = {
        { "Here: %s", "Hier: %s", "Ici : %s", "Qui: %s", "Aqui: %s" },
        MP_TEXT_FONT_COURIER, 288u },
    [MP_TEXT_REFUSED_MISSING - MP_TEXT_SECOND_TABLE_FIRST] = {
        { "missing", "fehlt", "absent", "manca", "falta" },
        MP_TEXT_FONT_NONE, 0u },

    /* A join refused for a DLL outside this release. The word stands when the refusal names no
     * DLL: a request that carried no readable list, or one this side cannot name. */
    [MP_TEXT_DENY_FOREIGN_DLL - MP_TEXT_SECOND_TABLE_FIRST] = {
        { "The host refuses a DLL loaded here",
          "Der Host lehnt eine DLL von hier ab",
          "L'hote refuse une DLL d'ici",
          "L'host rifiuta una DLL di qui",
          "El anfitrion rechaza una DLL de aqui" },
        MP_TEXT_FONT_SYSFONT, 400u },

    /* The band's sentences with the DLL's name: one the host's list does not name, and the first
     * of more DLLs than the request could name. The name is a file's own of up to 31 characters and
     * of any width, so it stands last: the band cuts a line at its end, and a long name then loses
     * its own end and never a word in front of it. With the longest name of this release,
     * enhanced_resolution.dll, a sentence fits the band's 400 pixels whole in every language. */
    [MP_TEXT_REFUSED_NOT_ALLOWED - MP_TEXT_SECOND_TABLE_FIRST] = {
        { "Refused by the host: %s",
          "Vom Host abgelehnt: %s",
          "Refusee par l'hote : %s",
          "Rifiutata dall'host: %s",
          "Vetada por el anfitrion: %s" },
        MP_TEXT_FONT_SYSFONT, 400u },
    [MP_TEXT_REFUSED_TOO_MANY - MP_TEXT_SECOND_TABLE_FIRST] = {
        { "Too many foreign DLLs: %s",
          "Zu viele fremde DLLs: %s",
          "Trop de DLL tierces : %s",
          "Troppe DLL estranee: %s",
          "Sobran DLL ajenas: %s" },
        MP_TEXT_FONT_SYSFONT, 400u },

    /* The host's row under such a refusal. The host has no build of the DLL to show, so the row
     * says what the host made of it. Only ever put into the row, which is measured with it. */
    [MP_TEXT_REFUSED_NOT_ALLOWED_WORD - MP_TEXT_SECOND_TABLE_FIRST] = {
        { "not allowed", "nicht erlaubt", "non autorisee", "non consentita", "no permitida" },
        MP_TEXT_FONT_NONE, 0u },

    /* The band of a machine that cannot host, with the name of the DLL of its own that blocks it,
     * last for the same reason as above. */
    [MP_TEXT_ARM_FOREIGN_DLL - MP_TEXT_SECOND_TABLE_FIRST] = {
        { "Hosting blocked by %s",
          "Hosten blockiert durch %s",
          "Impossible d'heberger : %s",
          "Impossibile ospitare: %s",
          "Anfitrion bloqueado por %s" },
        MP_TEXT_FONT_SYSFONT, 400u },
};

const mp_text_row_t *mp_text_second_row(mp_text_id_t id)
{
    if ((size_t)id < (size_t)MP_TEXT_SECOND_TABLE_FIRST || (size_t)id >= (size_t)MP_TEXT_COUNT) {
        return NULL;
    }
    return &ROWS[(size_t)id - (size_t)MP_TEXT_SECOND_TABLE_FIRST];
}
