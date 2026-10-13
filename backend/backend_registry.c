/* Backend registry.
 *
 * Provides a static registration system so that each backend .c file can
 * declare its entries without any central header listing them all.  The
 * registry is built as a linked list of backend descriptors.
 *
 * Registration happens at program startup via static constructors.  The
 * registry is fully populated before any call to stunrun_video_backend_find()
 * or stunrun_audio_backend_find().
 */
#include "backend.h"

#include <assert.h>
#include <stddef.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* ---- Registry node ---- */
typedef struct backend_node {
    enum { VIDEO_KIND, AUDIO_KIND } kind;
    union {
        const stunrun_video_backend_t *vid;
        const stunrun_audio_backend_t *aud;
    } be;
    struct backend_node *next;
} backend_node_t;

static backend_node_t *video_registry = NULL;
static backend_node_t *audio_registry = NULL;

/* ---- Registration entry points ---- */
/* Called by each backend's static init section */
void stunrun_backend_register_video(const stunrun_video_backend_t *vid)
{
    backend_node_t *n = (backend_node_t *)malloc(sizeof(backend_node_t));
    if (!n) {
        fprintf(stderr, "backend_registry: OOM registering video backend '%s'\n",
                vid->name ? vid->name : "(null)");
        return;
    }
    n->kind = VIDEO_KIND;
    n->be.vid = vid;
    n->next = video_registry;
    video_registry = n;
}

void stunrun_backend_register_audio(const stunrun_audio_backend_t *aud)
{
    backend_node_t *n = (backend_node_t *)malloc(sizeof(backend_node_t));
    if (!n) {
        fprintf(stderr, "backend_registry: OOM registering audio backend '%s'\n",
                aud->name ? aud->name : "(null)");
        return;
    }
    n->kind = AUDIO_KIND;
    n->be.aud = aud;
    n->next = audio_registry;
    audio_registry = n;
}

/* ---- Lookup ---- */
const stunrun_video_backend_t *
stunrun_video_backend_find(const char *name)
{
    if (!name)
        return NULL;
    for (backend_node_t *n = video_registry; n; n = n->next) {
        if (n->kind == VIDEO_KIND && n->be.vid->name &&
            strcmp(n->be.vid->name, name) == 0)
            return n->be.vid;
    }
    return NULL;
}

const stunrun_audio_backend_t *
stunrun_audio_backend_find(const char *name)
{
    if (!name)
        return NULL;
    for (backend_node_t *n = audio_registry; n; n = n->next) {
        if (n->kind == AUDIO_KIND && n->be.aud->name &&
            strcmp(n->be.aud->name, name) == 0)
            return n->be.aud;
    }
    return NULL;
}

/* ---- Autoselect ---- */
static const char *platform_autoselect_video(void)
{
#if defined(__APPLE__)
    return "metal";
#elif defined(__ANDROID__)
    return "vulkan";   /* or "opengles" if Vulkan unavailable */
#elif defined(__EMSCRIPTEN__)
    return "sdl_opengl";   /* WebGL via SDL */
#elif defined(__linux__) || defined(_WIN32) || defined(__FreeBSD__)
    return "sdl_opengl";
#else
    (void)0;  /* suppress empty translation unit warning */
    return "null";
#endif
}

/* ---- Backend pair init ---- */
static bool init_video_backend(stunrun_backend_pair_t *pair,
                                const char *name,
                                void *window_ctx,
                                uint32_t game_w, uint32_t game_h,
                                uint32_t win_w, uint32_t win_h)
{
    const stunrun_video_backend_t *templ =
        name ? stunrun_video_backend_find(name) : NULL;
    if (!templ) {
        fprintf(stderr, "backend_registry: video backend '%s' not found\n",
                name ? name : "(auto)");
        return false;
    }
    /* Copy the template into the inline struct in pair->video */
    pair->video = *templ;

    /* Allocate per-instance state */
    size_t state_size = templ->user_size ? templ->user_size : 256u;
    void *state = calloc(1, state_size);
    if (!state) {
        fprintf(stderr, "backend_registry: OOM for video backend state\n");
        return false;
    }
    pair->video.user = state;

    if (pair->video.create && !pair->video.create(state, window_ctx)) {
        fprintf(stderr, "backend_registry: video backend create() failed\n");
        free(state);
        return false;
    }
    if (pair->video.init && !pair->video.init(state, game_w, game_h, win_w, win_h)) {
        fprintf(stderr, "backend_registry: video backend init() failed\n");
        if (pair->video.shutdown) pair->video.shutdown(state);
        free(state);
        return false;
    }
    return true;
}

static bool init_audio_backend(stunrun_backend_pair_t *pair,
                                const char *name,
                                uint32_t sample_rate)
{
    const stunrun_audio_backend_t *templ =
        name ? stunrun_audio_backend_find(name) : NULL;
    if (!templ) {
        fprintf(stderr, "backend_registry: audio backend '%s' not found\n",
                name ? name : "(auto)");
        return false;
    }
    pair->audio = *templ;

    void *state = calloc(1, 256);
    if (!state) {
        fprintf(stderr, "backend_registry: OOM for audio backend state\n");
        return false;
    }
    pair->audio.user = state;

    if (pair->audio.create && !pair->audio.create(state)) {
        fprintf(stderr, "backend_registry: audio backend create() failed\n");
        free(state);
        return false;
    }
    if (pair->audio.init && !pair->audio.init(state, sample_rate)) {
        fprintf(stderr, "backend_registry: audio backend init() failed\n");
        if (pair->audio.shutdown) pair->audio.shutdown(state);
        free(state);
        return false;
    }
    return true;
}

bool stunrun_backend_pair_init(stunrun_backend_pair_t *pair,
                                void *window_ctx,
                                uint32_t game_w, uint32_t game_h,
                                uint32_t win_w, uint32_t win_h,
                                uint32_t audio_sample_rate)
{
    if (!pair)
        return false;
    memset(pair, 0, sizeof(*pair));

    const char *vid_name = getenv("STUNRUN_VIDEO_BACKEND");
    const char *aud_name = getenv("STUNRUN_AUDIO_BACKEND");

    /* Empty or "0" means "don't use this backend" */
    if (vid_name && (vid_name[0] == '\0' || strcmp(vid_name, "0") == 0))
        vid_name = NULL;
    if (aud_name && (aud_name[0] == '\0' || strcmp(aud_name, "0") == 0))
        aud_name = NULL;

    /* Autoselect if not specified */
    if (!vid_name)
        vid_name = platform_autoselect_video();
    if (!aud_name)
        aud_name = "sdl_audio";

    if (strcmp(vid_name, "null") != 0) {
        if (!init_video_backend(pair, vid_name, window_ctx,
                               game_w, game_h, win_w, win_h))
            return false;
    } else {
        /* The null backend is deliberately a no-op, so init_video_backend() is
         * skipped and the zeroed pair would report its name as NULL -- which the
         * caller prints as "(auto)", making an explicit "null" request look
         * like an unresolved autoselect.  Record the choice so the run log says
         * what was actually asked for. */
        pair->video.name = "null";
    }
    if (strcmp(aud_name, "null") != 0) {
        if (!init_audio_backend(pair, aud_name, audio_sample_rate))
            return false;
    } else {
        pair->audio.name = "null";
    }
    return true;
}

void stunrun_backend_pair_shutdown(stunrun_backend_pair_t *pair)
{
    if (!pair)
        return;
    if (pair->video.shutdown && pair->video.user) {
        pair->video.shutdown(pair->video.user);
        free(pair->video.user);
        pair->video.user = NULL;
    }
    if (pair->audio.shutdown && pair->audio.user) {
        pair->audio.shutdown(pair->audio.user);
        if (pair->audio.destroy)
            pair->audio.destroy(pair->audio.user);
        free(pair->audio.user);
        pair->audio.user = NULL;
    }
}

void stunrun_backend_pair_destroy(stunrun_backend_pair_t *pair)
{
    if (!pair)
        return;
    if (pair->video.destroy && pair->video.user)
        pair->video.destroy(pair->video.user);
    if (pair->audio.destroy && pair->audio.user)
        pair->audio.destroy(pair->audio.user);
    memset(pair, 0, sizeof(*pair));
}
