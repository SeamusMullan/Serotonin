// Advanced arpeggiator with different patterns
typedef enum
{
    ARP_UP,      // C-E-G-C
    ARP_DOWN,    // C-G-E-C
    ARP_UP_DOWN, // C-E-G-E-C
    ARP_RANDOM   // Random order
} arp_pattern_t;

void play_note(uint8_t channel, uint8_t note, uint32_t duration);
void play_note_on(uint8_t channel, uint8_t note);
void play_note_off(uint8_t channel);
void make_a_noise();
void play_chord(uint8_t ch_base, uint8_t n1, uint8_t n2, uint8_t n3, uint32_t duration);
void play_chord_progression();
void demo_arpeggio();
void demo_simple_melody();
void play_arpeggio_pattern(uint8_t channel, uint8_t *notes, uint8_t num_notes,
    uint32_t note_duration, uint8_t repeats, arp_pattern_t pattern);

void play_arpeggio(uint8_t channel, uint8_t *notes, uint8_t num_notes, uint32_t note_duration, uint8_t repeats);
