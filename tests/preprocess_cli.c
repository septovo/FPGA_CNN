#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include "digit_preprocess.h"

static DigitPreprocessWorkspace workspace;
static DigitPreprocessResult result;
static uint8_t gray[DIGIT_ROI_MAX_SIDE * DIGIT_ROI_MAX_SIDE];

int main(int argc, char **argv)
{
    FILE *input, *output;
    char debug_path[512];
    unsigned long width, height;
    DigitPreprocessStatus status;
    if (argc != 5) return 2;
    width = strtoul(argv[2], 0, 10);
    height = strtoul(argv[3], 0, 10);
    if (width > DIGIT_ROI_MAX_SIDE || height > DIGIT_ROI_MAX_SIDE ||
        width == 0UL || height == 0UL) return 2;
    input = fopen(argv[1], "rb");
    if (input == 0) return 2;
    if (fread(gray, 1, width * height, input) != width * height) {
        fclose(input);
        return 2;
    }
    fclose(input);
    status = digit_preprocess(gray, (uint16_t)width, (uint16_t)height,
                              (uint16_t)width, 0U, 0U,
                              &workspace, &result, 0, 0);
    printf("status=%d x=%u y=%u w=%u h=%u area=%lu components=%u otsu=%u\n",
           status, result.x, result.y, result.width, result.height,
           (unsigned long)result.foreground_pixels, result.components,
           result.otsu_threshold);
    if (snprintf(debug_path, sizeof(debug_path), "%s.binary", argv[4]) >=
        (int)sizeof(debug_path)) return 2;
    output = fopen(debug_path, "wb");
    if (output == 0) return 2;
    fwrite(workspace.binary, 1, width * height, output);
    fclose(output);
    if (snprintf(debug_path, sizeof(debug_path), "%s.closed", argv[4]) >=
        (int)sizeof(debug_path)) return 2;
    output = fopen(debug_path, "wb");
    if (output == 0) return 2;
    fwrite(workspace.closed, 1, width * height, output);
    fclose(output);
    if (status != DIGIT_FOUND) return status < 0 ? 2 : 0;
    output = fopen(argv[4], "wb");
    if (output == 0) return 2;
    if (fwrite(result.tensor, 1, DIGIT_MODEL_BYTES, output) != DIGIT_MODEL_BYTES) {
        fclose(output);
        return 2;
    }
    fclose(output);
    return 0;
}
