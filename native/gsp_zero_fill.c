/* ROM $548E-$54C0, transcribed verbatim.  Provenance, the driver decode that
 * makes the loop a no-op, and the continuation chain that reaches it are in
 * gsp_zero_fill.h.
 *
 *     0000548e: 00 79 18 00 00 c0 00 08  ori.w   #$1800, $c00008.l
 *     00005496: 23 fc ff 80 00 00 00 c0 00 02  move.l  #$ff800000, $c00002.l
 *     000054a0: 30 3c 00 00           move.w   #$0, D0
 *     000054a4: 34 3c 03 83           move.w   #$383, D2
 *     000054a8: 32 3c 00 ff           move.w   #$ff, D1
 *     000054ac: 33 c0 00 c0 00 0c     move.w   D0, $c0000c.l
 *     000054b2: 51 c9 ff f8           dbra    D1, $54ac
 *     000054b6: 42 79 00 60 80 00     clr.w   $608000.l
 *     000054bc: 51 ca ff ea           dbra    D2, $54a8
 *     000054c0: 4e d0                 jmp     (A0)
 *
 * Register names in the comments (D0, D1, D2, A0) are the ROM's.  The loop
 * seeds are transcribed as seeds, not as the iteration counts they produce,
 * because `dbra Dn,target` runs seed + 1 times and that off-by-one is exactly
 * the arithmetic a reader is most likely to get wrong.
 *
 * This is a decompilation, not an improvement, and not a "fix" for the
 * mismatch: the two discarded-address writes are reproduced as writes to
 * $C00008 and $C0000C and classified, not redirected to $C00006 where they
 * would do something.  See the "THE POINT" note in gsp_zero_fill.h.
 */
#include "gsp_zero_fill.h"

/* ── The pinned driver's host-port decode ─────────────────────────────── */

/* `multisync_68k_map` installs the window with no shift (atari/harddriv.cpp:611),
 * so the handler's offset is the byte offset.  `hd68k_gsp_io_w`
 * (atari/harddriv_m.cpp:215-221) then computes `(offset / 2) ^ 1` and hands
 * that straight to `tms340x0_device::host_w`, whose switch
 * (tms34010.cpp:1875) has exactly four cases.  Anything else falls out of the
 * switch and is dropped with no side effect.
 *
 * Derived here from the formula rather than written out as a table of four
 * addresses, so that the table cannot drift away from the arithmetic that
 * produces it -- the trap findings section 15.6 describes is a constant that
 * moves with the code and leaves the fixture sitting on it. */
stunrun_hostport_decode_t stunrun_gsp_zero_fill_classify(uint32_t address)
{
    uint32_t offset;
    uint32_t index;

    if (address < STUNRUN_GSP_HOST_WINDOW_BASE ||
        address >= STUNRUN_GSP_HOST_WINDOW_END)
        return STUNRUN_HOSTPORT_OUTSIDE;

    offset = address - STUNRUN_GSP_HOST_WINDOW_BASE;

    /* A 16-bit bus cannot raise an odd window offset, because the handler is
     * reached through a word-wide delegate that shifts the offset right by
     * one.  Classified anyway so the predicate is total, and so a caller that
     * hands it an odd address gets a verdict rather than a plausible one. */
    if ((offset & 1u) != 0u)
        return STUNRUN_HOSTPORT_DISCARDED;

    index = (offset / 2u) ^ 1u;
    if (index > 3u)
        return STUNRUN_HOSTPORT_DISCARDED;

    return (stunrun_hostport_decode_t)index;
}

const char *stunrun_gsp_zero_fill_decode_name(int decode)
{
    switch (decode) {
    case STUNRUN_HOSTPORT_HSTADRL:
        return "HSTADRL";
    case STUNRUN_HOSTPORT_HSTADRH:
        return "HSTADRH";
    case STUNRUN_HOSTPORT_HSTDAT:
        return "HSTDAT";
    case STUNRUN_HOSTPORT_HSTCTL:
        return "HSTCTL";
    case STUNRUN_HOSTPORT_DISCARDED:
        return "discarded";
    case STUNRUN_HOSTPORT_OUTSIDE:
        return "outside";
    default:
        return "unclassified";
    }
}

/* ── Bus plumbing ─────────────────────────────────────────────────────── */

static void zero_result(stunrun_gsp_zero_fill_result_t *out)
{
    out->reads = 0u;
    out->writes = 0u;
    out->control_reads = 0u;
    out->control_writes = 0u;
    out->address_writes = 0u;
    out->data_writes = 0u;
    out->watchdog_writes = 0u;
    out->live_writes = 0u;
    out->discarded_writes = 0u;
    out->outside_writes = 0u;
    out->live_data_writes = 0u;
    out->control_before = 0u;
    out->control_after = 0u;
    out->start_address = 0u;
    out->data_address = 0u;
    out->data_value = 0u;
    out->continuation = 0u;
}

/* One 16-bit store, reported on the seam and classified.  Classification lives
 * here rather than at each call site so that "discarded" is an accounted fact
 * about the stream and not an absence somebody has to notice. */
static void bus_write(stunrun_gsp_zero_fill_port_t *port,
                      stunrun_gsp_zero_fill_result_t *out, uint32_t address,
                      uint16_t value)
{
    int decode;

    if (port->bus_write != NULL)
        port->bus_write(port->ctx, address, value);
    out->writes++;

    decode = (int)stunrun_gsp_zero_fill_classify(address);
    if (decode == (int)STUNRUN_HOSTPORT_OUTSIDE)
        out->outside_writes++;
    else if (decode == (int)STUNRUN_HOSTPORT_DISCARDED)
        out->discarded_writes++;
    else {
        out->live_writes++;
        if (decode == (int)STUNRUN_HOSTPORT_HSTDAT)
            out->live_data_writes++;
    }
}

static void bus_read(stunrun_gsp_zero_fill_port_t *port,
                     stunrun_gsp_zero_fill_result_t *out, uint32_t address,
                     uint16_t value)
{
    if (port->bus_read != NULL)
        port->bus_read(port->ctx, address, value);
    out->reads++;
}

/* ── The whole routine ────────────────────────────────────────────────── */

int stunrun_gsp_zero_fill(uint32_t continuation,
                          stunrun_gsp_zero_fill_port_t *port,
                          stunrun_gsp_zero_fill_result_t *result)
{
    uint16_t control;
    uint16_t seed;  /* D0 */
    unsigned pass;  /* D2 */
    unsigned word;  /* D1 */

    if (port == NULL || result == NULL)
        return 0;

    zero_result(result);
    result->continuation = continuation;

    /* $548E ori.w #$1800,$c00008.l -- a read-modify-write, so the read half is
     * a real bus cycle and belongs in the stream ahead of the write.
     *
     * $C00008 is word offset 4, which decodes to (4/2)^1 = 5; `host_w` has no
     * case 5, so BOTH halves are dropped.  The value OR-ed in below therefore
     * reaches nothing, and INCW is never enabled anywhere by this routine --
     * which is the second half of why the 230,400 writes that follow cannot
     * walk an address even if they were decoded. */
    control = port->hstctl;
    result->control_before = control;
    bus_read(port, result, STUNRUN_GSP_ZERO_FILL_CONTROL, control);
    result->control_reads++;

    control = (uint16_t)(control | STUNRUN_GSP_ZERO_FILL_CONTROL_OR);
    result->control_after = control;
    port->hstctl = control;
    bus_write(port, result, STUNRUN_GSP_ZERO_FILL_CONTROL, control);
    result->control_writes++;

    /* $5496 move.l #$ff800000,$c00002.l -- two word writes, most significant
     * half first, at $C00002 and $C00004.  Unlike everything else in this
     * routine both of those decode: $C00002 is HSTADRL, so it takes $FF80,
     * and $C00004 is HSTCTL, so it takes $0000.
     *
     * Which register the ROM *meant* at each address is INFERENCE, not
     * transcription, and it is exactly the open question in findings section
     * 31.4: this project's constants have always read $C00002 as HSTADRH,
     * which would make the access a plain HSTADR pair.  Whether real hardware
     * puts HSTADRH at $C00002 -- in which case the driver decode is wrong and
     * the whole table above moves -- is undecided, and neither reading is
     * applied here.  What IS transcription is that these two words decode,
     * that the high half goes to the lower address, and that the control word
     * the routine ORs in never arrives. */
    result->start_address = STUNRUN_GSP_ZERO_FILL_GSP_BASE;
    result->data_address = STUNRUN_GSP_ZERO_FILL_GSP_BASE;
    bus_write(port, result, STUNRUN_GSP_ZERO_FILL_ADDRESS_HIGH,
              (uint16_t)(STUNRUN_GSP_ZERO_FILL_GSP_BASE >> 16));
    bus_write(port, result, STUNRUN_GSP_ZERO_FILL_ADDRESS_LOW,
              (uint16_t)STUNRUN_GSP_ZERO_FILL_GSP_BASE);
    result->address_writes += 2u;

    /* $54A0 move.w #$0,D0 -- the word every data write pushes. */
    seed = STUNRUN_GSP_ZERO_FILL_SEED;
    result->data_value = seed;

    /* $54A4 move.w #$383,D2 / $54A8 move.w #$ff,D1.  `dbra` runs seed + 1
     * times, so 900 passes of 256 words.  D0 is re-latched only once, before
     * the loops; it is never modified inside them, so all 230,400 writes carry
     * the same word and the loop carries no address of its own -- the address
     * was supposed to walk on HSTDAT post-increment, which this driver never
     * enabled (see above) and never would have, since the control write went
     * to a discarded address. */
    pass = STUNRUN_GSP_ZERO_FILL_PASSES;
    do {
        word = STUNRUN_GSP_ZERO_FILL_WORDS_PER_PASS;
        do {
            /* $54AC move.w D0,$c0000c.l -- word offset 6, decoding to
             * (6/2)^1 = 7, which is not a case in `host_w`.  Discarded. */
            bus_write(port, result, STUNRUN_GSP_ZERO_FILL_DATA, seed);
            result->data_writes++;
        } while (--word != 0u);

        /* $54B6 clr.w $608000.l -- BETWEEN the inner and the outer `dbra`, so
         * once per 256 data words and not once per pass, and not once at the
         * end.  $608000 is the watchdog reset window and is nowhere near the
         * GSP window, so it classifies as OUTSIDE rather than as a decode.
         * It is the only write in this routine with any effect on the board. */
        bus_write(port, result, STUNRUN_GSP_ZERO_FILL_WATCHDOG, 0u);
        result->watchdog_writes++;
    } while (--pass != 0u);

    /* $54C0 jmp (A0) -- the caller's continuation, passed through untouched.
     * No bus effect, no register effect: A0 was not written in the body and
     * the loop counts live in D1/D2, so the return address survives intact. */
    return 1;
}