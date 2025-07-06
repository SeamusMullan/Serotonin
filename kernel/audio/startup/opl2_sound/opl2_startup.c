#include "../../opl2/opl2.h"
#include "../../../io/io.h"
#include "../../../kernel.h"
#include <stdint.h>
#include <stddef.h>
#include "../../../stdlib/stdlib.h"
#include "opl2_startup.h"

/**
 * @brief Enhanced OPL2 audio with arpeggiator and dedicated note functions
 */

// Dedicated play note function with duration
void play_note(uint8_t channel, uint8_t note, uint32_t duration)
{
    opl2_play_note(channel, note);
    kernel_sleep(duration);
    opl2_set_frequency(channel, 440.0f, 0); // Stop the note
}

// Play a single note and hold it (doesn't auto-stop)
void play_note_on(uint8_t channel, uint8_t note)
{
    opl2_play_note(channel, note);
}

// Stop a note on a channel
void play_note_off(uint8_t channel)
{
    opl2_set_frequency(channel, 440.0f, 0);
}

// Simple arpeggiator - plays notes in sequence
void play_arpeggio(uint8_t channel, uint8_t *notes, uint8_t num_notes, uint32_t note_duration, uint8_t repeats)
{
    for (uint8_t rep = 0; rep < repeats; rep++)
    {
        for (uint8_t i = 0; i < num_notes; i++)
        {
            play_note(channel, notes[i], note_duration);
        }
    }
}

void play_arpeggio_pattern(uint8_t channel, uint8_t *notes, uint8_t num_notes,
                           uint32_t note_duration, uint8_t repeats, arp_pattern_t pattern)
{
    for (uint8_t rep = 0; rep < repeats; rep++)
    {
        switch (pattern)
        {
        case ARP_UP:
            for (uint8_t i = 0; i < num_notes; i++)
            {
                play_note(channel, notes[i], note_duration);
            }
            break;

        case ARP_DOWN:
            for (int8_t i = num_notes - 1; i >= 0; i--)
            {
                play_note(channel, notes[i], note_duration);
            }
            break;

        case ARP_UP_DOWN:
            // Up
            for (uint8_t i = 0; i < num_notes; i++)
            {
                play_note(channel, notes[i], note_duration);
            }
            // Down (skip the top note to avoid repetition)
            for (int8_t i = num_notes - 2; i >= 0; i--)
            {
                play_note(channel, notes[i], note_duration);
            }
            break;

        case ARP_RANDOM:
            for (uint8_t i = 0; i < num_notes; i++)
            {
                uint8_t random_idx = rand() % num_notes;
                play_note(channel, notes[random_idx], note_duration);
            }
            break;
        }
    }
}

// Your existing functions with some improvements
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
        .mod_tl = 0x02, // Silent modulator
        .car_tl = 0x00, // Full-volume carrier
        .mod_mult = 2,  // Irrelevant (mod is silent)
        .car_mult = 8,  // High harmonic density
        .mod_ar = 15,
        .mod_dr = 8,
        .mod_sl = 3,
        .mod_rr = 3,
        .car_ar = 15, // Fast attack
        .car_dr = 6,  // Some decay
        .car_sl = 3,
        .car_rr = 4,     // Slight release
        .feedback = 3,   // Maximum feedback = more raspy/buzzy
        .connection = 0, // Additive (mod not used)
    };

    for (int ch = 0; ch < 3; ++ch)
    {
        opl2_set_instrument(ch, &saw_like);
    }

    play_chord(0, NOTE_C3, NOTE_E3, NOTE_G3, 150); // C major
}

// Example usage functions
void demo_arpeggio()
{
    opl2_init(0x388);

    // Set up a nice arp instrument
    opl2_instrument_t arp_instrument = {
        .mod_tl = 0x20, // Moderate modulator
        .car_tl = 0x00, // Full carrier
        .mod_mult = 1,
        .car_mult = 2,
        .mod_ar = 5, // Fast attack
        .mod_dr = 8,
        .mod_sl = 2,
        .mod_rr = 4,
        .car_ar = 5, // Fast attack
        .car_dr = 6,
        .car_sl = 2,
        .car_rr = 3,
        .feedback = 4,
        .connection = 0,
    };

    opl2_set_instrument(0, &arp_instrument);

    // Define a C major arpeggio
    uint8_t c_major_notes[] = {
        NOTE_C5,
        NOTE_D5,
        NOTE_E5,
        NOTE_F5,
        NOTE_G5,
        NOTE_A5,
        NOTE_B5,
        NOTE_C6
    };

    // Play different arpeggio patterns
    play_arpeggio_pattern(0, c_major_notes, 8, 400, 1, ARP_UP);
    kernel_sleep(200);
    play_arpeggio_pattern(0, c_major_notes, 8, 400, 1, ARP_DOWN);
    kernel_sleep(200);
    play_arpeggio_pattern(0, c_major_notes, 8, 400, 1, ARP_UP_DOWN);
}

void demo_simple_melody()
{
    opl2_init(0x388);

    // Set up a melodic instrument
    opl2_instrument_t melody_instrument = {
        .mod_tl = 0x15,
        .car_tl = 0x00,
        .mod_mult = 1,
        .car_mult = 1,
        .mod_ar = 12,
        .mod_dr = 6,
        .mod_sl = 4,
        .mod_rr = 5,
        .car_ar = 12,
        .car_dr = 6,
        .car_sl = 4,
        .car_rr = 5,
        .feedback = 2,
        .connection = 1,
    };

    opl2_set_instrument(0, &melody_instrument);

    // Simple melody
    play_note(0, NOTE_C6, 200);
    play_note(0, NOTE_D6, 200);
    play_note(0, NOTE_E6, 200);
    play_note(0, NOTE_F6, 200);
    play_note(0, NOTE_G6, 400);
    play_note(0, NOTE_G6, 400);
}
