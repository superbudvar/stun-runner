/* JSA II sound command dispatcher.
 *
 * Bridges the MC68010 → 6502 command latch (jsa_latch) to the OKI6295
 * ADPCM chip emulator (oki6295) and ultimately to the audio backend.
 *
 * The 6502 receives command bytes from the 68010 via the latch, decodes them
 * into OKI6295 commands (and potentially YM2151/TMS5220 commands), and
 * writes them to the OKI6295.  This module replicates that decoding step
 * so that the native code can emit the correct audio without running the
 * 6502 ROM.
 *
 * What this module does:
 *   MC68010 command byte (from jsa_latch)
 *     → JSA II command dispatch table   (oki_cmdmap: $6100/$616D/$472F)
 *     → OKI6295 write sequence          (oki_cmdmap: $5BFE / $5C6F)
 *     → Decoded PCM samples via audio backend
 *
 * What this module does NOT do (yet):
 *   - YM2151 FM music synthesis (class 8, 32 of the 109 commands)
 *   - TMS5220 speech synthesis
 *   - Proper 6502 emulation (this is a behavioural model, not a full CPU)
 *   - The 6502 -> 68010 response direction (native/jsa_transport.c reads it
 *     through stunrun_jsa_main_response_read(), but with no 6502 nothing ever
 *     latches a response, so a 68010 read of $600000 still returns 0xFF)
 *
 * The command byte is NOT an OKI6295 command.  Translating it is
 * oki_cmdmap's job — a transcription of the JSA-II 6502 firmware ROM
 * (136070-2123.10c).  This module owns only the bridge: consume the latch,
 * call the translator, hand the emitted bytes to the chip in order, and fill
 * PCM.
 *
 * References:
 *   MAME src/mame/atari/atarijsa.cpp — JSA II hardware
 *   MAME src/devices/sound/okim6295.cpp — OKI6295 protocol
 *   reproduction/sound/jsa_latch.h — command latch transport
 *   reproduction/sound/oki_cmdmap.h  — the 6502 translation (ROM evidence)
 *   native/jsa_transport.h           — the 68010 -> latch transport
 */
#ifndef STUNRUN_SOUND_DISPATCH_H
#define STUNRUN_SOUND_DISPATCH_H

#include "jsa_latch.h"
#include "oki6295.h"
#include "oki_cmdmap.h"
#include "../../backend/audio.h"

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/* Forward declaration — the audio backend is set at init time. */
struct stunrun_audio_backend;

/* Sound dispatch state. */
typedef struct sound_dispatch {
    /* JSA latches — the 68010 -> 6502 command holding register.
     *
     * This is CONSUMED, not observed.  `sound_dispatch_frame()` reads it with
     * stunrun_jsa_sound_command_read(), which clears command_pending and
     * nmi_asserted exactly as the real `sound_command_r` at $2802 does.  See
     * the Defect-1 rationale in sound_dispatch.c before changing that. */
    stunrun_jsa_latches_t latches;

    /* 6502-side bookkeeping for the current dispatch: $1E (the effect gate)
     * and $2808 (/RDV voice-busy).  This is what makes a second command on a
     * busy voice behave like the ROM instead of restarting the sample. */
    oki_cmdmap_state_t cmdmap;

    /* OKI6295 chip state. */
    oki6295_t oki;

    /* Audio backend (NULL = no audio). */
    const stunrun_audio_backend_t *backend;
    void *backend_user;

    /* OKI ROM data (loaded from file, owned here). */
    uint8_t *oki_rom;
    uint32_t oki_rom_size;

    /* True once init has been called. */
    bool active;

    /* Statistics (diagnostic, and asserted on by sound_dispatch_test.c).
     *
     * commands_dispatched counts 68010 command bytes TRANSLATED, and is
     * incremented inside sound_dispatch_send_command() so it means the same
     * thing whether the command arrived through the frame loop or was pushed
     * in directly.
     *
     * oki_bytes_written counts bytes actually handed to oki6295_write().
     * A command that the 6502 handles without touching the chip (the four
     * NMI handshakes, the $1E YM2151 bank select, or a play suppressed by
     * the $1E gate) increments commands_dispatched but NOT this. */
    uint64_t frames_dispatched;
    uint64_t commands_dispatched;
    uint64_t oki_bytes_written;
    uint64_t samples_enqueued;
} sound_dispatch_t;

/* Initialise a sound_dispatch object.
 *
 * backend: audio backend to send samples to (may be NULL for headless).
 * backend_user: user pointer for the backend.
 * oki_rom_data: raw bytes of the OKI6295 sample ROM (256 KB, owned by caller
 *               until this function takes ownership).
 * oki_rom_size: must be 262144.
 *
 * Returns false if the ROM could not be loaded. */
bool sound_dispatch_init(sound_dispatch_t *d,
                          const stunrun_audio_backend_t *backend,
                          void *backend_user,
                          uint8_t *oki_rom_data,
                          uint32_t oki_rom_size);

/* Called each frame: consumes the pending command byte from the latch,
 * dispatches it through the 6502 command map to the OKI6295, then fills PCM
 * from all active voices and enqueues to the audio backend.
 *
 * Must be called once per frame (60 Hz) to keep sample playback smooth.
 *
 * A command is acted on EXACTLY ONCE: the latch read clears command_pending,
 * so a byte left in the latch is not re-dispatched on the following frames.
 * The real board consumes the byte in the 6502 NMI handler ($57E3); this
 * loop is a proxy for that, so a command arriving mid-frame is served up to
 * one frame late.  The count is faithful, the timing is approximate.
 *
 * Returns the number of PCM samples enqueued this frame (0 if nothing playing). */
size_t sound_dispatch_frame(sound_dispatch_t *d);

/* Shut down the dispatch and free resources. */
void sound_dispatch_shutdown(sound_dispatch_t *d);

/* ---- Low-level command dispatch ---- */

/* Send a raw JSA II command byte to the OKI6295 emulator.
 * This replicates what the 6502 would do after receiving the command from
 * the 68010 over the latch: dispatch on the class table at $6100, take the
 * argument from $616D, and run the class handler — which is what turns the
 * byte into 0, 1 or 3 OKI6295 writes.  The byte is NOT itself an OKI
 * command; see oki_cmdmap.h.
 *
 * Advances d->cmdmap (the $1E gate and the /RDV voice-busy bits), so calling
 * this out of band is only safe when no play is in flight. */
void sound_dispatch_send_command(sound_dispatch_t *d, uint8_t cmd_byte);

/* Send a raw JSA II response byte from the 6502 to the 68010 latch.
 * This is the reverse direction (6502 → 68010) for commands that require
 * a response. */
void sound_dispatch_send_response(sound_dispatch_t *d, uint8_t resp_byte);

/* ---- YM2151 / TMS5220 (not yet implemented) ---- */

/* Placeholders for future expansion.  Currently no-ops. */
void sound_dispatch_ym2151_write(sound_dispatch_t *d, uint8_t reg, uint8_t val);
void sound_dispatch_tms5220_write(sound_dispatch_t *d, uint8_t val);

#ifdef __cplusplus
}
#endif

#endif /* STUNRUN_SOUND_DISPATCH_H */
