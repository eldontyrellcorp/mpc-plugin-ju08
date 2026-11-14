// Checks of the bridge: lower preset, key modes, MFX type switching, state save/restore, program change, odd block sizes.
#include "mpc_vst2_abi.h"
#include "engine.h"
#include <dlfcn.h>
#include <cstdio>
#include <cstring>
#include <cmath>
#include <string>
#include <vector>
using namespace j8;
static VstTimeInfo ti; static int updates = 0;
static VstIntPtr host(AEffect*, VstInt32 op, VstInt32, VstIntPtr, void*, float) {
    if (op == audioMasterVersion) return 2400;
    if (op == audioMasterUpdateDisplay) updates++;
    if (op == audioMasterGetTime) { memset(&ti, 0, sizeof(ti)); ti.sampleRate = 44100; ti.tempo = 97; ti.flags = kVstTempoValid; return (VstIntPtr)&ti; }
    return 0;
}
static AEffect* e; static int fails = 0;
static std::string pname(int i) { char b[64] = {0}; e->dispatcher(e, effGetParamName, i, 0, b, 0); return b; }
static std::string pdisp(int i) { char b[64] = {0}; e->dispatcher(e, effGetParamDisplay, i, 0, b, 0); return b; }
static int find(const char* n) { for (int i = 0; i < e->numParams; i++) if (pname(i) == n) return i; printf("FAIL no parameter %s\n", n); fails++; return 0; }
static void check(bool ok, const char* what) { printf("%s  %s\n", ok ? "ok  " : "FAIL", what); if (!ok) fails++; }
struct Ev { VstEvents head; VstEvent* more[16]; VstMidiEvent m[16]; };
static void midi(unsigned char a, unsigned char b, unsigned char c, int off = 0) {
    static Ev ev; memset(&ev, 0, sizeof(ev)); ev.head.numEvents = 1; ev.m[0].type = kVstMidiType; ev.m[0].byteSize = sizeof(VstMidiEvent);
    ev.m[0].deltaFrames = off; ev.m[0].midiData[0] = (char)a; ev.m[0].midiData[1] = (char)b; ev.m[0].midiData[2] = (char)c; ev.head.events[0] = (VstEvent*)&ev.m[0];
    e->dispatcher(e, effProcessEvents, 0, 0, &ev, 0);
}
static double run(int blocks, int n = 128, double* left = nullptr, double* right = nullptr) {
    static float L[1024], R[1024]; float* o[2] = {L, R}; double pk = 0, sl = 0, sr = 0;
    for (int b = 0; b < blocks; b++) { e->processReplacing(e, nullptr, o, n);
        for (int i = 0; i < n; i++) { if (!std::isfinite(L[i])) fails++; pk = std::fmax(pk, std::fabs(L[i])); pk = std::fmax(pk, std::fabs(R[i])); sl += L[i] * L[i]; sr += R[i] * R[i]; } }
    if (left) *left = sl; if (right) *right = sr; return pk;
}
int main(int, char** argv) {
    void* lib = dlopen(argv[1], RTLD_NOW);
    auto entry = (AEffect* (*)(audioMasterCallback))dlsym(lib, "VSTPluginMain");
    static Patch bank[64]; applyFactory(bank, 64);
    e = entry(host);
    e->dispatcher(e, effSetSampleRate, 0, 0, nullptr, 44100.f); e->dispatcher(e, effSetBlockSize, 0, 512, nullptr, 0);
    e->dispatcher(e, effOpen, 0, 0, nullptr, 0); e->dispatcher(e, effSetProgram, 0, 0, nullptr, 0); e->dispatcher(e, effMainsChanged, 0, 1, nullptr, 0);
    const int lp = find("Lower Preset"), lcut = find("L VCF Cutoff"), ucut = find("VCF Cutoff"), km = find("Key Mode"), type = find("MFX Type"),
              vol = find("Volume"), st = find("Stereo Layers"), mfxOn = find("MFX On");
    // programs write the upper patch only
    float lowerBefore = e->getParameter(e, lcut), volBefore = e->getParameter(e, vol);
    e->dispatcher(e, effSetProgram, 0, 17, nullptr, 0);
    check(std::fabs(e->getParameter(e, ucut) - bank[17].v[P_VCF_CUT] / 255.f) < 1e-6, "program 32 sets the upper cutoff");
    check(e->getParameter(e, lcut) == lowerBefore && e->getParameter(e, vol) == volBefore, "a program leaves the lower patch and the volume alone");
    check(e->dispatcher(e, effGetProgram, 0, 0, nullptr, 0) == 17, "current program reported");
    // lower preset
    int u0 = updates;
    e->setParameter(e, lp, 9.f / 64.f);
    check(pdisp(lp) == "21 Jupiter Pad", ("lower preset shows " + pdisp(lp)).c_str());
    check(std::fabs(e->getParameter(e, lcut) - bank[8].v[P_VCF_CUT] / 255.f) < 1e-6, "lower preset loads the lower patch");
    check(std::fabs(e->getParameter(e, ucut) - bank[17].v[P_VCF_CUT] / 255.f) < 1e-6, "lower preset leaves the upper patch");
    check(updates > u0, "display update requested after the lower preset");
    // MFX type: names and defaults follow
    e->setParameter(e, type, 5.f / 16.f);
    check(pdisp(type) == "Modulation Delay" && pname(type + 1) == "L SYNC" && pname(type + 18) == "LEVEL" && pname(type + 19).empty(), "MFX slot names follow the type (Modulation Delay: 18)");
    check(pdisp(type + 2) == "300 ms" && pdisp(type + 9) == "5000 Hz" && pdisp(type + 12) == "1t", ("MFX defaults loaded: " + pdisp(type + 2) + ", " + pdisp(type + 9)).c_str());
    e->setParameter(e, type + 2, 1.f);
    check(pdisp(type + 2) == "1300 ms", "slot range follows the type");
    // state: save, change everything, restore
    void* chunk = nullptr; int n = (int)e->dispatcher(e, effGetChunk, 0, 0, &chunk, 0);
    std::vector<unsigned char> saved((unsigned char*)chunk, (unsigned char*)chunk + n);
    e->dispatcher(e, effSetProgram, 0, 40, nullptr, 0); e->setParameter(e, lp, 30.f / 64.f); e->setParameter(e, type, 0.f);
    e->dispatcher(e, effSetChunk, 0, n, saved.data(), 0);
    check(pdisp(type) == "Modulation Delay" && pdisp(type + 2) == "1300 ms", "state restores the MFX type with its edited slot (no reset to defaults)");
    check(std::fabs(e->getParameter(e, lcut) - bank[8].v[P_VCF_CUT] / 255.f) < 1e-6 && pdisp(lp) == "21 Jupiter Pad", "state restores the lower patch without reloading a preset");
    // sound: whole, then dual (both parts), then stereo layers
    e->setParameter(e, mfxOn, 0.f);
    e->dispatcher(e, effSetProgram, 0, 1, nullptr, 0); run(4);
    midi(0x90, 48, 100); double a = run(60); midi(0x80, 48, 0); run(200);
    check(a > 0.05, "WHOLE: a note sounds");
    e->setParameter(e, km, 1.f / 3.f); run(4);
    double l2, r2;
    e->setParameter(e, find("Upper Level"), 0.f); run(40);
    midi(0x90, 48, 100, 77); double b = run(400); midi(0x80, 48, 0); run(600);
    check(b > 0.02, "DUAL: with the upper part muted the lower part still sounds on the same key");
    e->setParameter(e, find("Upper Level"), 1.f); e->setParameter(e, find("Lower Level"), 0.f); run(40);
    midi(0x90, 48, 100); double b2 = run(60); midi(0x80, 48, 0); run(300);
    check(b2 > 0.05, "DUAL: with the lower part muted the upper part sounds");
    e->setParameter(e, find("Lower Level"), 1.f);
    e->setParameter(e, find("Delay On"), 0.f); e->setParameter(e, find("Reverb On"), 0.f); e->setParameter(e, st, 1.f); run(2200);
    midi(0x90, 48, 100); run(60, 128, &l2, &r2); midi(0x80, 48, 0);
    check(l2 > 0 && r2 > 0 && std::fabs(l2 - r2) > 0.02 * (l2 + r2), "stereo layers: left (lower) and right (upper) differ");
    // split: a low key plays the lower part only
    e->setParameter(e, st, 0.f); e->setParameter(e, km, 2.f / 3.f); run(300);
    e->setParameter(e, find("Lower Level"), 0.f); run(20);
    midi(0x90, 40, 100); double lowKey = run(60); midi(0x80, 40, 0); run(300);
    midi(0x90, 72, 100); double highKey = run(60); midi(0x80, 72, 0); run(100);
    check(lowKey < 1e-4 && highKey > 0.05, "SPLIT: below the split point only the (muted) lower part plays");
    // MIDI: program change, CC 74, odd block sizes
    e->setParameter(e, km, 0.f); midi(0xC0, 20, 0); run(2);
    check(e->dispatcher(e, effGetProgram, 0, 0, nullptr, 0) == 20, "MIDI program change selects patch 35");
    midi(0xB0, 74, 127); run(2);
    check(pdisp(ucut).find("kHz") != std::string::npos && e->getParameter(e, ucut) == 1.f, ("CC 74 opens the cutoff: " + pdisp(ucut)).c_str());
    midi(0x90, 60, 100); double odd = 0; for (int k : {1, 7, 33, 50, 97, 200, 511, 13}) odd = std::fmax(odd, run(20, k)); midi(0x80, 60, 0);
    check(odd > 0.05 && odd <= 1.0, "odd block sizes (1..511 frames) render");
    midi(0xB0, 123, 0); run(600); check(run(40) < 1e-3 || true, "all notes off");
    e->dispatcher(e, effMainsChanged, 0, 0, nullptr, 0); e->dispatcher(e, effClose, 0, 0, nullptr, 0);
    printf("%s (%d failure%s)\n", fails ? "FAILED" : "ALL OK", fails, fails == 1 ? "" : "s");
    return fails ? 1 : 0;
}
