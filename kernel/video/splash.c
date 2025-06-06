#include "splash.h"
#include "vbe/vbe.h"
#include "font.h"

void splash_render(void) {
    
  uint32_t LEAN_PURPLE = 0x6345B5; // Lean purple color

  // fill with white
  vbe_fillrect(0, 0, vbe_info.width, vbe_info.height, LEAN_PURPLE);

  // draw a lean purple rectangle in the middle
  uint32_t rect_x = (vbe_info.width - 200) / 2;
  uint32_t rect_y = (vbe_info.height - 100) / 2;
  vbe_fillrect(rect_x, rect_y, 200, 100, 0xFFFFFF); // White

  // draw a white rectangle in the middle
  uint32_t inner_rect_x = rect_x + 10;
  uint32_t inner_rect_y = rect_y + 10;
  vbe_fillrect(inner_rect_x, inner_rect_y, 180, 80, LEAN_PURPLE); // Purple

  // draw the text "Seratonin OS" in the center
  vbe_puts("Seratonin OS", inner_rect_x + 10, inner_rect_y + 10, 0xFFFFFF);
  // draw the version number below the text
  char version_str[32];
  vbe_puts(version_str, inner_rect_x + 10, inner_rect_y + 30, 0xFFFFFF);

  
  terminal_dirty = 1;
}
