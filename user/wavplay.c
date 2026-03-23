#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <string.h>
#include <fcntl.h>
#include <unistd.h>

#define BAR_WIDTH 30

/* Must match kernel's ac97_config_t exactly */
typedef struct ac97_config {
    uint16_t master_vol_left;
    uint16_t master_vol_right;
    uint16_t pcm_vol_left;
    uint16_t pcm_vol_right;
    uint16_t mic_gain;
    uint16_t mic_20db_boost;
    uint16_t master_mute;
    uint16_t pcm_mute;
    uint16_t sample_rate;
    uint16_t reserved[7];
} __attribute__((packed)) ac97_config_t;

/* Standard WAV file header (44 bytes for PCM) */
typedef struct wav_header {
    char     riff_id[4];        /* "RIFF" */
    uint32_t riff_size;
    char     wave_id[4];        /* "WAVE" */
} __attribute__((packed)) wav_header_t;

typedef struct wav_chunk {
    char     id[4];
    uint32_t size;
} __attribute__((packed)) wav_chunk_t;

typedef struct wav_fmt {
    uint16_t audio_format;      /* 1 = PCM */
    uint16_t num_channels;
    uint32_t sample_rate;
    uint32_t byte_rate;
    uint16_t block_align;
    uint16_t bits_per_sample;
} __attribute__((packed)) wav_fmt_t;

static void usage(const char *prog) {
    printf("usage: %s <file.wav>\n", prog);
}

static void draw_progress(uint32_t played, uint32_t total, uint32_t sample_rate,
                          uint16_t num_channels, uint16_t bits_per_sample) {
    /* avoid 64-bit division (no libgcc on i686) by reducing first */
    uint32_t scale = total / 1000;
    if (scale == 0) scale = 1;
    uint32_t played_k = played / scale;
    uint32_t total_k  = total  / scale;
    if (total_k == 0) total_k = 1;
    uint32_t pct = (played_k * 100) / total_k;
    int filled = (int)((played_k * BAR_WIDTH) / total_k);

    uint32_t bytes_per_sec = sample_rate * num_channels * (bits_per_sample / 8);
    uint32_t cur_sec = played / bytes_per_sec;
    uint32_t tot_sec = total / bytes_per_sec;

    printf("\r  [");
    for (int i = 0; i < BAR_WIDTH; i++)
        printf("%c", i < filled ? '=' : '.');
    printf("] %lu%% %lu:%02lu/%lu:%02lu",
           (unsigned long)pct,
           (unsigned long)(cur_sec / 60), (unsigned long)(cur_sec % 60),
           (unsigned long)(tot_sec / 60), (unsigned long)(tot_sec % 60));
    fflush(stdout);
}

int main(int argc, char **argv) {
    if (argc < 2) {
        usage(argv[0]);
        return 1;
    }

    const char *path = argv[1];
    int wav_fd = open(path, O_RDONLY);
    if (wav_fd < 0) {
        printf("wavplay: cannot open %s\n", path);
        return 1;
    }

    /* Read RIFF header */
    wav_header_t hdr;
    if (read(wav_fd, &hdr, sizeof(hdr)) != sizeof(hdr)) {
        printf("wavplay: failed to read RIFF header\n");
        close(wav_fd);
        return 1;
    }

    if (memcmp(hdr.riff_id, "RIFF", 4) != 0 || memcmp(hdr.wave_id, "WAVE", 4) != 0) {
        printf("wavplay: not a WAV file\n");
        close(wav_fd);
        return 1;
    }

    /* Scan chunks for fmt and data */
    wav_fmt_t fmt;
    int got_fmt = 0;
    uint32_t data_size = 0;
    int got_data = 0;

    while (!got_data) {
        wav_chunk_t chunk;
        int n = read(wav_fd, &chunk, sizeof(chunk));
        if (n != sizeof(chunk))
            break;

        if (memcmp(chunk.id, "fmt ", 4) == 0) {
            uint32_t to_read = chunk.size < sizeof(fmt) ? chunk.size : sizeof(fmt);
            if (read(wav_fd, &fmt, to_read) != (int)to_read) {
                printf("wavplay: failed to read fmt chunk\n");
                close(wav_fd);
                return 1;
            }
            got_fmt = 1;
            /* Skip any extra fmt bytes */
            if (chunk.size > sizeof(fmt))
                lseek(wav_fd, chunk.size - sizeof(fmt), SEEK_CUR);
        } else if (memcmp(chunk.id, "data", 4) == 0) {
            data_size = chunk.size;
            got_data = 1;
            /* File position is now at the start of PCM data */
        } else {
            /* Skip unknown chunk */
            lseek(wav_fd, chunk.size, SEEK_CUR);
        }
    }

    if (!got_fmt) {
        printf("wavplay: no fmt chunk found\n");
        close(wav_fd);
        return 1;
    }

    if (!got_data || data_size == 0) {
        printf("wavplay: no data chunk found\n");
        close(wav_fd);
        return 1;
    }

    if (fmt.audio_format != 1) {
        printf("wavplay: unsupported format %d (only PCM supported)\n", fmt.audio_format);
        close(wav_fd);
        return 1;
    }

    if (fmt.bits_per_sample != 16) {
        printf("wavplay: unsupported %d-bit (only 16-bit supported)\n", fmt.bits_per_sample);
        close(wav_fd);
        return 1;
    }

    printf("wavplay: %s\n", path);
    printf("  %lu Hz, %d ch, %d-bit PCM\n",
           (unsigned long)fmt.sample_rate, fmt.num_channels, fmt.bits_per_sample);
    printf("  %lu bytes of audio data\n", (unsigned long)data_size);

    /* Configure AC97: set sample rate and unmute */
    int cfg_fd = open("/dev/ac97/config", O_RDWR);
    if (cfg_fd < 0) {
        printf("wavplay: cannot open /dev/ac97/config\n");
        close(wav_fd);
        return 1;
    }

    ac97_config_t cfg;
    memset(&cfg, 0, sizeof(cfg));
    cfg.master_vol_left = 0;    /* 0 = loudest */
    cfg.master_vol_right = 0;
    cfg.pcm_vol_left = 0;
    cfg.pcm_vol_right = 0;
    cfg.master_mute = 0;
    cfg.pcm_mute = 0;
    cfg.sample_rate = (uint16_t)fmt.sample_rate;

    if (write(cfg_fd, &cfg, sizeof(cfg)) < 0) {
        printf("wavplay: failed to configure AC97\n");
        close(cfg_fd);
        close(wav_fd);
        return 1;
    }
    close(cfg_fd);

    /* Double-buffered streaming: read 512K from disk while the previous
     * 512K chunk is being played back to the DMA ring.  This avoids both
     * loading the entire file into memory AND the stutter from tiny reads. */
#define IOBUF_SIZE (1024 * 1024)

    int audio_fd = open("/dev/ac97/audio", O_WRONLY);
    if (audio_fd < 0) {
        printf("wavplay: cannot open /dev/ac97/audio\n");
        close(wav_fd);
        return 1;
    }

    uint32_t alloc_size = IOBUF_SIZE;
    if (fmt.num_channels == 1)
        alloc_size *= 2;  /* mono->stereo doubles the output size */

    uint8_t *iobuf = (uint8_t *)malloc(alloc_size);
    if (!iobuf) {
        printf("wavplay: out of memory\n");
        close(audio_fd);
        close(wav_fd);
        return 1;
    }

    uint32_t remaining = data_size;
    uint32_t played = 0;

    draw_progress(0, data_size, fmt.sample_rate, fmt.num_channels, fmt.bits_per_sample);

    while (remaining > 0) {
        /* Read a large chunk from disk */
        uint32_t to_read = remaining;
        if (to_read > IOBUF_SIZE)
            to_read = IOBUF_SIZE;

        uint32_t loaded = 0;
        while (loaded < to_read) {
            int n = read(wav_fd, iobuf + loaded, to_read - loaded);
            if (n <= 0)
                goto done;
            loaded += n;
        }
        remaining -= loaded;
        played += loaded;

        /* Convert / prepare the output data */
        uint8_t *out_ptr = iobuf;
        uint32_t out_size = loaded;

        if (fmt.num_channels == 1) {
            /* Expand mono to stereo in-place from the end to avoid overlap,
             * using the extra half of the buffer */
            int16_t *mono = (int16_t *)iobuf;
            int16_t *stereo = (int16_t *)(iobuf + IOBUF_SIZE);
            int samples = loaded / 2;
            for (int i = 0; i < samples; i++) {
                stereo[i * 2] = mono[i];
                stereo[i * 2 + 1] = mono[i];
            }
            out_ptr = (uint8_t *)stereo;
            out_size = samples * 4;
        }

        /* Stream to the audio device */
        uint32_t offset = 0;
        while (offset < out_size) {
            uint32_t chunk = out_size - offset;
            if (chunk > 65536)
                chunk = 65536;

            int n = write(audio_fd, out_ptr + offset, chunk);
            if (n <= 0)
                goto done;
            offset += n;
        }

        draw_progress(played, data_size, fmt.sample_rate, fmt.num_channels, fmt.bits_per_sample);
    }

done:
    draw_progress(data_size, data_size, fmt.sample_rate, fmt.num_channels, fmt.bits_per_sample);
    printf("\nwavplay: done\n");

    close(audio_fd);
    close(wav_fd);
    free(iobuf);
    return 0;
}
