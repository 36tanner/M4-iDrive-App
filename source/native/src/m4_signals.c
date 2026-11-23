#define _POSIX_C_SOURCE 200809L
#include "m4_signals.h"
#include <ctype.h>
#include <errno.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* Thresholds. Oil and coolant in C, boost in psi, pressure in bar. */
static const double kWarmOil = 75, kWarmCoolant = 75, kColdOil = 40;
static const double kHotOil = 135, kHotCoolant = 115, kHotIntake = 65;
static const double kWarmishOil = 120, kWarmishCoolant = 110, kWarmishIntake = 50;
static const double kCautionAfr = 13.0, kDeficitWarningPsi = 5;
static const double kBatteryLow = 12.0, kBatteryHigh = 15.3;
static const double kTireLowMargin = 0.2, kTireLowAbsolute = 1.6;
static const double kBoostGatePsi = 5, kAfrGatePsi = 8, kLeanAfr = 13.5;
static const double kDeficitTargetPsi = 8, kDeficitPsi = 3;
static const unsigned kDeficitMs = 1000;

static unsigned be16(const unsigned char *p) { return ((unsigned)p[0] << 8) | p[1]; }

int m4_decode_battery(const unsigned char *p, size_t n, double *volts) {
    unsigned r;
    double v;
    if (!p || !volts || n < 6) return 0;
    r = be16(p + 2);
    if (r == 0xffff) return 0;
    v = r / 4000.0 + 6;
    if (v < 6 || v > 20) return 0;
    *volts = v;
    return 1;
}

int m4_decode_rev_limit(const unsigned char *p, size_t n, double *rpm) {
    unsigned r;
    if (!p || !rpm || n != 2) return 0;
    r = be16(p);
    if (r < 3000 || r > 9000) return 0;
    *rpm = r;
    return 1;
}

static int le_signed16(const unsigned char *p, double *out) {
    unsigned r = (unsigned)p[0] | ((unsigned)p[1] << 8);
    if (r == 0xffff || r == 0x7fff || r == 0x8000) return 0;
    *out = (r & 0x8000 ? (int)r - 65536 : (int)r) / 1000.0;
    return 1;
}

int m4_decode_tire(const unsigned char *p, size_t n, double *bar, double *set_bar, double *celsius) {
    double x;
    int any = 0;
    if (!p || n < 10) return 0;
    if (bar && le_signed16(p + 5, &x) && x > .3 && x < 6) { *bar = x; any |= 1; }
    if (celsius && p[7] != 0x7f && p[7] != 0x80) {
        x = p[7] & 0x80 ? (int)p[7] - 256 : p[7];
        if (x >= -40 && x <= 125) { *celsius = x; any |= 2; }
    }
    if (set_bar && le_signed16(p + 8, &x) && x > .5 && x < 6) { *set_bar = x; any |= 4; }
    return any;
}

int m4_decode_pid(unsigned pid, const unsigned char *p, size_t n, double *out) {
    double x;
    if (!p || !out) return 0;
    switch (pid) {
    case 0x23: /* fuel rail gauge pressure, SAE J1979: 10 kPa per count */
        if (n != 2) return 0;
        x = be16(p) / 10.0;
        if (x < 0 || x > 500) return 0;
        break;
    default: return 0;
    }
    *out = x;
    return 1;
}

int m4_extra_encode(const m4_extra *x, char *out, size_t len) {
    int n;
    size_t used;
    unsigned i;
    if (!x || !out || !len) return -1;
    n = snprintf(out, len, "NBX1 %lu %u %llu", (unsigned long)x->seq, x->valid,
                 (unsigned long long)x->sent_ms);
    if (n < 0 || (size_t)n >= len) return -1;
    used = (size_t)n;
    for (i = 0; i < MX_COUNT; ++i) {
        n = snprintf(out + used, len - used, " %.4f %u", x->value[i], x->age_ms[i]);
        if (n < 0 || (size_t)n >= len - used) return -1;
        used += (size_t)n;
    }
    if (len - used < 2) return -1;
    out[used++] = '\n';
    out[used] = 0;
    return (int)used;
}

static int field_u(char **c, unsigned long long max, unsigned long long *out) {
    char *p = *c, *end;
    unsigned long long v;
    while (*p == ' ') ++p;
    if (!isdigit((unsigned char)*p)) return 0;
    errno = 0;
    v = strtoull(p, &end, 10);
    if (errno || end == p || (*end && *end != ' ' && *end != '\n') || v > max) return 0;
    *c = end; *out = v;
    return 1;
}
static int field_d(char **c, double *out) {
    char *p = *c, *end;
    double v;
    while (*p == ' ') ++p;
    errno = 0;
    v = strtod(p, &end);
    if (errno || end == p || (*end && *end != ' ' && *end != '\n') || !isfinite(v) ||
        fabs(v) > 100000) return 0;
    *c = end; *out = v;
    return 1;
}

int m4_extra_parse(const char *data, size_t len, m4_extra *out) {
    char b[BD_MAX_PACKET];
    char *c;
    m4_extra x;
    unsigned long long u;
    unsigned i;
    if (!data || !out || len < 6 || len >= sizeof b || memchr(data, 0, len)) return 0;
    if (memcmp(data, "NBX1 ", 5)) return 0;
    memcpy(b, data, len); b[len] = 0;
    memset(&x, 0, sizeof x);
    c = b + 4;
    if (!field_u(&c, 0xffffffffull, &u)) return 0;
    x.seq = (uint32_t)u;
    if (!field_u(&c, (1ull << MX_COUNT) - 1, &u)) return 0;
    x.valid = (unsigned)u;
    if (!field_u(&c, ~0ull, &u)) return 0;
    x.sent_ms = u;
    for (i = 0; i < MX_COUNT; ++i) {
        if (!field_d(&c, &x.value[i]) || !field_u(&c, 999999, &u)) return 0;
        x.age_ms[i] = (unsigned)u;
    }
    while (*c == ' ' || *c == '\n') ++c;
    if (*c) return 0;
    *out = x;
    return 1;
}

int m4_extra_fresh(const m4_extra *x, unsigned i, uint64_t elapsed) {
    unsigned limit = i == MX_BOOST_TARGET ? MX_STALE_MS : MX_SLOW_STALE_MS;
    return x && i < MX_COUNT && (x->valid & (1u << i)) && elapsed < limit &&
           x->age_ms[i] < limit - elapsed;
}

int m4_warmup(int have_oil, double oil, int have_coolant, double coolant) {
    if ((have_oil && oil >= kHotOil) || (have_coolant && coolant >= kHotCoolant)) return M4_WARM_HOT;
    if (!have_oil) return M4_WARM_UNKNOWN;
    if (oil < kColdOil) return M4_WARM_COLD;
    if (oil >= kWarmOil && (!have_coolant || coolant >= kWarmCoolant)) return M4_WARM_GO;
    return M4_WARM_WARMING;
}

int m4_warmup_step(int have_oil, double oil, unsigned steps) {
    double f;
    if (!have_oil || !isfinite(oil)) return 0;
    f = (oil - 20) / (kWarmOil - 20);
    if (f < 0) f = 0;
    if (f > 1) f = 1;
    return (int)(f * steps + .5);
}

const char *m4_warmup_word(int state) {
    switch (state) {
    case M4_WARM_COLD: return "COLD";
    case M4_WARM_WARMING: return "WARM";
    case M4_WARM_GO: return "GO";
    case M4_WARM_HOT: return "HOT";
    default: return "--";
    }
}

void m4_peaks_reset(m4_peaks *p) { if (p) memset(p, 0, sizeof *p); }

static void raise_max(int *have, double *peak, double v) {
    if (!*have || v > *peak) { *peak = v; *have = 1; }
}

void m4_peaks_update(m4_peaks *p, const bd_frame *f, uint64_t fe,
                     const m4_extra *x, uint64_t xe, uint64_t now) {
    int boost_ok;
    double boost = 0;
    if (!p || !f) return;
    boost_ok = bd_is_fresh(f, BD_BOOST, fe);
    if (boost_ok) { boost = f->value[BD_BOOST]; raise_max(&p->have_boost, &p->boost, boost); }
    if (bd_is_fresh(f, BD_IAT, fe)) raise_max(&p->have_iat, &p->iat, f->value[BD_IAT]);
    if (bd_is_fresh(f, BD_TRANS, fe)) raise_max(&p->have_oil, &p->oil, f->value[BD_TRANS]);
    if (bd_is_fresh(f, BD_COOLANT, fe)) raise_max(&p->have_coolant, &p->coolant, f->value[BD_COOLANT]);
    if (boost_ok && boost >= kBoostGatePsi && bd_is_fresh(f, BD_TIMING, fe) &&
        (!p->have_timing || f->value[BD_TIMING] < p->timing_low)) {
        p->timing_low = f->value[BD_TIMING];
        p->have_timing = 1;
    }
    if (boost_ok && boost >= kAfrGatePsi && bd_is_fresh(f, BD_AFR, fe))
        raise_max(&p->have_afr, &p->afr_lean, f->value[BD_AFR]);
    if (boost_ok && x && m4_extra_fresh(x, MX_BOOST_TARGET, xe) &&
        x->value[MX_BOOST_TARGET] >= kDeficitTargetPsi &&
        x->value[MX_BOOST_TARGET] - boost >= kDeficitPsi) {
        if (!p->deficit_since) p->deficit_since = now ? now : 1;
        else if (now - p->deficit_since >= kDeficitMs)
            raise_max(&p->have_deficit, &p->deficit, x->value[MX_BOOST_TARGET] - boost);
    } else if (boost_ok) p->deficit_since = 0;
}

static int above(double v, double caution, double warning) {
    return v >= warning ? M4_LEVEL_WARNING : v >= caution ? M4_LEVEL_CAUTION : M4_LEVEL_OK;
}

int m4_gauge_level(unsigned slot, const bd_frame *f, uint64_t fe, const m4_extra *x, uint64_t xe,
                   const m4_peaks *p, uint64_t now) {
    double v;
    if (!f || !bd_is_fresh(f, slot, fe)) return M4_LEVEL_OK;
    v = f->value[slot];
    switch (slot) {
    case BD_BOOST:
        /* Only a sustained shortfall: boost always trails the target while the turbos spool. */
        if (!p || !p->deficit_since || now - p->deficit_since < kDeficitMs ||
            !x || !m4_extra_fresh(x, MX_BOOST_TARGET, xe)) return M4_LEVEL_OK;
        return above(x->value[MX_BOOST_TARGET] - v, kDeficitPsi, kDeficitWarningPsi);
    case BD_AFR:
        if (!bd_is_fresh(f, BD_BOOST, fe) || f->value[BD_BOOST] < kAfrGatePsi) return M4_LEVEL_OK;
        return v > kLeanAfr ? M4_LEVEL_WARNING : v > kCautionAfr ? M4_LEVEL_CAUTION : M4_LEVEL_OK;
    case BD_IAT: return above(v, kWarmishIntake, kHotIntake);
    case BD_TRANS: return above(v, kWarmishOil, kHotOil);      /* oil temperature in the M4 build */
    case BD_COOLANT: return above(v, kWarmishCoolant, kHotCoolant);
    default: return M4_LEVEL_OK;
    }
}

static void add_issue(char *out, size_t len, int *count, const char *text) {
    size_t used = strlen(out);
    if (used + 1 >= len) return;
    snprintf(out + used, len - used, "%s%s", *count ? ", " : "Check: ", text);
    ++*count;
}

int m4_health(const bd_frame *f, uint64_t fe, const m4_extra *x, uint64_t xe,
              const m4_peaks *p, char *out, size_t len) {
    static const char *const tires[] = {"FL", "FR", "RL", "RR"};
    int count = 0, seen = 0;
    unsigned i;
    char text[48];
    if (!out || !len) return 0;
    out[0] = 0;
    if (f && bd_is_fresh(f, BD_TRANS, fe)) {
        seen = 1;
        if (f->value[BD_TRANS] >= kHotOil) add_issue(out, len, &count, "oil hot");
    }
    if (f && bd_is_fresh(f, BD_COOLANT, fe)) {
        seen = 1;
        if (f->value[BD_COOLANT] >= kHotCoolant) add_issue(out, len, &count, "coolant hot");
    }
    if (f && bd_is_fresh(f, BD_IAT, fe)) {
        seen = 1;
        if (f->value[BD_IAT] >= kHotIntake) add_issue(out, len, &count, "intake air hot");
    }
    if (x && m4_extra_fresh(x, MX_BATTERY, xe)) {
        seen = 1;
        if (x->value[MX_BATTERY] < kBatteryLow) add_issue(out, len, &count, "battery low");
        else if (x->value[MX_BATTERY] > kBatteryHigh) add_issue(out, len, &count, "charging voltage high");
    }
    for (i = 0; x && i < 4; ++i) {
        int have_set;
        double pressure;
        if (!m4_extra_fresh(x, MX_TP_FL + i, xe)) continue;
        seen = 1;
        pressure = x->value[MX_TP_FL + i];
        have_set = m4_extra_fresh(x, MX_TS_FL + i, xe);
        if ((have_set && pressure < x->value[MX_TS_FL + i] - kTireLowMargin) ||
            (!have_set && pressure < kTireLowAbsolute)) {
            snprintf(text, sizeof text, "%s tire low", tires[i]);
            add_issue(out, len, &count, text);
        }
    }
    if (p) {
        if (p->have_oil && p->oil >= kHotOil) add_issue(out, len, &count, "oil peaked hot");
        if (p->have_coolant && p->coolant >= kHotCoolant) add_issue(out, len, &count, "coolant peaked hot");
        if (p->have_deficit) {
            snprintf(text, sizeof text, "boost %.1f psi under target", p->deficit);
            add_issue(out, len, &count, text);
        }
        if (p->have_afr && p->afr_lean > kLeanAfr) {
            snprintf(text, sizeof text, "lean under boost (AFR %.1f)", p->afr_lean);
            add_issue(out, len, &count, text);
        }
    }
    if (!count) snprintf(out, len, "%s", seen ? "OK" : "Waiting for data");
    return count;
}
