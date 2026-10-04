#include "digit_osd.h"
#include "xil_io.h"

#define REG_X       0x00U
#define REG_Y       0x04U
#define REG_W       0x08U
#define REG_H       0x0cU
#define REG_CLASS   0x10U
#define REG_SCORE   0x14U
#define REG_FLAGS   0x18U
#define REG_SEQ     0x1cU
#define REG_TIMEOUT 0x20U
#define REG_COMMIT  0x24U
#define REG_STATUS  0x28U

static uint32_t osd_enabled = 1U;

static void put(uint32_t offset, uint32_t value)
{
    Xil_Out32(DIGIT_OSD_BASE + offset, value);
}

static void publish(uint32_t valid, uint32_t sequence)
{
    put(REG_FLAGS, (valid ? 1U : 0U) | (osd_enabled ? 2U : 0U));
    put(REG_SEQ, sequence);
    put(REG_TIMEOUT, DIGIT_OSD_TIMEOUT_FRAMES);
    put(REG_COMMIT, 1U);
}

void digit_osd_init(void)
{
    put(REG_X, 0U); put(REG_Y, 0U);
    put(REG_W, 0U); put(REG_H, 0U);
    put(REG_CLASS, 0U); put(REG_SCORE, 0U);
    publish(0U, 0U);
}

void digit_osd_set_enabled(uint32_t enabled)
{
    osd_enabled = enabled ? 1U : 0U;
    /* Preserve the current digit state; only switch the enable bit. */
    put(REG_FLAGS, (Xil_In32(DIGIT_OSD_BASE + REG_FLAGS) & 1U) |
                   (osd_enabled ? 2U : 0U));
    put(REG_COMMIT, 1U);
}

void digit_osd_show(uint16_t x, uint16_t y, uint16_t width,
                    uint16_t height, uint32_t digit,
                    uint32_t confidence_permille, uint32_t source_sequence)
{
    uint32_t percent;
    if (digit > 9U || width == 0U || height == 0U ||
        x > 4095U || y > 4095U || width > 4095U || height > 4095U) {
        digit_osd_no_digit(source_sequence);
        return;
    }
    percent = (confidence_permille + 5U) / 10U;
    if (percent > 100U) percent = 100U;
    put(REG_X, x); put(REG_Y, y);
    put(REG_W, width); put(REG_H, height);
    put(REG_CLASS, digit); put(REG_SCORE, percent);
    publish(1U, source_sequence);
}

void digit_osd_no_digit(uint32_t source_sequence)
{
    publish(0U, source_sequence);
}

uint32_t digit_osd_status(void)
{
    return Xil_In32(DIGIT_OSD_BASE + REG_STATUS);
}
