#include "runtime_stats.h"
#include <assert.h>
#include <stdio.h>
int main(void)
{
    RuntimeStats s; RuntimeStatsSnapshot o; unsigned int i;
    runtime_stats_reset(&s);
    runtime_stats_record_preprocess(&s, 100);
    runtime_stats_record_preprocess(&s, 300);
    for (i=1; i<=100; ++i) runtime_stats_record_inference(&s, i, 1);
    runtime_stats_record_inference(&s, 999, 0);
    runtime_stats_snapshot(&s, &o);
    assert(o.preprocess_average_us==200);
    assert(o.inference_average_us==50);
    assert(o.inference_p95_us==95);
    assert(o.inference_min_us==1 && o.inference_max_us==100);
    assert(s.inference_errors==1 && o.window_samples==100);
    for (i=0; i<300; ++i) runtime_stats_record_inference(&s, 1000+i, 1);
    runtime_stats_snapshot(&s, &o);
    assert(o.window_samples==256 && o.inference_max_us==1299);
    puts("RUNTIME_STATS_PASS");
    return 0;
}
