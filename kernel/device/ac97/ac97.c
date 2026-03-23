#include "ac97.h"
#include "dev_ac97.h"
#include "../../io/io.h"
#include "../../io/pci/pci.h"
#include "../../vmm/paging_init.h"
#include "../../kernel.h"
#include "../../stdio/stdio.h"
#include "../../stdlib/stdlib.h"
#include "../../schedule/schedule.h"
#include "../../syscall/sys/errno.h"

#define AC97_LOG(fmt, ...) printfs(PRINT_STATUS_INFO, "ac97: " fmt, ##__VA_ARGS__)

static ac97_dev_t ac97;
static int ac97_initialised = 0;

// nam reg access
static inline uint16_t nam_r16(uint16_t reg) { return inw(ac97.nam_base + reg); }
static inline void     nam_w16(uint16_t reg, uint16_t v) { outw(ac97.nam_base + reg, v); }

// nabm reg access
static inline uint8_t  nabm_r8(uint16_t reg)  { return inb(ac97.nabm_base + reg); }
static inline uint16_t nabm_r16(uint16_t reg) { return inw(ac97.nabm_base + reg); }
static inline uint32_t nabm_r32(uint16_t reg) { return inl(ac97.nabm_base + reg); }
static inline void nabm_w8(uint16_t reg, uint8_t v)   { outb(ac97.nabm_base + reg, v); }
static inline void nabm_w16(uint16_t reg, uint16_t v) { outw(ac97.nabm_base + reg, v); }
static inline void nabm_w32(uint16_t reg, uint32_t v) { outl(ac97.nabm_base + reg, v); }

static void ac97_drain_pending(void);

int ac97_is_up(void) {
    return ac97_initialised;
}

static void ac97_irq_handler(int irq, processor_context_t *ctx) {
    (void)irq;
    (void)ctx;

    uint16_t status = nabm_r16(NABM_PCM_OUT + NABM_SR);

    // is this irq for ac97?
    if (!(status & (AC97_SR_LVBCI | AC97_SR_BCIS | AC97_SR_FIFOE)))
        return;

    // ack
    nabm_w16(NABM_PCM_OUT + NABM_SR, status & 0x1C);

    // continue any blocked write now that a slot freed up
    if (status & (AC97_SR_BCIS | AC97_SR_LVBCI))
        ac97_drain_pending();
}

static int ac97_free_slots(void) {
    uint8_t civ = nabm_r8(NABM_PCM_OUT + NABM_CIV);
    // dist from write_idx to civ
    int used = (int)((ac97.write_idx - civ) & (AC97_BDL_ENTRIES - 1));
    return (AC97_BDL_ENTRIES - 1) - used;
}

/* submit as many chunks as there are free DMA slots, non-blocking.
 * returns bytes actually queued (may be less than size). */
int ac97_write_pcm(const void *data, uint32_t size) {
    if (!ac97_initialised)
        return -1;

    const uint8_t *src = (const uint8_t *)data;
    uint32_t written = 0;

    while (written < size) {
        if (ac97_free_slots() <= 0)
            break;

        uint32_t chunk = size - written;
        if (chunk > AC97_BUF_SIZE)
            chunk = AC97_BUF_SIZE;

        uint8_t idx = ac97.write_idx;
        memcpy(ac97.buffers[idx], src + written, chunk);

        uint16_t samples = (uint16_t)(chunk / 2);
        if (samples == 0)
            break;

        ac97.bdl[idx].addr = ac97.buffers_phys[idx];
        ac97.bdl[idx].samples = samples;
        ac97.bdl[idx].flags = AC97_BDL_IOC;

        ac97.write_idx = (idx + 1) % AC97_BDL_ENTRIES;

        // last valid index
        nabm_w8(NABM_PCM_OUT + NABM_LVI, idx);

        // start/resume dma
        uint8_t cr = nabm_r8(NABM_PCM_OUT + NABM_CR);
        if (!(cr & AC97_CR_RPBM)) {
            nabm_w16(NABM_PCM_OUT + NABM_SR, 0x1C);
            nabm_w8(NABM_PCM_OUT + NABM_CR, AC97_CR_RPBM | AC97_CR_IOCE | AC97_CR_LVBIE);
        }

        written += chunk;
    }

    return (int)written;
}

static void ac97_drain_pending(void) {
    if (!pending_write.active)
        return;

    uint32_t left = pending_write.kbuf_size - pending_write.kbuf_offset;
    int n = ac97_write_pcm(pending_write.kbuf + pending_write.kbuf_offset, left);
    if (n > 0)
        pending_write.kbuf_offset += n;

    if (pending_write.kbuf_offset >= pending_write.kbuf_size) {
        pending_write.task->processor_context->eax = pending_write.total;
        kernel_free(pending_write.kbuf);
        task_unblock(pending_write.task);
        pending_write.active = 0;
    }
}

void ac97_block_write(process_control_block_t *task, const void *kbuf, uint32_t total_size, uint32_t already_written) {
    uint32_t remaining = total_size - already_written;

    uint8_t *copy = (uint8_t *)kernel_malloc(remaining);
    if (!copy) {
        task->processor_context->eax = already_written ? already_written : (uint32_t)-EIO;
        return;
    }
    memcpy(copy, (const uint8_t *)kbuf + already_written, remaining);

    pending_write.task = task;
    pending_write.kbuf = copy;
    pending_write.kbuf_size = remaining;
    pending_write.kbuf_offset = 0;
    pending_write.total = total_size;
    pending_write.active = 1;

    task_block();
    __builtin_unreachable();
}

int ac97_apply_config(const ac97_config_t *cfg) {
    if (!ac97_initialised)
        return -1;

    // master volume: bits [5:0] right, [13:8] left, bit 15 mute
    uint16_t master = (cfg->master_vol_left & 0x3F) << 8 |
                      (cfg->master_vol_right & 0x3F);
    if (cfg->master_mute)
        master |= AC97_VOL_MUTE;
    nam_w16(NAM_MASTER_VOL, master);

    // PCM volume: bits [4:0] right, [12:8] left, bit 15 mute
    uint16_t pcm = (cfg->pcm_vol_left & 0x1F) << 8 |
                   (cfg->pcm_vol_right & 0x1F);
    if (cfg->pcm_mute)
        pcm |= AC97_VOL_MUTE;
    nam_w16(NAM_PCM_VOL, pcm);

    // mic gain: bits [3:0] gain, bit 6 = +20dB boost, bit 15 mute
    uint16_t mic = cfg->mic_gain & 0x0F;
    if (cfg->mic_20db_boost)
        mic |= (1 << 6);
    nam_w16(NAM_MIC_VOL, mic);

    // sample rate (if VRA supported)
    if (ac97.has_vra && cfg->sample_rate > 0) {
        nam_w16(NAM_FRONT_RATE, cfg->sample_rate);
        ac97.sample_rate = nam_r16(NAM_FRONT_RATE);
    }

    return 0;
}

void ac97_read_config(ac97_config_t *cfg) {
    memset(cfg, 0, sizeof(*cfg));

    if (!ac97_initialised)
        return;

    uint16_t master = nam_r16(NAM_MASTER_VOL);
    cfg->master_vol_right = master & 0x3F;
    cfg->master_vol_left = (master >> 8) & 0x3F;
    cfg->master_mute = (master & AC97_VOL_MUTE) ? 1 : 0;

    uint16_t pcm = nam_r16(NAM_PCM_VOL);
    cfg->pcm_vol_right = pcm & 0x1F;
    cfg->pcm_vol_left = (pcm >> 8) & 0x1F;
    cfg->pcm_mute = (pcm & AC97_VOL_MUTE) ? 1 : 0;

    uint16_t mic = nam_r16(NAM_MIC_VOL);
    cfg->mic_gain = mic & 0x0F;
    cfg->mic_20db_boost = (mic >> 6) & 1;

    cfg->sample_rate = ac97.sample_rate;
}

static int ac97_probe(pci_function_t *fn) {
    // bars should be io
    if (!pci_bar_is_io(fn, 0) || !pci_bar_is_io(fn, 1)) {
        AC97_LOG("BARs not I/O space, unsupported\n");
        return -1;
    }

    memset(&ac97, 0, sizeof(ac97));
    ac97.nam_base = (uint16_t)pci_bar_address(fn, 0);
    ac97.nabm_base = (uint16_t)pci_bar_address(fn, 1);

    ac97.irq = fn->interrupt_line;

    AC97_LOG("found at NAM 0x%x, NABM 0x%x, IRQ %d\n",
             ac97.nam_base, ac97.nabm_base, ac97.irq);

    pci_func_enable_io_space(fn);
    pci_func_enable_bus_mastering(fn);

    // reset
    nabm_w32(NABM_GLOB_CNT, AC97_GC_COLD_RESET | AC97_GC_GIE);

    // wait for primary codec ready
    int codec_ready = 0;
    for (int i = 0; i < 1000000; i++) {
        uint32_t gs = nabm_r32(NABM_GLOB_STA);
        if (gs & AC97_GS_PRIMARY_READY) {
            codec_ready = 1;
            break;
        }
        io_wait();
    }
    if (!codec_ready) {
        AC97_LOG("codec not ready, aborting\n");
        return -1;
    }

    nam_w16(NAM_RESET, 0xFFFF);

    // wait for codec reset after nam reset
    for (int i = 0; i < 100000; i++) {
        io_wait();
    }

    // variable rate audio support
    uint16_t ext_caps = nam_r16(NAM_EXT_CAPS);
    ac97.has_vra = (ext_caps & AC97_EXT_VRA) ? 1 : 0;
    if (ac97.has_vra) {
        // enable vra
        uint16_t ext_ctrl = nam_r16(NAM_EXT_CTRL);
        nam_w16(NAM_EXT_CTRL, ext_ctrl | AC97_EXT_VRA);
        // set sample rate
        nam_w16(NAM_FRONT_RATE, AC97_SAMPLE_RATE_48K);
        ac97.sample_rate = nam_r16(NAM_FRONT_RATE);
        AC97_LOG("VRA enabled, sample rate %d Hz\n", ac97.sample_rate);
    } else {
        ac97.sample_rate = AC97_SAMPLE_RATE_48K;
        AC97_LOG("fixed 48000 Hz\n");
    }

    // unmute + max volume
    nam_w16(NAM_MASTER_VOL, 0x0000);
    nam_w16(NAM_PCM_VOL, 0x0000);

    uint16_t mv = nam_r16(NAM_MASTER_VOL);
    uint16_t pv = nam_r16(NAM_PCM_VOL);
    AC97_LOG("master vol: 0x%x, pcm vol: 0x%x\n", mv, pv);

    // alloc dma bufs
    ac97.bdl = (ac97_bdl_entry_t *)kernel_malloc_align(4096,
        AC97_BDL_ENTRIES * sizeof(ac97_bdl_entry_t));
    if (!ac97.bdl) {
        AC97_LOG("failed to allocate BDL\n");
        return -1;
    }
    ac97.bdl_phys = virt_to_phys(ac97.bdl);
    memset(ac97.bdl, 0, AC97_BDL_ENTRIES * sizeof(ac97_bdl_entry_t));

    for (int i = 0; i < AC97_BUF_PAGES; i++) {
        ac97.buffers[i] = (uint8_t *)kernel_malloc_align(4096, AC97_BUF_SIZE);
        if (!ac97.buffers[i]) {
            AC97_LOG("failed to allocate buffer %d\n", i);
            return -1;
        }
        ac97.buffers_phys[i] = virt_to_phys(ac97.buffers[i]);
        memset(ac97.buffers[i], 0, AC97_BUF_SIZE);

        ac97.bdl[i].addr = ac97.buffers_phys[i];
        ac97.bdl[i].samples = 0;
        ac97.bdl[i].flags = AC97_BDL_IOC;
    }

    ac97.write_idx = 0;

    // reset pcm out bus master
    nabm_w8(NABM_PCM_OUT + NABM_CR, AC97_CR_RR);
    for (int i = 0; i < 100000; i++) {
        if (!(nabm_r8(NABM_PCM_OUT + NABM_CR) & AC97_CR_RR))
            break;
    }

    // set bdl base
    nabm_w32(NABM_PCM_OUT + NABM_BDBAR, ac97.bdl_phys);

    // clear pending status
    nabm_w16(NABM_PCM_OUT + NABM_SR, 0x1C);

    irq_register(ac97.irq, ac97_irq_handler);

    ac97_initialised = 1;
    AC97_LOG("driver initialised\n");

    dev_ac97_init();
    return 0;
}

void ac97_init(void) {
    pci_function_t *fn = pci_find_class(AC97_PCI_CLASS, AC97_PCI_SUBCLASS);
    if (fn) {
        ac97_probe(fn);
    }
}
