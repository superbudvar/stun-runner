#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>

int main(int argc, char **argv)
{
    if (argc < 3) { fprintf(stderr, "Usage: %s <roms_dir> <bank>\n", argv[0]); return 1; }
    const char *roms_dir = argv[1];
    unsigned bank = atoi(argv[2]);

    typedef struct { const char *even; const char *odd; unsigned offset; } Bank;
    Bank BANKS[] = {
        { "136070-2102.200r", "136070-2101.210r", 0x00000 },
        { "136070-6104.200s", "136070-6103.210s", 0x20000 },
        { "136070-6106.200t", "136070-6105.210t", 0x40000 },
        { "136070-2108.200u", "136070-2107.210u", 0x60000 },
        { "136070-2110.200v", "136070-2109.210v", 0x80000 },
        { "136070-2112.200w", "136070-2111.210w", 0xA0000 },
    };

    uint8_t even[65536], odd[65536];
    char path[512];
    snprintf(path, sizeof(path), "%s/%s", roms_dir, BANKS[bank].even);
    FILE *f = fopen(path, "rb");
    if (!f) { fprintf(stderr, "cannot open %s\n", path); return 1; }
    size_t n = fread(even, 1, 65536, f);
    fclose(f);
    fprintf(stderr, "even file read %zu bytes\n", n);

    snprintf(path, sizeof(path), "%s/%s", roms_dir, BANKS[bank].odd);
    f = fopen(path, "rb");
    if (!f) { fprintf(stderr, "cannot open %s\n", path); return 1; }
    n = fread(odd, 1, 65536, f);
    fclose(f);
    fprintf(stderr, "odd file read %zu bytes\n", n);

    fprintf(stderr, "even[0..7] = %02x %02x %02x %02x %02x %02x %02x %02x\n",
            even[0], even[1], even[2], even[3], even[4], even[5], even[6], even[7]);
    fprintf(stderr, "odd[0..7]  = %02x %02x %02x %02x %02x %02x %02x %02x\n",
            odd[0], odd[1], odd[2], odd[3], odd[4], odd[5], odd[6], odd[7]);

    fprintf(stderr, "word0 = even[0]<<8|odd[1] = %02x<<8|%02x = %04x\n", even[0], odd[1], (even[0]<<8)|odd[1]);
    fprintf(stderr, "word0 = even[0]<<8|odd[0] = %02x<<8|%02x = %04x (just odd[0])\n", even[0], odd[0], (even[0]<<8)|odd[0]);

    return 0;
}
