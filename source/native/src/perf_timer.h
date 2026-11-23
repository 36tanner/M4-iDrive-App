/* Performance timers from vehicle speed samples: 0-60 mph, 0-100 km/h, 60-130 mph, 100-200 km/h and
 * the quarter mile with trap speed. Crossings are interpolated between samples. Standing runs start at
 * the last zero-speed sample, so they include up to one sample interval: a little conservative. */
#ifndef PERF_TIMER_H
#define PERF_TIMER_H
#include <stdint.h>
#ifdef __cplusplus
extern "C" {
#endif

enum { PT_0_60, PT_0_100, PT_60_130, PT_100_200, PT_QUARTER, PT_COUNT };
enum { PT_WAITING, PT_ARMED, PT_RUNNING };

typedef struct {
    int have_last;
    uint64_t last_ms, stopped_since, zero_ms;
    double last_kph;
    int armed, standing;           /* standing: a run from rest in progress */
    uint64_t launch_ms;
    double distance_m, standing_peak;
    int rolling[2];                /* 60-130 mph, 100-200 km/h */
    double roll_start_s[2], roll_peak[2];
    int done_standing[3];          /* 0-60, 0-100, quarter finished in this run */
    int have[PT_COUNT], have_best[PT_COUNT];
    double result[PT_COUNT], best[PT_COUNT];   /* seconds */
    double trap_kph, best_trap_kph;
} perf_timer;

void perf_reset(perf_timer *t);              /* clears runs, results and bests */
/* One speed sample in km/h at its sample time; kph < 0 means unavailable and aborts running timers.
 * Returns 1 when a result was recorded by this sample. */
int perf_sample(perf_timer *t, uint64_t ms, double kph);
int perf_state(const perf_timer *t);
const char *perf_name(unsigned run, int metric);

#ifdef __cplusplus
}
#endif
#endif
