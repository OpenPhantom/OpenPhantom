/* mp_text.c: the table of the multiplayer's own texts. See the header.
 *
 * Every cell is printable ASCII, because the menu fonts are bitmap fonts whose width tables stop at
 * 0x7F: umlauts and accents are written out, as the other mods that draw their own text write them.
 * A mention of the red button inside a sentence names the button's own word in that language.
 *
 * SIZE NOTE: over 600 lines, and all but the last few are the table: a row for every text this
 * feature draws, in five languages. A seam by screen would part rows the test measures together and
 * give the lookup a table per file for no reader's benefit. The first seam taken put the chosen
 * language and the two reads beside the table, in mp_text_pick.c. The second is by id: every id
 * from MP_TEXT_SECOND_TABLE_FIRST on is a row of mp_text_second.c, the rows that stood last here
 * went there with it, and mp_text_row below asks the table an id belongs to. This file takes no new
 * row; a new text goes at the end of the enum and into the second file.
 */
#include "mp_text.h"

#include <stddef.h>
#include <stdint.h>

static const mp_text_row_t ROWS[MP_TEXT_SECOND_TABLE_FIRST] = {
    /* Buttons every screen shares. */
    [MP_TEXT_BACK] = {
        { "BACK", "ZURUECK", "RETOUR", "INDIETRO", "VOLVER" },
        MP_TEXT_FONT_INDUST, 90u },
    [MP_TEXT_APPLY] = {
        { "Apply", "Uebernehmen", "Appliquer", "Applica", "Aplicar" },
        MP_TEXT_FONT_SYSFONT, 190u },
    [MP_TEXT_CLEAR] = {
        { "Clear", "Loeschen", "Effacer", "Cancella", "Borrar" },
        MP_TEXT_FONT_SYSFONT, 190u },
    [MP_TEXT_CANCEL] = {
        { "Cancel", "Abbrechen", "Annuler", "Annulla", "Cancelar" },
        MP_TEXT_FONT_SYSFONT, 190u },

    /* The entry screen: the three rows of help under the edit bar. */
    [MP_TEXT_ENTRY_HELP_CHARS] = {
        { "Letters, digits and symbols.",
          "Buchstaben, Ziffern und Zeichen.",
          "Lettres, chiffres et symboles.",
          "Lettere, cifre e simboli.",
          "Letras, numeros y simbolos." },
        MP_TEXT_FONT_COURIER, 288u },
    [MP_TEXT_ENTRY_HELP_TRIM] = {
        { "Spaces at either end are dropped.",
          "Leerzeichen an den Enden fallen weg.",
          "Espaces aux bouts supprimes.",
          "Spazi agli estremi rimossi.",
          "Se quitan espacios extremos." },
        MP_TEXT_FONT_COURIER, 288u },
    [MP_TEXT_ENTRY_HELP_KEYS] = {
        { "Enter applies, Esc discards.",
          "Enter uebernimmt, Esc verwirft.",
          "Entree valide, Echap annule.",
          "Invio conferma, Esc annulla.",
          "Intro acepta, Esc descarta." },
        MP_TEXT_FONT_COURIER, 288u },

    /* The hub. */
    [MP_TEXT_HUB_TITLE] = {
        { "MULTIPLAYER", "MULTIPLAYER", "MULTIJOUEUR", "MULTIGIOCATORE", "MULTIJUGADOR" },
        MP_TEXT_FONT_INDUST, 248u },
    [MP_TEXT_HUB_HOST] = {
        { "HOST A GAME",
          "SPIEL HOSTEN",
          "HEBERGER UNE PARTIE",
          "OSPITA UNA PARTITA",
          "CREAR PARTIDA" },
        MP_TEXT_FONT_SYSFONT, 300u },
    [MP_TEXT_HUB_JOIN] = {
        { "JOIN A GAME",
          "SPIEL BEITRETEN",
          "REJOINDRE UNE PARTIE",
          "UNISCITI A UNA PARTITA",
          "UNIRSE A UNA PARTIDA" },
        MP_TEXT_FONT_SYSFONT, 300u },
    [MP_TEXT_HUB_NAME] = {
        { "PLAYER NAME", "SPIELERNAME", "NOM DU JOUEUR", "NOME GIOCATORE", "NOMBRE DEL JUGADOR" },
        MP_TEXT_FONT_SYSFONT, 300u },
    [MP_TEXT_HUB_NET_LAN] = {
        { "NETWORK: LAN", "NETZ: LAN", "RESEAU: LAN", "RETE: LAN", "RED: LAN" },
        MP_TEXT_FONT_SYSFONT, 300u },
    [MP_TEXT_HUB_NET_PUBLIC] = {
        { "NETWORK: PUBLIC", "NETZ: OEFFENTLICH", "RESEAU: PUBLIC", "RETE: PUBBLICA",
          "RED: PUBLICA" },
        MP_TEXT_FONT_SYSFONT, 300u },
    [MP_TEXT_NAME_TITLE] = {
        { "NAME", "NAME", "NOM", "NOME", "NOMBRE" },
        MP_TEXT_FONT_INDUST, 236u },
    [MP_TEXT_NAME_HINT] = {
        { "Your name as the other players see it",
          "Spielername, wie die anderen ihn sehen",
          "Votre nom tel que les autres le voient",
          "Il tuo nome come lo vedono gli altri",
          "Tu nombre tal como lo ven los demas" },
        MP_TEXT_FONT_SYSFONT, 400u },

    /* The player table, headings and cells, each cut to its column in courier. */
    [MP_TEXT_COL_NAME] = { { "NAME", "NAME", "NOM", "NOME", "NOMBRE" }, MP_TEXT_FONT_COURIER, 95u },
    [MP_TEXT_COL_HERO] = { { "HERO", "HELD", "PERS", "EROE", "PERS" }, MP_TEXT_FONT_COURIER, 40u },
    [MP_TEXT_COL_TEAM] = { { "TEAM", "TEAM", "EQ.", "SQ.", "EQ." }, MP_TEXT_FONT_COURIER, 38u },
    [MP_TEXT_COL_READY] = {
        { "READY", "BEREIT", "PRET", "PRONTO", "LISTO" },
        MP_TEXT_FONT_COURIER, 55u },
    [MP_TEXT_COL_PING] = { { "PING", "PING", "PING", "PING", "PING" }, MP_TEXT_FONT_COURIER, 44u },
    [MP_TEXT_CELL_HOST] = { { "Host", "Host", "Hote", "Host", "Host" }, MP_TEXT_FONT_COURIER, 44u },
    [MP_TEXT_CELL_YES] = { { "yes", "ja", "oui", "si", "si" }, MP_TEXT_FONT_COURIER, 50u },
    [MP_TEXT_CELL_NO] = { { "no", "nein", "non", "no", "no" }, MP_TEXT_FONT_COURIER, 50u },
    [MP_TEXT_CELL_TEAM] = { { "T%u", "T%u", "E%u", "S%u", "E%u" }, MP_TEXT_FONT_COURIER, 38u },

    /* Joining: the screen, the password question and the band. */
    [MP_TEXT_JOIN_TITLE] = {
        { "JOIN", "BEITRETEN", "REJOINDRE", "UNISCITI", "UNIRSE" },
        MP_TEXT_FONT_INDUST, 236u },
    [MP_TEXT_JOIN_GO] = {
        { "Join", "Beitreten", "Rejoindre", "Unisciti", "Unirse" },
        MP_TEXT_FONT_SYSFONT, 190u },
    [MP_TEXT_JOIN_KEEP] = {
        { "Remember", "Merken", "Memoriser", "Memorizza", "Recordar" },
        MP_TEXT_FONT_SYSFONT, 190u },
    [MP_TEXT_JOIN_FORGET] = {
        { "Forget", "Vergessen", "Oublier", "Dimentica", "Olvidar" },
        MP_TEXT_FONT_SYSFONT, 190u },
    [MP_TEXT_JOIN_PASSWORD_FOR] = {
        { "Enter the password for %s",
          "Passwort fuer %s eingeben",
          "Mot de passe pour %s",
          "Password per %s",
          "Contrasena para %s" },
        MP_TEXT_FONT_NONE, 0u },
    [MP_TEXT_JOIN_PASSWORD_WANTED] = {
        { "This host asks for a password",
          "Dieser Host verlangt ein Passwort",
          "Cet hote demande un mot de passe",
          "Questo host chiede una password",
          "Este anfitrion pide una contrasena" },
        MP_TEXT_FONT_SYSFONT, 400u },
    [MP_TEXT_PASSWORD_TITLE] = {
        { "PASSWORD", "PASSWORT", "MOT DE PASSE", "PASSWORD", "CONTRASENA" },
        MP_TEXT_FONT_INDUST, 236u },
    [MP_TEXT_JOIN_PICK_HINT] = {
        { "Pick a server or type address:port",
          "Server waehlen oder Adresse:Port eingeben",
          "Choisir un serveur ou taper adresse:port",
          "Scegli un server o scrivi indirizzo:porta",
          "Elige un servidor o escribe ip:puerto" },
        MP_TEXT_FONT_SYSFONT, 400u },
    [MP_TEXT_JOIN_NOTHING_TO_KEEP] = {
        { "Nothing to remember: pick a server first",
          "Nichts zu merken: erst einen Server waehlen",
          "Rien a memoriser: choisir un serveur",
          "Niente da memorizzare: scegli un server",
          "Nada que recordar: elige un servidor" },
        MP_TEXT_FONT_SYSFONT, 400u },
    [MP_TEXT_JOIN_KEPT] = {
        { "Remembered", "Gemerkt", "Memorise", "Memorizzato", "Recordado" },
        MP_TEXT_FONT_SYSFONT, 400u },
    [MP_TEXT_JOIN_FORGET_ONLY_FAV] = {
        { "Only servers marked FAV can be forgotten",
          "Vergessen geht nur bei gemerkten (FAV)",
          "Seul un serveur memorise (FAV) s'oublie",
          "Si dimentica solo un server salvato (FAV)",
          "Solo se olvida un servidor guardado (FAV)" },
        MP_TEXT_FONT_SYSFONT, 400u },
    [MP_TEXT_JOIN_FORGOTTEN] = {
        { "Forgotten", "Vergessen", "Oublie", "Dimenticato", "Olvidado" },
        MP_TEXT_FONT_SYSFONT, 400u },
    [MP_TEXT_JOIN_CODE_HINT] = {
        { "Pick a session or type its code",
          "Sitzung waehlen oder Code eingeben",
          "Choisir une partie ou taper son code",
          "Scegli una partita o scrivi il codice",
          "Elige una partida o escribe su codigo" },
        MP_TEXT_FONT_SYSFONT, 400u },
    [MP_TEXT_JOIN_CODE_BAD] = {
        { "A code has 8 signs, like ABCD-EFGH",
          "Ein Code hat 8 Zeichen, etwa ABCD-EFGH",
          "Un code a 8 signes, comme ABCD-EFGH",
          "Un codice ha 8 segni, come ABCD-EFGH",
          "Un codigo tiene 8 signos: ABCD-EFGH" },
        MP_TEXT_FONT_SYSFONT, 400u },
    [MP_TEXT_JOIN_PUBLIC_ASKING] = {
        { "Asking the relay for sessions...",
          "Relais wird nach Sitzungen gefragt...",
          "Demande des parties au relais...",
          "Chiedo le partite al relay...",
          "Pidiendo partidas al relay..." },
        MP_TEXT_FONT_SYSFONT, 400u },
    [MP_TEXT_JOIN_PUBLIC_SILENT] = {
        { "The relay does not answer, try a code",
          "Relais antwortet nicht, Code versuchen",
          "Le relais ne repond pas, tapez un code",
          "Il relay non risponde, prova un codice",
          "El relay no responde, prueba un codigo" },
        MP_TEXT_FONT_SYSFONT, 400u },
    [MP_TEXT_JOIN_PUBLIC_COUNT] = {
        { "Public sessions: %u",
          "Oeffentliche Sitzungen: %u",
          "Parties publiques: %u",
          "Partite pubbliche: %u",
          "Partidas publicas: %u" },
        MP_TEXT_FONT_SYSFONT, 400u },
    /* Where a row of the join lists came from: kept, typed in, heard on the LAN, listed by
     * the relay. */
    [MP_TEXT_SOURCE_FAVOURITE] = {
        { "FAV", "FAV", "FAV", "PREF", "FAV" }, MP_TEXT_FONT_COURIER, 34u },
    [MP_TEXT_SOURCE_TYPED] = {
        { "DIR", "ADR", "ADR", "IND", "DIR" }, MP_TEXT_FONT_COURIER, 34u },
    [MP_TEXT_SOURCE_LAN] = {
        { "LAN", "LAN", "LAN", "LAN", "LAN" }, MP_TEXT_FONT_COURIER, 34u },
    [MP_TEXT_SOURCE_PUBLIC] = {
        { "PUB", "OEFF", "PUBL", "PUBB", "PUBL" }, MP_TEXT_FONT_COURIER, 34u },

    /* Hosting: the three rows, the switches and the three questions. */
    [MP_TEXT_HOST_ROW_SESSION] = {
        { "Session name: %s",
          "Sitzungsname: %s",
          "Nom de session: %s",
          "Nome sessione: %s",
          "Nombre de sesion: %s" },
        MP_TEXT_FONT_NONE, 0u },
    [MP_TEXT_HOST_ROW_PASSWORD] = {
        { "Password: %s", "Passwort: %s", "Mot de passe: %s", "Password: %s", "Contrasena: %s" },
        MP_TEXT_FONT_NONE, 0u },
    [MP_TEXT_HOST_PASSWORD_SET] = {
        { "set", "gesetzt", "defini", "impostata", "definida" },
        MP_TEXT_FONT_NONE, 0u },
    [MP_TEXT_HOST_PASSWORD_NONE] = {
        { "none", "keins", "aucun", "nessuna", "ninguna" },
        MP_TEXT_FONT_NONE, 0u },
    [MP_TEXT_HOST_ROW_PORT] = {
        { "Port: %u", "Port: %u", "Port: %u", "Porta: %u", "Puerto: %u" },
        MP_TEXT_FONT_NONE, 0u },
    [MP_TEXT_HOST_TITLE] = {
        { "HOST", "HOSTEN", "HEBERGER", "OSPITA", "CREAR" },
        MP_TEXT_FONT_INDUST, 200u },
    [MP_TEXT_HOST_COOP] = {
        { "Co-op", "Koop", "Coop", "Coop", "Coop" },
        MP_TEXT_FONT_COURIER, 138u },
    [MP_TEXT_HOST_TDM] = {
        { "Team Deathmatch",
          "Team Deathmatch",
          "Team Deathmatch",
          "Team Deathmatch",
          "Team Deathmatch" },
        MP_TEXT_FONT_COURIER, 138u },
    [MP_TEXT_HOST_ANNOUNCE] = {
        { "Show on the LAN",
          "Im LAN ankuendigen",
          "Voir sur LAN",
          "Mostra in LAN",
          "Ver en la LAN" },
        MP_TEXT_FONT_COURIER, 138u },
    [MP_TEXT_HOST_LIST_PUBLIC] = {
        { "Public list",
          "Oeff. Liste",
          "Liste publique",
          "Lista pubblica",
          "Lista publica" },
        MP_TEXT_FONT_COURIER, 138u },
    [MP_TEXT_HOST_GO] = {
        { "ON TO THE LOBBY",
          "WEITER ZUR LOBBY",
          "VERS LE SALON",
          "VAI ALLA LOBBY",
          "IR A LA SALA" },
        MP_TEXT_FONT_SYSFONT, 300u },
    [MP_TEXT_HOST_SLOTS] = {
        { "Seats: %u of %u",
          "Plaetze: %u von %u",
          "Places: %u sur %u",
          "Posti: %u di %u",
          "Plazas: %u de %u" },
        MP_TEXT_FONT_NONE, 0u },
    [MP_TEXT_HOST_PASSWORD_HINT] = {
        { "Session password, empty means none",
          "Passwort der Sitzung, leer heisst keins",
          "Mot de passe de session, vide = aucun",
          "Password di sessione, vuota = nessuna",
          "Contrasena de sesion, vacia = ninguna" },
        MP_TEXT_FONT_SYSFONT, 400u },
    [MP_TEXT_HOST_PORT_HINT] = {
        { "Session port, 1 to 65535",
          "Port der Sitzung, 1 bis 65535",
          "Port de session, 1 a 65535",
          "Porta di sessione, da 1 a 65535",
          "Puerto de sesion, 1 a 65535" },
        MP_TEXT_FONT_SYSFONT, 400u },
    [MP_TEXT_PORT_TITLE] = {
        { "PORT", "PORT", "PORT", "PORTA", "PUERTO" },
        MP_TEXT_FONT_INDUST, 236u },
    [MP_TEXT_HOST_PORT_BAD] = {
        { "That is not a port. Allowed: 1 to 65535",
          "Das ist kein Port. Erlaubt ist 1 bis 65535",
          "Ce n'est pas un port. Permis: 1 a 65535",
          "Non e una porta. Ammessi: da 1 a 65535",
          "No es un puerto. Permitido: 1 a 65535" },
        MP_TEXT_FONT_SYSFONT, 400u },
    [MP_TEXT_SESSION_TITLE] = {
        { "SESSION", "SITZUNG", "SESSION", "SESSIONE", "SESION" },
        MP_TEXT_FONT_INDUST, 236u },
    [MP_TEXT_SESSION_HINT] = {
        { "Session name as the others see it",
          "Name der Sitzung, wie die anderen sie sehen",
          "Nom de session vu par les autres",
          "Nome di sessione visto dagli altri",
          "Nombre de sesion que ven los demas" },
        MP_TEXT_FONT_SYSFONT, 400u },

    /* The players screen, opened during a game. */
    [MP_TEXT_PLAYERS_TITLE] = {
        { "PLAYERS", "SPIELER", "JOUEURS", "GIOCATORI", "JUGADORES" },
        MP_TEXT_FONT_INDUST, 244u },
    [MP_TEXT_TEAM_1] = {
        { "TEAM 1", "TEAM 1", "EQUIPE 1", "SQUADRA 1", "EQUIPO 1" },
        MP_TEXT_FONT_INDUST, 150u },
    [MP_TEXT_TEAM_2] = {
        { "TEAM 2", "TEAM 2", "EQUIPE 2", "SQUADRA 2", "EQUIPO 2" },
        MP_TEXT_FONT_INDUST, 150u },
    [MP_TEXT_NO_TEAM] = {
        { "NO TEAM", "KEIN TEAM", "SANS EQUIPE", "SENZA SQUADRA", "SIN EQUIPO" },
        MP_TEXT_FONT_INDUST, 150u },
    [MP_TEXT_PLAYERS_IN_GAME] = {
        { "%u IN THE GAME", "%u IM SPIEL", "%u EN JEU", "%u IN GIOCO", "%u EN JUEGO" },
        MP_TEXT_FONT_NONE, 0u },
    [MP_TEXT_READY_WORD] = {
        { "READY", "BEREIT", "PRET", "PRONTO", "LISTO" },
        MP_TEXT_FONT_INDUST, 150u },
    [MP_TEXT_WAITING_WORD] = {
        { "WAITING", "WARTET", "ATTENTE", "IN ATTESA", "ESPERA" },
        MP_TEXT_FONT_INDUST, 150u },
    [MP_TEXT_PLAYERS_FOOT] = {
        { "Change in the lobby",
          "Wechsel nur in der Lobby",
          "Changer au salon",
          "Cambio in lobby",
          "Cambiar en la sala" },
        MP_TEXT_FONT_COURIER, 200u },

    /* The lobby: why a join was refused, the rows, the band and the level line. */
    [MP_TEXT_DENY_FULL] = {
        { "The session is full",
          "Die Sitzung ist voll",
          "La session est pleine",
          "La sessione e piena",
          "La sesion esta llena" },
        MP_TEXT_FONT_SYSFONT, 400u },
    [MP_TEXT_DENY_PROTOCOL] = {
        { "The host runs another version of the mod",
          "Der Host hat eine andere Version des Mods",
          "L'hote a une autre version du mod",
          "L'host ha un'altra versione della mod",
          "El anfitrion tiene otra version del mod" },
        MP_TEXT_FONT_SYSFONT, 400u },
    [MP_TEXT_DENY_CONTENT] = {
        { "The host plays with different game data",
          "Der Host spielt mit anderen Spieldaten",
          "L'hote joue avec d'autres donnees",
          "L'host usa altri dati di gioco",
          "El anfitrion usa otros datos de juego" },
        MP_TEXT_FONT_SYSFONT, 400u },
    [MP_TEXT_DENY_MODE] = {
        { "The host plays a different game",
          "Der Host spielt ein anderes Spiel",
          "L'hote joue un autre mode",
          "L'host gioca un'altra modalita",
          "El anfitrion juega otro modo" },
        MP_TEXT_FONT_SYSFONT, 400u },
    [MP_TEXT_DENY_PASSWORD] = {
        { "Wrong password",
          "Passwort falsch",
          "Mot de passe incorrect",
          "Password errata",
          "Contrasena incorrecta" },
        MP_TEXT_FONT_SYSFONT, 400u },
    [MP_TEXT_LOBBY_TITLE] = {
        { "LOBBY", "LOBBY", "SALON", "LOBBY", "SALA" },
        MP_TEXT_FONT_INDUST, 236u },
    [MP_TEXT_ROW_PICK_MAP] = {
        { "Choose a map", "Karte waehlen", "Choisir une carte", "Scegli la mappa", "Elegir mapa" },
        MP_TEXT_FONT_SYSFONT, 190u },
    [MP_TEXT_ROW_PICK_SAVE] = {
        { "Choose a saved game",
          "Spielstand waehlen",
          "Choisir une partie",
          "Scegli un salvataggio",
          "Elegir partida" },
        MP_TEXT_FONT_SYSFONT, 190u },
    [MP_TEXT_ROW_FRIENDLY_FIRE] = {
        { "Friendly fire: %s",
          "Beschuss: %s",
          "Tir allie: %s",
          "Fuoco amico: %s",
          "Fuego amigo: %s" },
        MP_TEXT_FONT_SYSFONT, 190u },
    [MP_TEXT_ROW_HERO] = {
        { "Hero: %s", "Held: %s", "Pers.: %s", "Eroe: %s", "Pers.: %s" },
        MP_TEXT_FONT_SYSFONT, 190u },
    [MP_TEXT_ROW_LEAVE_TEAM] = {
        { "Leave the team",
          "Team verlassen",
          "Quitter l'equipe",
          "Lascia la squadra",
          "Dejar el equipo" },
        MP_TEXT_FONT_SYSFONT, 190u },
    [MP_TEXT_ROW_TO_TEAM] = {
        { "Switch to team %u",
          "Zu Team %u wechseln",
          "Aller en equipe %u",
          "Passa a squadra %u",
          "Cambiar a equipo %u" },
        MP_TEXT_FONT_NONE, 0u },
    [MP_TEXT_ROW_NOT_READY] = {
        { "Not ready after all",
          "Doch nicht bereit",
          "Pas pret en fait",
          "Non ancora pronto",
          "Aun no listo" },
        MP_TEXT_FONT_SYSFONT, 190u },
    [MP_TEXT_ROW_READY] = {
        { "I am ready", "Bereit melden", "Je suis pret", "Sono pronto", "Estoy listo" },
        MP_TEXT_FONT_SYSFONT, 190u },
    [MP_TEXT_ROW_START] = {
        { "START THE GAME", "SPIEL STARTEN", "LANCER", "AVVIA", "EMPEZAR" },
        MP_TEXT_FONT_SYSFONT, 190u },
    [MP_TEXT_LOBBY_NOBODY_YET] = {
        { "(nobody has joined yet)",
          "(noch niemand beigetreten)",
          "(personne encore)",
          "(ancora nessuno)",
          "(aun nadie)" },
        MP_TEXT_FONT_COURIER, 288u },
    [MP_TEXT_LOBBY_WAIT_HOST] = {
        { "(waiting for the host)",
          "(warte auf den Host)",
          "(attente de l'hote)",
          "(attesa dell'host)",
          "(esperando al anfitrion)" },
        MP_TEXT_FONT_COURIER, 288u },
    [MP_TEXT_CONTENT_MISMATCH] = {
        { "Different game data than the host, cut off",
          "Andere Spieldaten als der Host, getrennt",
          "Donnees differentes de l'hote, coupe",
          "Dati diversi dall'host, disconnesso",
          "Datos distintos al anfitrion, desconectado" },
        MP_TEXT_FONT_SYSFONT, 400u },
    [MP_TEXT_JOIN_HOST_LEFT] = {
        { "The host left the session. BACK",
          "Der Host hat die Sitzung verlassen. ZURUECK",
          "L'hote a quitte la session. RETOUR",
          "L'host ha lasciato. INDIETRO",
          "El anfitrion se fue. VOLVER" },
        MP_TEXT_FONT_SYSFONT, 400u },
    [MP_TEXT_JOIN_GAVE_UP] = {
        { "The host does not answer. BACK and try again",
          "Host antwortet nicht. ZURUECK, neu versuchen",
          "L'hote ne repond pas. RETOUR et reessayer",
          "L'host non risponde. INDIETRO e riprova",
          "El anfitrion no responde. VOLVER y reintentar" },
        MP_TEXT_FONT_SYSFONT, 400u },
    [MP_TEXT_JOIN_CONNECTED] = {
        { "Connected, waiting for the host",
          "Verbunden, warte auf den Host",
          "Connecte, attente de l'hote",
          "Connesso, in attesa dell'host",
          "Conectado, esperando al anfitrion" },
        MP_TEXT_FONT_SYSFONT, 400u },
    [MP_TEXT_JOIN_CONNECTING] = {
        { "Connecting to the host...",
          "Verbinde mit dem Host...",
          "Connexion a l'hote...",
          "Connessione all'host...",
          "Conectando con el anfitrion..." },
        MP_TEXT_FONT_SYSFONT, 400u },
    [MP_TEXT_BAND_NOT_READY] = {
        { "Not ready yet: %s",
          "Noch nicht bereit: %s",
          "Pas encore prets: %s",
          "Non pronti: %s",
          "Aun no listos: %s" },
        MP_TEXT_FONT_NONE, 0u },
    [MP_TEXT_BAND_ALL_READY] = {
        { "%u in the room, all ready",
          "%u im Raum, alle bereit",
          "%u au salon, tous prets",
          "%u nella lobby, tutti pronti",
          "%u en la sala, todos listos" },
        MP_TEXT_FONT_NONE, 0u },
    [MP_TEXT_RULES_COOP] = {
        { "Co-op, friendly fire %s",
          "Koop, Beschuss %s",
          "Coop, tir allie %s",
          "Coop, fuoco amico %s",
          "Coop, fuego amigo %s" },
        MP_TEXT_FONT_NONE, 0u },
    [MP_TEXT_RULES_TDM] = {
        { "%s %s %s FF %s", "%s %s %s Beschuss %s", "%s %s %s tir allie %s",
          "%s %s %s fuoco amico %s", "%s %s %s fuego amigo %s" },
        MP_TEXT_FONT_NONE, 0u },
    [MP_TEXT_RULES_ON] = { { "on", "an", "oui", "si", "si" }, MP_TEXT_FONT_NONE, 0u },
    [MP_TEXT_RULES_OFF] = { { "off", "aus", "non", "no", "no" }, MP_TEXT_FONT_NONE, 0u },
    [MP_TEXT_RULES_POINTS] = {
        { "%u pts", "%u Pkt", "%u pts", "%u pt", "%u pts" },
        MP_TEXT_FONT_NONE, 0u },
    [MP_TEXT_RULES_NO_POINTS] = {
        { "no pts", "ohne Pkt", "sans pts", "senza pt", "sin pts" },
        MP_TEXT_FONT_NONE, 0u },
    [MP_TEXT_RULES_MINUTES] = {
        { "%u min", "%u min", "%u min", "%u min", "%u min" },
        MP_TEXT_FONT_NONE, 0u },
    [MP_TEXT_RULES_NO_TIME] = {
        { "no time", "ohne Zeit", "sans temps", "senza tempo", "sin tiempo" },
        MP_TEXT_FONT_NONE, 0u },
    [MP_TEXT_RULES_TEAMS] = {
        { "teams", "Teams", "equipes", "squadre", "equipos" },
        MP_TEXT_FONT_NONE, 0u },
    [MP_TEXT_RULES_FREE] = {
        { "free", "frei", "libre", "libero", "libre" },
        MP_TEXT_FONT_NONE, 0u },
    [MP_TEXT_LEVEL_FROM_SAVE] = {
        { "  (saved game)", "  (Spielstand)", "  (partie)", "  (salvataggio)", "  (partida)" },
        MP_TEXT_FONT_NONE, 0u },
    [MP_TEXT_LEVEL_NONE_HOST] = {
        { "nothing chosen yet",
          "noch nichts gewaehlt",
          "rien de choisi",
          "ancora niente",
          "nada elegido aun" },
        MP_TEXT_FONT_COURIER, 320u },
    [MP_TEXT_LEVEL_NONE_CLIENT] = {
        { "the host is still choosing",
          "der Host waehlt noch",
          "l'hote choisit encore",
          "l'host sta scegliendo",
          "el anfitrion aun elige" },
        MP_TEXT_FONT_COURIER, 320u },
    [MP_TEXT_MAP_OWN] = {
        { "  (custom)", "  (eigene)", "  (perso)", "  (tua)", "  (propio)" },
        MP_TEXT_FONT_NONE, 0u },
    [MP_TEXT_MAPS_NONE] = {
        { "No maps found",
          "Keine Karten gefunden",
          "Aucune carte trouvee",
          "Nessuna mappa trovata",
          "No hay mapas" },
        MP_TEXT_FONT_SYSFONT, 400u },
    [MP_TEXT_MAP_TITLE] = {
        { "MAP", "KARTE", "CARTE", "MAPPA", "MAPA" },
        MP_TEXT_FONT_INDUST, 236u },
    [MP_TEXT_MAP_HINT] = {
        { "Choose the map for the game",
          "Karte fuer das Spiel waehlen",
          "Choisir la carte de la partie",
          "Scegli la mappa della partita",
          "Elige el mapa de la partida" },
        MP_TEXT_FONT_SYSFONT, 400u },
    [MP_TEXT_SAVE_NEW_GAME] = {
        { "New game", "Neues Spiel", "Nouvelle partie", "Nuova partita", "Nueva partida" },
        MP_TEXT_FONT_COURIER, 288u },
    [MP_TEXT_SAVE_TITLE] = {
        { "GAME", "SPIEL", "PARTIE", "PARTITA", "PARTIDA" },
        MP_TEXT_FONT_INDUST, 236u },
    [MP_TEXT_SAVE_HINT] = {
        { "Load a saved game or start anew",
          "Spielstand laden oder neu anfangen",
          "Charger une partie ou recommencer",
          "Carica un salvataggio o ricomincia",
          "Cargar partida o empezar de nuevo" },
        MP_TEXT_FONT_SYSFONT, 400u },
    [MP_TEXT_SAVE_LEVEL_MISSING] = {
        { "This saved game's level is missing here",
          "Das Level dieses Spielstands fehlt hier",
          "Le niveau de cette partie manque ici",
          "Qui manca il livello di questo salvataggio",
          "Aqui falta el nivel de esta partida" },
        MP_TEXT_FONT_SYSFONT, 400u },
    [MP_TEXT_HERO_TITLE] = {
        { "HERO", "HELD", "HEROS", "EROE", "HEROE" },
        MP_TEXT_FONT_INDUST, 236u },
    [MP_TEXT_HERO_HINT] = {
        { "Choose the hero for this game",
          "Held fuer dieses Spiel waehlen",
          "Choisir le heros de la partie",
          "Scegli l'eroe della partita",
          "Elige el heroe de la partida" },
        MP_TEXT_FONT_SYSFONT, 400u },
    [MP_TEXT_START_MAP_FIRST] = {
        { "Choose a map first",
          "Erst eine Karte waehlen",
          "Choisir d'abord une carte",
          "Prima scegli una mappa",
          "Primero elige un mapa" },
        MP_TEXT_FONT_SYSFONT, 400u },
    [MP_TEXT_START_LEVEL_FAILED] = {
        { "The level does not load, see the log file",
          "Level laedt nicht, siehe Logdatei",
          "Le niveau ne charge pas, voir le journal",
          "Il livello non si carica, vedi il log",
          "El nivel no carga, ver el registro" },
        MP_TEXT_FONT_SYSFONT, 400u },
    [MP_TEXT_SAVE_STALLED] = {
        { "Saved game: %u %% - stalled. BACK cancels",
          "Spielstand: %u %% - stockt. ZURUECK bricht ab",
          "Partie: %u %% - bloquee. RETOUR annule",
          "Salvataggio: %u %% - fermo. INDIETRO annulla",
          "Partida: %u %% - atascada. VOLVER cancela" },
        MP_TEXT_FONT_NONE, 0u },
    [MP_TEXT_SAVE_COMING] = {
        { "The host's saved game is coming: %u %%",
          "Spielstand des Hosts kommt: %u %%",
          "Partie de l'hote en cours: %u %%",
          "Salvataggio dell'host in arrivo: %u %%",
          "Llega la partida del anfitrion: %u %%" },
        MP_TEXT_FONT_NONE, 0u },
    [MP_TEXT_HOST_MAP_MISSING] = {
        { "The host plays a map this game does not have",
          "Der Host spielt eine Karte, die es hier nicht gibt",
          "L'hote joue une carte absente ici",
          "L'host usa una mappa che qui manca",
          "El anfitrion juega un mapa que aqui falta" },
        MP_TEXT_FONT_SYSFONT, 400u },
    [MP_TEXT_ARM_NO_CONNECTION] = {
        { "No connection, see the log file",
          "Keine Verbindung, siehe Logdatei",
          "Pas de connexion, voir le journal",
          "Nessuna connessione, vedi il log",
          "Sin conexion, ver el registro" },
        MP_TEXT_FONT_SYSFONT, 400u },
    [MP_TEXT_ARM_ROLE_FROM_INI] = {
        { "The role comes from the ini, not the menu",
          "Rolle kommt aus der ini, nicht aus dem Menue",
          "Le role vient de l'ini, pas du menu",
          "Il ruolo viene dall'ini, non dal menu",
          "El rol viene del ini, no del menu" },
        MP_TEXT_FONT_SYSFONT, 400u },
    [MP_TEXT_ARM_ALREADY_HOST] = {
        { "Already host in this game, restart to join",
          "Schon Host, zum Beitreten neu starten",
          "Deja hote ici, relancer pour rejoindre",
          "Gia host qui, riavvia per unirti",
          "Ya eres anfitrion, reinicia para unirte" },
        MP_TEXT_FONT_SYSFONT, 400u },
    [MP_TEXT_ARM_ALREADY_CLIENT] = {
        { "Already client in this game, restart to host",
          "Schon Client, zum Hosten neu starten",
          "Deja client ici, relancer pour heberger",
          "Gia client qui, riavvia per ospitare",
          "Ya eres cliente, reinicia para crear" },
        MP_TEXT_FONT_SYSFONT, 400u },
    [MP_TEXT_LOBBY_CODE] = {
        { "Code: %s", "Code: %s", "Code: %s", "Codice: %s", "Codigo: %s" },
        MP_TEXT_FONT_COURIER, 288u },
    [MP_TEXT_LOBBY_CODE_WAIT] = {
        { "Code: asking the relay...",
          "Code: Relais wird gefragt...",
          "Code: demande au relais...",
          "Codice: chiedo al relay...",
          "Codigo: preguntando al relay..." },
        MP_TEXT_FONT_COURIER, 288u },
    [MP_TEXT_RELAY_CONNECTING] = {
        { "Reaching the relay...",
          "Verbinde mit dem Relais...",
          "Connexion au relais...",
          "Connessione al relay...",
          "Conectando con el relay..." },
        MP_TEXT_FONT_SYSFONT, 400u },
    [MP_TEXT_RELAY_RETRYING] = {
        { "The relay is silent, trying again...",
          "Relais schweigt, neuer Versuch...",
          "Relais muet, nouvel essai...",
          "Relay muto, nuovo tentativo...",
          "Relay mudo, reintentando..." },
        MP_TEXT_FONT_SYSFONT, 400u },
    [MP_TEXT_RELAY_FAILED] = {
        { "Relay out of reach, see the log file",
          "Relais nicht erreichbar, siehe Logdatei",
          "Relais injoignable, voir le journal",
          "Relay irraggiungibile, vedi il log",
          "Relay inalcanzable, ver el registro" },
        MP_TEXT_FONT_SYSFONT, 400u },
    [MP_TEXT_RELAY_NO_SESSION] = {
        { "No session has this code",
          "Keine Sitzung mit diesem Code",
          "Aucune partie avec ce code",
          "Nessuna partita con questo codice",
          "Ninguna partida con este codigo" },
        MP_TEXT_FONT_SYSFONT, 400u },
    [MP_TEXT_RELAY_FULL] = {
        { "The relay or the session is full",
          "Relais oder Sitzung ist voll",
          "Relais ou partie complet",
          "Relay o partita al completo",
          "Relay o partida llenos" },
        MP_TEXT_FONT_SYSFONT, 400u },
    [MP_TEXT_RELAY_CLOSED] = {
        { "That session has ended",
          "Diese Sitzung ist beendet",
          "Cette partie est terminee",
          "Questa partita e finita",
          "Esa partida ha terminado" },
        MP_TEXT_FONT_SYSFONT, 400u },

    /* The scoreboard panel and the notices over a level; the panel grows to its text. */
    [MP_TEXT_HUD_HOLD_HINT] = {
        { "%s HOLDS THE BOARD OPEN",
          "%s HAELT DIE TAFEL OFFEN",
          "%s GARDE LE TABLEAU OUVERT",
          "%s TIENE APERTO IL TABELLONE",
          "%s MANTIENE EL MARCADOR" },
        MP_TEXT_FONT_NONE, 0u },
    [MP_TEXT_HUD_HOST_SILENT] = {
        { "Connection to the host interrupted: %u s",
          "Verbindung zum Host unterbrochen: %u s",
          "Connexion a l'hote interrompue: %u s",
          "Connessione all'host interrotta: %u s",
          "Conexion con el anfitrion cortada: %u s" },
        MP_TEXT_FONT_NONE, 0u },
    /* Said on the band when a client presses load inside a session. Short on purpose: the band is
     * one line and the five languages have to fit the same box. */
    [MP_TEXT_HUD_HOST_LOADS] = {
        { "In co-op the host loads",
          "Im Koop laedt der Host",
          "En cooperatif, l'hote charge",
          "In cooperativa carica l'host",
          "En cooperativo carga el anfitrion" },
        MP_TEXT_FONT_NONE, 0u },
    /* Said while a client is down with nobody standing: somebody else is picking the next world,
     * and the wait has an end. */
    [MP_TEXT_HUD_WIPE_WAIT] = {
        { "Everybody is down. The host decides",
          "Alle sind gefallen. Der Host entscheidet",
          "Tous sont tombes. L'hote decide",
          "Tutti a terra. Decide l'host",
          "Todos han caido. Decide el anfitrion" },
        MP_TEXT_FONT_NONE, 0u },
    /* Said while the host's packets arrive and its world does not move: a menu, a load, or the
     * cheat panel. Not the same thing as a host that has gone quiet, and worded so. */
    [MP_TEXT_HUD_HOST_STILL] = {
        { "The host has paused the world",
          "Der Host haelt die Welt an",
          "L'hote a mis le monde en pause",
          "L'host ha messo in pausa il mondo",
          "El anfitrion ha pausado el mundo" },
        MP_TEXT_FONT_NONE, 0u },
    /* Said on the host for a few seconds after it sent a player away for falling behind. */
    [MP_TEXT_HUD_PLAYER_BEHIND] = {
        { "%s was disconnected: that computer fell too far behind",
          "%s wurde getrennt: sein Rechner lag zu weit zurueck",
          "%s a ete deconnecte: son ordinateur etait trop en retard",
          "%s e stato disconnesso: il suo computer era troppo indietro",
          "%s fue desconectado: su equipo iba muy atrasado" },
        MP_TEXT_FONT_NONE, 0u },
    [MP_TEXT_BOARD_COL_NAME] = {
        { "NAME", "NAME", "NOM", "NOME", "NOMBRE" },
        MP_TEXT_FONT_NONE, 0u },
    [MP_TEXT_BOARD_COL_POINTS] = { { "PTS", "PKT", "PTS", "PUNTI", "PTS" }, MP_TEXT_FONT_NONE, 0u },
    [MP_TEXT_BOARD_COL_DEATHS] = {
        { "DEATHS", "TODE", "MORTS", "MORTI", "MUERTES" },
        MP_TEXT_FONT_NONE, 0u },
    [MP_TEXT_BOARD_COL_TEAM] = {
        { "TEAM", "TEAM", "EQUIPE", "SQUADRA", "EQUIPO" },
        MP_TEXT_FONT_NONE, 0u },
    [MP_TEXT_BOARD_STAND_IN] = {
        { "PLAYER %u", "SPIELER %u", "JOUEUR %u", "GIOCATORE %u", "JUGADOR %u" },
        MP_TEXT_FONT_NONE, 0u },
    [MP_TEXT_BOARD_TITLE] = {
        { "SCORE", "PUNKTESTAND", "SCORE", "PUNTEGGIO", "PUNTUACION" },
        MP_TEXT_FONT_NONE, 0u },
    [MP_TEXT_BOARD_HEAD_OPEN] = {
        { "SCORE   TIME %s   NO TARGET",
          "PUNKTESTAND   ZEIT %s   ZIEL OFFEN",
          "SCORE   TEMPS %s   SANS OBJECTIF",
          "PUNTEGGIO   TEMPO %s   SENZA OBIETTIVO",
          "PUNTUACION   TIEMPO %s   SIN OBJETIVO" },
        MP_TEXT_FONT_NONE, 0u },
    [MP_TEXT_BOARD_HEAD_TARGET] = {
        { "SCORE   TIME %s   TARGET %u",
          "PUNKTESTAND   ZEIT %s   ZIEL %u",
          "SCORE   TEMPS %s   OBJECTIF %u",
          "PUNTEGGIO   TEMPO %s   OBIETTIVO %u",
          "PUNTUACION   TIEMPO %s   OBJETIVO %u" },
        MP_TEXT_FONT_NONE, 0u },
    [MP_TEXT_BOARD_RUNNING] = {
        { "THE ROUND IS ON",
          "DIE RUNDE LAEUFT",
          "MANCHE EN COURS",
          "ROUND IN CORSO",
          "RONDA EN CURSO" },
        MP_TEXT_FONT_NONE, 0u },
    [MP_TEXT_BOARD_WON_TEAM] = {
        { "WINNER: TEAM %s",
          "SIEGER: TEAM %s",
          "VAINQUEUR: EQUIPE %s",
          "VINCE: SQUADRA %s",
          "GANADOR: EQUIPO %s" },
        MP_TEXT_FONT_NONE, 0u },
    [MP_TEXT_BOARD_WON_NAME] = {
        { "WINNER: %s", "SIEGER: %s", "VAINQUEUR: %s", "VINCE: %s", "GANADOR: %s" },
        MP_TEXT_FONT_NONE, 0u },
    [MP_TEXT_BOARD_WON_SLOT] = {
        { "WINNER: PLAYER %u",
          "SIEGER: SPIELER %u",
          "VAINQUEUR: JOUEUR %u",
          "VINCE: GIOCATORE %u",
          "GANADOR: JUGADOR %u" },
        MP_TEXT_FONT_NONE, 0u },
    [MP_TEXT_BOARD_DRAW] = {
        { "DRAW", "UNENTSCHIEDEN", "EGALITE", "PAREGGIO", "EMPATE" },
        MP_TEXT_FONT_NONE, 0u },

    /* How a session ended, on the notice band. */
    [MP_TEXT_OVER_HOST_ENDED] = {
        { "The host ended the session",
          "Der Host hat die Sitzung beendet",
          "L'hote a termine la session",
          "L'host ha chiuso la sessione",
          "El anfitrion termino la sesion" },
        MP_TEXT_FONT_NONE, 0u },
    [MP_TEXT_OVER_HOST_LOST] = {
        { "Connection to the host lost",
          "Verbindung zum Host verloren",
          "Connexion a l'hote perdue",
          "Connessione all'host persa",
          "Conexion con el anfitrion perdida" },
        MP_TEXT_FONT_NONE, 0u },
    [MP_TEXT_OVER_ALL_LEFT] = {
        { "Every other player has left the session",
          "Alle Mitspieler haben die Sitzung verlassen",
          "Tous les autres joueurs sont partis",
          "Tutti gli altri giocatori sono usciti",
          "Todos los demas jugadores se fueron" },
        MP_TEXT_FONT_NONE, 0u },
    [MP_TEXT_OVER_BEHIND] = {
        { "The host disconnected you: this computer fell too far behind",
          "Der Host hat die Verbindung getrennt: dieser Rechner lag zu weit zurueck",
          "L'hote vous a deconnecte: cet ordinateur etait trop en retard",
          "L'host ti ha disconnesso: questo computer era troppo indietro",
          "El anfitrion te desconecto: este equipo iba muy atrasado" },
        MP_TEXT_FONT_NONE, 0u },

    /* The screen that tells a player at the title why the session ended. */
    [MP_TEXT_NOTICE_TITLE] = {
        { "SESSION OVER", "SITZUNG BEENDET", "SESSION TERMINEE", "SESSIONE CHIUSA",
          "SESION TERMINADA" },
        MP_TEXT_FONT_INDUST, 236u },
    [MP_TEXT_NOTICE_ALONE] = {
        { "You are back in single player.",
          "Du bist wieder im Einzelspieler.",
          "Retour au mode solo.",
          "Di nuovo in giocatore singolo.",
          "De vuelta al modo un jugador." },
        MP_TEXT_FONT_COURIER, 288u },
    [MP_TEXT_NOTICE_OK] = {
        { "OK", "OK", "OK", "OK", "OK" },
        MP_TEXT_FONT_SYSFONT, 190u },

    /* The line on the title screen, and a savegame with no name of its own. */
    [MP_TEXT_MENU_TITLE_LINE] = {
        { "MULTIPLAYER   (PRESS M)",
          "MULTIPLAYER   (TASTE M)",
          "MULTIJOUEUR   (TOUCHE M)",
          "MULTIGIOCATORE   (TASTO M)",
          "MULTIJUGADOR   (TECLA M)" },
        MP_TEXT_FONT_INDUST, 380u },
    [MP_TEXT_SAVE_FALLBACK_NAME] = {
        { "Saved game %u", "Spielstand %u", "Partie %u", "Salvataggio %u", "Partida %u" },
        MP_TEXT_FONT_NONE, 0u },
};

const mp_text_row_t *mp_text_row(mp_text_id_t id)
{
    /* The second table answers for its own ids and NULL for any id past the count, which is the
     * answer this read gave for those before the table was split. */
    if ((size_t)id >= (size_t)MP_TEXT_SECOND_TABLE_FIRST) {
        return mp_text_second_row(id);
    }
    return &ROWS[id];
}
