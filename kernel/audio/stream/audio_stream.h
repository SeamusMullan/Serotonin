#ifndef AUDIO_STREAM_H
#define AUDIO_STREAM_H

#include <stddef.h>
#include <stdint.h>
#include <kernel/audio/device/audio_device.h>
#include <kernel/audio/utils/audio_buffer.h>

// Audio stream state
typedef enum {
    AUDIO_STREAM_STOPPED,
    AUDIO_STREAM_RUNNING,
    AUDIO_STREAM_PAUSED
} AudioStreamState;

// Audio stream descriptor
typedef struct AudioStream {
    AudioDevice *device;
    AudioBuffer *buffer;
    AudioStreamState state;
    float volume;
    // Add more fields as needed (e.g., pan, callback, etc.)
} AudioStream;

// Stream control API
AudioStream *audio_stream_create(AudioDevice *device, size_t buffer_size);
void audio_stream_free(AudioStream *stream);
int audio_stream_start(AudioStream *stream);
int audio_stream_stop(AudioStream *stream);
int audio_stream_pause(AudioStream *stream);
int audio_stream_resume(AudioStream *stream);
int audio_stream_set_volume(AudioStream *stream, float volume);

#endif // AUDIO_STREAM_H
