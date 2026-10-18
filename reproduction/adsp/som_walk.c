/* Bounded C transcription of the ADSP-2100 SOM span walk ($0564-$05D1). */
#include "som_walk.h"

uint16_t stunrun_adsp_lmask(uint16_t length)
{
    /* m_mask_table, MAME adsp2100.cpp: thresholds and values verbatim. */
    if (length > 0x2000u) return 0x0000u;
    if (length > 0x1000u) return 0x2000u;
    if (length > 0x0800u) return 0x3000u;
    if (length > 0x0400u) return 0x3800u;
    if (length > 0x0200u) return 0x3c00u;
    if (length > 0x0100u) return 0x3e00u;
    if (length > 0x0080u) return 0x3f00u;
    if (length > 0x0040u) return 0x3f80u;
    if (length > 0x0020u) return 0x3fc0u;
    if (length > 0x0010u) return 0x3fe0u;
    if (length > 0x0008u) return 0x3ff0u;
    if (length > 0x0004u) return 0x3ff8u;
    if (length > 0x0002u) return 0x3ffcu;
    if (length > 0x0001u) return 0x3ffeu;
    return 0x3fffu;
}

void stunrun_adsp_index_write(stunrun_adsp_index_t *reg, uint16_t value)
{
    reg->index = (uint16_t)(value & 0x3fffu);
    reg->base = (uint16_t)(reg->index & stunrun_adsp_lmask(reg->length));
}

void stunrun_adsp_index_modify(stunrun_adsp_index_t *reg, int modifier)
{
    uint32_t i = (uint32_t)((reg->index + modifier) & 0x3fff);

    if (i < reg->base)
        i = (uint32_t)i + reg->length;
    else if (reg->length != 0u && i >= (uint32_t)reg->base + reg->length)
        i = i - reg->length;
    reg->index = (uint16_t)i & 0x3fffu;
}

/* ADSP AF is 16-bit and the IF GT / IF LT tests are signed on it. Making the
 * sign extension explicit avoids relying on implementation-defined
 * uint16_t -> int16_t conversion. */
static int32_t adsp_diff(uint16_t a, uint16_t b)
{
    uint16_t bits = (uint16_t)(a - b);
    return (bits & 0x8000u) != 0u ? (int32_t)bits - 0x10000 : (int32_t)bits;
}

/* DM(constant): read by absolute address, with no index register involved. */
static int dm_peek(const uint16_t *dm, size_t dm_word_count, uint16_t dm_base,
                   uint16_t address, uint16_t *value)
{
    if (address < dm_base)
        return 0;
    if ((size_t)(address - dm_base) >= dm_word_count)
        return 0;
    *value = dm[address - dm_base];
    return 1;
}

/* DM(Ix,Mx): read at the current index, then apply the modifier. */
static int dm_read(const uint16_t *dm, size_t dm_word_count, uint16_t dm_base,
                   stunrun_adsp_index_t *reg, int modifier, uint16_t *value)
{
    size_t offset;

    if (reg->index < dm_base)
        return 0;
    offset = (size_t)(reg->index - dm_base);
    if (offset >= dm_word_count)
        return 0;

    *value = dm[offset];
    stunrun_adsp_index_modify(reg, modifier);
    return 1;
}

/* $0526-$0544: walk the ring to find the two chain starting pointers. */
int stunrun_adsp_som_scan_select(
    const uint16_t *dm, size_t dm_word_count, uint16_t dm_base,
    uint16_t cntr, uint16_t *i1, uint16_t *i2, uint16_t *ring_length)
{
    stunrun_adsp_index_t i0, i1r, i2r, i3;
    uint16_t length;
    uint16_t ay0, ay1, ar;
    uint32_t iteration;

    if (dm == NULL || i1 == NULL || i2 == NULL || ring_length == NULL)
        return 0;
    /* $0529 doubles the counter into L0; a ring shorter than two words cannot
     * carry both the forward and backward chains. Untested by the capture. */
    if (cntr < 2u)
        return 0;

    length = (uint16_t)(cntr * 2u);
    i0.length = i1r.length = i2r.length = i3.length = length;

    /* $051F-$0525 seed all four index registers at $0040 with M0 = +1 and
     * M1 = M2 = M3 = 0. */
    stunrun_adsp_index_write(&i0, STUNRUN_ADSP_SOM_SCAN_BASE);
    stunrun_adsp_index_write(&i1r, STUNRUN_ADSP_SOM_SCAN_BASE);
    stunrun_adsp_index_write(&i2r, STUNRUN_ADSP_SOM_SCAN_BASE);
    stunrun_adsp_index_write(&i3, STUNRUN_ADSP_SOM_SCAN_BASE);

    /* $052B/$052C seed the first pair of samples, one word apart. */
    if (!dm_read(dm, dm_word_count, dm_base, &i0, 1, &ay0))
        return 0;
    if (!dm_read(dm, dm_word_count, dm_base, &i0, 1, &ay1))
        return 0;
    ar = ay1;                       /* $052D */

    /* $052F: DO $0544 UNTIL CE, with CNTR = DM($0039). */
    for (iteration = 0; iteration < (uint32_t)cntr; iteration++) {
        /* AF is a 16-bit result and IF GT / IF LT are signed tests on it. */
        int32_t difference = adsp_diff(ay1, ar);

        if (difference > 0) {
            /* $0531: the new pair is above AR; leave both chains put. */
        } else if (difference == 0) {
            /* $0532 -> $0537: compare AY0 against each chain's current value. */
            uint16_t sample;
            if (!dm_read(dm, dm_word_count, dm_base, &i1r, 0, &sample))
                return 0;
            difference = adsp_diff(ay0, sample);
            if (difference > 0) {
                if (!dm_read(dm, dm_word_count, dm_base, &i2r, 0, &sample))
                    return 0;
                difference = adsp_diff(ay0, sample);
                if (!(difference < 0)) {
                    /* $0540/$0541: the I2 chain is the larger minimum. */
                    ar = ay1;
                    i2r = i3;
                }
            } else {
                /* $053A/$053B: AY0 beats the I1 chain. */
                ar = ay1;
                i1r = i3;
            }
        } else {
            /* $0533-$0535: AY1 dropped below AR; both chains follow it. */
            ar = ay1;
            i1r = i3;
            i2r = i3;
        }

        /* $0542-$0544: lag I3 one step behind I0, then read the next pair. */
        i3 = i0;
        if (!dm_read(dm, dm_word_count, dm_base, &i0, 1, &ay0))
            return 0;
        if (!dm_read(dm, dm_word_count, dm_base, &i0, 1, &ay1))
            return 0;
    }

    *i1 = i1r.index;
    *i2 = i2r.index;
    *ring_length = length;
    return 1;
}

int stunrun_adsp_som_derive_record(
    const uint16_t *dm, size_t dm_word_count, uint16_t dm_base,
    const uint32_t *program_words, size_t program_word_count,
    stunrun_adsp_som_span_t *spans, size_t span_capacity,
    stunrun_adsp_som_record_t *record)
{
    stunrun_adsp_som_walk_t walk;
    stunrun_adsp_som_record_t built;
    uint16_t control, cntr, i1, i2, length;

    if (dm == NULL || record == NULL || program_words == NULL)
        return 0;
    if (span_capacity > 0 && spans == NULL)
        return 0;

    /* $0514/$0515 and $0526/$052E both read data memory by absolute address. */
    if (!dm_peek(dm, dm_word_count, dm_base, STUNRUN_ADSP_SOM_CONTROL_ADDR,
                 &control))
        return 0;
    if (!dm_peek(dm, dm_word_count, dm_base, STUNRUN_ADSP_SOM_CNTR_ADDR, &cntr))
        return 0;
    if (!stunrun_adsp_som_scan_select(dm, dm_word_count, dm_base, cntr,
                                      &i1, &i2, &length))
        return 0;
    if (!stunrun_adsp_som_walk_record(dm, dm_word_count, dm_base,
                                      program_words, program_word_count,
                                      i1, i2, length, spans, span_capacity,
                                      &walk))
        return 0;

    built.control = control;
    built.start_x = walk.start_x;
    built.delta_x = walk.delta_x;
    built.start_y = walk.start_y;
    built.spans = spans;
    built.span_count = walk.span_count;
    built.completion_ax0 = walk.final_ax0;
    built.completion_ax0_valid = walk.final_ax0_valid;
    *record = built;
    return 1;
}

int stunrun_adsp_som_walk_record(
    const uint16_t *dm, size_t dm_word_count, uint16_t dm_base,
    const uint32_t *program_words, size_t program_word_count,
    uint16_t i1, uint16_t i2, uint16_t length,
    stunrun_adsp_som_span_t *spans, size_t span_capacity,
    stunrun_adsp_som_walk_t *out)
{
    stunrun_adsp_index_t reg_i1, reg_i2, reg_i0, reg_i4;
    stunrun_adsp_som_walk_t result;
    stunrun_adsp_mac_pair_t side;
    uint16_t ax1, ay0, ay1;
    uint16_t side_3e = 0u, side_3f = 0u;
    uint16_t side_3c = 0u, side_3d = 0u;
    size_t span_count = 0;

    if (dm == NULL || out == NULL)
        return 0;
    if (span_capacity > 0 && spans == NULL)
        return 0;

    result.start_x = 0u;
    result.delta_x = 0u;
    result.start_y = 0u;
    result.span_count = 0u;
    result.terminated = 0;
    result.final_ax0 = 0u;
    result.final_ax0_valid = 0;

    reg_i1.length = length;
    reg_i2.length = length;
    reg_i0.length = length;
    reg_i4.length = length;
    stunrun_adsp_index_write(&reg_i1, i1);
    stunrun_adsp_index_write(&reg_i2, i2);
    stunrun_adsp_index_write(&reg_i0, 0u);
    stunrun_adsp_index_write(&reg_i4, 0u);

    /* $0564/$0565 start_x and $0566/$0567 delta_x. */
    if (!dm_read(dm, dm_word_count, dm_base, &reg_i1,
                 STUNRUN_ADSP_M_HDR, &result.start_x))
        return 0;
    if (!dm_read(dm, dm_word_count, dm_base, &reg_i2,
                 STUNRUN_ADSP_M_HDR, &result.delta_x))
        return 0;

    /* $0568 I0 = I2; $0569/$056B start_y becomes the first span's AX1. */
    stunrun_adsp_index_write(&reg_i0, reg_i2.index);
    if (!dm_read(dm, dm_word_count, dm_base, &reg_i0,
                 STUNRUN_ADSP_M_FWD, &result.start_y))
        return 0;
    ax1 = result.start_y;
    if (!dm_read(dm, dm_word_count, dm_base, &reg_i0,
                 STUNRUN_ADSP_M_A1, &ay1))
        return 0;

    /* $056C/$056D/$056E: the other chain's first sample. */
    stunrun_adsp_index_write(&reg_i4, reg_i1.index);
    stunrun_adsp_index_modify(&reg_i4, STUNRUN_ADSP_M_BACK);
    if (!dm_read(dm, dm_word_count, dm_base, &reg_i4,
                 STUNRUN_ADSP_M_A0, &ay0))
        return 0;

    /* $0574/$0577 prologue guards use IF LE: a non-positive span ends the
     * record immediately and the caller writes the terminator alone. $05DD runs
     * between the two tests, so it happens before the AY0 test. */
    if (ay1 > ax1) {
        if (!stunrun_adsp_som_interpolation_low_branch(
                (uint16_t)(ay1 - ax1), reg_i0.index, reg_i2.index,
                dm, dm_word_count, dm_base, program_words,
                program_word_count, &side))
            return 0;
        side_3e = side.mr0;
        side_3f = side.mr1;
    }
    if (ay1 > ax1 && ay0 > ax1) {
        if (!stunrun_adsp_som_interpolation_low_branch(
                (uint16_t)(ay0 - ax1), reg_i4.index, reg_i1.index,
                dm, dm_word_count, dm_base, program_words,
                program_word_count, &side))
            return 0;
        side_3c = side.mr0;
        side_3d = side.mr1;

        for (;;) {
            stunrun_adsp_som_span_t *span;

            if (span_count >= span_capacity) {
                /* The walk did not terminate inside the supplied buffer.
                 * Report failure instead of truncating a record. */
                *out = result;
                return 0;
            }
            span = &spans[span_count];
            span->ax1 = ax1;
            span->ay0 = ay0;
            span->ay1 = ay1;
            span->dm_3e.lo = side_3e;
            span->dm_3e.hi = side_3f;
            span->dm_3c.lo = side_3c;
            span->dm_3c.hi = side_3d;
            span_count++;

            /* $0589/$05A9/$05C3 converge here: I7 += 5, then compare the two
             * chain pointers and stop when they meet. */
            /* The instruction pair just before each compare copies I4 to AF
             * and I0 to AX0. A subsequent tail guard can terminate without
             * another compare, so retain this last copied value. */
            result.final_ax0 = reg_i0.index;
            result.final_ax0_valid = 1;
            if (reg_i4.index == reg_i0.index) {
                result.terminated = 1;
                break;
            }

            if (ay1 > ay0) {
                /* $059D emits from AY0; the $05AF tail walks the I1/I4 chain
                 * and then refreshes the $0607 side. */
                ax1 = ay0;
                stunrun_adsp_index_modify(&reg_i1, STUNRUN_ADSP_M_BACK);
                stunrun_adsp_index_modify(&reg_i4, STUNRUN_ADSP_M_BACK);
                if (!dm_read(dm, dm_word_count, dm_base, &reg_i4,
                             STUNRUN_ADSP_M_A0, &ay0))
                    return 0;
                /* $05B3/$05B4 use IF LT, so equality still refreshes. */
                if (!(ay0 < ax1)) {
                    if (!stunrun_adsp_som_interpolation_low_branch(
                            (uint16_t)(ay0 - ax1), reg_i4.index, reg_i1.index,
                            dm, dm_word_count, dm_base, program_words,
                            program_word_count, &side))
                        return 0;
                    side_3c = side.mr0;
                    side_3d = side.mr1;
                }
                result.terminated = 1;
            } else {
                /* $057D/$05B7 emit from AY1, and the tail is chosen by whether
                 * the two endpoints were equal:
                 *
                 *   equal -- $058F-$059C advances I2/I0, refreshes $05DD, then
                 *            advances I1/I4 and refreshes $0607 as well, so
                 *            BOTH sides are recomputed;
                 *   less  -- $05C9-$05D0 advances I2/I0 and refreshes only
                 *            $05DD; it never checks AY0, which is why $059A
                 *            is the only tail that does.
                 *
                 * A failed $0594/$05CE jumps straight to $05D1, so the rest of
                 * the tail must not run. */
                int endpoints_equal = (ay1 == ay0);
                int forward_ok;

                ax1 = ay1;
                stunrun_adsp_index_modify(&reg_i2, STUNRUN_ADSP_M_FWD);
                stunrun_adsp_index_modify(&reg_i0, STUNRUN_ADSP_M_FWD);
                if (!dm_read(dm, dm_word_count, dm_base, &reg_i0,
                             STUNRUN_ADSP_M_A1, &ay1))
                    return 0;
                forward_ok = !(ay1 < ax1);
                if (forward_ok) {
                    if (!stunrun_adsp_som_interpolation_low_branch(
                            (uint16_t)(ay1 - ax1), reg_i0.index, reg_i2.index,
                            dm, dm_word_count, dm_base, program_words,
                            program_word_count, &side))
                        return 0;
                    side_3e = side.mr0;
                    side_3f = side.mr1;
                }
                if (forward_ok && endpoints_equal) {
                    stunrun_adsp_index_modify(&reg_i1, STUNRUN_ADSP_M_BACK);
                    stunrun_adsp_index_modify(&reg_i4, STUNRUN_ADSP_M_BACK);
                    if (!dm_read(dm, dm_word_count, dm_base, &reg_i4,
                                 STUNRUN_ADSP_M_A0, &ay0))
                        return 0;
                    if (!(ay0 < ax1)) {
                        if (!stunrun_adsp_som_interpolation_low_branch(
                                (uint16_t)(ay0 - ax1), reg_i4.index,
                                reg_i1.index, dm, dm_word_count, dm_base,
                                program_words, program_word_count, &side))
                            return 0;
                        side_3c = side.mr0;
                        side_3d = side.mr1;
                    }
                }
            }

            /* Whichever guard failed above ends the record at $05D1. */
            if (ay1 < ax1 || ay0 < ax1) {
                result.terminated = 1;
                break;
            }
        }
    }

    result.span_count = span_count;
    *out = result;
    return 1;
}
