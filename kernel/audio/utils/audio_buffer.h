#ifndef AUDIO_BUFFER_H
#define AUDIO_BUFFER_H

#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct {
    float *data;
    size_t size;      // Number of samples in the buffer
    size_t capacity;  // Maximum number of samples
} AudioBuffer;

// Create a new audio buffer with the given capacity (number of float samples)
AudioBuffer *audio_buffer_create(size_t capacity);

// Free the audio buffer and its data
void audio_buffer_free(AudioBuffer *buffer);

// Clear the buffer (set all samples to zero)
void audio_buffer_clear(AudioBuffer *buffer);

// Write samples to the buffer (overwrites, does not resize)
// Returns the number of samples actually written
size_t audio_buffer_write(AudioBuffer *buffer, const float *input, size_t num_samples);

// Read samples from the buffer
// Returns the number of samples actually read
size_t audio_buffer_read(AudioBuffer *buffer, float *output, size_t num_samples);

#endif // AUDIO_BUFFER_H
