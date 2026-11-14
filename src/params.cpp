#pragma GCC optimize("O2", "fast-math")
#include "params.h"
#include "dsp_math.h"
#include <cmath>
#include <cstdio>
#include <cstring>

namespace j8 {

static const char* const kOnOff[] = {"OFF", "ON"};
static const char* const kLfoWave[] = {"SIN", "SAW", "SQR", "RND"};
static const char* const kDest[] = {"V1", "1+2", "V2"};
static const char* const kPwMode[] = {"LFO", "MAN", "EN1"};
static const char* const kRange[] = {"16'", "8'", "4'", "2'"};
static const char* const kV1Wave[] = {"TRI", "SAW", "PUL", "SQR"};
static const char* const kV2Wave[] = {"SIN", "SAW", "PUL", "NOI"};
static const char* const kNormLow[] = {"NRM", "LOW"};
static const char* const kSlope[] = {"-12", "-24"};
static const char* const kEnvSel[] = {"EN1", "EN2"};
static const char* const kVcaLfo[] = {"0", "1", "2", "3"};
static const char* const kPol[] = {"NRM", "INV"};
static const char* const kAssign[] = {"SOLO", "UNI", "PLY1", "PLY2"};
static const char* const kKeyMode[] = {"WHOLE", "DUAL", "SPLIT", "2PART"};
static const char* const kChan[] = {"OMNI","1","2","3","4","5","6","7","8","9","10","11","12","13","14","15","16"};
static const char* const kPanel[] = {"UPPER", "LOWER"};
static const char* const kArpMode[] = {"UP", "DOWN", "U&D", "RND"};
static const char* const kArpRange[] = {"1", "2", "3", "4"};
static const char* const kPorta[] = {"OFF", "ON", "UPR"};

#define SEL(arr) (uint8_t)(sizeof(arr) / sizeof(arr[0])), arr

static const ParamInfo kInfo[kParamTotal] = {
    // LFO
    {"RAT", "LFO RATE", 0, nullptr, Unit::HzLfo, 20},
    {"DLY", "LFO DELAY", 0, nullptr, Unit::Sec, 21},
    {"WAV", "LFO WAVEFORM", SEL(kLfoWave), Unit::None, 22},
    // VCO MOD
    {"LFO", "VCO LFO MOD", 0, nullptr, Unit::Oct, 23},
    {"ENV", "VCO ENV-1 MOD", 0, nullptr, Unit::Oct, 24},
    {"DST", "VCO MOD DEST", SEL(kDest), Unit::None, 25},
    {"PW", "PULSE WIDTH MOD", 0, nullptr, Unit::Percent, 26},
    {"PWM", "PULSE WIDTH MODE", SEL(kPwMode), Unit::None, 27},
    // VCO-1
    {"XM", "CROSS MOD", 0, nullptr, Unit::Oct, 28},
    {"RNG", "VCO-1 RANGE", SEL(kRange), Unit::None, 29},
    {"WAV", "VCO-1 WAVEFORM", SEL(kV1Wave), Unit::None, 30},
    // VCO-2
    {"SYN", "VCO-2 SYNC", SEL(kOnOff), Unit::None, 31},
    {"RNG", "VCO-2 RANGE", SEL(kRange), Unit::None, 33},
    {"FRQ", "VCO-2 RANGE MODE", SEL(kNormLow), Unit::None, 34},
    {"FIN", "VCO-2 FINE TUNE", 0, nullptr, Unit::Cents, 35},
    {"WAV", "VCO-2 WAVEFORM", SEL(kV2Wave), Unit::None, 36},
    // MIX, HPF, VCF
    {"MIX", "SOURCE MIX V1-V2", 0, nullptr, Unit::Percent, 37},
    {"HPF", "HPF CUTOFF", 0, nullptr, Unit::Hz, 38},
    {"CUT", "VCF CUTOFF", 0, nullptr, Unit::Hz, 74},
    {"RES", "VCF RESONANCE", 0, nullptr, Unit::Percent, 71},
    {"SLP", "VCF SLOPE dB/OCT", SEL(kSlope), Unit::None, 39},
    {"ENV", "VCF ENV MOD", 0, nullptr, Unit::Oct, 40},
    {"SEL", "VCF ENV SELECT", SEL(kEnvSel), Unit::None, 41},
    {"LFO", "VCF LFO MOD", 0, nullptr, Unit::Oct, 42},
    {"KEY", "VCF KEY FOLLOW", 0, nullptr, Unit::Percent, 43},
    // VCA
    {"LVL", "VCA LEVEL", 0, nullptr, Unit::Percent, 44},
    {"LFO", "VCA LFO MOD", SEL(kVcaLfo), Unit::None, 45},
    // ENV-1
    {"A", "ENV-1 ATTACK", 0, nullptr, Unit::AttackT, 46},
    {"D", "ENV-1 DECAY", 0, nullptr, Unit::DecayT, 47},
    {"S", "ENV-1 SUSTAIN", 0, nullptr, Unit::Percent, 48},
    {"R", "ENV-1 RELEASE", 0, nullptr, Unit::DecayT, 49},
    {"KEY", "ENV-1 KEY FOLLOW", SEL(kOnOff), Unit::None, 50},
    {"POL", "ENV-1 POLARITY", SEL(kPol), Unit::None, 51},
    // ENV-2
    {"A", "ENV-2 ATTACK", 0, nullptr, Unit::AttackT, 73},
    {"D", "ENV-2 DECAY", 0, nullptr, Unit::DecayT, 75},
    {"S", "ENV-2 SUSTAIN", 0, nullptr, Unit::Percent, 79},
    {"R", "ENV-2 RELEASE", 0, nullptr, Unit::DecayT, 72},
    {"KEY", "ENV-2 KEY FOLLOW", SEL(kOnOff), Unit::None, 52},
    {"ASN", "ASSIGN MODE", SEL(kAssign), Unit::None, 53},
    // ---- globals ----
    {"VOL", "MASTER VOLUME", 0, nullptr, Unit::Percent, 7},
    {"UPR", "BALANCE UPPER", 0, nullptr, Unit::Percent, 54},
    {"LWR", "BALANCE LOWER", 0, nullptr, Unit::Percent, 55},
    {"TUN", "MASTER TUNE", 0, nullptr, Unit::Cents, 56},
    {"KEY", "KEY MODE", SEL(kKeyMode), Unit::None, 57},
    {"PNL", "PANEL MODE", SEL(kPanel), Unit::None, 58},
    {"SPL", "SPLIT POINT", 0, nullptr, Unit::Note, 59},
    {"HLU", "HOLD UPPER", SEL(kOnOff), Unit::None, 60},
    {"HLL", "HOLD LOWER", SEL(kOnOff), Unit::None, 61},
    {"ARP", "ARPEGGIO", SEL(kOnOff), Unit::None, 62},
    {"MOD", "ARP MODE", SEL(kArpMode), Unit::None, 63},
    {"RNG", "ARP RANGE OCT", SEL(kArpRange), Unit::None, 85},
    {"RAT", "ARP RATE", 0, nullptr, Unit::HzLfo, 86},
    {"POR", "PORTAMENTO MODE", SEL(kPorta), Unit::None, 65},
    {"TIM", "PORTAMENTO TIME", 0, nullptr, Unit::Sec, 5},
    {"VCO", "BEND SENS VCO", 0, nullptr, Unit::Percent, 87},
    {"V1", "BEND TO VCO-1", SEL(kOnOff), Unit::None, 88},
    {"V2", "BEND TO VCO-2", SEL(kOnOff), Unit::None, 89},
    {"VCF", "BEND SENS VCF", 0, nullptr, Unit::Percent, 90},
    {"SW", "BEND TO VCF", SEL(kOnOff), Unit::None, 102},
    {"RIS", "MOD RISE TIME", 0, nullptr, Unit::Sec, 103},
    {"VCO", "MOD SENS VCO", 0, nullptr, Unit::Percent, 104},
    {"SW", "MOD TO VCO", SEL(kOnOff), Unit::None, 105},
    {"VCF", "MOD SENS VCF", 0, nullptr, Unit::Percent, 106},
    {"SW", "MOD TO VCF", SEL(kOnOff), Unit::None, 107},
    {"VIN", "ANALOG TOLERANCE", 0, nullptr, Unit::Percent, 108},
    {"BPM", "EFFECT TEMPO", 0, nullptr, Unit::None, 109},
    {"CH1", "PART 1 (UPPER) MIDI CH", SEL(kChan), Unit::None, 0},
    {"CH2", "PART 2 (LOWER) MIDI CH", SEL(kChan), Unit::None, 0},
};

const ParamInfo& paramInfo(int id) { return kInfo[id]; }

int ccToParam(uint8_t cc) {
    if (cc == 0) return -1;
    for (int i = 0; i < kParamTotal; i++)
        if (kInfo[i].cc == cc) return i;
    return -1;
}

static inline float n(uint8_t v) { return (float)v / 255.f; }
// Exponential laws use pow2f, because these run every block for every voice.
float mapLfoRate(uint8_t v) { return 0.05f * pow2f(9.6439f * n(v)); }
float mapLfoDelay(uint8_t v) { return 4.f * n(v) * n(v); }
float mapAttack(uint8_t v) { return 0.001f * pow2f(12.2877f * n(v)); }
float mapDecay(uint8_t v) { return 0.001f * pow2f(13.2877f * n(v)); }
float mapCutoff(uint8_t v) { return 16.f * pow2f(10.2877f * n(v)); }
float mapHpf(uint8_t v) { return v == 0 ? 6.f : 16.f * pow2f(7.6439f * n(v)); }
float mapReso(uint8_t v) { float x = n(v); return 3.85f * x * (0.35f + 0.65f * x); }
float mapVcoLfoOct(uint8_t v) { return 3.f * n(v) * n(v); }
float mapVcoEnvOct(uint8_t v) { return 5.f * n(v) * n(v); }
float mapVcfEnvOct(uint8_t v) { return 9.f * n(v); }
float mapVcfLfoOct(uint8_t v) { return 4.f * n(v) * n(v); }
float mapXmodOct(uint8_t v) { return 3.5f * n(v) * n(v); }
float mapArpRate(uint8_t v) { return pow2f(4.3219f * n(v)); }
float mapPortaTime(uint8_t v) { return v == 0 ? 0.f : 0.002f * pow2f(10.5507f * n(v)); }
float mapRiseTime(uint8_t v) { return 5.f * n(v) * n(v); }
float mapV2TuneCents(uint8_t v) { return ((float)v - 128.f) * (50.f / 127.f); }

static void fmtTime(float s, char* out, size_t len) {
    if (s < 1.f) snprintf(out, len, "%.0f ms", s * 1000.f);
    else snprintf(out, len, "%.2f s", s);
}

void formatParam(int id, uint8_t v, char* out, size_t len) {
    const ParamInfo& pi = kInfo[id];
    if (pi.steps) {
        int i = v < pi.steps ? v : pi.steps - 1;
        snprintf(out, len, "%s", pi.labels[i]);
        return;
    }
    static const char* const kNotes[] = {"C", "C#", "D", "D#", "E", "F", "F#", "G", "G#", "A", "A#", "B"};
    switch (id) {
        case P_LFO_RATE: snprintf(out, len, "%.2f Hz", mapLfoRate(v)); return;
        case P_LFO_DELAY: fmtTime(mapLfoDelay(v), out, len); return;
        case P_VM_LFO: snprintf(out, len, "%.2f oct", mapVcoLfoOct(v)); return;
        case P_VM_ENV: snprintf(out, len, "%.2f oct", mapVcoEnvOct(v)); return;
        case P_V1_XMOD: snprintf(out, len, "%.2f oct", mapXmodOct(v)); return;
        case P_V2_TUNE: snprintf(out, len, "%+.0f cent", mapV2TuneCents(v)); return;
        case P_HPF: case P_VCF_CUT: {
            float f = id == P_HPF ? mapHpf(v) : mapCutoff(v);
            if (f < 1000.f) snprintf(out, len, "%.0f Hz", f);
            else snprintf(out, len, "%.2f kHz", f / 1000.f);
            return;
        }
        case P_VCF_ENV: snprintf(out, len, "%.1f oct", mapVcfEnvOct(v)); return;
        case P_VCF_LFO: snprintf(out, len, "%.2f oct", mapVcfLfoOct(v)); return;
        case P_VCF_KEY: snprintf(out, len, "%.0f %%", n(v) * 120.f); return;
        case P_E1_A: case P_E2_A: fmtTime(mapAttack(v), out, len); return;
        case P_E1_D: case P_E1_R: case P_E2_D: case P_E2_R: fmtTime(mapDecay(v), out, len); return;
        case (int)P_COUNT + (int)G_TUNE: snprintf(out, len, "%+.0f cent", ((float)v - 128.f) * (50.f / 127.f)); return;
        case (int)P_COUNT + (int)G_SPLIT: snprintf(out, len, "%s%d", kNotes[v % 12], (int)(v / 12) - 1); return;
        case (int)P_COUNT + (int)G_ARP_RATE: snprintf(out, len, "%.1f Hz", mapArpRate(v)); return;
        case (int)P_COUNT + (int)G_PORTA_TIME: fmtTime(mapPortaTime(v), out, len); return;
        case (int)P_COUNT + (int)G_TEMPO: snprintf(out, len, "%d BPM", 40 + (int)v); return;
        case (int)P_COUNT + (int)G_MOD_RISE: fmtTime(mapRiseTime(v), out, len); return;
        default: snprintf(out, len, "%.0f %%", n(v) * 100.f); return;
    }
}

void initPatch(Patch& p, const char* name) {
    memset(&p, 0, sizeof(p));
    fxDefaults(p.fx);
    strncpy(p.name, name, sizeof(p.name) - 1);
    uint8_t* v = p.v;
    v[P_LFO_RATE] = 150; v[P_LFO_WAVE] = LFO_SINE;
    v[P_VM_DEST] = DEST_BOTH; v[P_VM_PW] = 0; v[P_VM_PWMODE] = PW_MAN;
    v[P_V1_RANGE] = 1; v[P_V1_WAVE] = V1_SAW;
    v[P_V2_RANGE] = 1; v[P_V2_TUNE] = 128; v[P_V2_WAVE] = V2_SAW;
    v[P_MIX] = 128;
    v[P_VCF_CUT] = 200; v[P_VCF_SLOPE] = 1; v[P_VCF_ENVSEL] = 1; v[P_VCF_KEY] = 128;
    v[P_VCA_LEVEL] = 220;
    v[P_E1_A] = 0; v[P_E1_D] = 120; v[P_E1_S] = 128; v[P_E1_R] = 120;
    v[P_E2_A] = 0; v[P_E2_D] = 120; v[P_E2_S] = 255; v[P_E2_R] = 110;
    v[P_ASSIGN] = AS_POLY1;
}

void defaultGlobals(uint8_t* g) {
    memset(g, 0, G_COUNT);
    g[G_VOLUME] = 200; g[G_BAL_U] = 255; g[G_BAL_L] = 255; g[G_TUNE] = 128;
    g[G_KEYMODE] = KM_2PART; g[G_PANEL] = 0; g[G_SPLIT] = 60;
    g[G_CH_U] = 1; g[G_CH_L] = 2;
    g[G_ARP_RATE] = 120; g[G_ARP_MODE] = ARP_UP;
    g[G_PORTA_TIME] = 100; g[G_PORTA_MODE] = PM_OFF;
    g[G_BEND_VCO] = 90; g[G_BEND_V1] = 1; g[G_BEND_V2] = 1; g[G_BEND_VCF] = 0;
    g[G_MOD_VCO] = 70; g[G_MOD_VCOSW] = 1; g[G_MOD_VCF] = 0;
    g[G_VINTAGE] = 128;
    g[G_TEMPO] = 80;   // 120 BPM
}

}  // namespace j8
