#include "diag_names.h"

#include <stddef.h>
#include <stdint.h>
#include <stdio.h>

#define ROTATING_BUFFERS 4
#define NUMBERED_NAME_MAX 48
#define SAFE_STRING_MAX   40

/* Music: the muscript symbols from IMUSE.DLL. The duplicate id 1000 (stateFinal01) is masked by
 * STATE_NULL in the DLL's own lookup and is therefore absent here too, exactly as in the
 * engine. */
const diag_name_t diag_music_states[] = {
    {1000,"STATE_NULL"},{1005,"stateFinal02"},{1010,"stateFinal03"},{1015,"stateFinal04"},
    {1020,"stateFinal05"},{1025,"stateFinal06"},{1030,"stateFinal07"},{1035,"stateFinal08"},
    {1100,"stateFedShip01"},{1105,"stateFedShip02"},{1110,"stateFedShip03"},{1115,"stateFedShip04"},
    {1120,"stateFedShip05"},{1125,"stateFedShip06"},{1200,"stateSwamp01"},{1205,"stateSwamp02"},
    {1210,"stateSwamp03"},{1215,"stateSwamp04"},{1220,"stateSwamp05"},{1225,"stateSwamp06"},
    {1300,"stateGunga01"},{1305,"stateGunga02"},{1310,"stateGunga03"},{1315,"stateGunga04"},
    {1320,"stateGunga05"},{1325,"stateGunga06"},{1330,"stateGunga07"},{1335,"stateGunga08"},
    {1340,"stateGunga09"},{1400,"stateGarden01"},{1405,"stateGarden02"},{1410,"stateGarden03"},
    {1415,"stateGarden04"},{1420,"stateGarden05"},{1425,"stateGarden06"},{1430,"stateGarden08"},
    {1435,"stateGarden09"},{1600,"stateQueen03"},{1605,"stateQueen04"},{1610,"stateQueen05"},
    {1615,"stateQueen06"},{1620,"stateQueen07"},{1625,"stateQueen08"},{1630,"stateQueen09"},
    {1635,"stateQueen10"},{1640,"stateQueen11"},{1645,"stateQueen12"},{1650,"stateQueen13"},
    {1655,"stateQueen14"},{1660,"stateQueen15"},{1665,"stateQueen16"},{1700,"stateEspa01"},
    {1705,"stateEspa02"},{1710,"stateEspa03"},{1715,"stateEspa04"},{1720,"stateEspa05"},
    {1725,"stateEspa06"},{1730,"stateEspa07"},{1735,"stateEspa08"},{1740,"stateEspa09"},
    {1745,"stateEspa10"},{1750,"stateEspa11"},{1755,"stateEspa12"},{1760,"stateEspa13"},
    {1765,"stateEspa14"},{1800,"stateRace01"},{1805,"stateRace02"},{1810,"stateRace03"},
    {1815,"stateRace04"},{1820,"stateRace05"},{1825,"stateRace06"},{1830,"stateRace07"},
    {1835,"stateRace08"},{1840,"stateRace09"},{1900,"stateBigCity01"},{1905,"stateBigCity02"},
    {1910,"stateBigCity03"},{1915,"stateBigCity04"},{1920,"stateBigCity05"},{1925,"stateBigCity06"},
    {1930,"stateBigCity07"},{1935,"stateBigCity08"},{1940,"stateBigCity09"},{1945,"stateBigCity10"},
    {1950,"stateBigCity11"},{1955,"stateBigCity12"},
    {0, NULL}
};

const diag_name_t diag_music_sequences[] = {
    {2000,"SEQ_NULL"},{2100,"seqDroidFightLoop"},{2101,"seqDroidFightEnd"},{2110,"seqSidiousMood"},
    {2120,"seqFight1"},{2125,"seqFightRescue"},{2126,"seqFight1End"},{2130,"seqFedIntro"},
    {2140,"seqElevatorSFX1"},{2150,"seqElevatorSFX2"},{2160,"seqQShipTakeoff"},
    {2170,"seqAssaultIntro"},{2180,"seqBigCity"},{2190,"seqEerie1"},{2200,"seqEspa1"},
    {2205,"seqEspa2"},{2210,"seqEspa3"},{2215,"seqEspa4"},{2220,"seqEspaWatto"},
    {2225,"seqEspaAnakin"},{2230,"seqEspaJar"},{2235,"seqEspaSebulba"},{2240,"seqEspaPod"},
    {2245,"seqEspaBoonta"},{2250,"seqEspaSlave"},{2255,"seqEspaJira"},{2260,"seqEspaShmi"},
    {2265,"seqEspaWin"},{2270,"seqEspaGasgano"},{2300,"seqGardenIntro"},{2305,"seqGarden2"},
    {2310,"seqGarden3"},{2315,"seqGarden4"},{2320,"seqGardenEnd"},{2330,"seqGungaIntro"},
    {2335,"seqGunga2"},{2340,"seqGunga3"},{2345,"seqGunga4"},{2350,"seqGungaEnd"},
    {2400,"seqMaulIntro"},{2405,"seqMaul2"},{2410,"seqMaul3"},{2415,"seqMaulEnd"},
    {2420,"seqMaulDeath"},{2430,"seqQueenIntro"},{2435,"seqQueen2"},{2440,"seqQueen3"},
    {2445,"seqQueen4"},{2450,"seqQueenEnd"},{2500,"seqRaceIntro"},{2505,"seqRace2"},
    {2510,"seqRace3"},{2515,"seqRaceEnd"},{2530,"seqSwampIntro"},{2535,"seqSwamp2"},
    {2540,"seqSwamp3"},{2545,"seqSwampEnd"},{2600,"seqFinalIntro"},{2605,"seqFinal2"},
    {2610,"seqFinal3"},{2615,"seqFinal4"},{2620,"seqFinalEnd"},{2630,"seqAssault2"},
    {2635,"seqAssault3"},{2640,"seqAssaultEnd"},{2700,"seqDeath"},{2705,"seqDeathJedi"},
    {2710,"seqVictory"},{2715,"seqPickup"},{2720,"seqSecret"},{2725,"seqAlarm"},
    {2730,"seqDoorOpen"},{2735,"seqStinger1"},{2740,"seqStinger2"},{2745,"seqStinger3"},
    {2750,"seqStinger4"},{2760,"seqCredits"},{2770,"seqMenu"},{2780,"seqLoad"},{2790,"seqOutro"},
    {0, NULL}
};

/* The AI script's vocabulary. The names are the ones the level editor shows. */
const diag_name_t diag_fsm_opcodes[] = {
    {0x100,"Scan For?"},   {0x101,"Chk Count?"},  {0x102,"Check Death?"},{0x103,"Chk For Hit?"},
    {0x105,"Switch"},      {0x106,"Mode Jmp"},    {0x107,"Range Check?"},{0x108,"Continue"},
    {0x109,"Check Timer?"},{0x10a,"Hit Points?"}, {0x10b,"Can See?"},    {0x10c,"Shot Fired?"},
    {0x10d,"Message?"},    {0x180,"Case:"},
    {0x201,"Move To"},     {0x202,"Animation"},   {0x203,"Inc Count"},   {0x204,"Dec Count"},
    {0x205,"Map Anim*"},   {0x206,"Stop Anim*"},  {0x207,"Start Light*"},{0x208,"Stop Light*"},
    {0x209,"Set Flag"},    {0x20a,"Check Flag"},  {0x20b,"AI Killed"},   {0x20c,"Camera Dolly"},
    {0x20d,"Timed Flag"},  {0x20e,"Aux Anim"},    {0x20f,"Chk Aux Anim"},{0x210,"Map Light"},
    {0x211,"Map Anim"},    {0x212,"Map Emitter"}, {0x213,"Map Sound"},
    {0x301,"Slash"},       {0x302,"Chop"},        {0x303,"Block"},       {0x304,"Sidestep"},
    {0x305,"Jump"},        {0x307,"ShootEmUp"},
    {0x400,"Set AI Mode"}, {0x401,"Return Mode"}, {0x405,"Set Count"},
    {0x408,"Move Speed"},  {0x409,"Move Mode"},   {0x40a,"Set Mass"},    {0x40b,"FT Speed"},
    {0x40c,"Set Hit Pnts"},{0x40d,"Set FOV"},     {0x40f,"Start Timer"}, {0x410,"Set Waypoint"},
    {0x411,"Set Cylinder"},{0x412,"Assign"},      {0x413,"Set Owner"},   {0x414,"Check Owner?"},
    {0x415,"Set E-Flag"},
    {0x500,"Dialog Box"},  {0x501,"Response:"},   {0x502,"Exit Dialog"}, {0x504,"Statement"},
    {0x600,"Set Glob"},    {0x601,"Clear Glob"},  {0x602,"Chk Global?"}, {0x603,"Play Sound"},
    {0x604,"LOCK Player"}, {0x605,"Check For?"},  {0x606,"Extra Func"},  {0x607,"Change Playr"},
    {0x608,"Particles"},   {0x609,"View Node"},   {0x60f,"Spawn AI"},    {0x610,"Global Flag"},
    {0, NULL}
};

/* The enemy reaction state, character+0x20. */
const diag_name_t diag_enemy_states[] = {
    {0,"NOT_ACTIVE"},{1,"ACTIVE"},{2,"INVALID"},{3,"STANDBY"},{4,"HITREACT"},{5,"HITPAUSE"},
    {6,"KN-BACK"},{7,"KN-FALL"},{8,"FALLING"},{9,"GETUP"},{10,"SABRHIT"},{11,"DEATH"},
    {12,"FADEOUT"},{13,"SHATTER"},{14,"CORPSE"},{15,"ERRATIC"},
    {0, NULL}
};

/* The 14 player modes, in the order of the pointer table at [0x4B54B0]. */
const char *diag_player_modes[] = {
    "Stand", "Swim", "Slide", "Sabre Attack", "Panaka Attack", "Push Block", "Jump",
    "Jedi Jump", "Fall", "Hang", "Sidle", "Fixed Jump", "Death", "Tripod Gun", NULL
};

const diag_name_t diag_mover_types[] = {
    {0,"AlwaysOn"},{1,"Triggered"},{2,"Door"},{3,"Kervorkian"},{4,"OneShot"},{5,"OneShot2"},
    {6,"PushButton"},{7,"PushBlock"},
    {0, NULL}
};

/* The direction is to be read PER TYPE; the label names both readings. */
const diag_name_t diag_mover_directions[] = {
    {0,"REST"},{1,"FORWARD"},{2,"LATCHED/OPEN-HOLD"},{3,"CLOSING/OPEN-WAIT"},{4,"CLOSING(type6)"},
    {5,"SHUT-DWELL(type6)"},
    {0, NULL}
};

/* ============================================================================================ */
const char *diag_name_of(const diag_name_t *table, int32_t id)
{
    int index;

    for (index = 0; table[index].name != NULL; ++index) {
        if (table[index].id == id) {
            return table[index].name;
        }
    }
    return NULL;
}

const char *diag_numbered_name(const diag_name_t *table, int32_t id)
{
    static char buffers[ROTATING_BUFFERS][NUMBERED_NAME_MAX];
    static int  turn;
    const char *name = diag_name_of(table, id);
    char       *buffer;

    turn = (turn + 1) & (ROTATING_BUFFERS - 1);
    buffer = buffers[turn];

    _snprintf(buffer, NUMBERED_NAME_MAX, (name != NULL) ? "%d %s" : "%d ?", (int)id, name);
    buffer[NUMBERED_NAME_MAX - 1] = '\0';
    return buffer;
}

void diag_sound_flags(uint32_t flags, char *out, int out_size)
{
    static const struct { uint32_t bit; const char *text; } table[] = {
        {0x00000001u,"AMB"},    {0x00000002u,"DIST"},   {0x00000004u,"3D"},
        {0x00000008u,"MUS-ST"}, {0x00000010u,"LOOP"},   {0x00000020u,"STATIC"},
        {0x00000040u,"DOPPLR"}, {0x00000080u,"FADEIN"}, {0x00000100u,"FADEOUT"},
        {0x00000200u,"NODUP"},  {0x00000400u,"MUS-SEQ"},{0x00000800u,"FIELDS"},
        {0x00001000u,"RESTART"},{0x00004000u,"FREEONSTOP"},
        {0x00020000u,"PLAYING"},{0x00040000u,"PAUSED"}, {0x00080000u,"FREE"}
    };
    int index;
    int length = 0;
    int character;

    /* Deliberately WITHOUT _snprintf's return value: it is negative on truncation, and a negative
     * length counter here would write out of the buffer. */
    out[0] = '\0';
    for (index = 0; index < (int)(sizeof(table) / sizeof(table[0])); ++index) {
        if ((flags & table[index].bit) == 0) {
            continue;
        }
        if (length != 0 && length < out_size - 1) {
            out[length++] = '|';
        }
        for (character = 0;
             table[index].text[character] != '\0' && length < out_size - 1;
             ++character) {
            out[length++] = table[index].text[character];
        }
        out[length] = '\0';
        if (length >= out_size - 1) {
            break;
        }
    }
}

const char *diag_safe_string(const char *source, int max_length)
{
    static char buffers[ROTATING_BUFFERS][SAFE_STRING_MAX];
    static int  turn;
    char       *buffer;
    int         limit;
    int         index;

    turn = (turn + 1) & (ROTATING_BUFFERS - 1);
    buffer = buffers[turn];

    limit = (max_length < SAFE_STRING_MAX - 1) ? max_length : SAFE_STRING_MAX - 1;
    for (index = 0; index < limit && source[index] != '\0'; ++index) {
        buffer[index] = (source[index] >= 32 && source[index] < 127) ? source[index] : '.';
    }
    buffer[index] = '\0';
    return buffer;
}
