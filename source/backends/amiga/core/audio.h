/* Audio output drivers of the Amiga backend.
 *
 * The backend runs one audio task (audio.c) that owns the output device for the whole
 * session. The engine never sees the device: the task pulls mono 16-bit blocks from the
 * sequencer with xe_audio_render() and hands them to one of these drivers, which converts
 * and queues them. Two blocks are in flight (double buffering): while the device plays one,
 * the task fills the other, so the task has one block duration to render a block.
 *
 * Every function here is called from the audio task only, in this order:
 *   open -> [block_buffer] fill, submit (block 0), fill, submit (block 1) -> start ->
 *   loop { wait_block -> fill -> submit } -> close
 * Signals the driver needs (interrupt or IO completion) are allocated inside open() and so
 * belong to the audio task.
 *
 * Voice (docs/voice.md §5.3): a driver may also play the voice clips as a second mono stream
 * at their own rate (Paula: the two channels the 8-bit music leaves free; AHI: a second
 * stream the device mixes). The engine pushes voice samples from its own task into a FIFO
 * (audio.c); the audio task opens the voice output when the engine asks for it, then keeps
 * two voice blocks in flight like the music's, filling them from the FIFO (silence when it is
 * empty). wait_block reports a finished voice block as AM_VOICE_BLOCK + its index, and
 * AM_AUDIO_CMD when one of `cmd_mask`'s signals came (the engine wants something). */
#ifndef XE_AMIGA_AUDIO_H
#define XE_AMIGA_AUDIO_H

#include <exec/types.h>
#include <stdint.h>

struct am_audio_drv {
    const char *name;
    /* Open the output for `rate` Hz, blocks of `frames` mono samples. Returns the rate the
     * hardware really plays (the engine re-creates its chip core for it), or 0 when the
     * device is unavailable (the backend then tries the next driver / stays silent). */
    int  (*open)(int rate, int frames);
    void (*close)(void);
    /* Block buffer the task may render into directly (16-bit mono, `frames` samples), or
     * NULL when the driver needs a conversion step (submit_block then converts from the
     * task's scratch buffer). */
    int16_t *(*block_buffer)(int idx);
    /* Queue block idx (its data is in block_buffer(idx) or in `pcm`). */
    void (*submit_block)(int idx, const int16_t *pcm, int frames);
    /* Start playback once both blocks are queued. */
    void (*start)(void);
    /* Block until a block is free to be refilled and return its index (a voice block:
     * AM_VOICE_BLOCK + index), AM_AUDIO_CMD for a signal of `cmd_mask`, or -1 when one of
     * `quit_mask`'s signals arrived (or the device was taken away). */
    int  (*wait_block)(ULONG quit_mask, ULONG cmd_mask);

    /* Voice output; NULL when the driver has none (the engine then mixes the voice into the
     * music). voice_open: a mono stream at `rate`, blocks of `frames`; returns the rate it
     * plays at, 0 if it cannot (channels taken, no memory). voice_submit queues voice block
     * idx from 16-bit samples (silence after `frames` real ones up to the block size);
     * voice_start once both are queued. */
    int  (*voice_open)(int rate, int frames);
    void (*voice_submit)(int idx, const int16_t *pcm, int frames);
    void (*voice_start)(void);
    void (*voice_close)(void);
    /* a cut: both voice blocks silenced in place, the one playing included */
    void (*voice_silence)(void);
    /* Hardware volume, 1..100 percent, which = XE_VOL_MUSIC or XE_VOL_VOICE; returns 1 if
     * the driver applies it (it keeps the value for open/start), 0 if it cannot (the engine
     * scales the samples). Called from the main task: only stores, or writes a register.
     * NULL = never. */
    int  (*set_volume)(int which, int percent);
};

#define AM_VOICE_BLOCK  2               /* wait_block: voice block 0 or 1 is free */
#define AM_AUDIO_CMD    4               /* wait_block: the engine asked for something */
#define AM_VOICE_LOST   5               /* wait_block: the voice output stopped for good */

/* diagnostics the drivers maintain (read by the main task at close) */
extern volatile ULONG am_audio_stat_ints;      /* device interrupts / request completions */
extern volatile ULONG am_audio_stat_flags;     /* driver-specific bits (paula: 1 = first music block
                                                 * latched, 2 = first voice block, 4 = voice
                                                 * channels stolen) */

extern const struct am_audio_drv am_audio_paula;    /* audio_paula.c: Paula through audio.device */
extern const struct am_audio_drv am_audio_ahi;      /* audio_ahi.c: ahi.device, device interface */

#endif
