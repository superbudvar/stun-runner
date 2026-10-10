/* frame_diff.c — Compare two frame images (PPM or PNG) and report diff metrics.
 *
 * Usage: frame_diff <imgA> <imgB> [threshold=10]
 *
 * Exit codes:
 *   0 — images are identical (max pixel distance = 0)
 *   1 — images differ (max pixel distance >= threshold)
 *   2 — usage / file error
 *
 * Output:
 *   width, height, total_pixels
 *   pixels_different (count and percentage)
 *   max_rgb_distance (per channel)
 *   ssim_score (0.0 – 1.0, simplified structural similarity)
 *   diff.png — annotated diff image (different pixels in red)
 *
 * Requires: stb_image.h (read), stb_image_write.h (write)
 * Compiled with STB_IMAGE_IMPLEMENTATION + STB_IMAGE_WRITE_IMPLEMENTATION.
 */

#define STB_IMAGE_IMPLEMENTATION
#define STB_IMAGE_WRITE_IMPLEMENTATION
#include "../third_party/stb/stb_image.h"
#include "../third_party/stb/stb_image_write.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <math.h>

/* ── Auto-detect format from magic bytes ── */

static int file_format(const char *path)
{
    FILE *f = fopen(path, "rb");
    if (!f) return -1;
    unsigned char magic[4];
    size_t n = fread(magic, 1, 4, f);
    fclose(f);
    if (n < 3) return -1;
    if (magic[0] == 'P' && magic[1] == '6' && magic[2] == '\n') return 1; /* PPM P6 */
    if (magic[0] == 0x89 && magic[1] == 'P' && magic[2] == 'N' && magic[3] == 'G') return 2; /* PNG */
    return -1;
}

/* ── PPM loader (binary P6, 8-bit only) ── */

static uint8_t *load_ppm(const char *path, int *w, int *h)
{
    FILE *f = fopen(path, "rb");
    if (!f) return NULL;
    char line[256];
    if (!fgets(line, sizeof(line), f) || line[0] != 'P' || line[1] != '6') { fclose(f); return NULL; }
    while (fgets(line, sizeof(line), f)) {
        if (line[0] == '#') continue;
        break;
    }
    if (sscanf(line, "%d %d", w, h) != 2) { fclose(f); return NULL; }
    if (!fgets(line, sizeof(line), f)) { fclose(f); return NULL; }
    int maxval;
    if (sscanf(line, "%d", &maxval) != 1 || maxval != 255) { fclose(f); return NULL; }
    size_t pixels = (size_t)(*w) * (*h);
    uint8_t *rgb = malloc(pixels * 3);
    if (!rgb) { fclose(f); return NULL; }
    size_t got = fread(rgb, 1, pixels * 3, f);
    fclose(f);
    if (got != pixels * 3) { free(rgb); return NULL; }
    return rgb;
}

/* ── Main ── */

int main(int argc, char **argv)
{
    if (argc < 3) {
        fprintf(stderr, "usage: %s <imgA> <imgB> [threshold=10]\n", argv[0]);
        return 2;
    }
    const char *path_a = argv[1];
    const char *path_b = argv[2];
    int threshold = (argc > 3) ? atoi(argv[3]) : 10;
    if (threshold < 0) threshold = 0;

    /* Load image A */
    int w_a = 0, h_a = 0, ch_a = 0;
    uint8_t *rgb_a = NULL;
    int fmt_a = file_format(path_a);
    if (fmt_a == 1)      rgb_a = load_ppm(path_a, &w_a, &h_a);
    else if (fmt_a == 2) rgb_a = stbi_load(path_a, &w_a, &h_a, &ch_a, 3);
    if (!rgb_a) { fprintf(stderr, "frame_diff: cannot load %s\n", path_a); return 2; }

    /* Load image B */
    int w_b = 0, h_b = 0, ch_b = 0;
    uint8_t *rgb_b = NULL;
    int fmt_b = file_format(path_b);
    if (fmt_b == 1)      rgb_b = load_ppm(path_b, &w_b, &h_b);
    else if (fmt_b == 2) rgb_b = stbi_load(path_b, &w_b, &h_b, &ch_b, 3);
    if (!rgb_b) { fprintf(stderr, "frame_diff: cannot load %s\n", path_b); free(rgb_a); return 2; }

    if (w_a != w_b || h_a != h_b) {
        fprintf(stderr, "frame_diff: dimension mismatch %dx%d vs %dx%d\n",
                w_a, h_a, w_b, h_b);
        free(rgb_a); free(rgb_b); return 2;
    }

    const int W = w_a, H = h_a;
    const size_t N = (size_t)W * H;

    /* ── Per-pixel diff + accumulate statistics ── */
    int max_dist = 0;
    size_t diff_count = 0;
    uint64_t sum_dr = 0, sum_dg = 0, sum_db = 0;

    /* SSIM components: use per-channel luminance mean and variance */
    double sum_mu_a = 0, sum_mu_b = 0;
    double sum_var_a = 0, sum_var_b = 0;
    double sum_covar = 0;

    uint8_t *diff_img = malloc(N * 3);
    if (!diff_img) { free(rgb_a); free(rgb_b); return 2; }

    for (size_t i = 0; i < N; i++) {
        int r1 = rgb_a[i*3+0], g1 = rgb_a[i*3+1], b1 = rgb_a[i*3+2];
        int r2 = rgb_b[i*3+0], g2 = rgb_b[i*3+1], b2 = rgb_b[i*3+2];

        int dr = abs(r1 - r2);
        int dg = abs(g1 - g2);
        int db = abs(b1 - b2);
        int dist = (dr >= dg && dr >= db) ? dr : (dg >= db ? dg : db);
        if (dist > max_dist) max_dist = dist;
        if (dist >= threshold) { diff_count++; diff_img[i*3+0] = 255; diff_img[i*3+1] = 0; diff_img[i*3+2] = 0; }
        else { diff_img[i*3+0] = 0; diff_img[i*3+1] = 0; diff_img[i*3+2] = 0; }

        sum_dr += (uint64_t)dr;
        sum_dg += (uint64_t)dg;
        sum_db += (uint64_t)db;

        /* Per-channel luminance for SSIM approximation */
        double mu_a = (r1 + g1 + b1) / 3.0;
        double mu_b = (r2 + g2 + b2) / 3.0;
        sum_mu_a += mu_a;
        sum_mu_b += mu_b;

        /* Pixel variance */
        double vr_a = (r1 - mu_a)*(r1 - mu_a) + (g1 - mu_a)*(g1 - mu_a) + (b1 - mu_a)*(b1 - mu_a);
        double vr_b = (r2 - mu_b)*(r2 - mu_b) + (g2 - mu_b)*(g2 - mu_b) + (b2 - mu_b)*(b2 - mu_b);
        sum_var_a += vr_a;
        sum_var_b += vr_b;

        /* Covariance */
        double co = (r1 - mu_a)*(r2 - mu_b) + (g1 - mu_a)*(g2 - mu_b) + (b1 - mu_a)*(b2 - mu_b);
        sum_covar += co;
    }

    double N_f = (double)N;
    double avg_r = (double)sum_dr / N_f;
    double avg_g = (double)sum_dg / N_f;
    double avg_b = (double)sum_db / N_f;
    double pct   = 100.0 * (double)diff_count / N_f;

    /* Simplified SSIM: compare distributions of per-pixel luminance.
     * Full SSIM requires 8×8 sliding window; this uses global stats. */
    double mu_a_avg = sum_mu_a / N_f;
    double mu_b_avg = sum_mu_b / N_f;
    double var_a    = sum_var_a / N_f;
    double var_b    = sum_var_b / N_f;
    double covar    = sum_covar / N_f;

    double C1 = 6.5024, C2 = 58.5225;
    double l  = (2.0 * mu_a_avg * mu_b_avg + C1) / (mu_a_avg*mu_a_avg + mu_b_avg*mu_b_avg + C1);
    double qv = (2.0 * sqrt(var_a) * sqrt(var_b) + C2) / (var_a + var_b + C2);
    double cv = (covar + C2/2.0) / (sqrt(var_a) * sqrt(var_b) + C2/2.0);
    double ssim = l * qv * cv;  /* luminance × quality × contrast */

    printf("frame_diff: width=%d height=%d total_pixels=%zu\n", W, H, N);
    printf("frame_diff: pixels_different=%zu (%.2f%%)\n", diff_count, pct);
    printf("frame_diff: max_rgb_distance=%d\n", max_dist);
    printf("frame_diff: avg_channel_distance R=%.2f G=%.2f B=%.2f\n", avg_r, avg_g, avg_b);
    printf("frame_diff: ssim_score=%.6f\n", ssim);

    /* Write diff image */
    int n = stbi_write_png("diff.png", W, H, 3, diff_img, W * 3);
    if (n) printf("frame_diff: diff.png written (%d bytes)\n", n);
    else   printf("frame_diff: diff.png FAILED\n");

    free(rgb_a);
    free(rgb_b);
    free(diff_img);

    return (max_dist == 0) ? 0 : 1;
}
