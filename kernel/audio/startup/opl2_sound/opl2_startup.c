#include "../opl2/opl2.h"
#include "../../io/io.h"
#include <stdint.h>
#include <stddef.h>

/**
 * @brief
 *
 * This file has some bs audio things, including a shitty ahh startup sound.
 * - ts could be cooked
 * - ts could sound shit
 * - ts is gonna be absolutely fried brother
 */

void make_a_noise()
{

    opl2_init(0x388);

    opl2_instrument_t bell = {
        .mod_tl = 0x00,
        .car_tl = 0x00,
        .mod_mult = 10,
        .car_mult = 1,
        .mod_ar = 12,
        .mod_dr = 3,
        .mod_sl = 2,
        .mod_rr = 4,
        .car_ar = 12,
        .car_dr = 3,
        .car_sl = 2,
        .car_rr = 4,
        .feedback = 0,
        .connection = 1,
    };

    opl2_set_instrument(0, &bell);

    typedef struct
    {
        uint8_t note;
        uint32_t duration;
    } note_event_t;

    static note_event_t melody[] = {
        {NOTE_C0, 400},
        {NOTE_C1, 400},
        {NOTE_C2, 400},
        {NOTE_C3, 400},
        {NOTE_C4, 400},
        {NOTE_C5, 400},
        {NOTE_C6, 400},
        {NOTE_C7, 400},
        {NOTE_C8, 400},
    };

    /* play sequence */
    for (size_t i = 0; i < sizeof(melody) / sizeof(*melody); ++i)
    {
        uint8_t n = melody[i].note;
        if (n)
        {
            opl2_play_note_duration(0, n, melody[i].duration);
        }
        else
        {
            /* rest */
            kernel_sleep(melody[i].duration);
        }
        /* brief gap between notes */
        kernel_sleep(50);
    }
}
