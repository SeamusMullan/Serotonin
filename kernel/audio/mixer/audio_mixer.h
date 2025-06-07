#ifndef AUDIO_MIXER_H
#define AUDIO_MIXER_H

#include <stddef.h>
#include "../stream/audio_stream.h"

// Mixer struct: mixes multiple streams into a single output
#define MAX_MIXER_STREAMS 8

typedef struct AudioMixer {
    AudioStream *streams[MAX_MIXER_STREAMS];
    size_t stream_count;
    float master_volume;
} AudioMixer;

// Mixer API
AudioMixer *audio_mixer_create(void);
void audio_mixer_free(AudioMixer *mixer);
int audio_mixer_add_stream(AudioMixer *mixer, AudioStream *stream);
int audio_mixer_remove_stream(AudioMixer *mixer, AudioStream *stream);
size_t audio_mixer_mix(AudioMixer *mixer, float *output, size_t frames);
void audio_mixer_set_master_volume(AudioMixer *mixer, float volume);

#endif // AUDIO_MIXER_H
