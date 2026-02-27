/*
 * Serotonin OPL2 (YM3812) Sound Driver
 *
 * Reference: Yamaha YM3812 Application Manual (LSI-2438124, 1994)
 *            AdLib Programmer's Reference Manual
 *
 * ── I/O ports ───────────────────────────────────────────────────────────────
 *   base+0 (write) : register select
 *   base+0 (read)  : status register  [7]=IRQ  [6]=T1  [5]=T2
 *   base+1 (write) : data
 *
 * ── Write timing (mandatory per §3 of the application manual) ───────────────
 *   After register-select write : ≥12 master clocks ≈ 3.3 µs
 *   After data write            : ≥84 master clocks ≈ 23  µs
 *   (master clock = 3.58 MHz)
 *   Achieved here by calling io_wait() the required number of times.
 */

#include "opl2.h"
#include "../../io/io.h"
#include "../../stdlib/stdlib.h"
#include "../../stdio/stdio.h"
#include "../../kernel.h"
#include <stdint.h>

static uint16_t opl2_base = 0x388;  /* default AdLib base port */

/*
 * Channel-to-operator hardware register offset table.
 *
 * The YM3812 operator registers (bases 0x20, 0x40, 0x60, 0x80, 0xE0) are
 * indexed by adding these offsets.  The chip has three groups of six
 * operators; offsets 0x06, 0x07, 0x0E, 0x0F are absent from the hardware.
 *
 *   ch │ mod_op │ car_op
 *   ───┼────────┼────────
 *    0 │  0x00  │  0x03
 *    1 │  0x01  │  0x04
 *    2 │  0x02  │  0x05
 *    3 │  0x08  │  0x0B
 *    4 │  0x09  │  0x0C
 *    5 │  0x0A  │  0x0D
 *    6 │  0x10  │  0x13
 *    7 │  0x11  │  0x14
 *    8 │  0x12  │  0x15
 */
static const opl2_channel_t _channel_map[9] = {
    { 0, 0x00, 0x03 },   /* ch0: mod=0x00, car=0x03 */
    { 1, 0x01, 0x04 },   /* ch1: mod=0x01, car=0x04 */
    { 2, 0x02, 0x05 },   /* ch2: mod=0x02, car=0x05 */
    { 3, 0x08, 0x0B },   /* ch3: mod=0x08, car=0x0B */
    { 4, 0x09, 0x0C },   /* ch4: mod=0x09, car=0x0C */
    { 5, 0x0A, 0x0D },   /* ch5: mod=0x0A, car=0x0D */
    { 6, 0x10, 0x13 },   /* ch6: mod=0x10, car=0x13 */
    { 7, 0x11, 0x14 },   /* ch7: mod=0x11, car=0x14 */
    { 8, 0x12, 0x15 },   /* ch8: mod=0x12, car=0x15 */
};

/* ── timing helpers ─────────────────────────────────────────────────────── */

/* ≥12 master clock cycles (≈3.3 µs) required after a register-select write */
static void _opl2_delay_addr(void)
{
    for (int i = 0; i < 6; i++) io_wait();
}

/* ≥84 master clock cycles (≈23 µs) required after a data write */
static void _opl2_delay_data(void)
{
    for (int i = 0; i < 35; i++) io_wait();
}

/* ── public API ─────────────────────────────────────────────────────────── */

/*
 * opl2_write – write one byte to an OPL2 register.
 *
 * Sends the register address to base+0, waits ≥3.3 µs, sends the data
 * byte to base+1, then waits ≥23 µs.  Both waits are mandatory; skipping
 * them corrupts the chip's internal state.
 */
void opl2_write(uint8_t reg, uint8_t value)
{
    outb(opl2_base,     reg);    /* select register   */
    _opl2_delay_addr();          /* wait ≥3.3 µs      */
    outb(opl2_base + 1, value);  /* write data byte   */
    _opl2_delay_data();          /* wait ≥23 µs       */
}

/*
 * opl2_init – configure base port and put the chip in a known state.
 *
 * Must be called before any other opl2_* function.
 *   reg 0x04 = 0x60 : reset both timer flags
 *   reg 0x04 = 0x80 : unmask both timers (required as a separate write)
 *   reg 0x01 = 0x20 : enable waveform-select register (0xE0+op)
 *                     without this bit, all operators produce only sine waves
 */
void opl2_init(uint16_t base_port)
{
    opl2_base = base_port;
    opl2_write(0x04, 0x60);
    opl2_write(0x04, 0x80);
    opl2_write(0x01, 0x20);
}

/*
 * opl2_detect – verify that a real YM3812 is present at the configured port.
 *
 * Uses the Timer 1 mechanism from the AdLib programmer's reference:
 *
 *  1. Reset both timer flags  (reg 0x04 = 0x60, then 0x80).
 *  2. Read status; bits [7:5] must be 0b000 (no pending interrupts).
 *  3. Load Timer 1 to 0xFF and start it  (fires in ≈80 µs).
 *  4. Wait ≥80 µs.
 *  5. Read status; bits [7:5] must be 0b110 (IRQ + Timer1 fired).
 *  6. Reset timers again to leave chip clean.
 *
 * Returns 1 if an OPL2-compatible chip is detected, 0 otherwise.
 *
 * Note: opl2_base must already be set (call opl2_init first, or rely on
 * the default 0x388).
 */
int opl2_detect(void)
{
    /* Step 1: reset both timer flags */
    opl2_write(0x04, 0x60);
    opl2_write(0x04, 0x80);

    /* Step 2: sample status – no timers should be set */
    uint8_t s1 = inb(opl2_base);

    /* Step 3: load Timer 1 count and start it */
    opl2_write(0x02, 0xFF);  /* Timer 1 preset = 0xFF → fires fastest (≈80 µs) */
    opl2_write(0x04, 0x21);  /* start Timer 1 (bit 0), mask Timer 2 (bit 5)    */

    /* Step 4: wait ≥80 µs for Timer 1 to expire */
    for (int i = 0; i < 100; i++) io_wait();

    /* Step 5: sample status – IRQ (bit 7) and Timer1 (bit 6) must be set */
    uint8_t s2 = inb(opl2_base);

    /* Step 6: reset timers back to clean state */
    opl2_write(0x04, 0x60);
    opl2_write(0x04, 0x80);

    /* Mask lower 5 bits (undefined); check only bits [7:5] */
    return ((s1 & 0xE0) == 0x00) && ((s2 & 0xE0) == 0xC0);
}

uint8_t opl2_read_status(void)
{
    return inb(opl2_base);
}

int opl2_get_channel(uint8_t ch, opl2_channel_t *out_chan)
{
    if (ch >= 9 || out_chan == NULL) return 0;
    *out_chan = _channel_map[ch];
    return 1;
}

/*
 * _calc_fnum_block – convert a frequency in Hz to a YM3812 F-number / block.
 *
 * The chip encodes frequency as:
 *   freq = Fnum × 49716 / 2^(20 – block)
 *
 * Rearranging for Fnum:
 *   Fnum = freq × 2^(20 – block) / 49716
 *
 * Block values 0–7 select the octave range.  We scan from block 0 upward
 * and take the first block where Fnum fits in 10 bits (≤1023), which
 * maximises Fnum and therefore pitch resolution.
 *
 * Frequency is clamped to [20 Hz, 6208 Hz].  6208 Hz is the highest
 * frequency the chip can represent (block=7, Fnum=1023).
 */
static void _calc_fnum_block(float freq_hz, uint16_t *out_fnum, uint8_t *out_block)
{
    if (freq_hz <   20.0f) freq_hz =   20.0f;
    if (freq_hz > 6208.0f) freq_hz = 6208.0f;

    for (uint8_t block = 0; block < 8; block++) {
        uint16_t fnum = (uint16_t)(freq_hz * (float)(1u << (20u - block)) / 49716.0f + 0.5f);
        if (fnum <= 0x3FFu) {
            *out_fnum  = fnum;
            *out_block = block;
            return;
        }
    }
    /* Unreachable after the 6208 Hz clamp, but safe fallback */
    *out_fnum  = 0x3FFu;
    *out_block = 7;
}

/*
 * opl2_set_frequency – program a channel's frequency and KEY-ON state.
 *
 * Register layout:
 *   0xA0 + ch : F-number bits [7:0]
 *   0xB0 + ch : [7:6]=0  [5]=KEY-ON  [4:2]=BLOCK[2:0]  [1:0]=F-number[9:8]
 */
void opl2_set_frequency(uint8_t ch, float freq_hz, int key_on)
{
    opl2_channel_t chan;
    if (!opl2_get_channel(ch, &chan)) return;

    uint16_t fnum;
    uint8_t  block;
    _calc_fnum_block(freq_hz, &fnum, &block);

    opl2_write(0xA0 + ch, (uint8_t)(fnum & 0xFFu));

    uint8_t b = ((key_on ? 1u : 0u) << 5)   /* bit  5  : KEY-ON        */
              | ((block  & 0x07u)    << 2)   /* bits 4:2: BLOCK[2:0]    */
              | ((fnum   >> 8)       & 0x03u);  /* bits 1:0: F-number[9:8] */
    opl2_write(0xB0 + ch, b);
}

/* opl2_play_note – key-on a MIDI note (A4=69=440 Hz); does not key-off. */
void opl2_play_note(uint8_t ch, uint8_t midi_note)
{
    float freq = 440.0f * powf(2.0f, ((int)midi_note - 69) / 12.0f);
    opl2_set_frequency(ch, freq, 1);
}

/* opl2_play_note_duration – key-on, sleep duration_ms milliseconds, key-off. */
void opl2_play_note_duration(uint8_t ch, uint8_t midi_note, uint32_t duration_ms)
{
    float freq = 440.0f * powf(2.0f, ((int)midi_note - 69) / 12.0f);
    opl2_set_frequency(ch, freq, 1);   /* key on  */
    kernel_sleep(duration_ms);
    opl2_set_frequency(ch, freq, 0);   /* key off */
}

/*
 * opl2_set_instrument – program both operators and the channel register.
 *
 * Register map (BASE + hardware_offset, see _channel_map):
 *
 *   0x20 + op : [7]=AM  [6]=VIB  [5]=EGT  [4]=KSR  [3:0]=MULT
 *   0x40 + op : [7:6]=KSL  [5:0]=TL
 *   0x60 + op : [7:4]=AR   [3:0]=DR
 *   0x80 + op : [7:4]=SL   [3:0]=RR
 *   0xE0 + op : [1:0]=WS   (waveform; requires reg 0x01 bit 5 set by init)
 *   0xC0 + ch : [3:1]=FB   [0]=CNX
 */
void opl2_set_instrument(uint8_t ch, const opl2_instrument_t *instr)
{
    opl2_channel_t c;
    if (!opl2_get_channel(ch, &c) || !instr) return;

    /* Reg 0x20: AM | VIB | EGT | KSR | MULT[3:0] */
    opl2_write(0x20 + c.mod_op,
               ((instr->mod_am  & 0x01u) << 7) |
               ((instr->mod_vib & 0x01u) << 6) |
               ((instr->mod_egt & 0x01u) << 5) |
               ((instr->mod_ksr & 0x01u) << 4) |
               ( instr->mod_mult & 0x0Fu));
    opl2_write(0x20 + c.car_op,
               ((instr->car_am  & 0x01u) << 7) |
               ((instr->car_vib & 0x01u) << 6) |
               ((instr->car_egt & 0x01u) << 5) |
               ((instr->car_ksr & 0x01u) << 4) |
               ( instr->car_mult & 0x0Fu));

    /* Reg 0x40: KSL[7:6] | TL[5:0] */
    opl2_write(0x40 + c.mod_op, ((instr->mod_ksl & 0x03u) << 6) | (instr->mod_tl & 0x3Fu));
    opl2_write(0x40 + c.car_op, ((instr->car_ksl & 0x03u) << 6) | (instr->car_tl & 0x3Fu));

    /* Reg 0x60: AR[7:4] | DR[3:0] */
    opl2_write(0x60 + c.mod_op, ((instr->mod_ar & 0x0Fu) << 4) | (instr->mod_dr & 0x0Fu));
    opl2_write(0x60 + c.car_op, ((instr->car_ar & 0x0Fu) << 4) | (instr->car_dr & 0x0Fu));

    /* Reg 0x80: SL[7:4] | RR[3:0] */
    opl2_write(0x80 + c.mod_op, ((instr->mod_sl & 0x0Fu) << 4) | (instr->mod_rr & 0x0Fu));
    opl2_write(0x80 + c.car_op, ((instr->car_sl & 0x0Fu) << 4) | (instr->car_rr & 0x0Fu));

    /* Reg 0xE0: WS[1:0] – waveform select */
    opl2_write(0xE0 + c.mod_op, instr->mod_ws & 0x03u);
    opl2_write(0xE0 + c.car_op, instr->car_ws & 0x03u);

    /* Reg 0xC0: FB[3:1] | CNX[0] */
    opl2_write(0xC0 + ch, ((instr->feedback & 0x07u) << 1) | (instr->connection & 0x01u));
}
