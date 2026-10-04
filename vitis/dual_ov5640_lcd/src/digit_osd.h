#ifndef DIGIT_OSD_H
#define DIGIT_OSD_H
#include <stdint.h>

/* Assigned by stage 6 system.bd: PS GP0 -> OSD AXI-Lite, 64 KiB window. */
#define DIGIT_OSD_BASE 0x43C20000U
#define DIGIT_OSD_TIMEOUT_FRAMES 120U

void digit_osd_init(void);
void digit_osd_set_enabled(uint32_t enabled);
void digit_osd_show(uint16_t x, uint16_t y, uint16_t width,
                    uint16_t height, uint32_t digit,
                    uint32_t confidence_permille, uint32_t source_sequence);
void digit_osd_no_digit(uint32_t source_sequence);
uint32_t digit_osd_status(void);
#endif
