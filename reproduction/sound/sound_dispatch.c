/* See sound_dispatch.h for overview. */
#include "sound_dispatch.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* Max PCM samples per frame.  The OKI6295 runs at 9039 Hz with PIN7 high
 * (the JSA-II default) or 7231 Hz with it low; at 60 Hz that is 150 or 121
 * samples per frame.  256 covers both with headroom. */
#define DISPATCH_SAMPLE_BUF_SIZE  256

/* Internal PCM buffer for OKI6295 fill → audio backend. */
typedef struct dispatch_pcm_buf {
    int16_t samples[DISPATCH_SAMPLE_BUF_SIZE * 2];  /* stereo */
    size_t count;   /* number of stereo samples */
} dispatch_pcm_buf_t;

bool sound_dispatch_init(sound_dispatch_t *d,
                          const stunrun_audio_backend_t *backend,
                          void *backend_user,
                          uint8_t *oki_rom_data,
                          uint32_t oki_rom_size)
{
    memset(d, 0, sizeof(*d));

    stunrun_jsa_latches_init(&d->latches);
    oki6295_init(&d->oki);
    oki_cmdmap_init(&d->cmdmap);

    d->backend       = backend;
    d->backend_user  = backend_user;
    d->oki_rom       = oki_rom_data;
    d->oki_rom_size  = oki_rom_size;
    d->active        = false;

    if (!oki6295_load_rom(&d->oki, oki_rom_data, oki_rom_size)) {
        fprintf(stderr, "sound_dispatch: OKI6295 ROM size %u != 262144\n",
                oki_rom_size);
        return false;
    }

    d->active = true;
    return true;
}

void sound_dispatch_shutdown(sound_dispatch_t *d)
{
    if (!d)
        return;
    /* Drain the audio backend before shutdown. */
    if (d->backend && d->backend->drain)
        d->backend->drain(d->backend_user);
    free(d->oki_rom);
    d->oki_rom = NULL;
    d->active  = false;

    /* Reset the 6502-side bookkeeping as well, so that shutdown/init are
     * inverse operations.  The firmware's own reset path at $412B zeroes $1E
     * and every /RDV voice bit; without this, an object that was left gated
     * OFF by command $01 before shutdown would come back from a later
     * sound_dispatch_init() already gated OFF, and stay silent forever. */
    oki_cmdmap_init(&d->cmdmap);
}

void sound_dispatch_send_command(sound_dispatch_t *d, uint8_t cmd_byte)
{
    uint8_t bytes[OKI_CMDMAP_MAX_WRITES];
    size_t  n, i;

    if (!d || !d->active)
        return;

    d->commands_dispatched++;

    /* DEFECT 2 (findings doc D2), fixed here.
     *
     * cmd_byte is a 68010 COMMAND, not an OKI6295 command.  The firmware
     * dispatches it at $4371: index the class table at $6100 by the byte,
     * take the argument from $616D, and jump through the entry-point table
     * at $472F/$4730.  Only the class-19 handler at $5BFE and the class-20
     * handler at $5C6F write to the chip, and they write THREE or ONE
     * bytes, not one.  Handing the raw byte to oki6295_write() meant that,
     * e.g., command $38 was parsed by the chip as an OKI command whose
     * second byte was whatever landed next on the bus. */
    n = oki_cmdmap_translate(&d->cmdmap, cmd_byte, bytes);

    for (i = 0; i < n; i++)
        (void)oki6295_write(&d->oki, bytes[i]);

    d->oki_bytes_written += n;

    /* NOTE: oki_cmdmap_translate() has already done the $2808 /RDV and
     * $52..$55 bookkeeping that oki_cmdmap_note_play() exposes — see
     * translate_play() and translate_stop() in oki_cmdmap.c.  Calling
     * oki_cmdmap_note_play() here as well would be redundant at best and
     * actively wrong at worst: n == 0 means the 6502 deliberately did not
     * start anything (gate closed at $5C61, voice locked and busy at $5C59,
     * or $66 naming a sample that is not playing), and a note_play() call
     * would then mark a voice busy that the ROM never touched, poisoning
     * every later command that wanted that voice.
     *
     * KNOWN LIMITATION: the real handler at $5BFE/$5C6F polls the chip's
     * /RDV line and re-issues the stop write until the voice really is idle
     * ($5C2F-$5C35) before reloading it.  We model /RDV from our own
     * bookkeeping and never feed the chip's actual "did the voice start"
     * answer back into d->cmdmap.  If the chip refuses a sample — start >=
     * stop, or a voice the chip still considers playing — cmdmap would still
     * believe that voice is busy.  Measured against the four STUN RUNNER OKI
     * ROMs (136070-2124.1fh / 2125.1ef / 2126.1de / 2127.1cd) every one of
     * the 53 class-19 commands -- all of which are reachable, because the
     * dispatcher at $4371 does `cpy #$6D` before consulting $6100 -- names a
     * well-formed OKI sample header (start < stop).  So no reachable command
     * triggers the divergence.  See docs/jsa2-sound-findings.md, sections 5.1
     * and 5.2. */
}

void sound_dispatch_send_response(sound_dispatch_t *d, uint8_t resp_byte)
{
    if (!d)
        return;
    (void)resp_byte;
    /* 6502 → 68010 response path.  The maincpu reads this asynchronously.
     * For audio purposes we don't need to model this direction. */
}

void sound_dispatch_ym2151_write(sound_dispatch_t *d, uint8_t reg, uint8_t val)
{
    (void)d; (void)reg; (void)val;
    /* TODO: YM2151 FM synthesis (music). */
}

void sound_dispatch_tms5220_write(sound_dispatch_t *d, uint8_t val)
{
    (void)d; (void)val;
    /* TODO: TMS5220 speech synthesis (voice). */
}

size_t sound_dispatch_frame(sound_dispatch_t *d)
{
    if (!d || !d->active)
        return 0;

    d->frames_dispatched++;

    /* Step 1: consume the 68010 command byte, exactly as the board does.
     *
     * DEFECT 1 (findings doc D15), fixed here.  This used to call
     * stunrun_jsa_peek_command_byte(), which takes a `const` latch and
     * deliberately leaves command_pending set, so a single latched byte was
     * re-dispatched on EVERY subsequent frame for the life of the process —
     * a permanently retriggering sound effect.
     *
     * We consume instead, with stunrun_jsa_sound_command_read().  That is
     * the model of the real register: `sound_command_r` at $2802 returns the
     * byte AND clears command_pending and nmi_asserted (see jsa_latch.c).
     * The firmware's NMI handler at $57E3 consumes it exactly once; there is
     * no hardware path by which the same byte is dispatched twice, so
     * "dispatched again next frame" is not an approximation of anything, it
     * is simply an extra dispatch with no counterpart on the board.
     *
     * WHY NOT A QUEUE: stunrun_jsa_latches_t is a single-byte holding
     * register, not a FIFO — stunrun_jsa_main_command_write() overwrites it
     * rather than appending.  Adding a queue inside sound_dispatch_t would
     * invent storage the hardware does not have and would silently make the
     * port diverge from the latch model it is supposed to reproduce.
     *
     * WHAT IS STILL AN APPROXIMATION, and it is the timing rather than the
     * count: on the real board the consume point is NMI time, whereas this
     * loop can consume at most one byte per video frame, so a command that
     * arrives mid-frame is served up to one frame late.  That is a stated
     * proxy for the NMI, not a hidden one. */
    int cmd = stunrun_jsa_sound_command_read(&d->latches);
    if (cmd != STUNRUN_JSA_NO_BYTE)
        sound_dispatch_send_command(d, (uint8_t)cmd);

    /* Step 2: If no backend, we're done. */
    if (!d->backend || !d->backend->stream_write)
        return 0;

    /* Step 3: Fill PCM from all playing voices. */
    static dispatch_pcm_buf_t buf;
    buf.count = 0;

    if (oki6295_is_playing(&d->oki)) {
        buf.count = oki6295_fill(&d->oki, buf.samples,
                                  DISPATCH_SAMPLE_BUF_SIZE);
    }

    if (buf.count == 0)
        return 0;

    /* Step 4: Enqueue PCM to the audio backend. */
    stunrun_audio_buffer_t abuf = {
        .samples       = buf.samples,
        .sample_count  = buf.count,
        .sample_rate   = oki6295_sample_rate(&d->oki),
        .format        = STUNRUN_AUDIO_FORMAT_S16LE_STEREO,
        .buffer_index  = d->frames_dispatched,
        .timestamp_seconds = (double)d->frames_dispatched / 60.0,
    };

    size_t enqueued = d->backend->stream_write(d->backend_user, &abuf);
    d->samples_enqueued += enqueued;
    return enqueued;
}
