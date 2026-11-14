// Effect parameter model, following the JUPITER-08 Reference Manual's parameter lists
// (types, names, ranges). Ping-pong is an addition. Sounds are my own models.
#pragma once
#include <cstdint>
#include <cstddef>

namespace j8 {

enum MfxType : uint8_t {
    MFX_CHO1, MFX_CHO2, MFX_CHO3, MFX_DLY1, MFX_DLY2, MFX_DLY3, MFX_RDLY, MFX_OD, MFX_FUZZ,
    MFX_FATTEN, MFX_BITC, MFX_LOFI, MFX_PHA1, MFX_PHA2, MFX_FILT, MFX_PCH1, MFX_PCH2, MFX_COUNT
};

// Parameter positions in FxPatch::m, by effect type
enum { C1_MODE, C1_NOISE, C1_BAL, C1_LEV };
enum { C2_INT, C2_LO, C2_HI, C2_LEV };
enum { C3_MODE, C3_LO, C3_HI, C3_LEV };
enum { D1_SYNC, D1_TIME, D1_NOTE, D1_FB, D1_BAL, D1_LO, D1_HI, D1_LEV };
enum { D2_SYNC, D2_TIME, D2_NOTE, D2_FB, D2_DAMP, D2_PAN1, D2_PAN2, D2_LV1, D2_LV2, D2_LO, D2_HI, D2_BAL, D2_LEV };
enum { D3_LSYNC, D3_LTIME, D3_LNOTE, D3_RSYNC, D3_RTIME, D3_RNOTE, D3_FBMODE, D3_FB, D3_DAMP,
       D3_RSYNC2, D3_RATE, D3_RNOTE2, D3_DEPTH, D3_PHASE, D3_LO, D3_HI, D3_BAL, D3_LEV };
enum { RD_SYNC, RD_TIME, RD_NOTE, RD_FB, RD_BAL, RD_LO, RD_HI, RD_LEV };
enum { OD_DIST, OD_TONE, OD_LEV };
enum { FZ_DRIVE, FZ_TONE, FZ_LEV };
enum { FT_ODD, FT_EVEN, FT_LEV };
enum { BC_RATE, BC_BIT, BC_FILT, BC_LO, BC_HI, BC_LEV };
enum { LF_COMP, LF_LOFI, LF_FILT, LF_COFF, LF_LO, LF_HI, LF_BAL, LF_LEV };
enum { P1_SPEED, P1_DEPTH, P1_LO, P1_HI, P1_LEV };
enum { P2_STAGES, P2_MANUAL, P2_SYNC, P2_RATE, P2_NOTE, P2_DEPTH, P2_RESO, P2_MIX, P2_PAN, P2_LO, P2_HI, P2_LEV };
enum { SF_TYPE, SF_SLOPE, SF_CUT, SF_RES, SF_GAIN, SF_MOD, SF_WAVE, SF_SYNC, SF_RATE, SF_NOTE, SF_DEPTH, SF_ATK, SF_LEV };
enum { PS_CRS, PS_FINE, PS_SYNC, PS_TIME, PS_NOTE, PS_FB, PS_LO, PS_HI, PS_BAL, PS_LEV };
enum { V2_1CRS, V2_1FIN, V2_1SYNC, V2_1TIME, V2_1NOTE, V2_1FB, V2_1PAN, V2_1LEV,
       V2_2CRS, V2_2FIN, V2_2SYNC, V2_2TIME, V2_2NOTE, V2_2FB, V2_2PAN, V2_2LEV, V2_LO, V2_HI, V2_BAL, V2_LEV };

// Delay section
enum { DL_SEND, DL_MODE, DL_SYNC, DL_TIME, DL_NOTE, DL_FB, DL_DAMP, DL_PAN1, DL_PAN2, DL_LV1, DL_LV2, DL_LEV, DL_RSEND, DL_COUNT };
enum { DLM_2TAP, DLM_PING };

// Reverb section
enum { RV_SEND, RV_MODE, RV_TYPE, RV_PDLY, RV_TIME, RV_LEV,
       RS_LDAMP, RS_HDAMP, RS_TIME, RS_DENS, RS_PDLY, RS_MRATE, RS_MDEPTH, RS_PCH1, RS_PCH2, RS_FB1, RS_FB2, RS_LV1, RS_LV2, RS_LEV, RV_COUNT };
enum { RVM_STD, RVM_SHIMMER };
enum { RVT_ROOM1, RVT_ROOM2, RVT_HALL1, RVT_HALL2, RVT_PLATE };

constexpr int kMfxMaxParams = 20;

struct FxPatch {
    uint8_t mfxType;
    uint8_t mfxOn, dlyOn, revOn;
    int16_t m[kMfxMaxParams];
    int16_t d[DL_COUNT];
    int16_t r[RV_COUNT];
};

enum class FxKind : uint8_t { Num, OnOff, Labels, Ms, Pct, Db, Hz100, Pan, Cents, Semi, Sec10, Deg, Mult };

struct FxParamDef {
    const char* name;
    int16_t lo, hi, def;
    FxKind kind;
    const char* const* labels;
};

const char* mfxName(int type);        // full name, "JUNO-106 Chorus"
int mfxParamCount(int type);
const FxParamDef& mfxParam(int type, int i);
const FxParamDef& delayParam(int i);
const FxParamDef& reverbParam(int i);
void formatFx(const FxParamDef& d, int value, char* out, size_t n);
void fxLoadMfxDefaults(FxPatch& f, int type);   // keep type, reset its parameters
void fxDefaults(FxPatch& f);                    // everything off, parameters at defaults
float noteBeats(int noteIndex);

}  // namespace j8
