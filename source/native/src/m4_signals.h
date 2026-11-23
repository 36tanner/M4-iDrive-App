/* F82 M4 / S55 additions: decoders for the extra read-only signals, the extras wire format, peak
 * tracking and the health summary. Signal provenance: docs/M4_S55.md (OBDb BMW-M4 2015 captures). */
#ifndef M4_SIGNALS_H
#define M4_SIGNALS_H
#include <stddef.h>
#include <stdint.h>
#include "gauge_core.h"
#ifdef __cplusplus
extern "C" {
#endif

/* Read requests outside UDS 22 are tagged so the collector's DID list can carry them. */
enum { M4_OBD_PID = 0x10000u, M4_RAIL_PID = M4_OBD_PID | 0x23u };

enum { M4_DME = 0x12, M4_DSC = 0x29 };
enum { M4_DID_OIL_TEMP = 0x4408, M4_DID_BOOST_TARGET = 0x4ab0, M4_DID_BATTERY = 0x402b,
       M4_DID_REV_LIMIT = 0x4a2e, M4_DID_TIRE_FL = 0xdc98 }; /* tires: dc98..dc9b, FL FR RL RR */

enum { MX_BOOST_TARGET, MX_BATTERY, MX_REV_LIMIT,
       MX_TP_FL, MX_TP_FR, MX_TP_RL, MX_TP_RR,       /* bar */
       MX_TS_FL, MX_TS_FR, MX_TS_RL, MX_TS_RR,       /* set point, bar */
       MX_TT_FL, MX_TT_FR, MX_TT_RL, MX_TT_RR,       /* tire temperature, C */
       MX_COUNT };
enum { MX_STALE_MS = 3000, MX_SLOW_STALE_MS = 25000 };

typedef struct {
    uint32_t seq;
    unsigned valid;
    uint64_t sent_ms;
    double value[MX_COUNT];
    unsigned age_ms[MX_COUNT];
} m4_extra;

/* Payload bytes after the 62 DID prefix. 1 decoded, 0 unavailable. */
int m4_decode_battery(const unsigned char *p, size_t n, double *volts);
int m4_decode_rev_limit(const unsigned char *p, size_t n, double *rpm);
/* Tire record: pressure and set point in bar, temperature in C. Any may be unavailable. */
int m4_decode_tire(const unsigned char *p, size_t n, double *bar, double *set_bar, double *celsius);
/* OBD service 01 payload after 41 <pid>. Rail pressure (PID 23) in bar. */
int m4_decode_pid(unsigned pid, const unsigned char *p, size_t n, double *out);

int m4_extra_encode(const m4_extra *x, char *out, size_t len);
int m4_extra_parse(const char *data, size_t len, m4_extra *out);
int m4_extra_fresh(const m4_extra *x, unsigned i, uint64_t elapsed_ms);

/* Warm-up: oil and coolant in C. */
enum { M4_WARM_COLD, M4_WARM_WARMING, M4_WARM_GO, M4_WARM_HOT, M4_WARM_UNKNOWN };
int m4_warmup(int have_oil, double oil, int have_coolant, double coolant);
int m4_warmup_step(int have_oil, double oil, unsigned steps); /* bar fill, 0..steps */
const char *m4_warmup_word(int state);

/* Peaks since the last reset. Boost-gated values only count above kBoostGatePsi. */
typedef struct {
    int have_boost, have_iat, have_oil, have_coolant, have_timing, have_afr, have_deficit;
    double boost, iat, oil, coolant, timing_low, afr_lean, deficit; /* psi, C, C, C, deg, AFR, psi */
    uint64_t deficit_since;   /* when the current under-target stretch began, 0 when none */
} m4_peaks;
void m4_peaks_reset(m4_peaks *p);
/* f: a received gauge frame; x: extras (may be 0). Only fresh values count. */
void m4_peaks_update(m4_peaks *p, const bd_frame *f, uint64_t f_elapsed,
                     const m4_extra *x, uint64_t x_elapsed, uint64_t now_ms);

/* Bar color: 0 normal (the chosen color), 1 caution (yellow), 2 warning (red). Only gauges with
 * limits we trust are colored: boost under target, AFR lean under boost, intake air, oil, coolant. */
enum { M4_LEVEL_OK, M4_LEVEL_CAUTION, M4_LEVEL_WARNING };
int m4_gauge_level(unsigned slot, const bd_frame *f, uint64_t f_elapsed, const m4_extra *x,
                   uint64_t x_elapsed, const m4_peaks *p, uint64_t now_ms);

/* One line, at most len bytes: "OK" or "Check: <issue>, <issue>". Returns the number of issues. */
int m4_health(const bd_frame *f, uint64_t f_elapsed, const m4_extra *x, uint64_t x_elapsed,
              const m4_peaks *p, char *out, size_t len);

#ifdef __cplusplus
}
#endif
#endif
