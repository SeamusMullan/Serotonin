#ifndef AUDIO_FORMAT_H
#define AUDIO_FORMAT_H

#include <stddef.h>
#include <stdint.h>

// Audio format descriptor (for extensibility)
typedef struct AudioFormat {
    unsigned sample_rate;
    unsigned channels;
    unsigned bits_per_sample;
} AudioFormat;

// Format utility API
size_t audio_format_frame_size(const AudioFormat *fmt);

#endif // AUDIO_FORMAT_H
