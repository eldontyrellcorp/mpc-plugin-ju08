// The performances' loudness, measured on the engine (as compare_engine drives it), against the 64 patches'.
//   perf_levels            checks: every performance written once, no control set twice, each sounds, each part sounds,
//                          the loudness within 4 dB of the patches' median, no clipping
//   perf_levels --table    works out the VCA LEVEL of each part (two rounds) and prints src/performance_levels.cpp
//
// The playing: a patch or a Dual performance, the chord C3 G3 C4 E4 held 2 s; a Split performance, four notes below
// the split point and four above. Loudness: RMS over the first second of the notes, with the effects (a pluck is
// judged by its attack, not by the silence after it).
#include "engine.h"
#include "performances.h"
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <vector>
using namespace j8;

static double play(const Patch& up, const Patch& lo, const uint8_t* g, bool stereo, const std::vector<int>& notes, double* peakOut = nullptr) {
    Engine* r = new Engine();
    r->init(44100);
    const int mfx = (int)(2.7f * 44100), dly = (int)(1.35f * 44100), rev = 36000;
    std::vector<int16_t*> mem;
    auto a = [&](int n) { int16_t* m = (int16_t*)calloc((size_t)n, 2); mem.push_back(m); return m; };
    r->fx.init(44100, a(mfx), a(mfx), mfx, a(dly), a(dly), dly, a(rev), rev);
    std::memcpy(r->global, g, G_COUNT);
    r->hostBpm = 120;
    r->stereoLayers = stereo;
    r->patch[0] = up;
    r->patch[1] = lo;
    r->pushEvent(Event{Event::AllOff, 0, 0, kAnyChannel});
    const int blocks = 44100 * 2 / kBlock, from = 2 + (int)(0.02 * 44100 / kBlock), to = 2 + (int)(1.0 * 44100 / kBlock);
    double s = 0, pk = 0;
    long n = 0;
    for (int b = 0; b < blocks; b++) {
        if (b == 2) for (int k : notes) r->pushEvent(Event{Event::NoteOn, (uint8_t)k, 0, 0});
        float u[kBlock] = {0}, l[kBlock] = {0}, L[kBlock], R[kBlock];
        r->prepareBlock();
        r->render(0, kNumVoices, u, l);
        r->fxWorker();
        r->finish(u, l, L, R);
        for (int i = 0; i < kBlock; i++) {
            pk = std::fmax(pk, std::fmax(std::fabs(L[i]), std::fabs(R[i])));
            if (b >= from && b < to) { s += 0.5 * ((double)L[i] * L[i] + (double)R[i] * R[i]); n++; }
        }
    }
    for (int16_t* m : mem) free(m);
    delete r;
    if (peakOut) *peakOut = pk;
    return std::sqrt(s / (double)(n ? n : 1));
}

static std::vector<int> notesFor(const uint8_t* g) {
    if (g[G_KEYMODE] == KM_SPLIT) {
        const int s = g[G_SPLIT];
        return {s - 24, s - 17, s - 12, s - 5, s, s + 4, s + 7, s + 12};
    }
    return {48, 55, 60, 64};
}

static double dB(double x) { return 20 * std::log10(x + 1e-12); }

int main(int argc, char** argv) {
    const bool table = argc > 1 && std::strcmp(argv[1], "--table") == 0;
    uint8_t defaults[G_COUNT];
    defaultGlobals(defaults);
    defaults[G_KEYMODE] = KM_WHOLE;
    defaults[G_CH_U] = 0;
    defaults[G_CH_L] = 2;
    int fails = 0;
    if (performanceCount() != kNumPerformances) { printf("FAIL  %d performances written, not %d\n", performanceCount(), kNumPerformances); fails++; }
    if (const char* d = performanceWithDuplicate()) { printf("FAIL  %s sets a control twice\n", d); fails++; }

    // the patches' loudness: their median is the target
    static Patch bank[64];
    applyFactory(bank, 64);
    Patch init;
    initPatch(init, "INIT");
    std::vector<double> ref;
    for (int i = 0; i < 64; i++) ref.push_back(play(bank[i], init, defaults, false, {48, 55, 60, 64}));
    std::vector<double> sorted = ref;
    std::sort(sorted.begin(), sorted.end());
    const double target = sorted[32];
    printf("INFO  the 64 patches: median %.1f dB (from %.1f to %.1f)\n", dB(target), dB(sorted[0]), dB(sorted[63]));

    static Performance perf[kNumPerformances];
    buildPerformances(perf, defaults);
    if (table) {
        // each part alone (the other one's level at 0) to its share of the target. The loudness goes with the square
        // of VCA LEVEL (40 to 220: 29 dB), hence the square root; three rounds for the effects and the saturation.
        for (int round = 0; round < 3; round++)
            for (int i = 0; i < kNumPerformances; i++) {
                Performance& p = perf[i];
                const double share = p.global[G_KEYMODE] == KM_DUAL ? 0.72 : 0.95;
                for (int part = 0; part < 2; part++) {
                    uint8_t g[G_COUNT];
                    std::memcpy(g, p.global, G_COUNT);
                    g[part == 0 ? G_BAL_L : G_BAL_U] = 0;
                    const double rms = play(p.upper, p.lower, g, p.stereoLayers, notesFor(p.global));
                    uint8_t& lv = (part == 0 ? p.upper : p.lower).v[P_VCA_LEVEL];
                    const double want = (double)lv * std::sqrt(target * share / std::fmax(rms, 1e-6));
                    lv = (uint8_t)std::lround(std::fmin(255.0, std::fmax(40.0, want)));
                }
            }
        printf("// generated by tests/perf_levels.cpp --table: VCA LEVEL of the Upper and Lower patch of each performance\n");
        printf("#include \"performances.h\"\n\nnamespace j8 {\n\nconst uint8_t kPerformanceLevel[kNumPerformances][2] = {\n");
        for (int i = 0; i < kNumPerformances; i++)
            printf("    {%d, %d},   // P%02d %s\n", perf[i].upper.v[P_VCA_LEVEL], perf[i].lower.v[P_VCA_LEVEL], i + 1, perf[i].name);
        printf("};\n\n}  // namespace j8\n");
        return 0;
    }

    int quiet = 0, loud = 0, clip = 0, mute = 0;
    for (int i = 0; i < kNumPerformances; i++) {
        const Performance& p = perf[i];
        double pk = 0;
        const double rms = play(p.upper, p.lower, p.global, p.stereoLayers, notesFor(p.global), &pk);
        const double off = dB(rms) - dB(target);
        for (int part = 0; part < 2; part++) {
            uint8_t g[G_COUNT];
            std::memcpy(g, p.global, G_COUNT);
            g[part == 0 ? G_BAL_L : G_BAL_U] = 0;
            if (dB(play(p.upper, p.lower, g, p.stereoLayers, notesFor(p.global))) - dB(target) < -18) {
                mute++;
                printf("FAIL  P%02d %s: the %s part is nearly silent\n", i + 1, p.name, part ? "Lower" : "Upper");
            }
        }
        if (off < -4) { quiet++; printf("FAIL  P%02d %s: %.1f dB under the patches' median\n", i + 1, p.name, -off); }
        if (off > 4) { loud++; printf("FAIL  P%02d %s: %.1f dB over the patches' median\n", i + 1, p.name, off); }
        if (pk > 0.99) { clip++; printf("FAIL  P%02d %s: peak %.2f\n", i + 1, p.name, pk); }
        if (argc > 1) printf("P%02d %-18s %+5.1f dB  peak %.2f\n", i + 1, p.name, off, pk);
    }
    fails += quiet + loud + clip + mute;
    printf("%s  100 performances: within 4 dB of the patches' median loudness, both parts sound, no clipping\n", fails ? "FAIL" : "ok  ");
    return fails ? 1 : 0;
}
