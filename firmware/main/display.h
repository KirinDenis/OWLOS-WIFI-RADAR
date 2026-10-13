/* display - one face for every screen. main.c draws a frame strip by strip and hands
 * each strip over, the driver sends it (LCD) or collects it for the next refresh (e-paper).
 * only one driver file is compiled in, see main/CMakeLists.txt */
#pragma once
#include <stdint.h>
#include "gfx.h"
#include "board.h"

void     disp_init(void);
void     disp_backlight(int percent);     // 0..100, does nothing where there is no backlight
// a strip of the frame, top to bottom. last strip of a frame may be shorter
void     disp_strip(const canvas_t *cv);
// frame done. e-paper refreshes here (seconds), LCDs have nothing to do
void     disp_frame_end(void);
// how often a new frame is worth drawing, ms. 0 = as fast as it goes
uint32_t disp_frame_ms(void);
// e-paper: make the next refresh a full one (new screen). LCDs ignore it
void     disp_full_next(void);
// strips that never got the done callback (LCD), refreshes that timed out (e-paper)
uint32_t disp_lost(void);
