/* Oracle-backed expectations for the ADSP $0214-$0237 record control word.
 *
 * Every expected value below is a literal transcribed from one artifact:
 * reference/experiments/stunrun/adsp-control-word-pm-f551/run-1/result.json
 * (sha256 fd2acd2aeab1dde50b595c4d4ea9b6c4e678a10e28555ee2484a9f41377f9ba1,
 * matching its metadata.json). That capture is the SAME transaction as the
 * committed adsp-som-pm-reads-f551 run -- identical 146 SOM words, identical
 * 4009 DM events, identical GINT event -- with 54 extra PM reads covering
 * $0214-$0237, recorded from MAME /mnt/build/mame-oracle/mame
 * 75b09ee422732ebce54d4c641e3bf38ddc9de4976963844691c60c361fb46b06
 * (`input=none`, power-on with a disposable NVRAM seed, transaction at frame
 * 551). Nothing in this file was produced by the implementation under test.
 *
 * What the capture holds for these PCs, per record, in `seq` order:
 *
 *   $021B  R  DM($0036) = $0002      <- what $01A0 wrote; the slice's input
 *   $0224  R  DM($0014)              } six DAG reads, L3=$0003 wrapping
 *   $0225  R  DM($0015)              } DM($0014)-DM($0016) back onto itself
 *   $0226  R  DM($0016)              } after the third.  That is 99 events
 *   $0224  R  DM($0014)              } across the nine records -- six DM and
 *   $0225  R  DM($0015)              } six PM reads per record, 54 PM reads
 *   $0226  R  DM($0016)              } over the transaction.
 *   $022C  W  DM($003A) = <below>    <- the MAC accumulator, per record
 *   $022E  R  DM($003A) = $0000      <- what $0238's $0253 left, not the $022C
 *   $0231  R  DM($0036) = $0002      <- unchanged since $01A0
 *   $0236  W  DM($0036) = $0030      <- the control word, all nine records
 *
 * and the six PM dwords, identical on all nine records:
 *
 *   PM($1019)=$00000000  PM($101A)=$00400000  PM($101B)=$00000000
 *   PM($101C)=$00000000  PM($101D)=$00000000  PM($101E)=$00C00000
 *
 * pgm_read_dag2 hands the MAC bits 23:8 of each (2100ops.hxx:689-692), so the
 * MY0 sequence is $0000, $4000, $0000, $0000, $0000, $C000 -- i.e. 0, 16384,
 * 0, 0, 0, -16384. Only two of the six products are non-zero, and they are
 * DM($0015) x 16384 and DM($0016) x -16384.
 */
#include <stdio.h>
#include <string.h>

#include "control_word.h"

static int failures;

#define check(condition) do { \
    if (!(condition)) { \
        printf("FAIL %s:%d: %s\n", __FILE__, __LINE__, #condition); \
        failures++; \
    } \
} while (0)

/* The two operand windows, read through absolute addresses as som_math.c
 * does. L3 = $0003 makes DM($0014)-DM($0016) wrap onto itself, so three words
 * are enough for six reads; L4 = $0000 leaves PM($1019) linear. */
#define DM_BASE 0x0014u
#define PM_BASE 0x1019u

static const uint32_t PM_WINDOW[6] = {
    0x00000000u, 0x00400000u, 0x00000000u,
    0x00000000u, 0x00000000u, 0x00C00000u
};
#define PM_COUNT (sizeof(PM_WINDOW) / sizeof(PM_WINDOW[0]))

typedef struct captured_record {
    uint16_t dm0036_in;        /* $021B reads DM($0036): $0002 on every record */
    uint16_t dm_window[3];     /* DM($0014), DM($0015), DM($0016) */
    uint16_t dm003a_022c;      /* $022C writes this to DM($003A) */
    uint16_t dm003a_after;     /* $022E reads this back; $0253 wrote $0000 */
    uint16_t dm0036_0236;      /* $0236 writes this to DM($0036) */
} captured_record_t;

#define CAPTURED_RECORDS 9u

/* The nine records of the frame-551 transaction, in capture order. The DM
 * triples are the six $0224/$0225/$0226 reads folded onto the three words the
 * L3=$0003 wrap re-reads; the capture shows the same value on the second read
 * of each, which is why three numbers and not six are recorded here. */
static const captured_record_t CAPTURED[CAPTURED_RECORDS] = {
    { 0x0002u, { 0x1927u, 0x3344u, 0xE323u }, 0x5552u, 0x0000u, 0x0030u },
    { 0x0002u, { 0xE6D9u, 0x3344u, 0xE323u }, 0x5552u, 0x0000u, 0x0030u },
    { 0x0002u, { 0x0000u, 0x3989u, 0xE3F8u }, 0x580Au, 0x0000u, 0x0030u },
    { 0x0002u, { 0x3883u, 0x0968u, 0xE385u }, 0x4033u, 0x0000u, 0x0030u },
    { 0x0002u, { 0xC77Du, 0x0968u, 0xE385u }, 0x4033u, 0x0000u, 0x0030u },
    { 0x0002u, { 0x1948u, 0xCC4Au, 0xE419u }, 0x215Au, 0x0000u, 0x0030u },
    { 0x0002u, { 0xCC7Bu, 0xE687u, 0xE3E7u }, 0x2E91u, 0x0000u, 0x0030u },
    { 0x0002u, { 0x3385u, 0xE687u, 0xE3E7u }, 0x2E91u, 0x0000u, 0x0030u },
    { 0x0002u, { 0xE6B8u, 0xCC4Au, 0xE419u }, 0x215Au, 0x0000u, 0x0030u },
};

static void test_captured_control_words(void)
{
    size_t i;

    for (i = 0; i < CAPTURED_RECORDS; ++i) {
        const captured_record_t *r = &CAPTURED[i];
        stunrun_adsp_control_word_t got;

        if (!stunrun_adsp_control_word_build(
                r->dm0036_in, r->dm003a_after, r->dm_window, 3u, DM_BASE,
                PM_WINDOW, PM_COUNT, PM_BASE, &got)) {
            printf("FAIL record %zu rejected\n", i);
            failures++;
            continue;
        }

        /* $022C DM($003A) = AR -- MR1 + $2D41. This is the write the $0238
         * call then throws away, and it is the only thing in the capture that
         * exercises the MAC loop at all; a test that only asserted $0030 would
         * not notice if it had been dropped. */
        if (got.dm003a_022c != r->dm003a_022c) {
            printf("FAIL record %zu $022C: got $%04X expected $%04X\n", i,
                   got.dm003a_022c, r->dm003a_022c);
            failures++;
        }
        /* $0236 DM($0036) = AR -- MR1 OR (SR0 + $0010). */
        if (got.dm0036_0236 != r->dm0036_0236) {
            printf("FAIL record %zu $0236: got $%04X expected $%04X\n", i,
                   got.dm0036_0236, r->dm0036_0236);
            failures++;
        }
        check(got.dm0036_0236 == 0x0030u);
        check(got.dm0036_0236 == r->dm0036_0236);
    }
}

/* The load-bearing consequence: because $0238's $0253 overwrote DM($003A)
 * with $0000, the value $022C stored is DISCARDED, $0230 multiplies zero, and
 * MR1 is zero -- so the control word is exactly ((DM($0036) << 4) + $0010).
 * Three separate assertions, because the interesting failure is the one where
 * $022C is still computed correctly but silently reaches $0230. */
static void test_dm003a_is_discarded(void)
{
    size_t i;

    for (i = 0; i < CAPTURED_RECORDS; ++i) {
        const captured_record_t *r = &CAPTURED[i];
        stunrun_adsp_control_word_t got, fed_back;

        check(stunrun_adsp_control_word_build(
            r->dm0036_in, r->dm003a_after, r->dm_window, 3u, DM_BASE,
            PM_WINDOW, PM_COUNT, PM_BASE, &got));

        /* 1. With the measured DM($003A) of $0000 the word is the shift plus
         *    the $0010 flag and nothing else. */
        check(got.dm0036_0236 ==
              (((uint16_t)(r->dm0036_in << STUNRUN_ADSP_CONTROL_WORD_SHIFT))
               + STUNRUN_ADSP_CONTROL_WORD_FLAG));

        /* 2. The two writes to DM($003A) are genuinely different events: the
         *    $022C value is not what $022E read back. */
        check(got.dm003a_022c != r->dm003a_after);

        /* 3. Had $0238 NOT zeroed DM($003A), $0230's multiply would have
         *    produced a non-zero MR1 and the control word would differ. This
         *    is what makes the discard load-bearing rather than incidental. */
        check(stunrun_adsp_control_word_build(
            r->dm0036_in, got.dm003a_022c, r->dm_window, 3u, DM_BASE,
            PM_WINDOW, PM_COUNT, PM_BASE, &fed_back));
        check(fed_back.dm003a_022c == got.dm003a_022c);
        check(fed_back.dm0036_0236 != got.dm0036_0236);
    }
}

/* MAME's (RND) MAC, written out so the expectations in this file are the
 * emulator's arithmetic and not this implementation's output. `prior` is MR's
 * contents before the instruction: 0 for the assigning form at $0230. */
static uint32_t mame_mac_rnd_product(uint16_t x, uint32_t prior)
{
    uint32_t temp = ((uint32_t)x * 0x0012u) << 1; /* shift 1: MSTAT fractional */
    uint32_t res = prior + temp;

    if ((temp & 0xffffu) == 0x8000u)
        res &= ~0x10000u;
    return res + 0x8000u;
}

/* $0230 is the ASSIGNING form: `MR = AR * MY0 (RND)`, so MR's prior contents do
 * not survive. This slice expresses that by passing the old MR as 0 to an
 * always-accumulating helper, which is a convention rather than a distinct entry
 * point. With the captured inputs the two readings are indistinguishable -- a
 * mutant passing a non-zero prior MR adds a value below 0x10000 and never
 * carries into MR1 -- so this block supplies a DM($003A) where it does carry.
 *
 * Expectations are MAME's arithmetic, not this implementation's output. The
 * (RND) rule is the live `#else` branch at 2100ops.hxx:1400-1408:
 *
 *     temp = (x * y) << shift;   res = temp;      (or mr + temp)
 *     temp &= 0xffff;            res += 0x8000;
 *     if (temp == 0x8000) res &= ~0x10000;
 *
 * Two details are easy to get wrong and both were, in the hand-derived versions
 * of these vectors. The MAC shift is 1 (MAME's MSTAT default when MSTAT_INTEGER
 * is clear, adsp2100.cpp:1381-1382), and the tie test looks at the low half of
 * `temp` -- the PRODUCT -- not at `res` after the bias is added. The disabled
 * `#if 0` branch above it tests the other thing; it is not what runs. */
static void test_the_multiply_at_0230_assigns_rather_than_accumulates(void)
{
    stunrun_adsp_control_word_t got;
    static const uint32_t ZERO_PM[6] = { 0u, 0u, 0u, 0u, 0u, 0u };
    static const uint16_t DM[3] = { 0x0100u, 0x0200u, 0x0300u };
    unsigned dm003a;

    /* A DM($003A) whose scaled product's low half is exactly $8000: MAME then
     * clears bit 16 and MR1 loses one. */
    {
        int found = 0;
        for (dm003a = 1u; dm003a < 0x10000u && !found; ++dm003a) {
            if ((mame_mac_rnd_product(dm003a, 0u) & 0xffffu) != 0x8000u)
                continue;
            found = 1;
            check(stunrun_adsp_control_word_build(
                0x0002u, (uint16_t)dm003a, DM, 3u, DM_BASE,
                ZERO_PM, 6u, PM_BASE, &got));
            check(got.dm0036_0236 ==
                  (uint16_t)((mame_mac_rnd_product(dm003a, 0u) >> 16) | 0x0030u));
            /* Dropping the tie-break would move the word by exactly bit 4. */
            check(got.dm0036_0236 !=
                  (uint16_t)(((mame_mac_rnd_product(dm003a, 0u) + 0x10000u) >> 16)
                             | 0x0030u));
        }
        check(found);
    }

    /* A DM($003A) where MR1 depends on whether MR's prior contents survive, which
     * is the whole point: the assigning form must ignore them. */
    {
        int found = 0;
        for (dm003a = 1u; dm003a < 0x10000u && !found; ++dm003a) {
            uint32_t assigning = mame_mac_rnd_product(dm003a, 0u);
            uint32_t accumulating = mame_mac_rnd_product(dm003a, 0x1234u);
            if ((assigning >> 16) == (accumulating >> 16))
                continue;
            found = 1;
            check(stunrun_adsp_control_word_build(
                0x0002u, (uint16_t)dm003a, DM, 3u, DM_BASE,
                ZERO_PM, 6u, PM_BASE, &got));
            check(got.dm0036_0236 ==
                  (uint16_t)((assigning >> 16) | 0x0030u));
            check(got.dm0036_0236 !=
                  (uint16_t)((accumulating >> 16) | 0x0030u));
        }
        check(found);
    }
}

/* Operands outside the captured frame. A lookup table keyed on the captured
 * values cannot answer any of these; the expectations are worked out from
 * MAME's mac_op_mr rules, not from this implementation's output.
 *
 *   MR starts at 0 ($0221 multiplies by the ZERO register -- MAME maps MAC Y
 *   field 3 to m_core.zero, adsp2100.cpp:250 -- so it is zero by construction,
 *   not by an assumed-zero MF). Six (SS) MACs accumulate
 *   2 * MX0 * MY0; the $0228 (RND) MAC adds its +$8000 bias and nothing else;
 *   $022A adds $2D41 to MR1.
 *
 * With the captured PM window the sum collapses to
 *   MR = 2 * 16384 * (DM($0015) - DM($0016)) + 0x8000
 * because DM($0014), and the second reads of $0014/$0015, all pair with a zero
 * MY0. Records 1/2: 2*16384*(0x3344+0x738D)+0x8000 = 672202752 = 10257 * 64 Ki
 * -- an exact multiple, which is the only reason +$2D41 lands on $5552.
 */
static void test_other_operands_give_other_results(void)
{
    stunrun_adsp_control_word_t got;

    /* A three-word DM window is enough for six reads. That is $0219's L3=$0003
     * wrapping DM($0016)+M1 back to DM($0014); an implementation that walked
     * the window linearly would need DM($0017)-DM($0019) and be refused here. */
    check(stunrun_adsp_control_word_build(
        0x0002u, 0x0000u, CAPTURED[0].dm_window, 3u, DM_BASE,
        PM_WINDOW, PM_COUNT, PM_BASE, &got));

    /* An all-zero PM sextet zeroes both live products, leaving MR = 0x8000 and
     * MR1 = 0, so $022C stores the bare $2D41 = $2D41. */
    {
        static const uint32_t ZERO_PM[6] = { 0u, 0u, 0u, 0u, 0u, 0u };
        static const uint16_t DM[3] = { 0x0100u, 0x0200u, 0x0300u };

        check(stunrun_adsp_control_word_build(
            0x0002u, 0x0000u, DM, 3u, DM_BASE,
            ZERO_PM, 6u, PM_BASE, &got));
        check(got.dm003a_022c == STUNRUN_ADSP_CONTROL_WORD_BIAS);
        check(got.dm0036_0236 == 0x0030u);
    }

    /* Record 1 with DM($0016) off by one bit, 0xE323 -> 0xE324. It pairs only
     * with PM($101E) = -16384, so 2 * (-7644 + 7389) * -16384 = 8355840 more,
     * MR1 falls by one and $022C becomes $5551. */
    {
        static const uint16_t DM[3] = { 0x1927u, 0x3344u, 0xE324u };

        check(stunrun_adsp_control_word_build(
            0x0002u, 0x0000u, DM, 3u, DM_BASE,
            PM_WINDOW, PM_COUNT, PM_BASE, &got));
        check(got.dm003a_022c == 0x5551u);
        check(got.dm0036_0236 == 0x0030u);
    }

    /* Record 1 with PM($101A) off by one bit, $400000 -> $408000, so the
     * MY0 that pairs with DM($0015) goes 16384 -> 16512. Only the $022C write
     * can move; $0036 never sees MR. */
    {
        static const uint32_t PM[6] = {
            0x00000000u, 0x00408000u, 0x00000000u,
            0x00000000u, 0x00000000u, 0x00C00000u
        };

        check(stunrun_adsp_control_word_build(
            0x0002u, 0x0000u, CAPTURED[0].dm_window, 3u, DM_BASE,
            PM, 6u, PM_BASE, &got));
        check(got.dm003a_022c == 0x5585u);
        check(got.dm0036_0236 == 0x0030u);
    }

    /* DM($0036) drives only $0231's shift, never the MAC. $0000, $000A and
     * $001E are all below the $001F guard so $021D still falls through, and
     * each moves the control word by its own << 4 while $022C stays put. */
    {
        static const struct { uint16_t in; uint16_t word; } cases[] = {
            { 0x0000u, 0x0010u }, { 0x000Au, 0x00B0u }, { 0x001Eu, 0x01F0u },
        };
        size_t i;

        for (i = 0; i < sizeof(cases) / sizeof(cases[0]); ++i) {
            check(stunrun_adsp_control_word_build(
                cases[i].in, 0x0000u, CAPTURED[0].dm_window, 3u, DM_BASE,
                PM_WINDOW, PM_COUNT, PM_BASE, &got));
            check(got.dm0036_0236 == cases[i].word);
            check(got.dm003a_022c == CAPTURED[0].dm003a_022c);
        }
    }

    /* DM($003A) after the call drives $0230's multiply and therefore MR1, and
     * MR1 is OR'd into the flag half, so a non-zero one shows up directly. With
     * DM($0036) = $0002 the shift half is $0030; $0400 gives
     * 2 * 1024 * $0012 = $9000 plus the RND bias $8000 = $11000 -> MR1 = $0001
     * -> $0031, $1000 gives $2C000 -> MR1 = $0002 -> $0032, and $7FFF gives
     * 2 * 32767 * $0012 = $11FFFC plus $8000 = $127FFC -> MR1 = $0012 -> $0032.
     * The last is the case that pins the multiplier itself: at $0011 it would
     * be $117FBE -> MR1 = $0011 -> $0031. $FFFF is -1 signed, so
     * 2 * -1 * $0012 = -$24 plus $8000 = $7FDC, still inside MR0, MR1 is zero
     * and the word is $0030 again. */
    {
        static const struct { uint16_t dm003a; uint16_t word; } cases[] = {
            { 0x0400u, 0x0031u }, { 0x1000u, 0x0032u },
            { 0x7FFFu, 0x0032u }, { 0xFFFFu, 0x0030u }, { 0x0000u, 0x0030u },
        };
        size_t i;

        for (i = 0; i < sizeof(cases) / sizeof(cases[0]); ++i) {
            check(stunrun_adsp_control_word_build(
                0x0002u, cases[i].dm003a, CAPTURED[0].dm_window, 3u, DM_BASE,
                PM_WINDOW, PM_COUNT, PM_BASE, &got));
            check(got.dm0036_0236 == cases[i].word);
            check(got.dm003a_022c == CAPTURED[0].dm003a_022c);
        }
    }
}

/* The (RND) tie case, which the captured operands never reach. mac_op_mr()
 * rounds half away from zero by adding $8000 and then, when the product's low
 * 16 bits are exactly $8000, clears bit 16 again (2100ops.hxx:1403-1407).
 *
 * It cannot be reached at $0228, whose Y operand is the zero register; $0230's
 * `MR = AR * MY0 (RND)` is the only reachable one. DM($003A) = $2000 gives
 * 2 * 8192 * $0012 = $48000, whose low 16 bits are $8000, so MR lands on $40000
 * after the bias and the tie, MR1 = $0004 and the control word is $0034. Without
 * the tie MR would be $50000, MR1 = $0005 and the word $0035. $022C is
 * unaffected, which is itself the point: the tie is downstream of it. */
static void test_rnd_tie_case(void)
{
    stunrun_adsp_control_word_t got;

    check(stunrun_adsp_control_word_build(
        0x0002u, 0x2000u, CAPTURED[0].dm_window, 3u, DM_BASE,
        PM_WINDOW, PM_COUNT, PM_BASE, &got));
    check(got.dm003a_022c == CAPTURED[0].dm003a_022c);
    check(got.dm0036_0236 == 0x0034u);
}

/* Everything the capture never exercised is refused, and refusal leaves the
 * caller's buffer exactly as it was. */
static void check_rejected(uint16_t dm0036_in, const uint16_t *dm, size_t dm_count,
                           uint16_t dm_base, const uint32_t *pm, size_t pm_count,
                           uint32_t pm_base, const char *what)
{
    stunrun_adsp_control_word_t built;
    size_t i;

    memset(&built, 0xa5, sizeof(built));
    if (stunrun_adsp_control_word_build(dm0036_in, 0x0000u, dm, dm_count, dm_base,
                                        pm, pm_count, pm_base, &built)) {
        printf("FAIL %s was accepted\n", what);
        failures++;
        return;
    }
    for (i = 0; i < sizeof(built) / sizeof(uint16_t); ++i) {
        const uint8_t *bytes = (const uint8_t *)&built;
        if (bytes[i] != 0xa5u) {
            printf("FAIL %s published partial output at byte %zu\n", what, i);
            failures++;
            return;
        }
    }
}

static void test_rejects_without_partial_output(void)
{
    const uint16_t *dm = CAPTURED[0].dm_window;

    check_rejected(0x0002u, NULL, 3u, DM_BASE, PM_WINDOW, PM_COUNT, PM_BASE,
                   "NULL DM window");
    check_rejected(0x0002u, dm, 3u, DM_BASE, NULL, PM_COUNT, PM_BASE,
                   "NULL PM window");
    check_rejected(0x0002u, dm, 0u, DM_BASE, PM_WINDOW, PM_COUNT, PM_BASE,
                   "empty DM window");
    check_rejected(0x0002u, dm, 3u, DM_BASE, PM_WINDOW, 0u, PM_BASE,
                   "empty PM window");
    /* The window is addressed absolutely, so a base above the first read and a
     * count short of the last one are both out-of-window rather than clamped. */
    check_rejected(0x0002u, dm, 3u, 0x0015u, PM_WINDOW, PM_COUNT, PM_BASE,
                   "DM window base above the first read");
    check_rejected(0x0002u, dm, 2u, DM_BASE, PM_WINDOW, PM_COUNT, PM_BASE,
                   "DM window short of the L3 wrap");
    check_rejected(0x0002u, dm, 3u, DM_BASE, PM_WINDOW, 5u, PM_BASE,
                   "PM window short of PM($101E)");
    check_rejected(0x0002u, dm, 3u, DM_BASE, PM_WINDOW, PM_COUNT, 0x101Au,
                   "PM window base above the first read");

    /* $021D IF LT is the guard on the whole routine. $001F - $001F is zero, so
     * $001F and anything above it fall through to the unexecuted $021E/$021F
     * mask-and-return and are refused. */
    check_rejected(STUNRUN_ADSP_CONTROL_WORD_GUARD, dm, 3u, DM_BASE,
                   PM_WINDOW, PM_COUNT, PM_BASE, "$021D fall-through at $001F");
    check_rejected(0x0020u, dm, 3u, DM_BASE, PM_WINDOW, PM_COUNT, PM_BASE,
                   "$021D fall-through just above $001F");
    check_rejected(0x003Fu, dm, 3u, DM_BASE, PM_WINDOW, PM_COUNT, PM_BASE,
                   "$021D fall-through at $003F");

    /* $022B IF LT is never taken by the capture. DM($0015) = $7FFF against
     * PM($101A) = 16384 and DM($0016) = $8000 against PM($101E) = -16384 gives
     * MR = 65536 * 32768 = 0x80000000, MR1 = $8000, and MR1 + $2D41 = $AD41 is
     * negative. */
    {
        static const uint16_t NEGATIVE[3] = { 0x1234u, 0x7FFFu, 0x8000u };

        check_rejected(0x0002u, NEGATIVE, 3u, DM_BASE, PM_WINDOW, PM_COUNT,
                       PM_BASE, "$022B IF LT taken");
    }
}

int main(void)
{
    test_captured_control_words();
    test_dm003a_is_discarded();
    test_the_multiply_at_0230_assigns_rather_than_accumulates();
    test_other_operands_give_other_results();
    test_rnd_tie_case();
    test_rejects_without_partial_output();
    if (failures == 0)
        puts("PASS");
    return failures == 0 ? 0 : 1;
}
