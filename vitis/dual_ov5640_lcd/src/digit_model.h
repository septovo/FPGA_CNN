#ifndef DIGIT_MODEL_H
#define DIGIT_MODEL_H

#include <stdint.h>

#define DIGIT_MODEL_INPUT_SIZE 784U
#define DIGIT_MODEL_CLASSES 10U
#define DIGIT_MODEL_WORKSPACE_BYTES 233984U

/* The pinned TFLite model casts raw uint8 pixels to float; no /255 scaling. */
int digit_model_infer(const uint8_t pixels[DIGIT_MODEL_INPUT_SIZE],
                      float scores[DIGIT_MODEL_CLASSES]);
const char *digit_model_sha256(void);

#endif

