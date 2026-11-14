// Parameter model: every control from the Jupiter-8 panel, plus a table that the
// engine (mapping) and UI (labels, units) share.
#pragma once
#include <cstdint>
#include <cstddef>
#include "fx_params.h"

namespace j8 {

enum PatchParam : uint8_t {
    P_LFO_RATE, P_LFO_DELAY, P_LFO_WAVE,
    P_VM_LFO, P_VM_ENV, P_VM_DEST, P_VM_PW, P_VM_PWMODE,
    P_V1_XMOD, P_V1_RANGE, P_V1_WAVE,
    P_V2_SYNC, P_V2_RANGE, P_V2_LOW, P_V2_TUNE, P_V2_WAVE,
    P_MIX, P_HPF,
    P_VCF_CUT, P_VCF_RES, P_VCF_SLOPE, P_VCF_ENV, P_VCF_ENVSEL, P_VCF_LFO, P_VCF_KEY,
    P_VCA_LEVEL, P_VCA_LFO,
    P_E1_A, P_E1_D, P_E1_S, P_E1_R, P_E1_KEY, P_E1_POL,
    P_E2_A, P_E2_D, P_E2_S, P_E2_R, P_E2_KEY,
    P_ASSIGN,
    P_COUNT
};

enum GlobalParam : uint8_t {
    G_VOLUME, G_BAL_U, G_BAL_L, G_TUNE,
    G_KEYMODE, G_PANEL, G_SPLIT, G_HOLD_U, G_HOLD_L,
    G_ARP_ON, G_ARP_MODE, G_ARP_RANGE, G_ARP_RATE,
    G_PORTA_MODE, G_PORTA_TIME,
    G_BEND_VCO, G_BEND_V1, G_BEND_V2, G_BEND_VCF, G_BEND_VCFSW,
    G_MOD_RISE, G_MOD_VCO, G_MOD_VCOSW, G_MOD_VCF, G_MOD_VCFSW,
    G_VINTAGE,
    G_TEMPO,
    G_CH_U, G_CH_L,     // MIDI channel per part: 0 = OMNI, 1-16
    G_COUNT
};

// Selector values
enum LfoWave { LFO_SINE, LFO_SAW, LFO_SQUARE, LFO_RANDOM };
enum ModDest { DEST_VCO1, DEST_BOTH, DEST_VCO2 };
enum PwMode { PW_LFO, PW_MAN, PW_ENV1 };
enum V1Wave { V1_TRI, V1_SAW, V1_PULSE, V1_SQUARE };
enum V2Wave { V2_SINE, V2_SAW, V2_PULSE, V2_NOISE };
enum Assign { AS_SOLO, AS_UNISON, AS_POLY1, AS_POLY2 };
enum KeyMode { KM_WHOLE, KM_DUAL, KM_SPLIT, KM_2PART };
enum ArpMode { ARP_UP, ARP_DOWN, ARP_UPDOWN, ARP_RANDOM };
enum PortaMode { PM_OFF, PM_ON, PM_UPPER };

struct Patch {
    char name[12];
    uint8_t v[P_COUNT];
    FxPatch fx;      // in Dual and Split, the Upper patch's effects apply to both
};

enum class Unit : uint8_t { None, Hz, HzLfo, Sec, Percent, Cents, Oct, Semi, Note, AttackT, DecayT };

struct ParamInfo {
    const char* shortName;   // up to 3 chars, under the control
    const char* longName;    // readout at the bottom of the screen
    uint8_t steps;           // 0 = continuous 0-255; otherwise a selector with this many values
    const char* const* labels;
    Unit unit;
    uint8_t cc;              // MIDI CC, 0 = none
};

constexpr int kParamTotal = (int)P_COUNT + (int)G_COUNT;
const ParamInfo& paramInfo(int id);          // id < P_COUNT: patch; otherwise P_COUNT + GlobalParam
void formatParam(int id, uint8_t v, char* out, size_t n);
int ccToParam(uint8_t cc);                   // -1 if unmapped

// Value mappings. Spec-sheet numbers are noted in README "Accuracy".
float mapLfoRate(uint8_t v);       // 0.05 - 40 Hz
float mapLfoDelay(uint8_t v);      // 0 - 4 s
float mapAttack(uint8_t v);        // 1 ms - 5 s
float mapDecay(uint8_t v);         // 1 ms - 10 s
float mapCutoff(uint8_t v);        // Hz
float mapHpf(uint8_t v);           // Hz
float mapReso(uint8_t v);          // feedback k, stops short of self-oscillation
float mapVcoLfoOct(uint8_t v);     // octaves peak
float mapVcoEnvOct(uint8_t v);     // octaves
float mapVcfEnvOct(uint8_t v);     // octaves
float mapVcfLfoOct(uint8_t v);     // octaves peak
float mapXmodOct(uint8_t v);       // octaves peak
float mapArpRate(uint8_t v);       // 1 - 20 Hz
float mapPortaTime(uint8_t v);     // s
float mapRiseTime(uint8_t v);      // s
float mapV2TuneCents(uint8_t v);   // -50..+50 cents around centre

void initPatch(Patch& p, const char* name);
void applyFactory(Patch* bank, int count);   // starting-point sounds, not Roland's factory patches
void defaultGlobals(uint8_t* g);

}  // namespace j8
