// Factory bank: 64 starting sounds, numbered 11-88 by bank and patch.
// These are original sounds, not copies of Roland's factory patches.
#include "params.h"
#include <cstdio>
#include <cstring>

namespace j8 {
namespace {

struct Pv { uint8_t id, val; };

// Short names so each sound fits on a line or two.
constexpr uint8_t CUT = P_VCF_CUT, RES = P_VCF_RES, VE = P_VCF_ENV, SEL = P_VCF_ENVSEL, KF = P_VCF_KEY, FL = P_VCF_LFO, SLP = P_VCF_SLOPE,
    HP = P_HPF, MX = P_MIX, W1 = P_V1_WAVE, W2 = P_V2_WAVE, RG1 = P_V1_RANGE, RG2 = P_V2_RANGE, TN = P_V2_TUNE, XM = P_V1_XMOD, SY = P_V2_SYNC,
    PD = P_VM_PW, PM = P_VM_PWMODE, VL = P_VM_LFO, PE = P_VM_ENV, DST = P_VM_DEST, RT = P_LFO_RATE, DL = P_LFO_DELAY, LW = P_LFO_WAVE,
    A1 = P_E1_A, D1 = P_E1_D, S1 = P_E1_S, R1 = P_E1_R, A2 = P_E2_A, D2 = P_E2_D, S2 = P_E2_S, R2 = P_E2_R, AS = P_ASSIGN, KA = P_VCA_LFO;
constexpr uint8_t TRI = V1_TRI, SAW1 = V1_SAW, PUL1 = V1_PULSE, SQR = V1_SQUARE, SIN2 = V2_SINE, SAW2 = V2_SAW, PUL2 = V2_PULSE, NOI = V2_NOISE;

enum FxSet { F_NONE, F_J1, F_J2, F_J1H, F_J2H, F_J12H, F_D2, F_D2H, F_D3H, F_CE, F_CEH, F_HALL, F_HALL2, F_PLATE, F_ROOM,
             F_PING, F_PINGH, F_TAP, F_SHIM, F_ODP, F_FATR, F_PHH };

void applyFx(FxPatch& f, int set) {
    auto mfx = [&](int type) { fxLoadMfxDefaults(f, type); f.mfxOn = 1; };
    auto rev = [&](int type, int time, int send) { f.revOn = 1; f.r[RV_TYPE] = (int16_t)type; f.r[RV_TIME] = (int16_t)time; f.r[RV_SEND] = (int16_t)send; };
    auto ping = [&](int note, int send, int fb) { f.dlyOn = 1; f.d[DL_MODE] = DLM_PING; f.d[DL_SYNC] = 1; f.d[DL_NOTE] = (int16_t)note; f.d[DL_SEND] = (int16_t)send; f.d[DL_FB] = (int16_t)fb; };
    switch (set) {
        case F_J1: mfx(MFX_CHO1); break;
        case F_J2: mfx(MFX_CHO1); f.m[C1_MODE] = 1; break;
        case F_J1H: mfx(MFX_CHO1); rev(RVT_HALL1, 70, 40); break;
        case F_J2H: mfx(MFX_CHO1); f.m[C1_MODE] = 1; rev(RVT_HALL1, 70, 40); break;
        case F_J12H: mfx(MFX_CHO1); f.m[C1_MODE] = 2; rev(RVT_HALL1, 65, 35); break;
        case F_D2: mfx(MFX_CHO3); f.m[C3_MODE] = 1; break;
        case F_D2H: mfx(MFX_CHO3); f.m[C3_MODE] = 1; rev(RVT_HALL1, 70, 40); break;
        case F_D3H: mfx(MFX_CHO3); f.m[C3_MODE] = 2; rev(RVT_HALL2, 70, 45); break;
        case F_CE: mfx(MFX_CHO2); f.m[C2_INT] = 90; break;
        case F_CEH: mfx(MFX_CHO2); f.m[C2_INT] = 90; rev(RVT_HALL1, 70, 40); break;
        case F_HALL: rev(RVT_HALL1, 75, 50); break;
        case F_HALL2: rev(RVT_HALL2, 80, 55); break;
        case F_PLATE: rev(RVT_PLATE, 65, 50); break;
        case F_ROOM: rev(RVT_ROOM2, 75, 40); break;
        case F_PING: ping(11, 60, 40); break;
        case F_PINGH: ping(11, 60, 45); rev(RVT_HALL1, 65, 30); break;
        case F_TAP: f.dlyOn = 1; f.d[DL_MODE] = DLM_2TAP; f.d[DL_SYNC] = 1; f.d[DL_NOTE] = 12; f.d[DL_SEND] = 60; f.d[DL_FB] = 40; break;
        case F_SHIM: f.revOn = 1; f.r[RV_MODE] = RVM_SHIMMER; f.r[RV_SEND] = 70; f.r[RS_TIME] = 50; break;
        case F_ODP: mfx(MFX_OD); f.m[OD_DIST] = 45; ping(9, 50, 40); break;
        case F_FATR: mfx(MFX_FATTEN); f.m[FT_ODD] = 120; f.m[FT_EVEN] = 150; break;
        case F_PHH: mfx(MFX_PHA2); f.m[P2_RATE] = 25; rev(RVT_PLATE, 60, 35); break;
        default: break;
    }
}

struct Preset { const char* name; int fx; const Pv* list; int n; };
#define SOUND(sym, ...) const Pv sym[] = {__VA_ARGS__}
#define E(nm, fx, sym) {nm, fx, sym, (int)(sizeof(sym) / sizeof(sym[0]))}

// ---- 1x basses
SOUND(b1, {W1,SAW1},{W2,SIN2},{RG1,1},{RG2,0},{MX,55},{CUT,140},{KF,100},{VE,50},{SEL,0},{D1,130},{S1,40},{D2,170},{S2,230},{R2,90},{AS,AS_SOLO});
SOUND(b2, {RG1,1},{RG2,0},{TN,140},{CUT,150},{RES,50},{VE,110},{SEL,0},{D1,130},{S1,70},{R1,100},{D2,160},{S2,200},{R2,85},{AS,AS_SOLO});
SOUND(b3, {W1,SQR},{W2,SIN2},{RG1,1},{RG2,0},{MX,50},{CUT,145},{KF,100},{VE,60},{SEL,0},{D1,140},{S1,60},{D2,170},{S2,210},{R2,85},{AS,AS_SOLO});
SOUND(b4, {MX,0},{RG1,1},{SLP,0},{CUT,105},{RES,190},{VE,170},{SEL,0},{D1,125},{S1,0},{D2,130},{S2,150},{R2,70},{AS,AS_SOLO});
SOUND(b5, {RG1,1},{RG2,0},{TN,132},{CUT,115},{RES,80},{VE,130},{SEL,0},{D1,120},{S1,30},{R1,90},{D2,140},{S2,190},{R2,75},{AS,AS_UNISON});
SOUND(b6, {SY,1},{RG1,1},{RG2,3},{MX,200},{PE,90},{DST,DEST_VCO2},{CUT,150},{VE,40},{SEL,0},{D1,125},{S1,0},{D2,150},{S2,170},{R2,80},{AS,AS_SOLO});
SOUND(b7, {W1,PUL1},{W2,SAW2},{RG1,1},{RG2,0},{PD,90},{PM,PW_MAN},{CUT,155},{VE,150},{SEL,0},{D1,125},{S1,50},{D2,180},{S2,140},{R2,90},{AS,AS_SOLO});
SOUND(b8, {XM,60},{W2,SIN2},{RG1,1},{RG2,2},{MX,70},{CUT,155},{VE,80},{SEL,0},{D1,130},{S1,0},{D2,160},{S2,180},{R2,85},{AS,AS_SOLO});
// ---- 2x pads
SOUND(p1, {TN,138},{CUT,135},{RES,20},{VE,30},{SEL,0},{A1,150},{D1,190},{S1,90},{R1,180},{A2,130},{R2,175},{VL,12},{RT,160});
SOUND(p2, {W1,PUL1},{W2,PUL2},{TN,134},{PD,150},{PM,PW_LFO},{RT,120},{CUT,150},{RES,35},{VE,40},{SEL,0},{D1,170},{A2,140},{R2,175});
SOUND(p3, {W1,TRI},{TN,136},{MX,100},{CUT,105},{A2,160},{R2,180},{VL,10},{RT,150});
SOUND(p4, {TN,140},{CUT,60},{RES,110},{VE,120},{SEL,0},{A1,205},{D1,215},{S1,70},{R1,205},{A2,165},{R2,195});
SOUND(p5, {W1,SQR},{W2,SIN2},{RG2,3},{XM,25},{MX,90},{HP,110},{CUT,200},{A2,170},{R2,200});
SOUND(p6, {RG1,0},{RG2,1},{TN,134},{CUT,110},{KF,80},{A2,175},{R2,190});
SOUND(p7, {W2,NOI},{MX,110},{HP,140},{CUT,190},{FL,40},{RT,70},{A2,190},{R2,205});
SOUND(p8, {W1,TRI},{W2,SIN2},{RG2,2},{MX,90},{CUT,160},{A2,170},{R2,190});
// ---- 3x strings
SOUND(s1, {TN,137},{HP,70},{CUT,175},{RES,10},{A2,150},{R2,170},{VL,22},{RT,170},{DL,90});
SOUND(s2, {TN,145},{HP,85},{CUT,190},{A2,135},{R2,165});
SOUND(s3, {TN,138},{HP,60},{CUT,150},{A2,185},{R2,190},{VL,18},{RT,165},{DL,140});
SOUND(s4, {TN,136},{HP,95},{CUT,215},{A2,120},{R2,150},{VL,20},{RT,175},{DL,80});
SOUND(s5, {RG1,1},{RG2,0},{TN,134},{HP,30},{CUT,145},{VE,30},{SEL,0},{A1,120},{D1,160},{A2,125},{R2,130},{VL,28},{RT,165},{DL,120});
SOUND(s6, {TN,136},{HP,40},{CUT,115},{VE,120},{SEL,0},{D1,125},{S1,0},{D2,140},{S2,0},{R2,100});
SOUND(s7, {W1,PUL1},{TN,136},{PD,100},{PM,PW_LFO},{RT,130},{HP,50},{CUT,170},{A2,140},{R2,170});
SOUND(s8, {RG1,2},{RG2,2},{TN,136},{HP,110},{CUT,210},{A2,160},{R2,185},{VL,16},{RT,170},{DL,100});
// ---- 4x brass
SOUND(r1, {TN,136},{CUT,110},{RES,30},{VE,140},{SEL,0},{A1,85},{D1,145},{S1,110},{R1,130},{A2,75},{D2,130},{S2,200},{R2,125});
SOUND(r2, {TN,134},{CUT,90},{RES,20},{VE,100},{SEL,0},{A1,110},{D1,160},{S1,110},{R1,140},{A2,100},{D2,140},{S2,210},{R2,135});
SOUND(r3, {W1,PUL1},{TN,136},{PD,60},{PM,PW_MAN},{CUT,120},{VE,130},{SEL,0},{A1,70},{D1,140},{S1,110},{A2,60},{D2,130},{S2,210},{R2,115});
SOUND(r4, {TN,136},{CUT,120},{VE,150},{SEL,0},{A1,40},{D1,125},{S1,60},{R1,110},{A2,30},{D2,130},{S2,150},{R2,95});
SOUND(r5, {TN,136},{CUT,100},{VE,150},{SEL,0},{A1,165},{D1,180},{S1,120},{R1,150},{A2,155},{D2,170},{S2,230},{R2,145});
SOUND(r6, {RG1,0},{RG2,1},{TN,132},{CUT,95},{RES,25},{VE,120},{SEL,0},{A1,90},{D1,150},{S1,100},{A2,80},{D2,140},{S2,210},{R2,120});
SOUND(r7, {W1,PUL1},{W2,PUL2},{TN,134},{PD,110},{PM,PW_MAN},{HP,90},{CUT,100},{RES,60},{VE,90},{SEL,0},{A1,80},{D1,140},{S1,90},{A2,70},{R2,110});
SOUND(r8, {SY,1},{RG2,2},{MX,170},{PE,50},{DST,DEST_VCO2},{CUT,140},{VE,90},{SEL,0},{A1,60},{D1,150},{S1,40},{A2,70},{D2,140},{S2,200},{R2,115});
// ---- 5x keys
SOUND(k1, {W1,TRI},{W2,SIN2},{RG2,3},{XM,28},{MX,50},{CUT,180},{D2,190},{S2,0},{R2,130});
SOUND(k2, {W1,PUL1},{MX,0},{PD,150},{PM,PW_MAN},{HP,70},{CUT,130},{RES,70},{VE,120},{SEL,0},{D1,105},{S1,0},{D2,135},{S2,0},{R2,70});
SOUND(k3, {W1,SQR},{W2,SIN2},{RG2,2},{MX,110},{HP,30},{CUT,220},{D2,100},{S2,255},{R2,50},{VL,8},{RT,175});
SOUND(k4, {W1,SQR},{TN,133},{MX,100},{CUT,90},{VE,100},{SEL,0},{D1,140},{S1,30},{D2,165},{S2,60},{R2,130});
SOUND(k5, {W2,PUL2},{RG2,2},{PD,170},{PM,PW_MAN},{HP,90},{CUT,190},{D2,165},{S2,0},{R2,90});
SOUND(k6, {TN,136},{CUT,150},{VE,110},{SEL,0},{D1,120},{S1,20},{D2,150},{S2,80},{R2,110});
SOUND(k7, {W1,TRI},{W2,SIN2},{RG2,2},{MX,80},{CUT,190},{D2,190},{S2,0},{R2,185},{KA,1},{RT,165});
SOUND(k8, {TN,136},{CUT,110},{VE,140},{SEL,0},{D1,125},{S1,0},{D2,150},{S2,0},{R2,130});
// ---- 6x leads
SOUND(l1, {SY,1},{RG2,2},{MX,255},{PE,110},{DST,DEST_VCO2},{D1,135},{S1,0},{CUT,215},{AS,AS_SOLO});
SOUND(l2, {TN,134},{CUT,170},{RES,40},{VE,50},{SEL,0},{D1,140},{S1,60},{VL,14},{RT,175},{DL,120},{AS,AS_SOLO});
SOUND(l3, {W1,SQR},{W2,PUL2},{TN,136},{CUT,160},{VL,14},{RT,175},{DL,120},{AS,AS_SOLO});
SOUND(l4, {W1,TRI},{W2,SIN2},{MX,100},{CUT,150},{A2,40},{R2,110},{VL,14},{RT,175},{DL,120},{AS,AS_SOLO});
SOUND(l5, {TN,140},{CUT,180},{RES,20},{AS,AS_UNISON});
SOUND(l6, {TN,134},{CUT,180},{RES,60},{VL,12},{RT,175},{DL,120},{AS,AS_SOLO});
SOUND(l7, {MX,60},{SLP,0},{CUT,130},{RES,170},{VE,70},{SEL,0},{D1,130},{S1,40},{AS,AS_SOLO});
SOUND(l8, {W1,PUL1},{XM,70},{MX,60},{PD,60},{PM,PW_MAN},{CUT,170},{VE,40},{SEL,0},{D1,140},{AS,AS_SOLO});
// ---- 7x bells and plucks
SOUND(c1, {W1,TRI},{W2,SIN2},{RG2,3},{XM,120},{MX,0},{CUT,230},{D2,190},{S2,0},{R2,180},{HP,50});
SOUND(c2, {W1,TRI},{W2,SIN2},{RG2,3},{TN,200},{XM,60},{MX,0},{HP,60},{CUT,220},{D2,200},{S2,0},{R2,190});
SOUND(c3, {W1,TRI},{W2,SIN2},{RG1,1},{RG2,3},{TN,175},{XM,90},{MX,0},{CUT,220},{D2,215},{S2,0},{R2,205});
SOUND(c4, {W1,TRI},{W2,SIN2},{RG2,2},{MX,60},{CUT,195},{D2,145},{S2,0},{R2,125});
SOUND(c5, {W1,TRI},{W2,SIN2},{XM,20},{MX,70},{CUT,210},{D2,145},{S2,0},{R2,130});
SOUND(c6, {W1,TRI},{W2,SIN2},{RG1,3},{RG2,3},{XM,30},{MX,40},{CUT,230},{D2,175},{S2,0},{R2,170});
SOUND(c7, {W1,PUL1},{W2,PUL2},{TN,136},{PD,120},{PM,PW_MAN},{HP,90},{CUT,145},{RES,120},{VE,170},{SEL,0},{D1,115},{S1,0},{D2,150},{S2,0},{R2,150});
SOUND(c8, {MX,40},{CUT,125},{VE,130},{SEL,0},{D1,120},{S1,0},{D2,145},{S2,0},{R2,110});
// ---- 8x effects and movement
SOUND(x1, {TN,136},{CUT,40},{RES,190},{SLP,0},{VE,170},{SEL,0},{A1,170},{D1,190},{S1,40},{R1,170},{R2,160});
SOUND(x2, {W2,NOI},{MX,255},{CUT,165},{RES,120},{FL,110},{RT,75},{A2,170},{R2,200});
SOUND(x3, {W2,NOI},{MX,255},{CUT,175},{RES,20},{FL,150},{RT,50},{A2,190},{R2,205});
SOUND(x4, {RG2,1},{P_V2_LOW,1},{W2,SIN2},{XM,55},{MX,0},{CUT,180},{R2,130});
SOUND(x5, {MX,0},{VL,200},{RT,130},{CUT,190},{A2,50},{R2,120});
SOUND(x6, {W1,SQR},{MX,0},{PE,220},{D1,125},{S1,0},{CUT,230},{D2,140},{S2,0},{R2,100});
SOUND(x7, {W1,PUL1},{MX,0},{PD,120},{PM,PW_MAN},{LW,LFO_SQUARE},{RT,190},{FL,100},{CUT,120},{RES,90});
SOUND(x8, {W2,NOI},{MX,120},{CUT,50},{RES,120},{VE,200},{SEL,0},{A1,240},{D1,230},{S1,255},{R1,170},{A2,230},{R2,170});

const Preset kPresets[64] = {
    E("DEEP SUB",F_NONE,b1), E("FAT SAW",F_NONE,b2), E("SOFT SQUARE",F_NONE,b3), E("RESO ACID",F_NONE,b4),
    E("UNISON BASS",F_NONE,b5), E("SYNC BASS",F_NONE,b6), E("PLUCK BASS",F_ROOM,b7), E("XMOD BASS",F_NONE,b8),
    E("JUPITER PAD",F_J1H,p1), E("PWM PAD",F_J1H,p2), E("WARM PAD",F_CEH,p3), E("SLOW SWEEP",F_J2H,p4),
    E("GLASS PAD",F_D3H,p5), E("DARK PAD",F_HALL2,p6), E("AIR PAD",F_PLATE,p7), E("SHIMMER PAD",F_SHIM,p8),
    E("STRINGS",F_D3H,s1), E("ENSEMBLE",F_J12H,s2), E("SLOW STRING",F_D2H,s3), E("BRIGHT STR",F_D2H,s4),
    E("CELLO",F_ROOM,s5), E("PIZZICATO",F_ROOM,s6), E("80S STRINGS",F_J2H,s7), E("HIGH STRING",F_PLATE,s8),
    E("BRASS SECT",F_ROOM,r1), E("SOFT BRASS",F_ROOM,r2), E("POLY BRASS",F_J1,r3), E("STAB BRASS",F_PLATE,r4),
    E("SWELL BRASS",F_HALL,r5), E("FAT HORN",F_ROOM,r6), E("MUTED HORN",F_ROOM,r7), E("SYNC BRASS",F_ROOM,r8),
    E("E-PIANO",F_J1,k1), E("CLAV",F_NONE,k2), E("ORGAN",F_ROOM,k3), E("SOFT KEYS",F_ROOM,k4),
    E("HARPSI",F_ROOM,k5), E("STAB KEYS",F_PING,k6), E("VIBES",F_PLATE,k7), E("POLY PLUCK",F_PINGH,k8),
    E("SYNC LEAD",F_PING,l1), E("SAW LEAD",F_PINGH,l2), E("SQUARE LEAD",F_PING,l3), E("SOFT LEAD",F_PLATE,l4),
    E("UNISON LEAD",F_PING,l5), E("DRIVE LEAD",F_ODP,l6), E("RESO LEAD",F_TAP,l7), E("XMOD LEAD",F_ROOM,l8),
    E("XMOD BELL",F_HALL,c1), E("GLASS BELL",F_PLATE,c2), E("TUBE BELL",F_HALL2,c3), E("MARIMBA",F_ROOM,c4),
    E("KALIMBA",F_ROOM,c5), E("MUSIC BOX",F_PLATE,c6), E("ICE PLUCK",F_PINGH,c7), E("ECHO PLUCK",F_PING,c8),
    E("RESO SWEEP",F_HALL,x1), E("NOISE WIND",F_HALL,x2), E("OCEAN",F_HALL2,x3), E("LOW XMOD",F_ROOM,x4),
    E("SIREN",F_ROOM,x5), E("LASER",F_PING,x6), E("ROBOT",F_PHH,x7), E("RISER",F_HALL2,x8),
};

// VCA LEVEL per preset, set from measured loudness of each sound (host/render_presets.cpp).
const uint8_t kLevel[64] = {
255,255,218,243,239,248,253,255,
172,132,243,203,125,194,180,131,
156,148,168,154,210,237,134,140,
178,201,155,209,169,172,145,179,
203,181,111,217,205,243,201,255,
193,250,184,228,137,124,189,165,
223,208,183,255,255,251,255,255,
179,255,255,146,146,219,158,207
};

}  // namespace

void applyFactory(Patch* bank, int count) {
    for (int i = 0; i < count && i < 64; i++) {
        initPatch(bank[i], kPresets[i].name);
        for (int k = 0; k < kPresets[i].n; k++) bank[i].v[kPresets[i].list[k].id] = kPresets[i].list[k].val;
        bank[i].v[P_VCA_LEVEL] = kLevel[i];
        applyFx(bank[i].fx, kPresets[i].fx);
    }
}

}  // namespace j8
