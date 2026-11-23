#include "perf_timer.h"
#include <string.h>

static const double kMph = 1.609344;
static const double kQuarterMetres = 402.336;
static const double kStoppedKph = .5;
static const unsigned kArmMs = 500, kGapMs = 1000, kStandingMaxMs = 60000, kRollingMaxMs = 40000;
static const double kLiftKph = 5;   /* a rolling run ends when speed falls this far below its peak */

static double roll_from(unsigned i) { return i ? 100 : 60 * kMph; }
static double roll_to(unsigned i) { return i ? 200 : 130 * kMph; }

void perf_reset(perf_timer *t) { if (t) memset(t, 0, sizeof *t); }

static void record(perf_timer *t, unsigned run, double seconds) {
    t->result[run] = seconds;
    t->have[run] = 1;
    if (!t->have_best[run] || seconds < t->best[run]) {
        t->best[run] = seconds;
        t->have_best[run] = 1;
        if (run == PT_QUARTER) t->best_trap_kph = t->trap_kph;
    }
}

/* The time at which speed crossed target between (t0, v0) and (t1, v1), in seconds. */
static double cross(double t0, double v0, double t1, double v1, double target) {
    return v1 == v0 ? t1 : t0 + (target - v0) / (v1 - v0) * (t1 - t0);
}

static void abort_runs(perf_timer *t) {
    t->standing = 0;
    t->armed = 0;
    t->rolling[0] = t->rolling[1] = 0;
}

int perf_sample(perf_timer *t, uint64_t ms, double kph) {
    int recorded = 0;
    unsigned i;
    double t0, t1, v0, v1;
    if (!t) return 0;
    if (kph < 0 || kph > 400) { abort_runs(t); t->have_last = 0; return 0; }
    if (!t->have_last || ms <= t->last_ms || ms - t->last_ms > kGapMs) {
        abort_runs(t);
        t->have_last = 1;
        t->last_ms = ms;
        t->last_kph = kph;
        t->stopped_since = kph <= kStoppedKph ? ms : 0;
        if (kph <= kStoppedKph) t->zero_ms = ms;
        return 0;
    }
    t0 = t->last_ms / 1000.0; t1 = ms / 1000.0;
    v0 = t->last_kph; v1 = kph;

    if (v1 <= kStoppedKph) {
        if (!t->stopped_since) t->stopped_since = ms;
        t->zero_ms = ms;
        t->standing = 0;
        if (ms - t->stopped_since >= kArmMs) t->armed = 1;
    } else {
        t->stopped_since = 0;
        if (t->armed && !t->standing) {
            t->standing = 1;
            t->armed = 0;
            t->launch_ms = t->zero_ms;
            t->distance_m = 0;
            t->standing_peak = 0;
            memset(t->done_standing, 0, sizeof t->done_standing);
            t0 = t->zero_ms / 1000.0;   /* the run starts at the last zero sample */
            v0 = 0;
        }
    }

    if (t->standing) {
        double start = t->launch_ms / 1000.0;
        double step = (v0 + v1) / 2 / 3.6 * (t1 - t0);
        if (v1 > t->standing_peak) t->standing_peak = v1;
        if (!t->done_standing[0] && v0 < 60 * kMph && v1 >= 60 * kMph) {
            record(t, PT_0_60, cross(t0, v0, t1, v1, 60 * kMph) - start);
            t->done_standing[0] = recorded = 1;
        }
        if (!t->done_standing[1] && v0 < 100 && v1 >= 100) {
            record(t, PT_0_100, cross(t0, v0, t1, v1, 100) - start);
            t->done_standing[1] = recorded = 1;
        }
        if (!t->done_standing[2] && t->distance_m + step >= kQuarterMetres && step > 0) {
            double f = (kQuarterMetres - t->distance_m) / step;
            t->trap_kph = v0 + (v1 - v0) * f;
            record(t, PT_QUARTER, t0 + f * (t1 - t0) - start);
            t->done_standing[2] = recorded = 1;
        }
        t->distance_m += step;
        if (t->done_standing[2] || ms - t->launch_ms > kStandingMaxMs) t->standing = 0;
    }

    for (i = 0; i < 2; ++i) {
        if (!t->rolling[i] && v0 < roll_from(i) && v1 >= roll_from(i)) {
            t->rolling[i] = 1;
            t->roll_start_s[i] = cross(t0, v0, t1, v1, roll_from(i));
            t->roll_peak[i] = v1;
        } else if (t->rolling[i]) {
            if (v1 > t->roll_peak[i]) t->roll_peak[i] = v1;
            if (v1 < t->roll_peak[i] - kLiftKph || t1 - t->roll_start_s[i] > kRollingMaxMs / 1000.0)
                t->rolling[i] = 0;
        }
        if (t->rolling[i] && v0 < roll_to(i) && v1 >= roll_to(i)) {
            record(t, i ? PT_100_200 : PT_60_130, cross(t0, v0, t1, v1, roll_to(i)) - t->roll_start_s[i]);
            t->rolling[i] = 0;
            recorded = 1;
        }
    }

    t->last_ms = ms;
    t->last_kph = kph;
    return recorded;
}

int perf_state(const perf_timer *t) {
    if (!t) return PT_WAITING;
    if (t->standing || t->rolling[0] || t->rolling[1]) return PT_RUNNING;
    return t->armed ? PT_ARMED : PT_WAITING;
}

const char *perf_name(unsigned run, int metric) {
    static const char *const names[PT_COUNT] = {"0-60 mph", "0-100 km/h", "60-130 mph", "100-200 km/h",
                                                "1/4 mile"};
    (void)metric;
    return run < PT_COUNT ? names[run] : "";
}
