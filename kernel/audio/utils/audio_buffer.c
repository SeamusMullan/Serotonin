#include <kernel/audio/utils/audio_buffer.h>
#include <stddef.h>
#include <stdint.h>
#include <kernel.h> // for kernel_malloc, kernel_free

/**
 * @brief Create a new audio buffer with the given capacity (number of float samples)
 * 
 * This function allocates memory for an AudioBuffer structure and its data array.
 * If the allocation fails, it returns NULL.
 * 
 * Normally the size is a power of two for performance reasons, but this is not enforced here.
 * 
 * @param capacity The maximum number of float samples the buffer can hold.
 * @return AudioBuffer* A pointer to the newly created AudioBuffer, or NULL on failure.
 */
AudioBuffer *audio_buffer_create(size_t capacity) {
    if (capacity == 0) return NULL;
    AudioBuffer *buffer = (AudioBuffer *)kernel_malloc(sizeof(AudioBuffer));
    if (!buffer) return NULL;
    buffer->data = (float *)kernel_malloc(sizeof(float) * capacity);
    if (!buffer->data) {
        kernel_free(buffer);
        return NULL;
    }
}

/**
 * @brief Free the audio buffer and its data
 * 
 * This function deallocates the memory used by the AudioBuffer structure and its data array.
 * If the buffer is NULL, it does nothing.
 * 
 * @param buffer The AudioBuffer to free.
 */
void audio_buffer_free(AudioBuffer *buffer) {
    if (!buffer) return;
    if (buffer->data) kernel_free(buffer->data);
    kernel_free(buffer);
}

/**
 * @brief Clear the buffer (set all samples to zero)
 * 
 * This function sets all samples in the buffer to zero and resets the size to 0.
 * If the buffer is NULL or has no data, it does nothing.
 * 
 * @param buffer The AudioBuffer to clear.
 */
void audio_buffer_clear(AudioBuffer *buffer) {
    if (!buffer || !buffer->data) return;
    for (size_t i = 0; i < buffer->capacity; ++i) {
        buffer->data[i] = 0.0f;
    }
    buffer->size = 0;
}

/**
 * @brief Write samples to the buffer (overwrites, does not resize)
 * 
 * This function writes samples from the input array to the buffer.
 * It overwrites existing samples and does not resize the buffer.
 * If the number of samples to write exceeds the buffer's capacity, it writes only up to the capacity.
 * 
 * @param buffer The AudioBuffer to write to.
 * @param input The input array containing samples to write.
 * @param num_samples The number of samples to write from the input array.
 * @return size_t The number of samples actually written to the buffer.
 */
size_t audio_buffer_write(AudioBuffer *buffer, const float *input, size_t num_samples) {
    if (!buffer || !buffer->data || !input) return 0;
    size_t to_write = num_samples;
    if (to_write > buffer->capacity) to_write = buffer->capacity;
    for (size_t i = 0; i < to_write; ++i) {
        buffer->data[i] = input[i];
    }
    buffer->size = to_write;
    return to_write;
}

/**
 * @brief Read samples from the buffer
 * 
 * This function reads samples from the buffer into the output array.
 * It reads up to the number of samples requested, but will not read more than the current size of the buffer.
 * 
 * @param buffer The AudioBuffer to read from.
 * @param output The output array where samples will be written.
 * @param num_samples The number of samples to read into the output array.
 * @return size_t The number of samples actually read from the buffer.
 */
size_t audio_buffer_read(AudioBuffer *buffer, float *output, size_t num_samples) {
    if (!buffer || !buffer->data || !output) return 0;
    size_t to_read = num_samples;
    if (to_read > buffer->size) to_read = buffer->size;
    for (size_t i = 0; i < to_read; ++i) {
        output[i] = buffer->data[i];
    }
    return to_read;
}
