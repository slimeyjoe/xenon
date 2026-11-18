/* Saves and progress (docs/re/save-format.md), stored through the HAL storage API. */
#ifndef XE_GAME_SAVE_H
#define XE_GAME_SAVE_H

#include <stdint.h>

struct cc_vm;

/* SAVE "saveN": write the current state. */
void save_write(const char *name, struct cc_vm *vm);
/* LOAD "saveN": returns 1 if loaded (vm flags/page/script set, image and music restored),
 * 0 when there is no data (the "no data saved" box was shown). */
int  save_read(const char *name, struct cc_vm *vm);

uint8_t progress_read(void);                /* the progress byte */
void    progress_set_bit(int n);            /* COMPLETE n */

#endif
