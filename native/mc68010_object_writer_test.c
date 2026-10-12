/* Tests for the MC68010 object record update slice at ROM $03CF40. */
#include "mc68010_object_writer.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define RAM_SIZE 0x8000u
#define RECORD_OFFSET 0x5D86u
#define RECORD_STRIDE 16u

static unsigned g_updater_03D120_calls;
static unsigned g_updater_03D1A0_calls;
static uint8_t *g_last_record;

static void mock_updater_03D120(uint8_t *record) { g_updater_03D120_calls++; g_last_record = record; }
static void mock_updater_03D1A0(uint8_t *record) { g_updater_03D1A0_calls++; g_last_record = record; }

static int check_eq(unsigned long long got, unsigned long long want, const char *what)
{
    if (got != want) {
        fprintf(stderr, "%s:%d: %s = %llu, expected %llu\n", __FILE__, __LINE__, what, got, want);
        return 0;
    }
    return 1;
}
#define CHECK_EQ(got, want) do { if (!check_eq((unsigned long long)(got), (unsigned long long)(want), #got)) return 1; } while (0)

/* ── Address calculation tests ── */

static int test_record_ptr_calculation(void)
{
    uint8_t *ram = calloc(RAM_SIZE, 1);
    uint8_t *ptr;
    printf("Test: record_ptr calculation (stride 16)\n");
    ptr = stunrun_object_record_ptr(0, ram, RAM_SIZE);
    CHECK_EQ((uintptr_t)(ptr - ram), (uintptr_t)RECORD_OFFSET);
    ptr = stunrun_object_record_ptr(1, ram, RAM_SIZE);
    CHECK_EQ((uintptr_t)(ptr - ram), (uintptr_t)(RECORD_OFFSET + 16u));
    ptr = stunrun_object_record_ptr(10, ram, RAM_SIZE);
    CHECK_EQ((uintptr_t)(ptr - ram), (uintptr_t)(RECORD_OFFSET + 160u));
    free(ram);
    return 0;
}

static int test_null_callbacks(void)
{
    uint8_t *ram = calloc(RAM_SIZE, 1);
    printf("Test: NULL callbacks return -1\n");
    CHECK_EQ(stunrun_mc68010_object_record_caller(0, 0, ram, RAM_SIZE, NULL, mock_updater_03D1A0), -1);
    CHECK_EQ(stunrun_mc68010_object_record_caller(0, 0, ram, RAM_SIZE, mock_updater_03D120, NULL), -1);
    free(ram);
    return 0;
}

static int test_null_ram(void)
{
    printf("Test: NULL RAM returns -1\n");
    CHECK_EQ(stunrun_mc68010_object_record_caller(0, 0, NULL, RAM_SIZE, mock_updater_03D120, mock_updater_03D1A0), -1);
    return 0;
}

static int test_both_updaters_called(void)
{
    uint8_t *ram = calloc(RAM_SIZE, 1);
    int i;
    printf("Test: both updaters called (stride 16)\n");
    for (i = 0; i < 16; i++) ram[RECORD_OFFSET + i] = (uint8_t)i;
    g_updater_03D120_calls = 0; g_updater_03D1A0_calls = 0;
    CHECK_EQ(stunrun_mc68010_object_record_caller(0, 0x42, ram, RAM_SIZE, mock_updater_03D120, mock_updater_03D1A0), 0);
    CHECK_EQ(g_updater_03D120_calls, 1u);
    CHECK_EQ(g_updater_03D1A0_calls, 1u);
    free(ram);
    return 0;
}

static int test_record_index_1(void)
{
    uint8_t *ram = calloc(RAM_SIZE, 1);
    int i;
    printf("Test: record index 1 -> base + 16\n");
    for (i = 0; i < 16; i++) ram[RECORD_OFFSET + 16 + i] = 0xAA;
    g_updater_03D120_calls = 0; g_updater_03D1A0_calls = 0;
    CHECK_EQ(stunrun_mc68010_object_record_caller(1, 0, ram, RAM_SIZE, mock_updater_03D120, mock_updater_03D1A0), 0);
    CHECK_EQ(g_updater_03D120_calls, 1u);
    CHECK_EQ(g_updater_03D1A0_calls, 1u);
    CHECK_EQ((uintptr_t)g_last_record, (uintptr_t)ram + RECORD_OFFSET + 16);
    free(ram);
    return 0;
}

static int test_input_byte_discarded(void)
{
    uint8_t *ram = calloc(RAM_SIZE, 1);
    int i;
    printf("Test: input byte is read but not used\n");
    ram[0] = 0x99;
    for (i = 0; i < 16; i++) ram[RECORD_OFFSET + i] = (uint8_t)i;
    g_updater_03D120_calls = 0;
    stunrun_mc68010_object_record_caller(0, 0x99, ram, RAM_SIZE, mock_updater_03D120, mock_updater_03D1A0);
    CHECK_EQ(g_updater_03D120_calls, 1u);
    free(ram);
    return 0;
}

static int test_ram_size_validation(void)
{
    uint8_t *ram_small = calloc(1, 1);
    uint8_t *ram = calloc(RAM_SIZE, 1);
    printf("Test: RAM size validation\n");
    CHECK_EQ(stunrun_object_record_ptr(0, ram_small, 1), NULL);
    CHECK_EQ(stunrun_object_record_ptr(0, ram, RAM_SIZE) != NULL, 1u);
    /* ptr_offset = 0x5D86 + 551*16 = 0x7FF6 < 0x8000 → valid */
    CHECK_EQ(stunrun_object_record_ptr(551, ram, RAM_SIZE) != NULL, 1u);
    /* ptr_offset = 0x5D86 + 552*16 = 0x8006 >= 0x8000 → NULL */
    CHECK_EQ(stunrun_object_record_ptr(552, ram, RAM_SIZE), NULL);
    free(ram_small); free(ram);
    return 0;
}

/* ── Clamp tests ── */

static int test_clamp_in_range_normal(void)
{
    uint8_t *ram = calloc(RAM_SIZE, 1);
    uint8_t *rec = &ram[RECORD_OFFSET];
    printf("Test: clamp in-range\n");
    rec[0] = 0x10; rec[1] = 0x30; rec[2] = 0x50;
    stunrun_mc68010_object_update_record(rec);
    CHECK_EQ(rec[0], 0x30U); CHECK_EQ(rec[1], 0x10U); CHECK_EQ(rec[2], 0x30U);
    free(ram); return 0;
}

static int test_clamp_pass_through(void)
{
    uint8_t *ram = calloc(RAM_SIZE, 1);
    uint8_t *rec = &ram[RECORD_OFFSET];
    printf("Test: clamp pass-through\n");
    rec[0] = 0x40; rec[1] = 0x30; rec[2] = 0x50;
    stunrun_mc68010_object_update_record(rec);
    CHECK_EQ(rec[0], 0x40U); CHECK_EQ(rec[1], 0x40U); CHECK_EQ(rec[2], 0x40U);
    free(ram); return 0;
}

static int test_clamp_below_range(void)
{
    uint8_t *ram = calloc(RAM_SIZE, 1);
    uint8_t *rec = &ram[RECORD_OFFSET];
    printf("Test: clamp below range\n");
    rec[0] = 0x05; rec[1] = 0x30; rec[2] = 0x50;
    stunrun_mc68010_object_update_record(rec);
    CHECK_EQ(rec[0], 0x30U); CHECK_EQ(rec[1], 0x05U); CHECK_EQ(rec[2], 0x30U);
    free(ram); return 0;
}

static int test_clamp_above_upper(void)
{
    uint8_t *ram = calloc(RAM_SIZE, 1);
    uint8_t *rec = &ram[RECORD_OFFSET];
    printf("Test: clamp above range\n");
    rec[0] = 0xFF; rec[1] = 0x80; rec[2] = 0x20;
    stunrun_mc68010_object_update_record(rec);
    CHECK_EQ(rec[0], 0x80U); CHECK_EQ(rec[1], 0xFFU); CHECK_EQ(rec[2], 0x80U);
    free(ram); return 0;
}

static int test_clamp_equal_bounds(void)
{
    uint8_t *ram = calloc(RAM_SIZE, 1);
    uint8_t *rec = &ram[RECORD_OFFSET];
    printf("Test: clamp equal bounds\n");
    rec[0] = 0x40; rec[1] = 0x80; rec[2] = 0x80;
    stunrun_mc68010_object_update_record(rec);
    CHECK_EQ(rec[0], 0x80U); CHECK_EQ(rec[1], 0x40U); CHECK_EQ(rec[2], 0x80U);
    free(ram); return 0;
}

static int test_clamp_inverted_bounds(void)
{
    uint8_t *ram = calloc(RAM_SIZE, 1);
    uint8_t *rec = &ram[RECORD_OFFSET];
    printf("Test: clamp inverted bounds\n");
    rec[0] = 0x7F; rec[1] = 0xFF; rec[2] = 0x00;
    stunrun_mc68010_object_update_record(rec);
    CHECK_EQ(rec[0], 0x7FU); CHECK_EQ(rec[1], 0x7FU); CHECK_EQ(rec[2], 0x7FU);
    free(ram); return 0;
}

static int test_clamp_exhaustive_sample(void)
{
    uint8_t *ram = calloc(RAM_SIZE, 1);
    uint8_t *rec = &ram[RECORD_OFFSET];
    unsigned errors = 0;
    printf("Test: exhaustive sample (1000 random)\n");
    srand(0x29c911a2U);
    for (unsigned i = 0; i < 1000; i++) {
        uint8_t v0 = (uint8_t)rand(), v1 = (uint8_t)rand(), v2 = (uint8_t)rand();
        uint8_t lower = (v1 <= v2) ? v1 : v2, upper = (v1 <= v2) ? v2 : v1;
        uint8_t clamped = (v0 < lower) ? lower : (v0 > upper) ? upper : v0;
        rec[0] = v0; rec[1] = v1; rec[2] = v2;
        stunrun_mc68010_object_update_record(rec);
        if (rec[0] != clamped || rec[1] != v0 || rec[2] != clamped) errors++;
    }
    free(ram);
    if (errors > 0) { fprintf(stderr, "%u errors\n", errors); return 1; }
    return 0;
}

/* ── Interpolation tests ── */

/* Test: Track 1 advances record[4] toward record[2] when record[4] < record[2].
 * After 6 calls with saved_target=0x10: record[4] = 0x04 + (0x10-0x04)/2 = 0x0A */
static int test_interp_track1_advances_toward_target(void)
{
    uint8_t *ram = calloc(RAM_SIZE, 1);
    uint8_t *rec = &ram[RECORD_OFFSET];
    int i;
    printf("Test: Track 1 advances toward target\n");
    rec[4] = 0x04; rec[2] = 0x10; rec[5] = 0; rec[6] = 0x10;
    rec[0xA] = 0x10; rec[0xB] = 0; rec[0xC] = 0x10; /* Track 2 inactive (equal) */
    for (i = 0; i < 6; i++) stunrun_mc68010_object_advance_interpolation(rec);
    CHECK_EQ(rec[4], 0x0AU);
    CHECK_EQ(rec[5], 0u);
    CHECK_EQ(rec[6], 0x10u);
    free(ram); return 0;
}

/* Test: Track 1 stops (resets counter) when record[4] >= record[2] */
static int test_interp_track1_stops_at_target(void)
{
    uint8_t *ram = calloc(RAM_SIZE, 1);
    uint8_t *rec = &ram[RECORD_OFFSET];
    printf("Test: Track 1 stops at target\n");
    rec[4] = 0x10; rec[2] = 0x10; rec[5] = 0; rec[6] = 0x08;
    stunrun_mc68010_object_advance_interpolation(rec);
    CHECK_EQ(rec[5], 0u);
    free(ram); return 0;
}

/* Test: Track 1 sets record[6]=0xFF when inactive */
static int test_interp_track1_inactive_marker(void)
{
    uint8_t *ram = calloc(RAM_SIZE, 1);
    uint8_t *rec = &ram[RECORD_OFFSET];
    printf("Test: Track 1 inactive marker\n");
    rec[4] = 0x10; rec[2] = 0x10; rec[5] = 0; rec[6] = 0x08;
    stunrun_mc68010_object_advance_interpolation(rec);
    CHECK_EQ(rec[6], 0xFFu);
    free(ram); return 0;
}

/* Test: Track 2 decreases record[10] toward record[2] when record[10] > record[2].
 * After 6 calls: record[10] = 0x10 - (0x10-0x04)/2 = 0x0A */
static int test_interp_track2_decreases_toward_target(void)
{
    uint8_t *ram = calloc(RAM_SIZE, 1);
    uint8_t *rec = &ram[RECORD_OFFSET];
    int i;
    printf("Test: Track 2 decreases toward target\n");
    rec[4] = 0x04; rec[2] = 0x04; rec[5] = 0; rec[6] = 0xFF;
    rec[0xA] = 0x10; rec[0xB] = 0; rec[0xC] = 0x04;
    for (i = 0; i < 6; i++) stunrun_mc68010_object_advance_interpolation(rec);
    CHECK_EQ(rec[0xA], 0x0AU);
    CHECK_EQ(rec[0xB], 0u);
    free(ram); return 0;
}

/* Test: Track 2 resets its counters when record[10] <= record[2] (bls branch).
 * Note: This test verifies bls branch TAKES the reset path.
 * The test checks that after 1 call, Track 2's counters are 0.
 * Track 1 is active and also increments, so Track 2's counter may be non-zero
 * if Track 1's advance path increments it (which happens when rec[4] < rec[2]).
 */
static int test_interp_track2_bls_resets_counters(void)
{
    uint8_t *ram = calloc(RAM_SIZE, 1);
    uint8_t *rec = &ram[RECORD_OFFSET];
    printf("Test: Track 2 bls branch takes reset path\n");
    /* Track 1 inactive: rec[4]=rec[2] → bcc inactive, counter stays 0 */
    /* Track 2: rec[10]=rec[2]=0x10 → bls inactive, counters reset to 0 */
    rec[4] = 0x10; rec[2] = 0x10; rec[5] = 0; rec[6] = 0xFF;
    rec[0xA] = 0x10; rec[0xB] = 0x03; rec[0xC] = 0x20; /* stale counters */
    stunrun_mc68010_object_advance_interpolation(rec);
    /* After 1 call: Track 2 counters reset to 0 by bls branch */
    CHECK_EQ(rec[0xB], 0u);
    CHECK_EQ(rec[0xC], 0u);
    free(ram); return 0;
}

/* Test: record[2] is overwritten by Track 2's write-back ($03D240)
 * which happens after Track 1's write-back ($03D1EC) */
static int test_interp_record2_overwritten_by_track2(void)
{
    uint8_t *ram = calloc(RAM_SIZE, 1);
    uint8_t *rec = &ram[RECORD_OFFSET];
    printf("Test: record[2] overwritten by Track 2\n");
    rec[4] = 0x04; rec[2] = 0x10; rec[5] = 5; rec[6] = 0x10;
    rec[0xA] = 0x10; rec[0xB] = 0; rec[0xC] = 0x10;
    stunrun_mc68010_object_advance_interpolation(rec);
    /* Track 2's write-back at $03D240 overwrites Track 1's at $03D1EC */
    CHECK_EQ(rec[2], 0x10U);
    free(ram); return 0;
}


/* Test: null RAM returns 0 */
static int test_copy_null_ram(void)
{
    uint8_t buf[32] = {0};
    printf("Test: copy with NULL RAM returns 0\n");
    CHECK_EQ(stunrun_mc68010_object_copy_to_buffer(0, 0, NULL, 0x8000, buf), 0u);
    return 0;
}

/* Test: null buffer returns 0 */
static int test_copy_null_buffer(void)
{
    uint8_t *ram = calloc(RAM_SIZE, 1);
    printf("Test: copy with NULL buffer returns 0\n");
    CHECK_EQ(stunrun_mc68010_object_copy_to_buffer(0, 0, ram, RAM_SIZE, NULL), 0u);
    free(ram); return 0;
}

/* Test: exact 16-byte copy from record to buffer */
static int test_copy_exact_16_bytes(void)
{
    uint8_t *ram = calloc(RAM_SIZE, 1);
    uint8_t buf[32] = {0};
    unsigned i;
    printf("Test: copy exact 16 bytes (record 0)\n");

    /* Fill record 0 at ram + RECORD_OFFSET with known data */
    for (i = 0; i < 16; i++) {
        ram[RECORD_OFFSET + i] = (uint8_t)(0xA0 + i);
    }

    stunrun_mc68010_object_copy_to_buffer(0, 0x1234, ram, RAM_SIZE, buf);

    for (i = 0; i < 16; i++) {
        CHECK_EQ(buf[i], (uint8_t)(0xA0 + i));
    }
    free(ram); return 0;
}

/* Test: record 1 is at offset +16 from base */
static int test_copy_record_stride_16(void)
{
    uint8_t *ram = calloc(RAM_SIZE, 1);
    uint8_t buf[32] = {0};
    unsigned i;
    printf("Test: copy uses stride 16 (record 1)\n");

    /* Fill record 1 at ram + RECORD_OFFSET + 16 */
    for (i = 0; i < 16; i++) {
        ram[RECORD_OFFSET + 16 + i] = (uint8_t)(0xB0 + i);
    }

    stunrun_mc68010_object_copy_to_buffer(1, 0, ram, RAM_SIZE, buf);

    for (i = 0; i < 16; i++) {
        CHECK_EQ(buf[i], (uint8_t)(0xB0 + i));
    }
    free(ram); return 0;
}

/* Test: record index 10 is at offset +160 */
static int test_copy_record_index_10(void)
{
    uint8_t *ram = calloc(RAM_SIZE, 1);
    uint8_t buf[32] = {0};
    unsigned i;
    printf("Test: copy record index 10\n");

    for (i = 0; i < 16; i++) {
        ram[RECORD_OFFSET + 160 + i] = (uint8_t)(0xC0 + i);
    }

    stunrun_mc68010_object_copy_to_buffer(10, 0, ram, RAM_SIZE, buf);

    for (i = 0; i < 16; i++) {
        CHECK_EQ(buf[i], (uint8_t)(0xC0 + i));
    }
    free(ram); return 0;
}

/* Test: buffer content is overwritten by record data */
static int test_copy_overwrites_buffer(void)
{
    uint8_t *ram = calloc(RAM_SIZE, 1);
    uint8_t buf[32] = {0};
    unsigned i;
    printf("Test: copy overwrites buffer\n");

    /* Pre-fill buffer with sentinel - must be overwritten by copy */
    for (i = 0; i < 16; i++) buf[i] = 0xCC;

    for (i = 0; i < 16; i++) {
        ram[RECORD_OFFSET + i] = (uint8_t)(0x11 * i);
    }

    stunrun_mc68010_object_copy_to_buffer(0, 0, ram, RAM_SIZE, buf);

    for (i = 0; i < 16; i++) {
        CHECK_EQ(buf[i], (uint8_t)(0x11 * i));
    }
    free(ram); return 0;
}

/* Test: record at maximum valid index (550) */
static int test_copy_max_valid_index(void)
{
    uint8_t *ram = calloc(RAM_SIZE, 1);
    uint8_t buf[32] = {0};
    unsigned i;
    /* ptr_offset = 0x5D86 + 550*16 = 0x5D86 + 0x2268 = 0x7FEE → valid
     * ptr_offset = 0x5D86 + 551*16 = 0x5D86 + 0x2270 = 0x7FF6 → overflows
     * 0x7FF6 + 16 = 0x8006 > 0x8000 (RAM end) */
    printf("Test: copy max valid index 550\n");

    for (i = 0; i < 16; i++) {
        ram[RECORD_OFFSET + 550 * 16 + i] = (uint8_t)(0xDD + i);
    }

    stunrun_mc68010_object_copy_to_buffer(550, 0, ram, RAM_SIZE, buf);

    for (i = 0; i < 16; i++) {
        CHECK_EQ(buf[i], (uint8_t)(0xDD + i));
    }
    free(ram); return 0;
}

/* Test: out-of-range index (552) returns 0 */
static int test_copy_index_out_of_range(void)
{
    uint8_t *ram = calloc(RAM_SIZE, 1);
    uint8_t buf[32] = {0};
    printf("Test: copy index 552 (out of range) returns 0\n");
    CHECK_EQ(stunrun_mc68010_object_copy_to_buffer(552, 0, ram, RAM_SIZE, buf), 0u);
    free(ram); return 0;
}

/* Test: geometry consumer stub returns 0 */
static int test_geometry_consumer_stub(void)
{
    uint8_t buf[32] = {0x11, 0x22, 0x33, 0x44, 0x55, 0x66, 0x77, 0x88,
                       0x99, 0xAA, 0xBB, 0xCC, 0xDD, 0xEE, 0xFF, 0x00};
    printf("Test: geometry consumer stub returns 0\n");
    CHECK_EQ(stunrun_mc68010_object_geometry_consumer(0x1234, buf), 0);
    CHECK_EQ(stunrun_mc68010_object_geometry_consumer(-1, buf), 0);
    return 0;
}


/* Forward declarations for $03E3FC geometry tests */
static int test_build_geometry_word_count(void);
static int test_build_geometry_known_values(void);
static int test_build_geometry_nonzero_index(void);
static int test_build_geometry_negative_arg(void);
static int test_build_geometry_selector(void);

/* ── Main (updated) ── */

int main(void)
{
    int failures = 0;
    printf("=== mc68010_object_writer tests ===\n");
    /* Original tests (updaters) */
    if (test_record_ptr_calculation() != 0) failures++;
    if (test_null_callbacks() != 0) failures++;
    if (test_null_ram() != 0) failures++;
    if (test_both_updaters_called() != 0) failures++;
    if (test_record_index_1() != 0) failures++;
    if (test_input_byte_discarded() != 0) failures++;
    if (test_ram_size_validation() != 0) failures++;
    if (test_clamp_in_range_normal() != 0) failures++;
    if (test_clamp_pass_through() != 0) failures++;
    if (test_clamp_below_range() != 0) failures++;
    if (test_clamp_above_upper() != 0) failures++;
    if (test_clamp_equal_bounds() != 0) failures++;
    if (test_clamp_inverted_bounds() != 0) failures++;
    if (test_clamp_exhaustive_sample() != 0) failures++;
    if (test_interp_track1_advances_toward_target() != 0) failures++;
    if (test_interp_track1_stops_at_target() != 0) failures++;
    if (test_interp_track1_inactive_marker() != 0) failures++;
    if (test_interp_track2_decreases_toward_target() != 0) failures++;
    if (test_interp_track2_bls_resets_counters() != 0) failures++;
    if (test_interp_record2_overwritten_by_track2() != 0) failures++;
    /* New tests ($03D012 copy function) */
    if (test_copy_null_ram() != 0) failures++;
    if (test_copy_null_buffer() != 0) failures++;
    if (test_copy_exact_16_bytes() != 0) failures++;
    if (test_copy_record_stride_16() != 0) failures++;
    if (test_copy_record_index_10() != 0) failures++;
    if (test_copy_overwrites_buffer() != 0) failures++;
    if (test_copy_max_valid_index() != 0) failures++;
    if (test_copy_index_out_of_range() != 0) failures++;
    if (test_geometry_consumer_stub() != 0) failures++;
    if (test_build_geometry_word_count() != 0) failures++;
    if (test_build_geometry_known_values() != 0) failures++;
    if (test_build_geometry_nonzero_index() != 0) failures++;
    if (test_build_geometry_negative_arg() != 0) failures++;
    if (test_build_geometry_selector() != 0) failures++;
    if (failures == 0) { printf("All tests passed.\n"); return 0; }
    else { printf("%d test(s) failed.\n", failures); return 1; }
}

/* ── Tests for $03E3FC object build geometry ─────────────────────────── */

static unsigned g_fifo_call_count;
static uint16_t g_fifo_words[7 * 5];
static unsigned g_fifo_word_count;

static void mock_fifo_word(void *ctx, uint16_t word)
{
    (void)ctx;
    if (g_fifo_word_count < 7 * 5) {
        g_fifo_words[g_fifo_word_count++] = word;
    }
}

static void reset_fifo_state(void)
{
    g_fifo_call_count = 0;
    g_fifo_word_count = 0;
    memset(g_fifo_words, 0, sizeof(g_fifo_words));
}

/* Helper: verify one packet (5 words) */
static int verify_packet(unsigned packet_idx,
                         uint16_t sel,
                         int16_t a1, int16_t a2, int16_t a3, int16_t a4)
{
    unsigned base = packet_idx * 5;
    int ok = 1;
    if (g_fifo_words[base + 0] != (uint16_t)(0x2000u + sel)) {
        fprintf(stderr, "packet %u[0]: got 0x%04X, expected 0x%04X\n",
                packet_idx, g_fifo_words[base + 0],
                (uint16_t)(0x2000u + sel));
        ok = 0;
    }
    if (g_fifo_words[base + 1] != (uint16_t)a1) {
        fprintf(stderr, "packet %u[1]: got 0x%04X, expected 0x%04X\n",
                packet_idx, g_fifo_words[base + 1], (uint16_t)a1);
        ok = 0;
    }
    if (g_fifo_words[base + 2] != (uint16_t)a2) {
        fprintf(stderr, "packet %u[2]: got 0x%04X, expected 0x%04X\n",
                packet_idx, g_fifo_words[base + 2], (uint16_t)a2);
        ok = 0;
    }
    if (g_fifo_words[base + 3] != (uint16_t)a3) {
        fprintf(stderr, "packet %u[3]: got 0x%04X, expected 0x%04X\n",
                packet_idx, g_fifo_words[base + 3], (uint16_t)a3);
        ok = 0;
    }
    if (g_fifo_words[base + 4] != (uint16_t)a4) {
        fprintf(stderr, "packet %u[4]: got 0x%04X, expected 0x%04X\n",
                packet_idx, g_fifo_words[base + 4], (uint16_t)a4);
        ok = 0;
    }
    return ok;
}

/* Test: with known buffer values, verify all 7 packets */
static int test_build_geometry_known_values(void)
{
    /* Buffer layout (16 bytes):
     * buffer[0x0A-0x0B] = record_index word (big-endian in MC68010)
     * buffer[0x0E-0x0F] = arg_word word
     * buffer[0x1B] = selector byte
     *
     * Use record_index = 0, arg_word = 0 (geometry result = 0)
     * D3 = record_index = 0
     * D2 = arg_word + 8 = 8
     * selector = 0x20
     */
    uint8_t buf[32] = {0};
    buf[0x0A] = 0x00; buf[0x0B] = 0x00; /* record_index = 0 */
    buf[0x0E] = 0x00; buf[0x0F] = 0x00; /* arg_word = 0 → D2 = 8 */
    buf[0x1B] = 0x20;                    /* selector */

    printf("Test: build geometry with record_index=0, arg_word=0\n");
    reset_fifo_state();

    stunrun_mc68010_object_build_geometry(0, 0, buf, NULL, mock_fifo_word);

    CHECK_EQ(g_fifo_word_count, 35u);  /* 7 packets × 5 words */

    /* Packet 1: (D2-3, D3+6, D2, D3) = (5, 6, 8, 0) */
    if (!verify_packet(0, 0x20, 5, 6, 8, 0)) return 1;

    /* Packet 2: (D2-3, D3+3, D2-3, D3+6) = (5, 3, 5, 6) */
    if (!verify_packet(1, 0x20, 5, 3, 5, 6)) return 1;

    /* Packet 3: (D2-6, D3+3, D2-3, D3+3) = (2, 3, 5, 3) */
    if (!verify_packet(2, 0x20, 2, 3, 5, 3)) return 1;

    /* Packet 4: (D2-6, D3-3, D2-6, D3+3) = (2, -3, 2, 3) */
    if (!verify_packet(3, 0x20, 2, -3, 2, 3)) return 1;

    /* Packet 5: (D2-3, D3-3, D2-3, D3-6) = (5, -3, 5, -6) */
    if (!verify_packet(4, 0x20, 5, -3, 5, -6)) return 1;

    /* Packet 6: (D2-3, D3-6, D2-3, D3-3) = (5, -6, 5, -3) */
    if (!verify_packet(5, 0x20, 5, -6, 5, -3)) return 1;

    /* Packet 7: (D2, D3, D2-3, D3-6) = (8, 0, 5, -6) */
    if (!verify_packet(6, 0x20, 8, 0, 5, -6)) return 1;

    return 0;
}

/* Test: record_index = 0x1234, arg_word = 0x0000
 * D3 = 0x1234, D2 = 8
 */
static int test_build_geometry_nonzero_index(void)
{
    uint8_t buf[32] = {0};
    buf[0x0A] = 0x12; buf[0x0B] = 0x34; /* record_index = 0x1234 */
    buf[0x0E] = 0x00; buf[0x0F] = 0x00; /* arg_word = 0 */
    buf[0x1B] = 0x20;                    /* selector */

    printf("Test: build geometry with record_index=0x1234\n");
    reset_fifo_state();

    stunrun_mc68010_object_build_geometry(0x1234, 0, buf, NULL, mock_fifo_word);

    CHECK_EQ(g_fifo_word_count, 35u);

    /* Packet 1: (D2-3, D3+6, D2, D3) = (5, 0x123A, 8, 0x1234) */
    if (!verify_packet(0, 0x20, 5, 0x123A, 8, 0x1234)) return 1;

    /* Packet 2: (D2-3, D3+3, D2-3, D3+6) = (5, 0x1237, 5, 0x123A) */
    if (!verify_packet(1, 0x20, 5, 0x1237, 5, 0x123A)) return 1;

    /* Packet 3: (D2-6, D3+3, D2-3, D3+3) = (2, 0x1237, 5, 0x1237) */
    if (!verify_packet(2, 0x20, 2, 0x1237, 5, 0x1237)) return 1;

    /* Packet 4: (D2-6, D3-3, D2-6, D3+3) = (2, 0x1231, 2, 0x1237) */
    if (!verify_packet(3, 0x20, 2, 0x1231, 2, 0x1237)) return 1;

    /* Packet 5: (D2-3, D3-3, D2-3, D3-6) = (5, 0x1231, 5, 0x122E) */
    if (!verify_packet(4, 0x20, 5, 0x1231, 5, 0x122E)) return 1;

    /* Packet 6: (D2-3, D3-6, D2-3, D3-3) = (5, 0x122E, 5, 0x1231) */
    if (!verify_packet(5, 0x20, 5, 0x122E, 5, 0x1231)) return 1;

    /* Packet 7: (D2, D3, D2-3, D3-6) = (8, 0x1234, 5, 0x122E) */
    if (!verify_packet(6, 0x20, 8, 0x1234, 5, 0x122E)) return 1;

    return 0;
}

/* Test: arg_word = 0xFFF0 (negative when sign-extended)
 * D2 = (int16_t)0xFFF0 + 8 = -16 + 8 = -8
 * D3 = 0
 */
static int test_build_geometry_negative_arg(void)
{
    uint8_t buf[32] = {0};
    buf[0x0A] = 0x00; buf[0x0B] = 0x00; /* record_index = 0 */
    buf[0x0E] = 0xFF; buf[0x0F] = 0xF0; /* arg_word = 0xFFF0 */
    buf[0x1B] = 0x20;

    printf("Test: build geometry with negative arg_word (0xFFF0)\n");
    reset_fifo_state();

    /* D2 = (int16_t)0xFFF0 + 8 = -16 + 8 = -8 */
    stunrun_mc68010_object_build_geometry(0, 0xFFF0, buf, NULL, mock_fifo_word);

    CHECK_EQ(g_fifo_word_count, 35u);

    /* Packet 1: (D2-3, D3+6, D2, D3) = (-11, 6, -8, 0) */
    if (!verify_packet(0, 0x20, -11, 6, -8, 0)) return 1;

    /* Packet 7: (D2, D3, D2-3, D3-6) = (-8, 0, -11, -6) */
    if (!verify_packet(6, 0x20, -8, 0, -11, -6)) return 1;

    return 0;
}

/* Test: verify 35 words total (7 calls × 5 words) */
static int test_build_geometry_word_count(void)
{
    uint8_t buf[32] = {0};
    buf[0x0A] = 0x00; buf[0x0B] = 0x00;
    buf[0x0E] = 0x00; buf[0x0F] = 0x00;
    buf[0x1B] = 0x20;

    printf("Test: build geometry produces 35 words\n");
    reset_fifo_state();

    stunrun_mc68010_object_build_geometry(0, 0, buf, NULL, mock_fifo_word);

    CHECK_EQ(g_fifo_word_count, 35u);
    return 0;
}

/* Test: selector from buffer[0x1B] controls the first FIFO word */
static int test_build_geometry_selector(void)
{
    uint8_t buf[32] = {0};
    buf[0x0A] = 0x00; buf[0x0B] = 0x00;
    buf[0x0E] = 0x00; buf[0x0F] = 0x00;
    buf[0x1B] = 0x55;  /* non-default selector */

    printf("Test: build geometry selector = 0x55\n");
    reset_fifo_state();

    stunrun_mc68010_object_build_geometry(0, 0, buf, NULL, mock_fifo_word);

    /* All 7 packets should have first word = 0x2055 */
    for (unsigned i = 0; i < 7; i++) {
        CHECK_EQ(g_fifo_words[i * 5 + 0], (uint16_t)(0x2000u + 0x55));
    }
    return 0;
}
