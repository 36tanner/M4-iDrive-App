/* M4 / S55 additions. Payloads marked OBDb are recorded responses from a 2015 F82 M4 in the OBDb
 * BMW-M4 repository (CC BY-SA 4.0); the rest are synthetic. */
#include "gauge_core.h"
#include "m4_signals.h"
#include "perf_timer.h"
#include <assert.h>
#include <math.h>
#include <stdio.h>
#include <string.h>

static int near(double a, double b, double tolerance) { return fabs(a - b) <= tolerance; }

static void check_did(unsigned did, const unsigned char *p, size_t n, double expected) {
    double x = -999;
    assert(bd_decode_did(did, p, n, &x));
    assert(near(x, expected, .0001));
}

static void test_decoders(void) {
    /* OBDb: 4300 one byte on pre-2019 DMEs, 4408 oil, 4AB0 boost set point, 4A2E rev limit. */
    const unsigned char coolant_a[] = {0x9d}, coolant_b[] = {0xb6}, coolant_none[] = {0xff};
    const unsigned char oil[] = {0x03, 0xce}, target[] = {0x73, 0x36}, rev[] = {0x1b, 0x89};
    const unsigned char battery[] = {0x01,0x0e,0x74,0xb4,0x0a,0x3c,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0xff,0xff};
    const unsigned char tire_fl[] = {0x05,0x26,0x03,0x39,0x00,0x02,0x08,0x17,0x1b,0x08,0x6f,0x8d,0x63,0x00};
    const unsigned char tire_rl[] = {0x08,0x35,0x03,0x39,0x02,0x6c,0x07,0x09,0xd0,0x07,0x15,0xc9,0x63,0x00};
    const unsigned char rail[] = {0x01, 0x90};
    const unsigned char coolant16[] = {0x0b, 0xf8};
    double x, set, temp;
    check_did(0x4300, coolant_a, 1, 69.75);
    check_did(0x4300, coolant_b, 1, 88.5);
    assert(!bd_decode_did(0x4300, coolant_none, 1, &x));
    check_did(0x4300, coolant16, 2, 3064 / 10.0 - 273.15);   /* the 16-bit form still decodes */
    check_did(0x4408, oil, 2, 97.4);
    check_did(0x4ab0, target, 2, 115.2109375);
    assert(m4_decode_rev_limit(rev, 2, &x) && near(x, 7049, 0));
    assert(m4_decode_battery(battery, sizeof battery, &x) && near(x, 13.469, .0005));
    assert(!m4_decode_battery(battery, 5, &x));
    x = set = temp = 0;
    assert(m4_decode_tire(tire_fl, sizeof tire_fl, &x, &set, &temp) == 7);
    assert(near(x, 2.05, 1e-9) && near(set, 2.075, 1e-9) && near(temp, 23, 0));
    assert(m4_decode_tire(tire_rl, sizeof tire_rl, &x, &set, &temp) == 7);
    assert(near(x, 1.9, 1e-9) && near(set, 2.0, 1e-9) && near(temp, 9, 0));
    assert(!m4_decode_tire(tire_rl, 9, &x, &set, &temp));
    assert(m4_decode_pid(0x23, rail, 2, &x) && near(x, 40, 1e-9));
    assert(!m4_decode_pid(0x23, rail, 1, &x));
    assert(!m4_decode_pid(0x0c, rail, 2, &x));
}

static void test_extras_wire(void) {
    m4_extra a, b;
    char packet[BD_MAX_PACKET];
    int n;
    memset(&a, 0, sizeof a);
    a.seq = 7; a.valid = (1u << MX_BATTERY) | (1u << MX_TP_RR); a.sent_ms = 123456789ULL;
    a.value[MX_BATTERY] = 13.47; a.value[MX_TP_RR] = 2.05; a.age_ms[MX_TP_RR] = 4000;
    n = m4_extra_encode(&a, packet, sizeof packet);
    assert(n > 0);
    assert(m4_extra_parse(packet, (size_t)n, &b));
    assert(b.seq == 7 && b.valid == a.valid && b.sent_ms == a.sent_ms);
    assert(near(b.value[MX_BATTERY], 13.47, 1e-4) && b.age_ms[MX_TP_RR] == 4000);
    assert(m4_extra_fresh(&b, MX_BATTERY, 0) && !m4_extra_fresh(&b, MX_BOOST_TARGET, 0));
    assert(m4_extra_fresh(&b, MX_TP_RR, 20000) && !m4_extra_fresh(&b, MX_TP_RR, 21000));
    /* A gauge frame is not an extras frame and the reverse. */
    assert(!m4_extra_parse("NBG4 1 0 0 1", 12, &b));
    { bd_frame g; assert(!bd_parse(packet, (size_t)n, &g)); }
    packet[n - 2] = 'x';
    assert(!m4_extra_parse(packet, (size_t)n, &b));
}

static bd_frame frame_with(unsigned slot, double v) {
    bd_frame f;
    memset(&f, 0, sizeof f);
    f.valid = 1u << slot;
    f.value[slot] = v;
    return f;
}

static void set(bd_frame *f, unsigned slot, double v) { f->valid |= 1u << slot; f->value[slot] = v; }

static void test_warmup_levels_health(void) {
    bd_frame f;
    m4_extra x;
    m4_peaks p;
    char text[128];
    assert(m4_warmup(0, 0, 0, 0) == M4_WARM_UNKNOWN);
    assert(m4_warmup(1, 30, 1, 40) == M4_WARM_COLD);
    assert(m4_warmup(1, 60, 1, 80) == M4_WARM_WARMING);
    assert(m4_warmup(1, 80, 1, 70) == M4_WARM_WARMING);   /* coolant still cold */
    assert(m4_warmup(1, 80, 1, 85) == M4_WARM_GO);
    assert(m4_warmup(1, 80, 0, 0) == M4_WARM_GO);
    assert(m4_warmup(1, 136, 1, 90) == M4_WARM_HOT && m4_warmup(1, 90, 1, 116) == M4_WARM_HOT);
    assert(m4_warmup_step(1, 20, 20) == 0 && m4_warmup_step(1, 75, 20) == 20 && m4_warmup_step(1, 47.5, 20) == 10);
    assert(!strcmp(m4_warmup_word(M4_WARM_GO), "GO"));

    f = frame_with(BD_TRANS, 110);
    assert(m4_gauge_level(BD_TRANS, &f, 0, 0, 0, 0, 0) == M4_LEVEL_OK);
    f.value[BD_TRANS] = 125;
    assert(m4_gauge_level(BD_TRANS, &f, 0, 0, 0, 0, 0) == M4_LEVEL_CAUTION);
    f.value[BD_TRANS] = 140;
    assert(m4_gauge_level(BD_TRANS, &f, 0, 0, 0, 0, 0) == M4_LEVEL_WARNING);
    assert(m4_gauge_level(BD_TRANS, &f, 4000, 0, 0, 0, 0) == M4_LEVEL_OK);   /* stale: no color */
    f = frame_with(BD_AFR, 13.8);
    assert(m4_gauge_level(BD_AFR, &f, 0, 0, 0, 0, 0) == M4_LEVEL_OK);        /* not under boost */
    set(&f, BD_BOOST, 12);
    assert(m4_gauge_level(BD_AFR, &f, 0, 0, 0, 0, 0) == M4_LEVEL_WARNING);
    f.value[BD_AFR] = 13.2;
    assert(m4_gauge_level(BD_AFR, &f, 0, 0, 0, 0, 0) == M4_LEVEL_CAUTION);
    f.value[BD_AFR] = 11.8;
    assert(m4_gauge_level(BD_AFR, &f, 0, 0, 0, 0, 0) == M4_LEVEL_OK);
    f = frame_with(BD_IAT, 55);
    assert(m4_gauge_level(BD_IAT, &f, 0, 0, 0, 0, 0) == M4_LEVEL_CAUTION);
    f = frame_with(BD_RAIL, 5);
    assert(m4_gauge_level(BD_RAIL, &f, 0, 0, 0, 0, 0) == M4_LEVEL_OK);       /* uncolored gauge */

    /* Boost under target: transient spool stays uncolored, a sustained shortfall turns yellow then red. */
    memset(&x, 0, sizeof x);
    x.valid = 1u << MX_BOOST_TARGET; x.value[MX_BOOST_TARGET] = 18;
    m4_peaks_reset(&p);
    f = frame_with(BD_BOOST, 14);
    m4_peaks_update(&p, &f, 0, &x, 0, 1000);
    assert(m4_gauge_level(BD_BOOST, &f, 0, &x, 0, &p, 1500) == M4_LEVEL_OK);
    m4_peaks_update(&p, &f, 0, &x, 0, 2100);
    assert(m4_gauge_level(BD_BOOST, &f, 0, &x, 0, &p, 2100) == M4_LEVEL_CAUTION);
    assert(p.have_deficit && near(p.deficit, 4, 1e-9));
    f.value[BD_BOOST] = 12;
    m4_peaks_update(&p, &f, 0, &x, 0, 2200);
    assert(m4_gauge_level(BD_BOOST, &f, 0, &x, 0, &p, 2200) == M4_LEVEL_WARNING);
    f.value[BD_BOOST] = 17.5;
    m4_peaks_update(&p, &f, 0, &x, 0, 2300);
    assert(!p.deficit_since && m4_gauge_level(BD_BOOST, &f, 0, &x, 0, &p, 2300) == M4_LEVEL_OK);
    assert(near(p.boost, 17.5, 1e-9) && near(p.deficit, 6, 1e-9));

    /* Peaks: boost-gated timing and AFR. */
    m4_peaks_reset(&p);
    f = frame_with(BD_BOOST, 2);
    set(&f, BD_TIMING, -3); set(&f, BD_AFR, 14.7);
    m4_peaks_update(&p, &f, 0, 0, 0, 10);
    assert(!p.have_timing && !p.have_afr);
    f.value[BD_BOOST] = 15; f.value[BD_TIMING] = 4.5; f.value[BD_AFR] = 11.9;
    m4_peaks_update(&p, &f, 0, 0, 0, 20);
    f.value[BD_TIMING] = 6; f.value[BD_AFR] = 11.5;
    m4_peaks_update(&p, &f, 0, 0, 0, 30);
    assert(near(p.timing_low, 4.5, 1e-9) && near(p.afr_lean, 11.9, 1e-9));

    /* Health line. */
    assert(m4_health(0, 0, 0, 0, 0, text, sizeof text) == 0 && !strcmp(text, "Waiting for data"));
    f = frame_with(BD_TRANS, 95);
    set(&f, BD_COOLANT, 92);
    memset(&x, 0, sizeof x);
    x.valid = (1u << MX_BATTERY) | (1u << MX_TP_FL) | (1u << MX_TS_FL) | (1u << MX_TP_RR);
    x.value[MX_BATTERY] = 14.1; x.value[MX_TP_FL] = 2.05; x.value[MX_TS_FL] = 2.08; x.value[MX_TP_RR] = 2.0;
    m4_peaks_reset(&p);
    assert(m4_health(&f, 0, &x, 0, &p, text, sizeof text) == 0 && !strcmp(text, "OK"));
    x.value[MX_BATTERY] = 11.7; x.value[MX_TP_FL] = 1.80;
    p.have_deficit = 1; p.deficit = 4.2;
    assert(m4_health(&f, 0, &x, 0, &p, text, sizeof text) == 3);
    assert(!strcmp(text, "Check: battery low, FL tire low, boost 4.2 psi under target"));
    assert(m4_health(&f, 0, &x, 0, &p, text, 20) >= 1 && strlen(text) < 20);
}

/* Constant acceleration from rest, sampled every period ms and rounded to whole km/h like OBD speed. */
static void drive(perf_timer *t, uint64_t *ms, double *kph, double accel_kph_s, double until_kph,
                  unsigned period) {
    while (*kph < until_kph) {
        *ms += period;
        *kph += accel_kph_s * period / 1000.0;
        perf_sample(t, *ms, (double)(long)(*kph + .5));
    }
}

static void test_timers(void) {
    perf_timer t;
    uint64_t ms = 100000;
    double kph = 0, a = 60 * 1.609344 / 4.0;   /* 0-60 in exactly 4.0 s */
    unsigned i;
    perf_reset(&t);
    assert(perf_state(&t) == PT_WAITING);
    for (i = 0; i < 10; ++i) perf_sample(&t, ms += 65, 0);
    assert(perf_state(&t) == PT_ARMED);
    drive(&t, &ms, &kph, a, 280, 65);   /* far enough for the quarter mile */
    assert(t.have[PT_0_60] && near(t.result[PT_0_60], 4.0, .1));
    assert(t.have[PT_0_100] && near(t.result[PT_0_100], 100 / a, .1));
    assert(t.have[PT_60_130] && near(t.result[PT_60_130], 70 * 1.609344 / a, .1));
    assert(t.have[PT_100_200] && near(t.result[PT_100_200], 100 / a, .1));
    {   /* d = v^2 / 2a */
        double a_ms2 = a / 3.6, et = sqrt(2 * 402.336 / a_ms2);
        assert(t.have[PT_QUARTER] && near(t.result[PT_QUARTER], et, .1));
        assert(near(t.trap_kph, a * et, 2));
    }
    assert(t.have_best[PT_0_60] && t.best[PT_0_60] == t.result[PT_0_60]);

    /* A slower second run keeps the best; a lift aborts the rolling 60-130. */
    {
        double best = t.best[PT_0_60];
        while (kph > 0) { kph -= 10; perf_sample(&t, ms += 65, kph > 0 ? kph : 0); }
        kph = 0;
        for (i = 0; i < 10; ++i) perf_sample(&t, ms += 65, 0);
        drive(&t, &ms, &kph, a / 2, 150, 65);
        assert(near(t.result[PT_0_60], 8.0, .15) && t.best[PT_0_60] == best);
        assert(perf_state(&t) == PT_RUNNING);
        perf_sample(&t, ms += 65, kph - 8);   /* lifted */
        assert(!t.rolling[0]);
    }
    /* Missing speed or a long gap aborts everything; nothing arms while moving. */
    perf_sample(&t, ms += 65, -1);
    assert(perf_state(&t) == PT_WAITING);
    perf_sample(&t, ms += 65, 50);
    perf_sample(&t, ms += 3000, 60);
    assert(perf_state(&t) == PT_WAITING);
    perf_reset(&t);
    assert(!t.have_best[PT_0_60]);
    assert(!strcmp(perf_name(PT_QUARTER, 0), "1/4 mile"));
}

int main(void) {
    test_decoders();
    test_extras_wire();
    test_warmup_levels_health();
    test_timers();
    puts("M4: S55 decoders (OBDb payloads), extras wire format, warm-up, colors, health, peaks and timers passed.");
    return 0;
}
