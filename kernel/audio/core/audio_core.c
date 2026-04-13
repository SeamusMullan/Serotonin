#include <kernel/audio/core/audio_core.h>

static audio_event_callback_t g_event_cb = 0;
static void *g_event_cb_data = 0;

void audio_core_init(void) {
    // Initialize audio engine, device registry, etc.
}

void audio_core_shutdown(void) {
    // Shutdown audio engine, free resources
}

void audio_core_set_event_callback(audio_event_callback_t cb, void *user_data) {
    g_event_cb = cb;
    g_event_cb_data = user_data;
}
