#ifndef XE_GAME_GAME_H
#define XE_GAME_GAME_H

/* Load script `name` (without extension) from the pack into the VM. Returns 1 on success. */
int game_load_script(const char *name);

/* canned sequences (engine.md §4) */
void seq_logo(void);
void seq_opening(int n);
void seq_staff_roll(void);

#endif
