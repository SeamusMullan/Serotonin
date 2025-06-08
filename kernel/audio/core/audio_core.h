#ifndef AUDIO_CORE_H
#define AUDIO_CORE_H

#include "../device/audio_device.h"
#include "../stream/audio_stream.h"
#include "../mixer/audio_mixer.h"
#include "../format/audio_format.h"

// Audio engine core API
void audio_core_init(void);
void audio_core_shutdown(void);

// Callback/event system (stubs)
typedef void (*audio_event_callback_t)(int event, void *user_data);
void audio_core_set_event_callback(audio_event_callback_t cb, void *user_data);

#endif // AUDIO_CORE_H
