#ifndef XE_SYS_H
#define XE_SYS_H

/* Command codes of kbd_read_cmd / input_get_cmd (platform.md §3.1). */
enum sys_cmd {
    SYS_CMD_CANCEL = 0, SYS_CMD_UP = 1, SYS_CMD_DOWN = 2, SYS_CMD_LEFT = 3, SYS_CMD_RIGHT = 4,
    SYS_CMD_OK = 5, SYS_CMD_OTHER = 6, SYS_CMD_NONE = 7, SYS_CMD_HELP = 8, SYS_CMD_F1 = 9
};

int  sys_kbd_read_cmd(void);        /* non-blocking: next queued command or SYS_CMD_NONE */
void sys_kbd_flush(void);
int  sys_input_get_cmd(void);       /* blocking, keyboard or mouse */
int  sys_poll_cmd(void);            /* non-blocking, keyboard or mouse; SYS_CMD_NONE if none */
int  sys_wait_ok_key(void);         /* blocking "press a key"; 1 if skipped by CTRL */

#endif
