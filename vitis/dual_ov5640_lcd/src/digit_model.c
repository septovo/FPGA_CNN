#include "digit_model.h"
#include "digit_weights.h"

#include <math.h>
#include <string.h>

/* Two buffers cover the peak live activations: 86,528 + 147,456 bytes. */
static float activation_a[26U * 26U * 32U];
static float activation_b[24U * 24U * 64U];

static float weight(const uint32_t *bits, unsigned int index)
{
    float value;
    memcpy(&value, bits + index, sizeof(value));
    return value;
}

const char *digit_model_sha256(void)
{
    return "b0ae6afa9e9bceed8ae3038c0a844634eddc24f30f262c5b7e5ae800db2db50d";
}

int digit_model_infer(const uint8_t pixels[DIGIT_MODEL_INPUT_SIZE],
                      float scores[DIGIT_MODEL_CLASSES])
{
    unsigned int y, x, oc, ky, kx, ic, p, j;
    float logits[10];
    float max_logit, total;
    if (pixels == 0 || scores == 0) return -1;

    /* CONV_2D: VALID, stride 1, OHWI filter, bias, fused ReLU. */
    for (y = 0; y < 26U; ++y) {
        for (x = 0; x < 26U; ++x) {
            for (oc = 0; oc < 32U; ++oc) {
                float sum = weight(conv1_bias_bits, oc);
                for (ky = 0; ky < 3U; ++ky)
                    for (kx = 0; kx < 3U; ++kx)
                        sum += (float)pixels[(y + ky) * 28U + x + kx] *
                               weight(conv1_filter_bits, (oc * 3U + ky) * 3U + kx);
                activation_a[(y * 26U + x) * 32U + oc] = fmaxf(sum, 0.0f);
            }
        }
    }
    for (y = 0; y < 24U; ++y) {
        for (x = 0; x < 24U; ++x) {
            for (oc = 0; oc < 64U; ++oc) {
                float sum = weight(conv2_bias_bits, oc);
                for (ky = 0; ky < 3U; ++ky) {
                    for (kx = 0; kx < 3U; ++kx) {
                        const float *src = &activation_a[((y + ky) * 26U + x + kx) * 32U];
                        const unsigned int base = ((oc * 3U + ky) * 3U + kx) * 32U;
                        for (ic = 0; ic < 32U; ++ic)
                            sum += src[ic] * weight(conv2_filter_bits, base + ic);
                    }
                }
                activation_b[(y * 24U + x) * 64U + oc] = fmaxf(sum, 0.0f);
            }
        }
    }

    /* MAX_POOL_2D 2x2 stride 2; reuse activation_a as flattened output. */
    for (y = 0; y < 12U; ++y) {
        for (x = 0; x < 12U; ++x) {
            for (oc = 0; oc < 64U; ++oc) {
                unsigned int at = ((2U * y) * 24U + 2U * x) * 64U + oc;
                float v = activation_b[at];
                if (activation_b[at + 64U] > v) v = activation_b[at + 64U];
                if (activation_b[at + 24U * 64U] > v) v = activation_b[at + 24U * 64U];
                if (activation_b[at + 25U * 64U] > v) v = activation_b[at + 25U * 64U];
                activation_a[(y * 12U + x) * 64U + oc] = v;
            }
        }
    }

    /* FULLY_CONNECTED: output-major weights [10, 9216]. */
    for (j = 0; j < 10U; ++j) {
        float sum = weight(dense_bias_bits, j);
        for (p = 0; p < 9216U; ++p)
            sum += activation_a[p] * weight(dense_filter_bits, j * 9216U + p);
        logits[j] = sum;
        if (!isfinite(sum)) return -2;
    }
    max_logit = logits[0];
    for (j = 1; j < 10U; ++j)
        if (logits[j] > max_logit) max_logit = logits[j];
    total = 0.0f;
    for (j = 0; j < 10U; ++j) {
        scores[j] = expf(logits[j] - max_logit);
        total += scores[j];
    }
    if (!(total > 0.0f) || !isfinite(total)) return -3;
    for (j = 0; j < 10U; ++j) scores[j] /= total;
    return 0;
}
