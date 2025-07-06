#include "../../opl2/opl2.h"
#include "../../../io/io.h"
#include "../../../kernel.h"
#include <stdint.h>
#include <stddef.h>
#include "../../../stdlib/stdlib.h"
#include "opl2_startup.h"

/**
 * @brief
 *
 * This file has some bs audio things, including a startup sound
 * that is arguably "not that bad" now.
 */

void make_a_noise()
{
    opl2_init(0x388);

    opl2_instrument_t bell = {
        .mod_tl = 0x10, // slightly softened
        .car_tl = 0x00,
        .mod_mult = 6,
        .car_mult = 1,
        .mod_ar = 15,
        .mod_dr = 4,
        .mod_sl = 0,
        .mod_rr = 3,
        .car_ar = 15,
        .car_dr = 4,
        .car_sl = 0,
        .car_rr = 3,
        .feedback = 2,
        .connection = 1,
    };

    opl2_set_instrument(0, &bell);

    play_chord_progression();
}

void play_chord(uint8_t ch_base, uint8_t n1, uint8_t n2, uint8_t n3, uint32_t duration)
{
    opl2_play_note(ch_base + 0, n1);
    opl2_play_note(ch_base + 1, n2);
    opl2_play_note(ch_base + 2, n3);

    kernel_sleep(duration);

    opl2_set_frequency(ch_base + 0, 440.0f, 0);
    opl2_set_frequency(ch_base + 1, 440.0f, 0);
    opl2_set_frequency(ch_base + 2, 440.0f, 0);
}

void play_chord_progression()
{
    opl2_init(0x388);

    opl2_instrument_t saw_like = {
        .mod_tl = 0x3F, // Silent modulator
        .car_tl = 0x00, // Full-volume carrier
        .mod_mult = 1,  // Irrelevant (mod is silent)
        .car_mult = 15,  // High harmonic density
        .mod_ar = 4,
        .mod_dr = 8,
        .mod_sl = 0,
        .mod_rr = 3,
        .car_ar = 15, // Fast attack
        .car_dr = 6,  // Some decay
        .car_sl = 0,
        .car_rr = 4,     // Slight release
        .feedback = 7,   // Maximum feedback = more raspy/buzzy
        .connection = 1, // Additive (mod not used)
    };

    for (int ch = 0; ch < 3; ++ch)
    {
        opl2_set_instrument(ch, &saw_like);
    }

    play_chord(0, NOTE_C5, NOTE_E5, NOTE_G5, 600); // C major
    kernel_sleep(100);
    play_chord(0, NOTE_A4, NOTE_C5, NOTE_E5, 600); // A minor
    kernel_sleep(100);
    play_chord(0, NOTE_F4, NOTE_A4, NOTE_C5, 600); // F major
    kernel_sleep(100);
    play_chord(0, NOTE_G4, NOTE_B4, NOTE_D5, 600); // G major
}
