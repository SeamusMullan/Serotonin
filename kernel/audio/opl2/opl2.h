#ifndef _AUDIO_OPL2
#define _AUDIO_OPL2

#include <stdint.h>

/*
 * YM3812 (OPL2) kernel driver — Serotonin
 *
 * Reference: Yamaha YM3812 Application Manual (LSI-2438124, 1994)
 *            AdLib Programmer's Reference Manual
 *
 * The chip has 9 melodic channels.  Each channel is driven by two
 * operators: a modulator (op1) and a carrier (op2).  All per-operator
 * registers are addressed as  BASE + hardware_offset, where the offsets
 * follow a non-sequential layout that skips slots 0x06, 0x07, 0x0E, 0x0F:
 *
 *   Group │ mod offsets       │ car offsets       │ channels
 *   ──────┼───────────────────┼───────────────────┼──────────
 *     A   │ 0x00  0x01  0x02 │ 0x03  0x04  0x05 │ 0, 1, 2
 *     B   │ 0x08  0x09  0x0A │ 0x0B  0x0C  0x0D │ 3, 4, 5
 *     C   │ 0x10  0x11  0x12 │ 0x13  0x14  0x15 │ 6, 7, 8
 *
 * mod_op / car_op in opl2_channel_t hold these raw hardware offsets.
 */
typedef struct {
    uint8_t ch;      /* channel number (0–8) */
    uint8_t mod_op;  /* modulator hardware register offset (see table above) */
    uint8_t car_op;  /* carrier  hardware register offset  (see table above) */
} opl2_channel_t;

/*
 * opl2_instrument_t – complete two-operator voice definition.
 *
 * Fields map directly to YM3812 hardware register bits.
 * Prefix "mod_" = modulator (op1), "car_" = carrier (op2).
 *
 * ── Reg 0x20 + op : AM | VIB | EGT | KSR | MULT ───────────────────────────
 *   am   : 0/1  – tremolo (amplitude modulation) enable
 *   vib  : 0/1  – vibrato enable
 *   egt  : 0/1  – envelope generator type: 0=decay after sustain, 1=hold
 *   ksr  : 0/1  – key scale rate (envelope shortened at higher pitches)
 *   mult : 0–15 – frequency multiplier (0=×0.5, 1=×1, 2=×2 … 15=×15)
 *
 * ── Reg 0x40 + op : KSL | TL ───────────────────────────────────────────────
 *   ksl  : 0–3  – key scale level: output attenuation vs. pitch
 *                 0=off, 1=1.5 dB/oct, 2=3 dB/oct, 3=6 dB/oct
 *   tl   : 0–63 – total output level (0=full volume, 63=silent; 0.75 dB/step)
 *
 * ── Reg 0x60 + op : AR | DR ────────────────────────────────────────────────
 *   ar   : 0–15 – attack  rate (0=slowest, 15=fastest)
 *   dr   : 0–15 – decay   rate (0=slowest, 15=fastest)
 *
 * ── Reg 0x80 + op : SL | RR ────────────────────────────────────────────────
 *   sl   : 0–15 – sustain level (0=loudest, 15=silent)
 *   rr   : 0–15 – release rate  (0=slowest, 15=fastest)
 *
 * ── Reg 0xE0 + op : WS ─────────────────────────────────────────────────────
 *   ws   : 0–3  – waveform: 0=sine, 1=half-sine, 2=abs(sine), 3=pulse-sine
 *                 (requires reg 0x01 bit 5 to be set; opl2_init() does this)
 *
 * ── Reg 0xC0 + ch : feedback | connection ──────────────────────────────────
 *   feedback   : 0–7 – op1 self-feedback strength (0=none)
 *   connection : 0/1 – 0=FM (op1 modulates op2), 1=additive (both output)
 */
typedef struct {
    /* modulator (op1) */
    uint8_t mod_am,  mod_vib,  mod_egt, mod_ksr;  /* reg 0x20 bits [7:4] */
    uint8_t mod_mult;                              /* reg 0x20 bits [3:0] */
    uint8_t mod_ksl, mod_tl;                       /* reg 0x40            */
    uint8_t mod_ar,  mod_dr;                       /* reg 0x60            */
    uint8_t mod_sl,  mod_rr;                       /* reg 0x80            */
    uint8_t mod_ws;                                /* reg 0xE0            */

    /* carrier (op2) */
    uint8_t car_am,  car_vib,  car_egt, car_ksr;  /* reg 0x20 bits [7:4] */
    uint8_t car_mult;                              /* reg 0x20 bits [3:0] */
    uint8_t car_ksl, car_tl;                       /* reg 0x40            */
    uint8_t car_ar,  car_dr;                       /* reg 0x60            */
    uint8_t car_sl,  car_rr;                       /* reg 0x80            */
    uint8_t car_ws;                                /* reg 0xE0            */

    /* channel-wide */
    uint8_t feedback;    /* reg 0xC0 bits [3:1] */
    uint8_t connection;  /* reg 0xC0 bit  [0]   */
} opl2_instrument_t;

/* MIDI note numbers (standard: A4 = 69 = 440 Hz) */
enum {
    NOTE_C0  = 0,   NOTE_CS0, NOTE_D0,  NOTE_DS0, NOTE_E0,  NOTE_F0,
    NOTE_FS0, NOTE_G0,  NOTE_GS0, NOTE_A0,  NOTE_AS0, NOTE_B0,
    NOTE_C1  = 12,  NOTE_CS1, NOTE_D1,  NOTE_DS1, NOTE_E1,  NOTE_F1,
    NOTE_FS1, NOTE_G1,  NOTE_GS1, NOTE_A1,  NOTE_AS1, NOTE_B1,
    NOTE_C2  = 24,  NOTE_CS2, NOTE_D2,  NOTE_DS2, NOTE_E2,  NOTE_F2,
    NOTE_FS2, NOTE_G2,  NOTE_GS2, NOTE_A2,  NOTE_AS2, NOTE_B2,
    NOTE_C3  = 36,  NOTE_CS3, NOTE_D3,  NOTE_DS3, NOTE_E3,  NOTE_F3,
    NOTE_FS3, NOTE_G3,  NOTE_GS3, NOTE_A3,  NOTE_AS3, NOTE_B3,
    NOTE_C4  = 48,  NOTE_CS4, NOTE_D4,  NOTE_DS4, NOTE_E4,  NOTE_F4,
    NOTE_FS4, NOTE_G4,  NOTE_GS4, NOTE_A4,  NOTE_AS4, NOTE_B4,
    NOTE_C5  = 60,  NOTE_CS5, NOTE_D5,  NOTE_DS5, NOTE_E5,  NOTE_F5,
    NOTE_FS5, NOTE_G5,  NOTE_GS5, NOTE_A5,  NOTE_AS5, NOTE_B5,
    NOTE_C6  = 72,  NOTE_CS6, NOTE_D6,  NOTE_DS6, NOTE_E6,  NOTE_F6,
    NOTE_FS6, NOTE_G6,  NOTE_GS6, NOTE_A6,  NOTE_AS6, NOTE_B6,
    NOTE_C7  = 84,  NOTE_CS7, NOTE_D7,  NOTE_DS7, NOTE_E7,  NOTE_F7,
    NOTE_FS7, NOTE_G7,  NOTE_GS7, NOTE_A7,  NOTE_AS7, NOTE_B7,
    NOTE_C8  = 96,  NOTE_CS8, NOTE_D8,  NOTE_DS8, NOTE_E8,  NOTE_F8,
    NOTE_FS8, NOTE_G8,  NOTE_GS8, NOTE_A8,  NOTE_AS8, NOTE_B8,
    NOTE_C9  = 108, NOTE_CS9, NOTE_D9,  NOTE_DS9, NOTE_E9,  NOTE_F9,
    NOTE_FS9, NOTE_G9,  NOTE_GS9, NOTE_A9,  NOTE_AS9, NOTE_B9,
    NOTE_C10 = 120, NOTE_CS10, NOTE_D10, NOTE_DS10, NOTE_E10, NOTE_F10,
    NOTE_FS10, NOTE_G10, NOTE_GS10, NOTE_A10, NOTE_AS10, NOTE_B10
};

/*
 * opl2_init        – set base I/O port, reset timers, enable waveform select.
 *                    Call before any other function.
 * opl2_detect      – timer-based hardware presence check.
 *                    Returns 1 if an OPL2-compatible chip answers, 0 otherwise.
 *                    Leaves the chip in a clean state on exit.
 * opl2_write       – write one byte to a register (handles timing internally).
 * opl2_read_status – read the status register (bits: 7=IRQ, 6=T1, 5=T2).
 * opl2_get_channel – fill *out_chan with the operator offsets for channel ch.
 * opl2_set_frequency   – set channel frequency (Hz) and KEY-ON state.
 * opl2_play_note       – key-on from a MIDI note number (no duration).
 * opl2_play_note_duration – key-on, sleep duration_ms, key-off.
 * opl2_set_instrument  – program both operators and the channel register.
 */
void    opl2_init(uint16_t base_port);
int     opl2_detect(void);
void    opl2_write(uint8_t reg, uint8_t value);
uint8_t opl2_read_status(void);
int     opl2_get_channel(uint8_t ch, opl2_channel_t *out_chan);
void    opl2_set_frequency(uint8_t ch, float freq_hz, int key_on);
void    opl2_play_note(uint8_t ch, uint8_t midi_note);
void    opl2_play_note_duration(uint8_t ch, uint8_t midi_note, uint32_t duration_ms);
void    opl2_set_instrument(uint8_t ch, const opl2_instrument_t *instr);

#endif /* _AUDIO_OPL2 */
