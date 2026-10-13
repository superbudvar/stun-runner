/* Road/Track Data Decoder for S.T.U.N. Runner
 *
 * Decodes the road geometry from the 68010 ROM.
 * Each 16-bit word is expanded via road_expand_bits to 32 bits:
 *   - Upper 8 bits: color/type code
 *   - Bits 16-23: left edge position
 *   - Bits 0-7: right edge position
 *
 * Color codes:
 *   0x2a = ROAD (asphalt surface)
 *   0x22 = EDGE (barrier/edge markings)
 *   0x20 = SHOULDER (rumble strip)
 *   0x08 = CENTER (center line dashes)
 *   0x00 = EMPTY (no drawing)
 */

#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <string.h>

/* ROM helpers */
static uint16_t rom_word(const uint8_t *rom, uint32_t address)
{
    return (uint16_t)(((uint16_t)rom[address] << 8) |
                      (uint16_t)rom[address + 1]);
}

/* road_expand_bits from mc68010_road_writer.c */
static uint32_t road_expand_bits(uint16_t word)
{
    uint32_t d6 = 0;
    uint16_t d5 = word;
    unsigned i;

    for (i = 0; i < 16; i++) {
        d5 = (uint16_t)((d5 << 1) | (d6 >> 31));
        d6 = (d6 << 2) | (d5 >> 15);
    }
    d6 >>= 1;
    return d6;
}

/* Track table addresses */
typedef struct {
    const char *name;
    uint32_t rom_base;
} track_entry_t;

static const track_entry_t track_tables[] = {
    {"Track 0",  0x044630},
    {"Track 1",  0x044930},
    {"Track 2",  0x044C30},
    {"Track 3",  0x044F30},
    {"Track 4",  0x045530},
    {"Track 5",  0x045230},
    {"Track 6",  0x045530},
};
static const unsigned NUM_TRACKS = sizeof(track_tables) / sizeof(track_tables[0]);

/* Color/type names */
static const char *color_name(uint8_t code)
{
    switch (code) {
        case 0x2a: return "ROAD";
        case 0x22: return "EDGE";
        case 0x20: return "SHOULDER";
        case 0x08: return "CENTER";
        case 0x00: return "EMPTY";
        case 0x02: return "TUNNEL_A";
        case 0x0a: return "TUNNEL_B";
        case 0x28: return "TUNNEL_C";
        default:   return "UNKNOWN";
    }
}

int main(int argc, char **argv)
{
    const char *rom_path = NULL;
    unsigned track_idx = 0;
    int decode_strips = 0;
    int raw_dump = 0;

    int i;
    for (i = 1; i < argc; i++) {
        if (strcmp(argv[i], "-t") == 0 && i + 1 < argc) {
            track_idx = atoi(argv[++i]);
        } else if (strcmp(argv[i], "-s") == 0) {
            decode_strips = 1;
        } else if (strcmp(argv[i], "-r") == 0) {
            raw_dump = 1;
        } else if (strcmp(argv[i], "-l") == 0) {
            printf("Available tracks:\n");
            unsigned j;
            for (j = 0; j < NUM_TRACKS; j++) {
                printf("  %u: %s (ROM base 0x%06X)\n",
                       j, track_tables[j].name, track_tables[j].rom_base);
            }
            return 0;
        } else if (argv[i][0] != "-") {
            rom_path = argv[i];
        }
    }

    if (!rom_path) {
        fprintf(stderr, "Usage: %s <rom_path> [options]\n", argv[0]);
        fprintf(stderr, "  -t <n>  Select track (0-%u)\n", NUM_TRACKS - 1);
        fprintf(stderr, "  -s      Decode strip table data\n");
        fprintf(stderr, "  -r      Raw dump (all 384 words)\n");
        fprintf(stderr, "  -l      List tracks\n");
        return 1;
    }

    if (track_idx >= NUM_TRACKS) {
        fprintf(stderr, "Track index out of range (0-%u)\n", NUM_TRACKS - 1);
        return 1;
    }

    FILE *f = fopen(rom_path, "rb");
    if (!f) {
        fprintf(stderr, "Cannot open ROM: %s\n", rom_path);
        return 1;
    }
    uint8_t *rom = malloc(1024 * 1024);
    size_t bytes = fread(rom, 1, 1024 * 1024, f);
    fclose(f);

    printf("ROM: %zu bytes\n", bytes);
    printf("Track: %s at 0x%06X\n\n",
           track_tables[track_idx].name, track_tables[track_idx].rom_base);

    if (decode_strips) {
        printf("=== Strip Data Table (at 0x22E1E) ===\n\n");
        
        uint32_t strip_base = 0x022E1E;
        int idx;
        for (idx = 0x2A; idx <= 0x3F; idx++) {
            uint32_t table = strip_base + (idx - 0x2A) * 32;
            
            int has_data = 0;
            int row;
            for (row = 0; row < 8; row++) {
                uint16_t w = rom_word(rom, table + row * 2);
                if (w != 0) { has_data = 1; break; }
            }
            
            if (!has_data) continue;
            
            printf("Index 0x%02X (table at 0x%06X):\n", idx, table);
            printf("  %-4s  %-8s  %-8s  %-8s  %s\n",
                   "Row", "Raw", "Expanded", "Left", "Right");
            printf("  ----------------------------------------\n");
            
            int left_sum = 0, right_sum = 0;
            for (row = 0; row < 8; row++) {
                uint16_t w = rom_word(rom, table + row * 2);
                uint32_t exp = road_expand_bits(w);
                int left = (exp >> 16) & 0xff;
                int right = exp & 0xff;
                left_sum += left;
                right_sum += right;
                printf("  %2d   0x%04X    0x%08X  %-8d  %-8d\n",
                       row, w, exp, left, right);
            }
            printf("  (avg center: %d)\n\n", (left_sum + right_sum) / 16);
        }
    }

    printf("\n=== Track Table Decoding (%u words) ===\n", raw_dump ? 384 : 64);

    uint32_t base = track_tables[track_idx].rom_base;
    unsigned w;
    unsigned limit = raw_dump ? 384 : 64;
    
    printf("%-4s  %-6s  %-8s  %-4s  %-5s  %-5s  %s\n",
           "Idx", "Raw", "Expanded", "Code", "Left", "Right", "Type");
    printf("------------------------------------------------------------\n");

    for (w = 0; w < limit; w++) {
        uint16_t raw = rom_word(rom, base + w * 2);
        uint32_t exp = road_expand_bits(raw);
        uint8_t code = (exp >> 24) & 0xff;
        int left = (exp >> 16) & 0xff;
        int right = exp & 0xff;
        
        const char *type = color_name(code);
        
        int curve = 0;
        if (code == 0x2a || code == 0x22) {
            int center = (left + right) / 2;
            if (center < 70) curve = -1;
            else if (center > 100) curve = 1;
        }
        
        const char *curve_str = "";
        if (curve < 0) curve_str = " <-LEFT";
        else if (curve > 0) curve_str = " RIGHT->";
        
        printf("%3u   0x%04X   0x%08X  0x%02X   %-5d  %-5d  %s%s\n",
               w, raw, exp, code, left, right, type, curve_str);
    }

    if (!raw_dump) {
        printf("\n  ... (showing first 64 of 384 words, use -r for full dump)\n");
    }

    free(rom);
    return 0;
}
