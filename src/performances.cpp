// The 100 performances (programs P01-P100): two patches, a key mode and the performance settings each.
// Original sounds, made for this plugin (not in the Mini-Ju08 firmware).
//
// Rough values of the 0-255 controls (params.cpp has the curves):
//   LFO RATE 30 0.1 Hz, 60 0.25 Hz, 100 0.7 Hz, 120 1.2 Hz, 150 2.5 Hz, 175 5 Hz, 190 7 Hz
//   ATTACK 120 26 ms, 150 0.15 s, 180 0.4 s, 200 0.8 s, 220 1.6 s;  DECAY 130 0.1 s, 170 0.45 s, 190 1 s, 210 2 s, 230 4 s
//   CUTOFF 100 260 Hz, 140 800 Hz, 175 2 kHz, 200 3.9 kHz, 220 7 kHz;  VCF ENV 100: 3.5 octaves
//   VCF LFO 60 0.2 oct, 100 0.6 oct, 150 1.4 oct;  VCO LFO 30: +-0.5 semitone;  ARP RATE 100 3 Hz, 130 4.5 Hz, 160 6.8 Hz
//   PORTA TIME 120 60 ms, 150 0.13 s, 170 0.2 s;  SPLIT 48 C3, 55 G3, 60 C4
#include "performances.h"
#include <cstring>
#include <vector>

namespace j8 {

const uint8_t kPerformanceGlobals[] = {
    G_BAL_U, G_BAL_L, G_KEYMODE, G_SPLIT, G_HOLD_U, G_HOLD_L, G_ARP_ON, G_ARP_MODE, G_ARP_RANGE, G_ARP_RATE,
    G_PORTA_MODE, G_PORTA_TIME, G_BEND_VCO, G_BEND_V1, G_BEND_V2, G_BEND_VCF, G_BEND_VCFSW,
    G_MOD_RISE, G_MOD_VCO, G_MOD_VCOSW, G_MOD_VCF, G_MOD_VCFSW};
const int kNumPerformanceGlobals = (int)sizeof(kPerformanceGlobals);

namespace {

struct Pv { uint8_t id, val; };

constexpr uint8_t CUT = P_VCF_CUT, RES = P_VCF_RES, VE = P_VCF_ENV, SEL = P_VCF_ENVSEL, KF = P_VCF_KEY, FL = P_VCF_LFO, SLP = P_VCF_SLOPE,
    HP = P_HPF, MX = P_MIX, W1 = P_V1_WAVE, W2 = P_V2_WAVE, RG1 = P_V1_RANGE, RG2 = P_V2_RANGE, TN = P_V2_TUNE, XM = P_V1_XMOD, SY = P_V2_SYNC,
    V2L = P_V2_LOW, PD = P_VM_PW, PM = P_VM_PWMODE, VL = P_VM_LFO, PE = P_VM_ENV, DST = P_VM_DEST, RT = P_LFO_RATE, DL = P_LFO_DELAY,
    LW = P_LFO_WAVE, A1 = P_E1_A, D1 = P_E1_D, S1 = P_E1_S, R1 = P_E1_R, POL = P_E1_POL, A2 = P_E2_A, D2 = P_E2_D,
    S2 = P_E2_S, R2 = P_E2_R, AS = P_ASSIGN, KA = P_VCA_LFO;
constexpr uint8_t TRI = V1_TRI, SAW1 = V1_SAW, PUL1 = V1_PULSE, SQR = V1_SQUARE, SIN2 = V2_SINE, SAW2 = V2_SAW, PUL2 = V2_PULSE, NOI = V2_NOISE;
// performance settings
constexpr uint8_t ARP = G_ARP_ON, AMODE = G_ARP_MODE, ARANGE = G_ARP_RANGE, ARATE = G_ARP_RATE, PORTA = G_PORTA_MODE, PTIME = G_PORTA_TIME,
    MVCF = G_MOD_VCF, MVCFSW = G_MOD_VCFSW, MVCOSW = G_MOD_VCOSW, MRISE = G_MOD_RISE;
constexpr uint8_t DU = KM_DUAL, SP = KM_SPLIT;

enum Fx { F_NONE, F_J1H, F_J2H, F_J12H, F_D2H, F_D3H, F_CEH, F_HALL, F_HALL2, F_PLATE, F_ROOM, F_PING, F_PINGH, F_TAP, F_SHIM,
          F_SHIMJ, F_ODP, F_PHH, F_PH1H, F_J2PH, F_FILTH, F_LOFIR, F_FATP, F_LONG };

void applyFx(FxPatch& f, int set) {
    auto mfx = [&](int type) { fxLoadMfxDefaults(f, type); f.mfxOn = 1; };
    auto rev = [&](int type, int time, int send) { f.revOn = 1; f.r[RV_MODE] = RVM_STD; f.r[RV_TYPE] = (int16_t)type; f.r[RV_TIME] = (int16_t)time; f.r[RV_SEND] = (int16_t)send; };
    auto shim = [&](int send) { f.revOn = 1; f.r[RV_MODE] = RVM_SHIMMER; f.r[RV_SEND] = (int16_t)send; f.r[RS_TIME] = 50; };
    auto ping = [&](int note, int send, int fb) { f.dlyOn = 1; f.d[DL_MODE] = DLM_PING; f.d[DL_SYNC] = 1; f.d[DL_NOTE] = (int16_t)note; f.d[DL_SEND] = (int16_t)send; f.d[DL_FB] = (int16_t)fb; };
    switch (set) {
        case F_J1H: mfx(MFX_CHO1); rev(RVT_HALL1, 70, 40); break;
        case F_J2H: mfx(MFX_CHO1); f.m[C1_MODE] = 1; rev(RVT_HALL1, 70, 40); break;
        case F_J12H: mfx(MFX_CHO1); f.m[C1_MODE] = 2; rev(RVT_HALL1, 65, 35); break;
        case F_D2H: mfx(MFX_CHO3); f.m[C3_MODE] = 1; rev(RVT_HALL1, 70, 40); break;
        case F_D3H: mfx(MFX_CHO3); f.m[C3_MODE] = 2; rev(RVT_HALL2, 70, 45); break;
        case F_CEH: mfx(MFX_CHO2); f.m[C2_INT] = 90; rev(RVT_HALL1, 70, 40); break;
        case F_HALL: rev(RVT_HALL1, 75, 50); break;
        case F_HALL2: rev(RVT_HALL2, 80, 55); break;
        case F_PLATE: rev(RVT_PLATE, 65, 50); break;
        case F_ROOM: rev(RVT_ROOM2, 75, 40); break;
        case F_PING: ping(11, 60, 40); break;
        case F_PINGH: ping(11, 60, 45); rev(RVT_HALL1, 65, 30); break;
        case F_TAP: f.dlyOn = 1; f.d[DL_MODE] = DLM_2TAP; f.d[DL_SYNC] = 1; f.d[DL_NOTE] = 12; f.d[DL_SEND] = 60; f.d[DL_FB] = 40; break;
        case F_SHIM: shim(70); break;
        case F_SHIMJ: mfx(MFX_CHO1); f.m[C1_MODE] = 1; shim(60); break;
        case F_ODP: mfx(MFX_OD); f.m[OD_DIST] = 45; ping(9, 50, 40); break;
        case F_PHH: mfx(MFX_PHA2); f.m[P2_RATE] = 25; rev(RVT_PLATE, 60, 35); break;
        case F_PH1H: mfx(MFX_PHA1); rev(RVT_HALL1, 70, 40); break;
        case F_J2PH: mfx(MFX_CHO1); f.m[C1_MODE] = 1; ping(11, 45, 35); rev(RVT_HALL1, 65, 30); break;
        case F_FILTH: mfx(MFX_FILT); rev(RVT_HALL1, 65, 35); break;
        case F_LOFIR: mfx(MFX_LOFI); rev(RVT_ROOM2, 70, 35); break;
        case F_FATP: mfx(MFX_FATTEN); f.m[FT_ODD] = 110; f.m[FT_EVEN] = 140; ping(11, 40, 30); break;
        case F_LONG: mfx(MFX_CHO3); f.m[C3_MODE] = 2; ping(13, 45, 50); rev(RVT_HALL2, 90, 60); break;
        default: break;
    }
}

struct Def {
    const char* name;
    uint8_t km, split, balU, balL;
    int fx;
    std::vector<Pv> up, lo, g;
};

// Patches are written as changes from the INIT patch (saw + saw, 8', VCF 200, ENV-2 organ shape, Poly 1).
const std::vector<Def>& defs() {
    static const std::vector<Def> d = {
    // ================================================================ P01-P30: Dual (both patches on every key)
    {"BRASS STRINGS", DU, 60, 255, 230, F_J1H,
        {{TN,136},{CUT,110},{RES,30},{VE,140},{SEL,0},{A1,85},{D1,145},{S1,110},{R1,130},{A2,75},{D2,130},{S2,200},{R2,125}},
        {{TN,137},{HP,70},{CUT,175},{RES,10},{A2,150},{R2,170},{VL,22},{RT,170},{DL,90}}, {}},
    {"PAD AND BELLS", DU, 60, 210, 255, F_D3H,
        {{W1,TRI},{W2,SIN2},{RG2,3},{XM,110},{MX,0},{HP,50},{CUT,230},{D2,190},{S2,0},{R2,180}},
        {{TN,138},{CUT,135},{RES,20},{VE,30},{SEL,0},{A1,150},{D1,190},{S1,90},{R1,180},{A2,140},{R2,180},{VL,12},{RT,160}}, {}},
    {"OCTAVE PWM", DU, 60, 230, 255, F_J2H,
        {{W1,PUL1},{W2,PUL2},{RG1,2},{RG2,2},{TN,134},{PD,150},{PM,PW_LFO},{RT,115},{CUT,170},{RES,30},{A2,140},{R2,175}},
        {{W1,PUL1},{W2,PUL2},{RG1,0},{RG2,0},{TN,122},{PD,140},{PM,PW_LFO},{RT,95},{CUT,120},{A2,150},{R2,180}}, {}},
    {"SYNC STACK", DU, 60, 255, 220, F_J1H,
        {{SY,1},{RG2,2},{MX,200},{PE,120},{DST,DEST_VCO2},{A1,40},{D1,200},{S1,0},{R1,150},{CUT,190},{A2,60},{R2,150}},
        {{TN,140},{CUT,120},{RES,25},{VE,40},{SEL,0},{A1,160},{D1,200},{S1,80},{A2,120},{R2,170}}, {}},
    {"FILTER WAVES", DU, 60, 255, 255, F_CEH,
        {{TN,136},{CUT,120},{RES,90},{FL,110},{RT,62},{A2,130},{R2,175}},
        {{TN,121},{RG1,0},{RG2,1},{CUT,110},{RES,80},{FL,110},{RT,74},{LW,LFO_SAW},{A2,140},{R2,180}}, {}},
    {"PWM ENSEMBLE", DU, 60, 255, 255, F_J12H,
        {{W1,PUL1},{W2,PUL2},{TN,135},{PD,160},{PM,PW_LFO},{RT,112},{CUT,160},{HP,60},{A2,120},{R2,170}},
        {{W1,PUL1},{W2,SAW2},{TN,123},{PD,130},{PM,PW_LFO},{RT,138},{CUT,145},{A2,130},{R2,175}}, {}},
    {"GLASS CHOIR", DU, 60, 255, 200, F_HALL2,
        {{W1,SQR},{W2,SIN2},{RG2,3},{XM,25},{MX,90},{HP,110},{CUT,200},{A2,170},{R2,200},{VL,10},{RT,165},{DL,120}},
        {{W2,NOI},{MX,110},{HP,140},{CUT,190},{RES,60},{FL,45},{RT,70},{A2,190},{R2,205}}, {}},
    {"STRING MACHINE", DU, 60, 255, 220, F_D3H,
        {{TN,137},{HP,70},{CUT,180},{A2,140},{R2,170},{VL,20},{RT,170},{DL,100}},
        {{RG1,2},{RG2,2},{TN,140},{HP,120},{CUT,215},{A2,150},{R2,180},{VL,16},{RT,168},{DL,130}}, {}},
    {"BRASS SWELL DUO", DU, 60, 255, 255, F_HALL,
        {{TN,136},{CUT,100},{VE,150},{SEL,0},{A1,170},{D1,185},{S1,120},{R1,150},{A2,160},{D2,170},{S2,230},{R2,145}},
        {{TN,134},{RG1,0},{RG2,1},{CUT,115},{VE,130},{SEL,0},{A1,70},{D1,145},{S1,100},{A2,60},{D2,130},{S2,210},{R2,125}}, {}},
    {"SAW WALL", DU, 60, 255, 255, F_J2H,
        {{TN,146},{CUT,165},{RES,15},{A2,110},{R2,165}},
        {{TN,112},{RG1,0},{RG2,0},{CUT,130},{A2,120},{R2,170}}, {}},
    {"INVERTED SWEEP", DU, 60, 255, 240, F_J2H,
        {{TN,138},{CUT,190},{RES,70},{VE,110},{SEL,0},{POL,1},{A1,200},{D1,215},{S1,60},{R1,200},{A2,150},{R2,185}},
        {{TN,135},{CUT,70},{RES,60},{VE,120},{SEL,0},{A1,200},{D1,215},{S1,70},{R1,200},{A2,160},{R2,190}}, {}},
    {"KEY TRACK PAD", DU, 60, 240, 255, F_D2H,
        {{W1,PUL1},{PD,100},{PM,PW_MAN},{TN,136},{CUT,120},{KF,255},{A2,140},{R2,175}},
        {{W1,TRI},{W2,SIN2},{RG1,0},{RG2,1},{MX,100},{CUT,150},{KF,0},{A2,150},{R2,180}}, {}},
    {"TREMOLO KEYS", DU, 60, 255, 200, F_PLATE,
        {{W1,TRI},{W2,SIN2},{RG2,3},{XM,28},{MX,50},{CUT,180},{D2,190},{S2,40},{R2,140},{KA,2},{RT,160}},
        {{W1,TRI},{MX,0},{CUT,140},{A2,150},{R2,170}}, {}},
    {"SAMPLE AND HOLD", DU, 60, 255, 220, F_PINGH,
        {{TN,136},{CUT,120},{RES,150},{FL,120},{LW,LFO_RANDOM},{RT,150},{A2,120},{R2,160}},
        {{W1,TRI},{TN,136},{MX,100},{CUT,110},{A2,150},{R2,180}}, {}},
    {"TRILL PAD", DU, 60, 230, 255, F_J1H,
        {{W1,SQR},{W2,SIN2},{RG2,2},{MX,110},{VL,32},{LW,LFO_SQUARE},{RT,165},{DST,DEST_BOTH},{CUT,190},{A2,120},{R2,160}},
        {{TN,137},{CUT,140},{RES,20},{A2,150},{R2,180}}, {}},
    {"ORGAN AND PAD", DU, 60, 255, 210, F_J1H,
        {{W1,SQR},{W2,SIN2},{RG2,2},{MX,110},{HP,30},{CUT,220},{D2,100},{S2,255},{R2,50},{VL,8},{RT,175}},
        {{TN,137},{HP,70},{CUT,160},{A2,170},{R2,180}}, {}},
    {"BELL PAD", DU, 60, 220, 255, F_HALL2,
        {{W1,TRI},{W2,SIN2},{RG1,1},{RG2,3},{TN,175},{XM,90},{MX,0},{CUT,220},{D2,215},{S2,0},{R2,205}},
        {{TN,138},{CUT,125},{RES,30},{VE,40},{SEL,0},{A1,200},{D1,210},{S1,80},{R1,200},{A2,200},{R2,200},{VL,10},{RT,150}}, {}},
    {"CLAV AND BRASS", DU, 60, 255, 220, F_ROOM,
        {{W1,PUL1},{MX,0},{PD,150},{PM,PW_MAN},{HP,70},{CUT,130},{RES,70},{VE,120},{SEL,0},{D1,105},{S1,0},{D2,135},{S2,0},{R2,70}},
        {{TN,136},{CUT,120},{VE,150},{SEL,0},{A1,40},{D1,125},{S1,60},{R1,110},{A2,30},{D2,130},{S2,150},{R2,95}}, {}},
    {"HOLLOW ENV PWM", DU, 60, 255, 230, F_J2H,
        {{W1,PUL1},{W2,PUL2},{PD,200},{PM,PW_ENV1},{A1,170},{D1,210},{S1,40},{R1,180},{TN,134},{CUT,170},{A2,140},{R2,175}},
        {{W1,TRI},{W2,SIN2},{RG1,0},{RG2,1},{MX,128},{CUT,160},{A2,150},{R2,180}}, {}},
    {"RESONANT DREAM", DU, 60, 255, 230, F_D3H,
        {{TN,137},{CUT,80},{RES,170},{SLP,0},{VE,110},{SEL,0},{A1,190},{D1,220},{S1,50},{R1,200},{FL,40},{RT,90},{A2,160},{R2,190}},
        {{W1,TRI},{TN,135},{MX,90},{CUT,130},{A2,170},{R2,190}}, {}},
    {"SPACE CHOIR", DU, 60, 255, 220, F_SHIM,
        {{TN,138},{CUT,150},{RES,40},{FL,70},{RT,55},{A2,190},{R2,205}},
        {{W2,NOI},{MX,140},{HP,150},{CUT,180},{RES,90},{FL,90},{RT,40},{A2,200},{R2,210}}, {}},
    {"VINTAGE POLY", DU, 60, 255, 220, F_J1H,
        {{TN,136},{CUT,150},{VE,80},{SEL,0},{D1,170},{S1,90},{KF,180},{A2,20},{D2,180},{S2,200},{R2,140}},
        {{W1,SQR},{W2,PUL2},{PD,90},{PM,PW_MAN},{TN,133},{CUT,130},{A2,40},{R2,150}}, {}},
    {"TWIN VIBRATO STR", DU, 60, 255, 255, F_D3H,
        {{TN,137},{HP,60},{CUT,170},{A2,160},{R2,180},{VL,24},{RT,168},{DL,150}},
        {{TN,135},{RG1,0},{RG2,1},{HP,40},{CUT,150},{A2,170},{R2,185},{VL,24},{RT,158},{DL,180}}, {}},
    {"OCTAVE BRASS", DU, 60, 240, 255, F_ROOM,
        {{RG1,2},{RG2,2},{TN,136},{CUT,120},{VE,130},{SEL,0},{A1,80},{D1,145},{S1,110},{A2,70},{D2,130},{S2,200},{R2,120}},
        {{TN,134},{CUT,105},{RES,25},{VE,140},{SEL,0},{A1,90},{D1,150},{S1,100},{A2,80},{D2,140},{S2,210},{R2,125}}, {}},
    {"SOFT HORNS", DU, 60, 255, 220, F_HALL,
        {{TN,134},{CUT,90},{RES,20},{VE,100},{SEL,0},{A1,120},{D1,160},{S1,110},{R1,140},{A2,110},{D2,140},{S2,210},{R2,140}},
        {{TN,137},{HP,80},{CUT,160},{A2,160},{R2,180},{VL,18},{RT,165},{DL,140}}, {}},
    {"CRYSTAL PAD", DU, 60, 220, 255, F_PLATE,
        {{SY,1},{RG2,3},{MX,230},{PE,60},{DST,DEST_VCO2},{A1,0},{D1,170},{S1,0},{HP,120},{CUT,230},{D2,180},{S2,0},{R2,180}},
        {{W1,PUL1},{W2,PUL2},{TN,135},{PD,150},{PM,PW_LFO},{RT,110},{CUT,140},{A2,150},{R2,180}}, {}},
    {"PLUCK OVER SWEEP", DU, 60, 255, 230, F_PINGH,
        {{TN,136},{CUT,110},{VE,140},{SEL,0},{D1,125},{S1,0},{D2,150},{S2,0},{R2,130}},
        {{TN,138},{CUT,100},{RES,110},{FL,140},{RT,35},{A2,170},{R2,190}}, {}},
    {"DARK DUAL", DU, 60, 255, 230, F_HALL2,
        {{TN,137},{CUT,95},{RES,60},{FL,60},{RT,80},{A2,150},{R2,190}},
        {{W1,TRI},{W2,SIN2},{RG1,0},{RG2,0},{MX,100},{CUT,120},{A2,170},{R2,190}}, {}},
    {"PHASE PAD", DU, 60, 255, 255, F_PHH,
        {{TN,138},{CUT,150},{RES,30},{A2,150},{R2,180}},
        {{W1,PUL1},{W2,PUL2},{RG1,0},{RG2,1},{PD,140},{PM,PW_LFO},{RT,100},{CUT,130},{A2,160},{R2,185}}, {}},
    {"DREAMLAND", DU, 60, 255, 230, F_SHIMJ,
        {{W1,TRI},{W2,SIN2},{RG2,2},{MX,90},{CUT,170},{FL,40},{RT,60},{A2,210},{R2,215}},
        {{TN,138},{CUT,110},{RES,40},{VE,50},{SEL,0},{A1,210},{D1,220},{S1,90},{R1,210},{A2,200},{R2,210}}, {}},

    // ================================================================ P31-P55: Split (Lower below the split point)
    {"BASS AND PIANO", SP, 48, 230, 255, F_J1H,
        {{W1,TRI},{W2,SIN2},{RG2,3},{XM,28},{MX,50},{CUT,180},{D2,190},{S2,0},{R2,130}},
        {{W1,SAW1},{W2,SIN2},{RG1,1},{RG2,0},{MX,55},{CUT,140},{KF,100},{VE,50},{SEL,0},{D1,130},{S1,40},{D2,170},{S2,230},{R2,90},{AS,AS_SOLO}}, {}},
    {"BASS AND BRASS", SP, 48, 230, 255, F_ROOM,
        {{TN,136},{CUT,110},{RES,30},{VE,140},{SEL,0},{A1,85},{D1,145},{S1,110},{R1,130},{A2,75},{D2,130},{S2,200},{R2,125}},
        {{RG1,1},{RG2,0},{TN,140},{CUT,150},{RES,50},{VE,110},{SEL,0},{D1,130},{S1,70},{R1,100},{D2,160},{S2,200},{R2,85},{AS,AS_SOLO}}, {}},
    {"BASS AND STRINGS", SP, 48, 230, 255, F_D3H,
        {{TN,137},{HP,70},{CUT,175},{RES,10},{A2,150},{R2,170},{VL,22},{RT,170},{DL,90}},
        {{W1,PUL1},{W2,SAW2},{RG1,1},{RG2,0},{PD,90},{PM,PW_MAN},{CUT,155},{VE,150},{SEL,0},{D1,125},{S1,50},{D2,180},{S2,140},{R2,90},{AS,AS_SOLO}}, {}},
    {"ACID AND PAD", SP, 48, 210, 255, F_J1H,
        {{TN,138},{CUT,135},{RES,20},{VE,30},{SEL,0},{A1,150},{D1,190},{S1,90},{R1,180},{A2,130},{R2,175},{VL,12},{RT,160}},
        {{MX,0},{W1,SAW1},{RG1,1},{SLP,0},{CUT,95},{RES,200},{VE,180},{SEL,0},{D1,140},{S1,0},{D2,130},{S2,150},{R2,70},{FL,30},{RT,60},{AS,AS_SOLO}}, {}},
    {"SYNC BASS LEAD", SP, 48, 220, 255, F_PING,
        {{SY,1},{RG2,2},{MX,255},{PE,110},{DST,DEST_VCO2},{D1,135},{S1,0},{CUT,215},{AS,AS_SOLO}},
        {{SY,1},{RG1,1},{RG2,3},{MX,200},{PE,90},{DST,DEST_VCO2},{CUT,150},{VE,40},{SEL,0},{D1,125},{S1,0},{D2,150},{S2,170},{R2,80},{AS,AS_SOLO}},
        {{PORTA,PM_UPPER},{PTIME,135}}},
    {"SUB AND GLIDE", SP, 48, 220, 255, F_PINGH,
        {{TN,134},{CUT,170},{RES,40},{VE,50},{SEL,0},{D1,140},{S1,60},{VL,14},{RT,175},{DL,120},{AS,AS_SOLO}},
        {{W1,TRI},{W2,SIN2},{RG1,1},{RG2,0},{MX,128},{CUT,120},{D2,170},{S2,230},{R2,80},{AS,AS_SOLO}},
        {{PORTA,PM_UPPER},{PTIME,150}}},
    {"FUNK SPLIT", SP, 48, 230, 255, F_ROOM,
        {{W1,PUL1},{MX,0},{PD,150},{PM,PW_MAN},{HP,70},{CUT,130},{RES,70},{VE,120},{SEL,0},{D1,105},{S1,0},{D2,135},{S2,0},{R2,70}},
        {{W1,SQR},{W2,SIN2},{RG1,1},{RG2,0},{MX,50},{CUT,145},{KF,100},{VE,60},{SEL,0},{D1,140},{S1,60},{D2,170},{S2,210},{R2,85},{AS,AS_SOLO}}, {}},
    {"ORGAN SPLIT", SP, 48, 220, 255, F_ROOM,
        {{W1,SQR},{W2,SIN2},{RG2,2},{MX,110},{HP,30},{CUT,220},{D2,100},{S2,255},{R2,50},{VL,8},{RT,175}},
        {{W1,SQR},{W2,SIN2},{RG1,0},{RG2,1},{MX,90},{CUT,170},{D2,100},{S2,255},{R2,40},{AS,AS_SOLO}}, {}},
    {"WOBBLE AND STAB", SP, 48, 210, 255, F_PING,
        {{TN,136},{CUT,150},{VE,110},{SEL,0},{D1,120},{S1,20},{D2,150},{S2,80},{R2,110}},
        {{RG1,1},{RG2,0},{TN,132},{SLP,1},{CUT,90},{RES,130},{FL,150},{LW,LFO_SINE},{RT,140},{D2,170},{S2,240},{R2,70},{AS,AS_SOLO}}, {}},
    {"BASS AND BELLS", SP, 48, 220, 255, F_HALL,
        {{W1,TRI},{W2,SIN2},{RG2,3},{XM,120},{MX,0},{CUT,230},{D2,190},{S2,0},{R2,180},{HP,50}},
        {{W1,TRI},{W2,SIN2},{RG1,1},{RG2,0},{MX,100},{CUT,130},{VE,40},{SEL,0},{D1,140},{S1,80},{D2,170},{S2,220},{R2,90},{AS,AS_SOLO}}, {}},
    {"DRONE AND PAD", SP, 48, 230, 255, F_HALL2,
        {{TN,138},{CUT,135},{RES,20},{A2,150},{R2,185},{VL,12},{RT,160}},
        {{RG1,0},{RG2,0},{TN,124},{CUT,90},{RES,90},{FL,80},{RT,40},{A2,180},{R2,200},{AS,AS_SOLO}}, {}},
    {"BASS AND VIBES", SP, 48, 230, 255, F_PLATE,
        {{W1,TRI},{W2,SIN2},{RG2,2},{MX,80},{CUT,190},{D2,190},{S2,0},{R2,185},{KA,1},{RT,165}},
        {{W1,TRI},{W2,SIN2},{RG1,1},{RG2,0},{MX,60},{CUT,125},{D2,175},{S2,200},{R2,90},{AS,AS_SOLO}}, {}},
    {"UNISON AND PAD", SP, 48, 220, 255, F_J1H,
        {{W1,PUL1},{W2,PUL2},{TN,134},{PD,150},{PM,PW_LFO},{RT,120},{CUT,150},{RES,35},{A2,140},{R2,175}},
        {{RG1,1},{RG2,0},{TN,132},{CUT,115},{RES,80},{VE,130},{SEL,0},{D1,120},{S1,30},{R1,90},{D2,140},{S2,190},{R2,75},{AS,AS_UNISON}}, {}},
    {"CELLO AND FLUTE", SP, 55, 255, 230, F_ROOM,
        {{W1,TRI},{W2,NOI},{MX,30},{CUT,150},{A2,60},{R2,120},{VL,16},{RT,175},{DL,120}},
        {{RG1,1},{RG2,0},{TN,134},{HP,30},{CUT,145},{VE,30},{SEL,0},{A1,120},{D1,160},{A2,125},{R2,130},{VL,28},{RT,165},{DL,120}}, {}},
    {"BASS AND RESO KEYS", SP, 48, 220, 255, F_PING,
        {{W1,PUL1},{W2,PUL2},{TN,136},{PD,120},{PM,PW_MAN},{HP,90},{CUT,145},{RES,120},{VE,170},{SEL,0},{D1,115},{S1,0},{D2,150},{S2,0},{R2,150}},
        {{RG1,1},{RG2,0},{TN,138},{CUT,130},{RES,40},{VE,90},{SEL,0},{D1,140},{S1,40},{D2,170},{S2,220},{R2,80},{AS,AS_SOLO}}, {}},
    {"XMOD AND GLASS", SP, 48, 220, 255, F_PLATE,
        {{W1,TRI},{W2,SIN2},{RG2,3},{TN,200},{XM,60},{MX,0},{HP,60},{CUT,220},{D2,200},{S2,0},{R2,190}},
        {{XM,60},{W2,SIN2},{RG1,1},{RG2,2},{MX,70},{CUT,155},{VE,80},{SEL,0},{D1,130},{S1,0},{D2,160},{S2,180},{R2,85},{AS,AS_SOLO}}, {}},
    {"RANDOM BASS LEAD", SP, 48, 220, 255, F_PINGH,
        {{TN,134},{CUT,180},{RES,60},{VL,12},{RT,175},{DL,120},{AS,AS_SOLO}},
        {{RG1,1},{RG2,0},{TN,134},{CUT,110},{RES,140},{FL,110},{LW,LFO_RANDOM},{RT,145},{D2,170},{S2,220},{R2,80},{AS,AS_SOLO}},
        {{PORTA,PM_UPPER},{PTIME,120}}},
    {"STRINGS AND LEAD", SP, 60, 255, 220, F_D2H,
        {{W1,SQR},{W2,PUL2},{TN,136},{CUT,160},{VL,14},{RT,175},{DL,120},{AS,AS_SOLO}},
        {{TN,137},{HP,70},{CUT,170},{A2,150},{R2,175},{VL,20},{RT,170},{DL,90}},
        {{PORTA,PM_UPPER},{PTIME,140}}},
    {"PAD AND PWM LEAD", SP, 60, 255, 220, F_J2PH,
        {{W1,PUL1},{W2,PUL2},{PD,120},{PM,PW_LFO},{RT,170},{TN,134},{CUT,175},{RES,30},{A2,30},{R2,120},{AS,AS_SOLO}},
        {{TN,138},{CUT,130},{RES,25},{A2,150},{R2,180},{VL,10},{RT,150}},
        {{PORTA,PM_UPPER},{PTIME,125}}},
    {"BASS AND CHOIR", SP, 48, 230, 255, F_HALL2,
        {{W1,SQR},{W2,SIN2},{RG2,3},{XM,25},{MX,90},{HP,110},{CUT,200},{A2,170},{R2,200}},
        {{W1,TRI},{W2,SIN2},{RG1,1},{RG2,0},{MX,70},{CUT,135},{VE,30},{SEL,0},{D1,150},{S1,90},{D2,170},{S2,230},{R2,95},{AS,AS_SOLO}}, {}},
    {"TECHNO SPLIT", SP, 48, 220, 255, F_PING,
        {{W1,SQR},{TN,133},{MX,100},{CUT,100},{RES,100},{VE,120},{SEL,0},{D1,130},{S1,0},{D2,145},{S2,0},{R2,120}},
        {{MX,0},{RG1,1},{SLP,0},{CUT,105},{RES,190},{VE,170},{SEL,0},{D1,125},{S1,0},{D2,130},{S2,150},{R2,70},{AS,AS_SOLO}}, {}},
    {"HOUSE ORGAN", SP, 48, 220, 255, F_ROOM,
        {{W1,SQR},{W2,SIN2},{RG2,2},{MX,120},{HP,60},{CUT,190},{RES,40},{VE,60},{SEL,0},{D1,130},{S1,60},{D2,150},{S2,100},{R2,90}},
        {{W1,TRI},{W2,SIN2},{RG1,1},{RG2,0},{MX,128},{CUT,110},{D2,170},{S2,230},{R2,70},{AS,AS_SOLO}}, {}},
    {"PIZZ AND STRINGS", SP, 60, 255, 230, F_D3H,
        {{TN,137},{HP,70},{CUT,175},{A2,150},{R2,170},{VL,22},{RT,170},{DL,90}},
        {{TN,136},{HP,40},{CUT,115},{VE,120},{SEL,0},{D1,125},{S1,0},{D2,140},{S2,0},{R2,100}}, {}},
    {"SUB AND SYNC BRASS", SP, 48, 220, 255, F_ROOM,
        {{SY,1},{RG2,2},{MX,170},{PE,50},{DST,DEST_VCO2},{CUT,140},{VE,90},{SEL,0},{A1,60},{D1,150},{S1,40},{A2,70},{D2,140},{S2,200},{R2,115}},
        {{W1,SAW1},{W2,SIN2},{RG1,1},{RG2,0},{MX,55},{CUT,140},{KF,100},{VE,50},{SEL,0},{D1,130},{S1,40},{D2,170},{S2,230},{R2,90},{AS,AS_SOLO}}, {}},
    {"WHEEL FILTER SPLIT", SP, 48, 230, 255, F_J1H,
        {{TN,137},{CUT,100},{RES,110},{A2,120},{R2,170}},
        {{RG1,1},{RG2,0},{TN,140},{CUT,90},{RES,100},{D2,170},{S2,230},{R2,85},{AS,AS_SOLO}},
        {{MVCF,170},{MVCFSW,1},{MVCOSW,0}}},

    // ================================================================ P56-P70: Arpeggios
    // (in Split the arpeggio plays the Lower zone, in Dual both patches)
    {"ARP SPLIT", SP, 60, 255, 230, F_J1H,
        {{TN,138},{CUT,135},{RES,20},{A2,150},{R2,180},{VL,12},{RT,160}},
        {{TN,136},{CUT,110},{VE,140},{SEL,0},{D1,125},{S1,0},{D2,150},{S2,0},{R2,130}},
        {{ARP,1},{AMODE,ARP_UP},{ARANGE,1},{ARATE,150}}},
    {"ARP CASCADE", SP, 60, 255, 230, F_D3H,
        {{TN,137},{HP,70},{CUT,175},{A2,150},{R2,170},{VL,22},{RT,170},{DL,90}},
        {{W1,TRI},{W2,SIN2},{RG2,3},{XM,90},{MX,0},{CUT,220},{D2,190},{S2,0},{R2,170}},
        {{ARP,1},{AMODE,ARP_UPDOWN},{ARANGE,2},{ARATE,160}}},
    {"ARP BASSLINE", SP, 48, 230, 255, F_ROOM,
        {{TN,136},{CUT,110},{RES,30},{VE,140},{SEL,0},{A1,85},{D1,145},{S1,110},{R1,130},{A2,75},{D2,130},{S2,200},{R2,125}},
        {{RG1,1},{RG2,0},{TN,134},{CUT,115},{RES,90},{VE,120},{SEL,0},{D1,120},{S1,10},{D2,140},{S2,0},{R2,70}},
        {{ARP,1},{AMODE,ARP_UP},{ARANGE,0},{ARATE,140}}},
    {"ARP RANDOM", SP, 60, 255, 230, F_PINGH,
        {{TN,137},{CUT,95},{RES,60},{A2,170},{R2,190}},
        {{W1,PUL1},{W2,PUL2},{TN,136},{PD,120},{PM,PW_MAN},{HP,90},{CUT,150},{RES,120},{VE,160},{SEL,0},{D1,115},{S1,0},{D2,140},{S2,0},{R2,140}},
        {{ARP,1},{AMODE,ARP_RANDOM},{ARANGE,3},{ARATE,165}}},
    {"ARP DUAL", DU, 60, 255, 230, F_PINGH,
        {{TN,136},{CUT,120},{VE,130},{SEL,0},{D1,120},{S1,0},{D2,145},{S2,0},{R2,120}},
        {{W1,SQR},{W2,SIN2},{RG1,0},{RG2,1},{MX,80},{CUT,120},{VE,90},{SEL,0},{D1,125},{S1,0},{D2,150},{S2,0},{R2,110}},
        {{ARP,1},{AMODE,ARP_UP},{ARANGE,1},{ARATE,160}}},
    {"ARP FILTER SWEEP", SP, 60, 255, 230, F_J1H,
        {{TN,138},{CUT,130},{A2,160},{R2,180}},
        {{TN,136},{CUT,100},{RES,140},{SLP,0},{FL,130},{RT,40},{VE,60},{SEL,0},{D1,120},{S1,0},{D2,140},{S2,0},{R2,110}},
        {{ARP,1},{AMODE,ARP_UPDOWN},{ARANGE,1},{ARATE,150}}},
    {"ARP ECHOES", SP, 60, 255, 230, F_PINGH,
        {{W1,TRI},{TN,136},{MX,90},{CUT,140},{A2,170},{R2,190}},
        {{W1,SQR},{MX,0},{CUT,150},{VE,110},{SEL,0},{D1,115},{S1,0},{D2,140},{S2,0},{R2,100}},
        {{ARP,1},{AMODE,ARP_DOWN},{ARANGE,2},{ARATE,120}}},
    {"ARP SYNC", SP, 60, 255, 230, F_D3H,
        {{TN,137},{HP,70},{CUT,175},{A2,150},{R2,170}},
        {{SY,1},{RG2,3},{MX,220},{PE,100},{DST,DEST_VCO2},{A1,0},{D1,140},{S1,0},{CUT,190},{D2,150},{S2,0},{R2,110}},
        {{ARP,1},{AMODE,ARP_UPDOWN},{ARANGE,1},{ARATE,155}}},
    {"ARP AND GLIDE LEAD", SP, 60, 220, 255, F_PING,
        {{TN,134},{CUT,175},{RES,40},{VL,14},{RT,175},{DL,120},{AS,AS_SOLO}},
        {{RG1,1},{RG2,0},{TN,134},{CUT,120},{RES,70},{VE,120},{SEL,0},{D1,120},{S1,0},{D2,140},{S2,0},{R2,80}},
        {{ARP,1},{AMODE,ARP_UP},{ARANGE,1},{ARATE,145},{PORTA,PM_UPPER},{PTIME,140}}},
    {"ARP TWINKLE", SP, 60, 230, 255, F_SHIMJ,
        {{W1,TRI},{W2,SIN2},{RG2,2},{MX,90},{CUT,170},{A2,200},{R2,210}},
        {{W1,TRI},{W2,SIN2},{RG1,3},{RG2,3},{XM,30},{MX,40},{CUT,230},{D2,170},{S2,0},{R2,160}},
        {{ARP,1},{AMODE,ARP_UP},{ARANGE,3},{ARATE,185}}},
    {"ARP RESO", SP, 60, 230, 255, F_PING,
        {{TN,136},{CUT,100},{VE,150},{SEL,0},{A1,165},{D1,180},{S1,120},{R1,150},{A2,155},{D2,170},{S2,230},{R2,145}},
        {{MX,40},{SLP,0},{CUT,90},{RES,200},{VE,150},{SEL,0},{D1,125},{S1,0},{D2,140},{S2,0},{R2,100}},
        {{ARP,1},{AMODE,ARP_UPDOWN},{ARANGE,1},{ARATE,165}}},
    {"ARP KEYS", SP, 60, 230, 255, F_ROOM,
        {{W1,SQR},{W2,SIN2},{RG2,2},{MX,110},{HP,30},{CUT,220},{D2,100},{S2,255},{R2,50},{VL,8},{RT,175}},
        {{W1,PUL1},{MX,0},{PD,150},{PM,PW_MAN},{HP,70},{CUT,130},{RES,70},{VE,120},{SEL,0},{D1,105},{S1,0},{D2,135},{S2,0},{R2,70}},
        {{ARP,1},{AMODE,ARP_UP},{ARANGE,1},{ARATE,135}}},
    {"ARP XMOD", SP, 60, 255, 230, F_HALL2,
        {{TN,137},{CUT,95},{RES,60},{FL,60},{RT,80},{A2,150},{R2,190}},
        {{XM,90},{W2,SIN2},{RG2,2},{MX,60},{CUT,170},{VE,80},{SEL,0},{D1,120},{S1,0},{D2,140},{S2,0},{R2,110}},
        {{ARP,1},{AMODE,ARP_RANDOM},{ARANGE,2},{ARATE,150}}},
    {"ARP BELL DUAL", DU, 60, 255, 220, F_HALL,
        {{W1,TRI},{W2,SIN2},{RG2,3},{XM,110},{MX,0},{CUT,230},{D2,180},{S2,0},{R2,170}},
        {{W1,TRI},{W2,SIN2},{RG1,0},{RG2,0},{MX,100},{CUT,140},{D2,160},{S2,0},{R2,130}},
        {{ARP,1},{AMODE,ARP_UPDOWN},{ARANGE,2},{ARATE,140}}},
    {"ARP SLOW TRIANGLE", SP, 60, 255, 230, F_D3H,
        {{TN,137},{HP,70},{CUT,165},{A2,160},{R2,180},{VL,20},{RT,165},{DL,120}},
        {{W1,TRI},{W2,SIN2},{RG2,2},{MX,80},{CUT,170},{D2,175},{S2,60},{R2,150}},
        {{ARP,1},{AMODE,ARP_UPDOWN},{ARANGE,2},{ARATE,95}}},

    // ================================================================ P71-P80: Leads, two patches stacked
    {"UNISON STACK", DU, 60, 255, 230, F_PING,
        {{TN,142},{CUT,175},{RES,25},{AS,AS_UNISON}},
        {{W1,SQR},{W2,PUL2},{RG1,0},{RG2,1},{TN,124},{CUT,140},{AS,AS_UNISON}},
        {{PORTA,PM_ON},{PTIME,110}}},
    {"SYNC DUO", DU, 60, 255, 220, F_PINGH,
        {{SY,1},{RG2,2},{MX,255},{PE,120},{DST,DEST_VCO2},{D1,150},{S1,40},{CUT,210},{VL,14},{RT,175},{DL,120},{AS,AS_SOLO}},
        {{RG1,0},{RG2,1},{TN,136},{CUT,150},{RES,30},{AS,AS_SOLO}},
        {{PORTA,PM_ON},{PTIME,130}}},
    {"WAH LEAD", DU, 60, 255, 210, F_PING,
        {{MX,60},{SLP,0},{CUT,110},{RES,180},{VE,110},{SEL,0},{A1,90},{D1,150},{S1,40},{AS,AS_SOLO}},
        {{W1,TRI},{W2,SIN2},{RG1,0},{RG2,0},{MX,128},{CUT,120},{AS,AS_SOLO}},
        {{PORTA,PM_ON},{PTIME,110},{MVCF,140},{MVCFSW,1}}},
    {"VIBRATO DUET", DU, 60, 255, 240, F_PLATE,
        {{W1,SQR},{W2,PUL2},{TN,136},{CUT,170},{VL,18},{RT,172},{DL,100},{AS,AS_SOLO}},
        {{W1,TRI},{W2,SIN2},{RG1,0},{RG2,1},{MX,90},{CUT,150},{VL,20},{RT,160},{DL,160},{AS,AS_SOLO}},
        {{PORTA,PM_ON},{PTIME,120}}},
    {"SCREAM LEAD", DU, 60, 255, 220, F_ODP,
        {{SY,1},{RG2,2},{XM,60},{MX,220},{PE,90},{DST,DEST_VCO2},{D1,170},{S1,60},{CUT,190},{RES,60},{AS,AS_SOLO}},
        {{TN,138},{RG1,0},{RG2,1},{CUT,160},{RES,40},{VL,14},{RT,175},{DL,140},{AS,AS_SOLO}},
        {{PORTA,PM_ON},{PTIME,100}}},
    {"WHEEL LEAD", DU, 60, 255, 220, F_PINGH,
        {{TN,134},{CUT,120},{RES,120},{SLP,0},{AS,AS_SOLO}},
        {{W1,PUL1},{W2,PUL2},{PD,100},{PM,PW_LFO},{RT,165},{RG1,0},{RG2,1},{CUT,110},{RES,60},{AS,AS_SOLO}},
        {{MVCF,200},{MVCFSW,1},{MVCOSW,0},{MRISE,60}}},
    {"SQUARE DUO", DU, 60, 255, 255, F_PING,
        {{W1,SQR},{W2,PUL2},{PD,60},{PM,PW_MAN},{TN,137},{CUT,165},{AS,AS_SOLO}},
        {{W1,PUL1},{W2,PUL2},{PD,160},{PM,PW_MAN},{RG1,0},{RG2,1},{TN,120},{CUT,140},{AS,AS_SOLO}},
        {{PORTA,PM_ON},{PTIME,115}}},
    {"FLUTE AND STRINGS", DU, 60, 255, 200, F_HALL,
        {{W1,TRI},{W2,NOI},{MX,35},{CUT,155},{A2,70},{R2,130},{VL,16},{RT,175},{DL,130}},
        {{TN,137},{HP,80},{CUT,165},{A2,170},{R2,180},{VL,20},{RT,168},{DL,120}}, {}},
    {"SOFT SOLO", DU, 60, 255, 220, F_PLATE,
        {{W1,TRI},{W2,SIN2},{MX,100},{CUT,150},{A2,40},{R2,110},{VL,14},{RT,175},{DL,120},{AS,AS_SOLO}},
        {{W1,TRI},{W2,SIN2},{RG1,0},{RG2,0},{MX,200},{CUT,120},{A2,60},{R2,120},{AS,AS_SOLO}},
        {{PORTA,PM_ON},{PTIME,140}}},
    {"BRASS LEAD", DU, 60, 255, 230, F_ROOM,
        {{TN,136},{CUT,120},{RES,40},{VE,150},{SEL,0},{A1,60},{D1,150},{S1,90},{A2,40},{D2,130},{S2,220},{R2,100},{VL,16},{RT,172},{DL,140},{AS,AS_SOLO}},
        {{W1,PUL1},{W2,SAW2},{PD,90},{PM,PW_MAN},{RG1,0},{RG2,1},{CUT,110},{VE,120},{SEL,0},{A1,70},{D1,150},{S1,90},{AS,AS_SOLO}},
        {{PORTA,PM_ON},{PTIME,90}}},

    // ================================================================ P81-P100: Movement and textures
    {"RISING TIDE", DU, 60, 230, 255, F_HALL2,
        {{W2,NOI},{MX,120},{CUT,50},{RES,120},{VE,200},{SEL,0},{A1,240},{D1,230},{S1,255},{R1,170},{A2,230},{R2,170}},
        {{TN,138},{CUT,110},{RES,40},{VE,60},{SEL,0},{A1,220},{D1,220},{S1,80},{R1,210},{A2,200},{R2,200}}, {}},
    {"SPACE STATION", DU, 60, 255, 220, F_PINGH,
        {{W2,NOI},{MX,90},{CUT,150},{RES,190},{FL,150},{LW,LFO_RANDOM},{RT,165},{A2,120},{R2,180}},
        {{RG1,0},{RG2,0},{V2L,1},{TN,128},{XM,30},{CUT,100},{RES,60},{A2,150},{R2,200}}, {}},
    {"HELICOPTER", DU, 60, 255, 230, F_ROOM,
        {{W2,NOI},{MX,255},{CUT,195},{RES,60},{KA,3},{LW,LFO_SQUARE},{RT,175},{A2,100},{R2,150}},
        {{W2,NOI},{MX,255},{CUT,145},{RES,80},{FL,90},{RT,100},{A2,150},{R2,170}}, {}},
    {"ALIEN TALK", DU, 60, 255, 220, F_PINGH,
        {{VL,90},{LW,LFO_RANDOM},{RT,170},{CUT,150},{RES,150},{FL,80},{AS,AS_POLY1},{A2,40},{R2,120}},
        {{W1,TRI},{W2,SIN2},{RG1,0},{RG2,2},{XM,140},{MX,0},{CUT,140},{A2,150},{R2,180}}, {}},
    {"TWIN SIRENS", DU, 60, 255, 255, F_ROOM,
        {{MX,0},{VL,190},{RT,120},{CUT,190},{A2,50},{R2,120}},
        {{W1,SQR},{MX,0},{VL,170},{RT,95},{LW,LFO_SAW},{RG1,0},{CUT,170},{A2,50},{R2,120}}, {}},
    {"WIND AND BELLS", DU, 60, 255, 200, F_HALL2,
        {{W1,TRI},{W2,SIN2},{RG2,3},{XM,120},{MX,0},{CUT,230},{D2,190},{S2,0},{R2,180},{HP,50}},
        {{W2,NOI},{MX,255},{CUT,175},{RES,20},{FL,150},{RT,50},{A2,190},{R2,205}}, {}},
    {"THUNDER", DU, 60, 230, 255, F_HALL2,
        {{W2,NOI},{MX,255},{CUT,140},{RES,40},{VE,90},{SEL,0},{A1,0},{D1,210},{S1,0},{A2,0},{D2,225},{S2,0},{R2,220}},
        {{W2,NOI},{MX,255},{CUT,105},{RES,150},{VE,90},{SEL,0},{A1,60},{D1,230},{S1,40},{A2,30},{D2,235},{S2,60},{R2,230}}, {}},
    {"COMPUTER BLIPS", DU, 60, 255, 220, F_PING,
        {{W1,SQR},{MX,0},{RG1,3},{PE,200},{D1,110},{S1,0},{CUT,230},{D2,120},{S2,0},{R2,90}},
        {{W2,SIN2},{MX,255},{RG2,2},{XM,0},{CUT,230},{D2,125},{S2,0},{R2,90}},
        {{ARP,1},{AMODE,ARP_RANDOM},{ARANGE,3},{ARATE,200}}},
    {"CHOPPED PAD", DU, 60, 255, 230, F_J2PH,
        {{TN,138},{CUT,150},{RES,50},{FL,90},{LW,LFO_SQUARE},{RT,160},{KA,3},{A2,120},{R2,160}},
        {{W1,PUL1},{W2,PUL2},{PD,140},{PM,PW_LFO},{RT,100},{RG1,0},{RG2,1},{CUT,130},{A2,150},{R2,180}}, {}},
    {"PULSAR", DU, 60, 255, 230, F_PH1H,
        {{W1,PUL1},{W2,PUL2},{PD,180},{PM,PW_LFO},{RT,190},{TN,133},{CUT,180},{RES,40},{A2,100},{R2,160}},
        {{W1,PUL1},{MX,0},{RG1,0},{PD,120},{PM,PW_LFO},{RT,60},{CUT,110},{A2,130},{R2,170}}, {}},
    {"METAL RAIN", DU, 60, 230, 255, F_PINGH,
        {{W1,TRI},{W2,SIN2},{RG1,2},{RG2,3},{TN,175},{XM,160},{MX,0},{CUT,230},{D2,160},{S2,0},{R2,150}},
        {{W2,NOI},{MX,255},{CUT,170},{RES,200},{SLP,0},{VE,80},{SEL,0},{D1,150},{S1,0},{KF,255},{D2,175},{S2,0},{R2,150}}, {}},
    {"DEEP SPACE", DU, 60, 255, 240, F_SHIM,
        {{W1,TRI},{W2,SIN2},{RG2,2},{MX,128},{CUT,160},{FL,70},{RT,30},{A2,220},{R2,220}},
        {{RG1,0},{RG2,0},{TN,124},{CUT,90},{RES,110},{FL,100},{RT,22},{A2,220},{R2,225}}, {}},
    {"SWEEPING STRINGS", DU, 60, 255, 240, F_D3H,
        {{TN,137},{HP,60},{CUT,130},{RES,70},{FL,140},{RT,32},{A2,150},{R2,175}},
        {{TN,136},{HP,40},{CUT,180},{RES,50},{VE,90},{SEL,0},{POL,1},{A1,200},{D1,220},{S1,40},{R1,200},{A2,160},{R2,180}}, {}},
    {"BREATHING PAD", DU, 60, 255, 255, F_CEH,
        {{TN,138},{CUT,130},{RES,40},{FL,90},{RT,70},{KA,1},{A2,160},{R2,185}},
        {{W1,TRI},{W2,SAW2},{TN,124},{MX,140},{CUT,120},{FL,90},{RT,58},{A2,170},{R2,190}}, {}},
    {"ROBOT CHOIR", DU, 60, 255, 230, F_PHH,
        {{W1,PUL1},{MX,0},{PD,120},{PM,PW_MAN},{LW,LFO_SQUARE},{RT,180},{FL,100},{CUT,120},{RES,110},{A2,80},{R2,150}},
        {{W1,SQR},{W2,SIN2},{RG2,2},{MX,90},{HP,90},{CUT,180},{A2,150},{R2,180}}, {}},
    {"DRONE MACHINE", DU, 60, 255, 255, F_LONG,
        {{RG2,1},{V2L,1},{W2,SIN2},{XM,70},{MX,0},{CUT,170},{RES,40},{A2,200},{R2,210}},
        {{RG1,0},{RG2,0},{TN,121},{CUT,100},{RES,80},{FL,60},{RT,25},{A2,200},{R2,220}}, {}},
    {"FM TOWERS", DU, 60, 255, 230, F_HALL2,
        {{W1,TRI},{W2,SIN2},{RG2,3},{XM,200},{MX,0},{CUT,210},{A2,150},{R2,200},{VL,10},{RT,150}},
        {{W1,TRI},{W2,SIN2},{RG1,0},{RG2,2},{XM,150},{MX,0},{CUT,170},{FL,60},{RT,40},{A2,170},{R2,210}}, {}},
    {"BOWED METAL", DU, 60, 230, 255, F_HALL2,
        {{W1,TRI},{W2,SIN2},{RG1,1},{RG2,3},{TN,175},{XM,100},{MX,0},{CUT,200},{A2,200},{R2,210}},
        {{TN,137},{CUT,80},{RES,190},{SLP,0},{FL,60},{RT,45},{A2,190},{R2,205}}, {}},
    {"COSMIC ARP", DU, 60, 255, 230, F_SHIMJ,
        {{W1,TRI},{W2,SIN2},{RG2,3},{XM,60},{MX,0},{CUT,220},{D2,170},{S2,0},{R2,160}},
        {{W1,PUL1},{MX,0},{PD,100},{PM,PW_LFO},{RT,90},{CUT,150},{RES,80},{VE,100},{SEL,0},{D1,120},{S1,0},{D2,145},{S2,0},{R2,130}},
        {{ARP,1},{AMODE,ARP_RANDOM},{ARANGE,3},{ARATE,175}}},
    {"GRAND FINALE", DU, 60, 255, 230, F_D3H,
        {{TN,136},{CUT,120},{RES,30},{VE,150},{SEL,0},{A1,90},{D1,150},{S1,110},{R1,140},{A2,80},{D2,140},{S2,210},{R2,150}},
        {{TN,137},{HP,60},{CUT,180},{A2,150},{R2,190},{VL,22},{RT,170},{DL,90}}, {}},
    };
    return d;
}

}  // namespace

void buildPerformances(Performance* out, const uint8_t* defaults) {
    const std::vector<Def>& d = defs();
    for (int i = 0; i < kNumPerformances && i < (int)d.size(); ++i) {
        Performance& p = out[i];
        std::memset(p.name, 0, sizeof(p.name));
        std::strncpy(p.name, d[i].name, sizeof(p.name) - 1);
        initPatch(p.upper, d[i].name);
        initPatch(p.lower, d[i].name);
        for (const Pv& v : d[i].up) p.upper.v[v.id] = v.val;
        for (const Pv& v : d[i].lo) p.lower.v[v.id] = v.val;
        p.upper.v[P_VCA_LEVEL] = kPerformanceLevel[i][0];
        p.lower.v[P_VCA_LEVEL] = kPerformanceLevel[i][1];
        applyFx(p.upper.fx, d[i].fx);
        std::memcpy(p.global, defaults, G_COUNT);
        p.global[G_KEYMODE] = d[i].km;
        p.global[G_SPLIT] = d[i].split;
        p.global[G_BAL_U] = d[i].balU;
        p.global[G_BAL_L] = d[i].balL;
        for (const Pv& v : d[i].g) p.global[v.id] = v.val;
        p.stereoLayers = true;
    }
}

// For the tests: how many performances are written, and the first one that sets a control twice (or nullptr).
int performanceCount() { return (int)defs().size(); }
const char* performanceWithDuplicate() {
    for (const Def& d : defs())
        for (const std::vector<Pv>* l : {&d.up, &d.lo, &d.g})
            for (size_t a = 0; a < l->size(); ++a)
                for (size_t b = a + 1; b < l->size(); ++b)
                    if ((*l)[a].id == (*l)[b].id) return d.name;
    return nullptr;
}

}  // namespace j8
