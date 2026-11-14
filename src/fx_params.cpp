#include "fx_params.h"
#include <cstdio>
#include <cstring>

namespace j8 {

static const char* const kNotes[] = {"64t","1/64","32t","1/32","16t","1/32.","1/16","1/8t","1/16.","1/8","1/4t","1/8.",
                                     "1/4","1/2t","1/4.","1/2","1t","1/2.","1","2t","1.","2"};
static const float kBeats[] = {0.0416667f,0.0625f,0.0833333f,0.125f,0.1666667f,0.1875f,0.25f,0.3333333f,0.375f,0.5f,0.6666667f,0.75f,
                               1.f,1.3333333f,1.5f,2.f,2.6666667f,3.f,4.f,5.3333333f,6.f,8.f};
float noteBeats(int i) { return kBeats[i < 0 ? 0 : (i > 21 ? 21 : i)]; }

static const char* const kOff[] = {"OFF", "ON"};
static const char* const kDamp[] = {"200 Hz","250 Hz","315 Hz","400 Hz","500 Hz","630 Hz","800 Hz","1000 Hz","1250 Hz","1600 Hz",
                                    "2000 Hz","2500 Hz","3150 Hz","4000 Hz","5000 Hz","6300 Hz","8000 Hz","BYPASS"};
static const char* const kC1Mode[] = {"I", "II", "I+II", "JX 1", "JX 2"};
static const char* const kC3Mode[] = {"1", "2", "3", "4", "1+4", "2+4", "3+4"};
static const char* const kFbMode[] = {"NORMAL", "CROSS"};
static const char* const kPost[] = {"OFF", "LPF", "HPF"};
static const char* const kStages[] = {"4", "8", "12", "16", "20", "24"};
static const char* const kFType[] = {"LPF", "BPF", "HPF", "NOTCH"};
static const char* const kFSlope[] = {"GENTLE", "STEEP", "EXTREME"};
static const char* const kFWave[] = {"TRI", "SQR", "SIN", "SAW UP", "SAW DOWN"};
static const char* const kDlMode[] = {"2 TAP", "PING-PONG"};
static const char* const kRvMode[] = {"STANDARD", "SHIMMER"};
static const char* const kRvType[] = {"ROOM 1", "ROOM 2", "HALL 1", "HALL 2", "PLATE"};

#define N(n, lo, hi, d) {n, lo, hi, d, FxKind::Num, nullptr}
#define K(n, lo, hi, d, kind) {n, lo, hi, d, FxKind::kind, nullptr}
#define L(n, arr, d) {n, 0, (int16_t)(sizeof(arr) / sizeof(arr[0]) - 1), d, FxKind::Labels, arr}
#define SW(n, d) {n, 0, 1, d, FxKind::OnOff, kOff}
#define LO K("EQ LOW", -15, 15, 0, Db)
#define HI K("EQ HIGH", -15, 15, 0, Db)
#define BAL(d) K("BALANCE", 0, 100, d, Pct)
#define LEV N("LEVEL", 0, 127, 100)
#define FB(d) K("FEEDBACK", -98, 98, d, Pct)
#define NOTE(d) L("NOTE", kNotes, d)

static const FxParamDef kC1[] = {L("MODE", kC1Mode, 0), N("NOISE", 0, 127, 15), BAL(50), LEV};
static const FxParamDef kC2[] = {N("INTENSITY", 0, 127, 100), LO, HI, LEV};
static const FxParamDef kC3[] = {L("MODE", kC3Mode, 1), LO, HI, LEV};
static const FxParamDef kD1[] = {SW("SYNC", 0), K("TIME", 1, 1300, 400, Ms), NOTE(12), FB(35), BAL(30), LO, HI, LEV};
static const FxParamDef kD2[] = {SW("SYNC", 0), K("TIME", 1, 2600, 600, Ms), NOTE(12), FB(35), L("HIGH DAMP", kDamp, 13),
    K("PAN 1", -64, 63, -64, Pan), K("PAN 2", -64, 63, 63, Pan), N("LEVEL 1", 0, 127, 127), N("LEVEL 2", 0, 127, 127), LO, HI, BAL(30), LEV};
static const FxParamDef kD3[] = {SW("L SYNC", 0), K("L TIME", 1, 1300, 300, Ms), NOTE(12), SW("R SYNC", 0), K("R TIME", 1, 1300, 450, Ms), NOTE(12),
    L("FB MODE", kFbMode, 1), FB(35), L("HIGH DAMP", kDamp, 14), SW("MOD SYNC", 0), K("MOD RATE", 5, 1000, 50, Hz100), NOTE(16),
    N("MOD DEPTH", 0, 127, 40), K("MOD PHASE", 0, 180, 180, Deg), LO, HI, BAL(30), LEV};
static const FxParamDef kRD[] = {SW("SYNC", 0), K("TIME", 1, 1300, 500, Ms), NOTE(12), FB(30), BAL(40), LO, HI, LEV};
static const FxParamDef kOD[] = {N("DISTORTION", 0, 127, 60), N("TONE", 0, 127, 70), LEV};
static const FxParamDef kFZ[] = {N("DRIVE", 0, 127, 90), N("TONE", 0, 100, 55), LEV};
static const FxParamDef kFT[] = {K("ODD", 0, 400, 100, Pct), K("EVEN", 0, 400, 100, Pct), LEV};
static const FxParamDef kBC[] = {N("RATE", 0, 127, 40), N("BITS", 0, 20, 8), N("FILTER", 0, 127, 30), LO, HI, LEV};
static const FxParamDef kLF[] = {N("COMPRESSOR", 1, 6, 3), N("LOFI", 1, 9, 4), L("POST FILTER", kPost, 0), N("POST FREQ", 1, 16, 10), LO, HI, BAL(100), LEV};
static const FxParamDef kP1[] = {N("SPEED", 0, 100, 40), N("DEPTH", 0, 127, 100), LO, HI, LEV};
static const FxParamDef kP2[] = {L("STAGES", kStages, 2), N("MANUAL", 0, 127, 64), SW("SYNC", 0), K("RATE", 5, 1000, 30, Hz100), NOTE(19),
    N("DEPTH", 0, 127, 90), N("RESONANCE", 0, 127, 70), N("MIX", 0, 127, 100), K("PAN", -64, 63, 0, Pan), LO, HI, LEV};
static const FxParamDef kSF[] = {L("TYPE", kFType, 0), L("SLOPE", kFSlope, 1), N("CUTOFF", 0, 127, 70), N("RESONANCE", 0, 100, 40), K("GAIN", 0, 12, 0, Db),
    SW("MOD", 1), L("WAVE", kFWave, 0), SW("SYNC", 0), K("RATE", 5, 1000, 50, Hz100), NOTE(12), N("DEPTH", 0, 127, 70), N("ATTACK", 0, 127, 60), LEV};
static const FxParamDef kPS[] = {K("COARSE", -24, 12, 7, Semi), K("FINE", -100, 100, 0, Cents), SW("SYNC", 0), K("TIME", 1, 1300, 20, Ms), NOTE(9), FB(0), LO, HI, BAL(40), LEV};
static const FxParamDef kPV[] = {K("1 COARSE", -24, 12, -12, Semi), K("1 FINE", -100, 100, 0, Cents), SW("1 SYNC", 0), K("1 TIME", 1, 1300, 20, Ms), NOTE(9), FB(0), K("1 PAN", -64, 63, -40, Pan), N("1 LEVEL", 0, 127, 110),
    K("2 COARSE", -24, 12, 12, Semi), K("2 FINE", -100, 100, 0, Cents), SW("2 SYNC", 0), K("2 TIME", 1, 1300, 30, Ms), NOTE(9), FB(0), K("2 PAN", -64, 63, 40, Pan), N("2 LEVEL", 0, 127, 80), LO, HI, BAL(40), LEV};

namespace { struct Tab { const char* name; const FxParamDef* p; int n; }; }
#define T(nm, a) {nm, a, (int)(sizeof(a) / sizeof(a[0]))}
static const Tab kTabs[MFX_COUNT] = {
    T("JUNO-106 Chorus", kC1), T("CE-1", kC2), T("SDD-320", kC3), T("Time Ctrl Delay", kD1), T("2Tap Pan Delay", kD2),
    T("Modulation Delay", kD3), T("Reverse Delay", kRD), T("T-Scream", kOD), T("Fuzz", kFZ), T("Tone Fattener", kFT),
    T("Bit Crusher", kBC), T("LOFI Compress", kLF), T("Script 90", kP1), T("Multi Stage Phaser", kP2), T("Super Filter", kSF),
    T("Pitch Shifter", kPS), T("2Voice Pitch Shift", kPV),
};

static const FxParamDef kDelay[DL_COUNT] = {
    N("SEND", 0, 127, 60), L("MODE", kDlMode, DLM_PING), SW("SYNC", 0), K("TIME", 1, 1300, 300, Ms), NOTE(9), FB(45), L("HIGH DAMP", kDamp, 14),
    K("PAN 1", -64, 63, -64, Pan), K("PAN 2", -64, 63, 63, Pan), N("LEVEL 1", 0, 127, 110), N("LEVEL 2", 0, 127, 110), N("OUTPUT", 0, 127, 100), N("TO REVERB", 0, 127, 0),
};
static const FxParamDef kReverb[RV_COUNT] = {
    N("SEND", 0, 127, 50), L("MODE", kRvMode, RVM_STD),
    L("TYPE", kRvType, RVT_HALL1), N("PRE-DELAY", 0, 100, 15), N("TIME", 1, 100, 50), N("LEVEL", 0, 127, 100),
    N("LOW DAMP", -50, 50, 0), N("HIGH DAMP", -50, 50, 10), K("TIME", 1, 100, 40, Sec10), N("DENSITY", 1, 10, 8), K("PRE-DELAY", 0, 100, 20, Ms),
    N("MOD RATE", 0, 100, 30), N("MOD DEPTH", 0, 100, 25), K("PITCH 1", -24, 24, 12, Semi), K("PITCH 2", -24, 24, 7, Semi),
    K("FB 1", -98, 98, 35, Pct), K("FB 2", -98, 98, 0, Pct), N("LEVEL 1", 0, 100, 60), N("LEVEL 2", 0, 100, 20), N("LEVEL", 0, 127, 100),
};

const char* mfxName(int t) { return kTabs[t].name; }
int mfxParamCount(int t) { return kTabs[t].n; }
const FxParamDef& mfxParam(int t, int i) { return kTabs[t].p[i]; }
const FxParamDef& delayParam(int i) { return kDelay[i]; }
const FxParamDef& reverbParam(int i) { return kReverb[i]; }

void formatFx(const FxParamDef& d, int v, char* o, size_t n) {
    switch (d.kind) {
        case FxKind::OnOff:
        case FxKind::Labels: snprintf(o, n, "%s", d.labels[v < d.lo ? d.lo : (v > d.hi ? d.hi : v)]); break;
        case FxKind::Ms: snprintf(o, n, "%d ms", v); break;
        case FxKind::Pct: snprintf(o, n, "%d %%", v); break;
        case FxKind::Db: snprintf(o, n, "%+d dB", v); break;
        case FxKind::Hz100: snprintf(o, n, "%.2f Hz", (double)v / 100.0); break;
        case FxKind::Pan: if (v == 0) snprintf(o, n, "C"); else snprintf(o, n, "%c%d", v < 0 ? 'L' : 'R', v < 0 ? -v : v); break;
        case FxKind::Cents: snprintf(o, n, "%+d cent", v); break;
        case FxKind::Semi: snprintf(o, n, "%+d st", v); break;
        case FxKind::Sec10: snprintf(o, n, "%.1f s", (double)v / 10.0); break;
        case FxKind::Deg: snprintf(o, n, "%d deg", v); break;
        default: snprintf(o, n, "%d", v); break;
    }
}

void fxLoadMfxDefaults(FxPatch& f, int type) {
    f.mfxType = (uint8_t)type;
    memset(f.m, 0, sizeof(f.m));
    for (int i = 0; i < kTabs[type].n; i++) f.m[i] = kTabs[type].p[i].def;
}

void fxDefaults(FxPatch& f) {
    memset(&f, 0, sizeof(f));
    fxLoadMfxDefaults(f, MFX_CHO1);
    for (int i = 0; i < DL_COUNT; i++) f.d[i] = kDelay[i].def;
    for (int i = 0; i < RV_COUNT; i++) f.r[i] = kReverb[i].def;
}

}  // namespace j8
