#ifndef _AUDIO_OPL2
#define _AUDIO_OPL2

#include <stdint.h>

typedef struct {
    uint8_t ch;       // channel number (0–8)
    uint8_t mod_op;   // modulator operator index (0–17)
    uint8_t car_op;   // carrier operator index (0–17)
} opl2_channel_t;

typedef struct {  
    // output levels (0=full, 63=off)
    uint8_t mod_tl, car_tl;  

    // frequency multipliers (0,15 = *1, *15)
    uint8_t mod_mult, car_mult;  

    // envelopes: attack/decay (0–15), sustain/release (0–15)
    uint8_t mod_ar, mod_dr, mod_sl, mod_rr;  
    uint8_t car_ar, car_dr, car_sl, car_rr;  

    // feedback (0–7) and routing: 0=FM, 1=additive
    uint8_t feedback;  
    uint8_t connection;  
} opl2_instrument_t;


enum {
    NOTE_C0  = 0,   NOTE_CS0, NOTE_D0,  NOTE_DS0, NOTE_E0,  NOTE_F0,  NOTE_FS0, NOTE_G0,  NOTE_GS0, NOTE_A0,  NOTE_AS0, NOTE_B0,
    NOTE_C1  = 12,  NOTE_CS1, NOTE_D1,  NOTE_DS1, NOTE_E1,  NOTE_F1,  NOTE_FS1, NOTE_G1,  NOTE_GS1, NOTE_A1,  NOTE_AS1, NOTE_B1,
    NOTE_C2  = 24,  NOTE_CS2, NOTE_D2,  NOTE_DS2, NOTE_E2,  NOTE_F2,  NOTE_FS2, NOTE_G2,  NOTE_GS2, NOTE_A2,  NOTE_AS2, NOTE_B2,
    NOTE_C3  = 36,  NOTE_CS3, NOTE_D3,  NOTE_DS3, NOTE_E3,  NOTE_F3,  NOTE_FS3, NOTE_G3,  NOTE_GS3, NOTE_A3,  NOTE_AS3, NOTE_B3,
    NOTE_C4  = 48,  NOTE_CS4, NOTE_D4,  NOTE_DS4, NOTE_E4,  NOTE_F4,  NOTE_FS4, NOTE_G4,  NOTE_GS4, NOTE_A4,  NOTE_AS4, NOTE_B4,
    NOTE_C5  = 60,  NOTE_CS5, NOTE_D5,  NOTE_DS5, NOTE_E5,  NOTE_F5,  NOTE_FS5, NOTE_G5,  NOTE_GS5, NOTE_A5,  NOTE_AS5, NOTE_B5,
    NOTE_C6  = 72,  NOTE_CS6, NOTE_D6,  NOTE_DS6, NOTE_E6,  NOTE_F6,  NOTE_FS6, NOTE_G6,  NOTE_GS6, NOTE_A6,  NOTE_AS6, NOTE_B6,
    NOTE_C7  = 84,  NOTE_CS7, NOTE_D7,  NOTE_DS7, NOTE_E7,  NOTE_F7,  NOTE_FS7, NOTE_G7,  NOTE_GS7, NOTE_A7,  NOTE_AS7, NOTE_B7,
    NOTE_C8  = 96,  NOTE_CS8, NOTE_D8,  NOTE_DS8, NOTE_E8,  NOTE_F8,  NOTE_FS8, NOTE_G8,  NOTE_GS8, NOTE_A8,  NOTE_AS8, NOTE_B8,
    NOTE_C9  = 108, NOTE_CS9, NOTE_D9,  NOTE_DS9, NOTE_E9,  NOTE_F9,  NOTE_FS9, NOTE_G9,  NOTE_GS9, NOTE_A9,  NOTE_AS9, NOTE_B9,
    NOTE_C10 = 120, NOTE_CS10, NOTE_D10, NOTE_DS10, NOTE_E10, NOTE_F10, NOTE_FS10, NOTE_G10, NOTE_GS10, NOTE_A10, NOTE_AS10, NOTE_B10
};

void opl2_init(uint16_t base_port);
int opl2_detect(void);
void opl2_write(uint8_t reg, uint8_t value);
uint8_t opl2_read_status(void);
int opl2_get_channel(uint8_t ch, opl2_channel_t* out_chan);
void opl2_set_frequency(uint8_t ch, float freq_hz, int key_on);
void opl2_play_note(uint8_t ch, uint8_t midi_note);
void opl2_play_note_duration(uint8_t ch, uint8_t midi_note, uint32_t duration_ms);
void opl2_set_instrument(uint8_t ch, const opl2_instrument_t* instr);

#endif