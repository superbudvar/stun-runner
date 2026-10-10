#include <stdio.h>
#include <stdint.h>
int main(void) {
    FILE *f;
    uint8_t buf[16];

    f = fopen("../roms/stunrun/136070-2102.200r", "rb");
    fread(buf, 1, 16, f); fclose(f);
    printf("even[0..15] = ");
    for (int i = 0; i < 16; i++) printf("%02x ", buf[i]);
    printf("\n");

    f = fopen("../roms/stunrun/136070-2101.210r", "rb");
    fread(buf, 1, 16, f); fclose(f);
    printf("odd[0..15]  = ");
    for (int i = 0; i < 16; i++) printf("%02x ", buf[i]);
    printf("\n");

    /* What do we get if we interleave as (even[i] << 8) | odd[i]? */
    f = fopen("../roms/stunrun/136070-2102.200r", "rb");
    uint8_t even[16]; fread(even, 1, 16, f); fclose(f);
    f = fopen("../roms/stunrun/136070-2101.210r", "rb");
    uint8_t odd[16]; fread(odd, 1, 16, f); fclose(f);
    printf("\nInterleaved words (MSB=even[i], LSB=odd[i]):\n");
    for (int i = 0; i < 8; i++)
        printf("  word[%d] = %02x%02x = %04x\n", i, even[i], odd[i], (even[i]<<8)|odd[i]);

    /* What about (odd[i] << 8) | even[i]? */
    printf("\nInterleaved words (MSB=odd[i], LSB=even[i]):\n");
    for (int i = 0; i < 8; i++)
        printf("  word[%d] = %02x%02x = %04x\n", i, odd[i], even[i], (odd[i]<<8)|even[i]);
    return 0;
}
