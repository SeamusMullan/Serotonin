#ifndef AUDIO_DEVICE_H
#define AUDIO_DEVICE_H

#include <stddef.h>
#include <stdint.h>

// Audio format descriptor
typedef struct AudioFormat {
    unsigned sample_rate;
    unsigned channels;
    unsigned bits_per_sample;
} AudioFormat;

struct AudioDevice;
struct AudioStream;

// Device operation interface (to be implemented by drivers)
typedef struct AudioDeviceOps {
    int (*init)(struct AudioDevice *dev, AudioFormat *fmt);
    int (*start)(struct AudioDevice *dev);
    int (*stop)(struct AudioDevice *dev);
    int (*write)(struct AudioDevice *dev, const float *data, size_t frames);
    int (*read)(struct AudioDevice *dev, float *data, size_t frames);
    int (*set_format)(struct AudioDevice *dev, const AudioFormat *fmt);
    int (*set_volume)(struct AudioDevice *dev, float volume);
    // Add more as needed
} AudioDeviceOps;

// Audio device descriptor
typedef struct AudioDevice {
    const char *name;
    AudioDeviceOps *ops;
    void *driver_data; // For driver-specific state
    AudioFormat format;
    int is_default;
} AudioDevice;

// Device registration API
void audio_register_device(AudioDevice *dev);
void audio_unregister_device(AudioDevice *dev);
AudioDevice *audio_get_default_output(void);
AudioDevice *audio_get_default_input(void);
AudioDevice *audio_find_device(const char *name);

#endif // AUDIO_DEVICE_H
