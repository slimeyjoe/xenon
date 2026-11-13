/* ADT frame animations (graphics.md §3.6): ADT_LOAD / ADT_PLAY / ADT_FREE. */
#ifndef XE_GFX_ADT_H
#define XE_GFX_ADT_H

int  adt_load(const char *name);
void adt_play(void);
void adt_free(void);

#endif
