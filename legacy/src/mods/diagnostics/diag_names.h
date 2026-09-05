/* diag_names.h: names instead of numbers.
 *
 * Where a name table exists, the name is printed: the music states and music sequences (from
 * IMUSE.DLL's muscript tables), the FSM opcodes (the obiold opcode picker at 0x58FD34), the
 * player modes, the enemy reaction states, the mover types and phases, and the SNDF_* bits.
 *
 * The NUMBER is always printed alongside, so a missing name can never be mistaken for a different
 * value.
 */
#ifndef DIAG_NAMES_H
#define DIAG_NAMES_H

#include <stdint.h>

typedef struct diag_name {
    int32_t     id;
    const char *name;
} diag_name_t;

extern const diag_name_t diag_music_states[];
extern const diag_name_t diag_music_sequences[];
extern const diag_name_t diag_fsm_opcodes[];
extern const diag_name_t diag_enemy_states[];
extern const diag_name_t diag_mover_types[];
extern const diag_name_t diag_mover_directions[];
extern const char       *diag_player_modes[];   /* NULL-terminated, index = table position */

/* The bare name, or NULL. */
const char *diag_name_of(const diag_name_t *table, int32_t id);

/* "1305 stateFedShip02" or "1307 ?". Four rotating buffers, so several calls inside one printf
 * do not collide. */
const char *diag_numbered_name(const diag_name_t *table, int32_t id);

/* SNDF_* bits, written out as "LOOP|3D|PLAYING". */
void diag_sound_flags(uint32_t flags, char *out, int out_size);

/* A char[N] out of the game is not guaranteed to be NUL-terminated, 3193 shipped text fields
 * carry garbage behind the NUL. Always copy through this. Four rotating buffers. */
const char *diag_safe_string(const char *source, int max_length);

#endif /* DIAG_NAMES_H */
