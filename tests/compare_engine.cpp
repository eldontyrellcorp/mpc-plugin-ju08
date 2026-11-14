// Loads the native Ju-08 plugin and checks every program against the engine driven directly.
#include "mpc_vst2_abi.h"
#include "engine.h"
#include <dlfcn.h>
#include <cstdio>
#include <cstring>
#include <cmath>
#include <vector>
#include <array>
#include <chrono>
using namespace j8;
static VstTimeInfo ti;
static VstIntPtr host(AEffect*, VstInt32 op, VstInt32, VstIntPtr, void*, float) {
    if (op == audioMasterVersion) return 2400;
    if (op == audioMasterGetTime) { memset(&ti, 0, sizeof(ti)); ti.sampleRate = 44100; ti.tempo = 120; ti.flags = kVstTempoValid; return (VstIntPtr)&ti; }
    return 0;
}
struct Ev { VstEvents head; VstEvent* more[16]; VstMidiEvent m[16]; };
static void send(AEffect* e, std::vector<std::array<unsigned char,3>> msgs) {
    static Ev ev; memset(&ev, 0, sizeof(ev));
    ev.head.numEvents = (int)msgs.size();
    VstEvent** arr = ev.head.events;
    for (size_t i = 0; i < msgs.size(); i++) {
        ev.m[i].type = kVstMidiType; ev.m[i].byteSize = sizeof(VstMidiEvent);
        ev.m[i].midiData[0] = (char)msgs[i][0]; ev.m[i].midiData[1] = (char)msgs[i][1]; ev.m[i].midiData[2] = (char)msgs[i][2];
        arr[i] = (VstEvent*)&ev.m[i];
    }
    e->dispatcher(e, effProcessEvents, 0, 0, &ev, 0);
}
int main(int argc, char** argv) {
    void* lib = dlopen(argv[1], RTLD_NOW);
    if (!lib) { printf("dlopen: %s\n", dlerror()); return 1; }
    auto entry = (AEffect* (*)(audioMasterCallback))dlsym(lib, "VSTPluginMain");
    static Patch bank[64]; applyFactory(bank, 64);
    const int chord[8] = {36, 43, 48, 52, 55, 60, 64, 67};
    const int N = 44100 * 3 / 128;
    double worstDiff = 0, worstCpu = 0; int worstCpuI = 0, silent = 0, bad = 0;
    for (int pi = 0; pi < 64; pi++) {
        AEffect* e = entry(host);
        e->dispatcher(e, effSetSampleRate, 0, 0, nullptr, 44100.f);
        e->dispatcher(e, effSetBlockSize, 0, 128, nullptr, 0);
        e->dispatcher(e, effOpen, 0, 0, nullptr, 0);
        e->dispatcher(e, effSetProgram, 0, pi, nullptr, 0);
        e->dispatcher(e, effMainsChanged, 0, 1, nullptr, 0);
        Engine* r = new Engine(); r->init(44100);
        const int mfx = (int)(2.7f * 44100), dly = (int)(1.35f * 44100), rev = 36000;
        auto a = [](int n) { return (int16_t*)calloc((size_t)n, 2); };
        r->fx.init(44100, a(mfx), a(mfx), mfx, a(dly), a(dly), dly, a(rev), rev);
        r->global[G_KEYMODE] = KM_WHOLE; r->global[G_CH_U] = 0; r->global[G_CH_L] = 2; r->hostBpm = 120;
        r->patch[0] = bank[pi];
        r->pushEvent(Event{Event::AllOff, 0, 0, kAnyChannel});
        float L[128], R[128], rl[128], rr2[128]; float* outs[2] = {L, R};
        double diff = 0, peak = 0, cpu = 0;
        for (int b = 0; b < N; b++) {
            std::vector<std::array<unsigned char,3>> m;
            if (b == 4) for (int n : chord) { m.push_back(std::array<unsigned char,3>{0x90, (unsigned char)n, 100}); r->pushEvent(Event{Event::NoteOn, (uint8_t)n, 0, 0}); }
            if (b == N * 6 / 10) for (int n : chord) { m.push_back(std::array<unsigned char,3>{0x80, (unsigned char)n, 0}); r->pushEvent(Event{Event::NoteOff, (uint8_t)n, 0, 0}); }
            send(e, m);
            auto t0 = std::chrono::steady_clock::now();
            e->processReplacing(e, nullptr, outs, 128);
            double dt = std::chrono::duration<double>(std::chrono::steady_clock::now() - t0).count();
            if (b > 8 && dt > cpu) cpu = dt;
            for (int k = 0; k < 4; k++) {
                float u[kBlock] = {0}, l[kBlock] = {0};
                r->prepareBlock(); r->render(0, kNumVoices, u, l); r->fxWorker(); r->finish(u, l, rl + k * 32, rr2 + k * 32);
            }
            for (int i = 0; i < 128; i++) {
                if (!std::isfinite(L[i]) || !std::isfinite(R[i])) bad++;
                diff = std::fmax(diff, std::fmax(std::fabs(L[i] - rl[i]), std::fabs(R[i] - rr2[i])));
                peak = std::fmax(peak, std::fmax(std::fabs(L[i]), std::fabs(R[i])));
            }
        }
        char name[64] = {0}; e->dispatcher(e, effGetProgramNameIndexed, pi, 0, name, 0);
        double pct = cpu / (128.0 / 44100.0) * 100.0;
        if (peak < 1e-3) silent++;
        if (diff > worstDiff) worstDiff = diff;
        if (pct > worstCpu) { worstCpu = pct; worstCpuI = pi; }
        if (argc > 2) printf("%-18s peak %.3f  maxdiff %.2e  worst block %.2f%%\n", name, peak, diff, pct);
        e->dispatcher(e, effMainsChanged, 0, 0, nullptr, 0);
        e->dispatcher(e, effClose, 0, 0, nullptr, 0);
        delete r;
    }
    printf("64 programs: worst difference to the engine %.2e, silent %d, non-finite samples %d, worst block %.2f%% of real time (program %d)\n", worstDiff, silent, bad, worstCpu, worstCpuI);
    return 0;
}
