#include "runtime_stats.h"
#include <string.h>

void runtime_stats_reset(RuntimeStats *stats)
{
    if (stats == 0) return;
    memset(stats, 0, sizeof(*stats));
    stats->inference_min_us = UINT32_MAX;
}

void runtime_stats_record_preprocess(RuntimeStats *stats, uint32_t elapsed_us)
{
    if (stats == 0) return;
    ++stats->preprocess_samples;
    stats->preprocess_total_us += elapsed_us;
}

void runtime_stats_record_inference(RuntimeStats *stats, uint32_t elapsed_us,
                                    int success)
{
    if (stats == 0) return;
    if (!success) {
        ++stats->inference_errors;
        return;
    }
    ++stats->inference_samples;
    stats->inference_total_us += elapsed_us;
    if (elapsed_us < stats->inference_min_us) stats->inference_min_us = elapsed_us;
    if (elapsed_us > stats->inference_max_us) stats->inference_max_us = elapsed_us;
    stats->inference_window[stats->inference_window_next] = elapsed_us;
    stats->inference_window_next =
        (uint16_t)((stats->inference_window_next + 1U) % RUNTIME_STATS_WINDOW);
    if (stats->inference_window_count < RUNTIME_STATS_WINDOW)
        ++stats->inference_window_count;
}

void runtime_stats_snapshot(const RuntimeStats *stats, RuntimeStatsSnapshot *out)
{
    uint32_t sorted[RUNTIME_STATS_WINDOW];
    uint32_t i, j, value, rank;
    if (out == 0) return;
    memset(out, 0, sizeof(*out));
    if (stats == 0) return;
    if (stats->preprocess_samples != 0U)
        out->preprocess_average_us =
            (uint32_t)(stats->preprocess_total_us / stats->preprocess_samples);
    if (stats->inference_samples != 0U) {
        out->inference_average_us =
            (uint32_t)(stats->inference_total_us / stats->inference_samples);
        out->inference_min_us = stats->inference_min_us;
        out->inference_max_us = stats->inference_max_us;
    }
    out->window_samples = stats->inference_window_count;
    for (i = 0; i < stats->inference_window_count; ++i)
        sorted[i] = stats->inference_window[i];
    for (i = 1; i < stats->inference_window_count; ++i) {
        value = sorted[i];
        j = i;
        while (j != 0U && sorted[j - 1U] > value) {
            sorted[j] = sorted[j - 1U];
            --j;
        }
        sorted[j] = value;
    }
    if (stats->inference_window_count != 0U) {
        rank = (95U * stats->inference_window_count + 99U) / 100U;
        if (rank == 0U) rank = 1U;
        out->inference_p95_us = sorted[rank - 1U];
    }
}
