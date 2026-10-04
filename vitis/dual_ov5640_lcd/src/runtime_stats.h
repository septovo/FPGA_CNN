#ifndef RUNTIME_STATS_H
#define RUNTIME_STATS_H
#include <stdint.h>
#define RUNTIME_STATS_WINDOW 256U

typedef struct {
    uint32_t preprocess_samples;
    uint32_t inference_samples;
    uint32_t inference_errors;
    uint64_t preprocess_total_us;
    uint64_t inference_total_us;
    uint32_t inference_min_us;
    uint32_t inference_max_us;
    uint32_t inference_window[RUNTIME_STATS_WINDOW];
    uint16_t inference_window_count;
    uint16_t inference_window_next;
} RuntimeStats;

typedef struct {
    uint32_t preprocess_average_us;
    uint32_t inference_average_us;
    uint32_t inference_p95_us;
    uint32_t inference_min_us;
    uint32_t inference_max_us;
    uint32_t window_samples;
} RuntimeStatsSnapshot;

void runtime_stats_reset(RuntimeStats *stats);
void runtime_stats_record_preprocess(RuntimeStats *stats, uint32_t elapsed_us);
void runtime_stats_record_inference(RuntimeStats *stats, uint32_t elapsed_us,
                                    int success);
void runtime_stats_snapshot(const RuntimeStats *stats, RuntimeStatsSnapshot *out);
#endif
