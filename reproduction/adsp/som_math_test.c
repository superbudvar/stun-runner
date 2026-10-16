/* Oracle-backed vector tests for the ADSP SOM interpolation helper.
 *
 * DM operands, selector AR, MY0, and MR results come from the same-run capture
 * reference/experiments/stunrun/adsp-som-transaction-f551/result.json. The PM
 * lookup words are from MAME oracle ADSP program snapshots at frames 551 and
 * 600 (image SHA-256 values are in the adjacent metadata):
 * PM[$103D]=$00124900 -> MY0=$1249; PM[$103B]=$00199900 -> MY0=$1999.
 * The outputs match the observed stores at PC=$05F2/$05F3 and $061C/$061D.
 */
#include <stdio.h>

#include "som_math.h"

static int failures;
#define check(c) do { if (!(c)) { printf("FAIL %s\n", #c); failures++; } } while (0)

int main(void)
{
    static uint16_t dm[STUNRUN_ADSP_SOM_DATA_WORDS];
    static uint32_t pm[STUNRUN_ADSP_SOM_PROGRAM_WORDS];
    stunrun_adsp_mac_pair_t out;

    /* Captured helper call at $05DD:
     * AR/index=7, I0=$45 -> DM[$44]=$FB, I2=$43 -> DM[$42]=$FF;
     * PM[$1036+7]=$00124900 -> MY0=$1249; signed diff=-4;
     * MR=2*(-4)*$1249=$FFFF6DB8, stored DM[$3E]=$6DB8 / DM[$3F]=$FFFF. */
    dm[0x0044] = 0x00FBu;
    dm[0x0042] = 0x00FFu;
    pm[0x103D] = 0x00124900u;
    check(stunrun_adsp_som_interpolation_low_branch(
        7u, 0x0045u, 0x0043u, dm, 0x2000u, pm, 0x2000u, &out));
    check(out.mr0 == 0x6DB8u);
    check(out.mr1 == 0xFFFFu);

    /* Captured helper call at $0607:
     * AR/index=5, I4=$41 -> DM[$40]=$F4, I1=$43 -> DM[$42]=$FF;
     * PM[$1036+5]=$00199900 -> MY0=$1999; diff=-11;
     * MR=2*(-11)*$1999=$FFFDCCDA, stored DM[$3C]=$CCDA / DM[$3D]=$FFFD. */
    dm[0x0040] = 0x00F4u;
    pm[0x103B] = 0x00199900u;
    check(stunrun_adsp_som_interpolation_low_branch(
        5u, 0x0041u, 0x0043u, dm, 0x2000u, pm, 0x2000u, &out));
    check(out.mr0 == 0xCCDAu);
    check(out.mr1 == 0xFFFDu);

    /* The high-index scaling branch is intentionally not guessed. Invalid
     * inputs must not overwrite output. */
    out.mr0 = 0x1234u;
    out.mr1 = 0x5678u;
    check(!stunrun_adsp_som_interpolation_low_branch(
        0x0041u, 0x0045u, 0x0043u, dm, 0x2000u, pm, 0x2000u, &out));
    check(out.mr0 == 0x1234u && out.mr1 == 0x5678u);
    check(!stunrun_adsp_som_interpolation_low_branch(
        7u, 0u, 0x43u, dm, 0x2000u, pm, 0x2000u, &out));
    check(!stunrun_adsp_som_interpolation_low_branch(
        7u, 0x0045u, 0x0043u, NULL, 0x2000u, pm, 0x2000u, &out));
    check(!stunrun_adsp_som_interpolation_low_branch(
        7u, 0x0045u, 0x0043u, dm, 0x2000u, pm, 0x103Du, &out));
    check(!stunrun_adsp_som_interpolation_low_branch(
        7u, 0x0045u, 0x0043u, dm, 0x2000u, pm, 0x2000u, NULL));

    if (failures == 0) puts("PASS");
    return failures == 0 ? 0 : 1;
}
