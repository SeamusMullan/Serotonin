#include <kernel/audio/device/audio_device.h>
#include <stddef.h>

// Device registry (simple static array for now)
#define MAX_AUDIO_DEVICES 8
static AudioDevice *audio_devices[MAX_AUDIO_DEVICES];
static size_t audio_device_count = 0;

void audio_register_device(AudioDevice *dev) {
    if (audio_device_count < MAX_AUDIO_DEVICES) {
        audio_devices[audio_device_count++] = dev;
        if (dev->is_default) {
            // Optionally set as default
        }
    }
}

void audio_unregister_device(AudioDevice *dev) {
    for (size_t i = 0; i < audio_device_count; ++i) {
        if (audio_devices[i] == dev) {
            for (size_t j = i; j < audio_device_count - 1; ++j)
                audio_devices[j] = audio_devices[j + 1];
            --audio_device_count;
            break;
        }
    }
}

AudioDevice *audio_get_default_output(void) {
    for (size_t i = 0; i < audio_device_count; ++i) {
        if (audio_devices[i]->is_default)
            return audio_devices[i];
    }
    return audio_device_count > 0 ? audio_devices[0] : NULL;
}

AudioDevice *audio_get_default_input(void) {
    // For now, same as output; can be extended for input devices
    return audio_get_default_output();
}

AudioDevice *audio_find_device(const char *name) {
    for (size_t i = 0; i < audio_device_count; ++i) {
        if (audio_devices[i]->name && name && strcmp(audio_devices[i]->name, name) == 0)
            return audio_devices[i];
    }
    return NULL;
}
