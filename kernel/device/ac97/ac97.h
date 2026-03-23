#ifndef _DEVICE_AC97
#define _DEVICE_AC97

#include <stdint.h>
#include "../../schedule/schedule.h"

#define AC97_PCI_CLASS      0x04    /* Multimedia */
#define AC97_PCI_SUBCLASS   0x01    /* Audio device */
#define NAM_RESET           0x00    /* Reset / Capabilities (read) */
#define NAM_MASTER_VOL      0x02    /* Master Output Volume */
#define NAM_AUX_OUT_VOL     0x04    /* AUX Output Volume */
#define NAM_MIC_VOL         0x0E    /* Microphone Volume (gain) */
#define NAM_PCM_VOL         0x18    /* PCM Output Volume */
#define NAM_REC_SELECT      0x1A    /* Record Select */
#define NAM_REC_GAIN        0x1C    /* Record Gain */
#define NAM_MIC_GAIN        0x1E    /* Record Gain of Microphone */
#define NAM_EXT_CAPS        0x28    /* Extended Capabilities */
#define NAM_EXT_CTRL        0x2A    /* Control of Extended Capabilities */
#define NAM_FRONT_RATE      0x2C    /* Sample Rate of PCM Front DAC */
#define NAM_SURR_RATE       0x2E    /* Sample Rate of PCM Surr DAC */
#define NAM_LFE_RATE        0x30    /* Sample Rate of PCM LFE DAC */
#define NAM_LR_ADC_RATE     0x32    /* Sample Rate of PCM L/R ADC */
#define AC97_VOL_MUTE       (1 << 15)
#define AC97_EXT_VRA        (1 << 0)    /* Variable Rate Audio */
#define NABM_PCM_IN         0x00
#define NABM_PCM_OUT        0x10
#define NABM_MIC            0x20
#define NABM_BDBAR          0x00    /* Buffer Descriptor Base Address (dword) */
#define NABM_CIV            0x04    /* Current Index Value (byte) */
#define NABM_LVI            0x05    /* Last Valid Index (byte) */
#define NABM_SR             0x06    /* Status (word) */
#define NABM_PICB           0x08    /* Position in Current Buffer (word) */
#define NABM_PIV            0x0A    /* Prefetched Index Value (byte) */
#define NABM_CR             0x0B    /* Control (byte) */
#define NABM_GLOB_CNT       0x2C    /* Global Control (dword) */
#define NABM_GLOB_STA       0x30    /* Global Status (dword) */
#define AC97_SR_DCH          (1 << 0)    /* DMA controller halted */
#define AC97_SR_CELV         (1 << 1)    /* Current equals last valid */
#define AC97_SR_LVBCI        (1 << 2)    /* Last valid buffer completion interrupt */
#define AC97_SR_BCIS         (1 << 3)    /* Buffer completion interrupt status (IOC) */
#define AC97_SR_FIFOE        (1 << 4)    /* FIFO error */
#define AC97_CR_RPBM         (1 << 0)    /* Run/Pause bus master */
#define AC97_CR_RR           (1 << 1)    /* Reset registers */
#define AC97_CR_LVBIE        (1 << 2)    /* Last valid buffer interrupt enable */
#define AC97_CR_IOCE         (1 << 3)    /* IOC interrupt enable */
#define AC97_CR_FEIE         (1 << 4)    /* FIFO error interrupt enable */
#define AC97_GC_GIE          (1 << 0)    /* Global Interrupt Enable */
#define AC97_GC_COLD_RESET   (1 << 1)    /* Cold reset (resume from reset) */
#define AC97_GC_WARM_RESET   (1 << 2)    /* Warm reset */
#define AC97_GC_SHUT_DOWN    (1 << 3)    /* Shut down */
#define AC97_GC_2CH          (0 << 20)   /* 2 channels */
#define AC97_GC_4CH          (1 << 20)   /* 4 channels */
#define AC97_GC_6CH          (2 << 20)   /* 6 channels */
#define AC97_GC_16BIT        (0 << 22)   /* 16-bit samples */
#define AC97_GC_20BIT        (1 << 22)   /* 20-bit samples */
#define AC97_GS_PRIMARY_READY (1 << 8)   /* Primary codec ready */
#define AC97_GS_CHAN_MASK     (3 << 20)
#define AC97_GS_SAMPLE_MASK  (3 << 22)
#define AC97_GS_20BIT        (1 << 22)
#define AC97_BDL_ENTRIES     32
#define AC97_BDL_IOC         (1 << 15)   /* Interrupt on completion */
#define AC97_BDL_BUP         (1 << 14)   /* Buffer underrun policy: last entry */

typedef struct ac97_bdl_entry {
    uint32_t addr;          /* Physical address of sample data */
    uint16_t samples;       /* Number of samples in buffer */
    uint16_t flags;         /* BDL_IOC, BDL_BUP */
} __attribute__((packed)) ac97_bdl_entry_t;

#define AC97_MAX_SAMPLES     0xFFFE

#define AC97_SAMPLE_RATE_48K  48000
#define AC97_SAMPLE_RATE_44K  44100
#define AC97_BUF_PAGES       AC97_BDL_ENTRIES
#define AC97_BUF_SIZE        4096    /* bytes per buffer page */
#define AC97_SAMPLES_PER_BUF 2048    /* 4096 / 2 bytes per sample */

typedef struct ac97_config {
    uint16_t master_vol_left;   /* 0 = loudest, 63 = quietest */
    uint16_t master_vol_right;
    uint16_t pcm_vol_left;      /* 0 = loudest, 31 = quietest */
    uint16_t pcm_vol_right;
    uint16_t mic_gain;          /* 0-15, each step 1.5 dB */
    uint16_t mic_20db_boost;    /* 0 or 1 */
    uint16_t master_mute;       /* 0 or 1 */
    uint16_t pcm_mute;          /* 0 or 1 */
    uint16_t sample_rate;       /* e.g. 48000, 44100 */
    uint16_t reserved[7];
} __attribute__((packed)) ac97_config_t;

typedef struct ac97_dev {
    uint16_t nam_base;          /* BAR0: Native Audio Mixer I/O base */
    uint16_t nabm_base;         /* BAR1: Native Audio Bus Master I/O base */
    uint8_t  irq;

    ac97_bdl_entry_t *bdl;      /* Buffer Descriptor List (32 entries) */
    uint32_t bdl_phys;          /* Physical address of BDL */

    uint8_t *buffers[AC97_BUF_PAGES]; /* Virtual addresses of sample buffers */
    uint32_t buffers_phys[AC97_BUF_PAGES]; /* Physical addresses */

    volatile uint8_t write_idx; /* Next BDL slot to fill with data */

    int has_vra;                /* Variable Rate Audio support */
    uint16_t sample_rate;
} ac97_dev_t;

/* pending blocked write state */
static struct {
    process_control_block_t *task;
    uint8_t *kbuf;          /* heap copy of remaining data */
    uint32_t kbuf_size;     /* total size of kbuf */
    uint32_t kbuf_offset;   /* how far into kbuf we've submitted */
    uint32_t total;         /* total bytes requested by userspace (for eax) */
    int active;
} pending_write;

void ac97_init(void);
int  ac97_is_up(void);
int ac97_write_pcm(const void *data, uint32_t size);
void ac97_block_write(process_control_block_t *task, const void *kbuf, uint32_t total_size, uint32_t already_written);
int ac97_apply_config(const ac97_config_t *cfg);
void ac97_read_config(ac97_config_t *cfg);

#endif
