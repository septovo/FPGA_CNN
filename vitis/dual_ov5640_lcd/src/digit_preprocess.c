#include "digit_preprocess.h"

#include <string.h>

#define DP_PIXELS (DIGIT_ROI_MAX_SIDE * DIGIT_ROI_MAX_SIDE)
#define DP_MIN_BOX 14U
#define DP_MIN_AREA 32U

static uint64_t now_or_zero(DigitPreprocessClock clock, void *context)
{
    return clock != 0 ? clock(context) : 0U;
}

static uint8_t otsu_threshold(const uint8_t *gray, uint16_t width,
                              uint16_t height, uint16_t stride)
{
    uint32_t hist[256] = {0};
    uint32_t total = (uint32_t)width * height;
    uint32_t sum = 0U, left_sum = 0U, left_count = 0U;
    double best = -1.0;
    uint8_t best_threshold = 0U;
    uint32_t x, y, t;

    for (y = 0; y < height; ++y)
        for (x = 0; x < width; ++x)
            hist[gray[y * stride + x]]++;
    for (t = 0; t < 256U; ++t) sum += t * hist[t];
    for (t = 0; t < 256U; ++t) {
        uint32_t right_count;
        double mean_left, mean_right, variance;
        left_count += hist[t];
        left_sum += t * hist[t];
        right_count = total - left_count;
        if (left_count == 0U || right_count == 0U) continue;
        mean_left = (double)left_sum / left_count;
        mean_right = (double)(sum - left_sum) / right_count;
        variance = (double)left_count * right_count *
                   (mean_left - mean_right) * (mean_left - mean_right);
        if (variance > best) {
            best = variance;
            best_threshold = (uint8_t)t;
        }
    }
    return best_threshold;
}

static void close_5x5(DigitPreprocessWorkspace *work,
                      uint16_t width, uint16_t height)
{
    uint32_t x, y;
    int dx, dy;
    for (y = 0; y < height; ++y) {
        for (x = 0; x < width; ++x) {
            uint8_t value = 0U;
            for (dy = -2; dy <= 2 && value == 0U; ++dy) {
                int sy = (int)y + dy;
                if (sy < 0 || sy >= height) continue;
                for (dx = -2; dx <= 2; ++dx) {
                    int sx = (int)x + dx;
                    if (sx >= 0 && sx < width &&
                        work->binary[(uint32_t)sy * width + (uint32_t)sx]) {
                        value = 255U;
                        break;
                    }
                }
            }
            work->dilated[y * width + x] = value;
        }
    }
    for (y = 0; y < height; ++y) {
        for (x = 0; x < width; ++x) {
            uint8_t value = 255U;
            for (dy = -2; dy <= 2 && value != 0U; ++dy) {
                int sy = (int)y + dy;
                if (sy < 0 || sy >= height) continue;
                for (dx = -2; dx <= 2; ++dx) {
                    int sx = (int)x + dx;
                    if (sx >= 0 && sx < width &&
                        work->dilated[(uint32_t)sy * width + (uint32_t)sx] == 0U) {
                        value = 0U;
                        break;
                    }
                }
            }
            work->closed[y * width + x] = value;
        }
    }
}

static int is_visited(const uint8_t *visited, uint32_t index)
{
    return (visited[index >> 3U] & (uint8_t)(1U << (index & 7U))) != 0U;
}

static void mark_visited(uint8_t *visited, uint32_t index)
{
    visited[index >> 3U] |= (uint8_t)(1U << (index & 7U));
}

typedef struct {
    uint16_t x, y, width, height;
    uint32_t pixels;
} Candidate;

static DigitPreprocessStatus select_component(DigitPreprocessWorkspace *work,
                                               uint16_t width, uint16_t height,
                                               Candidate *best,
                                               uint16_t *component_count)
{
    uint32_t index, count = (uint32_t)width * height;
    memset(work->visited, 0, (count + 7U) / 8U);
    memset(best, 0, sizeof(*best));
    *component_count = 0U;

    for (index = 0; index < count; ++index) {
        uint32_t head = 0U, tail = 0U, pixels = 0U;
        uint16_t min_x, min_y, max_x, max_y;
        if (work->closed[index] == 0U || is_visited(work->visited, index))
            continue;
        if (++*component_count > DIGIT_MAX_COMPONENTS)
            return DIGIT_TOO_NOISY;
        min_x = max_x = (uint16_t)(index % width);
        min_y = max_y = (uint16_t)(index / width);
        mark_visited(work->visited, index);
        work->queue[tail++] = (uint16_t)index;

        while (head < tail) {
            uint32_t current = work->queue[head++];
            uint16_t cx = (uint16_t)(current % width);
            uint16_t cy = (uint16_t)(current / width);
            int dy, dx;
            ++pixels;
            if (cx < min_x) min_x = cx;
            if (cx > max_x) max_x = cx;
            if (cy < min_y) min_y = cy;
            if (cy > max_y) max_y = cy;
            for (dy = -1; dy <= 1; ++dy) {
                int ny = (int)cy + dy;
                if (ny < 0 || ny >= height) continue;
                for (dx = -1; dx <= 1; ++dx) {
                    int nx = (int)cx + dx;
                    uint32_t neighbor;
                    if ((dx == 0 && dy == 0) || nx < 0 || nx >= width)
                        continue;
                    neighbor = (uint32_t)ny * width + (uint32_t)nx;
                    if (work->closed[neighbor] == 0U ||
                        is_visited(work->visited, neighbor)) continue;
                    mark_visited(work->visited, neighbor);
                    work->queue[tail++] = (uint16_t)neighbor;
                }
            }
        }
        {
            uint16_t box_w = (uint16_t)(max_x - min_x + 1U);
            uint16_t box_h = (uint16_t)(max_y - min_y + 1U);
            if (min_x < DIGIT_BORDER || min_y < DIGIT_BORDER ||
                max_x > width - 2U - DIGIT_BORDER ||
                max_y > height - 2U - DIGIT_BORDER ||
                box_w < DP_MIN_BOX || box_h < DP_MIN_BOX ||
                box_w > width / 2U || box_h > height / 2U ||
                pixels < DP_MIN_AREA) continue;
            if (pixels > best->pixels ||
                (pixels == best->pixels &&
                 (min_y < best->y ||
                  (min_y == best->y && min_x < best->x)))) {
                best->x = min_x;
                best->y = min_y;
                best->width = box_w;
                best->height = box_h;
                best->pixels = pixels;
            }
        }
    }
    return best->pixels == 0U ? DIGIT_NONE : DIGIT_FOUND;
}

static uint8_t sample_padded(const DigitPreprocessWorkspace *work,
                             uint16_t roi_width, const Candidate *box,
                             uint32_t sx, uint32_t sy,
                             uint32_t x_pad, uint32_t y_pad)
{
    if (sx < x_pad || sy < y_pad ||
        sx >= x_pad + box->width || sy >= y_pad + box->height)
        return 0U;
    return work->closed[((uint32_t)box->y + sy - y_pad) * roi_width +
                        (uint32_t)box->x + sx - x_pad];
}

static uint8_t rounded_byte(uint64_t value, uint64_t divisor)
{
    uint64_t quotient = value / divisor;
    uint64_t remainder = value % divisor;
    if (remainder * 2U > divisor ||
        (remainder * 2U == divisor && (quotient & 1U))) ++quotient;
    return (uint8_t)(quotient > 255U ? 255U : quotient);
}

static void resize_area_28(const DigitPreprocessWorkspace *work,
                           uint16_t roi_width, const Candidate *box,
                           uint8_t *tensor)
{
    uint32_t longer = box->width > box->height ? box->width : box->height;
    uint32_t x_pad = (box->height > box->width ?
                      (box->height - box->width) / 2U : 0U) + longer / 5U;
    uint32_t y_pad = (box->width > box->height ?
                      (box->width - box->height) / 2U : 0U) + longer / 5U;
    uint32_t source_w = box->width + 2U * x_pad;
    uint32_t source_h = box->height + 2U * y_pad;
    uint32_t out_x, out_y;

    for (out_y = 0; out_y < DIGIT_MODEL_SIDE; ++out_y) {
        for (out_x = 0; out_x < DIGIT_MODEL_SIDE; ++out_x) {
            uint32_t left = out_x * source_w;
            uint32_t right = (out_x + 1U) * source_w;
            uint32_t top = out_y * source_h;
            uint32_t bottom = (out_y + 1U) * source_h;
            uint32_t sy, sx;
            uint64_t sum = 0U;
            for (sy = top / DIGIT_MODEL_SIDE;
                 sy <= (bottom - 1U) / DIGIT_MODEL_SIDE; ++sy) {
                uint32_t overlap_y = (sy + 1U) * DIGIT_MODEL_SIDE < bottom ?
                                     (sy + 1U) * DIGIT_MODEL_SIDE : bottom;
                uint32_t start_y = sy * DIGIT_MODEL_SIDE > top ?
                                   sy * DIGIT_MODEL_SIDE : top;
                overlap_y -= start_y;
                for (sx = left / DIGIT_MODEL_SIDE;
                     sx <= (right - 1U) / DIGIT_MODEL_SIDE; ++sx) {
                    uint32_t overlap_x = (sx + 1U) * DIGIT_MODEL_SIDE < right ?
                                         (sx + 1U) * DIGIT_MODEL_SIDE : right;
                    uint32_t start_x = sx * DIGIT_MODEL_SIDE > left ?
                                       sx * DIGIT_MODEL_SIDE : left;
                    overlap_x -= start_x;
                    sum += (uint64_t)sample_padded(work, roi_width, box,
                                                   sx, sy, x_pad, y_pad) *
                           overlap_x * overlap_y;
                }
            }
            tensor[out_y * DIGIT_MODEL_SIDE + out_x] =
                rounded_byte(sum, (uint64_t)source_w * source_h);
        }
    }
}

DigitPreprocessStatus digit_preprocess(
    const uint8_t *gray, uint16_t width, uint16_t height, uint16_t stride,
    uint16_t origin_x, uint16_t origin_y,
    DigitPreprocessWorkspace *workspace, DigitPreprocessResult *result,
    DigitPreprocessClock clock, void *clock_context)
{
    uint32_t x, y;
    uint8_t threshold;
    uint64_t start, end;
    Candidate candidate;
    DigitPreprocessStatus status;
    if (gray == 0 || workspace == 0 || result == 0 ||
        width < DIGIT_MODEL_SIDE || height < DIGIT_MODEL_SIDE ||
        width > DIGIT_ROI_MAX_SIDE || height > DIGIT_ROI_MAX_SIDE ||
        stride < width) return DIGIT_BAD_INPUT;
    memset(result, 0, sizeof(*result));

    start = now_or_zero(clock, clock_context);
    threshold = otsu_threshold(gray, width, height, stride);
    result->otsu_threshold = threshold;
    for (y = 0; y < height; ++y)
        for (x = 0; x < width; ++x)
            workspace->binary[y * width + x] =
                gray[y * stride + x] <= threshold ? 255U : 0U;
    end = now_or_zero(clock, clock_context);
    result->threshold_ticks = end - start;

    start = end;
    close_5x5(workspace, width, height);
    end = now_or_zero(clock, clock_context);
    result->morphology_ticks = end - start;

    start = end;
    status = select_component(workspace, width, height,
                              &candidate, &result->components);
    end = now_or_zero(clock, clock_context);
    result->component_ticks = end - start;
    if (status != DIGIT_FOUND) return status;

    start = end;
    resize_area_28(workspace, width, &candidate, result->tensor);
    end = now_or_zero(clock, clock_context);
    result->resize_ticks = end - start;
    result->x = (uint16_t)(origin_x + candidate.x);
    result->y = (uint16_t)(origin_y + candidate.y);
    result->width = candidate.width;
    result->height = candidate.height;
    result->foreground_pixels = candidate.pixels;
    return DIGIT_FOUND;
}
