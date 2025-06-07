#include "audio_format.h"

size_t audio_format_frame_size(const AudioFormat *fmt) {
    if (!fmt) return 0;
    return (fmt->bits_per_sample / 8) * fmt->channels;
}
