/* `./run.sh --adsp-som`: draw the ADSP SOM producer's derived output.
 *
 * This binary needs no ROM, no MAME and no Verilator. The ADSP produces the
 * display geometry; the capture of it is committed under reference/, so the
 * producer's output can be derived and drawn on any machine with a C compiler.
 * That is also why it is worth having as its own entry point rather than a
 * mode bolted onto the shell: the shell's job is to run the ORIGINAL game, and
 * this is a picture of our own derivation.
 *
 * What it prints is the part to read. The picture shows decoded record fields;
 * the per-record table shows the derived word stream and whether it matches
 * what the original game latched. If that table says 9/9, the picture is of
 * the real transaction.
 *
 * Options:
 *   --png PATH        also write the frame as a PNG
 *   --ppm PATH        also write the frame as a PPM
 *   --window          present in a window and wait for it to close
 *   --frames N        windowed frames to present before giving up (default 0)
 *   -h, --help        this text
 *
 * Environment:
 *   STUNRUN_VIDEO_BACKEND   backend name (default: autoselect, null headless)
 *   STUNRUN_ADSP_SOM_WINDOW 1 to default to --window
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#include "adsp_som_view.h"
#include "backend/backend.h"

static void usage(const char *program)
{
    printf("usage: %s [--png PATH] [--ppm PATH] [--window] [--frames N]\n",
           program);
    printf("  Derive the captured ADSP SOM transaction and draw its records.\n");
    printf("  Needs no ROM and no MAME. Exits non-zero if the derived words\n");
    printf("  do not match the capture.\n");
}

static int report(const stunrun_adsp_som_view_t *view)
{
    unsigned i, s;
    int all_match = 1;

    printf("adsp-som: %u records, %u spans, %u derived words "
           "(+2 prologue = %u latched)\n",
           view->record_count, view->total_spans, view->total_words,
           view->total_words + 2u);
    printf("adsp-som: run lengths %lu..%lu rows\n",
           view->run_rows_min, view->run_rows_max);
    printf("adsp-som: derived words matching the capture: %u/%u\n\n",
           view->records_matching, view->record_count);

    printf("  rec ctl   start_x  start_y  entry   spans  match   fields\n");
    for (i = 0; i < view->record_count; i++) {
        const stunrun_adsp_som_view_record_t *record = &view->records[i];
        printf("  %3u %04X  %04X    %04X    %04X/%04X %u      %s   ",
               i, record->control, record->start_x, record->start_y,
               record->entry_i1, record->entry_i2, record->span_count,
               record->matches_capture ? "yes" : "NO ");
        for (s = 0; s < record->span_count; s++) {
            const stunrun_adsp_som_view_span_t *span = &record->spans[s];
            printf("%sax1=%04X h=%u c=%04X/%04X",
                   s ? " " : "", span->ax1, span->run_rows,
                   span->side_3c_lo, span->side_3c_hi);
        }
        printf("\n");
        if (!record->matches_capture)
            all_match = 0;
    }

    if (!all_match) {
        fprintf(stderr,
                "adsp-som: the derived words do NOT match the capture; the "
                "picture is of something the original game did not emit\n");
        return 1;
    }
    return 0;
}

int main(int argc, char **argv)
{
    const char *png_path = NULL;
    const char *ppm_path = NULL;
    int windowed = 0;
    unsigned frames = 0u;
    stunrun_adsp_som_view_t view;
    stunrun_renderer_t renderer;
    int status;
    int i;

    for (i = 1; i < argc; i++) {
        if (strcmp(argv[i], "--png") == 0 && i + 1 < argc) {
            png_path = argv[++i];
        } else if (strcmp(argv[i], "--ppm") == 0 && i + 1 < argc) {
            ppm_path = argv[++i];
        } else if (strcmp(argv[i], "--window") == 0) {
            windowed = 1;
        } else if (strcmp(argv[i], "--frames") == 0 && i + 1 < argc) {
            frames = (unsigned)strtoul(argv[++i], NULL, 10);
        } else if (strcmp(argv[i], "-h") == 0 ||
                   strcmp(argv[i], "--help") == 0) {
            usage(argv[0]);
            return 0;
        } else {
            fprintf(stderr, "adsp-som: unknown option %s\n", argv[i]);
            usage(argv[0]);
            return 2;
        }
    }
    if (getenv("STUNRUN_ADSP_SOM_WINDOW") != NULL &&
        getenv("STUNRUN_ADSP_SOM_WINDOW")[0] != '\0')
        windowed = 1;

    if (!stunrun_adsp_som_view_build(&view)) {
        fprintf(stderr, "adsp-som: derivation failed\n");
        return 1;
    }

    stunrun_render_init(&renderer);
    stunrun_render_begin(&renderer, 0u, 0x0au, 0x0cu, 0x14u);
    if (!stunrun_adsp_som_view_render(&renderer, &view)) {
        fprintf(stderr, "adsp-som: render failed\n");
        return 1;
    }

    status = report(&view);

    {
        unsigned lit = 0u, colours = 0u;
        stunrun_render_content_stats(&renderer, &lit, &colours);
        printf("\nadsp-som: frame hash %08X, %u lit pixels, %u distinct colours\n",
               stunrun_render_hash(&renderer), lit, colours);
    }

    if (png_path != NULL && !stunrun_render_write_png(&renderer, png_path)) {
        fprintf(stderr, "adsp-som: PNG write failed: %s\n", png_path);
        status = status ? status : 1;
    } else if (png_path != NULL) {
        printf("adsp-som: wrote %s\n", png_path);
    }
    if (ppm_path != NULL && !stunrun_render_write_ppm(&renderer, ppm_path)) {
        fprintf(stderr, "adsp-som: PPM write failed: %s\n", ppm_path);
        status = status ? status : 1;
    } else if (ppm_path != NULL) {
        printf("adsp-som: wrote %s\n", ppm_path);
    }

    if (windowed) {
        stunrun_backend_pair_t pair;
        stunrun_video_frame_t frame;
        unsigned shown;

        memset(&pair, 0, sizeof(pair));
        if (!stunrun_backend_pair_init(&pair, NULL, STUNRUN_RENDER_WIDTH,
                                       STUNRUN_RENDER_HEIGHT,
                                       STUNRUN_RENDER_WIDTH * 2u,
                                       STUNRUN_RENDER_HEIGHT * 2u, 0u)) {
            fprintf(stderr, "adsp-som: backend init failed; render without "
                            "--window, or set STUNRUN_VIDEO_BACKEND=null\n");
            return status ? status : 3;
        }
        if (frames == 0u)
            frames = 600u;   /* ~10 s at 60 Hz, then exit */

        stunrun_video_frame_init(&frame, STUNRUN_RENDER_WIDTH,
                                 STUNRUN_RENDER_HEIGHT,
                                 STUNRUN_RENDER_WIDTH * 3u,
                                 STUNRUN_VIDEO_FORMAT_RGB888,
                                 renderer.pixels);
        for (shown = 0u; shown < frames; shown++) {
            struct timespec pause;
            if (pair.video.upload == NULL ||
                !pair.video.upload(pair.video.user, &frame)) {
                fprintf(stderr, "adsp-som: upload failed\n");
                stunrun_backend_pair_shutdown(&pair);
                return status ? status : 3;
            }
            /* present is void in the backend vtable, so it cannot be part of
             * the success test above; an optional step cannot fail here. */
            if (pair.video.present != NULL)
                pair.video.present(pair.video.user);
            /* Bounded, not interactive: this binary deliberately has no event
             * loop, so it holds the frame and exits rather than pretending to
             * be a game window. */
            pause.tv_sec = 0;
            pause.tv_nsec = 16666667L;
            nanosleep(&pause, NULL);
        }
        stunrun_backend_pair_shutdown(&pair);
    }

    return status;
}