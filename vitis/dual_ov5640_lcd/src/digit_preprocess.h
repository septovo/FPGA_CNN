#ifndef DIGIT_PREPROCESS_H_
#define DIGIT_PREPROCESS_H_

#include <stdint.h>

#define DIGIT_ROI_MAX_SIDE 256U
#define DIGIT_MODEL_SIDE 28U
#define DIGIT_MODEL_BYTES 784U
#define DIGIT_BORDER 40U
#define DIGIT_MAX_COMPONENTS 1024U

typedef enum {
    DIGIT_FOUND = 0,
    DIGIT_NONE = 1,
    DIGIT_TOO_NOISY = 2,
    DIGIT_BAD_INPUT = -1
} DigitPreprocessStatus;

typedef struct {
    uint8_t binary[DIGIT_ROI_MAX_SIDE * DIGIT_ROI_MAX_SIDE];
    uint8_t dilated[DIGIT_ROI_MAX_SIDE * DIGIT_ROI_MAX_SIDE];
    uint8_t closed[DIGIT_ROI_MAX_SIDE * DIGIT_ROI_MAX_SIDE];
    uint8_t visited[(DIGIT_ROI_MAX_SIDE * DIGIT_ROI_MAX_SIDE) / 8U];
    uint16_t queue[DIGIT_ROI_MAX_SIDE * DIGIT_ROI_MAX_SIDE];
} DigitPreprocessWorkspace;

typedef struct {
    uint16_t x;
    uint16_t y;
    uint16_t width;
    uint16_t height;
    uint32_t foreground_pixels;
    uint16_t components;
    uint8_t otsu_threshold;
    uint8_t tensor[DIGIT_MODEL_BYTES];
    uint64_t threshold_ticks;
    uint64_t morphology_ticks;
    uint64_t component_ticks;
    uint64_t resize_ticks;
} DigitPreprocessResult;

typedef uint64_t (*DigitPreprocessClock)(void *context);

/* Grayscale ROI is compact or row-strided, width/height <= 256.
 * Coordinates in result use the original camera frame origin.
 * Workspace and result are caller-owned; result tensor is uint8 [28][28].
 */
DigitPreprocessStatus digit_preprocess(
    const uint8_t *gray, uint16_t width, uint16_t height, uint16_t stride,
    uint16_t origin_x, uint16_t origin_y,
    DigitPreprocessWorkspace *workspace, DigitPreprocessResult *result,
    DigitPreprocessClock clock, void *clock_context);

#endif
