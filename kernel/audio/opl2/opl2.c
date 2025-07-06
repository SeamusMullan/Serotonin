/*
 * Serotonin OPL2 Sound Driver
*/

#include "opl2.h"
#include "../../io/io.h"
#include "../../stdlib/stdlib.h"
#include "../../stdio/stdio.h"
#include "../../kernel.h"
#include <stdint.h>

#define OPL2_REG  0x388
#define OPL2_DATA 0x389

static uint16_t opl2_base = 0x388;

static const opl2_channel_t _channel_map[9] = {
    { 0,  0,  3 },  /* ch0: mod=op0, car=op3 */
    { 1,  1,  4 },  /* ch1: mod=op1, car=op4 */
    { 2,  2,  5 },  /* ch2: mod=op3, car=op5 */
    { 3,  6,  9 },  /* ch3: mod=op6, car=op9 */
    { 4,  7, 10 },  /* ch4: mod=op7, car=op10 */
    { 5,  8, 11 },  /* ch5: mod=op8, car=op11 */
    { 6, 12, 15 },  /* ch6: mod=op12,car=op15 */
    { 7, 13, 16 },  /* ch7: mod=op13,car=op16 */
    { 8, 14, 17 },  /* ch8: mod=op14,car=op17 */
};

void opl2_init(uint16_t base_port)
{
    opl2_base = base_port;
}

int opl2_detect(void)
{
    const int max_tries = 10000;
    for (int i = 0; i < max_tries; i++) {
        if ((inb(opl2_base) & 0x80) == 0)
            return 1;
        io_wait();
    }
    return 0;
}

void opl2_write(uint8_t reg, uint8_t value)
{
    while (inb(opl2_base) & 0x80)
        io_wait();

    outb(opl2_base, reg);
    io_wait();

    while (inb(opl2_base) & 0x80)
        io_wait();

    outb(opl2_base + 1, value);
    io_wait();
}

uint8_t opl2_read_status(void)
{
    return inb(opl2_base);
}

int opl2_get_channel(uint8_t ch, opl2_channel_t* out_chan) {
    if (ch >= 9 || out_chan == NULL) return 0;
    *out_chan = _channel_map[ch];
    return 1;
}

/*
 * the OPL2 uses a 10-bit "F-number" and a 3-bit block (octave) to set freq:
 *    freq = Fnum * 49716 / 2^(20 - block)
 *
 * inverting that, for a desired freq_hz we choose the smallest block
 * such that Fnum <= 1023.
 */
static void _calc_fnum_block(float freq_hz,
                             uint16_t* out_fnum,
                             uint8_t *out_block)
{
    // clamp sensible range
    if (freq_hz < 20.0f)  freq_hz = 20.0f;
    if (freq_hz > 20000.0f) freq_hz = 20000.0f;

    uint8_t block;
    uint16_t fnum;

    for (block = 0; block < 8; block++) {
        float ratio = freq_hz * (1u << (20 - block)) / 49716.0f;
        fnum = (uint16_t)(ratio + 0.5f);
        if (fnum <= 0x3FF) {
            *out_fnum  = fnum;
            *out_block = block;
            return;
        }
    }
    // if all else fails, cap at highest block and fnum=1023
    *out_block = 7;
    *out_fnum  = 0x3FF;
}

void opl2_set_frequency(uint8_t ch, float freq_hz, int key_on) {
    opl2_channel_t chan;
    if (!opl2_get_channel(ch, &chan))
        return;

    uint16_t fnum;
    uint8_t  block;
    _calc_fnum_block(freq_hz, &fnum, &block);

    uint8_t fnum_lo = (uint8_t)(fnum & 0xFF);
    uint8_t fnum_hi = (uint8_t)((fnum >> 8) & 0x03);

    opl2_write(0xA0 + ch, fnum_lo);

    uint8_t b = (fnum_hi << 6) | ((key_on ? 1 : 0) << 5) | (block << 2);
    opl2_write(0xB0 + ch, b);
}

void opl2_play_note(uint8_t ch, uint8_t midi_note) {
    float freq = 440.0f * powf(2.0f, ((int)midi_note - 69) / 12.0f);
    opl2_set_frequency(ch, freq, 1);
}

void opl2_play_note_duration(uint8_t ch, uint8_t midi_note, uint32_t duration_ms) {
    // frequency for MIDI note (A4=69=440Hz)
    float freq = 440.0f * powf(2.0f, ((int)midi_note - 69) / 12.0f);

    // key o
    opl2_set_frequency(ch, freq, 1);

    // hold
    kernel_sleep(duration_ms);

    // key off
    opl2_set_frequency(ch, freq, 0);
}

void opl2_set_instrument(uint8_t ch, const opl2_instrument_t* instr) {
    opl2_channel_t c;
    if (!opl2_get_channel(ch, &c) || !instr) return;

    // output levels
    opl2_write(0x40 + c.mod_op, instr->mod_tl);
    opl2_write(0x40 + c.car_op, instr->car_tl);

    // multipliers
    opl2_write(0x20 + c.mod_op, instr->mod_mult & 0x0F);
    opl2_write(0x20 + c.car_op, instr->car_mult & 0x0F);

    // ADSR
    opl2_write(0x60 + c.mod_op, (instr->mod_ar << 4) | (instr->mod_dr & 0x0F));
    opl2_write(0x60 + c.car_op, (instr->car_ar << 4) | (instr->car_dr & 0x0F));
    opl2_write(0x80 + c.mod_op, (instr->mod_sl << 4) | (instr->mod_rr & 0x0F));
    opl2_write(0x80 + c.car_op, (instr->car_sl << 4) | (instr->car_rr & 0x0F));

    // feedback & connection
    uint8_t v = ((instr->feedback & 0x07) << 1) | (instr->connection ? 1 : 0);
    opl2_write(0xC0 + ch, v);
}
