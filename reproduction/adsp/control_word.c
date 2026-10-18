#include "control_word.h"

#include <string.h>

/* ---------------------------------------------------------------------------
 * Register-file plumbing, transcribed from MAME's ADSP-2100 core.
 *
 * Every helper below names the MAME source it models. This is not an ADSP
 * emulator: it reproduces only the register moves $0214-$0237 performs.
 * ------------------------------------------------------------------------- */

/* m_core.mr.mr is 64-bit with MR0 = bits 15:0, MR1 = 31:16, MR2 = 47:32
 * (adsp2100.h:321-332; the union is written so both endiannesses agree).
 * `int16_t` throughout is adsp_reg16::s (adsp2100.h:303-308). */
typedef int64_t mr_t;

static int16_t as_s16(uint16_t v)
{
    return (v >= 0x8000u) ? (int16_t)((int32_t)v - 0x10000) : (int16_t)v;
}

static uint16_t mr1_of(mr_t mr)
{
    return (uint16_t)(((uint64_t)mr >> 16) & 0xffffu);
}

/* The DAG base MAME computes is `m_i & m_lmask[which]`, with the mask table
 * L -> mask at adsp2100.cpp:1088-1106. For the two literals this routine loads,
 * that reduces to the identity -- provably, not by luck:
 *
 *   L3 = $0003 -> mask $3FFC; $0014 & $3FFC = $0014
 *   L4 = $0000 -> mask $3FFF; $1019 & $3FFF = $1019
 *
 * and every L from 0 to 31 whose mask differs ($3FFE, at L = 2) still yields
 * $0014, so no entry in that table can move the base here. The table is
 * therefore NOT carried: I3 and L3 are ROM literals rather than inputs, so a
 * mutant to the mask width SURVIVES this slice's test (verified, findings
 * sec. 99) and would be untestable code. What is observable, and is modelled,
 * is the wrap LENGTH, which comes from L itself and not from the mask -- see
 * dag_step, and note the three-word DM window that pins it. Carrying the mask
 * faithfully needs a caller that supplies I and L as inputs. */
typedef char stunrun_adsp_control_word_dag_base_is_identity[
    ((STUNRUN_ADSP_CONTROL_WORD_I3 & 0x3ffcu) == STUNRUN_ADSP_CONTROL_WORD_I3 &&
     (STUNRUN_ADSP_CONTROL_WORD_I4 & 0x3fffu) == STUNRUN_ADSP_CONTROL_WORD_I4) ? 1 : -1];

/* data_read_dag1 / pgm_read_dag2 (2100ops.hxx:599-622 and 680-699). Both read
 * at I, then advance I by M and wrap into [base, base+L). */
static uint32_t dag_step(uint32_t index, int32_t step, uint32_t base, uint32_t length)
{
    uint32_t next = (index + (uint32_t)step) & 0x3fffu;
    if (next < base)
        next += length;
    else if (next >= base + length)
        next -= length;
    return next;
}

/* Absolute-address windows, the shape som_math.c already uses: DM(a) is
 * dm_words[a - dm_base]. A read outside the window is refused so a truncated
 * capture fails instead of returning plausible garbage. */
static int read_dm(const uint16_t *words, size_t count, uint32_t base,
                   uint32_t address, uint16_t *value)
{
    if (address < base || (address - base) >= (uint32_t)count)
        return 0;
    *value = words[address - base];
    return 1;
}

/* pgm_read_dag2 reads the whole 32-bit program word, latches it in PX and then
 * hands the MAC bits 23:8 (2100ops.hxx:689-692). */
static int read_pm(const uint32_t *words, size_t count, uint32_t base,
                   uint32_t address, uint16_t *value)
{
    if (address < base || (address - base) >= (uint32_t)count)
        return 0;
    *value = (uint16_t)((words[address - base] >> 8) & 0xffffu);
    return 1;
}

/* mac_op_mr() (2100ops.hxx:1380-1535).
 *
 * `shift` is MSTAT_INTEGER ? 0 : 1 (2100ops.hxx:1382). This machine's DSP is
 * an ADSP2100 (harddriv.cpp:1598), whose MSTAT write mask is 0x0f
 * (adsp2100.cpp:167), so MSTAT_INTEGER (0x10) is unreachable and shift is
 * structurally 1. Recorded, not merely observed.
 *
 * MAME forms the product in int32 before shifting and only then widens it into
 * the 64-bit MR. That is reproduced here, including the wrap, because outside
 * the captured operand range the original's signed overflow is
 * implementation-defined and this slice does not invent a wider result. */
#define STUNRUN_ADSP_CONTROL_WORD_MAC_SHIFT 1

/* `temp = (xop * yop) << shift`, kept in the int32 MAME computes it in. */
static uint32_t mac_term(uint16_t x_word, uint16_t y_word)
{
    int32_t product = (int32_t)as_s16(x_word) * (int32_t)as_s16(y_word);
    return (uint32_t)((int32_t)((uint32_t)product <<
                                STUNRUN_ADSP_CONTROL_WORD_MAC_SHIFT));
}

/* (SS), accumulating form `MR = MR + X * Y (SS)`: no bias, no tie case. */
static mr_t mac_accumulate_ss(mr_t mr, uint16_t x_word, uint16_t y_word)
{
    return mr + (mr_t)(int32_t)mac_term(x_word, y_word);
}

/* (RND), accumulating form `MR = MR + X * Y (RND)`: `temp &= 0xffff; res +=
 * 0x8000; if (temp == 0x8000) res &= ~0x10000;`. The +$8000 is the
 * round-to-nearest bias and it fires even when the product is zero -- which is
 * exactly why $0228 moves MR1 on every captured record. Passing mr = 0 gives
 * the assigning form `MR = X * Y (RND)`. */
static mr_t mac_accumulate_rnd(mr_t mr, uint16_t x_word, uint16_t y_word)
{
    uint32_t term = mac_term(x_word, y_word);
    mr_t res = mr + (mr_t)(int32_t)term;

    res += 0x8000;
    if ((term & 0xffffu) == 0x8000u)
        res &= ~(mr_t)0x10000u;
    return res;
}

/* --- ASTAT ---------------------------------------------------------------
 * CALC_N/CALC_Z/CALC_V/CALC_C (2100ops.hxx:41-49) read the *untruncated* int32
 * result, while alu_op_ar/af store only its low 16 bits
 * (2100ops.hxx:854 and 2100ops.hxx:1241). Both halves matter here: $022B tests LT
 * on flags derived from the wide value. The masks keep only bit 15 of each
 * intermediate, so evaluating the shifts on the unsigned spelling gives the same
 * answers as MAME's signed arithmetic shift without relying on how this
 * compiler treats a negative right shift. */

/* $021C `AF = AR - AY0` -> CALC_NZVC_SUB (2100ops.hxx:781-786). */
static int alu_lt_from_sub(uint16_t x, uint16_t y)
{
    uint32_t s = x, d = y, r = s - d;      /* int32_t res in MAME */
    int n = (int)((r >> 14) & 2u);
    int v = (int)(((s ^ d ^ r ^ (r >> 1)) >> 13) & 4u);
    return n ^ v;                          /* m_condition_table[.. | 0x400] */
}

/* $022A `AR = MR1 + AY1` -> CALC_NZVC (2100ops.hxx:753-759). */
static int alu_lt_from_add(uint16_t x, uint16_t y)
{
    uint32_t s = x, d = y, r = s + d;
    int n = (int)((r >> 14) & 2u);
    int v = (int)(((s ^ d ^ r ^ (r >> 1)) >> 13) & 4u);
    return n ^ v;
}

/* $0232 `SR = LSHIFT SI BY 4 (LO)`: shift_op_imm() case 0x02 (2100ops.hxx:
 * 2180-2186) writes the whole 32-bit SR, and SR0 is its low half
 * (adsp2100.h:311-319). The count is taken as (int8_t)op, so +4 is a left
 * shift of an unsigned operand. */
static uint16_t lshift_lo_by4(uint16_t si)
{
    return (uint16_t)(((uint32_t)si << STUNRUN_ADSP_CONTROL_WORD_SHIFT) & 0xffffu);
}

/* One DAG pair: read DM(I3,M1) into MX0 and PM(I4,M4) into MY0, then advance
 * both cursors. This is what $0224, $0225 and $0226 each do. */
typedef struct dag_cursor {
    uint32_t index;
    uint32_t base;
    uint32_t length;
} dag_cursor_t;

static int dag_load_pair(const uint16_t *dm_words, size_t dm_count, uint32_t dm_base,
                         dag_cursor_t *dm,
                         const uint32_t *pm_words, size_t pm_count, uint32_t pm_base,
                         dag_cursor_t *pm, uint16_t *mx0, uint16_t *my0)
{
    if (!read_dm(dm_words, dm_count, dm_base, dm->index, mx0))
        return 0;
    if (!read_pm(pm_words, pm_count, pm_base, pm->index, my0))
        return 0;
    dm->index = dag_step(dm->index, (int32_t)STUNRUN_ADSP_CONTROL_WORD_M1,
                         dm->base, dm->length);
    pm->index = dag_step(pm->index, (int32_t)STUNRUN_ADSP_CONTROL_WORD_M4,
                         pm->base, pm->length);
    return 1;
}

/* The MAC Y operand at $0221 and $0228 is NOT MF. It is the ZERO register.
 *
 * MAME maps the two-bit Y field through a fixed table
 * (adsp2100.cpp:247-250):
 *
 *     m_mac_yregs[0] = &m_core.my0;
 *     m_mac_yregs[1] = &m_core.my1;
 *     m_mac_yregs[2] = &m_core.mf;
 *     m_mac_yregs[3] = &m_core.zero;
 *
 * and both opcodes carry Y field 3:
 *
 *     $0221  op=0x0020980f  BIT(op,11,2) = 3
 *     $0228  op=0x0020580f  BIT(op,11,2) = 3
 *
 * so the executor computes X * ZERO and MR is zero by construction. MF is not
 * read at all, and there is consequently nothing to infer about its value: an
 * earlier reading of this slice took the field to be 2, inferred "MF = 0" from
 * the nine $022C writes, and claimed MAME's disassembler text disagreed with
 * its own executor. All three were wrong. The arithmetic was unaffected, because
 * a zero register and an assumed-zero MF multiply the same way -- but the
 * inference was an invented unknown where the hardware has none. See
 * docs/mc68010-boot-findings.md sec. 99.
 *
 * MX0 is a stale register here: nothing in $0214-$0237 sets it, so it is not an
 * input either, and the product is zero whatever it held. */
#define STUNRUN_ADSP_CONTROL_WORD_Y_FIELD 3u

/* Both MAC sites decode to Y field 3 (the zero register). Bind the values in so
 * a change to the transcribed opcodes cannot quietly turn a structural zero
 * into a register read. */
typedef char stunrun_adsp_control_word_y_field_is_three[
    (STUNRUN_ADSP_CONTROL_WORD_Y_FIELD == 3u) ? 1 : -1];

int stunrun_adsp_control_word_build(
    uint16_t dm0036_in, uint16_t dm003a_after_call,
    const uint16_t *dm_words, size_t dm_word_count, uint16_t dm_base,
    const uint32_t *pm_words, size_t pm_word_count, uint32_t pm_base,
    stunrun_adsp_control_word_t *out)
{
    stunrun_adsp_control_word_t built;
    dag_cursor_t dm, pm;
    uint16_t mx0 = 0u, my0 = 0u;
    mr_t mr;
    uint16_t ar;
    unsigned pass;

    if (dm_words == NULL || pm_words == NULL || out == NULL)
        return 0;
    if (dm_word_count == 0u || pm_word_count == 0u)
        return 0;

    memset(&built, 0, sizeof(built));

    /* $021B AR = DM($0036). */
    ar = dm0036_in;

    /* $021C AF = AR - AY0 ($021A AY0 = $001F); $021D IF LT JUMP $0221.
     * DM($0036) was $0002 on all nine records, so 2 - 31 is negative and the
     * branch is taken every time. The fall-through at $021E/$021F masks
     * DM($0036) with $001F and returns; the capture never ran it, so what the
     * control word would have become is UNKNOWN and the call is refused rather
     * than guessed. */
    if (!alu_lt_from_sub(ar, STUNRUN_ADSP_CONTROL_WORD_GUARD))
        return 0;

    /* $0221 MR = MX0 * 0 (SS): `X * Y (SS)` with the assigning form, Y field 3
     * (the zero register), so MR is zero and its prior contents do not
     * survive. */
    mr = mac_accumulate_ss(0, 0u, 0u);

    /* $0222 CNTR = $0002; $0223 DO $0227 UNTIL CE. MAME turns the DO word into
     * a loop address plus a loop condition (loop_stack_push,
     * 2100ops.hxx:230-242) and tests it at the loop's last instruction, where
     * NOT CE is slow_condition(): `--m_cntr > 0` (2100ops.hxx:316-325). So the
     * body $0224-$0227 runs exactly twice and $0228 runs once after it. */
    dm.index = STUNRUN_ADSP_CONTROL_WORD_I3;
    dm.base = STUNRUN_ADSP_CONTROL_WORD_I3;   /* see dag_base_is_identity */
    dm.length = STUNRUN_ADSP_CONTROL_WORD_L3;
    pm.index = STUNRUN_ADSP_CONTROL_WORD_I4;
    pm.base = STUNRUN_ADSP_CONTROL_WORD_I4;
    pm.length = STUNRUN_ADSP_CONTROL_WORD_L4;

    /* $0224 is `MX0 = DM(I3,M1), MY0 = PM(I4,M4)` whose MAC field decodes to
     * case 0x00, a no-op (2100ops.hxx:1390-1392); $0225 and $0226 are
     * `MR = MR + MX0 * MY0 (SS)` each with a trailing load, and $0227 is the
     * same MAC with no load. So each pass is load, MAC, load, MAC, load, MAC:
     * three of each, six reads of each window over the two passes. */
    for (pass = 0; pass < STUNRUN_ADSP_CONTROL_WORD_CNTR; ++pass) {
        if (!dag_load_pair(dm_words, dm_word_count, dm_base, &dm,
                           pm_words, pm_word_count, pm_base, &pm, &mx0, &my0))
            return 0;
        mr = mac_accumulate_ss(mr, mx0, my0);

        if (!dag_load_pair(dm_words, dm_word_count, dm_base, &dm,
                           pm_words, pm_word_count, pm_base, &pm, &mx0, &my0))
            return 0;
        mr = mac_accumulate_ss(mr, mx0, my0);

        if (!dag_load_pair(dm_words, dm_word_count, dm_base, &dm,
                           pm_words, pm_word_count, pm_base, &pm, &mx0, &my0))
            return 0;
        mr = mac_accumulate_ss(mr, mx0, my0);
    }

    /* $0228 MR = MR + MX0 * 0 (RND): xop 0 = the MX0 the last $0226 load left,
     * yop field 3 = the zero register. The product is zero, so only the RND bias
     * survives -- +$8000 into MR, which is what puts MR1 on its exact 64 Ki
     * multiple for the $022A sum. */
    mr = mac_accumulate_rnd(mr, mx0, 0u);

    /* $022A AR = MR1 + AY1 ($0229 AY1 = $2D41); the store keeps 16 bits while
     * the flags are taken from the wide sum. */
    ar = (uint16_t)(mr1_of(mr) + STUNRUN_ADSP_CONTROL_WORD_BIAS);

    /* $022B IF LT AR = AF. Not taken on any captured record; if it fires the
     * routine stores AF instead and the value this slice reports would be
     * wrong, so refuse rather than invent the outcome. */
    if (alu_lt_from_add(mr1_of(mr), STUNRUN_ADSP_CONTROL_WORD_BIAS))
        return 0;

    /* $022C DM($003A) = AR. */
    built.dm003a_022c = ar;

    /* $022D CALL $0238 .. $022E AR = DM($003A). Out of scope: $0238-$0253 is a
     * separate routine and the captured run reached its $0253 write of $0000
     * every time. dm003a_after_call carries what $022E saw. */
    ar = dm003a_after_call;

    /* $022F MY0 = $0012; $0230 MR = AR * MY0 (RND): xop 2 = AR, yop 0 = MY0. */
    mr = mac_accumulate_rnd(0, ar, STUNRUN_ADSP_CONTROL_WORD_MULTIPLIER);

    /* $0231 SI = DM($0036) -- still the $01A0 value: nothing in $0214-$0235
     * writes it. $0232 SR = LSHIFT SI BY 4 (LO); $0233 AY0 = $0010;
     * $0234 AF = SR0 + AY0. */
    {
        uint16_t sr0 = lshift_lo_by4(dm0036_in);
        uint16_t af = (uint16_t)(sr0 + STUNRUN_ADSP_CONTROL_WORD_FLAG);

        /* $0235 AR = MR1 OR AF (alu_op_ar case 0x0d, 2100ops.hxx:821-827) --
         * MR1 enters as its raw 16-bit pattern via ALU_GETXREG_UNSIGNED. */
        ar = (uint16_t)(mr1_of(mr) | af);
    }

    /* $0236 DM($0036) = AR. */
    built.dm0036_0236 = ar;

    *out = built;
    return 1;
}
