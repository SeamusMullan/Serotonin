#include <kernel/audio/pcspeaker/pcspeaker.h>
#include <kernel/io/io.h>
#include <stdint.h>

void play_pc_speaker_sound(uint32_t frequency) {
    uint32_t divisor = 1193180 / frequency;

    // set the PIT to mode 3 (square wave) on channel 2
    outb(0x43, 0xB6);
    outb(0x42, (uint8_t)(divisor & 0xFF));      // low byte
    outb(0x42, (uint8_t)((divisor >> 8) & 0xFF)); // high byte

    // enable the speaker (set bits 0 and 1)
    uint8_t tmp = inb(0x61);
    if ((tmp & 3) != 3) {
        outb(0x61, tmp | 3);
    }
}

void stop_pc_speaker_sound(void) {
    uint8_t tmp = inb(0x61) & 0xFC; // clear bits 0 and 1
    outb(0x61, tmp);
}