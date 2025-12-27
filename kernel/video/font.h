#ifndef _VIDEO_FONT
#define _VIDEO_FONT

#include <stdint.h>
#include <stddef.h>

typedef struct {
    uint16_t codepoint;
    uint8_t data[16];
} FontGlyph;

extern FontGlyph ascii_font[];
extern const int ascii_font_glyph_count;
extern FontGlyph *find_glyph(uint16_t codepoint);

#endif